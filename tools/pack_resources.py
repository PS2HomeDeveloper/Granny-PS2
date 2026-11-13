#!/usr/bin/env python3
"""Build an external, path-preserving GPAK v2 resource archive."""
from __future__ import annotations

import argparse
import os
import struct
import zlib
from pathlib import Path

MAGIC = b"GPAK"
VERSION = 2
HEADER = struct.Struct("<4sIIIII")  # magic, version, count, index offset, entry size, file size
PATH_BYTES = 256
ENTRY = struct.Struct(f"<{PATH_BYTES}sIIII")  # UTF-8 path, offset, size, CRC32, reserved
MAX_U32 = 0xFFFFFFFF


def collect(resource_dir: Path) -> list[tuple[str, Path]]:
    root = resource_dir.resolve(strict=True)
    if not root.is_dir():
        raise ValueError(f"resource root is not a directory: {root}")
    files: list[tuple[str, Path]] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        resolved = path.resolve(strict=True)
        if root not in resolved.parents:
            raise ValueError(f"resource symlink escapes resource root: {path}")
        relative = path.relative_to(root).as_posix()
        encoded = relative.encode("utf-8")
        if not encoded or len(encoded) >= PATH_BYTES:
            raise ValueError(f"resource path is too long for GPAK v2: {relative}")
        if relative.startswith("/") or "\\" in relative or any(part in ("", ".", "..") for part in relative.split("/")):
            raise ValueError(f"unsafe resource path: {relative}")
        files.append((relative, path))
    if not files:
        raise ValueError(f"no files found under {root}")
    paths = [name for name, _ in files]
    if len(paths) != len(set(paths)):
        raise ValueError("duplicate relative paths found in resources")
    return files


def build(resource_dir: Path, output: Path) -> None:
    files = collect(resource_dir)
    index_offset = HEADER.size
    data_offset = index_offset + len(files) * ENTRY.size
    if data_offset > MAX_U32:
        raise ValueError("GPAK index exceeds the 32-bit archive limit")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    offset = data_offset
    try:
        with temporary.open("wb") as archive:
            archive.write(b"\0" * data_offset)
            index = bytearray()
            total_payload = 0
            for name, path in files:
                data = path.read_bytes()
                if len(data) > MAX_U32 or offset + len(data) > MAX_U32:
                    raise ValueError(f"resource or archive exceeds 4 GiB: {name}")
                encoded = name.encode("utf-8")
                index.extend(ENTRY.pack(encoded.ljust(PATH_BYTES, b"\0"), offset,
                                         len(data), zlib.crc32(data) & MAX_U32, 0))
                archive.seek(offset)
                archive.write(data)
                offset += len(data)
                total_payload += len(data)
            archive.seek(0)
            archive.write(HEADER.pack(MAGIC, VERSION, len(files), index_offset,
                                      ENTRY.size, offset))
            archive.seek(index_offset)
            archive.write(index)
            archive.flush()
            os.fsync(archive.fileno())
        temporary.replace(output)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    print(f"Wrote {output} ({offset:,} bytes, {len(files):,} files, "
          f"{total_payload:,} payload bytes, {ENTRY.size}-byte index entries)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resource-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build(args.resource_dir, args.output)


if __name__ == "__main__":
    main()
