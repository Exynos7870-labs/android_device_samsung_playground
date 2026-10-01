/*
 * host_sim.c -- a simulated kbase for running kbase_probe on a workstation.
 *
 * Purpose: exercise the whole M1 sequence (control flow, ioctl structs, atom
 * layout, event parsing) before spending device time.  It is NOT a kbase
 * emulator: it answers the ioctls the probe uses with plausible values and, for
 * JOB_SUBMIT, decodes the atom array with an INDEPENDENT declaration of the
 * kernel layout -- so a field-offset mistake in the client header shows up as a
 * bogus decode here rather than as a silent failure on the device.
 *
 * Build: make host        (links this with kbase_probe.c -DHOST_SIM)
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "kb_sys.h"
#include "../uapi/kbase_uapi_r28p0.h"

/* ---- kernel-side view of base_jd_atom_v2 (independent re-declaration) ---- */
struct sim_atom {
	uint64_t jc;
	uint64_t udata[2];
	uint64_t extres_list;
	uint16_t nr_extres;
	uint16_t compat_core_req;
	uint8_t dep[2][2];
	uint8_t atom_number;
	uint8_t prio;
	uint8_t device_nr;
	uint8_t padding;
	uint32_t core_req;
	uint32_t sruk_gles_ctx;
	uint32_t sruk_frame;
	uint64_t sruk_surface;
};
_Static_assert(sizeof(struct sim_atom) == 64, "sim must use the same 64-byte stride");

/* ------------------------------- queued events ---------------------------- */

static uint8_t event_q[16 * KBASE_EVENT_SIZE];
static size_t event_q_len;

static void queue_event(uint32_t code, uint8_t atom_number, const uint64_t udata[2])
{
	if (event_q_len + KBASE_EVENT_SIZE > sizeof(event_q))
		return;
	struct base_jd_event_v2 ev = { .event_code = code, .atom_number = atom_number };
	memcpy(ev.udata.blob, udata, sizeof(ev.udata.blob));
	memcpy(event_q + event_q_len, &ev, sizeof(ev));
	event_q_len += sizeof(ev);
}

/* -------------------------------- the "fd" -------------------------------- */

#define SIM_FD 42

int kb_open_kbase(void)
{
	printf("  [sim] open(\"/dev/mali0\") -> fd %d\n", SIM_FD);
	return SIM_FD;
}

void kb_close(int fd) { printf("  [sim] close(%d)\n", fd); }

void *kb_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
	(void)addr;
	(void)flags;
	(void)fd;
	if (offset == (off_t)BASE_MEM_MAP_TRACKING_PFN) {
		printf("  [sim] mmap tracking page (offset %lld)\n", (long long)offset);
		return mmap(NULL, len, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	}
	printf("  [sim] mmap region at offset 0x%llx (= gpu_va 0x%llx), len %zu\n",
	       (unsigned long long)offset, (unsigned long long)offset << 12, len);
	return mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

int kb_munmap(void *addr, size_t len) { return munmap(addr, len); }

int kb_ioctl(int fd, unsigned long request, void *arg)
{
	(void)fd; /* single simulated device */
	/* The encoding is the same on the host (sys/ioctl.h), so match directly. */
	if (request == KBASE_IOCTL_VERSION_CHECK) {
		struct kbase_ioctl_version_check *vc = arg;
		printf("  [sim] VERSION_CHECK offering %u.%u\n", vc->major, vc->minor);
		if (vc->major != 11) {
			errno = EINVAL;
			return -1;
		}
		vc->minor = 13;
		return 0;
	}
	if (request == KBASE_IOCTL_SET_FLAGS) {
		printf("  [sim] SET_FLAGS create_flags=0x%x\n",
		       ((struct kbase_ioctl_set_flags *)arg)->create_flags);
		return 0;
	}
	if (request == KBASE_IOCTL_GET_GPUPROPS) {
		struct kbase_ioctl_get_gpuprops *gp = arg;
		/* Synthetic stream: real keys come from the M0 capture / the fork's
		 * enum kbase_gpuprop.  Shape follows the kernel's documented format
		 * (u32 key with size in bits 0..1, then the value). */
		static uint8_t stream[] = {
			/* The low two bits of a key encode its value size:
			 * 0 = u8, 1 = u16, 2 = u32, 3 = u64 (mali_kbase_ioctl.h:135+). */
			/* key 0x02 (u32): product id 0x830 -> Mali-T830 -> Panfrost arch 5 */
			0x02, 0x00, 0x00, 0x00, 0x30, 0x08, 0x00, 0x00,
			/* key 0x06 (u32): major revision */
			0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			/* key 0x0a (u32): minor revision */
			0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			/* key 0x0f (u64): gpu features word */
			0x0f, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		};
		if (gp->size == 0)
			return (int)sizeof(stream);
		if (gp->size < sizeof(stream)) {
			errno = EINVAL;
			return -1;
		}
		memcpy((void *)(uintptr_t)gp->buffer, stream, sizeof(stream));
		return (int)sizeof(stream);
	}
	if (request == KBASE_IOCTL_MEM_ALLOC) {
		union kbase_ioctl_mem_alloc *m = arg;
		printf("  [sim] MEM_ALLOC va_pages=%llu commit=%llu flags=0x%llx\n",
		       (unsigned long long)m->in.va_pages,
		       (unsigned long long)m->in.commit_pages,
		       (unsigned long long)m->in.flags);
		m->out.flags = m->in.flags;
		m->out.gpu_va = 0x100000000ull; /* typical same-VA address base */
		return 0;
	}
	if (request == KBASE_IOCTL_MEM_EXEC_INIT) {
		printf("  [sim] MEM_EXEC_INIT (ioctl 38) va_pages=%llu\n",
		       (unsigned long long)((struct kbase_ioctl_mem_exec_init *)arg)->va_pages);
		return 0;
	}
	if (request == KBASE_IOCTL_MEM_FREE) {
		printf("  [sim] MEM_FREE gpu_addr=0x%llx\n",
		       (unsigned long long)((struct kbase_ioctl_mem_free *)arg)->gpu_addr);
		return 0;
	}
	if (request == KBASE_IOCTL_JOB_SUBMIT) {
		struct kbase_ioctl_job_submit *js = arg;
		printf("  [sim] JOB_SUBMIT nr=%u stride=%u addr=0x%llx\n", js->nr_atoms,
		       js->stride, (unsigned long long)js->addr);
		if (js->stride != sizeof(struct sim_atom)) {
			printf("  [sim] *** stride mismatch: kernel would reject ***\n");
			errno = EINVAL;
			return -1;
		}
		for (uint32_t i = 0; i < js->nr_atoms; i++) {
			const struct sim_atom *a =
				(const struct sim_atom *)(uintptr_t)(js->addr +
								     (uint64_t)i * js->stride);
			printf("  [sim]   atom#%u core_req=0x%x jc=0x%llx udata=0x%llx "
			       "dep0=(%u,%u) prio=%u\n",
			       a->atom_number, a->core_req, (unsigned long long)a->jc,
			       (unsigned long long)a->udata[0], a->dep[0][0], a->dep[0][1],
			       a->prio);

			if ((a->core_req & 0x200u) &&
			    (a->core_req & 0x7fu) == 0x1u /* SOFT_DUMP_CPU_GPU_TIME */) {
				if (a->jc & 63) {
					printf("  [sim] *** jc not cache-line aligned: "
					       "kernel would reject ***\n");
					errno = EINVAL;
					return -1;
				}
				/* emulate kbase_dump_cpu_gpu_time() */
				struct base_dump_cpu_gpu_counters *c =
					(void *)(uintptr_t)a->jc;
				c->system_time = 0x1122334455667788ull;
				c->cycle_counter = 0x99aabbccddeeff00ull;
				c->sec = 1700000000;
				c->usec = 123456;
				queue_event(BASE_JD_EVENT_DONE, a->atom_number, a->udata);
			} else {
				printf("  [sim]   (not a soft dump atom; no emulation)\n");
			}
		}
		return 0;
	}
	printf("  [sim] unhandled ioctl 0x%lx\n", request);
	errno = ENOTTY;
	return -1;
}

long kb_read(int fd, void *buf, size_t len)
{
	(void)fd;
	if (len < KBASE_EVENT_SIZE || event_q_len == 0) {
		errno = EAGAIN;
		return -1;
	}
	size_t n = event_q_len < len ? event_q_len : len;
	n -= n % KBASE_EVENT_SIZE;
	memcpy(buf, event_q, n);
	event_q_len -= n;
	memmove(event_q, event_q + n, event_q_len);
	return (long)n;
}

int kb_wait_readable(int fd, int timeout_ms)
{
	(void)fd;
	(void)timeout_ms;
	return event_q_len > 0;
}
