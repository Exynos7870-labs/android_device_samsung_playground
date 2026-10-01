/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#undef LOG_TAG
#define LOG_TAG "repeater"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <ion/ion.h>
#include <sys/mman.h>

#include <utils/Log.h>
#include "exynos_format.h"
#include "repeater.h"
#include "repeater_hal.h"

#define REPEATER_DEV_NAME       "/dev/repeater"

/* FIX ME */
#define __ALIGN_UP(x,a) (((x) + ((a) - 1)) & ~((a) - 1))
#define NV12N_Y_SIZE(w,h) (__ALIGN_UP((w), 16) * __ALIGN_UP((h), 16) + 256)
#define NV12N_CBCR_SIZE(w,h) (__ALIGN_UP((__ALIGN_UP((w), 16) * (__ALIGN_UP((h), 16) / 2) + 256), 16))
#define v4l2_fourcc(a,b,c,d) ((__u32) (a) | ((__u32) (b) << 8) | ((__u32) (c) << 16) | ((__u32) (d) << 24))
#define V4L2_PIX_FMT_NV12N v4l2_fourcc('N', 'N', '1', '2')

#define ION_EXYNOS_HEAP_ID_SYSTEM        0
#define ION_EXYNOS_HEAP_ID_VIDEO_FRAME   5
#define ION_FLAG_PROTECTED      16

#define MAX_HEAP_NAME 32

struct ion_heap_data {
    char name[MAX_HEAP_NAME];
    __u32 type;
    __u32 heap_id;
    __u32 size;       /* reserved 0 */
    __u32 heap_flags; /* reserved 1 */
    __u32 reserved2;
};

struct repeater_hal {
    struct repeater_info info;
    int repeater_fd;
    int ion_client_fd;
    void *buf_addr[MAX_SHARED_BUFFER_NUM];
};

void *repeater_open()
{
    struct repeater_hal *hal;
    int i;

    hal = (struct repeater_hal *)malloc(sizeof(struct repeater_hal));

    hal->repeater_fd = open(REPEATER_DEV_NAME, O_RDWR);
    if (hal->repeater_fd < 0)
    {
        ALOGE("%s: open fail repeater module (%s)", __FUNCTION__, REPEATER_DEV_NAME);
        free(hal);
        return NULL;
    }
    for (i = 0; i < MAX_SHARED_BUFFER_NUM; i++)
        hal->info.buf_fd[i] = -1;

    hal->ion_client_fd = ion_open();

    ALOGI("repeater opened");

    return hal;
}

void repeater_close(void *handle)
{
    struct repeater_hal *hal;

    if (!handle)
        return;

    hal = (struct repeater_hal *)handle;

    if (hal->ion_client_fd > 0) {
        ion_close(hal->ion_client_fd);
        hal->ion_client_fd = -1;
    }

    if (hal->repeater_fd > 0) {
        close(hal->repeater_fd);
        hal->repeater_fd = -1;
    }

    free(hal);

    ALOGI("repeater close");
}

int repeater_map(void *handle, int w, int h, int fps, bool enable_hdcp)
{
    int ret;
    struct repeater_hal *hal;
    int i;
    int size = 0;
    unsigned int heap_id = ION_EXYNOS_HEAP_ID_SYSTEM;
    unsigned int ion_flags = 0;

    if (!handle)
        return -ENOENT;

    hal = (struct repeater_hal *)handle;

    /* ion free */
    for (i = 0; i < MAX_SHARED_BUFFER_NUM; i++) {
        if (hal->info.buf_fd[i] > 0) {
            close(hal->info.buf_fd[i]);
            hal->info.buf_fd[i] = -1;
        }
    }

    size = NV12N_Y_SIZE(w, h) + NV12N_CBCR_SIZE(w, h);

    if (enable_hdcp) {
        heap_id = ION_EXYNOS_HEAP_ID_VIDEO_FRAME;
        ion_flags = ION_FLAG_PROTECTED;
    }

    if (!ion_is_legacy(hal->ion_client_fd)) {
        int heap_cnt = 0;

        ret = ion_query_heap_cnt(hal->ion_client_fd, &heap_cnt);
        if (ret < 0) {
            ALOGE("fail to query the heap count");
            return ret;
        }

        struct ion_heap_data heaps[heap_cnt];

        ret = ion_query_get_heaps(hal->ion_client_fd, heap_cnt, heaps);
        if (ret < 0) {
            ALOGE("fail to query the heaps");
            return ret;
        }

        for (int i = 0; i < heap_cnt; i++) {
            if (enable_hdcp) {
                if (strcmp(heaps[i].name, "vframe_heap") == 0)
                    heap_id = heaps[i].heap_id;
            } else {
                if (strcmp(heaps[i].name, "ion_system_heap") == 0)
                    heap_id = heaps[i].heap_id;
            }
        }
    }

    ALOGI("hwfc buffer attribute: heap_id %d, ion_flags 0x%x", heap_id, ion_flags);
    ALOGI("repeater_map(), width %d, height %d, NV12N_Y_SIZE() %d, NV12N_CBCR_SIZE() %d",
        w, h, NV12N_Y_SIZE(w, h), NV12N_CBCR_SIZE(w, h));

    /* ion alloc */
    for (i = 0; i < MAX_SHARED_BUFFER_NUM; i++) {
        ret = ion_alloc_fd(hal->ion_client_fd, size, 0, 1 << heap_id, ion_flags, &hal->info.buf_fd[i]);
        if (ret < 0) {
            ALOGE("fail to ion_alloc_fd");
            hal->info.buf_fd[i] = -1;
            return ret;
        }
        hal->buf_addr[i] = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, hal->info.buf_fd[i], 0);
    }

    hal->info.width = w;
    hal->info.height = h;
    hal->info.fps = fps;
    hal->info.pixel_format = V4L2_PIX_FMT_NV12N;
    hal->info.buffer_count = MAX_SHARED_BUFFER_NUM;

    ret = ioctl(hal->repeater_fd, REPEATER_IOCTL_MAP_BUF, &hal->info);
    if (ret < 0) {
        ALOGE("fail to ioctl: REPEATER_IOCTL_MAP_BUF");
        return ret;
    }

    ALOGI("repeater map success");

    return ret;
}

void repeater_unmap(void *handle)
{
    struct repeater_hal *hal;
    int i;
    int width, height;
    int size;

    if (!handle)
        return;

    hal = (struct repeater_hal *)handle;

    ioctl(hal->repeater_fd, REPEATER_IOCTL_UNMAP_BUF);

    /* ion free */
    width = hal->info.width;
    height = hal->info.height;
    size = NV12N_Y_SIZE(width, height) + NV12N_CBCR_SIZE(width, height);
    for (i = 0; i < MAX_SHARED_BUFFER_NUM; i++) {
        if (hal->info.buf_fd[i] > 0) {
            if (hal->buf_addr[i])
                munmap(hal->buf_addr[i], size);
            close(hal->info.buf_fd[i]);
            hal->info.buf_fd[i] = -1;
        }
    }

    ALOGI("repeater_unmap");
}

int repeater_start(void *handle)
{
    int ret;
    struct repeater_hal *hal;

    if (!handle)
        return -ENOENT;

    hal = (struct repeater_hal *)handle;

    ret = ioctl(hal->repeater_fd, REPEATER_IOCTL_START);
    if (ret < 0) {
        ALOGE("fail to ioctl: REPEATER_IOCTL_START");
        return ret;
    }

    ALOGI("repeater start success");

    return ret;
}

int repeater_stop(void *handle)
{
    int ret;
    struct repeater_hal *hal;

    if (!handle)
        return -ENOENT;

    hal = (struct repeater_hal *)handle;

    ret = ioctl(hal->repeater_fd, REPEATER_IOCTL_STOP);
    if (ret < 0) {
        ALOGE("fail to ioctl: REPEATER_IOCTL_STOP");
        return ret;
    }

    ALOGI("repeater stop success");

    return ret;
}

int repeater_pause(void *handle)
{
    int ret;
    struct repeater_hal *hal;

    if (!handle)
        return -ENOENT;

    hal = (struct repeater_hal *)handle;

    ret = ioctl(hal->repeater_fd, REPEATER_IOCTL_PAUSE);
    if (ret < 0) {
        ALOGE("fail to ioctl: REPEATER_IOCTL_PAUSE");
        return ret;
    }

    ALOGI("repeater pause success");

    return ret;
}

int repeater_resume(void *handle)
{
    int ret;
    struct repeater_hal *hal;

    if (!handle)
        return -ENOENT;

    hal = (struct repeater_hal *)handle;

    ret = ioctl(hal->repeater_fd, REPEATER_IOCTL_RESUME);
    if (ret < 0) {
        ALOGE("fail to ioctl: REPEATER_IOCTL_RESUME");
        return ret;
    }

    ALOGI("repeater resume success");

    return ret;
}

int repeater_dump(void *handle, char *name)
{
    int ret;
    struct repeater_hal *hal;
    int buf_idx = -1;
    int width, height;
    int size;

    if (!handle)
        return -ENOENT;

    hal = (struct repeater_hal *)handle;

    ret = ioctl(hal->repeater_fd, REPEATER_IOCTL_DUMP, &buf_idx);
    ALOGI("repeater_dump() ioctl ret %d, buf_idx %d", ret, buf_idx);

    if (ret == 0 && buf_idx >= 0) {
        FILE *pFile = fopen(name, "wb");
        width = hal->info.width;
        height = hal->info.height;
        size = NV12N_Y_SIZE(width, height) + NV12N_CBCR_SIZE(width, height);
        ALOGI("repeater_dump() fopen(%s) pFile %p, hal->buf_addr[buf_idx] %p, size %d",
            name, pFile, hal->buf_addr[buf_idx], size);
        ALOGI("repeater_dump(), width %d, height %d, NV12N_Y_SIZE() %d, NV12N_CBCR_SIZE() %d",
            width, height, NV12N_Y_SIZE(width, height), NV12N_CBCR_SIZE(width, height));
        if (pFile) {
            if (hal->buf_addr[buf_idx]) {
                fwrite(hal->buf_addr[buf_idx], 0x1, width * height, pFile);
                fwrite((char *)(hal->buf_addr[buf_idx]) + NV12N_Y_SIZE(width, height), 0x1, (width * height) / 2, pFile);
            }
            fclose(pFile);
        }
    }

    return ret;
}

