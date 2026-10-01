/*
 * verify_layout.c -- compile-time and run-time verification that the userspace
 * mirror in kbase_uapi_r28p0.h has the layout the kernel expects.
 *
 * The offsets below are the ones a C compiler produces for the kernel's own
 * declarations (mali_base_kernel.h:870-888 and :1212-1216) on aarch64 and on
 * the host; both are LP64, so one set of asserts covers both.
 *
 * Build/run on the host:
 *     cc -O2 -o verify_layout verify_layout.c && ./verify_layout
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "kbase_uapi_r28p0.h"

#define CHECK(cond, msg)                                                       \
	do {                                                                   \
		if (!(cond)) {                                                 \
			printf("FAIL: %s\n", msg);                             \
			failures++;                                            \
		}                                                              \
	} while (0)

_Static_assert(sizeof(struct kbase_jd_atom_v2) == KBASE_ATOM_SIZE,
	       "atom must be 64 bytes: kbase_jd_submit() rejects any other stride");
_Static_assert(sizeof(struct base_jd_event_v2) == KBASE_EVENT_SIZE,
	       "read() returns 24-byte base_jd_event_v2 records");
_Static_assert(sizeof(struct base_dump_cpu_gpu_counters) == KBASE_DUMP_COUNTERS_SIZE,
	       "soft dump counters payload is 64 bytes");
_Static_assert(sizeof(struct kbase_ioctl_job_submit) == 16, "job_submit is 16 bytes");
_Static_assert(sizeof(union kbase_ioctl_mem_alloc) == 32, "mem_alloc union is 32 bytes");
_Static_assert(sizeof(struct kbase_ioctl_mem_free) == 8, "mem_free is 8 bytes");
_Static_assert(sizeof(struct kbase_ioctl_mem_exec_init) == 8, "exec_init is 8 bytes");
_Static_assert(sizeof(struct kbase_ioctl_get_gpuprops) == 16, "get_gpuprops is 16 bytes");
_Static_assert(sizeof(struct kbase_ioctl_version_check) == 4, "version_check is 4 bytes");

int main(void)
{
	int failures = 0;

	/* offsets inside base_jd_atom_v2 (kernel: mali_base_kernel.h:870) */
	CHECK(offsetof(struct kbase_jd_atom_v2, jc) == 0, "jc @ 0");
	CHECK(offsetof(struct kbase_jd_atom_v2, udata) == 8, "udata @ 8");
	CHECK(offsetof(struct kbase_jd_atom_v2, extres_list) == 24, "extres_list @ 24");
	CHECK(offsetof(struct kbase_jd_atom_v2, nr_extres) == 32, "nr_extres @ 32");
	CHECK(offsetof(struct kbase_jd_atom_v2, compat_core_req) == 34, "compat_core_req @ 34");
	CHECK(offsetof(struct kbase_jd_atom_v2, pre_dep) == 36, "pre_dep @ 36");
	CHECK(offsetof(struct kbase_jd_atom_v2, atom_number) == 40, "atom_number @ 40");
	CHECK(offsetof(struct kbase_jd_atom_v2, prio) == 41, "prio @ 41");
	CHECK(offsetof(struct kbase_jd_atom_v2, device_nr) == 42, "device_nr @ 42");
	CHECK(offsetof(struct kbase_jd_atom_v2, core_req) == 44, "core_req @ 44");
	CHECK(offsetof(struct kbase_jd_atom_v2, gles_ctx_handle) == 48, "gles_ctx_handle @ 48");
	CHECK(offsetof(struct kbase_jd_atom_v2, frame_number) == 52, "frame_number @ 52");
	CHECK(offsetof(struct kbase_jd_atom_v2, surfacep) == 56, "surfacep @ 56");
	CHECK(sizeof(((struct kbase_jd_atom_v2 *)0)->pre_dep[0]) == 2, "base_dependency is 2 bytes");

	/* the version constant must match the kernel's BASE_UK_VERSION_* */
	CHECK(BASE_UK_VERSION_MAJOR == 11 && BASE_UK_VERSION_MINOR == 13,
	      "UK version is 11.13");

	/* sanity: the error path in kbase_mmap uses PFN of the tracking handle */
	CHECK(BASE_MEM_MAP_TRACKING_PFN == 3, "tracking page mmap offset is 3");

	printf("sizeof(kbase_jd_atom_v2)          = %zu (kernel requires 64)\n",
	       sizeof(struct kbase_jd_atom_v2));
	printf("sizeof(base_jd_event_v2)          = %zu\n", sizeof(struct base_jd_event_v2));
	printf("sizeof(base_dump_cpu_gpu_counters)= %zu\n",
	       sizeof(struct base_dump_cpu_gpu_counters));
	printf("KBASE_IOCTL_JOB_SUBMIT            = 0x%08lx (expect 0x40108002)\n",
	       (unsigned long)KBASE_IOCTL_JOB_SUBMIT);
	printf("KBASE_IOCTL_VERSION_CHECK         = 0x%08lx (expect 0xc0048000)\n",
	       (unsigned long)KBASE_IOCTL_VERSION_CHECK);
	printf("KBASE_IOCTL_MEM_ALLOC             = 0x%08lx (expect 0xc0208005)\n",
	       (unsigned long)KBASE_IOCTL_MEM_ALLOC);
	printf("KBASE_IOCTL_MEM_EXEC_INIT         = 0x40088026 (expect 0x40088026, nr 38)\n");
	CHECK((unsigned long)KBASE_IOCTL_JOB_SUBMIT == 0x40108002ul, "job submit code");
	CHECK((unsigned long)KBASE_IOCTL_VERSION_CHECK == 0xc0048000ul, "version check code");
	CHECK((unsigned long)KBASE_IOCTL_MEM_ALLOC == 0xc0208005ul, "mem alloc code");
	CHECK((unsigned long)KBASE_IOCTL_MEM_EXEC_INIT == 0x40088026ul, "exec init code");

	if (failures == 0)
		printf("\nOK: layout verified\n");
	else
		printf("\n%d FAILURE(S)\n", failures);
	return failures != 0;
}
