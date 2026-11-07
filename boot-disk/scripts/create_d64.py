#!/usr/bin/env python3
"""Create a 35-track Commodore 1541 disk image (.d64) from a directory of files.

Each file becomes one PRG (or, with a .seq suffix, SEQ) directory entry. Files are
added in the order given by --files, or sorted by name; the first is what
LOAD"*",8 finds. Written from the public disk format, standard library only.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

SECTOR_BYTES = 256
TRACKS = 35
DIR_TRACK = 18
BAM_SECTOR = 0
FIRST_DIR_SECTOR = 1
DIR_INTERLEAVE = 3
FILE_INTERLEAVE = 10
ENTRIES_PER_SECTOR = 8
SHIFTED_SPACE = 0xA0
TYPE_CLOSED = 0x80
TYPE_SEQ = 1
TYPE_PRG = 2


def sectors_on_track(track: int) -> int:
    if 1 <= track <= 17:
        return 21
    if 18 <= track <= 24:
        return 19
    if 25 <= track <= 30:
        return 18
    if 31 <= track <= 40:
        return 17
    raise ValueError(f"track {track} is not on a 1541 disk")


def petscii_name(name: str, limit: int) -> bytes:
    """A name as PETSCII in the machine's power-on case: letters are capitals."""
    out = bytearray()
    for ch in name.upper():
        code = ord(ch)
        if code < 0x20 or code > 0x5F:
            raise ValueError(f"{name!r}: {ch!r} has no plain PETSCII form")
        out.append(code)
    if not out or len(out) > limit:
        raise ValueError(f"{name!r}: a name must be 1-{limit} characters")
    return bytes(out)


def padded(name: bytes, length: int) -> bytes:
    return name + bytes([SHIFTED_SPACE]) * (length - len(name))


class D64:
    def __init__(self, title: str, disk_id: str) -> None:
        self.data = bytearray(sum(sectors_on_track(t) for t in range(1, TRACKS + 1)) * SECTOR_BYTES)
        self.free = {t: set(range(sectors_on_track(t))) for t in range(1, TRACKS + 1)}
        self.entries = 0
        self.title = petscii_name(title, 16)
        self.disk_id = petscii_name(disk_id, 2)
        if len(self.disk_id) != 2:
            raise ValueError("a disk ID is two characters")
        self.free[DIR_TRACK].discard(BAM_SECTOR)
        self.free[DIR_TRACK].discard(FIRST_DIR_SECTOR)
        self.dir_sector = FIRST_DIR_SECTOR
        self._sector_view(DIR_TRACK, FIRST_DIR_SECTOR)[1] = 0xFF

    def _offset(self, track: int, sector: int) -> int:
        block = sum(sectors_on_track(t) for t in range(1, track)) + sector
        return block * SECTOR_BYTES

    def _sector_view(self, track: int, sector: int) -> memoryview:
        offset = self._offset(track, sector)
        return memoryview(self.data)[offset : offset + SECTOR_BYTES]

    def _take_sector(self, previous: tuple[int, int] | None) -> tuple[int, int]:
        """The next free sector, working outward from the directory track."""
        if previous is not None:
            track, sector = previous
            count = sectors_on_track(track)
            if self.free[track]:
                for step in range(count):
                    candidate = (sector + FILE_INTERLEAVE + step) % count
                    if candidate in self.free[track]:
                        self.free[track].discard(candidate)
                        return track, candidate
        for distance in range(1, TRACKS):
            for track in (DIR_TRACK - distance, DIR_TRACK + distance):
                if 1 <= track <= TRACKS and self.free[track]:
                    candidate = min(self.free[track])
                    self.free[track].discard(candidate)
                    return track, candidate
        raise ValueError("disk full")

    def _next_dir_slot(self) -> memoryview:
        slot = self.entries % ENTRIES_PER_SECTOR
        if self.entries and slot == 0:
            count = sectors_on_track(DIR_TRACK)
            for step in range(count):
                candidate = (self.dir_sector + DIR_INTERLEAVE + step) % count
                if candidate in self.free[DIR_TRACK]:
                    break
            else:
                raise ValueError("directory full")
            self.free[DIR_TRACK].discard(candidate)
            current = self._sector_view(DIR_TRACK, self.dir_sector)
            current[0] = DIR_TRACK
            current[1] = candidate
            self._sector_view(DIR_TRACK, candidate)[1] = 0xFF
            self.dir_sector = candidate
        self.entries += 1
        start = slot * 32
        return self._sector_view(DIR_TRACK, self.dir_sector)[start : start + 32]

    def add_file(self, name: str, payload: bytes, file_type: int = TYPE_PRG) -> None:
        name_bytes = petscii_name(name, 16)
        if not payload:
            raise ValueError(f"{name!r}: a file needs at least one byte")

        first = None
        previous = None
        previous_view = None
        blocks = 0
        for start in range(0, len(payload), SECTOR_BYTES - 2):
            chunk = payload[start : start + SECTOR_BYTES - 2]
            location = self._take_sector(previous)
            view = self._sector_view(*location)
            view[2 : 2 + len(chunk)] = chunk
            if first is None:
                first = location
            if previous_view is not None:
                previous_view[0] = location[0]
                previous_view[1] = location[1]
            # A last sector: track 0, and sector holds the index of its last byte.
            view[0] = 0
            view[1] = len(chunk) + 1
            previous = location
            previous_view = view
            blocks += 1

        entry = self._next_dir_slot()
        entry[2] = TYPE_CLOSED | file_type
        entry[3] = first[0]
        entry[4] = first[1]
        entry[5:21] = padded(name_bytes, 16)
        entry[30] = blocks & 0xFF
        entry[31] = blocks >> 8

    def _write_bam(self) -> None:
        bam = self._sector_view(DIR_TRACK, BAM_SECTOR)
        bam[0] = DIR_TRACK
        bam[1] = FIRST_DIR_SECTOR
        bam[2] = ord("A")
        for track in range(1, TRACKS + 1):
            bitmap = 0
            for sector in self.free[track]:
                bitmap |= 1 << sector
            at = 4 + (track - 1) * 4
            bam[at] = len(self.free[track])
            bam[at + 1] = bitmap & 0xFF
            bam[at + 2] = (bitmap >> 8) & 0xFF
            bam[at + 3] = (bitmap >> 16) & 0xFF
        bam[0x90:0xA0] = padded(self.title, 16)
        bam[0xA0] = SHIFTED_SPACE
        bam[0xA1] = SHIFTED_SPACE
        bam[0xA2:0xA4] = self.disk_id
        bam[0xA4] = SHIFTED_SPACE
        bam[0xA5] = ord("2")
        bam[0xA6] = ord("A")
        bam[0xA7:0xAB] = bytes([SHIFTED_SPACE]) * 4

    def image(self) -> bytes:
        self._write_bam()
        return bytes(self.data)


def build(input_dir: Path, names: list[str], title: str, disk_id: str) -> bytes:
    disk = D64(title, disk_id)
    for name in names:
        source = input_dir / name
        stem, suffix = source.stem, source.suffix.lower()
        file_type = TYPE_SEQ if suffix == ".seq" else TYPE_PRG
        disk.add_file(stem, source.read_bytes(), file_type)
    return disk.image()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-i", "--input", required=True, type=Path, help="directory holding the files")
    parser.add_argument("-o", "--output", required=True, type=Path, help="the .d64 to write")
    parser.add_argument("-t", "--title", default="FUJINET NIO", help="disk name")
    parser.add_argument("--id", default="FN", help="two-character disk ID")
    parser.add_argument("--files", nargs="*", help="files in directory order (default: sorted by name)")
    args = parser.parse_args()

    names = args.files or sorted(p.name for p in args.input.iterdir() if p.is_file())
    try:
        image = build(args.input, names, args.title, args.id)
    except (OSError, ValueError) as error:
        print(f"create_d64: {error}", file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(f"{args.output}: {len(names)} file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
