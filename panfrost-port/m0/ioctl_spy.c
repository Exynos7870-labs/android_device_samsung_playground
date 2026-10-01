/*
 * ioctl_spy.c -- LD_PRELOAD shim that dumps the Mali kbase atoms the vendor GL
 * driver (/vendor/lib64/egl/libGLES_mali.so) submits on this device.
 *
 * WHY: a Panfrost port for Midgard-on-kbase needs the exact core_req values and
 * dependency shapes T830's vendor driver uses.  The JimVulkan Bifrost port
 * recovered its values the same way ("core_req values are TAKEN VERBATIM from
 * the vendor driver's own atoms"): on a G76 they came out as 0x4e (vertex/tiler)
 * and 0x8001 (fragment).  We must not guess them for T830.
 *
 * WHAT IT DOES: interposes ioctl(2).  When the request is KBASE_IOCTL_JOB_SUBMIT
 * (_IOW(0x80, 2, 16-byte struct) = 0x40108002) it reads the atom array out of our
 * own address space (the ioctl argument is a pointer in this process, so no
 * ptrace is needed) and appends a hex dump to the log file.
 *
 * The 64-byte atom layout is deliberately NOT decoded here: this kernel (r28p0 +
 * Samsung SRUK) and newer ARM kbase disagree about field offsets, and dumping raw
 * keeps the same capture useful for both.  Decode with decode_atoms.py.
 *
 * Build (aarch64; build the 32-bit variant for 32-bit processes):
 *   $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang \
 *       -shared -fPIC -O2 -o libkbase_spy.so ioctl_spy.c -ldl
 *
 * Run (a standalone GLES test binary is the easiest target):
 *   adb push libkbase_spy.so /data/local/tmp/
 *   adb shell "LD_PRELOAD=/data/local/tmp/libkbase_spy.so \
 *              KBASE_SPY_OUT=/data/local/tmp/atoms.txt /data/local/tmp/glprobe"
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef int (*ioctl_fn)(int, unsigned long, ...);
static ioctl_fn real_ioctl;

#define KBASE_TYPE 0x80u
#define KBASE_JOB_SUBMIT_NR 2u
#define KBASE_IOCTL_JOB_SUBMIT 0x40108002ul /* _IOW(0x80, 2, 16-byte payload) */

struct kbase_ioctl_job_submit {
	uint64_t addr;
	uint32_t nr_atoms;
	uint32_t stride;
};

static FILE *spy_out;
static int spy_disabled;

static FILE *get_out(void)
{
	if (!spy_out) {
		const char *p = getenv("KBASE_SPY_OUT");
		spy_out = fopen(p ? p : "/data/local/tmp/kbase_atoms.txt", "a");
		if (spy_out)
			setvbuf(spy_out, NULL, _IOLBF, 0);
	}
	if (!spy_out)
		spy_disabled = 1;
	return spy_out;
}

static void dump_hex(FILE *f, const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		fprintf(f, "%02x", p[i]);
}

static void spy_job_submit(int fd, void *arg, long ret)
{
	struct kbase_ioctl_job_submit sub;
	FILE *f;

	if (spy_disabled || !arg)
		return;
	memcpy(&sub, arg, sizeof(sub));

	/* Sanity: at most 64 atoms, 8..256 bytes stride, non-null array. */
	if (sub.nr_atoms == 0 || sub.nr_atoms > 64 || sub.stride < 8 ||
	    sub.stride > 256 || !sub.addr)
		return;

	f = get_out();
	if (!f)
		return;

	/* The array is a legit userspace array the driver just handed to ioctl(). */
	const uint8_t *base = (const uint8_t *)(uintptr_t)sub.addr;

	fprintf(f, "SUBMIT fd=%d ret=%ld nr=%u stride=%u addr=%p\n", fd, ret,
		sub.nr_atoms, sub.stride, (void *)(uintptr_t)sub.addr);
	for (uint32_t i = 0; i < sub.nr_atoms; i++) {
		const uint8_t *a = base + (size_t)i * sub.stride;
		fprintf(f, "  ATOM %u: ", i);
		dump_hex(f, a, sub.stride);
		fputc('\n', f);
	}
}

int ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	void *arg;

	if (!real_ioctl) {
		real_ioctl = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
		if (!real_ioctl) {
			errno = ENOSYS;
			return -1;
		}
	}

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);

	/* Match on dir+type+nr, ignoring the size field (dir == _IOC_WRITE == 1). */
	if ((request & 0xc000fffful) ==
	    ((1ul << 30) | (KBASE_TYPE << 8) | KBASE_JOB_SUBMIT_NR)) {
		int ret = real_ioctl(fd, request, arg);
		spy_job_submit(fd, arg, ret);
		return ret;
	}

	return real_ioctl(fd, request, arg);
}
