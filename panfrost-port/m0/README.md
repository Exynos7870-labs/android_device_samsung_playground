# M0 — capture the vendor GL driver's kbase atoms

The Panfrost port needs T830's own `core_req` values and job shapes. Do not guess
them: the JimVulkan Bifrost port captured its values the same way and got
`0x4e` / `0x8001` on a G76, which are not necessarily what the T830 blob sends.

## Build

```bash
NDK=$ANDROID_NDK_HOME            # NDK r21+ is fine, this is plain C
# 64-bit processes:
$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang \
    -shared -fPIC -O2 -o libkbase_spy.so ioctl_spy.c -ldl
# and the 32-bit variant if you test a 32-bit GLES app:
$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi31-clang \
    -shared -fPIC -O2 -o libkbase_spy32.so ioctl_spy.c -ldl
```

## Run

Easiest target is a standalone GLES test binary (any self-contained EGL/GLES
program; it does not have to be the fork's `glprobe`).

```bash
adb push libkbase_spy.so /data/local/tmp/
adb shell "LD_PRELOAD=/data/local/tmp/libkbase_spy.so \
           KBASE_SPY_OUT=/data/local/tmp/atoms.txt \
           /data/local/tmp/glprobe"
adb pull /data/local/tmp/atoms.txt
```

Notes:

* Only processes that actually render with the vendor blob produce submissions —
  the blob is what opens `/dev/mali0` and does the ioctls.
* `LD_PRELOAD` must be in the environment of the process that loads the blob. For
  an app you cannot spawn yourself, wrap the launch (`am start` from a script)
  rather than trying to inject afterwards.
* `/data/local/tmp` is fine for both the library and the log.

## Decode

```bash
python3 decode_atoms.py atoms.txt
```

Prints, per submission, the atom number, `core_req` with the `BASE_JD_REQ_*` bits
spelled out, the job-chain GPU VA, priority, device number, external-resource
count and the pre-dependency graph. It decodes both 64-byte layouts (our
`r28p0 + Samsung SRUK` one and the newer ARM kbase one) and picks the one whose
`jc` looks like a GPU VA — so the capture doubles as on-device confirmation of
the layout declared in `../uapi/kbase_uapi_r28p0.h`.

## What to look for

1. `core_req` of the tiler/vertex atom and of the fragment atom (Bifrost:
   `0x4e` and `0x8001`). Record the actual T830 values.
2. Whether the fragment atom depends on the tiler atom with type 1 (data) or
   2 (order).
3. Atoms per frame; whether compute or soft jobs appear.
4. `jc` alignment and size (job-chain sizes) — useful for sizing our own chains.
5. Anything with `SOFT_*` bits: that is how the vendor blob gets its fences.

Keep the capture: it is the reference the port will be diffed against.
