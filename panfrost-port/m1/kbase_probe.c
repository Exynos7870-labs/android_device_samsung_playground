/*
 * kbase_probe.c -- M1: first contact with kbase from a plain userspace binary.
 *
 * This is the step that has to work before any Mesa/Panfrost work makes sense:
 * a standalone program that talks to /dev/mali0 with THIS device's r28p0 UAPI
 * and gets a job completed through the kernel.
 *
 * Sequence (each step prints OK/FAIL and the raw values):
 *   1. open /dev/mali0
 *   2. VERSION_CHECK   (11.13)
 *   3. mmap the tracking page (kbase refuses MEM_ALLOC before this)
 *   4. SET_FLAGS       (0)
 *   5. GET_GPUPROPS    (size query, then the buffer; raw key/value dump)
 *   6. MEM_ALLOC       (1 page, SAME_VA, CPU RW, GPU RW)
 *   7. CPU write/read-back through the mapping
 *   8. MEM_EXEC_INIT   (ioctl 38, the one the fork's backend had to measure)
 *   9. JOB_SUBMIT      (one SOFT_DUMP_CPU_GPU_TIME atom -- no GPU code needed:
 *                       the kernel writes timestamps into the atom's jc pointer)
 *  10. read()          completion event (24 bytes, DONE == 0x01)
 *  11. MEM_FREE + cleanup
 *
 * Build: see Makefile (host sim or NDK).
 * Run:   ./kbase_probe            (device: root not required)
 *
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kb_sys.h"
#include "../uapi/kbase_uapi_r28p0.h"

#define PAGE_SIZE_ 4096

static int failures;

static void ok(const char *msg) { printf("  [ OK ] %s\n", msg); }
static void fail(const char *msg)
{
	printf("  [FAIL] %s (errno=%d %s)\n", msg, errno, strerror(errno));
	failures++;
}

/* ------------------------------------------------------------------ steps */

static int step_version_check(int fd)
{
	struct kbase_ioctl_version_check vc = {
		.major = BASE_UK_VERSION_MAJOR,
		.minor = BASE_UK_VERSION_MINOR,
	};

	printf("1. VERSION_CHECK (offering %u.%u)\n", vc.major, vc.minor);
	if (kb_ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &vc) != 0) {
		fail("VERSION_CHECK ioctl");
		return -1;
	}
	printf("     kernel accepted; negotiated %u.%u\n", vc.major, vc.minor);
	if (vc.major != BASE_UK_VERSION_MAJOR) {
		fail("major version mismatch");
		return -1;
	}
	ok("version handshake");
	return 0;
}

static void *step_tracking_page(int fd)
{
	printf("2. mmap tracking page (offset %llu)\n",
	       (unsigned long long)BASE_MEM_MAP_TRACKING_PFN);
	void *p = kb_mmap(NULL, PAGE_SIZE_, PROT_NONE, MAP_SHARED, fd,
			  (off_t)BASE_MEM_MAP_TRACKING_PFN);
	if (p == MAP_FAILED) {
		fail("tracking-page mmap (required before MEM_ALLOC)");
		return NULL;
	}
	ok("tracking page mapped");
	return p;
}

static int step_set_flags(int fd)
{
	struct kbase_ioctl_set_flags f = { .create_flags = 0 };

	printf("3. SET_FLAGS (create_flags=0)\n");
	if (kb_ioctl(fd, KBASE_IOCTL_SET_FLAGS, &f) != 0) {
		fail("SET_FLAGS ioctl");
		return -1;
	}
	ok("context flags set");
	return 0;
}

static void step_gpuprops(int fd, uint8_t **props_out, uint32_t *size_out)
{
	struct kbase_ioctl_get_gpuprops gp = { 0 };

	printf("4. GET_GPUPROPS\n");
	int sz = kb_ioctl(fd, KBASE_IOCTL_GET_GPUPROPS, &gp);
	if (sz <= 0) {
		fail("GPU props size query");
		return;
	}
	printf("     buffer size = %d bytes\n", sz);
	uint8_t *buf = calloc(1, (size_t)sz);
	if (!buf) {
		fail("calloc props buffer");
		return;
	}
	gp.buffer = (uint64_t)(uintptr_t)buf;
	gp.size = (uint32_t)sz;
	gp.flags = 0;
	int got = kb_ioctl(fd, KBASE_IOCTL_GET_GPUPROPS, &gp);
	if (got <= 0) {
		fail("GPU props fetch");
		free(buf);
		return;
	}
	/* dump the raw key/value stream; naming comes from the M0 capture and the
	 * fork's enum kbase_gpuprop -- do not guess names here */
	uint32_t off = 0;
	int n = 0;
	printf("     raw property stream (%d bytes):\n", got);
	while (off + 4 <= (uint32_t)got && n < 24) {
		uint32_t key;
		memcpy(&key, buf + off, 4);
		off += 4;
		uint32_t vsize = 1u << (key & 3);
		char val[32] = "?";
		if (key == 0)
			break;
		if (off + vsize > (uint32_t)got)
			break;
		switch (vsize) {
		case 1:
			snprintf(val, sizeof(val), "%u", buf[off]);
			break;
		case 2: {
			uint16_t v;
			memcpy(&v, buf + off, 2);
			snprintf(val, sizeof(val), "%u", v);
			break;
		}
		case 4: {
			uint32_t v;
			memcpy(&v, buf + off, 4);
			snprintf(val, sizeof(val), "0x%08x (%u)", v, v);
			break;
		}
		case 8: {
			uint64_t v;
			memcpy(&v, buf + off, 8);
			snprintf(val, sizeof(val), "0x%016" PRIx64, v);
			break;
		}
		}
		printf("       key 0x%08x (size %u) = %s\n", key, vsize, val);
		off += vsize;
		n++;
	}
	*props_out = buf;
	*size_out = (uint32_t)got;
	ok("GPU props read (raw)");
}

static void *step_mem_alloc(int fd, uint64_t *gpu_va_out)
{
	union kbase_ioctl_mem_alloc m;

	printf("5. MEM_ALLOC (1 page, SAME_VA, CPU RW, GPU RW)\n");
	memset(&m, 0, sizeof(m));
	m.in.va_pages = 1;
	m.in.commit_pages = 1;
	m.in.extent = 0;
	m.in.flags = BASE_MEM_SAME_VA | BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
		     BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR | BASE_MEM_CACHED_CPU;
	if (kb_ioctl(fd, KBASE_IOCTL_MEM_ALLOC, &m) != 0) {
		fail("MEM_ALLOC");
		return NULL;
	}
	printf("     gpu_va = 0x%016" PRIx64 ", flags = 0x%016" PRIx64 "\n",
	       m.out.gpu_va, m.out.flags);
	*gpu_va_out = m.out.gpu_va;
	ok("allocation created");
	return (void *)(uintptr_t)m.out.gpu_va;
}

static void *step_map_and_check(int fd, void *gpu_va)
{
	printf("6. mmap the region and check CPU access\n");
	void *cpu = kb_mmap(NULL, PAGE_SIZE_, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
			    (off_t)((uintptr_t)gpu_va >> 12));
	if (cpu == MAP_FAILED) {
		fail("region mmap");
		return NULL;
	}
	printf("     cpu va = %p (gpu va = %p)\n", cpu, gpu_va);
	memset(cpu, 0x5a, 64);
	if (*(volatile uint8_t *)cpu != 0x5a) {
		fail("write/read-back through the mapping");
		return NULL;
	}
	ok("CPU mapping works");
	return cpu;
}

static void step_exec_init(int fd)
{
	struct kbase_ioctl_mem_exec_init e = { .va_pages = 1 };

	printf("7. MEM_EXEC_INIT (ioctl 38, va_pages=1)\n");
	if (kb_ioctl(fd, KBASE_IOCTL_MEM_EXEC_INIT, &e) != 0) {
		/* Not fatal for M1: the vendor blob issues it, but our soft job
		 * does not need the EXEC_VA zone.  Report and continue. */
		printf("     note: rejected (%s) -- non-fatal for this probe\n",
		       strerror(errno));
		return;
	}
	ok("EXEC_VA zone initialised");
}

static struct base_dump_cpu_gpu_counters *counters;

static int step_soft_job(int fd)
{
	printf("8. JOB_SUBMIT (SOFT_DUMP_CPU_GPU_TIME, 1 atom)\n");

	if (posix_memalign((void **)&counters, 64, sizeof(*counters)) != 0) {
		fail("aligned counters buffer");
		return -1;
	}
	memset(counters, 0, sizeof(*counters));

	static struct kbase_jd_atom_v2 atom __attribute__((aligned(64)));
	memset(&atom, 0, sizeof(atom));
	atom.jc = (uint64_t)(uintptr_t)counters; /* soft jobs take a user pointer */
	atom.udata.blob[0] = 0xfeedfacecafebeefull;
	atom.udata.blob[1] = 0x0123456789abcdefull;
	atom.atom_number = 1;
	atom.prio = BASE_JD_PRIO_MEDIUM;
	atom.device_nr = 0;
	atom.core_req = BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME;

	struct kbase_ioctl_job_submit js = {
		.addr = (uint64_t)(uintptr_t)&atom,
		.nr_atoms = 1,
		.stride = sizeof(atom),
	};
	printf("     atom: jc=%p core_req=0x%x atom_number=%u udata=0x%016" PRIx64 "\n",
	       counters, atom.core_req, atom.atom_number, atom.udata.blob[0]);
	if (kb_ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &js) != 0) {
		fail("JOB_SUBMIT");
		return -1;
	}
	ok("submit accepted");
	return 0;
}

static int step_read_events(int fd)
{
	printf("9. read() completion events\n");
	uint8_t buf[KBASE_EVENT_SIZE * 8];
	int total = 0;

	for (int tries = 0; tries < 20; tries++) {
		if (!kb_wait_readable(fd, 500)) {
			printf("     no event within %d ms\n", 500);
			break;
		}
		long n = kb_read(fd, buf, sizeof(buf));
		if (n == -EPIPE) {
			printf("     read() -> EPIPE (DRV_TERMINATED)\n");
			break;
		}
		if (n < 0) {
			if (errno == EAGAIN)
				continue;
			fail("read()");
			break;
		}
		for (long off = 0; off + KBASE_EVENT_SIZE <= n; off += KBASE_EVENT_SIZE) {
			struct base_jd_event_v2 ev;
			memcpy(&ev, buf + off, sizeof(ev));
			total++;
			printf("     event: code=0x%02x atom=%u udata=0x%016" PRIx64
			       " 0x%016" PRIx64 "%s\n",
			       ev.event_code, ev.atom_number, ev.udata.blob[0],
			       ev.udata.blob[1],
			       ev.event_code == BASE_JD_EVENT_DONE ? " (DONE)" : "");
			if (ev.event_code == BASE_JD_EVENT_DONE &&
			    ev.udata.blob[0] != 0xfeedfacecafebeefull) {
				printf("       WARNING: udata does not echo ours -- "
				       "atom layout mismatch?\n");
				failures++;
			}
		}
		if (total > 0)
			break;
	}
	if (total == 0) {
		fail("no completion event received");
		return -1;
	}
	ok("completion received");

	printf("     counters buffer: system_time=%" PRIu64 " cycle_counter=%" PRIu64
	       " sec=%" PRIu64 " usec=%u\n",
	       counters->system_time, counters->cycle_counter, counters->sec,
	       counters->usec);
	if (counters->system_time == 0 && counters->cycle_counter == 0)
		printf("     note: counters are zero (GPU may be idle/powered down)\n");
	return 0;
}

static void step_free(int fd, uint64_t gpu_va)
{
	struct kbase_ioctl_mem_free f = { .gpu_addr = gpu_va };

	printf("10. MEM_FREE\n");
	if (kb_ioctl(fd, KBASE_IOCTL_MEM_FREE, &f) != 0)
		printf("     note: MEM_FREE rejected (%s)\n", strerror(errno));
	else
		ok("region freed");
}

/* ------------------------------------------------------------------- main */

int main(void)
{
	printf("kbase_probe -- r28p0 UAPI bring-up (M1)\n");
	printf("atom layout under test: %zu bytes, core_req @ %zu, jc @ %zu\n\n",
	       sizeof(struct kbase_jd_atom_v2),
	       offsetof(struct kbase_jd_atom_v2, core_req),
	       offsetof(struct kbase_jd_atom_v2, jc));

	int fd = kb_open_kbase();
	if (fd < 0) {
		fail("open /dev/mali0");
		return 1;
	}
	ok("opened /dev/mali0");

	if (step_version_check(fd) != 0)
		goto out;

	void *tracking = step_tracking_page(fd);
	if (!tracking)
		goto out;

	if (step_set_flags(fd) != 0)
		goto out;

	uint8_t *props = NULL;
	uint32_t props_size = 0;
	step_gpuprops(fd, &props, &props_size);

	uint64_t gpu_va = 0;
	void *cpu = step_mem_alloc(fd, &gpu_va);
	if (!cpu)
		goto out;
	void *mapping = step_map_and_check(fd, cpu);
	if (!mapping)
		goto out;

	step_exec_init(fd);

	if (step_soft_job(fd) == 0)
		step_read_events(fd);

	step_free(fd, gpu_va);

	free(props);
	free(counters);

out:
	kb_close(fd);
	printf("\n%s (%d failure%s)\n", failures ? "BRING-UP FAILED" : "BRING-UP OK",
	       failures, failures == 1 ? "" : "s");
	return failures != 0;
}
