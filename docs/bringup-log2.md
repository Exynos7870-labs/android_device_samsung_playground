# Galaxy A3 (a3y17lte): Log_2 and upstream review

Reviewed on 2026-10-02 against
[`samsungexynos7870/android_device_samsung_universal7870-common`, `lineage-22.2`, `c34b07f`](https://github.com/samsungexynos7870/android_device_samsung_universal7870-common/commit/c34b07ff11af93d3de719aaff04f2c238f910d7f).
The GitHub default branch (`aex`) is not the current Android 15 tree.

## Baseline and scope

- **RIL is working according to the device owner's current test.** Log_2 still
  contains the older rild restart/phone ANR loop; it is not evidence that the
  subsequently fixed build has regressed. The CPEFS mount and CP boot/reset
  handling already match upstream and were left unchanged.
- Almost all of upstream's latest init cleanup was already in this checkout.
  The missing product-library additions were brought over: `libxml2.vendor`,
  `libcrypto`, `libcrypto.vendor`, `libnetutils.vendor`, `librilutils`, and
  `libsqlite.vendor`. The product package set now matches that upstream commit.
- Upstream's global `setrlimit rtprio 12 12` was **not** brought over. The
  Bluetooth UID already receives `CAP_SYS_NICE` from LineageOS 22.2's
  `core/jni/com_android_internal_os_Zygote.cpp`. That capability bypasses the
  RTPRIO ceiling, but not the kernel's zero-budget RT-cgroup check. Raising
  init's limit would also make it inheritable by unrelated processes.

## Changes for the next build

### Bluetooth and process profiles: A3 kernel configuration

Log_2 shows seven Bluetooth SIGABRT tombstones with
`main_thread_start_up: unable to enable real time scheduling`, preceded by
`SCHED_FIFO priority 1 ... Operation not permitted`. This is a different
failure from the earlier Mali/ShadowCallStack crash. SensorService reports
another SCHED_FIFO failure.

The selected `exynos7870-a3y17lte_defconfig` enabled `CONFIG_RT_GROUP_SCHED`.
In this kernel, new groups start with **zero** RT runtime
(`kernel/sched/rt.c:alloc_rt_sched_group`).
`kernel/sched/core.c:__sched_setscheduler` returns EPERM for those groups even
when the caller has `CAP_SYS_NICE`; modern init does not allocate their budgets.

The A3 defconfig now disables **per-group** RT scheduling. It retains
`CONFIG_CGROUP_SCHED`, fair-group scheduling, capability checks, and the normal
**global** RT throttle (950000 microseconds per 1000000-microsecond period).
There is no `sched_rt_runtime_us=-1` workaround or Bluetooth abort suppression.
An on-device capability/cgroup check is still needed to confirm the diagnosis.

The same log shows cpuset and blkio mount failures followed by failed process
profile applications. Enable `CONFIG_CPUSETS`, `CONFIG_PROC_PID_CPUSET`,
`CONFIG_BLK_CGROUP`, `CONFIG_CFQ_GROUP_IOSCHED`, and
`CONFIG_BLK_DEV_THROTTLING` so init can create those v1 hierarchies and apply
CPU-placement and I/O profiles. These changes are scoped to the A3 defconfig;
other device configurations were not changed.

### LMKD: understand the Linux 3.18 zoneinfo format

Log_2 repeatedly reports `/proc/zoneinfo parse error` and
`Failed to get free memory!` once vmpressure monitoring starts. The 3.18 kernel
prints file LRU counters in each zone. Android 15's parser instead requires
`per-node stats` (introduced in Linux 4.8), skips all legacy zones, and returns
an error. This prevents that userspace memory-pressure path from making useful
kill decisions on a 2 GB device.

The new patch is:

```
android_vendor_samsung_universal7870-common-patches/system/memory/lmkd/
  0001-Support-zoneinfo-without-per-node-statistics.patch
```

It detects the format before tokenization, sums the old per-zone file counters
into node totals, preserves modern per-node accounting without double-counting,
and checks truncated node headers and the zone-array limit. It does not hide
the error by returning fake free-memory values. The patch is registered in
`apply_patches.sh` for a fresh patch application.

`system/memory/lmkd` is not part of this playground, so the change is supplied
as a patch against AOSP `android-15.0.0_r32` (`lmkd.cpp` blob
`54129c77941992d66a04c3d91b0085f9210bb50b`), matching the LineageOS manifest.

## Local validation

- Patch application checked against the exact AOSP source above.
- The unpatched parser rejects the synthetic 3.18 multiple-zone fixture;
  the patched parser passes **14 host regression tests**, both normally and
  with UndefinedBehaviorSanitizer. Tests extract the actual parser from the
  supplied source; only file I/O and Android logging are stubbed.
- Tests cover legacy and modern formats, multiple nodes/zones, empty leading
  zones, reserve/file-page totals, malformed/truncated input, and array limits.
- Kconfig successfully resolves all changed A3 settings. The existing legacy
  choice/stack-protector warnings remain; no new Kconfig warnings appeared.
- Patch-runner shell syntax and tracked-file whitespace checks pass.

**Not validated here:** a full Android/ARM64 kernel build, a flashed boot image,
Bluetooth pairing/audio, or live memory-pressure behavior. The playground does
not contain a full Android build tree or a connected handset.

## Applying only the new LMKD patch to an already-patched Android tree

From the full LineageOS source root, set `PATCHES` to the absolute path of this
updated patch bundle. Do not rerun every existing patch on an already-patched
source tree.

```sh
PATCHES=/absolute/path/to/android_vendor_samsung_universal7870-common-patches
PATCH="$PATCHES/system/memory/lmkd/0001-Support-zoneinfo-without-per-node-statistics.patch"
git -C system/memory/lmkd apply --check "$PATCH"
git -C system/memory/lmkd am "$PATCH"
python3 "$PATCHES/tests/test_lmkd_zoneinfo.py" \
    --source system/memory/lmkd/lmkd.cpp --sanitize -v
```

Rebuild the A3 ROM including its **boot image/kernel** and LMKD. A system-only
update cannot apply the defconfig fix. Follow the device's normal flashing
procedure; no partition or modem/NV data changes are required by these fixes.

## Next on-device checks

1. Confirm the flashed kernel has `CONFIG_RT_GROUP_SCHED` unset and cpuset/blkio
   enabled; `/proc/config.gz` is available when IKCONFIG_PROC is enabled.
2. Confirm `/proc/sys/kernel/sched_rt_runtime_us` is still `950000`, and
   `/dev/cpuset/foreground/cgroup.procs` and `/dev/blkio/cgroup.procs` exist.
3. Enable Bluetooth in Settings. Check that its process remains alive without
   the scheduler abort; then test pairing, A2DP, and a headset call. A stable
   process alone does not establish that the QCA transport/firmware works.
4. Exercise ordinary app switching while collecting a new boot/runtime log.
   The recurring LMKD parse errors should disappear. Save the device's actual
   `/proc/zoneinfo` alongside the next log if they remain.
5. Recheck calls, SMS, and mobile data to guard the now-working RIL.

## Other observations, not patched in this pass

- **Camera:** HAL1 candidates are rejected, but both `device@3.3/legacy/*`
  cameras are then enumerated and the provider reports two devices. Those HAL1
  messages alone do not prove the cameras are broken. Test front/rear preview,
  capture, and video before changing the wrapper or advertised API versions.
- **Wi-Fi:** association, DHCP, and default-network selection occur. Unsupported
  optional HAL features and BPF/network-statistics warnings are not equivalent
  to a broken Wi-Fi HAL. Traffic accounting remains a separate legacy-kernel
  concern.
- **Audio:** the OSS amplifier logs a zero-impedance/calibration error. Speaker
  output and call audio still need an explicit test; do not blindly write
  calibration or NV data based on this boot log.
- **Media:** software OMX configuration entries refer to unavailable legacy
  codec libraries. Check actual playback/recording and Codec2 fallback before
  adding obsolete OMX modules.
- **Security:** the supplied boot is SELinux-permissive. AVC entries should be
  addressed before an enforcing release, not treated as the cause of this
  permissive-boot scheduler failure.
