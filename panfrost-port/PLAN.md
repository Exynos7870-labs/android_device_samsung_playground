# Panfrost (Mesa) on Mali-T830 via kbase — port plan

Status: **executing.**  Tooling for M0 and M1 is written and host-verified.
Device runs (the two steps that need the phone) are pending.

## 0. Why

The SCS/x18 crash loop is currently fixed by building with ShadowCallStack off
(PR #1). A permanent fix needs GL code that does not clobber x18, and every
vendor option is exhausted:

* the newest 64-bit T830 userspace Samsung shipped **is already in this tree**
  (MaliT830_r28p0, byte-identical to the A7 2017 Android 9 and J6 dumps);
* the r29p0 blobs (Android 10, clang 9.0.3 — the interesting toolchain) exist
  **only as 32-bit** and cannot fix a 64-bit SCS crash;
* Huawei's EMUI 9 T830 blob is the same DDK with the same ~8.4k x18 references.

Mesa/Panfrost is the only remaining source of a driver that fits.  Panfrost
Midgard is non-conformant, has no Vulkan, and is slower than the blob — the value
is a driver we build ourselves with a modern clang (x18 reserved ⇒ SCS can stay
on), plus a maintainable GL stack.

## 1. The two halves (both exist; the join is the work)

**Half A — Midgard support in Mesa** (present in the JimVulkan fork tree):
`src/panfrost/compiler/midgard/` (ISA compiler), `pan_jm.c` (JM submission, with
`#if PAN_ARCH == 5` blocks alongside Bifrost/Valhall), `pan_cmdstream.c`
(Midgard command streams).

**Half B — kbase backend** (same fork, MIT): `src/panfrost/lib/kmod/kbase_kmod.c/.h`
(`pan_kmod` backend over `/dev/mali0`: version check, GPU props, BO alloc/import,
GPU VA, sync provider) and `pan_jm.c: jm_submit_batch_kbase()` (two atoms:
vertex/tiler chain, then fragment chain waiting on it).

The fork scopes itself to Bifrost and app-local use; the code is arch-parameterised,
which is why the join is plausible.

## 2. ABI: ours vs. the fork's target (all read from this repo's kernel)

| Interface | Fork (newer kbase) | Our r28p0 | Status |
|---|---|---|---|
| ioctls used by the backend | 0,1,2,3,5,7,22,38 | all exist (`mali_kbase_ioctl.h`), 38 = `MEM_EXEC_INIT` | OK |
| `JOB_SUBMIT` payload | `{u64 addr; u32 nr_atoms; u32 stride}` | identical | OK |
| Atom stride | 64 | kernel enforces `stride == sizeof(base_jd_atom_v2)` = 64 | OK |
| Atom **layout** | `seq_nr@0 … core_req@52` | `jc@0 … core_req@44` + SRUK tail (`gles_ctx_handle@48`, `frame_number@52`, `surfacep@56`) | must use ours — declared in `uapi/kbase_uapi_r28p0.h`, asserted in `uapi/verify_layout.c` |
| Completion | poll + `read()`, 24-byte records, code 1 = DONE | `base_jd_event_v2` = 24 bytes, `BASE_JD_EVENT_DONE = 0x01`, `EPIPE` = terminated | OK |
| Dep types | 0/1/2 | `BASE_JD_DEP_TYPE_INVALID/DATA/ORDER` = 0/1/2 | OK |
| `core_req` values | `0x4e` / `0x8001` captured on G76 | T830's values unknown | **M0 capture** |
| MEM_ALLOC precondition | — | tracking page must be mapped first (`kbase_api_mem_alloc()` rejects while `kctx->process_mm != current->mm`) | handled in M1 |
| mmap convention | — | `mmap(fd, offset = gpu_va >> 12)` | handled in M1 |
| SRUK fields | absent | systrace-only copy (`mali_kbase_jd.c:828`), zeros fine | verify on device |

This kernel family also returns **success for DRM ioctls it does not implement** —
every step must be verified by readback, never by ioctl return alone.

## 3. Work items and status

| # | Item | Status |
|---|---|---|
| W0/M0 | Capture the vendor driver's atoms on the device | **tooling ready** (`m0/`: LD_PRELOAD interposer + decoder + README); device run pending |
| W1/M1 | kbase bring-up: version check, tracking page, props, alloc + CPU map, exec-init, one soft job completed via `read()` | **tooling ready** (`m1/kbase_probe.c`), host simulation green; device run pending |
| W2 | Mesa-side: r28p0 atom layout in `kbase_kmod.c/h`; `core_req` from the M0 capture | pending M0 |
| W3 | Midgard job chains over the kbase path (`jm_submit_batch_kbase`, tiler heap for T8xx) | pending M1 |
| W4 | Sync: in-process sync provider + `SOFT_FENCE_TRIGGER` (0x202) against r28p0 | pending M1 |
| W5 | Userspace build with a modern clang (the whole point) — meson, app-local first | pending W2–W4 |
| W5a | Acceptance check for W5/W6: `tools/x18scan.py libEGL_mesa.so` must report **0** x18 references (the vendor blob has ~8.5k; this is the condition for re-enabling SCS) | tooling ready |
| W6 | Integration: ship the stack, delete the SCS-off patch, confirm a clean boot log | pending M2 |

## 4. Milestones and kill criteria

| Milestone | Done when | Kill if |
|---|---|---|
| M0 probe | ≥2 submissions decoded; T830 `core_req` values + tiler/fragment shape recorded | — |
| M1 bring-up | `kbase_probe` prints BRING-UP OK on the device (soft job completes, udata echoes) | r28p0 rejects our atoms for reasons our own headers do not explain |
| M2 first triangle | Midgard tiler + fragment chains render into a standalone EGL surface | tiler heap/descriptor semantics cannot be matched |
| M3 usable GL | real apps run app-local; perf/stability measured | — |
| M4 integration | system GL or shipped app-local **and** the SCS patch deleted with a clean boot log | — |

## 5. Risks

* Tiler heap / Midgard descriptors on kbase — the one place neither half proves
  the combination. M0 captures exactly what the vendor blob passes.
* Samsung kbase modifications (SRUK systrace fields, SLSI ioctls 42/44,
  `MEM_EXEC_INIT`) may gate behaviour; only device testing answers this.
* Non-conformant Midgard Panfrost, slower than the blob, no Vulkan.
* The kbase backend is fork-local (not upstreamed): we carry patches on Mesa.

## 6. Licensing

Mesa: MIT. kbase backend: MIT (© 2026 XclipseTools). Kernel kbase: GPLv2, already
in-tree. Nothing here blocks shipping.

## 7. Immediate next steps

1. Device: run `m1/kbase_probe_arm64` (see `m1/README.md`). Expected: BRING-UP OK.
2. Device: run the M0 capture against a GLES test binary (`m0/README.md`) and
   decode — that pins T830's `core_req` values and the tiler/fragment shape.
3. Then W2: patch the fork's `kbase_kmod.c/h` with the r28p0 layout declared in
   `uapi/kbase_uapi_r28p0.h` and start M2 (first triangle).
