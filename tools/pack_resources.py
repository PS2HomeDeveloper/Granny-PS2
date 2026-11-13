#!/usr/bin/env python3
"""Build an external GPAK v2 archive from every regular file under resources."""
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
MAX_ENTRIES = 32768
MAX_ARCHIVE_BYTES = 0x7FFFFFFF  # Matches GPAK_MAX_FILE_SIZE in the PS2 runtime.


def collect(resource_dir: Path) -> list[tuple[str, Path]]:
    root = resource_dir.resolve(strict=True)
    if not root.is_dir():
        raise ValueError(f"resource root is not a directory: {root}")

    files: list[tuple[str, Path]] = []
    seen: set[str] = set()
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        resolved = path.resolve(strict=True)
        if resolved == root or root not in resolved.parents:
            raise ValueError(f"resource resolves outside resource root: {path}")
        relative = path.relative_to(root).as_posix()
        if not relative or relative.startswith("/") or "\\" in relative or ":" in relative:
            raise ValueError(f"unsafe resource path: {relative!r}")
        if any(part in ("", ".", "..") for part in relative.split("/")):
            raise ValueError(f"unsafe resource path: {relative!r}")
        encoded = relative.encode("utf-8")
        if len(encoded) >= PATH_BYTES:
            raise ValueError(f"resource path is too long for GPAK v2: {relative}")
        if relative in seen:
            raise ValueError(f"duplicate resource path: {relative}")
        seen.add(relative)
        files.append((relative, path))

    if not files:
        raise ValueError(f"resource root contains no files: {root}")
    if len(files) > MAX_ENTRIES:
        raise ValueError(f"too many resource files for GPAK v2: {len(files)} > {MAX_ENTRIES}")
    return sorted(files, key=lambda item: item[0])


def build(resource_dir: Path, output: Path) -> None:
    root = resource_dir.resolve(strict=True)
    output_resolved = output.resolve()
    if output_resolved == root or root in output_resolved.parents:
        raise ValueError("DATA.PAK output must be outside the resources tree")
    files = collect(root)
    index_offset = HEADER.size
    data_offset = index_offset + len(files) * ENTRY.size
    if data_offset > MAX_ARCHIVE_BYTES:
        raise ValueError("GPAK index exceeds the PS2 runtime package-size limit")

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
                if len(data) > MAX_U32 or offset + len(data) > MAX_ARCHIVE_BYTES:
                    raise ValueError(f"resource or archive exceeds the PS2 GPAK limit: {name}")
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
