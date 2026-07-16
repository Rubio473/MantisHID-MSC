#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import math
import os
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

SECTOR_SIZE = 512
PARTITION_START = 2048
RESERVED_SECTORS = 32
FAT_COUNT = 2
ROOT_CLUSTER = 2
VOLUME_LABEL = b"MANTISUSB  "
FAT_EOC = 0x0FFFFFFF
MIN_SIZE_MIB = 64
MAX_SIZE_MIB = 4095

DEVICE_BYTES = 0
DEVICE_SECTORS = 0
PARTITION_SECTORS = 0
SECTORS_PER_CLUSTER = 0
VOLUME_ID = 0
FAT_SECTORS = 0
CLUSTER_COUNT = 0
FIRST_DATA_REL = 0
CLUSTER_BYTES = 0


def calculate_fat_sectors(partition_sectors: int, sectors_per_cluster: int) -> tuple[int, int]:
    fat_sectors = 1
    for _ in range(64):
        data_sectors = partition_sectors - RESERVED_SECTORS - FAT_COUNT * fat_sectors
        clusters = data_sectors // sectors_per_cluster
        needed = math.ceil((clusters + 2) * 4 / SECTOR_SIZE)
        if needed == fat_sectors:
            return fat_sectors, clusters
        fat_sectors = needed
    raise RuntimeError("FAT geometry did not converge")


def configure_geometry(size_mib: int) -> None:
    global DEVICE_BYTES, DEVICE_SECTORS, PARTITION_SECTORS
    global SECTORS_PER_CLUSTER, VOLUME_ID, FAT_SECTORS, CLUSTER_COUNT
    global FIRST_DATA_REL, CLUSTER_BYTES
    if size_mib < MIN_SIZE_MIB or size_mib > MAX_SIZE_MIB:
        raise ValueError(f"size must be {MIN_SIZE_MIB}-{MAX_SIZE_MIB} MiB")
    device_bytes = size_mib * 1024 * 1024
    device_sectors = device_bytes // SECTOR_SIZE
    partition_sectors = device_sectors - PARTITION_START
    selected = None
    for spc in (128, 64, 32, 16, 8, 4, 2, 1):
        fat_sectors, clusters = calculate_fat_sectors(partition_sectors, spc)
        if 65525 <= clusters < 0x0FFFFFF5:
            selected = (spc, fat_sectors, clusters)
            break
    if selected is None:
        raise ValueError("requested size cannot be represented as FAT32")
    DEVICE_BYTES = device_bytes
    DEVICE_SECTORS = device_sectors
    PARTITION_SECTORS = partition_sectors
    SECTORS_PER_CLUSTER, FAT_SECTORS, CLUSTER_COUNT = selected
    FIRST_DATA_REL = RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS
    CLUSTER_BYTES = SECTORS_PER_CLUSTER * SECTOR_SIZE
    VOLUME_ID = (0x4D550000 ^ size_mib ^ (size_mib << 12)) & 0xFFFFFFFF


def put16(buf: bytearray, off: int, value: int) -> None:
    struct.pack_into("<H", buf, off, value)


def put32(buf: bytearray, off: int, value: int) -> None:
    struct.pack_into("<I", buf, off, value)


def lfn_checksum(short11: bytes) -> int:
    checksum = 0
    for b in short11:
        checksum = (((checksum & 1) << 7) | (checksum >> 1)) + b
        checksum &= 0xFF
    return checksum


def sanitize_component(text: str) -> str:
    allowed = "$%'-_@~`!(){}^#&"
    out = []
    for ch in text.upper():
        if ch.isalnum() or ch in allowed:
            out.append(ch)
        else:
            out.append("_")
    return "".join(out)


def make_short_name(name: str, used: set[bytes]) -> bytes:
    if name in (".", ".."):
        return name.ljust(11).encode("ascii")
    stem, dot, ext = name.rpartition(".")
    if not dot:
        stem, ext = name, ""
    stem_s = sanitize_component(stem)
    ext_s = sanitize_component(ext)

    direct = (stem_s[:8].ljust(8) + ext_s[:3].ljust(3)).encode("ascii", "replace")
    valid_direct = (
        1 <= len(stem_s) <= 8
        and len(ext_s) <= 3
        and stem_s == stem.upper()
        and ext_s == ext.upper()
    )
    if valid_direct and direct not in used:
        used.add(direct)
        return direct

    base = stem_s or "FILE"
    ext3 = ext_s[:3]
    for n in range(1, 1000000):
        suffix = f"~{n}"
        prefix = base[: max(1, 8 - len(suffix))]
        short = (prefix + suffix).ljust(8)[:8] + ext3.ljust(3)
        encoded = short.encode("ascii", "replace")
        if encoded not in used:
            used.add(encoded)
            return encoded
    raise RuntimeError(f"Cannot allocate short name for {name}")


def lfn_entries(name: str, short11: bytes) -> list[bytes]:
    units = list(struct.unpack("<" + "H" * (len(name.encode("utf-16le")) // 2), name.encode("utf-16le")))
    units.append(0x0000)
    while len(units) % 13:
        units.append(0xFFFF)
    count = len(units) // 13
    checksum = lfn_checksum(short11)
    result: list[bytes] = []
    offsets = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]
    for seq in range(count, 0, -1):
        entry = bytearray([0xFF] * 32)
        entry[0] = seq | (0x40 if seq == count else 0)
        entry[11] = 0x0F
        entry[12] = 0
        entry[13] = checksum
        entry[26] = 0
        entry[27] = 0
        chunk = units[(seq - 1) * 13 : seq * 13]
        for off, code in zip(offsets, chunk):
            put16(entry, off, code)
        result.append(bytes(entry))
    return result


@dataclass
class Node:
    name: str
    is_dir: bool
    data: bytes = b""
    parent: "Node | None" = None
    children: list["Node"] = field(default_factory=list)
    first_cluster: int = 0
    clusters: list[int] = field(default_factory=list)
    short_name: bytes = b""

    @property
    def size(self) -> int:
        return 0 if self.is_dir else len(self.data)


def build_tree(source_root: Path) -> Node:
    root = Node(name="", is_dir=True)

    def add_dir(parent: Node, path: Path) -> None:
        for child_path in sorted(path.iterdir(), key=lambda p: (not p.is_dir(), p.name.lower())):
            if child_path.name.startswith("."):
                continue
            if child_path.is_dir():
                child = Node(name=child_path.name, is_dir=True, parent=parent)
                parent.children.append(child)
                add_dir(child, child_path)
            elif child_path.is_file():
                child = Node(name=child_path.name, is_dir=False, data=child_path.read_bytes(), parent=parent)
                parent.children.append(child)

    mantis_sd = Node(name="MantisSD", is_dir=True, parent=root)
    root.children.append(mantis_sd)
    add_dir(mantis_sd, source_root)
    size_mib = DEVICE_BYTES // 1024 // 1024
    readme = (
        f"MANTISUSB {size_mib} MiB\r\n"
        "====================\r\n\r\n"
        "This FAT32 virtual disk is stored in MANTISUSB.IMG on the physical\r\n"
        "microSD card. Always eject MANTISUSB before disconnecting the device\r\n"
        "or synchronizing payloads and layouts.\r\n"
    ).encode("utf-8")
    root.children.append(Node(name="README.txt", is_dir=False, data=readme, parent=root))
    return root


def walk_nodes(root: Node) -> Iterable[Node]:
    yield root
    for child in root.children:
        yield from walk_nodes(child)


def assign_short_names(directory: Node) -> None:
    used: set[bytes] = set()
    for child in directory.children:
        child.short_name = make_short_name(child.name, used)
    for child in directory.children:
        if child.is_dir:
            assign_short_names(child)


def needs_lfn(node: Node) -> bool:
    decoded_stem = node.short_name[:8].decode("ascii").rstrip()
    decoded_ext = node.short_name[8:11].decode("ascii").rstrip()
    decoded = decoded_stem + (("." + decoded_ext) if decoded_ext else "")
    return decoded != node.name.upper()


def directory_entry_count(directory: Node) -> int:
    count = 0 if directory.parent is None else 2
    if directory.parent is None:
        count += 1
    for child in directory.children:
        if needs_lfn(child):
            count += math.ceil((len(child.name.encode("utf-16le")) // 2 + 1) / 13)
        count += 1
    count += 1
    return count


def allocate_clusters(root: Node) -> tuple[list[int], int]:
    fat = [0] * (CLUSTER_COUNT + 2)
    fat[0] = 0x0FFFFFF8
    fat[1] = 0xFFFFFFFF
    next_cluster = ROOT_CLUSTER

    for node in walk_nodes(root):
        if node.is_dir:
            bytes_needed = directory_entry_count(node) * 32
            cluster_needed = max(1, math.ceil(bytes_needed / CLUSTER_BYTES))
        else:
            cluster_needed = max(1, math.ceil(len(node.data) / CLUSTER_BYTES)) if node.data else 0
        if cluster_needed == 0:
            node.first_cluster = 0
            node.clusters = []
            continue
        if next_cluster + cluster_needed > CLUSTER_COUNT + 2:
            raise RuntimeError("Image contents do not fit")
        node.clusters = list(range(next_cluster, next_cluster + cluster_needed))
        node.first_cluster = node.clusters[0]
        for a, b in zip(node.clusters, node.clusters[1:]):
            fat[a] = b
        fat[node.clusters[-1]] = FAT_EOC
        next_cluster += cluster_needed

    return fat, next_cluster


def make_short_entry(node: Node, attr: int | None = None) -> bytes:
    entry = bytearray(32)
    entry[:11] = node.short_name
    entry[11] = (0x10 if node.is_dir else 0x20) if attr is None else attr
    dos_date = ((2026 - 1980) << 9) | (1 << 5) | 1
    put16(entry, 14, 0)
    put16(entry, 16, dos_date)
    put16(entry, 18, dos_date)
    put16(entry, 22, 0)
    put16(entry, 24, dos_date)
    put16(entry, 20, (node.first_cluster >> 16) & 0xFFFF)
    put16(entry, 26, node.first_cluster & 0xFFFF)
    put32(entry, 28, node.size)
    return bytes(entry)


def dot_entry(name: str, cluster: int) -> bytes:
    entry = bytearray(32)
    entry[:11] = (name.ljust(11)).encode("ascii")
    entry[11] = 0x10
    put16(entry, 20, (cluster >> 16) & 0xFFFF)
    put16(entry, 26, cluster & 0xFFFF)
    return bytes(entry)


def make_directory_bytes(directory: Node) -> bytes:
    entries: list[bytes] = []
    if directory.parent is None:
        label = bytearray(32)
        label[:11] = VOLUME_LABEL
        label[11] = 0x08
        entries.append(bytes(label))
    else:
        entries.append(dot_entry(".", directory.first_cluster))
        parent_cluster = directory.parent.first_cluster if directory.parent.parent is not None else ROOT_CLUSTER
        entries.append(dot_entry("..", parent_cluster))

    for child in directory.children:
        if needs_lfn(child):
            entries.extend(lfn_entries(child.name, child.short_name))
        entries.append(make_short_entry(child))
    entries.append(bytes(32))
    data = b"".join(entries)
    allocated = len(directory.clusters) * CLUSTER_BYTES
    if len(data) > allocated:
        raise RuntimeError(f"Directory {directory.name} overflow")
    return data.ljust(allocated, b"\x00")


def cluster_offset(cluster: int) -> int:
    rel_sector = FIRST_DATA_REL + (cluster - 2) * SECTORS_PER_CLUSTER
    return (PARTITION_START + rel_sector) * SECTOR_SIZE


def make_mbr() -> bytes:
    b = bytearray(SECTOR_SIZE)
    put32(b, 440, VOLUME_ID)
    part = 446
    b[part + 0] = 0x00
    b[part + 1 : part + 4] = b"\xFE\xFF\xFF"
    b[part + 4] = 0x0C
    b[part + 5 : part + 8] = b"\xFE\xFF\xFF"
    put32(b, part + 8, PARTITION_START)
    put32(b, part + 12, PARTITION_SECTORS)
    b[510:512] = b"\x55\xAA"
    return bytes(b)


def make_boot_sector() -> bytes:
    b = bytearray(SECTOR_SIZE)
    b[0:3] = b"\xEB\x58\x90"
    b[3:11] = b"MSWIN4.1"
    put16(b, 11, SECTOR_SIZE)
    b[13] = SECTORS_PER_CLUSTER
    put16(b, 14, RESERVED_SECTORS)
    b[16] = FAT_COUNT
    put16(b, 17, 0)
    put16(b, 19, 0)
    b[21] = 0xF8
    put16(b, 22, 0)
    put16(b, 24, 63)
    put16(b, 26, 255)
    put32(b, 28, PARTITION_START)
    put32(b, 32, PARTITION_SECTORS)
    put32(b, 36, FAT_SECTORS)
    put16(b, 40, 0)
    put16(b, 42, 0)
    put32(b, 44, ROOT_CLUSTER)
    put16(b, 48, 1)
    put16(b, 50, 6)
    b[64] = 0x80
    b[66] = 0x29
    put32(b, 67, VOLUME_ID)
    b[71:82] = VOLUME_LABEL
    b[82:90] = b"FAT32   "
    b[510:512] = b"\x55\xAA"
    return bytes(b)


def make_fsinfo(free_clusters: int, next_free: int) -> bytes:
    b = bytearray(SECTOR_SIZE)
    put32(b, 0, 0x41615252)
    put32(b, 484, 0x61417272)
    put32(b, 488, free_clusters)
    put32(b, 492, next_free)
    put32(b, 508, 0xAA550000)
    return bytes(b)


def write_image(output: Path, source_root: Path) -> None:
    root = build_tree(source_root)
    assign_short_names(root)
    fat, next_free = allocate_clusters(root)
    used_clusters = next_free - 2
    free_clusters = CLUSTER_COUNT - used_clusters

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as f:
        f.truncate(DEVICE_BYTES)
        f.seek(0)
        f.write(make_mbr())

        partition_base = PARTITION_START * SECTOR_SIZE
        boot = make_boot_sector()
        fsinfo = make_fsinfo(free_clusters, next_free)
        for rel, data in ((0, boot), (1, fsinfo), (6, boot), (7, fsinfo)):
            f.seek(partition_base + rel * SECTOR_SIZE)
            f.write(data)

        fat_bytes = bytearray(FAT_SECTORS * SECTOR_SIZE)
        for cluster, value in enumerate(fat):
            struct.pack_into("<I", fat_bytes, cluster * 4, value)
        for copy_index in range(FAT_COUNT):
            rel = RESERVED_SECTORS + copy_index * FAT_SECTORS
            f.seek(partition_base + rel * SECTOR_SIZE)
            f.write(fat_bytes)

        for node in walk_nodes(root):
            if not node.clusters:
                continue
            data = make_directory_bytes(node) if node.is_dir else node.data
            remaining = data
            for cluster in node.clusters:
                f.seek(cluster_offset(cluster))
                chunk = remaining[:CLUSTER_BYTES]
                f.write(chunk.ljust(CLUSTER_BYTES, b"\x00"))
                remaining = remaining[CLUSTER_BYTES:]

    verify_image(output)


def read_at(f, offset: int, size: int) -> bytes:
    f.seek(offset)
    data = f.read(size)
    if len(data) != size:
        raise RuntimeError("Short image read")
    return data


def verify_image(path: Path) -> None:
    if path.stat().st_size != DEVICE_BYTES:
        raise RuntimeError("Incorrect image size")
    with path.open("rb") as f:
        mbr = read_at(f, 0, SECTOR_SIZE)
        if mbr[510:512] != b"\x55\xAA":
            raise RuntimeError("Invalid MBR signature")
        start, count = struct.unpack_from("<II", mbr, 454)
        if start != PARTITION_START or count != PARTITION_SECTORS:
            raise RuntimeError("Incorrect partition geometry")
        boot = read_at(f, PARTITION_START * SECTOR_SIZE, SECTOR_SIZE)
        if boot[510:512] != b"\x55\xAA" or boot[82:90] != b"FAT32   ":
            raise RuntimeError("Invalid FAT32 boot sector")
        if struct.unpack_from("<H", boot, 11)[0] != SECTOR_SIZE or boot[13] != SECTORS_PER_CLUSTER:
            raise RuntimeError("Unexpected FAT32 cluster geometry")
        root = read_at(f, cluster_offset(ROOT_CLUSTER), CLUSTER_BYTES)
        if VOLUME_LABEL not in root or b"MANTISSD" not in root:
            raise RuntimeError("Required root entries missing")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--size-mib", type=int, default=500)
    parser.add_argument("--source", type=Path, default=Path("sd_card/MantisSD"))
    parser.add_argument("--output", type=Path, default=Path("MANTISUSB.IMG"))
    args = parser.parse_args()
    configure_geometry(args.size_mib)
    if not args.source.is_dir():
        raise SystemExit(f"source directory not found: {args.source}")
    write_image(args.output, args.source)
    print(f"Created: {args.output}")
    print(f"Size: {args.output.stat().st_size} bytes ({args.output.stat().st_size / 1024 / 1024:.1f} MiB)")
    print(f"Sectors: {DEVICE_SECTORS}; FAT sectors: {FAT_SECTORS}; clusters: {CLUSTER_COUNT}")
    print(f"SHA256: {sha256(args.output)}")


if __name__ == "__main__":
    main()
