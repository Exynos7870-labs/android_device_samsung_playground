# M1 — kbase bring-up (first contact from userspace)

Goal: prove that a plain userspace binary can drive this device's kbase
(`/dev/mali0`, r28p0, UK 11.13) — version handshake, tracking page, props, a GPU
allocation with a working CPU mapping, and **one job completed through the kernel**.

This is the gate for the whole port: if a soft job cannot be submitted and
completed here, no amount of Mesa work matters.

## Files

| File | Role |
|---|---|
| `kbase_probe.c` | the bring-up sequence (10 steps, PASS/FAIL per step) |
| `kb_sys.h` | syscall indirection: real libc on device, simulator on the host |
| `host_sim.c` | simulated kbase for host runs; decodes atoms with an *independent* copy of the kernel layout |
| `Makefile` | `make host`, `make android` |
| `../uapi/kbase_uapi_r28p0.h` | the UAPI mirror, derived from this repo's kernel headers |
| `../uapi/verify_layout.c` | static asserts + offset checks for that mirror |

## Host run (no device needed)

```bash
make host
```

Runs `verify_layout` (proves the atom is 64 bytes, `core_req @ 44`, event 24 bytes,
ioctl codes match the kernel's) and then the full bring-up against the simulator.
Expected tail:

```
9. read() completion events
     event: code=0x01 atom=1 udata=0xfeedfacecafebeef 0x0123456789abcdef (DONE)
  [ OK ] completion received
...
BRING-UP OK (0 failures)
```

## Device run

```bash
export ANDROID_NDK_HOME=/path/to/ndk     # r21+ is enough, this is plain C
make android
adb push kbase_probe_arm64 /data/local/tmp/
adb shell /data/local/tmp/kbase_probe_arm64
```

Also builds a 32-bit variant (`kbase_probe_arm32`) — useful because the vendor GL
stack is 32-bit on several 7870 devices, and both must work eventually.

No root needed: `/dev/mali0` is world-openable on this platform. Run the 64-bit
binary first; it is the one whose result gates the port.

### What the output means

* Steps 1–4 must pass. `VERSION_CHECK` negotiating 11.13 proves the kernel is the
  revision this UAPI mirror was written against.
* Step 5 `MEM_ALLOC` failing with `EINVAL` usually means the **tracking page**
  (step 2) is missing — `kbase_api_mem_alloc()` refuses while
  `kctx->process_mm != current->mm` (`mali_kbase_core_linux.c:587`).
* Step 6 proves the mmap offset convention: `mmap(fd, offset = gpu_va >> 12)`.
* Step 8 is the real test: one `SOFT_DUMP_CPU_GPU_TIME` atom (`core_req = 0x201`).
  It needs no GPU code at all — the kernel writes timestamps into the pointer in
  `atom.jc` (`mali_kbase_softjobs.c:134`), so a DONE event plus non-zero counters
  proves submit → execute → complete → event delivery.
* Step 9: events are 24-byte `base_jd_event_v2` records; `udata` must echo what we
  submitted. A mismatch means the atom layout is wrong (the whole point of step 8
  being a soft job: it isolates the layout from any GPU work).
* Step 7 `MEM_EXEC_INIT` (ioctl 38) is reported but non-fatal — the vendor blob
  issues it, our soft job does not need the EXEC_VA zone.

## Capturing the result

Keep the output; it is the reference for the next milestone (Midgard job chains).
If step 8/9 fails, the printed errno and the raw prop stream are the two things
worth reporting.
