# LineageOS 22.2/Android 15 patches for Galaxy S7 (HeroLTE) support

These instructions assumes that:

- you have installed LineageOS 22.2 sources installed in ```~/android/system```
  (See <https://github.com/LineageOS/android/tree/lineage-22.2>)

  ```#!/bin/bash
  repo init -u <https://github.com/LineageOS/android.git> -b lineage-22.2 --git-lfs
  repo sync
  ```

- you have installed these patches to ```~/patches```

## 1. Apply device specific repo's

```#!/bin/bash
mkdir ~/android/system/.repo/local_manifests
cd ~/patches
cp roomservice.xml to ~/android/system/.repo/local_manifests
cd ~/android/system
repo sync
```  

## 2. Apply the patches

```#!/bin/bash
cd ~/android/system
. ~/patches/apply.sh
```

## 3. Optional, apply the "Battery Life Extender"-feature patches

```#!/bin/bash
cd ~/android/system
. ~/patches/features/batterylifeextender/apply.sh
```

## 4. Build the ROM

```#!/bin/bash
cd ~/android/system
. build/envsetup.sh
brunch herolte
```

Your build will be put (after many hours) in:

```~/android/system/out/target/product/herolte```

## How to revert the patches

```#!/bin/bash
cd ~/android/system
. ~/patches/features/batterylifeextender/revert.sh
. ~/patches/revert.sh
```

## Notes

* `build/soong/0001-Disable-ShadowCallStack-for-Exynos-7870-builds.patch` turns
  ShadowCallStack off for the whole build.  The vendor GL driver blob of the
  7870 devices (`libGLES_mali.so`, built with the stock 8.0 toolchain) uses
  x18 as a general-purpose register and destroys the shadow stack pointer of
  every process it runs in.  As a result `com.android.bluetooth` and
  `com.android.nfc` crashed in a loop on every boot (`SIGSEGV`, `x18 == 0`
  in the tombstone register dump).  SCS can be re-enabled if the GL driver is
  ever replaced.

### Exynos7870 LMKD compatibility (Log_2 follow-up, 2026-10-02)

`system/memory/lmkd/0001-Support-zoneinfo-without-per-node-statistics.patch`
adds support for the Linux 3.18 per-zone file LRU counters. Without it, Android
15's LMKD expects a `per-node stats` section, rejects `/proc/zoneinfo`, and
cannot handle vmpressure events correctly. Modern per-node parsing is retained.

The patch is included in the patch runner's project map. On an Android tree
that already has the older patches applied, apply **only this new patch** from
the Android source root (use an absolute path to the patch bundle):

```sh
PATCHES=/absolute/path/to/android_vendor_samsung_universal7870-common-patches
PATCH="$PATCHES/system/memory/lmkd/0001-Support-zoneinfo-without-per-node-statistics.patch"
git -C system/memory/lmkd apply --check "$PATCH"
git -C system/memory/lmkd am "$PATCH"
python3 "$PATCHES/tests/test_lmkd_zoneinfo.py" \
    --source system/memory/lmkd/lmkd.cpp --sanitize -v
```

The host tests compile the parser extracted from that source; they need Python
3 and a C++17 compiler (`g++`, or set `CXX`). They do not build the Android
service or test it on a handset. The new patch was checked against AOSP
`android-15.0.0_r32`, with 14 regression tests passing, including UBSan.

For the Bluetooth/RT-cgroup diagnosis, A3 kernel changes, upstream review, and
on-device checklist, see the playground's `docs/bringup-log2.md`.

Have fun!

The original patch set was last tested on:
22 nov 2025.
