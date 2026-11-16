#!/usr/bin/env python3
"""Build an external GPAK v2 archive and a compact main-scene map index."""
from __future__ import annotations

import argparse
import json
import math
import os
import struct
import zlib
from pathlib import Path

MAGIC = b"GPAK"
VERSION = 2
HEADER = struct.Struct("<4sIIIII")  # magic, version, count, index offset, entry size, file size
PATH_BYTES = 256
ENTRY = struct.Struct(f"<{PATH_BYTES}sIIII")  # UTF-8 path, offset, size, CRC32, reserved
MAP_INDEX_PATH = "3d/maps/MAPIDX.BIN"
MAP_INDEX_HEADER = struct.Struct("<4sIIII")  # magic, version, count, record size, total size
MAP_INDEX_SAMPLE_COUNT = 16
MAP_INDEX_RECORD = struct.Struct(f"<128s6f4I{MAP_INDEX_SAMPLE_COUNT * 3}f")
GMAP_HEADER_SIZE = 96
GMAP_VERTEX_SIZE = 32
PLAYER_START_SAMPLE = (13.75, 21.027, 7.16)
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
        # Exporter metadata and audit reports are editor-side inputs, not
        # runtime assets. GMIX is generated separately from map sidecars.
        if path.suffix.lower() in {".json", ".txt", ".tsv"}:
            continue
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


def _read_map_samples(path: Path) -> list[tuple[float, float, float]]:
    data = path.read_bytes()
    if len(data) < GMAP_HEADER_SIZE or data[:4] != b"GMAP":
        raise ValueError("missing or truncated GMAP header")
    version = struct.unpack_from("<I", data, 4)[0]
    declared_size = struct.unpack_from("<I", data, 8)[0]
    count = struct.unpack_from("<I", data, 12)[0]
    vertex_offset = struct.unpack_from("<I", data, 28)[0]
    if version != 2 or declared_size != len(data) or not count:
        raise ValueError("unsupported GMAP version, size mismatch, or empty geometry")
    if vertex_offset < GMAP_HEADER_SIZE or vertex_offset % 16 or vertex_offset > len(data):
        raise ValueError("invalid GMAP vertex section offset")
    if count > (len(data) - vertex_offset) // GMAP_VERTEX_SIZE:
        raise ValueError("GMAP vertex section exceeds file size")

    points: list[tuple[float, float, float]] = []
    sx = sy = sz = 0.0
    for i in range(count):
        point = struct.unpack_from("<3f", data, vertex_offset + i * GMAP_VERTEX_SIZE)
        if not all(math.isfinite(value) for value in point):
            raise ValueError(f"non-finite GMAP vertex at index {i}")
        points.append(point)
        sx += point[0]
        sy += point[1]
        sz += point[2]

    # Reserve one sample for the original Unity PlayerStartPos. The remaining
    # farthest-point samples span each chunk better than its broad AABB.
    centroid = (sx / count, sy / count, sz / count)
    start_index = min(
        range(count),
        key=lambda i: sum((points[i][k] - PLAYER_START_SAMPLE[k]) ** 2
                          for k in range(3)),
    )
    selected = [points[start_index]]
    if len(selected) < min(MAP_INDEX_SAMPLE_COUNT, count):
        selected.append(centroid)
    nearest_d2 = [
        min(sum((point[k] - chosen[k]) ** 2 for k in range(3))
            for chosen in selected)
        for point in points
    ]
    while len(selected) < min(MAP_INDEX_SAMPLE_COUNT, count):
        best_index = max(range(count), key=nearest_d2.__getitem__)
        chosen = points[best_index]
        selected.append(chosen)
        for i, point in enumerate(points):
            d2 = sum((point[k] - chosen[k]) ** 2 for k in range(3))
            if d2 < nearest_d2[i]:
                nearest_d2[i] = d2
    while len(selected) < MAP_INDEX_SAMPLE_COUNT:
        selected.append(selected[-1])
    return selected


def build_map_index(resource_dir: Path) -> bytes | None:
    """Create the boot-scene catalog from JSON sidecars and actual GMAP vertices."""
    root = resource_dir.resolve(strict=True)
    maps_root = root / "3d" / "maps"
    scene_root = maps_root / "Scene"
    if not scene_root.is_dir():
        return None

    records: list[bytes] = []
    for metadata in sorted(scene_root.rglob("*.json"), key=lambda p: p.as_posix()):
        map_path = metadata.with_suffix(".map")
        if not map_path.is_file():
            continue
        try:
            payload = json.loads(metadata.read_text(encoding="utf-8"))
            bounds = payload["bounds"]
            if not isinstance(bounds, list) or len(bounds) != 6:
                raise ValueError("bounds must contain six numbers")
            values = tuple(float(value) for value in bounds)
            if not all(math.isfinite(value) for value in values):
                raise ValueError("bounds contain non-finite values")
            if any(values[i] > values[i + 3] for i in range(3)):
                raise ValueError("bounds minimum exceeds maximum")
            relative = map_path.relative_to(root).as_posix()
            encoded = relative.encode("utf-8")
            if len(encoded) >= 128:
                raise ValueError(f"map path is too long: {relative}")
            vertices = int(payload.get("vertices", 0))
            triangles = int(payload.get("triangles", 0))
            colliders = int(payload.get("colliders", 0))
            if any(value < 0 or value > MAX_U32 for value in (vertices, triangles, colliders)):
                raise ValueError("map counts exceed 32-bit range")
            samples = _read_map_samples(map_path)
            records.append(MAP_INDEX_RECORD.pack(
                encoded.ljust(128, b"\0"), *values,
                vertices, triangles, colliders, 0,
                *(component for sample in samples for component in sample),
            ))
        except (OSError, UnicodeError, json.JSONDecodeError, KeyError, TypeError, ValueError, struct.error) as exc:
            raise ValueError(f"cannot build map index from {metadata.relative_to(root)}: {exc}") from exc

    if not records:
        return None
    record_blob = b"".join(records)
    total_size = MAP_INDEX_HEADER.size + len(record_blob)
    return MAP_INDEX_HEADER.pack(
        b"GMIX", 1, len(records), MAP_INDEX_RECORD.size, total_size
    ) + record_blob


def build(resource_dir: Path, output: Path) -> None:
    root = resource_dir.resolve(strict=True)
    output_resolved = output.resolve()
    if output_resolved == root or root in output_resolved.parents:
        raise ValueError("DATA.PAK output must be outside the resources tree")
    files: list[tuple[str, Path | bytes]] = list(collect(root))
    map_index = build_map_index(root)
    if map_index is not None:
        if any(name == MAP_INDEX_PATH for name, _ in files):
            raise ValueError(f"generated map index path already exists: {MAP_INDEX_PATH}")
        files.append((MAP_INDEX_PATH, map_index))
        files.sort(key=lambda item: item[0])
    if len(files) > MAX_ENTRIES:
        raise ValueError(f"too many GPAK entries after adding map index: {len(files)} > {MAX_ENTRIES}")

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
            for name, source in files:
                data = source.read_bytes() if isinstance(source, Path) else source
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
          f"{total_payload:,} payload bytes, {ENTRY.size}-byte index entries, "
          f"{MAP_INDEX_RECORD.size}-byte map records)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resource-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build(args.resource_dir, args.output)


if __name__ == "__main__":
    main()
