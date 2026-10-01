/*
 * kbase_uapi_r28p0.h -- userspace mirror of the Mali kbase UAPI as this
 * device's kernel actually implements it.
 *
 * PROVENANCE (authoritative, all read out of this repository):
 *   drivers/gpu/arm/t83x/r28p0/mali_kbase_ioctl.h    ioctl numbers + structs
 *   drivers/gpu/arm/t83x/r28p0/mali_base_kernel.h    atom/flags/event structs
 *   drivers/gpu/arm/t83x/r28p0/mali_kbase_jd.c       kbase_jd_submit() rules
 *   drivers/gpu/arm/t83x/r28p0/mali_kbase_core_linux.c  file_operations, read()
 *   drivers/gpu/arm/t83x/r28p0/mali_kbase_mem_linux.c   mmap offset convention
 *   drivers/gpu/arm/t83x/r28p0/mali_kbase_softjobs.c    soft job semantics
 *
 * WHY this exists: the JimVulkan panfrost kbase backend (MIT) recovered the
 * UAPI of a NEWER kbase by measurement.  We do not have to: the kernel source is
 * in-tree, so our offsets are read, not guessed.  The two disagree -- in
 * particular the 64-byte base_jd_atom_v2 layout and the Samsung SRUK tail --
 * which is exactly why the port needs this file.
 *
 * Kernel revision: r28p0, UK 11.13 (BASE_UK_VERSION_MAJOR/MINOR below).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef KBASE_UAPI_R28P0_H
#define KBASE_UAPI_R28P0_H

#include <stdint.h>
#include <sys/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ ioctls */

#define KBASE_IOCTL_TYPE 0x80

/* 11.13 */
#define BASE_UK_VERSION_MAJOR 11
#define BASE_UK_VERSION_MINOR 13

struct kbase_ioctl_version_check {
	uint16_t major;
	uint16_t minor;
};
#define KBASE_IOCTL_VERSION_CHECK \
	_IOWR(KBASE_IOCTL_TYPE, 0, struct kbase_ioctl_version_check)

struct kbase_ioctl_set_flags {
	uint32_t create_flags;
};
#define KBASE_IOCTL_SET_FLAGS \
	_IOW(KBASE_IOCTL_TYPE, 1, struct kbase_ioctl_set_flags)

struct kbase_ioctl_job_submit {
	uint64_t addr;    /* user pointer to an array of kbase_jd_atom_v2 */
	uint32_t nr_atoms;
	uint32_t stride;  /* must equal sizeof(kbase_jd_atom_v2) == 64 */
};
#define KBASE_IOCTL_JOB_SUBMIT \
	_IOW(KBASE_IOCTL_TYPE, 2, struct kbase_ioctl_job_submit)

struct kbase_ioctl_get_gpuprops {
	uint64_t buffer;
	uint32_t size;   /* 0 -> query size; else must be >= prop_buffer_size */
	uint32_t flags;  /* must be 0 */
};
#define KBASE_IOCTL_GET_GPUPROPS \
	_IOW(KBASE_IOCTL_TYPE, 3, struct kbase_ioctl_get_gpuprops)

union kbase_ioctl_mem_alloc {
	struct {
		uint64_t va_pages;
		uint64_t commit_pages;
		uint64_t extent;
		uint64_t flags;
	} in;
	struct {
		uint64_t flags;
		uint64_t gpu_va;
	} out;
};
#define KBASE_IOCTL_MEM_ALLOC \
	_IOWR(KBASE_IOCTL_TYPE, 5, union kbase_ioctl_mem_alloc)

union kbase_ioctl_mem_query {
	struct {
		uint64_t gpu_addr;
		uint64_t query;
	} in;
	struct {
		uint64_t value;
	} out;
};
#define KBASE_IOCTL_MEM_QUERY \
	_IOWR(KBASE_IOCTL_TYPE, 6, union kbase_ioctl_mem_query)
#define KBASE_MEM_QUERY_COMMIT_SIZE ((uint64_t)1)
#define KBASE_MEM_QUERY_VA_SIZE     ((uint64_t)2)
#define KBASE_MEM_QUERY_FLAGS       ((uint64_t)3)

struct kbase_ioctl_mem_free {
	uint64_t gpu_addr;
};
#define KBASE_IOCTL_MEM_FREE \
	_IOW(KBASE_IOCTL_TYPE, 7, struct kbase_ioctl_mem_free)

struct kbase_ioctl_mem_exec_init {
	uint64_t va_pages;
};
#define KBASE_IOCTL_MEM_EXEC_INIT \
	_IOW(KBASE_IOCTL_TYPE, 38, struct kbase_ioctl_mem_exec_init)

/* -------------------------------------------------------- memory flags */

#define BASE_MEM_PROT_CPU_RD           ((uint64_t)1 << 0)
#define BASE_MEM_PROT_CPU_WR           ((uint64_t)1 << 1)
#define BASE_MEM_PROT_GPU_RD           ((uint64_t)1 << 2)
#define BASE_MEM_PROT_GPU_WR           ((uint64_t)1 << 3)
#define BASE_MEM_GPU_VA_SAME_4GB_PAGE  ((uint64_t)1 << 6)
#define BASE_MEM_COHERENT_SYSTEM       ((uint64_t)1 << 10)
#define BASE_MEM_CACHED_CPU            ((uint64_t)1 << 12)
#define BASE_MEM_SAME_VA               ((uint64_t)1 << 13)
#define BASE_MEM_UNCACHED_GPU          ((uint64_t)1 << 21)

/*
 * The tracking page must be mapped (mmap offset PFN_DOWN(3 << 12) == 3) before
 * MEM_ALLOC works: kbase_api_mem_alloc() rejects the call while
 * kctx->process_mm != current->mm (mali_kbase_core_linux.c:587-595).
 */
#define BASE_MEM_MAP_TRACKING_HANDLE   ((uint64_t)3 << 12)
#define BASE_MEM_MAP_TRACKING_PFN      (BASE_MEM_MAP_TRACKING_HANDLE >> 12)

/* ------------------------------------------------------------- job atoms */

/* core requirements (mali_base_kernel.h:529+) */
#define BASE_JD_REQ_DEP                    ((uint32_t)0)
#define BASE_JD_REQ_FS                     ((uint32_t)1 << 0)
#define BASE_JD_REQ_CS                     ((uint32_t)1 << 1)
#define BASE_JD_REQ_T                      ((uint32_t)1 << 2)
#define BASE_JD_REQ_CF                     ((uint32_t)1 << 3)
#define BASE_JD_REQ_V                      ((uint32_t)1 << 4)
#define BASE_JD_REQ_EVENT_COALESCE         ((uint32_t)1 << 5)
#define BASE_JD_REQ_COHERENT_GROUP         ((uint32_t)1 << 6)
#define BASE_JD_REQ_PERMON                 ((uint32_t)1 << 7)
#define BASE_JD_REQ_EXTERNAL_RESOURCES     ((uint32_t)1 << 8)
#define BASE_JD_REQ_SOFT_JOB               ((uint32_t)1 << 9)
#define BASE_JD_REQ_ONLY_COMPUTE           ((uint32_t)1 << 10)
#define BASE_JD_REQ_SPECIFIC_COHERENT_GROUP ((uint32_t)1 << 11)
#define BASE_JD_REQ_EVENT_ONLY_ON_FAILURE  ((uint32_t)1 << 12)
#define BASE_JD_REQ_FS_AFBC                ((uint32_t)1 << 13)
#define BASE_JD_REQ_SKIP_CACHE_START       ((uint32_t)1 << 15)
#define BASE_JD_REQ_SKIP_CACHE_END         ((uint32_t)1 << 16)

/* soft job types are (BASE_JD_REQ_SOFT_JOB | n) */
#define BASE_JD_REQ_SOFT_JOB_TYPE          0x0000007fu
#define BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME (BASE_JD_REQ_SOFT_JOB | 0x1)
#define BASE_JD_REQ_SOFT_FENCE_TRIGGER     (BASE_JD_REQ_SOFT_JOB | 0x2)
#define BASE_JD_REQ_SOFT_FENCE_WAIT        (BASE_JD_REQ_SOFT_JOB | 0x3)
#define BASE_JD_REQ_SOFT_EVENT_WAIT        (BASE_JD_REQ_SOFT_JOB | 0x5)
#define BASE_JD_REQ_SOFT_EVENT_SET         (BASE_JD_REQ_SOFT_JOB | 0x6)
#define BASE_JD_REQ_SOFT_EVENT_RESET       (BASE_JD_REQ_SOFT_JOB | 0x7)

/* dependency types (mali_base_kernel.h:508-510) */
#define BASE_JD_DEP_TYPE_INVALID 0
#define BASE_JD_DEP_TYPE_DATA    1
#define BASE_JD_DEP_TYPE_ORDER   2

/* priorities 0..2 (BASE_JD_NR_PRIO_LEVELS == 3) */
#define BASE_JD_PRIO_LOW    0
#define BASE_JD_PRIO_MEDIUM 1
#define BASE_JD_PRIO_HIGH   2

struct base_jd_udata {
	uint64_t blob[2];
};

struct base_dependency {
	uint8_t atom_id;
	uint8_t dependency_type;
};

/*
 * base_jd_atom_v2 -- 64 bytes, EXACTLY as this kernel declares it
 * (mali_base_kernel.h:870-888), including the Samsung SRUK tail
 * (MALI_SYSTRACE_SUPPORT).  Offsets are asserted in verify_layout.c.
 */
struct kbase_jd_atom_v2 {
	uint64_t jc;                 /* 0  job-chain GPU VA (or user ptr for soft jobs) */
	struct base_jd_udata udata;  /* 8  echoed back in the completion event */
	uint64_t extres_list;        /* 24 GPU VA of the external-resource list */
	uint16_t nr_extres;          /* 32 number of external resources / JIT ids */
	uint16_t compat_core_req;    /* 34 UK 10.2 legacy core_req (u16) */
	struct base_dependency pre_dep[2]; /* 36,38 pre-dependencies */
	uint8_t atom_number;         /* 40 1..255, 0 means unset */
	uint8_t prio;                /* 41 */
	uint8_t device_nr;           /* 42 */
	uint8_t padding;             /* 43 */
	uint32_t core_req;           /* 44 u32 requirements */
	/* SRUK-MALI_SYSTRACE_SUPPORT: systrace-only, zeros are fine */
	uint32_t gles_ctx_handle;    /* 48 */
	uint32_t frame_number;       /* 52 */
	uint64_t surfacep;           /* 56 */
};

/* ---------------------------------------------------------- events (read) */

#define BASE_JD_EVENT_DONE          0x01
#define BASE_JD_EVENT_DRV_TERMINATED 0x08 /* -> read() returns -EPIPE */

/*
 * base_jd_event_v2 -- what read(/dev/mali0) returns, 24 bytes
 * (mali_base_kernel.h:1212-1216; mali_kbase_core_linux.c:1384).
 */
struct base_jd_event_v2 {
	uint32_t event_code;
	uint8_t atom_number;
	uint8_t padding[3];
	struct base_jd_udata udata;
};

/*
 * Payload written by SOFT_DUMP_CPU_GPU_TIME into the memory the atom's jc
 * points at (mali_base_kernel.h:1230-1236).  jc must be cache-line aligned
 * (mali_kbase_softjobs.c:1534-1538).
 */
struct base_dump_cpu_gpu_counters {
	uint64_t system_time;
	uint64_t cycle_counter;
	uint64_t sec;
	uint32_t usec;
	uint8_t padding[36];
};

/* ------------------------------------------------------------ fixed sizes */

#define KBASE_ATOM_SIZE 64
#define KBASE_EVENT_SIZE 24
#define KBASE_DUMP_COUNTERS_SIZE 64

#ifdef __cplusplus
}
#endif
#endif /* KBASE_UAPI_R28P0_H */
