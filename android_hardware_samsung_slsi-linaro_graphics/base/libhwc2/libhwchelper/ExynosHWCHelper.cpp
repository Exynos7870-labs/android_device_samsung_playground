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
#include <utils/Errors.h>
#include <videodev2.h>
#include <utils/CallStack.h>
#include <android/sync.h>
#include "ExynosHWCHelper.h"
#include "ExynosHWCDebug.h"
#include "ExynosHWC.h"
#include "ExynosLayer.h"
#include "exynos_sync.h"

#define FT_LOGD(msg, ...) \
    {\
        if (exynosHWCControl.fenceTracer >= 2) \
            ALOGD("[FenceTracer]::" msg, ##__VA_ARGS__); \
    }
#define FT_LOGE(msg, ...) \
    {\
        if (exynosHWCControl.fenceTracer > 0) \
            ALOGE("[FenceTracer]::" msg, ##__VA_ARGS__); \
    }
#define FT_LOGW(msg, ...) \
    {\
        if (exynosHWCControl.fenceTracer >= 1) \
            ALOGD("[FenceTracer]::" msg, ##__VA_ARGS__); \
    }

extern struct exynos_hwc_cotrol exynosHWCControl;
extern char fence_names[FENCE_MAX][32];
uint32_t getHWC1CompType(int32_t type) {

    uint32_t cType = HWC_FRAMEBUFFER;

    switch(type) {
    case HWC2_COMPOSITION_DEVICE:
    case HWC2_COMPOSITION_EXYNOS:
        cType = HWC_OVERLAY;
        break;
    case HWC2_COMPOSITION_SOLID_COLOR:
        cType = HWC_BACKGROUND;
        break;
    case HWC2_COMPOSITION_CURSOR:
        cType = HWC_CURSOR_OVERLAY;
        break;
    case HWC2_COMPOSITION_SIDEBAND:
        cType = HWC_SIDEBAND;
        break;
    case HWC2_COMPOSITION_CLIENT:
    case HWC2_COMPOSITION_INVALID:
    default:
        cType = HWC_FRAMEBUFFER;
        break;
    }

    return cType;
}

#ifdef GRALLOC_VERSION1
uint32_t getDrmMode(uint64_t flags)
{
    if (flags & GRALLOC1_PRODUCER_USAGE_PROTECTED) {
        if (flags & GRALLOC1_PRODUCER_USAGE_PRIVATE_NONSECURE)
            return NORMAL_DRM;
        else
            return SECURE_DRM;
    }

    return NO_DRM;
}
#else
uint32_t getDrmMode(uint64_t flags) {
    if (flags & GRALLOC_USAGE_PROTECTED) {
        if (flags & GRALLOC_USAGE_PRIVATE_NONSECURE)
            return NORMAL_DRM;
        else
            return SECURE_DRM;
    }
    return NO_DRM;
}
#endif

uint32_t getDrmMode(const private_handle_t *handle)
{
#ifdef GRALLOC_VERSION1
    if (handle->producer_usage & GRALLOC1_PRODUCER_USAGE_PROTECTED) {
        if (handle->producer_usage & GRALLOC1_PRODUCER_USAGE_PRIVATE_NONSECURE)
            return NORMAL_DRM;
        else
            return SECURE_DRM;
    }
#else
    if (handle->flags & GRALLOC_USAGE_PROTECTED) {
        if (handle->flags & GRALLOC_USAGE_PRIVATE_NONSECURE)
            return NORMAL_DRM;
        else
            return SECURE_DRM;
    }
#endif
    return NO_DRM;
}

unsigned int isNarrowRgb(int format, android_dataspace data_space)
{
    if (format == HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL)
        return 0;
    else {
        if (isFormatRgb(format))
            return 0;
        else {
            uint32_t data_space_range = (data_space & HAL_DATASPACE_RANGE_MASK);
            if (data_space_range == HAL_DATASPACE_RANGE_UNSPECIFIED) {
                return 1;
            } else if (data_space_range == HAL_DATASPACE_RANGE_FULL) {
                return 0;
            } else {
                return 1;
            }
        }
    }
}

uint8_t formatToBpp(int format)
{
    switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
#ifdef EXYNOS_SUPPORT_BGRX_8888
    case HAL_PIXEL_FORMAT_BGRX_8888:
#endif
        return 32;
    case HAL_PIXEL_FORMAT_RGB_565:
        return 16;
    case HAL_PIXEL_FORMAT_EXYNOS_YV12_M:
    case HAL_PIXEL_FORMAT_YV12:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_PRIV:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_TILED:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_PN:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_TILED:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_S10B:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_S10B:
        return 12;
    default:
        ALOGW("unrecognized pixel format %u", format);
        return 0;
    }
}

uint8_t DeconFormatToBpp(int format)
{
    switch (format) {
        case DECON_PIXEL_FORMAT_ARGB_8888:
        case DECON_PIXEL_FORMAT_ABGR_8888:
        case DECON_PIXEL_FORMAT_RGBA_8888:
        case DECON_PIXEL_FORMAT_BGRA_8888:
        case DECON_PIXEL_FORMAT_XRGB_8888:
        case DECON_PIXEL_FORMAT_XBGR_8888:
        case DECON_PIXEL_FORMAT_RGBX_8888:
        case DECON_PIXEL_FORMAT_BGRX_8888:
            return 32;
        case DECON_PIXEL_FORMAT_RGBA_5551:
        case DECON_PIXEL_FORMAT_RGB_565:
            return 16;
        case DECON_PIXEL_FORMAT_NV16:
        case DECON_PIXEL_FORMAT_NV61:
        case DECON_PIXEL_FORMAT_YVU422_3P:
        case DECON_PIXEL_FORMAT_NV12:
        case DECON_PIXEL_FORMAT_NV21:
        case DECON_PIXEL_FORMAT_NV12M:
        case DECON_PIXEL_FORMAT_NV21M:
        case DECON_PIXEL_FORMAT_YUV420:
        case DECON_PIXEL_FORMAT_YVU420:
        case DECON_PIXEL_FORMAT_YUV420M:
        case DECON_PIXEL_FORMAT_YVU420M:
        case DECON_PIXEL_FORMAT_NV12N:
        case DECON_PIXEL_FORMAT_NV12N_10B:
            return 12;
        default:
            ALOGW("unrecognized decon format %u", format);
            return 0;
    }
}

bool isFormatRgb(int format)
{
    switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_RGB_888:
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_EXYNOS_ARGB_8888:
        return true;

    default:
        return false;
    }
}

bool isFormatYUV(int format)
{
    if (isFormatRgb(format))
        return false;
    return true;
}

bool isFormatYUV420(int format)
{
    switch (format) {
    case HAL_PIXEL_FORMAT_EXYNOS_YV12_M:
    case HAL_PIXEL_FORMAT_YV12:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_PRIV:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_TILED:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_PN:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_TILED:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_S10B:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_S10B:
        return true;
    default:
        return false;
    }
}

bool isFormatYUV422(int __unused format)
{
    // Might add support later
    return false;
}
bool isFormatYCrCb(int format)
{
    return format == HAL_PIXEL_FORMAT_EXYNOS_YV12_M;
}

bool isCompressed(const hwc_layer_1_t &layer)
{
    if (layer.handle) {
        private_handle_t *handle = private_handle_t::dynamicCast(layer.handle);
        if (handle->internal_format == (((uint64_t)(1) << 32) | handle->format))
            return true;
    }
    return false;
}

bool isCompressed(const private_handle_t *handle)
{
    if ((handle != NULL) &&
        (handle->internal_format == (((uint64_t)(1) << 32) | handle->format)))
        return true;

    return false;
}

uint32_t halDataSpaceToV4L2ColorSpace(android_dataspace data_space)
{
    uint32_t standard_data_space = (data_space & HAL_DATASPACE_STANDARD_MASK);
    switch (standard_data_space) {
    case HAL_DATASPACE_STANDARD_BT2020:
    case HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE:
        return V4L2_COLORSPACE_BT2020;
    case HAL_DATASPACE_STANDARD_DCI_P3:
        return V4L2_COLORSPACE_DCI_P3;
    case HAL_DATASPACE_STANDARD_BT709:
        return V4L2_COLORSPACE_REC709;
    default:
        return V4L2_COLORSPACE_DEFAULT;
    }
    return V4L2_COLORSPACE_DEFAULT;
}

enum decon_pixel_format halFormatToS3CFormat(int format)
{
    switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
        return DECON_PIXEL_FORMAT_RGBA_8888;
    case HAL_PIXEL_FORMAT_RGBX_8888:
        return DECON_PIXEL_FORMAT_RGBX_8888;
    case HAL_PIXEL_FORMAT_RGB_565:
        return DECON_PIXEL_FORMAT_RGB_565;
    case HAL_PIXEL_FORMAT_BGRA_8888:
        return DECON_PIXEL_FORMAT_BGRA_8888;
#ifdef EXYNOS_SUPPORT_BGRX_8888
    case HAL_PIXEL_FORMAT_BGRX_8888:
        return DECON_PIXEL_FORMAT_BGRX_8888;
#endif
    case HAL_PIXEL_FORMAT_EXYNOS_YV12_M:
        return DECON_PIXEL_FORMAT_YVU420M;
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P_M:
        return DECON_PIXEL_FORMAT_YUV420M;
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL:
        return DECON_PIXEL_FORMAT_NV21M;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        return DECON_PIXEL_FORMAT_NV21;
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_PRIV:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_S10B:
        return DECON_PIXEL_FORMAT_NV12M;
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_S10B:
        return DECON_PIXEL_FORMAT_NV12N_10B;
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN:
        return DECON_PIXEL_FORMAT_NV12N;
    default:
        return DECON_PIXEL_FORMAT_MAX;
    }
}

void dumpMPPImage(exynos_mpp_img &c)
{
    ALOGV("\tx = %u, y = %u, w = %u, h = %u, fw = %u, fh = %u",
            c.x, c.y, c.w, c.h, c.fw, c.fh);
    ALOGV("\tfotmat = %u", c.format);
    ALOGV("\taddr = {%lu, %lu, %lu}, rot = %u, cacheable = %u, drmMode = %u",
            c.yaddr, c.uaddr, c.vaddr, c.rot, c.cacheable, c.drmMode);
    ALOGV("\tnarrowRgb = %u, acquireFenceFd = %d, releaseFenceFd = %d, mem_type = %u",
            c.narrowRgb, c.acquireFenceFd, c.releaseFenceFd, c.mem_type);
}

void dumpMPPImage(uint32_t type, exynos_mpp_img &c)
{
    HDEBUGLOGD(type, "\tx = %u, y = %u, w = %u, h = %u, fw = %u, fh = %u",
            c.x, c.y, c.w, c.h, c.fw, c.fh);
    HDEBUGLOGD(type, "\tf = %u", c.format);
    HDEBUGLOGD(type, "\taddr = {%lu, %lu, %lu}, rot = %d, cacheable = %d, drmMode = %d",
            c.yaddr, c.uaddr, c.vaddr, c.rot, c.cacheable, c.drmMode);
    HDEBUGLOGD(type, "\tnarrowRgb = %u, acquireFenceFd = %d, releaseFenceFd = %d, mem_type = %u",
            c.narrowRgb, c.acquireFenceFd, c.releaseFenceFd, c.mem_type);
}

void dumpHandle(uint32_t type, private_handle_t *h)
{
    if (h == NULL)
        return;
    HDEBUGLOGD(type, "\t\tformat = %d, width = %u, height = %u, stride = %u, vstride = %u",
            h->format, h->width, h->height, h->stride, h->vstride);
}

void dumpLayer(uint32_t type, hwc_layer_1_t &l)
{
    HDEBUGLOGD(type, "\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, "
            "{%7.1f,%7.1f,%7.1f,%7.1f}, {%d,%d,%d,%d}",
            l.compositionType, l.flags, l.handle, l.transform,
            l.blending,
            l.sourceCropf.left,
            l.sourceCropf.top,
            l.sourceCropf.right,
            l.sourceCropf.bottom,
            l.displayFrame.left,
            l.displayFrame.top,
            l.displayFrame.right,
            l.displayFrame.bottom);

#if 0
    if(l.handle && !(l.flags & HWC_SKIP_LAYER))
        dumpHandle(type, private_handle_t::dynamicCast(l.handle));
#else
    HDEBUGLOGD(type, "\t\thandle: %p", l.handle);
#endif
}

void dumpLayer(hwc_layer_1_t &l)
{
    ALOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, "
            "fence = %d, {%7.1f,%7.1f,%7.1f,%7.1f}, {%d,%d,%d,%d}",
            l.compositionType, l.flags, l.handle, l.transform,
            l.blending,
            l.acquireFenceFd,
            l.sourceCropf.left,
            l.sourceCropf.top,
            l.sourceCropf.right,
            l.sourceCropf.bottom,
            l.displayFrame.left,
            l.displayFrame.top,
            l.displayFrame.right,
            l.displayFrame.bottom);

    if (l.handle != NULL) {
        private_handle_t *handle = (private_handle_t*)l.handle;
        ALOGD("\t\thandle: %p, internal_format: 0x%" PRIx64 "", handle, handle->internal_format);
    } else {
        ALOGD("\t\thandle: %p", l.handle);
    }
}
void dumpLayer(String8& result, hwc_layer_1_t &l)
{
     result.appendFormat("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, "
            "fence = %d, {%7.1f,%7.1f,%7.1f,%7.1f}, {%d,%d,%d,%d}\n",
            l.compositionType, l.flags, l.handle, l.transform,
            l.blending,
            l.acquireFenceFd,
            l.sourceCropf.left,
            l.sourceCropf.top,
            l.sourceCropf.right,
            l.sourceCropf.bottom,
            l.displayFrame.left,
            l.displayFrame.top,
            l.displayFrame.right,
            l.displayFrame.bottom);

    if (l.handle != NULL) {
        private_handle_t *handle = (private_handle_t*)l.handle;
         result.appendFormat("\t\thandle: %p, internal_format: 0x%" PRIx64 "\n", handle, handle->internal_format);
    } else {
         result.appendFormat("\t\thandle: %p\n", l.handle);
    }
}

void dumpExynosImage(uint32_t type, exynos_image &img)
{
    if (!hwcCheckDebugMessages(type))
        return;
    ALOGD("\tbufferHandle: %p, fullWidth: %d, fullHeight: %d, x: %d, y: %d, w: %d, h: %d, format: %s",
            img.bufferHandle, img.fullWidth, img.fullHeight, img.x, img.y, img.w, img.h, getFormatStr(img.format).string());
    ALOGD("\thandleFlags: 0x%" PRIx64 ", layerFlags: 0x%8x, acquireFenceFd: %d, releaseFenceFd: %d",
            img.handleFlags, img.layerFlags, img.acquireFenceFd, img.releaseFenceFd);
    ALOGD("\tdataSpace(%d), blending(%d), transform(0x%2x), compressed(%d)",
            img.dataSpace, img.blending, img.transform, img.compressed);
}

void dumpExynosImage(String8& result, exynos_image &img)
{
    result.appendFormat("\tbufferHandle: %p, fullWidth: %d, fullHeight: %d, x: %d, y: %d, w: %d, h: %d, format: %s\n",
            img.bufferHandle, img.fullWidth, img.fullHeight, img.x, img.y, img.w, img.h, getFormatStr(img.format).string());
    result.appendFormat("\thandleFlags: 0x%" PRIx64 ", layerFlags: 0x%8x, acquireFenceFd: %d, releaseFenceFd: %d\n",
            img.handleFlags, img.layerFlags, img.acquireFenceFd, img.releaseFenceFd);
    result.appendFormat("\tdataSpace(%d), blending(%d), transform(0x%2x), compressed(%d)\n",
            img.dataSpace, img.blending, img.transform, img.compressed);
    if (img.bufferHandle != NULL) {
        result.appendFormat("\tbuffer's stride: %d, %d\n", img.bufferHandle->stride, img.bufferHandle->vstride);
    }
}

bool isSrcCropFloat(hwc_frect &frect)
{
    return (frect.left != (int)frect.left) ||
        (frect.top != (int)frect.top) ||
        (frect.right != (int)frect.right) ||
        (frect.bottom != (int)frect.bottom);
}

bool isScaled(exynos_image &src, exynos_image &dst)
{
    uint32_t srcW = src.w;
    uint32_t srcH = src.h;
    uint32_t dstW = dst.w;
    uint32_t dstH = dst.h;

    if (!!(src.transform & HAL_TRANSFORM_ROT_90)) {
        dstW = dst.h;
        dstH = dst.w;
    }

    return ((srcW != dstW) || (srcH != dstH));
}

bool isScaledDown(exynos_image &src, exynos_image &dst)
{
    uint32_t srcW = src.w;
    uint32_t srcH = src.h;
    uint32_t dstW = dst.w;
    uint32_t dstH = dst.h;

    if (!!(src.transform & HAL_TRANSFORM_ROT_90)) {
        dstW = dst.h;
        dstH = dst.w;
    }

    return ((srcW > dstW) || (srcH > dstH));
}

String8 getFormatStr(int format) {
    switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:                    return String8("RGBA_8888");
    case HAL_PIXEL_FORMAT_RGBX_8888:                    return String8("RGBx_8888");
    case HAL_PIXEL_FORMAT_RGB_888:                      return String8("RGB_888");
    case HAL_PIXEL_FORMAT_RGB_565:                      return String8("RGB_565");
    case HAL_PIXEL_FORMAT_BGRA_8888:                    return String8("BGRA_8888");
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P_M:         return String8("EXYNOS_YCbCr_420_P_M");
    case HAL_PIXEL_FORMAT_EXYNOS_CbYCrY_422_I:          return String8("EXYNOS_CbYCrY_422_I");
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M:        return String8("EXYNOS_YCbCr_420_SP_M");
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_422_SP:          return String8("EXYNOS_YCrCb_422_SP");
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_TILED:  return String8("EXYNOS_YCbCr_420_SP_M_TILED");
    case HAL_PIXEL_FORMAT_EXYNOS_ARGB_8888:             return String8("EXYNOS_ARGB_8888");
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_422_I:           return String8("EXYNOS_YCrCb_422_I");
    case HAL_PIXEL_FORMAT_EXYNOS_CrYCbY_422_I:          return String8("EXYNOS_CrYCbY_422_I");
    case HAL_PIXEL_FORMAT_EXYNOS_YV12_M:                return String8("EXYNOS_YV12_M");
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M:        return String8("EXYNOS_YCrCb_420_SP_M");
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL:   return String8("EXYNOS_YCrCb_420_SP_M_FULL");
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P:           return String8("EXYNOS_YCbCr_420_P");
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP:          return String8("EXYNOS_YCbCr_420_SP");
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_PRIV:   return String8("EXYNOS_YCbCr_420_SP_M_PRIV");
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_PN:          return String8("EXYNOS_YCbCr_420_PN");
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN:         return String8("EXYNOS_YCbCr_420_SPN");
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_TILED:   return String8("EXYNOS_YCbCr_420_SPN_TILED");
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_S10B:   return String8("EXYNOS_YCbCr_420_SP_M_S10B");
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_S10B:    return String8("EXYNOS_YCbCr_420_SPN_S10B");
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:                 return String8("YCrCb_420_SP");
    case HAL_PIXEL_FORMAT_YV12:                         return String8("YV12");
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:       return String8("ImplDef");
    default:
       String8 result;
       result.appendFormat("? %08x", format);
       return result;

    }
}

void adjustRect(hwc_rect_t &rect, int32_t width, int32_t height)
{
    if (rect.left < 0)
        rect.left = 0;
    if (rect.left > width)
        rect.left = width;
    if (rect.top < 0)
        rect.top = 0;
    if (rect.top > height)
        rect.top = height;
    if (rect.right < rect.left)
        rect.right = rect.left;
    if (rect.right > width)
        rect.right = width;
    if (rect.bottom < rect.top)
        rect.bottom = rect.top;
    if (rect.bottom > height)
        rect.bottom = height;
}

uint32_t getBufferNumOfFormat(int format)
{
    switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_RGB_888:
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_EXYNOS_ARGB_8888:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_PN:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_TILED:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_S10B:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_YV12:
        return 1;
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_PRIV:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_S10B:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_TILED:
        return 2;
    case HAL_PIXEL_FORMAT_EXYNOS_YV12_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P_M:
        return 3;
    default:
        return 0;
    }
}

void setFenceName(int fenceFd, hwc_fence_type fenceType)
{
    if (fenceFd >= 0)
        ioctl(fenceFd, SYNC_IOC_FENCE_NAME, fence_names[fenceType]);
}

int fence_close(int fence, ExynosDisplay* display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip) {
    if (display != NULL)
        setFenceInfo(fence, display, type, ip, FENCE_CLOSE);
    return hwcFdClose(fence);
}

bool fence_valid(int fence) {
    if (fence >= 3)
        return true;
    else if (fence == -1){
        HDEBUGLOGD(eDebugFence, "%s : fence is -1", __func__);
    } else {
        ALOGW("%s : fence (fd:%d) is less than 3", __func__, fence);
        hwc_print_stack();
    }
    return false;
}

int hwcFdClose(int fd) {
    if (fd>= 3)
        close(fd);
    else if (fd == -1){
        HDEBUGLOGD(eDebugFence, "%s : Fd is -1", __func__);
    } else {
        ALOGW("%s : Fd:%d is less than 3", __func__, fd);
        hwc_print_stack();
    }
    return -1;
}

int hwc_dup(int fd, ExynosDisplay* display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip) {

    int dup_fd = -1;

    if (fd>= 3)
        dup_fd = dup(fd);
    else if (fd == -1){
        HDEBUGLOGD(eDebugFence, "%s : Fd is -1", __func__);
    } else {
        ALOGW("%s : Fd:%d is less than 3", __func__, fd);
        hwc_print_stack();
    }

    if ((dup_fd < 3) && (dup_fd != -1)) {
        ALOGW("%s : Dupulicated Fd:%d is less than 3 : %d", __func__, fd, dup_fd);
        hwc_print_stack();
    }

    setFenceInfo(dup_fd, display, type, ip, FENCE_FROM);
    FT_LOGD("duplicated %d from %d", dup_fd, fd);

    return dup_fd;
}

int hwc_print_stack() {
#if 0
    CallStack stack;
    stack.update();
    stack.log("HWCException", ANDROID_LOG_ERROR, "HWCException");
#endif
    return 0;
}


struct tm* getLocalTime(struct timeval tv) {
    return (struct tm*)localtime((time_t*)&tv.tv_sec);
}

void setFenceInfo(uint32_t fd, ExynosDisplay* display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip,
        uint32_t direction, bool __unused pendingAllowed) {

    if (!fence_valid(fd) || display == NULL) return;

    ExynosDevice* device = display->mDevice;
    hwc_fence_info_t* info = &device->mFenceInfo[fd];
    info->displayId = display->mDisplayId;
    struct timeval tv;
    //info->sync_data = get_fence_info(fd);
#ifdef USED_LEGACY_SYNC
    if (exynosHWCControl.sysFenceLogging) {
        struct sync_pt_info* pt_info = NULL;

        info->sync_data = sync_fence_info(fd);
        if (info->sync_data != NULL) {
            pt_info = sync_pt_info(info->sync_data, pt_info);
            if (pt_info !=NULL) {
                FT_LOGD("real name : %s status : %s pt_obj : %s pt_drv : %s",
                        info->sync_data->name, info->sync_data->status==1 ? "Active":"Signaled",
                        pt_info->obj_name, pt_info->driver_name);
            } else {
                FT_LOGD("real name : %s status : %d pt_info : %p",
                        info->sync_data->name, info->sync_data->status, pt_info);
            }
            sync_fence_info_free(info->sync_data);
        }
    }
#endif
    fenceTrace_t *trace = NULL;

    switch(direction) {
        case FENCE_FROM:
            trace = &info->from;
            info->to.type = FENCE_TYPE_UNDEFINED;
            info->to.ip = FENCE_IP_UNDEFINED;
            info->usage++;
            break;
        case FENCE_TO:
            trace = &info->to;
            info->usage--;
            break;
        case FENCE_DUP:
            trace = &info->dup;
            info->usage++;
            break;
        case FENCE_CLOSE:
            trace = &info->close;
            info->usage--;
            if (info->usage < 0) info->usage = 0;
            break;
        default:
            ALOGE("Fence trace : Undefined direction!");
            break;
    }

    if (trace != NULL) {
        trace->type = type;
        trace->ip = ip;
        gettimeofday(&trace->time, NULL);
        tv = trace->time;
        trace->curFlag = 1;
        FT_LOGW("FD : %d, direction : %d, type : %d, ip : %d", fd, direction, trace->type, trace->ip);
        //  device->fenceTracing.appendFormat("FD : %d, From : %s\n", fd, info->trace.fromName);
    }

#if 0
    struct tm* localTime = getLocalTime(tv);
    device->fenceTracing.appendFormat("usage : %d, time:%02d-%02d %02d:%02d:%02d.%03lu(%lu)\n", info->usage,
            localTime->tm_mon+1, localTime->tm_mday,
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, tv.tv_usec/1000,
            ((tv.tv_sec * 1000) + (tv.tv_usec / 1000)));
#endif

    // Fence's usage count shuld be zero at end of frame(present done).
    // This flag means usage count of the fence can be pended over frame.
    info->pendingAllowed = pendingAllowed;

    if (info->usage == 0)
        info->pendingAllowed = false;

    info->last_dir = direction;
}

void printLastFenceInfo(uint32_t fd, ExynosDisplay* display) {

    struct timeval tv;

    if (!fence_valid(fd)) return;

    ExynosDevice* device = display->mDevice;
    hwc_fence_info_t* info = &device->mFenceInfo[fd];

    FT_LOGD("---- Fence FD : %d, Display(%d) ----", fd, info->displayId);

    fenceTrace_t *trace = NULL;

    switch(info->last_dir) {
        case FENCE_FROM:
            trace = &info->from;
            break;
        case FENCE_TO:
            trace = &info->to;
            break;
        case FENCE_DUP:
            trace = &info->dup;
            break;
        case FENCE_CLOSE:
            trace = &info->close;
            break;
        default:
            ALOGE("Fence trace : Undefined direction!");
            break;
    }

    if (trace != NULL) {
        FT_LOGD("Last state : %d, type(%d), ip(%d)",
                info->last_dir, trace->type, trace->ip);
        tv = info->from.time;
    }

    FT_LOGD("from : %d, %d (cur : %d), to : %d, %d (cur : %d), hwc_dup : %d, %d (cur : %d),hwc_close : %d, %d (cur : %d)",
            info->from.type, info->from.ip, info->from.curFlag,
            info->to.type, info->to.ip, info->to.curFlag,
            info->dup.type, info->dup.ip, info->dup.curFlag,
            info->close.type, info->close.ip, info->close.curFlag);

    struct tm* localTime = getLocalTime(tv);

    FT_LOGD("usage : %d, time:%02d-%02d %02d:%02d:%02d.%03lu(%lu)", info->usage,
            localTime->tm_mon+1, localTime->tm_mday,
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, tv.tv_usec/1000,
            ((tv.tv_sec * 1000) + (tv.tv_usec / 1000)));
}

void dumpFenceInfo(ExynosDisplay *display, int32_t __unused depth) {

    ExynosDevice* device = display->mDevice;
    hwc_fence_info_t* info = device->mFenceInfo;

    FT_LOGD("Dump fence ++");
    for (int i=0; i<1024; i++){
        if ((info[i].usage >= 1 || info[i].usage <= -1) && (!info[i].pendingAllowed))
            printLastFenceInfo(i, display);
    }
    FT_LOGD("Dump fence --");
}

void printLeakFds(ExynosDisplay *display){

    ExynosDevice* device = display->mDevice;
    hwc_fence_info_t* info = device->mFenceInfo;

    int cnt = 1, i = 0;
    String8 errStringOne;
    String8 errStringMinus;

    errStringOne.appendFormat("Leak Fds (1) :\n");

    for (i=0; i<1024; i++){
        if(info[i].usage == 1) {
            errStringOne.appendFormat("%d,", i);
            if(cnt++%10 == 0)
                errStringOne.appendFormat("\n");
        }
    }
    FT_LOGW("%s", errStringOne.string());

    errStringMinus.appendFormat("Leak Fds (-1) :\n");

    cnt = 1;
    for (i=0; i<1024; i++){
        if(info[i].usage < 0) {
            errStringMinus.appendFormat("%d,", i);
            if(cnt++%10 == 0)
                errStringMinus.appendFormat("\n");
        }
    }
    FT_LOGW("%s", errStringMinus.string());
}

void dumpNCheckLeak(ExynosDisplay *display, int32_t __unused depth) {

    ExynosDevice* device = display->mDevice;
    hwc_fence_info_t* info = device->mFenceInfo;

    FT_LOGD("Dump leaking fence ++");
    for (int i=0; i<1024; i++){
        if ((info[i].usage >= 1 || info[i].usage <= -1) && (!info[i].pendingAllowed))
            // leak is occured in this frame first
            if (!info[i].leaking) {
                info[i].leaking = true;
                printLastFenceInfo(i, display);
            }
    }

    int priv = exynosHWCControl.fenceTracer;
    exynosHWCControl.fenceTracer = 3;
    printLeakFds(display);
    exynosHWCControl.fenceTracer = priv;

    FT_LOGD("Dump leaking fence --");
}

bool fenceWarn(ExynosDisplay *display, uint32_t threshold) {

    uint32_t cnt = 0, r_cnt = 0;
    ExynosDevice* device = display->mDevice;
    hwc_fence_info_t* info = device->mFenceInfo;

    // Set true if you want check burden
    // sync_fence_info and pt_info function include system call
    unsigned long el = 0;
    struct timeval start, end;

    if (exynosHWCControl.sysFenceLogging) {
        bool burden = false;
        if (burden) {
            gettimeofday(&start, NULL);
        }
#ifdef USED_LEGACY_SYNC
        for (int i=3; i<1024; i++){
            struct sync_fence_info_data* data = nullptr;
            data = sync_fence_info(i);
            if (data != NULL) {
                r_cnt++;
                sync_fence_info_free(data);
            }
        }

#endif
        if (burden) {
            gettimeofday(&end, NULL);
            // Get el value if you want check burden
            el = (((end.tv_sec * 1000) + (end.tv_usec / 1000)) -
                    ((start.tv_sec * 1000) + (start.tv_usec / 1000)));
        }
    }
    for (int i=0; i<1024; i++){
        if(info[i].usage >= 1 || info[i].usage <= -1)
            cnt++;
    }

    if ((cnt>threshold) || (exynosHWCControl.fenceTracer > 0))
        dumpFenceInfo(display, 0);

    if (r_cnt>threshold)
        ALOGE("Fence leak somewhare!!");

    FT_LOGD("fence hwc : %d, real : %d", cnt, r_cnt);

    return (cnt>threshold) ? true : false;

}

void resetFenceCurFlag(ExynosDisplay *display) {
    ExynosDevice* device = display->mDevice;
    hwc_fence_info_t* info = device->mFenceInfo;
    for (int i=0; i<1024; i++){
        if (info[i].usage == 0) {
            info[i].displayId = HWC_DISPLAY_PRIMARY;
            info[i].leaking = false;
            info[i].from.curFlag = 0;
            info[i].to.curFlag = 0;
            info[i].dup.curFlag = 0;
            info[i].close.curFlag = 0;
            info[i].curFlag = 0;
        } else {
            info[i].curFlag = 0;
        }
    }
}

bool validateFencePerFrame(ExynosDisplay *display) {

    ExynosDevice* device = display->mDevice;
    hwc_fence_info_t* info = device->mFenceInfo;
    bool ret = true;

    for (int i=0; i<1024; i++){
        if (info[i].displayId != display->mDisplayId)
            continue;
        if ((info[i].usage >= 1 || info[i].usage <= -1) &&
                (!info[i].pendingAllowed) && (!info[i].leaking)) {
            ret = false;
        }
    }

    if (!ret) {
        int priv = exynosHWCControl.fenceTracer;
        exynosHWCControl.fenceTracer = 3;
        dumpNCheckLeak(display, 0);
        exynosHWCControl.fenceTracer = priv;
    }

    return ret;
}

void setFenceName(uint32_t fd, ExynosDisplay *display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip,
        uint32_t direction, bool pendingAllowed) {

    ExynosDevice* device = display->mDevice;
    if (!fence_valid(fd) || device == NULL) return;

    hwc_fence_info_t* info = &device->mFenceInfo[fd];
    info->displayId = display->mDisplayId;
    fenceTrace_t *trace = NULL;

    switch(direction) {
        case FENCE_FROM:
            trace = &info->from;
            break;
        case FENCE_TO:
            trace = &info->to;
            break;
        case FENCE_DUP:
            trace = &info->dup;
            break;
        case FENCE_CLOSE:
            trace = &info->close;
            break;
        default:
            ALOGE("Fence trace : Undefined direction!");
            break;
    }

    if (trace != NULL) {
        trace->type = type;
        trace->ip = ip;
        FT_LOGD("FD : %d, direction : %d, type(%d), ip(%d) (changed)", fd, direction, type, ip);
    }

    info->pendingAllowed = pendingAllowed;

    if (info->usage == 0)
        info->pendingAllowed = false;
}

