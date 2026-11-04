#!/usr/bin/env python3
"""Create the compact DATA.PAK embedded in the PS2 USB executable."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

MAGIC = b"GPAK"
VERSION = 1
ENTRY_SIZE = 64
HEADER_SIZE = 16


def collect(resource_dir: Path) -> list[Path]:
    files = []
    for path in sorted(resource_dir.rglob("*")):
        if not path.is_file() or path.name == "report.json":
            continue
        if path.suffix.lower() not in {".mesh", ".skeleton", ".anim", ".material", ".tm2", ".tim2", ".raw"}:
            continue
        files.append(path)
    if not files:
        raise ValueError("no final runtime resources found")
    if len({path.name for path in files}) != len(files):
        raise ValueError("DATA.PAK requires unique resource basenames")
    return files


def build(resource_dir: Path, output: Path) -> None:
    files = collect(resource_dir)
    index_offset = HEADER_SIZE
    data_offset = index_offset + ENTRY_SIZE * len(files)
    entries = bytearray()
    payload = bytearray()
    offset = data_offset
    for path in files:
        data = path.read_bytes()
        encoded = path.name.encode("ascii")
        if len(encoded) >= 48:
            raise ValueError("resource basename is too long: " + path.name)
        entries.extend(encoded.ljust(48, b"\0"))
        entries.extend(struct.pack("<II", offset, len(data)))
        entries.extend(b"\0" * 8)
        payload.extend(data)
        offset += len(data)
    header = struct.pack("<4sIII", MAGIC, VERSION, len(files), index_offset)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + entries + payload)
    print(f"Wrote {output} ({output.stat().st_size} bytes, {len(files)} resources)")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--resource-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build(args.resource_dir, args.output)


if __name__ == "__main__":
    main()
