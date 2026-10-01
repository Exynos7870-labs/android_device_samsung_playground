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
#include <sys/mman.h>
#include <videodev2.h>
#include "ExynosLayer.h"
#include "ExynosHWCHelper.h"
#include "ExynosResourceManager.h"
#include "ExynosHWCDebug.h"

#include "VendorVideoAPI.h"

using namespace android;

/**
 * ExynosLayer implementation
 */

ExynosLayer::ExynosLayer(ExynosDisplay* display)
: mDisplay(display),
    mCompositionType(HWC2_COMPOSITION_INVALID),
    mExynosCompositionType(HWC2_COMPOSITION_INVALID),
    mValidateCompositionType(HWC2_COMPOSITION_INVALID),
    mValidateExynosCompositionType(HWC2_COMPOSITION_INVALID),
    mOverlayInfo(0x0),
    mSupportedMPPFlag(0x0),
    mFps(0),
    mOverlayPriority(ePriorityLow),
    mGeometryChanged(0x0),
    mWindowIndex(0),
    mCompressed(false),
    mAcquireFence(-1),
    mReleaseFence(-1),
    mFrameCount(0),
    mLastFrameCount(0),
    mLastFpsTime(0),
    mLastLayerBuffer(NULL),
    mLayerBuffer(NULL),
    mBlending(HWC_BLENDING_NONE),
    mPlaneAlpha(0),
    mTransform(0),
    mZOrder(0),
    mDataSpace(HAL_DATASPACE_UNKNOWN),
    mLayerFlag(0x0),
    mIsDimLayer(false)
{
    mSurfaceDamage.numRects = 0;
    mSurfaceDamage.rects = NULL;
    memset(&mDisplayFrame, 0, sizeof(mDisplayFrame));
    memset(&mSourceCrop, 0, sizeof(mSourceCrop));
    mVisibleRegionScreen.numRects = 0;
    mVisibleRegionScreen.rects = NULL;
    memset(&mColor, 0, sizeof(mColor));
    memset(&mPreprocessedInfo, 0, sizeof(mPreprocessedInfo));
    mCheckMPPFlag.clear();
    mCheckMPPFlag.reserve(MPP_LOGICAL_TYPE_NUM);

    ExynosMPPSource(MPP_SOURCE_LAYER, this);
}

ExynosLayer::~ExynosLayer() {
}

/**
 * @param type
 * @return int32_t
 */
int32_t ExynosLayer::setCompositionType(int32_t __unused type) {
    return 0;
}

/**
 * @param timeDiff
 * @return uint32_t
 */
uint32_t ExynosLayer::checkFps() {
    uint32_t frameDiff;
    if (mLastLayerBuffer != mLayerBuffer) {
        mFrameCount++;
    }
    nsecs_t now = systemTime();
    nsecs_t diff = now - mLastFpsTime;
    if (mFrameCount >= mLastFrameCount)
        frameDiff = (mFrameCount - mLastFrameCount);
    else
        frameDiff = (mFrameCount + (UINT_MAX - mLastFrameCount));

    if (diff >= ms2ns(250)) {
        mFps = (uint32_t)(frameDiff * float(s2ns(1))) / diff;
        mLastFrameCount = mFrameCount;
        mLastFpsTime = now;
    }
    return mFps;
}

/**
 * @return uint32_t
 */
uint32_t ExynosLayer::getFps() {
    return mFps;
}

/**
 * @param flag
 * @return int
 */
int ExynosLayer::setSupportedMPPFlag(uint32_t __unused flag) {
    mGeometryChanged |= GEOMETRY_MPP_FLAG_CHANGED;
    return 0;
}

int32_t ExynosLayer::doPreProcess()
{
    //TODO mGeometryChanged  here
    mOverlayPriority = ePriorityLow;

    mPreprocessedInfo.preProcessed = false;
    mPreprocessedInfo.sourceCrop = mSourceCrop;
    mPreprocessedInfo.displayFrame = mDisplayFrame;
    mPreprocessedInfo.interlacedType = V4L2_FIELD_NONE;

    if (mLayerBuffer == NULL)
        return NO_ERROR;

    if (isFormatYUV(mLayerBuffer->format)) {
        mPreprocessedInfo.sourceCrop.top = (int)mSourceCrop.top;
        mPreprocessedInfo.sourceCrop.left = (int)mSourceCrop.left;
        mPreprocessedInfo.sourceCrop.bottom = (int)(mSourceCrop.bottom + 0.9);
        mPreprocessedInfo.sourceCrop.right = (int)(mSourceCrop.right + 0.9);
        mPreprocessedInfo.preProcessed = true;
    }

    /* support to process interlaced color format data */
    if (mLayerBuffer->format == HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_PRIV) {
        ExynosVideoMeta *metaData = NULL;
        int interlacedType = -1;

        if (mLayerBuffer->fd2 >= 0) {
            metaData = (ExynosVideoMeta *)mmap(0, sizeof(ExynosVideoMeta), PROT_READ|PROT_WRITE, MAP_SHARED, mLayerBuffer->fd2, 0);
            if ((metaData) &&
                (metaData->eType & VIDEO_INFO_TYPE_INTERLACED)) {
                interlacedType = metaData->data.dec.nInterlacedType;
            } else {
                interlacedType = -1;
            }
        }

        if (interlacedType != -1)
            mPreprocessedInfo.interlacedType = interlacedType;

        if (interlacedType == V4L2_FIELD_INTERLACED_BT) {
            if ((int)mSourceCrop.left < (int)(mLayerBuffer->stride)) {
                mPreprocessedInfo.sourceCrop.left = (int)mSourceCrop.left + mLayerBuffer->stride;
                mPreprocessedInfo.sourceCrop.right = (int)mSourceCrop.right + mLayerBuffer->stride;
            }
        }
        if (interlacedType == V4L2_FIELD_INTERLACED_TB || interlacedType == V4L2_FIELD_INTERLACED_BT) {
            mPreprocessedInfo.sourceCrop.top = (int)(mSourceCrop.top)/2;
            mPreprocessedInfo.sourceCrop.bottom = (int)(mSourceCrop.bottom)/2;
        }

        if (metaData)
            munmap(metaData, 64);

        mPreprocessedInfo.preProcessed = true;
    }

    exynos_image src_img;
    exynos_image dst_img;
    setSrcExynosImage(&src_img);
    setDstExynosImage(&dst_img);
    uint32_t standard_dataspace = (src_img.dataSpace & HAL_DATASPACE_STANDARD_MASK);
    ExynosMPP *exynosMPPVG = ExynosResourceManager::getExynosMPP(MPP_LOGICAL_DPP_VG);

    if (isFormatYUV(mLayerBuffer->format)) {
        /*
         * layer's sourceCrop should be aligned
         */
        uint32_t srcCropXAlign = exynosMPPVG->getSrcXOffsetAlign(src_img);
        uint32_t srcCropYAlign = exynosMPPVG->getSrcYOffsetAlign(src_img);
        uint32_t srcCropWidthAlign = exynosMPPVG->getSrcWidthAlign(src_img);
        uint32_t srcCropHeightAlign = exynosMPPVG->getSrcHeightAlign(src_img);
        mPreprocessedInfo.sourceCrop.left = pixel_align((int)mPreprocessedInfo.sourceCrop.left, srcCropXAlign);
        mPreprocessedInfo.sourceCrop.top = pixel_align((int)mPreprocessedInfo.sourceCrop.top, srcCropYAlign);
        mPreprocessedInfo.sourceCrop.right = mPreprocessedInfo.sourceCrop.left +
            pixel_align_down(WIDTH(mPreprocessedInfo.sourceCrop), srcCropWidthAlign);
        mPreprocessedInfo.sourceCrop.bottom = mPreprocessedInfo.sourceCrop.top +
            pixel_align_down(HEIGHT(mPreprocessedInfo.sourceCrop), srcCropHeightAlign);

    }

    if ((getDrmMode(mLayerBuffer) != NO_DRM) ||
        (standard_dataspace == HAL_DATASPACE_STANDARD_BT2020) ||
        (standard_dataspace == HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE) ||
        (standard_dataspace == HAL_DATASPACE_STANDARD_DCI_P3))
    {
        if ((mSupportedMPPFlag & (MPP_LOGICAL_DPP_G | MPP_LOGICAL_DPP_VG | MPP_LOGICAL_DPP_VGF)) == 0)
        {
            /*
             * M2mMPP should be used for DRM, HDR video
             * layer's displayFrame is the source of DPP
             */
            uint32_t cropWidthAlign = exynosMPPVG->getSrcCropWidthAlign(src_img);
            uint32_t cropHeightAlign = exynosMPPVG->getSrcCropHeightAlign(src_img);

            mPreprocessedInfo.displayFrame.right = mDisplayFrame.left +
                pixel_align(WIDTH(mDisplayFrame), cropWidthAlign);
            mPreprocessedInfo.displayFrame.bottom = mDisplayFrame.top +
                pixel_align(HEIGHT(mDisplayFrame), cropHeightAlign);

            if (mPreprocessedInfo.displayFrame.right > (int)(mDisplay->mXres)) {
                mPreprocessedInfo.displayFrame.left = mDisplay->mXres -
                    pixel_align(WIDTH(mPreprocessedInfo.displayFrame), cropWidthAlign);
                mPreprocessedInfo.displayFrame.right = mDisplay->mXres;
            }

            if (mPreprocessedInfo.displayFrame.bottom > (int)(mDisplay->mYres)) {
                mPreprocessedInfo.displayFrame.top = mDisplay->mYres -
                    pixel_align(HEIGHT(mPreprocessedInfo.displayFrame), cropHeightAlign);
                mPreprocessedInfo.displayFrame.bottom = mDisplay->mYres;
            }
        }

        uint32_t minDstWidth = exynosMPPVG->getDstMinWidth(dst_img);
        uint32_t minDstHeight = exynosMPPVG->getDstMinHeight(dst_img);
        if ((uint32_t)WIDTH(mDisplayFrame) < minDstWidth) {
            HWC_LOGE(mDisplay, "DRM layer displayFrame width %d is smaller than otf minWidth %d",
                    WIDTH(mDisplayFrame), minDstWidth);
            mPreprocessedInfo.displayFrame.right = mDisplayFrame.left +
                pixel_align(WIDTH(mDisplayFrame), minDstWidth);

            if (mPreprocessedInfo.displayFrame.right > (int)(mDisplay->mXres)) {
                mPreprocessedInfo.displayFrame.left = mDisplay->mXres -
                    pixel_align(WIDTH(mPreprocessedInfo.displayFrame), minDstWidth);
                mPreprocessedInfo.displayFrame.right = mDisplay->mXres;
            }
        }
        if ((uint32_t)HEIGHT(mDisplayFrame) < minDstHeight) {
            HWC_LOGE(mDisplay, "DRM layer displayFrame height %d is smaller than vpp minHeight %d",
                    HEIGHT(mDisplayFrame), minDstHeight);
            mPreprocessedInfo.displayFrame.bottom = mDisplayFrame.top +
                pixel_align(HEIGHT(mDisplayFrame), minDstHeight);

            if (mPreprocessedInfo.displayFrame.bottom > (int)(mDisplay->mYres)) {
                mPreprocessedInfo.displayFrame.top = mDisplay->mYres -
                    pixel_align(HEIGHT(mPreprocessedInfo.displayFrame), minDstHeight);
                mPreprocessedInfo.displayFrame.bottom = mDisplay->mYres;
            }
        }
        mPreprocessedInfo.preProcessed = true;
    }

    if ((mLayerBuffer != NULL) && (getDrmMode(mLayerBuffer) != NO_DRM)) {
        mOverlayPriority = ePriorityMax;
    } else if ((standard_dataspace == HAL_DATASPACE_STANDARD_BT2020) ||
               (standard_dataspace == HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE) ||
               (standard_dataspace == HAL_DATASPACE_STANDARD_DCI_P3)) {
        mOverlayPriority = ePriorityHigh;
    } else {
        mOverlayPriority = ePriorityLow;
    }

    return NO_ERROR;
}

int32_t ExynosLayer::setCursorPosition(int32_t __unused x, int32_t __unused y) {
    return 0;
}


int32_t ExynosLayer::setLayerBuffer(buffer_handle_t buffer, int32_t acquireFence) {

    /* TODO : Exception here ? */
    //TODO mGeometryChanged  here

    private_handle_t *handle = NULL;
    uint64_t internal_format = 0;

    if (buffer != NULL) {
        handle = (private_handle_t*)buffer;
        internal_format = handle->internal_format;
    }

    mLayerBuffer = handle;
    mAcquireFence = hwcCheckFenceDebug(mDisplay, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER, acquireFence);
    mReleaseFence = -1;
#ifdef DISABLE_FENCE
    if (mAcquireFence >= 0)
        fence_close(mAcquireFence);
    mAcquireFence = -1;
#endif
    mCompressed = isCompressed(mLayerBuffer);

    if (handle != NULL) {
        /*
         * HAL_DATASPACE_V0_JFIF = HAL_DATASPACE_STANDARD_BT601_625 |
         * HAL_DATASPACE_TRANSFER_SMPTE_170M | HAL_DATASPACE_RANGE_FULL,
         */
        if (handle->format == HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL)
            mDataSpace = HAL_DATASPACE_V0_JFIF;
    } else {
        mDataSpace = HAL_DATASPACE_UNKNOWN;
    }

    HDEBUGLOGD(eDebugFence, "layers bufferHandle: %p, mDataSpace: 0x%8x, acquireFence: %d, compressed: %d, internal_format: 0x%" PRIx64 "",
            mLayerBuffer, mDataSpace, mAcquireFence, mCompressed,
            internal_format);

    return 0;
}


int32_t ExynosLayer::setLayerSurfaceDamage(hwc_region_t damage) {

    /* TODO : Exception here ? */
    //TODO mGeometryChanged  here

    mSurfaceDamage = damage;

    return 0;
}

int32_t ExynosLayer::setLayerBlendMode(int32_t /*hwc2_blend_mode_t*/ mode) {

    //TODO mGeometryChanged  here
    if (mode < 0)
        return HWC2_ERROR_BAD_PARAMETER;
    mBlending = mode;
    return HWC2_ERROR_NONE;
}


int32_t ExynosLayer::setLayerColor(hwc_color_t color) {
    /* TODO : Implementation here */
    mColor = color;
    return 0;
}

int32_t ExynosLayer::setLayerCompositionType(int32_t /*hwc2_composition_t*/ type) {

    if (type < 0)
        return HWC2_ERROR_BAD_PARAMETER;

    mCompositionType = type;

    return HWC2_ERROR_NONE;
}

int32_t ExynosLayer::setLayerDataspace(int32_t /*android_dataspace_t*/ dataspace) {
    if ((mLayerBuffer != NULL) && (mLayerBuffer->format == HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL))
        mDataSpace = HAL_DATASPACE_V0_JFIF;
    else {
        /* Change legacy dataspace */
        switch (dataspace) {
        case HAL_DATASPACE_SRGB_LINEAR:
            mDataSpace = HAL_DATASPACE_V0_SRGB_LINEAR;
            break;
        case HAL_DATASPACE_SRGB:
            mDataSpace = HAL_DATASPACE_V0_SRGB;
            break;
        case HAL_DATASPACE_JFIF:
            mDataSpace = HAL_DATASPACE_V0_JFIF;
            break;
        case HAL_DATASPACE_BT601_625:
            mDataSpace = HAL_DATASPACE_V0_BT601_625;
            break;
        case HAL_DATASPACE_BT601_525:
            mDataSpace = HAL_DATASPACE_V0_BT601_525;
            break;
        case HAL_DATASPACE_BT709:
            mDataSpace = HAL_DATASPACE_V0_BT709;
            break;
        default:
            mDataSpace = (android_dataspace)dataspace;
            break;
        }
    }

    return HWC2_ERROR_NONE;
}

int32_t ExynosLayer::setLayerDisplayFrame(hwc_rect_t frame) {

    /* TODO : Exception here ? */

    mDisplayFrame = frame;

    return HWC2_ERROR_NONE;
}

int32_t ExynosLayer::setLayerPlaneAlpha(float alpha) {

    if (alpha < 0)
        return HWC2_ERROR_BAD_LAYER;

    mPlaneAlpha = alpha;

    return HWC2_ERROR_NONE;
}

int32_t ExynosLayer::setLayerSidebandStream(const native_handle_t* __unused stream) {
    return 0;
}

int32_t ExynosLayer::setLayerSourceCrop(hwc_frect_t crop) {

    /* TODO : Exception here ? */

    mSourceCrop = crop;

    return HWC2_ERROR_NONE;
}

int32_t ExynosLayer::setLayerTransform(int32_t /*hwc_transform_t*/ transform) {

    mTransform = transform;

    return HWC2_ERROR_NONE;
}

int32_t ExynosLayer::setLayerVisibleRegion(hwc_region_t visible) {

    /* TODO : Exception here ? */

    mVisibleRegionScreen = visible;

    return HWC2_ERROR_NONE;
}

int32_t ExynosLayer::setLayerZOrder(uint32_t z) {
    /* TODO : need to check minus?*/
    mZOrder = z;
    return HWC2_ERROR_NONE;
}

int32_t ExynosLayer::setLayerFlag(int32_t /*user define*/ flag)
{
    mLayerFlag = flag;
    mIsDimLayer = !!(mLayerFlag & HWC_DIM_LAYER);

    return HWC2_ERROR_NONE;
}

/* TODO : to using sorting vector */
int ExynosLayer::do_compare(const void* lhs,
        const void* rhs) const
{
    if (lhs == NULL || rhs == NULL)
        return 0;

#if 0
    uint8_t mppNum = sizeof(VPP_ASSIGN_ORDER)/sizeof(VPP_ASSIGN_ORDER[0]);

    if (l == NULL || r == NULL)
        return 0;

    if (l->mType != r->mType) {
        uint8_t lhsOrder = 0;
        uint8_t rhsOrder = 0;

        for (uint8_t i = 0; i < mppNum; i++) {
            if (l->mType == VPP_ASSIGN_ORDER[i]) {
                lhsOrder = i;
                break;
            }
        }
        for (uint8_t i = 0; i < mppNum; i++) {
            if (r->mType == VPP_ASSIGN_ORDER[i]) {
                rhsOrder = i;
                break;
            }
        }
        return lhsOrder - rhsOrder;
    }

    return l->mIndex - r->mIndex;
#endif
    return 0;
}

void ExynosLayer::resetValidateData()
{
    mValidateCompositionType = HWC2_COMPOSITION_INVALID;
    mOtfMPP = NULL;
    mM2mMPP = NULL;
    mOverlayInfo = 0x0;
    mWindowIndex = 0;
}

int32_t ExynosLayer::setSrcExynosImage(exynos_image *src_img)
{
    private_handle_t *handle = mLayerBuffer;
    if (mIsDimLayer) {
        src_img->format = HAL_PIXEL_FORMAT_RGBA_8888;
        src_img->handleFlags = 0xb00;
        src_img->bufferHandle = 0;

        src_img->x = 0;
        src_img->y = 0;

        if (mDisplay != NULL) {
            src_img->fullWidth = src_img->w = mDisplay->mXres;
            src_img->fullHeight = src_img->h = mDisplay->mYres;
        } else {
            src_img->fullWidth = src_img->w = 1440;
            src_img->fullHeight = src_img->h = 2560;
        }

        src_img->layerFlags = mLayerFlag;
        src_img->acquireFenceFd = mAcquireFence;
        src_img->releaseFenceFd = -1;
        src_img->dataSpace = HAL_DATASPACE_SRGB;
        src_img->blending = mBlending;
        src_img->transform = mTransform;
        src_img->compressed = mCompressed;
        src_img->planeAlpha = mPlaneAlpha;
        src_img->zOrder = mZOrder;

        return NO_ERROR;
    }

    if (handle == NULL) {
        src_img->fullWidth = 0;
        src_img->fullHeight = 0;
        src_img->format = 0;
        src_img->handleFlags = 0x0;
        src_img->bufferHandle = handle;
    } else {
        if ((mPreprocessedInfo.interlacedType == V4L2_FIELD_INTERLACED_TB) ||
            (mPreprocessedInfo.interlacedType == V4L2_FIELD_INTERLACED_BT))
        {
            src_img->fullWidth = (handle->stride * 2);
            src_img->fullHeight = pixel_align_down((handle->vstride / 2), 2);
        } else {
            src_img->fullWidth = handle->stride;
            src_img->fullHeight = handle->vstride;
        }
        src_img->format = handle->format;
#ifdef GRALLOC_VERSION1
        src_img->handleFlags = handle->producer_usage;
#else
        src_img->handleFlags = handle->flags;
#endif
        src_img->bufferHandle = handle;
    }
    src_img->x = (int)mPreprocessedInfo.sourceCrop.left;
    src_img->y = (int)mPreprocessedInfo.sourceCrop.top;
    src_img->w = (int)mPreprocessedInfo.sourceCrop.right - (int)mPreprocessedInfo.sourceCrop.left;
    src_img->h = (int)mPreprocessedInfo.sourceCrop.bottom - (int)mPreprocessedInfo.sourceCrop.top;
    if ((mPreprocessedInfo.interlacedType == V4L2_FIELD_INTERLACED_TB) ||
        (mPreprocessedInfo.interlacedType == V4L2_FIELD_INTERLACED_BT))
    {
        while ((src_img->h % 2 != 0) ||
               (src_img->h > src_img->fullHeight)) {
            src_img->h -= 1;
        }
    }
    src_img->layerFlags = mLayerFlag;
    src_img->acquireFenceFd = mAcquireFence;
    src_img->releaseFenceFd = -1;
    src_img->dataSpace = mDataSpace;
    src_img->blending = mBlending;
    src_img->transform = mTransform;
    src_img->compressed = mCompressed;
    src_img->planeAlpha = mPlaneAlpha;
    src_img->zOrder = mZOrder;

    return NO_ERROR;
}

int32_t ExynosLayer::setDstExynosImage(exynos_image *dst_img)
{
    private_handle_t *handle = mLayerBuffer;

    if (handle == NULL) {
        dst_img->handleFlags = 0x0;
    } else {
#ifdef GRALLOC_VERSION1
        dst_img->handleFlags = handle->producer_usage;
#else
        dst_img->handleFlags = handle->flags;
#endif
    }

    if (mIsDimLayer) {
        dst_img->handleFlags = 0xb00;
    }

    dst_img->format = DEFAULT_MPP_DST_FORMAT;
    dst_img->x = mPreprocessedInfo.displayFrame.left;
    dst_img->y = mPreprocessedInfo.displayFrame.top;
    dst_img->w = (mPreprocessedInfo.displayFrame.right - mPreprocessedInfo.displayFrame.left);
    dst_img->h = (mPreprocessedInfo.displayFrame.bottom - mPreprocessedInfo.displayFrame.top);
    dst_img->fullWidth = dst_img->w;
    dst_img->fullHeight = dst_img->h;
    dst_img->layerFlags = mLayerFlag;
    dst_img->acquireFenceFd = -1;
    dst_img->releaseFenceFd = -1;
    dst_img->bufferHandle = NULL;
    dst_img->dataSpace = HAL_DATASPACE_SRGB;
    dst_img->blending = mBlending;
    dst_img->transform = mTransform;
    dst_img->compressed = 0;
    dst_img->planeAlpha = mPlaneAlpha;
    dst_img->zOrder = mZOrder;

    return NO_ERROR;
}

int32_t ExynosLayer::resetAssignedResource()
{
    int32_t ret = NO_ERROR;
    if (mM2mMPP != NULL) {
        HDEBUGLOGD(eDebugResourceManager, "\t\t %s mpp is reset", mM2mMPP->mName.string());
        mM2mMPP->resetAssignedState(this);
        mM2mMPP = NULL;
    }
    if (mOtfMPP != NULL) {
        HDEBUGLOGD(eDebugResourceManager, "\t\t %s mpp is reset", mOtfMPP->mName.string());
        mOtfMPP->resetAssignedState();
        mOtfMPP = NULL;
    }
    return ret;
}

void ExynosLayer::dump(String8& result)
{
    int format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
    int32_t fd, fd1, fd2;
    if (mLayerBuffer != NULL)
    {
        format = mLayerBuffer->format;
        fd = mLayerBuffer->fd;
        fd1 = mLayerBuffer->fd1;
        fd2 = mLayerBuffer->fd2;
    } else {
        format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
        fd = -1;
        fd1 = -1;
        fd2 = -1;
    }

    result.appendFormat("+---------------+------------+------+------+------------+-----------+---------+------------+--------+------------------------+-----+----------+--------------------+\n");
    result.appendFormat("|     handle    |     fd     |  tr  | AFBC | dataSpace  |  format   |  blend  | planeAlpha | zOrder |          color         | fps | priority | windowIndex        | \n");
    result.appendFormat("+---------------+------------+------+------+------------+-----------+---------+------------+--------+------------------------+-----+----------+--------------------+\n");
    result.appendFormat("|  %8p | %d, %d, %d | 0x%2x |   %1d  | 0x%8x | %s | 0x%4x  |    %3.1f   |    %d   | 0x%2x, 0x%2x, 0x%2x, 0x%2x |  %2d  |    %d     |    %d               |\n",
            mLayerBuffer, fd, fd1, fd2, mTransform, mCompressed, mDataSpace, getFormatStr(format).string(),
            mBlending, mPlaneAlpha, mZOrder, mColor.r, mColor.g, mColor.b, mColor.a, mFps, mOverlayPriority, mWindowIndex);
    result.appendFormat("|               +--------------------------------------------------------------------------------------------------------------------------------------------------+ \n");
    result.appendFormat("|               |            sourceCrop           |          dispFrame       | type | exynosType | validateType | overlayInfo | supportedMPPFlag | geometryChanged | \n");
    result.appendFormat("|               +---------------------------------+--------------------------+------+------------+--------------+-------------+------------------+-----------------+ \n");
    result.appendFormat("|               | %7.1f,%7.1f,%7.1f,%7.1f | %5d,%5d,%5d,%5d  |  %2d  |     %2d     |      %2d      |  0x%8x |    0x%8x    | 0x%8x      |\n",
            mPreprocessedInfo.sourceCrop.left, mPreprocessedInfo.sourceCrop.top, mPreprocessedInfo.sourceCrop.right, mPreprocessedInfo.sourceCrop.bottom,
            mPreprocessedInfo.displayFrame.left, mPreprocessedInfo.displayFrame.top, mPreprocessedInfo.displayFrame.right, mPreprocessedInfo.displayFrame.bottom,
            mCompositionType, mExynosCompositionType, mValidateCompositionType, mOverlayInfo, mSupportedMPPFlag, mGeometryChanged);
    result.appendFormat("|               +--------------------------------------------------------------------------------------------------------------------------------------------------+\n");
    result.appendFormat("|               |   DPP_G    |   DPP_VG   |   DPP_VGF  |     MSC    |  MSC_YUV   |  G2D_YUV   |  G2D_RGB  |  G2D_COMBO                                             |\n");
    result.appendFormat("|               +------------+------------+------------+------------+------------+------------+-----------+--------------------------------------------------------+\n");
    result.appendFormat("|               | 0x%8x | 0x%8x | 0x%8x | 0x%8x | 0x%8x | 0x%8x | 0x%8x| 0x%8x                                             |\n",
            mCheckMPPFlag[MPP_LOGICAL_DPP_G], mCheckMPPFlag[MPP_LOGICAL_DPP_VG], mCheckMPPFlag[MPP_LOGICAL_DPP_VGF],
            mCheckMPPFlag[MPP_LOGICAL_MSC], mCheckMPPFlag[MPP_LOGICAL_MSC_YUV], mCheckMPPFlag[MPP_LOGICAL_G2D_YUV],
            mCheckMPPFlag[MPP_LOGICAL_G2D_RGB], mCheckMPPFlag[MPP_LOGICAL_G2D_COMBO]);
    result.appendFormat("+-------------- +------------+------------+------------+------------+------------+------------+-----------+--------------------------------------------------------+\n");
    result.appendFormat("acquireFence: %d\n", mAcquireFence);
    if ((mOtfMPP == NULL) && (mM2mMPP == NULL))
        result.appendFormat("\tresource is not assigned.\n");
    if (mOtfMPP != NULL)
        result.appendFormat("\tassignedMPP: %s\n", mOtfMPP->mName.string());
    if (mM2mMPP != NULL)
        result.appendFormat("\tassignedM2mMPP: %s\n", mM2mMPP->mName.string());
    result.appendFormat("\tdump midImg\n");
    dumpExynosImage(result, mMidImg);

}

void ExynosLayer::printLayer()
{
    int format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
    int32_t fd, fd1, fd2;
    String8 result;
    if (mLayerBuffer != NULL)
    {
        format = mLayerBuffer->format;
        fd = mLayerBuffer->fd;
        fd1 = mLayerBuffer->fd1;
        fd2 = mLayerBuffer->fd2;
    } else {
        format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
        fd = -1;
        fd1 = -1;
        fd2 = -1;
    }
    result.appendFormat("handle: %p [fd: %d, %d, %d], acquireFence: %d, tr: 0x%2x, AFBC: %1d, dataSpace: 0x%8x, format: %s\n",
            mLayerBuffer, fd, fd1, fd2, mAcquireFence, mTransform, mCompressed, mDataSpace, getFormatStr(format).string());
    result.appendFormat("\tblend: 0x%4x, planeAlpha: %3.1f, zOrder: %d, color[0x%2x, 0x%2x, 0x%2x, 0x%2x]\n",
            mBlending, mPlaneAlpha, mZOrder, mColor.r, mColor.g, mColor.b, mColor.a);
    result.appendFormat("\tfps: %d, priority: %d, windowIndex: %d\n", mFps, mOverlayPriority, mWindowIndex);
    result.appendFormat("\tsourceCrop[%7.1f,%7.1f,%7.1f,%7.1f], dispFrame[%5d,%5d,%5d,%5d]\n",
            mSourceCrop.left, mSourceCrop.top, mSourceCrop.right, mSourceCrop.bottom,
            mDisplayFrame.left, mDisplayFrame.top, mDisplayFrame.right, mDisplayFrame.bottom);
    result.appendFormat("\ttype: %2d, exynosType: %2d, validateType: %2d\n",
            mCompositionType, mExynosCompositionType, mValidateCompositionType);
    result.appendFormat("\toverlayInfo: 0x%8x, supportedMPPFlag: 0x%8x, geometryChanged: 0x%8x\n",
            mOverlayInfo, mSupportedMPPFlag, mGeometryChanged);
    result.appendFormat("\tDPP_G: 0x%8x, DPP_VG: 0x%8x, DPP_VGF: 0x%8x, MSC: 0x%8x, MSC_YUV: 0x%8x, G2D_YUV: 0x%8x, G2D_RGB: 0x%8x, G2D_COMBO: 0x%8x\n",
            mCheckMPPFlag[MPP_LOGICAL_DPP_G], mCheckMPPFlag[MPP_LOGICAL_DPP_VG], mCheckMPPFlag[MPP_LOGICAL_DPP_VGF],
            mCheckMPPFlag[MPP_LOGICAL_MSC], mCheckMPPFlag[MPP_LOGICAL_MSC_YUV], mCheckMPPFlag[MPP_LOGICAL_G2D_YUV], mCheckMPPFlag[MPP_LOGICAL_G2D_RGB], mCheckMPPFlag[MPP_LOGICAL_G2D_COMBO]);
    ALOGD("%s", result.string());
    result.clear();

    if ((mOtfMPP == NULL) && (mM2mMPP == NULL))
        ALOGD("\tresource is not assigned.");
    if (mOtfMPP != NULL)
        ALOGD("\tassignedMPP: %s", mOtfMPP->mName.string());
    if (mM2mMPP != NULL)
        ALOGD("\tassignedM2mMPP: %s", mM2mMPP->mName.string());
    ALOGD("\t++ dump midImg ++");
    dumpExynosImage(result, mMidImg);
    ALOGD("%s", result.string());

}
