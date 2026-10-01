/*
 * kb_sys.h -- syscall indirection for the M1 bring-up tool.
 *
 * On the device the wrappers are thin libc calls.  With -DHOST_SIM they are
 * provided by host_sim.c, so the whole bring-up sequence can be exercised on a
 * workstation against a simulated kbase (validating control flow, the atom
 * layout and the event parsing before spending device sessions).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef KB_SYS_H
#define KB_SYS_H

#include <stddef.h>
#include <stdint.h>

#ifdef HOST_SIM
#include <sys/mman.h>
#include <sys/types.h>
int  kb_open_kbase(void);
void kb_close(int fd);
int  kb_ioctl(int fd, unsigned long request, void *arg);
void *kb_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
int  kb_munmap(void *addr, size_t len);
long kb_read(int fd, void *buf, size_t len);
int  kb_wait_readable(int fd, int timeout_ms); /* 1 = readable, 0 = timeout */
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static inline int kb_open_kbase(void)
{
	return open("/dev/mali0", O_RDWR | O_CLOEXEC);
}

static inline void kb_close(int fd)
{
	close(fd);
}

static inline int kb_ioctl(int fd, unsigned long request, void *arg)
{
	return ioctl(fd, request, arg);
}

static inline void *kb_mmap(void *addr, size_t len, int prot, int flags, int fd,
			    off_t offset)
{
	return mmap(addr, len, prot, flags, fd, offset);
}

static inline int kb_munmap(void *addr, size_t len)
{
	return munmap(addr, len);
}

static inline long kb_read(int fd, void *buf, size_t len)
{
	return read(fd, buf, len);
}

static inline int kb_wait_readable(int fd, int timeout_ms)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int r = poll(&pfd, 1, timeout_ms);
	return r > 0 && (pfd.revents & (POLLIN | POLLHUP));
}
#endif

#endif /* KB_SYS_H */
