#!/usr/bin/env python3
"""RTR-OS - creates a FAT32 card image for the emulator.

Writes a freshly formatted volume (no partition table) and puts the given
files in its root directory under 8.3 names.

Usage: make-sd-image.py <image> <size in MiB> [NAME.EXT=path ...]
"""
import struct
import sys

SECTOR = 512
SECTORS_PER_CLUSTER = 1
RESERVED = 32
FATS = 2
ROOT_CLUSTER = 2
END_OF_CHAIN = 0x0FFFFFFF


def boot_sector(total_sectors, fat_size):
    b = bytearray(SECTOR)
    b[0:3] = b"\xEB\x58\x90"
    b[3:11] = b"RTROS   "
    struct.pack_into("<HBHBHHBHHHII", b, 11, SECTOR, SECTORS_PER_CLUSTER, RESERVED, FATS,
                     0, 0, 0xF8, 0, 63, 255, 0, total_sectors)
    struct.pack_into("<IHHIHH", b, 36, fat_size, 0, 0, ROOT_CLUSTER, 1, 6)
    struct.pack_into("<BBBI", b, 64, 0x80, 0, 0x29, 0x52545230)
    b[71:82] = b"RTR-OS     "
    b[82:90] = b"FAT32   "
    b[510:512] = b"\x55\xAA"
    return bytes(b)


def fs_info(free_clusters):
    b = bytearray(SECTOR)
    struct.pack_into("<I", b, 0, 0x41615252)
    struct.pack_into("<I", b, 484, 0x61417272)
    struct.pack_into("<II", b, 488, free_clusters, 3)
    b[510:512] = b"\x55\xAA"
    return bytes(b)


def short_name(name):
    base, _, ext = name.upper().partition(".")
    if len(base) > 8 or len(ext) > 3 or not base:
        raise SystemExit(f"not an 8.3 name: {name}")
    return base.ljust(8).encode() + ext.ljust(3).encode()


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    image = sys.argv[1]
    size_mib = int(sys.argv[2])
    files = []
    for spec in sys.argv[3:]:
        name, _, path = spec.partition("=")
        files.append((name, open(path, "rb").read()))

    total = size_mib * 1024 * 1024 // SECTOR
    clusters = (total - RESERVED) // SECTORS_PER_CLUSTER
    fat_size = (clusters + 2) * 4 // SECTOR + 1
    data_start = RESERVED + FATS * fat_size
    clusters = (total - data_start) // SECTORS_PER_CLUSTER
    cluster_bytes = SECTORS_PER_CLUSTER * SECTOR

    fat = bytearray(fat_size * SECTOR)
    struct.pack_into("<III", fat, 0, 0x0FFFFFF8, END_OF_CHAIN, END_OF_CHAIN)
    root = bytearray(cluster_bytes)
    next_cluster = ROOT_CLUSTER + 1
    data = b""

    for index, (name, content) in enumerate(files):
        needed = max(1, (len(content) + cluster_bytes - 1) // cluster_bytes)
        first = next_cluster
        for i in range(needed):
            nxt = END_OF_CHAIN if i == needed - 1 else first + i + 1
            struct.pack_into("<I", fat, (first + i) * 4, nxt)
        entry = bytearray(32)
        entry[0:11] = short_name(name)
        entry[11] = 0x20
        struct.pack_into("<H", entry, 20, first >> 16)
        struct.pack_into("<H", entry, 26, first & 0xFFFF)
        struct.pack_into("<I", entry, 28, len(content))
        root[index * 32:(index + 1) * 32] = entry
        data += content.ljust(needed * cluster_bytes, b"\0")
        next_cluster += needed

    with open(image, "wb") as f:
        boot = boot_sector(total, fat_size)
        info = fs_info(clusters - (next_cluster - ROOT_CLUSTER))
        f.write(boot)
        f.write(info)
        f.write(b"\0" * SECTOR * 4)
        f.write(boot)
        f.write(info)
        f.write(b"\0" * SECTOR * (RESERVED - 8))
        for _ in range(FATS):
            f.write(fat)
        f.write(root)
        f.write(data)
        f.truncate(total * SECTOR)
    print(f"card image ready: {image} ({size_mib} MiB, {len(files)} files: "
          f"{', '.join(n for n, _ in files) or 'none'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
