#!/usr/bin/env python3
"""Stage boot-disk files from a YAML apps manifest."""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    import yaml
except ImportError as exc:  # pragma: no cover - environment check
    raise SystemExit("PyYAML is required to read boot-disk manifests") from exc


@dataclass(frozen=True)
class AppEntry:
    src: Path
    name: str
    required: bool = True


def resolve_path(path: str, repo_root: Path) -> Path:
    expanded = os.path.expanduser(os.path.expandvars(path.strip()))
    candidate = Path(expanded)
    if not candidate.is_absolute():
        candidate = repo_root / candidate
    return candidate.resolve()


def load_manifest(manifest: Path, repo_root: Path) -> list[AppEntry]:
    data = yaml.safe_load(manifest.read_text())
    if not isinstance(data, dict):
        raise ValueError(f"{manifest}: expected a YAML mapping at top level")

    raw_apps = data.get("apps")
    if raw_apps is None:
        return []
    if not isinstance(raw_apps, list):
        raise ValueError(f"{manifest}: 'apps' must be a list")

    entries: list[AppEntry] = []
    for index, item in enumerate(raw_apps):
        if not isinstance(item, dict):
            raise ValueError(f"{manifest}: apps[{index}] must be a mapping")
        if "src" not in item or "name" not in item:
            raise ValueError(f"{manifest}: apps[{index}] requires 'src' and 'name'")

        entries.append(
            AppEntry(
                src=resolve_path(str(item["src"]), repo_root),
                name=str(item["name"]),
                required=bool(item.get("required", True)),
            )
        )

    return entries


def validate_name(name: str) -> str:
    normalized = name.replace("\\", "/").strip("/")
    if not normalized or normalized.startswith("../") or "/../" in normalized:
        raise ValueError(f"Invalid disk name: {name!r}")
    return normalized


def stage_manifest(manifest: Path, repo_root: Path, stage: Path) -> int:
    entries = load_manifest(manifest, repo_root)
    copied = 0
    skipped: list[str] = []

    for entry in entries:
        dest_name = validate_name(entry.name)
        dest = stage / dest_name
        if not entry.src.is_file():
            if entry.required:
                raise FileNotFoundError(f"Required boot-disk file not found: {entry.src}")
            skipped.append(f"{dest_name} ({entry.src})")
            continue

        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(entry.src, dest)
        copied += 1

    if skipped:
        print("Skipping optional boot-disk file(s) not found:", file=sys.stderr)
        for item in skipped:
            print(f"  {item}", file=sys.stderr)

    if copied == 0:
        raise ValueError(f"No boot-disk files were available to copy: {manifest}")

    return copied


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, help="YAML manifest to stage")
    parser.add_argument("--stage", required=True, help="Output staging directory")
    parser.add_argument(
        "--repo-root",
        default=".",
        help="Root used to resolve relative paths in the manifest",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    repo_root = Path(args.repo_root).resolve()
    manifest = resolve_path(args.manifest, repo_root)
    stage = resolve_path(args.stage, repo_root)

    try:
        copied = stage_manifest(manifest, repo_root, stage)
    except (FileNotFoundError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    print(f"Staged {copied} boot-disk file(s) from {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
