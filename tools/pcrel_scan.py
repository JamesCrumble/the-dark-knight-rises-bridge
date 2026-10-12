#!/usr/bin/env python3
"""ARM-mode code that builds an address PC-relatively (ldr rM,[pc,#i] ... add rD,pc,rM) landing in a range.
pcrel_scan.py <lib> <lo> <hi>   -> "add address: target" for every target in [lo, hi)
Finds the anchor a function uses for a block of strings when no literal names one of them exactly."""
import struct
import sys

d = open(sys.argv[1], 'rb').read()
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
TEXT_LO, TEXT_HI = 0xc5cb0, 0xa3bfa4          # .text of libKRAS.so 1.1.6 (file offset == address)
if len(sys.argv) > 5:
    TEXT_LO, TEXT_HI = int(sys.argv[4], 16), int(sys.argv[5], 16)
for a in range(TEXT_LO, TEXT_HI, 4):
    w, = struct.unpack_from('<I', d, a)
    if w & 0xffff0ff0 != 0xe08f0000:
        continue
    rm = w & 15
    for back in range(1, 24):
        a2 = a - 4 * back
        w2, = struct.unpack_from('<I', d, a2)
        if w2 & 0xff7f0000 == 0xe51f0000 and (w2 >> 12) & 15 == rm:
            imm = w2 & 0xfff
            lit = a2 + 8 + (imm if w2 & 0x00800000 else -imm)
            val, = struct.unpack_from('<I', d, lit)
            target = (a + 8 + val) & 0xffffffff
            if lo <= target < hi:
                print('0x%x: 0x%x' % (a, target))
            break
