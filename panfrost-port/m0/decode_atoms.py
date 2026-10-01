#!/usr/bin/env python3
"""
decode_atoms.py -- decode the JOB_SUBMIT capture written by ioctl_spy.c.

Usage:  python3 decode_atoms.py /data/local/tmp/atoms.txt

Decodes the 64-byte kbase atoms in BOTH layouts that matter, because the two
kbase generations disagree on field offsets:

  r28p0 + Samsung SRUK (this device's kernel -- authoritative here, see
  panfrost-port/uapi/kbase_uapi_r28p0.h and
  drivers/gpu/arm/t83x/r28p0/mali_base_kernel.h:870):
      jc u64@0, udata u64[2]@8, extres_list u64@24, nr_extres u16@32,
      compat_core_req u16@34, pre_dep[2] {u8,u8}@36,38, atom_number u8@40,
      prio u8@41, device_nr u8@42, pad u8@43, core_req u32@44,
      SRUK: gles_ctx_handle u32@48, frame_number u32@52, surfacep u64@56

  newer ARM kbase (what the JimVulkan Bifrost port captured):
      seq_nr u64@0, jc u64@8, udata u64[2]@16, extres_list u64@32,
      nr_extres u16@40, jit_id u8[2]@42, pre_dep[2]@44,46, atom_number u8@48,
      prio@49, device_nr@50, jobslot@51, core_req u32@52, renderpass_id u8@56

Both are 64 bytes, so the stride alone cannot tell them apart -- the tell is
whether `jc` looks like a GPU VA at the candidate offset.

SPDX-License-Identifier: MIT
"""

import re
import struct
import sys

CORE_REQ = [
    (1 << 0, "FS"),
    (1 << 1, "CS"),
    (1 << 2, "T"),
    (1 << 3, "CF"),
    (1 << 4, "V"),
    (1 << 5, "EVENT_COALESCE"),
    (1 << 6, "COHERENT_GROUP"),
    (1 << 7, "PERMON"),
    (1 << 8, "EXTERNAL_RESOURCES"),
    (1 << 9, "SOFT_JOB"),
    (1 << 10, "ONLY_COMPUTE"),
    (1 << 11, "SPECIFIC_COHERENT_GROUP"),
    (1 << 12, "EVENT_ONLY_ON_FAILURE"),
    (1 << 13, "FS_AFBC"),
    (1 << 15, "SKIP_CACHE_START"),
    (1 << 16, "SKIP_CACHE_END"),
]
SOFT_KIND = {
    0x201: "SOFT_DUMP_CPU_GPU_TIME",
    0x202: "SOFT_FENCE_TRIGGER",
    0x203: "SOFT_FENCE_WAIT",
    0x205: "SOFT_EVENT_WAIT",
    0x206: "SOFT_EVENT_SET",
    0x207: "SOFT_EVENT_RESET",
    0x208: "SOFT_DEBUG_COPY",
    0x209: "SOFT_JIT_ALLOC",
    0x20A: "SOFT_JIT_FREE",
    0x20B: "SOFT_EXT_RES_MAP",
    0x20C: "SOFT_EXT_RES_UNMAP",
}
DEP_TYPE = {0: "invalid", 1: "data", 2: "order"}

LAYOUTS = {
    "r28p0+SRUK": {"jc": 0, "nr_extres": 32, "pre_dep": (36, 38),
                   "atom_number": 40, "prio": 41, "device_nr": 42,
                   "core_req": 44},
    "newer-kbase": {"jc": 8, "nr_extres": 40, "pre_dep": (44, 46),
                    "atom_number": 48, "prio": 49, "device_nr": 50,
                    "core_req": 52},
}


def core_req_names(v):
    if v == 0:
        return "DEP(none)"
    if v in SOFT_KIND:
        return "0x%x {%s}" % (v, SOFT_KIND[v])
    names = [n for bit, n in CORE_REQ if v & bit]
    rest = v & ~sum(bit for bit, _ in CORE_REQ)
    if rest:
        names.append("unknown(0x%x)" % rest)
    return "0x%x {%s}" % (v, ",".join(names))


def decode_atom(blob, layout):
    (jc,) = struct.unpack_from("<Q", blob, layout["jc"])
    (nr_extres,) = struct.unpack_from("<H", blob, layout["nr_extres"])
    (core_req,) = struct.unpack_from("<I", blob, layout["core_req"])
    deps = []
    for off in layout["pre_dep"]:
        aid, dtype = blob[off], blob[off + 1]
        if aid and dtype:
            deps.append("atom %u (%s)" % (aid, DEP_TYPE.get(dtype, "?")))
    return {
        "jc": jc,
        "core_req": core_req,
        "atom_number": blob[layout["atom_number"]],
        "prio": blob[layout["prio"]],
        "device_nr": blob[layout["device_nr"]],
        "nr_extres": nr_extres,
        "deps": deps,
    }


def plausible_jc(v):
    # GPU VAs are mapped high; tiny values are not job-chain addresses.
    return v > 0x1000


def main(path):
    submits = 0
    atoms = 0
    with open(path) as f:
        for line in f:
            m = re.match(r"SUBMIT .* nr=(\d+) stride=(\d+)", line)
            if m:
                submits += 1
                if submits <= 20 or submits % 200 == 0:
                    print("\n=== submission #%d: %s atoms ===" % (submits, m.group(1)))
                continue
            m = re.match(r"\s+ATOM \d+: ([0-9a-f]+)\s*$", line)
            if not m:
                continue
            blob = bytes.fromhex(m.group(1))
            if len(blob) != 64:
                print("  (atom size %d, skipping decode)" % len(blob))
                continue
            atoms += 1
            layout = None
            for name, lay in LAYOUTS.items():
                if plausible_jc(struct.unpack_from("<Q", blob, lay["jc"])[0]):
                    layout = (name, lay)
                    break
            if layout is None:
                layout = ("r28p0+SRUK", LAYOUTS["r28p0+SRUK"])
            name, lay = layout
            a = decode_atom(blob, lay)
            if submits <= 20 or submits % 200 == 0:
                print("  atom#%-3u core_req=%-34s jc=0x%012x prio=%u dev=%u "
                      "extres=%u deps=%s [%s]"
                      % (a["atom_number"], core_req_names(a["core_req"]), a["jc"],
                         a["prio"], a["device_nr"], a["nr_extres"],
                         a["deps"] or "none", name))
    print("\n%d submissions, %d atoms decoded." % (submits, atoms))
    if submits == 0:
        print("Nothing captured.  Check that KBASE_SPY_OUT is writable and that the\n"
              "target process really renders with the vendor GL driver.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
