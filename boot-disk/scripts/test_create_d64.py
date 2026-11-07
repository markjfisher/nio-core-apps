#!/usr/bin/env python3
"""Check create_d64's output against the public disk format, by reading the bytes back."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from create_d64 import D64, sectors_on_track  # noqa: E402

HEADER_OFFSET = 0x16500


def offset(track: int, sector: int) -> int:
    return (sum(sectors_on_track(t) for t in range(1, track)) + sector) * 256


def read_chain(image: bytes, track: int, sector: int) -> bytes:
    out = bytearray()
    for _ in range(683):
        block = image[offset(track, sector) : offset(track, sector) + 256]
        if block[0] == 0:
            return bytes(out + block[2 : block[1] + 1])
        out += block[2:]
        track, sector = block[0], block[1]
    raise AssertionError("the chain never ended")


class CreateD64Test(unittest.TestCase):
    def test_blank_disk_has_664_blocks_free_and_a_named_header(self) -> None:
        image = D64("HELLO WORLD", "AB").image()
        self.assertEqual(len(image), 174848)
        header = image[HEADER_OFFSET : HEADER_OFFSET + 256]
        self.assertEqual(header[0:3], bytes([18, 1, ord("A")]))
        self.assertEqual(header[0x90:0xA0], b"HELLO WORLD" + bytes([0xA0]) * 5)
        self.assertEqual(header[0xA2:0xA7], b"AB\xa02A")
        free = sum(header[4 + (t - 1) * 4] for t in range(1, 36) if t != 18)
        self.assertEqual(free, 664)

    def test_each_file_size_reads_back_through_its_chain(self) -> None:
        for size in (1, 253, 254, 255, 508, 5000, 39761):
            with self.subTest(size=size):
                disk = D64("D", "ID")
                payload = bytes((i * 7 + 1) & 0xFF for i in range(size))
                disk.add_file("FILE", payload)
                image = disk.image()
                entry = image[offset(18, 1) : offset(18, 1) + 32]
                self.assertEqual(entry[2], 0x82)
                self.assertEqual(read_chain(image, entry[3], entry[4]), payload)
                self.assertEqual(entry[30] | entry[31] << 8, (size + 253) // 254)

    def test_bam_counts_what_the_files_used(self) -> None:
        disk = D64("D", "ID")
        disk.add_file("FILE", bytes(5000))
        image = disk.image()
        header = image[HEADER_OFFSET : HEADER_OFFSET + 256]
        free = sum(header[4 + (t - 1) * 4] for t in range(1, 36) if t != 18)
        self.assertEqual(free, 664 - 20)

    def test_directory_continues_into_another_sector_after_eight_files(self) -> None:
        disk = D64("D", "ID")
        for number in range(9):
            disk.add_file(f"F{number}", bytes([number]))
        image = disk.image()
        first = image[offset(18, 1) : offset(18, 1) + 256]
        self.assertEqual(first[0], 18)
        second = image[offset(18, first[1]) : offset(18, first[1]) + 256]
        self.assertEqual(second[0:2], bytes([0, 0xFF]))
        self.assertEqual(second[5:7], b"F8")

    def test_names_become_capitals_and_are_bounded(self) -> None:
        disk = D64("D", "ID")
        disk.add_file("config", b"x")
        image = disk.image()
        self.assertEqual(image[offset(18, 1) + 5 : offset(18, 1) + 11], b"CONFIG")
        with self.assertRaises(ValueError):
            disk.add_file("A" * 17, b"x")
        with self.assertRaises(ValueError):
            disk.add_file("", b"x")
        with self.assertRaises(ValueError):
            disk.add_file("EMPTY", b"")


if __name__ == "__main__":
    unittest.main()
