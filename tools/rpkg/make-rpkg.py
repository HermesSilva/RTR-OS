#!/usr/bin/env python3
"""RTR-OS - builds an application package (.rpkg).

A package is an uncompressed container of files that the system unpacks
into the root of the SD card. Layout, little-endian:

    magic      8 bytes   "RPKG1\\0\\0\\0"
    count      uint32    number of entries
    reserved   uint32
    entries    count * { name char[16] (8.3, NUL padded), offset uint32, size uint32 }
    data       each entry's bytes at its offset, 4-byte aligned

Usage: make-rpkg.py <package.rpkg> NAME.EXT=path [NAME.EXT=path ...]
"""
import struct
import sys

MAGIC = b"RPKG1\0\0\0"
ENTRY = struct.Struct("<16sII")


def short_name(name):
    base, _, ext = name.upper().partition(".")
    if not base or len(base) > 8 or len(ext) > 3 or not all(c.isalnum() or c in "_-" for c in base + ext):
        raise SystemExit(f"not an 8.3 name: {name}")
    return f"{base}.{ext}" if ext else base


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    files = []
    for spec in sys.argv[2:]:
        name, _, path = spec.partition("=")
        files.append((short_name(name), open(path, "rb").read()))

    offset = len(MAGIC) + 8 + ENTRY.size * len(files)
    entries, data = b"", b""
    for name, content in files:
        entries += ENTRY.pack(name.encode("ascii"), offset + len(data), len(content))
        data += content + b"\0" * (-len(content) % 4)
    with open(sys.argv[1], "wb") as f:
        f.write(MAGIC + struct.pack("<II", len(files), 0) + entries + data)
    print(f"package ready: {sys.argv[1]} ({len(files)} files: {', '.join(n for n, _ in files)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
