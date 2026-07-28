#!/usr/bin/env python3
"""
create_msdos_img.py - Create MS-DOS FAT disk images from a folder.

The output is a raw FAT12/FAT16 image suitable for fujinet-nio DiskService,
QEMU tooling, and MS-DOS 6.x. Sidecar .inf files are ignored so the same
staging folders can coexist with BBC disk-image workflows.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


FAT83_RE = re.compile(r"^[A-Z0-9!#$%&'()@^_`{}~-]{1,8}(?:\.[A-Z0-9!#$%&'()@^_`{}~-]{1,3})?$")
LABEL_RE = re.compile(r"^[A-Z0-9!#$%&'()@^_`{}~-]{1,11}$")


@dataclass(frozen=True)
class ImageEntry:
    source: Path
    image_path: str
    is_dir: bool


def run_checked(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, check=True, text=True, capture_output=True)


def require_tools() -> None:
    missing = [tool for tool in ("mkfs.fat", "mcopy", "mdir", "mmd") if shutil.which(tool) is None]
    if missing:
        raise RuntimeError(
            "Missing required tool(s): "
            + ", ".join(missing)
            + " (install dosfstools and mtools)"
        )


def is_ignored(path: Path) -> bool:
    name = path.name
    return (
        name.startswith(".")
        or name.lower().endswith(".inf")
        or name.endswith("~")
    )


def validate_label(label: str) -> str:
    upper = label.upper()
    if not LABEL_RE.match(upper):
        raise ValueError(f"MS-DOS volume label must be 1-11 FAT characters: {label!r}")
    return upper


def validate_fat83(name: str, source: Path) -> str:
    upper = name.upper()
    if not FAT83_RE.match(upper):
        raise ValueError(
            f"{source}: MS-DOS 6.x image names must be 8.3 compatible; got {name!r}"
        )
    return upper


def image_join(parent: str, child: str) -> str:
    return f"{parent}/{child}" if parent else child


def collect_entries(input_dir: Path) -> list[ImageEntry]:
    entries: list[ImageEntry] = []

    def walk(host_dir: Path, image_dir: str) -> None:
        children = sorted(
            (p for p in host_dir.iterdir() if not is_ignored(p)),
            key=lambda p: (not p.is_dir(), p.name.upper()),
        )
        for child in children:
            fat_name = validate_fat83(child.name, child)
            child_image_path = image_join(image_dir, fat_name)
            if child.is_dir():
                entries.append(ImageEntry(child, child_image_path, True))
                walk(child, child_image_path)
            elif child.is_file():
                entries.append(ImageEntry(child, child_image_path, False))

    walk(input_dir, "")
    return entries


def make_image_kb(output: Path, size_kb: int, fat_bits: int, label: str, cluster_sectors: int | None) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()

    with output.open("wb") as handle:
        handle.truncate(size_kb * 1024)

    args = ["mkfs.fat", "-F", str(fat_bits), "-n", label]
    if cluster_sectors is not None:
        args.extend(["-s", str(cluster_sectors)])
    args.append(str(output))
    run_checked(args)


def mtools_path(image_path: str) -> str:
    return "::/" + image_path


def populate_image(image: Path, entries: list[ImageEntry]) -> None:
    for entry in entries:
        if entry.is_dir:
            run_checked(["mmd", "-i", str(image), mtools_path(entry.image_path)])
        else:
            run_checked([
                "mcopy",
                "-o",
                "-i",
                str(image),
                str(entry.source),
                mtools_path(entry.image_path),
            ])


def list_image(image: Path) -> None:
    result = run_checked(["mdir", "-i", str(image), "-/"])
    print(result.stdout.rstrip())


def create_msdos_img(
    *,
    input_dir: Path,
    output: Path,
    size_kb: int,
    fat_bits: int,
    label: str,
    cluster_sectors: int | None,
    show_listing: bool,
) -> None:
    require_tools()

    if not input_dir.is_dir():
        raise FileNotFoundError(f"Input directory not found: {input_dir}")
    if size_kb <= 0:
        raise ValueError("Image size must be positive")
    if fat_bits not in (12, 16):
        raise ValueError("FAT size must be 12 or 16")
    if cluster_sectors is not None and cluster_sectors not in (1, 2, 4, 8, 16, 32, 64):
        raise ValueError("Cluster sectors must be one of: 1, 2, 4, 8, 16, 32, 64")

    label = validate_label(label)
    entries = collect_entries(input_dir)

    print("Creating MS-DOS FAT disk image...")
    print(f"  Input:  {input_dir}")
    print(f"  Output: {output}")
    print(f"  Size:   {size_kb} KiB")
    print(f"  FAT:    FAT{fat_bits}")
    if cluster_sectors is not None:
        print(f"  Cluster:{cluster_sectors} sector(s)")
    print(f"  Label:  {label}")
    print(f"  Files:  {sum(1 for e in entries if not e.is_dir)}")
    print(f"  Dirs:   {sum(1 for e in entries if e.is_dir)}")

    make_image_kb(output, size_kb, fat_bits, label, cluster_sectors)
    populate_image(output, entries)

    if show_listing:
        print()
        list_image(output)

    print(f"\nMS-DOS disk image created: {output}")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create an MS-DOS FAT disk image from a folder.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Examples:
  msdos/scripts/create_msdos_img.py -i msdos/bin -o build/msdos-apps.img
  msdos/scripts/create_msdos_img.py -i stage -o ~/8bit/TNFS/msdos/apps.img -p 1440 -l NIOAPPS
  msdos/scripts/create_msdos_img.py -i big-stage -o ~/8bit/TNFS/msdos/big.img -s 16 -f 16
  msdos/scripts/create_msdos_img.py -i big-stage -o ~/8bit/TNFS/msdos/big.img -s 16 -f 16 -c 8

The image is a raw FAT volume. In MS-DOS, after FHOST points at your TNFS
server, use FIN/FMOUNT with the image path, for example:
  FIN 0 apps.img
  FMOUNT 0 D: RW
""",
    )
    parser.add_argument("-i", "--input", required=True, type=Path, help="Input folder")
    parser.add_argument("-o", "--output", required=True, type=Path, help="Output .img file")
    parser.add_argument("-s", "--size-mb", type=int, default=None, help="Image size in MiB")
    parser.add_argument("-p", "--preset", choices=("1440", "2880"), default="1440",
                        help="Floppy-style image size in KiB when --size-mb is omitted (default: 1440)")
    parser.add_argument("-f", "--fat", type=int, choices=(12, 16), default=None,
                        help="FAT type (default: FAT12 for presets, FAT16 with --size-mb)")
    parser.add_argument("-c", "--cluster-sectors", type=int, default=None,
                        help="Sectors per cluster for mkfs.fat -s (default: 8 for FAT16 size images, mkfs default otherwise)")
    parser.add_argument("-l", "--label", default="NIOAPPS", help="MS-DOS volume label (default: NIOAPPS)")
    parser.add_argument("--no-list", action="store_true", help="Do not print mdir listing after creation")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.size_mb is None:
        size_kb = int(args.preset)
        fat_bits = args.fat or 12
    else:
        size_kb = args.size_mb * 1024
        fat_bits = args.fat or 16
    cluster_sectors = args.cluster_sectors
    if cluster_sectors is None and args.size_mb is not None and fat_bits == 16:
        cluster_sectors = 8

    try:
        create_msdos_img(
            input_dir=args.input.resolve(),
            output=args.output.expanduser().resolve(),
            size_kb=size_kb,
            fat_bits=fat_bits,
            label=args.label,
            cluster_sectors=cluster_sectors,
            show_listing=not args.no_list,
        )
    except (FileNotFoundError, RuntimeError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        if isinstance(exc, subprocess.CalledProcessError):
            if exc.stdout:
                print(exc.stdout, file=sys.stderr)
            if exc.stderr:
                print(exc.stderr, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
