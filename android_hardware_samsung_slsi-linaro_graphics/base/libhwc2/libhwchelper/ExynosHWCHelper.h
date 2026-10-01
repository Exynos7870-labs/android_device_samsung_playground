/*
 * Copyright (C) 2012 The Android Open Source Project
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
#ifndef _EXYNOSHWCHELPER_H
#define _EXYNOSHWCHELPER_H

#include <utils/String8.h>
#include <hardware/hwcomposer2.h>
#include <hardware/hwcomposer.h>
#include "MppFactory.h"
#ifdef GRALLOC_VERSION1
#include "gralloc1_priv.h"
#else
#include "gralloc_priv.h"
#endif
#include "DeconHeader.h"
#include "exynos_sync.h"

#define MAX_FENCE_NAME 64

#define UHD_WIDTH       3840
#define UHD_HEIGHT      2160

template<typename T> inline T max(T a, T b) { return (a > b) ? a : b; }
template<typename T> inline T min(T a, T b) { return (a < b) ? a : b; }

class ExynosLayer;
class ExynosDisplay;

using namespace android;

typedef enum hwc_fdebug_fence_type_t {
    FENCE_TYPE_SRC_RELEASE = 1,
    FENCE_TYPE_SRC_ACQUIRE = 2,
    FENCE_TYPE_DST_RELEASE = 3,
    FENCE_TYPE_DST_ACQUIRE = 4,
    FENCE_TYPE_FREE_RELEASE = 5,
    FENCE_TYPE_FREE_ACQUIRE = 6,
    FENCE_TYPE_HW_STATE = 7,
    FENCE_TYPE_RETIRE = 8,
    FENCE_TYPE_ALL = 9,
    FENCE_TYPE_UNDEFINED = 100
} hwc_fdebug_fence_type;

typedef enum hwc_fdebug_ip_type_t {
    FENCE_IP_DPP = 0,
    FENCE_IP_MSC = 1,
    FENCE_IP_G2D = 2,
    FENCE_IP_FB = 3,
    FENCE_IP_LAYER = 4,
    FENCE_IP_ALL = 5,
    FENCE_IP_UNDEFINED = 100
} hwc_fdebug_ip_type;

typedef enum hwc_fence_type_t {
    FENCE_LAYER_RELEASE_DPP     = 0,
    FENCE_LAYER_RELEASE_MPP     = 1,
    FENCE_LAYER_RELEASE_MSC     = 2,
    FENCE_LAYER_RELEASE_G2D     = 3,
    FENCE_DPP_HW_STATE          = 4,
    FENCE_MSC_HW_STATE          = 5,
    FENCE_G2D_HW_STATE          = 6,
    FENCE_MSC_SRC_LAYER         = 7,
    FENCE_G2D_SRC_LAYER         = 8,
    FENCE_MPP_DST_DPP           = 9,
    FENCE_MSC_DST_DPP           = 10,
    FENCE_G2D_DST_DPP           = 11,
    FENCE_DPP_SRC_MPP           = 12,
    FENCE_DPP_SRC_MSC           = 13,
    FENCE_DPP_SRC_G2D           = 14,
    FENCE_DPP_SRC_LAYER         = 15,
    FENCE_MPP_FREE_BUF_ACQUIRE  = 16,
    FENCE_MPP_FREE_BUF_RELEASE  = 17,
    FENCE_RETIRE                = 18,
    FENCE_MAX
} hwc_fence_type;

enum {
    EXYNOS_ERROR_NONE       = 0,
    EXYNOS_ERROR_CHANGED    = 1
};

enum {
    eSkipLayer                    =     0x00000001,
    eInvalidHandle                =     0x00000002,
    eHasFloatSrcCrop              =     0x00000004,
    eUpdateExynosComposition      =     0x00000008,
    eDynamicRecomposition         =     0x00000010,
    eForceFbEnabled               =     0x00000020,
    eSandwitchedBetweenGLES       =     0x00000040,
    eSandwitchedBetweenEXYNOS     =     0x00000080,
    eInsufficientWindow           =     0x00000100,
    eInsufficientMPP              =     0x00000200,
    eSkipStaticLayer              =     0x00000400,
    eUnSupportedUseCase           =     0x00000800,
    eDimLayer                     =     0x00001000,
    eResourcePendingWork          =     0x00002000,
    eSourceOverBelow              =     0x00004000,
    eSkipRotateAnim               =     0x00008000,
    eReallocOnGoingForDDI         =     0x00010000,
    eResourceAssignFail           =     0x20000000,
    eMPPUnsupported               =     0x40000000,
    eUnknown                      =     0x80000000,
};

enum regionType {
    eTransparentRegion          =       0,
    eCoveredOpaqueRegion        =       1,
    eDamageRegionByDamage       =       2,
    eDamageRegionByLayer        =       3,
};

enum {
    eDamageRegionFull = 0,
    eDamageRegionPartial,
    eDamageRegionSkip,
    eDamageRegionError,
};

/*
 * bufferHandle can be NULL if it is not allocated yet
 * or size or format information can be different between other field values and
 * member of bufferHandle. This means bufferHandle should be reallocated.
 * */
typedef struct exynos_image {
    uint32_t fullWidth;
    uint32_t fullHeight;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t format;
    uint64_t handleFlags;
    uint32_t layerFlags;
    int acquireFenceFd;
    int releaseFenceFd;
    private_handle_t *bufferHandle;
    android_dataspace dataSpace;
    uint32_t blending;
    uint32_t transform;
    uint32_t compressed;
    float planeAlpha;
    uint32_t zOrder;
} exynos_image_t;

uint32_t getHWC1CompType(int32_t /*hwc2_composition_t*/ type);

uint32_t getDrmMode(uint64_t flags);
uint32_t getDrmMode(const private_handle_t *handle);

inline int WIDTH(const hwc_rect &rect) { return rect.right - rect.left; }
inline int HEIGHT(const hwc_rect &rect) { return rect.bottom - rect.top; }
inline int WIDTH(const hwc_frect_t &rect) { return (int)(rect.right - rect.left); }
inline int HEIGHT(const hwc_frect_t &rect) { return (int)(rect.bottom - rect.top); }

uint32_t halDataSpaceToV4L2ColorSpace(android_dataspace data_space);
enum decon_pixel_format halFormatToS3CFormat(int format);
uint8_t formatToBpp(int format);
uint8_t DeconFormatToBpp(int format);
bool isFormatRgb(int format);
bool isFormatYUV(int format);
bool isFormatYUV420(int format);
bool isFormatYUV422(int format);
bool isFormatYCrCb(int format);
unsigned int isNarrowRgb(int format, android_dataspace data_space);
bool isCompressed(const hwc_layer_1_t &layer);
bool isCompressed(const private_handle_t *handle);
bool isSrcCropFloat(hwc_frect &frect);
bool isScaled(exynos_image &src, exynos_image &dst);
bool isScaledDown(exynos_image &src, exynos_image &dst);

void dumpMPPImage(exynos_mpp_img &c);
void dumpMPPImage(uint32_t type, exynos_mpp_img &c);
void dumpExynosImage(uint32_t type, exynos_image &img);
void dumpExynosImage(String8& result, exynos_image &img);
void dumpHandle(uint32_t type, private_handle_t *h);
void dumpLayer(uint32_t type, hwc_layer_1_t &l);
void dumpLayer(hwc_layer_1_t &l);
void dumpLayer(String8& result, hwc_layer_1_t &l);
String8 getFormatStr(int format);
void adjustRect(hwc_rect_t &rect, int32_t width, int32_t height);
uint32_t getBufferNumOfFormat(int format);

int fence_close(int fence, ExynosDisplay* display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip);
bool fence_valid(int fence);

int hwcFdClose(int fd);
int hwc_dup(int fd, ExynosDisplay* display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip);
int hwc_print_stack();

inline hwc_rect expand(const hwc_rect &r1, const hwc_rect &r2)
{
    hwc_rect i;
    i.top = min(r1.top, r2.top);
    i.bottom = max(r1.bottom, r2.bottom);
    i.left = min(r1.left, r2.left);
    i.right = max(r1.right, r2.right);
    return i;
}

inline int pixel_align_down(int x, int a) {
    if ((a != 0) && ((x % a) != 0))
        return ((x) - (x % a));
    return x;
}

inline int pixel_align(int x, int a) {
    if ((a != 0) && ((x % a) != 0))
        return ((x) - (x % a)) + a;
    return x;
}

//class hwc_fence_info(sync_fence_info_data* data, sync_pt_info* info) {
struct tm* getHwcFenceTime();

enum {
    FENCE_FROM = 0,
    FENCE_TO,
    FENCE_DUP,
    FENCE_CLOSE,
};

typedef struct fenceTrace {
    hwc_fdebug_fence_type type;
    hwc_fdebug_ip_type ip;
    struct timeval time;
    int32_t curFlag;
} fenceTrace_t;

typedef struct hwc_fence_info {
    uint32_t displayId;
#ifdef USED_LEGACY_SYNC
    struct sync_fence_info_data* sync_data;
    struct sync_pt_info* pt_info;
#endif
    fenceTrace_t from;
    fenceTrace_t to;
    fenceTrace_t dup;
    fenceTrace_t close;
    int32_t usage;
    int32_t curFlag;
    uint32_t last_dir;
    bool pendingAllowed = false;
    bool leaking = false;
} hwc_fence_info_t;


void setFenceName(int fenceFd, hwc_fence_type fenceType);
void setFenceName(uint32_t fd, ExynosDisplay *display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip,
        uint32_t direction, bool pendingAllowed = false);
void setFenceInfo(uint32_t fd, ExynosDisplay *display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip,
        uint32_t direction, bool pendingAllowed = false);
void printFenceInfo(uint32_t fd, hwc_fence_info_t* info);
void dumpFenceInfo(ExynosDisplay *display, int32_t __unused depth);
bool fenceWarn(hwc_fence_info_t **info, uint32_t threshold);
void resetFenceCurFlag(ExynosDisplay *display);
bool fenceWarn(ExynosDisplay *display, uint32_t threshold);
void printLeakFds(ExynosDisplay *display);
bool validateFencePerFrame(ExynosDisplay *display);

#endif
