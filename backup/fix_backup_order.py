#!/usr/bin/env python3
"""Fix a scrambled ESP8266 4MB full flash dump.

Problem
-------
The provided `backup/backup.sh` originally merged chunk files using:

  cat part_*.bin > backup_full.bin

In bash, `part_10.bin` sorts before `part_2.bin`, producing a scrambled 4MB image.
Flashing that scrambled `backup_full.bin` will boot (first chunks are OK) but most
of the flash contents (FS/config/system params) land at the wrong addresses.

This tool reconstructs the correct 4MB image by reordering 0x40000 chunks.

Assumptions
-----------
- Chunk filenames were `part_0.bin`..`part_15.bin` (no zero padding)
- The glob expansion order was lexicographic
- Chunk size is 0x40000 and total size is 0x400000 (4MB)

Usage
-----
  python3 backup/fix_backup_full_order.py backup/backup_full.bin --out backup/backup_full_fixed.bin
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Layout:
    flash_size: int = 0x400000
    chunk_size: int = 0x40000
    chunks: int = 16

    def chunk_count(self) -> int:
        return self.flash_size // self.chunk_size


def lexicographic_cat_order(chunks: int) -> list[int]:
    return sorted(range(chunks), key=lambda i: f"part_{i}.bin")


def invert_order(order: list[int]) -> list[int]:
    pos_of_chunk = [0] * len(order)
    for pos, chunk_index in enumerate(order):
        pos_of_chunk[chunk_index] = pos
    return pos_of_chunk


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("input", type=Path, help="Scrambled 4MB dump (e.g. backup_full.bin)")
    ap.add_argument("--out", type=Path, required=True, help="Output fixed dump path")
    args = ap.parse_args()

    layout = Layout()
    blob = args.input.read_bytes()
    if len(blob) != layout.flash_size:
        raise SystemExit(f"Expected 4MB (0x{layout.flash_size:X}) input, got 0x{len(blob):X}")

    order = lexicographic_cat_order(layout.chunks)
    pos_of_chunk = invert_order(order)

    fixed = bytearray(layout.flash_size)
    for chunk_index in range(layout.chunks):
        src_pos = pos_of_chunk[chunk_index]
        src_off = src_pos * layout.chunk_size
        dst_off = chunk_index * layout.chunk_size
        fixed[dst_off : dst_off + layout.chunk_size] = blob[src_off : src_off + layout.chunk_size]

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(fixed)

    print(f"Wrote fixed dump: {args.out}")
    print("Reorder mapping (chunk_index <- input_chunk_position):")
    for i in range(layout.chunks):
        print(f"  chunk {i:2d} <- input_pos {pos_of_chunk[i]:2d} (was part_{order[pos_of_chunk[i]]}.bin)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
