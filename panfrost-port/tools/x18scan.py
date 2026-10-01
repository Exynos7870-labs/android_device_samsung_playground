#!/usr/bin/env python3
"""
x18scan.py -- count x18 references in an aarch64 ELF's .text.

This is the acceptance check for the whole port: the vendor blob
(libGLES_mali.so, clang 6.0.2) contains ~8.5k x18 references and destroys
bionic's ShadowCallStack pointer, which is what crashes com.android.bluetooth and
com.android.nfc at their first SCS prologue (x18 == 0, store to address 0).

A driver built with a modern clang (>= 8, i.e. anything in the Android NDK r21+)
reserves x18 for Android targets, so its count should be 0 -- that is the
condition for deleting the SCS-off Soong patch in
android_vendor_samsung_universal7870-common-patches/.

Usage:
    python3 x18scan.py libEGL_mesa.so [more.so ...]

Needs: pip install capstone pyelftools
SPDX-License-Identifier: MIT
"""

import gc
import re
import sys
import time

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from elftools.elf.elffile import ELFFile

RX = re.compile(r"(?<![0-9a-fA-Fx])[xw]18(?![0-9a-fA-F])")
CHUNK = 1 << 20


def scan(path):
    with open(path, "rb") as f:
        elf = ELFFile(f)
        text = elf.get_section_by_name(".text")
        if text is None:
            print(f"{path}: no .text")
            return None
        taddr, toff, tsize = text["sh_addr"], text["sh_offset"], text["sh_size"]
        f.seek(toff)
        code = f.read(tsize)

    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.skipdata = True
    n = 0
    refs = []
    t0 = time.time()
    for off in range(0, tsize, CHUNK):
        for insn in md.disasm(code[off:off + CHUNK], taddr + off):
            n += 1
            if insn.mnemonic == ".byte":
                continue
            if RX.search(insn.op_str):
                refs.append((insn.address, insn.mnemonic, insn.op_str))
        gc.collect()

    print(f"{path}")
    print(f"  .text {tsize/1048576:.1f} MB, {n} insns, {time.time()-t0:.0f}s")
    print(f"  x18 references: {len(refs)}")
    for addr, mnem, ops in refs[:10]:
        print(f"    0x{addr:x}  {mnem:8s} {ops}")
    if len(refs) > 10:
        print(f"    ... and {len(refs)-10} more")
    if not refs:
        print("  -> SCS-safe: x18 is untouched (SCS can be enabled)")
    return len(refs)


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    total = 0
    for path in argv[1:]:
        got = scan(path)
        if got:
            total += got
    print(f"\nTOTAL x18 references: {total}")
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
