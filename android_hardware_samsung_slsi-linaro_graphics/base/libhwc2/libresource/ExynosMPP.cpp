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

/**
 * Project HWC 2.0 Design
 */
#include <utils/Errors.h>
#include <android/sync.h>
#include <sys/mman.h>
#include <cutils/properties.h>
#include "ExynosMPP.h"
#include "exynos_scaler.h"
#ifdef GRALLOC_VERSION1
#include "gralloc1_priv.h"
#else
#include "gralloc_priv.h"
#endif
#include "ExynosHWCDebug.h"
#include "ExynosDisplay.h"
#include "ExynosLayer.h"
#include "ExynosHWCHelper.h"
#include "exynos_sync.h"

/**
 * ExynosMPP implementation
 *
 * Abstraction class for HW Resource
 */

using namespace android;

int ExynosMPP::mainDisplayWidth = 0;
int ExynosMPP::mainDisplayHeight = 0;
extern struct exynos_hwc_cotrol exynosHWCControl;

void dumpExynosMPPImgInfo(uint32_t type, exynos_mpp_img_info &imgInfo)
{
    HDEBUGLOGD(type, "\tbuffer: %p, bufferType: %d, mppImg: size[%d, %d, %d, %d, %d, %d], "
            "format: %d, addr[%lu, %lu, %lu], rot: %d, cacheable: %d, drmMode: %d, narrowRgb: %d, "
            "fence[%d, %d], memtype: %d, pre_multi: %d",
            imgInfo.bufferHandle, imgInfo.bufferType,
            imgInfo.mppImg.x, imgInfo.mppImg.y, imgInfo.mppImg.w, imgInfo.mppImg.h, imgInfo.mppImg.fw, imgInfo.mppImg.fh,
            imgInfo.mppImg.format, imgInfo.mppImg.yaddr, imgInfo.mppImg.uaddr, imgInfo.mppImg.vaddr,
            imgInfo.mppImg.rot, imgInfo.mppImg.cacheable, imgInfo.mppImg.drmMode, imgInfo.mppImg.narrowRgb,
            imgInfo.mppImg.acquireFenceFd, imgInfo.mppImg.releaseFenceFd, imgInfo.mppImg.mem_type,
            imgInfo.mppImg.pre_multi);
}

bool exynosMPPSourceComp(const ExynosMPPSource* l, const ExynosMPPSource* r)
{
    if (l == NULL || r == NULL) {
        HWC_LOGE(NULL, "exynosMPP compare error");
        return 0;
    }
    return (l->mSrcImg.zOrder < r->mSrcImg.zOrder);
}

ExynosMPPSource::ExynosMPPSource()
    : mSourceType(MPP_SOURCE_NO_TYPE),
    mSource(NULL),
    mOtfMPP(NULL),
    mM2mMPP(NULL)
{
    memset(&mSrcImg, 0, sizeof(mSrcImg));
    mSrcImg.acquireFenceFd = -1;
    mSrcImg.releaseFenceFd = -1;
    memset(&mDstImg, 0, sizeof(mDstImg));
    mDstImg.acquireFenceFd = -1;
    mDstImg.releaseFenceFd = -1;
    memset(&mMidImg, 0, sizeof(mMidImg));
    mMidImg.acquireFenceFd = -1;
    mMidImg.releaseFenceFd = -1;
}

ExynosMPPSource::ExynosMPPSource(uint32_t sourceType, void *source)
    : mSourceType(sourceType),
    mSource(source),
    mOtfMPP(NULL),
    mM2mMPP(NULL)
{
    memset(&mSrcImg, 0, sizeof(mSrcImg));
    mSrcImg.acquireFenceFd = -1;
    mSrcImg.releaseFenceFd = -1;
    memset(&mDstImg, 0, sizeof(mDstImg));
    mDstImg.acquireFenceFd = -1;
    mDstImg.releaseFenceFd = -1;
    memset(&mMidImg, 0, sizeof(mMidImg));
    mMidImg.acquireFenceFd = -1;
    mMidImg.releaseFenceFd = -1;
}

void ExynosMPPSource::setExynosImage(exynos_image src_img, exynos_image dst_img)
{
    mSrcImg = src_img;
    mDstImg = dst_img;
}

void ExynosMPPSource::setExynosMidImage(exynos_image mid_img)
{
    mMidImg = mid_img;
}

ExynosMPP::ExynosMPP(uint32_t physicalType, uint32_t logicalType, const char *name,
        uint32_t physicalIndex, uint32_t logicalIndex, uint32_t preAssignInfo)
: mMPPType(MPP_TYPE_NONE),
    mPhysicalType(physicalType),
    mLogicalType(logicalType),
    mName(name),
    mPhysicalIndex(physicalIndex),
    mLogicalIndex(logicalIndex),
    mPreAssignDisplayList(preAssignInfo),
    mHWState(MPP_HW_STATE_IDLE),
    mLastStateFenceFd(-1),
    mAssignedState(MPP_ASSIGN_STATE_FREE),
    mEnable(true),
    mAssignedDisplay(NULL),
    mMaxSrcLayerNum(1),
    mPrevAssignedState(MPP_ASSIGN_STATE_FREE),
    mPrevAssignedDisplayType(-1),
    mReservedDisplay(-1),
#ifdef GRALLOC_VERSION1
    mAllocator(NULL),
    mMapper(NULL),
#else
    mAllocDevice(NULL),
#endif
    mResourceManageThread(this),
    mCapacity(-1),
    mUsedCapacity(0),
    mAllocOutBufFlag(true),
    mFreeOutBufFlag(true),
    mHWBusyFlag(false),
    mCurrentDstBuf(0),
    mNeedCompressedTarget(false),
    mMPPHandle(NULL)
{
    if (mPhysicalType == MPP_G2D) {
        if (mLogicalType == MPP_LOGICAL_G2D_RGB) {

            char value[256];
            int afbc_prop;
            property_get("ro.vendor.ddk.set.afbc", value, "0");
            afbc_prop = atoi(value);
            if (afbc_prop == 0)
                mNeedCompressedTarget = false;
            else
                mNeedCompressedTarget = true;

            mMaxSrcLayerNum = G2D_MAX_SRC_NUM;
        } else if (mLogicalType == MPP_LOGICAL_G2D_COMBO) {
            mMaxSrcLayerNum = G2D_MAX_SRC_NUM - 1;
            mAllocOutBufFlag = false;
            mNeedCompressedTarget = false;
        }
        /* Capacity means time(ms) that can be used for operation */
        mCapacity = MPP_G2D_CAPACITY;

        mCompositor = AcrylicFactory::createAcrylic("default_compositor");
        if (mCompositor == NULL) {
            MPP_LOGE("Fail to allocate compositor");
        } else {
            mMPPHandle = mCompositor;
            MPP_LOGI("Compositor is created: %p", mMPPHandle);
            if (mLogicalType != MPP_LOGICAL_G2D_YUV)
                mCompositor->setDefaultColor(0, 0, 0, 0);
        }
    }

    if (mPhysicalType == MPP_MSC) {
        /* To do
        * Capacity should be set
        */
        mCapacity = -1;
    }

    mAssignedSources.clear();
    setupRestriction();
    mResourceManageThread.mRunning = true;
    mResourceManageThread.run("MPPThread");

    memset(&mPrevFrameInfo, 0, sizeof(mPrevFrameInfo));

    for (uint32_t i = 0; i < NUM_MPP_SRC_BUFS; i++) {
        memset(&mSrcImgs[i], 0, sizeof(exynos_mpp_img_info));
        if (mPhysicalType == MPP_MSC) {
            mSrcImgs[i].mppImg.acquireFenceFd = -1;
            mSrcImgs[i].mppImg.releaseFenceFd = -1;
        } else {
            mSrcImgs[i].acrylicAcquireFenceFd = -1;
            mSrcImgs[i].acrylicReleaseFenceFd = -1;
        }
    }
    for (uint32_t i = 0; i < NUM_MPP_DST_BUFS(mLogicalType); i++) {
        memset(&mDstImgs[i], 0, sizeof(exynos_mpp_img_info));
        if (mPhysicalType == MPP_MSC) {
            mDstImgs[i].mppImg.acquireFenceFd = -1;
            mDstImgs[i].mppImg.releaseFenceFd = -1;
        } else {
            mDstImgs[i].acrylicAcquireFenceFd = -1;
            mDstImgs[i].acrylicReleaseFenceFd = -1;
        }
    }
}

ExynosMPP::~ExynosMPP()
{
    mResourceManageThread.mRunning = false;
    mResourceManageThread.requestExitAndWait();
}


ExynosMPP::ResourceManageThread::ResourceManageThread(ExynosMPP *exynosMPP)
: mExynosMPP(exynosMPP),
    mRunning(false)
{
}

ExynosMPP::ResourceManageThread::~ResourceManageThread()
{
}

bool ExynosMPP::isCSCSupportedByMPP(struct exynos_image &src, struct exynos_image &dst)
{
    /* YUV -> RGB or RGB -> YUV case */
    if (((isFormatRgb(src.format) == false) && (isFormatRgb(dst.format) == true)) ||
        ((isFormatRgb(src.format) == true) && (isFormatRgb(dst.format) == false))) {
        uint32_t standard_dataspace = (src.dataSpace & HAL_DATASPACE_STANDARD_MASK);
        switch (standard_dataspace) {
        case HAL_DATASPACE_STANDARD_UNSPECIFIED:
            return true;
        case HAL_DATASPACE_STANDARD_BT709:
        case HAL_DATASPACE_STANDARD_BT601_625:
        case HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED:
        case HAL_DATASPACE_STANDARD_BT601_525:
        case HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED:
        case HAL_DATASPACE_STANDARD_BT2020:
        case HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE:
        case HAL_DATASPACE_STANDARD_DCI_P3:
            if ((mPhysicalType == MPP_MSC) ||
                (mPhysicalType == MPP_DPP_VG) ||
                (mPhysicalType == MPP_DPP_VGF) ||
                (mPhysicalType == MPP_G2D))
                return true;
            else
                return false;
        default:
            return true;
        }
    }
    return true;
}


bool ExynosMPP::isSupportedHStrideCrop(struct exynos_image __unused &src)
{
    return true;
}

bool ExynosMPP::isSupportedBlend(struct exynos_image &src)
{
    switch(src.blending) {
    case HWC_BLENDING_NONE:
    case HWC_BLENDING_PREMULT:
    case HWC_BLENDING_COVERAGE:
        return true;
    default:
        return false;
    }
}

bool ExynosMPP::isSupportedTransform(struct exynos_image &src)
{
    switch (mPhysicalType)
    {
    case MPP_MSC:
    case MPP_G2D:
        return true;
    default:
        if (src.transform)
            return false;
        else
            return true;
    }
}
bool ExynosMPP::isSupportedCompression(struct exynos_image &src)
{
    if (src.compressed) {
        if ((mPhysicalType == MPP_DPP_VGF) || (mPhysicalType == MPP_G2D))
            return true;
        else
            return false;
    } else {
        return true;
    }
}

bool ExynosMPP::isSupportedDRM(struct exynos_image &src)
{
    if (getDrmMode(src.handleFlags) == NO_DRM)
        return true;

    if (mLogicalType == MPP_LOGICAL_G2D_RGB)
        return false;

    return true;
}

bool ExynosMPP::isDimLayerSupported()
{
    if ((mPhysicalType == MPP_DPP_G) ||
        (mPhysicalType == MPP_DPP_VG) ||
        (mPhysicalType == MPP_DPP_VGF))
        return true;

    return false;
}

bool ExynosMPP::isSrcFormatSupported(ExynosDisplay *display, struct exynos_image &src)
{
    if ((mLogicalType == MPP_LOGICAL_G2D_YUV) &&
        isFormatRgb(src.format))
        return false;
    if ((mLogicalType == MPP_LOGICAL_G2D_RGB) &&
        isFormatYUV(src.format))
        return false;
    if ((mLogicalType == MPP_LOGICAL_MSC_YUV) &&
        isFormatRgb(src.format)) {
        return false;
    }

    if ((display->mType == HWC_DISPLAY_EXTERNAL) &&
            (mPhysicalType != MPP_MSC) && (mPhysicalType != MPP_G2D) && isFormatYUV(src.format))
        return false;


    for (uint32_t i = 0 ; i < sizeof(restiction_format_table)/sizeof(restriction_key); i++) {
        if ((restiction_format_table[i].hwType == mPhysicalType) &&
            ((restiction_format_table[i].nodeType == NODE_NONE) ||
             (restiction_format_table[i].nodeType == NODE_SRC)) &&
            (restiction_format_table[i].format == src.format))
            return true;
    }
    return false;
}

bool ExynosMPP::isDstFormatSupported(struct exynos_image &dst)
{
    if (mPhysicalType == MPP_G2D) {
        uint32_t standard_dataspace = (dst.dataSpace & HAL_DATASPACE_STANDARD_MASK);
        if ((standard_dataspace == HAL_DATASPACE_STANDARD_BT2020) ||
            (standard_dataspace == HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE) ||
            (standard_dataspace == HAL_DATASPACE_STANDARD_DCI_P3))
            return false;
    }

    for (uint32_t i = 0 ; i < sizeof(restiction_format_table)/sizeof(restriction_key); i++) {
        if ((restiction_format_table[i].hwType == mPhysicalType) &&
            ((restiction_format_table[i].nodeType == NODE_NONE) ||
             (restiction_format_table[i].nodeType == NODE_DST)) &&
            (restiction_format_table[i].format == dst.format))
            return true;
    }
    return false;
}

uint32_t ExynosMPP::getMaxUpscale(struct exynos_image &src, struct exynos_image __unused &dst)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].maxUpScale;
}

uint32_t ExynosMPP::getMaxDownscale(struct exynos_image __unused &src, struct exynos_image &dst)
{
    uint32_t idx = getRestrictionClassification(src);

    bool isPerpendicular = !!(dst.transform & HAL_TRANSFORM_ROT_90);
    float scaleRatio_H = (float)src.w/(float)dst.w;
    float scaleRatio_V = (float)src.h/(float)dst.h;
    float dstW = (float)dst.w;
    float displayW = (float)ExynosMPP::mainDisplayWidth;
    float displayH = (float)ExynosMPP::mainDisplayHeight;
    if (isPerpendicular)
        dstW = (float)dst.h;
    float resolClock = displayW * displayH * VPP_RESOL_CLOCK_FACTOR;

    if (mPhysicalType == MPP_DPP_VGF) {
        if ((float)VPP_CLOCK < ((resolClock * scaleRatio_H * scaleRatio_V * VPP_DISP_FACTOR)/VPP_PIXEL_PER_CLOCK * (dstW/displayW)))
            return 1;
    }

    if ((mPhysicalType == MPP_G2D) &&
        (mPreAssignDisplayList & HWC_DISPLAY_VIRTUAL_BIT)) {
        return 8192;
    }

    return mDstSizeRestrictions[idx].maxDownScale;
}

uint32_t ExynosMPP::getSrcXOffsetAlign(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].cropXAlign;
}
uint32_t ExynosMPP::getSrcXOffsetAlign(uint32_t idx)
{
    if (idx >= RESTRICTION_MAX)
    {
        MPP_LOGE("invalid idx: %d", idx);
        return 16;
    }
    return mSrcSizeRestrictions[idx].cropXAlign;
}
uint32_t ExynosMPP::getSrcYOffsetAlign(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].cropYAlign;
}
uint32_t ExynosMPP::getSrcYOffsetAlign(uint32_t idx)
{
    if (idx >= RESTRICTION_MAX)
    {
        MPP_LOGE("invalid idx: %d", idx);
        return 16;
    }
    return mSrcSizeRestrictions[idx].cropYAlign;
}
uint32_t ExynosMPP::getSrcWidthAlign(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].fullWidthAlign;
}
uint32_t ExynosMPP::getSrcHeightAlign(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].fullHeightAlign;
}
uint32_t ExynosMPP::getSrcMaxWidth(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].maxFullWidth;
}
uint32_t ExynosMPP::getSrcMaxHeight(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].maxFullHeight;
}
uint32_t ExynosMPP::getSrcMinWidth(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].minFullWidth;
}
uint32_t ExynosMPP::getSrcMinHeight(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].minFullHeight;
}
uint32_t ExynosMPP::getSrcMaxCropWidth(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].maxCropWidth;
}
uint32_t ExynosMPP::getSrcMaxCropHeight(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].maxCropHeight;
}
uint32_t ExynosMPP::getSrcMinCropWidth(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].minCropWidth;
}
uint32_t ExynosMPP::getSrcMinCropHeight(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].minCropHeight;
}
uint32_t ExynosMPP::getSrcCropWidthAlign(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].cropWidthAlign;
}
uint32_t ExynosMPP::getSrcCropWidthAlign(uint32_t idx)
{
    if (idx >= RESTRICTION_MAX)
    {
        MPP_LOGE("invalid idx: %d", idx);
        return 16;
    }
    return mSrcSizeRestrictions[idx].cropWidthAlign;
}
uint32_t ExynosMPP::getSrcCropHeightAlign(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].cropHeightAlign;
}
uint32_t ExynosMPP::getSrcCropHeightAlign(uint32_t idx)
{
    if (idx >= RESTRICTION_MAX)
    {
        MPP_LOGE("invalid idx: %d", idx);
        return 16;
    }
    return mSrcSizeRestrictions[idx].cropHeightAlign;
}
uint32_t ExynosMPP::getDstMaxWidth(struct exynos_image &dst)
{
    uint32_t idx = getRestrictionClassification(dst);
    return mDstSizeRestrictions[idx].maxCropWidth;
}
uint32_t ExynosMPP::getDstMaxHeight(struct exynos_image &dst)
{
    uint32_t idx = getRestrictionClassification(dst);
    return mDstSizeRestrictions[idx].maxCropHeight;
}
uint32_t ExynosMPP::getDstMinWidth(struct exynos_image &dst)
{
    uint32_t idx = getRestrictionClassification(dst);
    return mDstSizeRestrictions[idx].minCropWidth;
}
uint32_t ExynosMPP::getDstMinHeight(struct exynos_image &dst)
{
    uint32_t idx = getRestrictionClassification(dst);
    return mDstSizeRestrictions[idx].minCropHeight;
}
uint32_t ExynosMPP::getDstWidthAlign(struct exynos_image &dst)
{
    uint32_t idx = getRestrictionClassification(dst);
    return mDstSizeRestrictions[idx].cropWidthAlign;
}
uint32_t ExynosMPP::getDstHeightAlign(struct exynos_image &dst)
{
    uint32_t idx = getRestrictionClassification(dst);
    return mDstSizeRestrictions[idx].cropHeightAlign;
}
uint32_t ExynosMPP::getOutBufAlign()
{
    if (mNeedCompressedTarget)
        return 16;
    else
        return 1;
}

bool ExynosMPP::ResourceManageThread::threadLoop()
{
    if (mExynosMPP == NULL)
        return false;

    ALOGI("%s threadLoop is started", mExynosMPP->mName.string());
    while(mRunning) {
        {
            Mutex::Autolock lock(mMutex);
            while((mFreedBuffers.size() == 0) &&
                  (mStateFences.size() == 0)) {
                    mCondition.wait(mMutex);
            }

            if ((mExynosMPP->mHWState == MPP_HW_STATE_RUNNING) &&
                (mStateFences.size() != 0)) {
                if (checkStateFences()) {
                    mExynosMPP->mHWState = MPP_HW_STATE_IDLE;
                }
            } else {
                if ((mStateFences.size() != 0) &&
                    (mExynosMPP->mHWState != MPP_HW_STATE_RUNNING)) {
                    ALOGW("%s, mHWState(%d) but mStateFences size(%zu)",
                            mExynosMPP->mName.string(), mExynosMPP->mHWState,
                            mStateFences.size());
                    checkStateFences();
                }
            }
            if (mFreedBuffers.size() != 0) {
                freeBuffers();
            }
        }
    }
    return true;
}

void ExynosMPP::ResourceManageThread::freeBuffers()
{
#ifndef GRALLOC_VERSION1
    alloc_device_t* alloc_device = mExynosMPP->mAllocDevice;
#endif
    android::List<exynos_mpp_img_info >::iterator it;
    android::List<exynos_mpp_img_info >::iterator end;
    it = mFreedBuffers.begin();
    end = mFreedBuffers.end();

    uint32_t freebufNum = 0;
    while (it != end) {
        exynos_mpp_img_info freeBuffer = (exynos_mpp_img_info)(*it);
        HDEBUGLOGD(eDebugMPP|eDebugFence|eDebugBuf, "freebufNum: %d, buffer: %p", freebufNum, freeBuffer.bufferHandle);
        dumpExynosMPPImgInfo(eDebugMPP|eDebugFence|eDebugBuf, freeBuffer);
        if (mExynosMPP->mPhysicalType == MPP_MSC) {
            if (fence_valid(freeBuffer.mppImg.acquireFenceFd) >= 0) {
                if (sync_wait(freeBuffer.mppImg.acquireFenceFd, 1000) < 0)
                    HWC_LOGE(NULL, "%s:: acquire fence sync_wait error", mExynosMPP->mName.string());
                freeBuffer.mppImg.acquireFenceFd = fence_close(freeBuffer.mppImg.acquireFenceFd,
                        mExynosMPP->mAssignedDisplay, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_MSC);
            }
            if (fence_valid(freeBuffer.mppImg.releaseFenceFd)) {
                if (sync_wait(freeBuffer.mppImg.releaseFenceFd, 1000) < 0)
                    HWC_LOGE(NULL, "%s:: release fence sync_wait error", mExynosMPP->mName.string());
                freeBuffer.mppImg.releaseFenceFd = fence_close(freeBuffer.mppImg.releaseFenceFd,
                        mExynosMPP->mAssignedDisplay, FENCE_TYPE_SRC_RELEASE, FENCE_IP_MSC);
            }
        } else {
            if (fence_valid(freeBuffer.acrylicAcquireFenceFd)) {
                if (sync_wait(freeBuffer.acrylicAcquireFenceFd, 1000) < 0)
                    HWC_LOGE(NULL, "%s:: acquire fence sync_wait error", mExynosMPP->mName.string());
                freeBuffer.acrylicAcquireFenceFd = fence_close(freeBuffer.acrylicAcquireFenceFd,
                        mExynosMPP->mAssignedDisplay, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_ALL);
            }
            if (fence_valid(freeBuffer.acrylicReleaseFenceFd)) {
                if (sync_wait(freeBuffer.acrylicReleaseFenceFd, 1000) < 0)
                    HWC_LOGE(NULL, "%s:: release fence sync_wait error", mExynosMPP->mName.string());
                freeBuffer.acrylicReleaseFenceFd = fence_close(freeBuffer.acrylicReleaseFenceFd,
                        mExynosMPP->mAssignedDisplay, FENCE_TYPE_SRC_RELEASE, FENCE_IP_ALL);
            }
        }
#ifdef GRALLOC_VERSION1
        mExynosMPP->mMapper->freeBuffer(freeBuffer.bufferHandle);
#else
        alloc_device->free(alloc_device, freeBuffer.bufferHandle);
#endif
        it = mFreedBuffers.erase(it);
    }
}

bool ExynosMPP::ResourceManageThread::checkStateFences()
{
    bool ret = true;
    android::List<int >::iterator it;
    android::List<int >::iterator end;

    it = mStateFences.begin();
    end = mStateFences.end();
    uint32_t waitFenceNum = 0;
    while (it != end) {
        int fence = (int)(*it);
        HDEBUGLOGD(eDebugMPP|eDebugFence, "%d wait fence: %d", waitFenceNum, fence);
        waitFenceNum++;
        if (fence_valid(fence) >= 0) {
            if (sync_wait(fence, 5000) < 0) {
                HWC_LOGE(NULL, "%s::[%s][%d] sync_wait(%d) error(%s)", __func__,
                        mExynosMPP->mName.string(), mExynosMPP->mLogicalIndex, fence, strerror(errno));
                ret = false;
            }
            fence = fence_close(fence, mExynosMPP->mAssignedDisplay, FENCE_TYPE_ALL, FENCE_IP_ALL);
        }
        it = mStateFences.erase(it);
    }
    return ret;
}

void ExynosMPP::ResourceManageThread::addFreedBuffer(exynos_mpp_img_info freedBuffer)
{
    android::Mutex::Autolock lock(mMutex);
    mFreedBuffers.push_back(freedBuffer);
    mCondition.signal();
}

void ExynosMPP::ResourceManageThread::addStateFence(int fence)
{
    HDEBUGLOGD(eDebugMPP|eDebugFence, "wait fence is added: %d", fence);
    Mutex::Autolock lock(mMutex);
    mStateFences.push_back(fence);
    mCondition.signal();
}

#ifdef GRALLOC_VERSION1
void ExynosMPP::setAllocDevice(GrallocWrapper::Allocator* allocator, GrallocWrapper::Mapper* mapper)
{
    mAllocator = allocator;
    mMapper = mapper;
}
#else
void ExynosMPP::setAllocDevice(alloc_device_t* allocDevice)
{
    mAllocDevice = allocDevice;
}
#endif

/**
 * @param w
 * @param h
 * @param color
 * @param usage
 * @return int32_t
 */
int32_t ExynosMPP::allocOutBuf(uint32_t w, uint32_t h, uint32_t format, uint64_t usage, uint32_t index) {

    MPP_LOGD(eDebugMPP|eDebugBuf, "index: %d++++++++", index);

    if (index >= NUM_MPP_DST_BUFS(mLogicalType)) {
        return -EINVAL;
    }

    exynos_mpp_img_info freeDstBuf = mDstImgs[index];
    MPP_LOGD(eDebugMPP|eDebugBuf, "mDstImg[%d] is reallocated", index);
    dumpExynosMPPImgInfo(eDebugMPP, mDstImgs[index]);

#ifdef GRALLOC_VERSION1
    uint32_t dstStride = 0;
    uint64_t allocUsage = getBufferUsage(usage);
    buffer_handle_t dstBuffer;

    MPP_LOGD(eDebugMPP|eDebugBuf, "\tw: %d, h: %d, format: 0x%8x, previousBuffer: %p, allocUsage: 0x8%" PRIx64 ", usage: 0x8%" PRIx64 "",
            w, h, format, freeDstBuf.bufferHandle, allocUsage, usage);

    if ((mAllocator == NULL) && (mMapper == NULL))
        ExynosDevice::getAllocator(&mMapper, &mAllocator);

    GrallocWrapper::IMapper::BufferDescriptorInfo info = {};
    info.width = w;
    info.height = h;
    info.layerCount = 1;
    info.format = static_cast<GrallocWrapper::PixelFormat>(format);
    info.usage = allocUsage;
    GrallocWrapper::Error error = GrallocWrapper::Error::NONE;

    {
        ATRACE_CALL();
        error = mAllocator->allocate(info, &dstStride, &dstBuffer);
    }

    if ((error != GrallocWrapper::Error::NONE) || (dstBuffer == NULL)) {
        MPP_LOGE("failed to allocate destination buffer(%dx%d): %d", w, h, error);
        return -EINVAL;
    }
#else
    int dstStride;
    int allocUsage = getBufferUsage(usage);
    buffer_handle_t dstBuffer = NULL;

    MPP_LOGD(eDebugMPP|eDebugBuf, "\tw: %d, h: %d, format: 0x%8x, previousBuffer: %p, allocUsage: 0x%8x, usage: 0x%8x",
            w, h, format, freeDstBuf.bufferHandle, allocUsage, usage);

    int ret = mAllocDevice->alloc(mAllocDevice, w, h, format, allocUsage,
            &dstBuffer, &dstStride);
    if ((ret < 0) || (dstBuffer == NULL)) {
        MPP_LOGE("failed to allocate destination buffer(%dx%d): %s", w, h,
                strerror(-ret));
        return ret;
    }
#endif

    memset(&mDstImgs[index], 0, sizeof(exynos_mpp_img_info));

    if (mPhysicalType == MPP_MSC) {
        mDstImgs[index].mppImg.acquireFenceFd = -1;
        mDstImgs[index].mppImg.releaseFenceFd = -1;
    } else {
        mDstImgs[index].acrylicAcquireFenceFd = -1;
        mDstImgs[index].acrylicReleaseFenceFd = -1;
    }

    mDstImgs[index].bufferHandle = private_handle_t::dynamicCast(dstBuffer);
    mDstImgs[index].bufferType = getBufferType((uint64_t)usage);
    mDstImgs[index].format = format;

    MPP_LOGD(eDebugMPP|eDebugBuf, "free outbuf[%d] %p", index, freeDstBuf.bufferHandle);
    if (freeDstBuf.bufferHandle != NULL)
        freeOutBuf(freeDstBuf);
    else {
        if (mPhysicalType == MPP_MSC) {
            freeDstBuf.mppImg.acquireFenceFd = fence_close(freeDstBuf.mppImg.acquireFenceFd,
                    mAssignedDisplay, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_MSC);
            freeDstBuf.mppImg.releaseFenceFd = fence_close(freeDstBuf.mppImg.releaseFenceFd,
                    mAssignedDisplay, FENCE_TYPE_SRC_RELEASE, FENCE_IP_MSC);
        } else {
            freeDstBuf.acrylicAcquireFenceFd = fence_close(freeDstBuf.acrylicAcquireFenceFd,
                    mAssignedDisplay, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_G2D);
            freeDstBuf.acrylicReleaseFenceFd = fence_close(freeDstBuf.acrylicReleaseFenceFd,
                    mAssignedDisplay, FENCE_TYPE_SRC_RELEASE, FENCE_IP_G2D);
        }
    }

    MPP_LOGD(eDebugMPP|eDebugBuf, "dstBuffer(%p)-----------", dstBuffer);

    return NO_ERROR;
}

/**
 * @param outbuf
 * @return int32_t
 */
int32_t ExynosMPP::setOutBuf(buffer_handle_t outbuf, int32_t fence) {
    mDstImgs[mCurrentDstBuf].bufferHandle = NULL;
    if (outbuf != NULL) {
        mDstImgs[mCurrentDstBuf].bufferHandle = private_handle_t::dynamicCast(outbuf);
        mDstImgs[mCurrentDstBuf].format = mDstImgs[mCurrentDstBuf].bufferHandle->format;
    }
    setDstAcquireFence(fence);
    return NO_ERROR;
}

/**
 * @param dst
 * @return int32_t
 */
int32_t ExynosMPP::freeOutBuf(struct exynos_mpp_img_info dst) {
    mResourceManageThread.addFreedBuffer(dst);
    dst.bufferHandle = NULL;
    return NO_ERROR;
}

uint32_t ExynosMPP::getBufferType(uint64_t usage)
{
    if (getDrmMode(usage) == SECURE_DRM)
        return MPP_BUFFER_SECURE_DRM;
    else if (getDrmMode(usage) == NORMAL_DRM)
        return MPP_BUFFER_NORMAL_DRM;
    else
        return MPP_BUFFER_NORMAL;
}

uint32_t ExynosMPP::getBufferType(const private_handle_t *handle)
{
#ifdef GRALLOC_VERSION1
    uint64_t usage = handle->producer_usage;
#else
    uint64_t usage = (uint64_t)(handle->flags);
#endif
    return getBufferType(usage);
}

uint64_t ExynosMPP::getBufferUsage(uint64_t usage)
{
    uint64_t allocUsage = 0;

#ifdef GRALLOC_VERSION1
    if (getBufferType(usage) == MPP_BUFFER_DUMP) {
        allocUsage = GRALLOC1_PRODUCER_USAGE_CPU_READ_OFTEN |
            GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN;
    } else {
        allocUsage = GRALLOC1_CONSUMER_USAGE_CPU_READ_NEVER |
            GRALLOC1_PRODUCER_USAGE_CPU_WRITE_NEVER |
            GRALLOC1_PRODUCER_USAGE_NOZEROED |
            GRALLOC1_CONSUMER_USAGE_HWCOMPOSER;
    }

    if (getDrmMode(usage) == SECURE_DRM) {
        allocUsage |= GRALLOC1_PRODUCER_USAGE_PROTECTED;
        allocUsage &= ~GRALLOC1_PRODUCER_USAGE_PRIVATE_NONSECURE;
    } else if (getDrmMode(usage) == NORMAL_DRM) {
        allocUsage |= GRALLOC1_PRODUCER_USAGE_PROTECTED;
        allocUsage |= GRALLOC1_PRODUCER_USAGE_PRIVATE_NONSECURE;
    }

    /* HACK: for distinguishing FIMD_VIDEO_region */
    if (!((allocUsage & GRALLOC1_PRODUCER_USAGE_PROTECTED) &&
          !(allocUsage & GRALLOC1_PRODUCER_USAGE_PRIVATE_NONSECURE) &&
          !(allocUsage & GRALLOC1_CONSUMER_USAGE_VIDEO_EXT))) {
        allocUsage |= (GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE | GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET);
    }
#else
    allocUsage = GRALLOC_USAGE_SW_READ_NEVER |
            GRALLOC_USAGE_SW_WRITE_NEVER |
            GRALLOC_USAGE_NOZEROED |
            GRALLOC_USAGE_HW_COMPOSER;

    if (getDrmMode(usage) == SECURE_DRM) {
        allocUsage |= GRALLOC_USAGE_PROTECTED;
        allocUsage &= ~GRALLOC_USAGE_PRIVATE_NONSECURE;
    } else if (getDrmMode(usage) == NORMAL_DRM) {
        allocUsage |= GRALLOC_USAGE_PROTECTED;
        allocUsage |= GRALLOC_USAGE_PRIVATE_NONSECURE;
    }

    /* HACK: for distinguishing FIMD_VIDEO_region */
    if (!((allocUsage & GRALLOC_USAGE_PROTECTED) &&
          !(allocUsage & GRALLOC_USAGE_PRIVATE_NONSECURE) &&
          !(allocUsage & GRALLOC_USAGE_VIDEO_EXT))) {
        allocUsage |= (GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_RENDER);
    }
#endif

    return allocUsage;
}
bool ExynosMPP::needReConfig(struct exynos_image &src, struct exynos_image &dst)
{
    bool ret = false;
    if (mPhysicalType == MPP_MSC) {
        ret = (dst.x != mDstImgs[mCurrentDstBuf].mppImg.x) ||
            (dst.y != mDstImgs[mCurrentDstBuf].mppImg.y) ||
            (dst.w != mDstImgs[mCurrentDstBuf].mppImg.w) ||
            (dst.h != mDstImgs[mCurrentDstBuf].mppImg.h) ||
            (dst.format != mDstImgs[mCurrentDstBuf].mppImg.format) ||
            (dst.transform != mDstImgs[mCurrentDstBuf].mppImg.rot) ||
            (isNarrowRgb(dst.format, dst.dataSpace) != mDstImgs[mCurrentDstBuf].mppImg.narrowRgb) ||
            // (c1.cacheable != c2.cacheable )
            ((dst.blending != HWC_BLENDING_COVERAGE) != mDstImgs[mCurrentDstBuf].mppImg.pre_multi) ||
            (!!(getDrmMode(dst.handleFlags) == SECURE_DRM) != mDstImgs[mCurrentDstBuf].mppImg.drmMode) ||
            (src.fullWidth != mSrcImgs[0].mppImg.fw) ||
            (src.fullHeight != mSrcImgs[0].mppImg.fh) ||
            (src.x != mSrcImgs[0].mppImg.x) ||
            (src.y != mSrcImgs[0].mppImg.y) ||
            (src.w != mSrcImgs[0].mppImg.w) ||
            (src.h != mSrcImgs[0].mppImg.h) ||
            (src.format != mSrcImgs[0].mppImg.format) ||
            (src.transform != mSrcImgs[0].mppImg.rot) ||
            (isNarrowRgb(src.format, src.dataSpace) != mSrcImgs[0].mppImg.narrowRgb) ||
            // (c1.cacheable != c2.cacheable )
            ((src.blending != HWC_BLENDING_COVERAGE) != mSrcImgs[0].mppImg.pre_multi) ||
            (!!(getDrmMode(src.handleFlags) == SECURE_DRM) != mSrcImgs[0].mppImg.drmMode) ||
            (dst.w != mPrevFrameInfo.dstInfo[0].w) ||
            (dst.h != mPrevFrameInfo.dstInfo[0].h) ||
            (dst.format != mPrevFrameInfo.dstInfo[0].format) ||
            (dst.transform != mPrevFrameInfo.dstInfo[0].transform) ||
            (isNarrowRgb(dst.format, dst.dataSpace) != isNarrowRgb(mPrevFrameInfo.dstInfo[0].format, mPrevFrameInfo.dstInfo[0].dataSpace)) ||
            ((dst.blending != HWC_BLENDING_COVERAGE) != (mPrevFrameInfo.dstInfo[0].blending != HWC_BLENDING_COVERAGE));
    }

    return ret;
}

bool ExynosMPP::needReCreate(struct exynos_image __unused &src, struct exynos_image __unused &dst)
{
    bool ret = false;
    if (mPhysicalType == MPP_MSC)
    {
        ret |= (mMPPHandle == NULL)/* |
               ((!!(getDrmMode(src.bufferHandle) == SECURE_DRM)) != mSrcImgs[0].mppImg.drmMode)*/;
    }
    return ret;
}

bool ExynosMPP::needDstBufRealloc(struct exynos_image &dst, uint32_t index)
{
    MPP_LOGD(eDebugMPP|eDebugBuf, "index: %d++++++++", index);

    if (index >= NUM_MPP_DST_BUFS(mLogicalType)) {
        MPP_LOGE("%s:: index(%d) is not valid", __func__, index);
        return false;
    }
    private_handle_t *dst_handle = NULL;
    if (mDstImgs[index].bufferHandle != NULL)
        dst_handle = private_handle_t::dynamicCast(mDstImgs[index].bufferHandle);

    if (dst_handle == NULL) {
        MPP_LOGD(eDebugMPP|eDebugBuf, "\tDstImag[%d]  handle is NULL", index);
        return true;
    }

    int32_t assignedDisplay = -1;
    if (mAssignedDisplay != NULL) {
        assignedDisplay = mAssignedDisplay->mType;
    } else {
        MPP_LOGE("%s:: mpp is not assigned", __func__);
        return false;
    }

    MPP_LOGD(eDebugMPP|eDebugBuf, "\tdst_handle(%p)", dst_handle);
    MPP_LOGD(eDebugMPP|eDebugBuf, "\tAssignedDisplay[%d, %d] format[0x%8x, 0x%8x], bufferType[%d, %d], handleFlags: 0x8%" PRIx64 "",
            mPrevAssignedDisplayType, assignedDisplay, dst_handle->format, dst.format,
            mDstImgs[index].bufferType, getBufferType((uint64_t)(dst.handleFlags)), dst.handleFlags);

    bool realloc = (mPrevAssignedDisplayType != assignedDisplay) ||
        (formatToBpp(dst_handle->format) < formatToBpp(dst.format)) ||
        (dst_handle->stride != (int)ALIGN_UP(mAssignedDisplay->mXres, getOutBufAlign())) ||
        (dst_handle->vstride != (int)ALIGN_UP(mAssignedDisplay->mYres, getOutBufAlign())) ||
        ((dst_handle->stride * dst_handle->vstride) < (int)(dst.fullWidth * dst.fullHeight)) ||
        (mDstImgs[index].bufferType != getBufferType((uint64_t)(dst.handleFlags)));

    MPP_LOGD(eDebugMPP|eDebugBuf, "realloc: %d--------", realloc);
    return realloc;
}

bool ExynosMPP::canUsePrevFrame()
{
    if (exynosHWCControl.skipM2mProcessing == false)
        return false;

    /* virtual display always require composition */
    if (mAllocOutBufFlag == false)
        return false;

    if (mPrevFrameInfo.srcNum != mAssignedSources.size())
        return false;

    for (uint32_t i = 0; i < mPrevFrameInfo.srcNum; i++) {
        if ((mPrevFrameInfo.srcInfo[i].bufferHandle != mAssignedSources[i]->mSrcImg.bufferHandle) ||
            (mPrevFrameInfo.srcInfo[i].x !=  mAssignedSources[i]->mSrcImg.x) ||
            (mPrevFrameInfo.srcInfo[i].y !=  mAssignedSources[i]->mSrcImg.y) ||
            (mPrevFrameInfo.srcInfo[i].w !=  mAssignedSources[i]->mSrcImg.w) ||
            (mPrevFrameInfo.srcInfo[i].h !=  mAssignedSources[i]->mSrcImg.h) ||
            (mPrevFrameInfo.srcInfo[i].format !=  mAssignedSources[i]->mSrcImg.format) ||
            (mPrevFrameInfo.srcInfo[i].handleFlags !=  mAssignedSources[i]->mSrcImg.handleFlags) ||
            (mPrevFrameInfo.srcInfo[i].dataSpace !=  mAssignedSources[i]->mSrcImg.dataSpace) ||
            (mPrevFrameInfo.srcInfo[i].blending !=  mAssignedSources[i]->mSrcImg.blending) ||
            (mPrevFrameInfo.srcInfo[i].transform !=  mAssignedSources[i]->mSrcImg.transform) ||
            (mPrevFrameInfo.srcInfo[i].compressed !=  mAssignedSources[i]->mSrcImg.compressed) ||
            (mPrevFrameInfo.srcInfo[i].planeAlpha !=  mAssignedSources[i]->mSrcImg.planeAlpha) ||
            (mPrevFrameInfo.dstInfo[i].x != mAssignedSources[i]->mMidImg.x) ||
            (mPrevFrameInfo.dstInfo[i].y != mAssignedSources[i]->mMidImg.y) ||
            (mPrevFrameInfo.dstInfo[i].w != mAssignedSources[i]->mMidImg.w) ||
            (mPrevFrameInfo.dstInfo[i].h != mAssignedSources[i]->mMidImg.h) ||
            (mPrevFrameInfo.dstInfo[i].format != mAssignedSources[i]->mMidImg.format))
            return false;
    }

    return true;
}

int32_t ExynosMPP::setupScalerSrc(exynos_mpp_img_info *srcImgInfo, struct exynos_image &src)
{
    if (mPhysicalType != MPP_MSC) {
        MPP_LOGE("%s:: MPP is not MSC", __func__);
        return -EINVAL;
    }
    exynos_mpp_img &srcImg = srcImgInfo->mppImg;
    private_handle_t *srcHandle = NULL;
    if (src.bufferHandle != NULL)
        srcHandle = private_handle_t::dynamicCast(src.bufferHandle);

    if (srcHandle == NULL) {
        MPP_LOGE("%s:: source handle is NULL", __func__);
        return -EINVAL;
    }

    srcImgInfo->bufferHandle = srcHandle;

    srcImg.fw = srcHandle->stride;
    srcImg.fh = srcHandle->vstride;

    if (srcHandle->format == HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_PRIV) {
         srcImg.fw = src.fullWidth;
         srcImg.fh = src.fullHeight;
    }

    if (srcImg.fw > (unsigned int)getSrcMaxWidth(src))
        srcImg.fw = (unsigned int)getSrcMaxWidth(src);
    if (srcImg.fh > (unsigned int)getSrcMaxHeight(src))
        srcImg.fh = (unsigned int)getSrcMaxHeight(src);
    srcImg.fw = pixel_align((unsigned int)srcImg.fw, getSrcWidthAlign(src));
    srcImg.fh = pixel_align((unsigned int)srcImg.fh, getSrcHeightAlign(src));

    srcImg.x = pixel_align(src.x, getSrcXOffsetAlign(src));
    srcImg.y = pixel_align(src.y, getSrcYOffsetAlign(src));
    srcImg.w = src.w - (srcImg.x - src.x);
    srcImg.h = src.h - (srcImg.y - src.y);
    if (srcImg.x + srcImg.w > srcImg.fw)
        srcImg.w = srcImg.fw - srcImg.x;
    if (srcImg.y + srcImg.h > srcImg.fh)
        srcImg.h = srcImg.fh - srcImg.y;
    srcImg.w = pixel_align_down(srcImg.w, getSrcCropWidthAlign(src));
    srcImg.h = pixel_align_down(srcImg.h, getSrcCropHeightAlign(src));

    srcImg.yaddr = srcHandle->fd;
    if (isFormatYCrCb(src.format)) {
        srcImg.uaddr = srcHandle->fd2;
        srcImg.vaddr = srcHandle->fd1;
    } else {
        srcImg.uaddr = srcHandle->fd1;
        srcImg.vaddr = srcHandle->fd2;
    }
    if (srcHandle->format != HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL)
        srcImg.format = srcHandle->format;
    else
        srcImg.format = HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M;

    if (src.blending == HWC_BLENDING_COVERAGE)
        srcImg.pre_multi = false;
    else
        srcImg.pre_multi = true;
    srcImg.drmMode = !!(getDrmMode(src.handleFlags) == SECURE_DRM);
    srcImg.acquireFenceFd = hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_MSC, src.acquireFenceFd);
    srcImg.mem_type = MPP_MEM_DMABUF;
    srcImgInfo->format = srcImg.format;

    return NO_ERROR;
}

int32_t ExynosMPP::setupScalerDst(exynos_mpp_img_info *dstImgInfo, struct exynos_image &dst)
{
    if (mPhysicalType != MPP_MSC) {
        MPP_LOGE("%s:: MPP is not MSC", __func__);
        return -EINVAL;
    }

    exynos_mpp_img &dstImg = dstImgInfo->mppImg;
    private_handle_t *dstHandle = mDstImgs[mCurrentDstBuf].bufferHandle;
    if (dstHandle == NULL) {
        MPP_LOGE("%s:: dst handle is NULL", __func__);
        return -EINVAL;
    }

    dstImg.fw = dstHandle->stride;
    dstImg.fh = dstHandle->vstride;
    dstImg.yaddr = dstHandle->fd;
    dstImg.uaddr = dstHandle->fd1;
    dstImg.vaddr = dstHandle->fd2;

    /*
     * dstImg.acquireFenceFd wasn't set in this function.
     * dstImg.acquireFenceFd should be releaseFence of DECON
     * It is set by ExynosDisplay using setDstAcquireFence()
     */
    dstImg.drmMode = !!(getDrmMode(dst.handleFlags) == SECURE_DRM);

    dstImg.x = 0;
    dstImg.y = 0;
    dstImg.w = dst.w;
    dstImg.h = dst.h;
    dstImg.rot = dst.transform;
    if (dst.blending == HWC_BLENDING_COVERAGE)
        dstImg.pre_multi = false;
    else
        dstImg.pre_multi = true;
    dstImg.drmMode = !!(getDrmMode(dst.handleFlags) == SECURE_DRM);
    dstImg.format = dst.format;
    if (dst.format == HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_PRIV)
        dstImg.format = HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M;
    dstImg.mem_type = MPP_MEM_DMABUF;
    dstImg.narrowRgb = isNarrowRgb(dst.format, dst.dataSpace);
    dstImgInfo->format = dstImg.format;

    return NO_ERROR;
}

int32_t ExynosMPP::doScalerPostProcessing(struct exynos_image &src, struct exynos_image &dst)
{
    int ret = 0;

    /* Set HW state to running */
    Mutex::Autolock lock(mResourceManageThread.mMutex);

    MPP_LOGD(eDebugMPP, "source configuration:");
    dumpMPPImage(eDebugMPP, mSrcImgs[0].mppImg);

    bool reconfig = needReConfig(src, dst);
    bool recreate = needReCreate(src, dst);
    MPP_LOGD(eDebugMPP, "mCurrentDstBuf: %d, reconfig: %d, recreate: %d +++++++++++++++++",
            mCurrentDstBuf, reconfig, recreate);
    MPP_LOGD(eDebugMPP, "src image");
    dumpExynosImage(eDebugMPP, src);
    MPP_LOGD(eDebugMPP, "dst image");
    dumpExynosImage(eDebugMPP, dst);

    //  set exynos_mpp_img srcImg, dstImg
    if ((ret = setupScalerSrc(&mSrcImgs[0], src)) != NO_ERROR)
        return ret;

    if ((ret = setupScalerDst(&mDstImgs[mCurrentDstBuf], dst)) != NO_ERROR)
        return ret;

    /* Rotation, range info is set on src */
    mDstImgs[mCurrentDstBuf].mppImg.rot = src.transform;
    if (isFormatRgb(src.format) == false) {
        mDstImgs[mCurrentDstBuf].mppImg.narrowRgb = isNarrowRgb(src.format, src.dataSpace);
    }

    MPP_LOGD(eDebugMPP, "destination configuration:");
    dumpMPPImage(eDebugMPP, mDstImgs[mCurrentDstBuf].mppImg);

    unsigned int colorspace = halDataSpaceToV4L2ColorSpace(src.dataSpace);

    if (recreate) {
        mMPPHandle = createMPPLib();
        if (!mMPPHandle) {
            MPP_LOGE("failed to create gscaler handle");
            return -1;
        }
    }

    if (reconfig) {
        ret = stopMPP(mMPPHandle);
        if (ret < 0) {
            MPP_LOGE("failed to stop");
            goto err;
        }

        MPP_LOGD(eDebugMPP, "setCSCProperty: dataspace: 0x%8x, fullrange: %d, colorspace: %d",
                src.dataSpace, !(mDstImgs[mCurrentDstBuf].mppImg.narrowRgb), colorspace);
        if ((ret = setCSCProperty(mMPPHandle, 0,
                        !mDstImgs[mCurrentDstBuf].mppImg.narrowRgb, colorspace)) < 0) {
            MPP_LOGE("failed to CSC mPhysicalType(%u) mPhysicalIndex(%u) mLogicalIndex(%u) "
                    "narrowRgb(%d) colorspace(%d)",
                    mPhysicalType, mPhysicalIndex, mLogicalIndex,
                    !mDstImgs[mCurrentDstBuf].mppImg.narrowRgb, colorspace);
            goto err;
        }
        if ((ret = configMPP(mMPPHandle, &mSrcImgs[0].mppImg,
                        &mDstImgs[mCurrentDstBuf].mppImg)) < 0) {
            MPP_LOGE("failed to configure mPhysicalType(%u) mPhysicalIndex(%u) mLogicalIndex(%u)",
                    mPhysicalType, mPhysicalIndex, mLogicalIndex);
            goto err;
        }
    }

    MPP_LOGD(eDebugFence, "Fence info before run src[%d, %d], dst[%d][%d, %d]",
            mSrcImgs[0].mppImg.acquireFenceFd, mSrcImgs[0].mppImg.releaseFenceFd,
            mCurrentDstBuf,
            mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd, mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd);

    if (fence_valid(mSrcImgs[0].mppImg.acquireFenceFd))
        setFenceName(mSrcImgs[0].mppImg.acquireFenceFd, FENCE_MSC_SRC_LAYER);
    if (fence_valid(mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd))
        setFenceName(mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd, FENCE_MSC_DST_DPP);

    //  run MPP
    ret = runMPP(mMPPHandle, &mSrcImgs[0].mppImg, &mDstImgs[mCurrentDstBuf].mppImg);

    MPP_LOGD(eDebugFence, "Fence info after run src[%d, %d], dst[%d][%d, %d]",
            mSrcImgs[0].mppImg.acquireFenceFd, mSrcImgs[0].mppImg.releaseFenceFd,
            mCurrentDstBuf,
            mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd, mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd);

    if (ret < 0) {
        MPP_LOGE("runMPP error(%d)", ret);
        mSrcImgs[0].mppImg.acquireFenceFd = fence_close(mSrcImgs[0].mppImg.acquireFenceFd,
                mAssignedDisplay, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_ALL);
        mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd =
            fence_close(mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd,
                    mAssignedDisplay, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_ALL);
    } else {
#ifdef DISABLE_FENCE
        mSrcImgs[0].mppImg.releaseFenceFd = fence_close(mSrcImgs[0].mppImg.releaseFenceFd,
                mAssignedDisplay, FENCE_TYPE_SRC_RELEASE, FENCE_IP_ALL);
        mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd =
            fence_close(mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd,
                    mAssignedDisplay, FENCE_TYPE_DST_RELEASE, FENCE_IP_ALL);
#endif
        mSrcImgs[0].mppImg.releaseFenceFd =
            hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_SRC_RELEASE, FENCE_IP_MSC, mSrcImgs[0].mppImg.releaseFenceFd);
        mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd =
            hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_DST_RELEASE, FENCE_IP_MSC, mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd);

#ifndef DISABLE_FENCE
        /* This fence is used for manage HWState */
        int fence = hwc_dup(mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd, mAssignedDisplay, FENCE_TYPE_SRC_RELEASE, FENCE_IP_G2D);
        if (fence_valid(hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_HW_STATE, FENCE_IP_MSC, fence))) {
            //mResourceManageThread.addStateFence(fence);
            setFenceName(fence, FENCE_MSC_HW_STATE);
            setHWStateFence(fence);
        }
#endif
    }

    mSrcImgs[0].mppImg.acquireFenceFd = -1;
    mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd = -1;

    MPP_LOGD(eDebugMPP, "-----------------------");

    return ret;
err:
    destroyMPPLib(mMPPHandle);
    mMPPHandle = NULL;

    return ret;

}

int32_t ExynosMPP::setupLayer(exynos_mpp_img_info *srcImgInfo, struct exynos_image &src, struct exynos_image &dst)
{
    int ret = NO_ERROR;

    if (srcImgInfo->mppLayer == NULL) {
        if ((srcImgInfo->mppLayer = mCompositor->createLayer()) == NULL)
        {
            MPP_LOGE("%s:: Fail to create layer", __func__);
            return -EINVAL;
        }
    }

    if (src.bufferHandle == NULL) {
        MPP_LOGE("%s:: Invalid source handle", __func__);
        return -EINVAL;
    }

    private_handle_t *srcHandle = NULL;
    if (src.bufferHandle != NULL)
        srcHandle = private_handle_t::dynamicCast(src.bufferHandle);
    int bufFds[MAX_HW2D_PLANES];
    size_t bufLength[MAX_HW2D_PLANES];
    uint32_t attribute = 0;
    uint32_t bufferNum = getBufferNumOfFormat(srcHandle->format);
    float pixelByte = (float)formatToBpp(srcHandle->format)/8;
    android_dataspace_t dataspace = src.dataSpace;
    if (dataspace == HAL_DATASPACE_UNKNOWN)
    {
        if (isFormatRgb(srcHandle->format))
            dataspace = HAL_DATASPACE_SRGB;
        else
            dataspace = HAL_DATASPACE_BT601_625;
    }

    if (bufferNum == 0)
    {
        MPP_LOGE("%s:: Fail to get bufferNum(%d), format(0x%8x)", __func__, bufferNum, srcHandle->format);
        return -EINVAL;
    }
    bufFds[0] = srcHandle->fd;
    bufFds[1] = srcHandle->fd1;
    bufFds[2] = srcHandle->fd2;
    bufLength[0] = src.fullWidth * src.fullHeight * pixelByte;
    bufLength[1] = 0;
    bufLength[2] = 0;

    if (isFormatYUV420(srcHandle->format))
    {
        switch (bufferNum) {
        case 1:
            bufLength[0] += (bufLength[0]/2);
            break;
        case 2:
            bufLength[1] = (bufLength[0]/2);
            break;
        case 3:
            bufLength[1] = (bufLength[0]/4);
            bufLength[2] = bufLength[1];
            break;
        default:
            MPP_LOGE("%s:: invalid bufferNum(%d), format(0x%8x)", __func__, bufferNum, srcHandle->format);
            return -EINVAL;
        }
    }

    srcImgInfo->bufferType = getBufferType(srcHandle);

    if (srcImgInfo->bufferType == MPP_BUFFER_SECURE_DRM)
        attribute |= AcrylicCanvas::ATTR_PROTECTED;
    if (src.compressed)
        attribute |= AcrylicCanvas::ATTR_COMPRESSED;

    srcImgInfo->bufferHandle = srcHandle;
    srcImgInfo->acrylicAcquireFenceFd =
        hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_G2D, src.acquireFenceFd);

    MPP_LOGD(eDebugMPP|eDebugFence, "source configuration:");
    MPP_LOGD(eDebugMPP, "\tImageDimension[%d, %d], ImageType[0x%8x, 0x%8x]",
            src.fullWidth, src.fullHeight,
            srcHandle->format, dataspace);
    MPP_LOGD(eDebugMPP|eDebugFence, "\tImageBuffer handle: %p, fds[%d, %d, %d], bufLength[%zu, %zu, %zu], bufferNum: %d, acquireFence: %d, attribute: %d",
            srcHandle, bufFds[0], bufFds[1], bufFds[2], bufLength[0], bufLength[1], bufLength[2],
            bufferNum, srcImgInfo->acrylicAcquireFenceFd, attribute);
    MPP_LOGD(eDebugMPP, "\tsrc_rect[%d, %d, %d, %d], dst_rect[%d, %d, %d, %d], transform(0x%4x)",
            (int)src.x, (int)src.y, (int)(src.x + src.w), (int)(src.y + src.h),
            (int)dst.x, (int)dst.y, (int)(dst.x + dst.w), (int)(dst.y + dst.h), src.transform);

    srcImgInfo->mppLayer->setImageDimension(src.fullWidth, src.fullHeight);

    if ((srcHandle->format == HAL_PIXEL_FORMAT_RGBA_8888) &&
        (src.layerFlags & HWC_SET_OPAQUE)) {
        srcImgInfo->mppLayer->setImageType(HAL_PIXEL_FORMAT_RGBX_8888, dataspace);
    } else {
        srcImgInfo->mppLayer->setImageType(srcHandle->format, dataspace);
    }

    if (mPhysicalType == MPP_G2D) {
        setFenceName(srcImgInfo->acrylicAcquireFenceFd, FENCE_G2D_SRC_LAYER);
        setFenceInfo(srcImgInfo->acrylicAcquireFenceFd, mAssignedDisplay,
                FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_G2D, FENCE_TO);
    } else if (mPhysicalType == MPP_MSC) {
        setFenceName(mSrcImgs[mCurrentDstBuf].mppImg.acquireFenceFd, FENCE_MSC_SRC_LAYER);
        setFenceInfo(mSrcImgs[mCurrentDstBuf].mppImg.acquireFenceFd, mAssignedDisplay,
                FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_MSC, FENCE_TO);
    } else {
        MPP_LOGE("%s:: invalid mPhysicalType(%d)", __func__, mPhysicalType);
    }

    srcImgInfo->mppLayer->setImageBuffer(bufFds, bufLength, bufferNum,
            srcImgInfo->acrylicAcquireFenceFd, attribute);
    srcImgInfo->mppLayer->setCompositMode(src.blending, src.planeAlpha, src.zOrder);
    hwc_rect_t src_rect = {(int)src.x, (int)src.y, (int)(src.x + src.w), (int)(src.y + src.h)};
    hwc_rect_t dst_rect = {(int)dst.x, (int)dst.y, (int)(dst.x + dst.w), (int)(dst.y + dst.h)};

#ifdef USES_VIRTUAL_DISPLAY
    if ((mPreAssignDisplayList & HWC_DISPLAY_EXTERNAL_BIT) || (mPreAssignDisplayList & HWC_DISPLAY_VIRTUAL_BIT))
        srcImgInfo->mppLayer->setCompositArea(src_rect, dst_rect, src.transform, AcrylicLayer::ATTR_NORESAMPLING);
    else {
        if(isFormatYUV(src.format))
            srcImgInfo->mppLayer->setCompositArea(src_rect, dst_rect, src.transform, AcrylicLayer::ATTR_NORESAMPLING);
        else
            srcImgInfo->mppLayer->setCompositArea(src_rect, dst_rect, src.transform);
    }
#else
    if(isFormatYUV(src.format))
        srcImgInfo->mppLayer->setCompositArea(src_rect, dst_rect, src.transform, AcrylicLayer::ATTR_NORESAMPLING);
    else
        srcImgInfo->mppLayer->setCompositArea(src_rect, dst_rect, src.transform);
#endif
    srcImgInfo->acrylicAcquireFenceFd = -1;
    srcImgInfo->format = srcHandle->format;

    if ((srcImgInfo->format == HAL_PIXEL_FORMAT_RGBA_8888) &&
        (src.layerFlags & HWC_SET_OPAQUE)) {
        srcImgInfo->format = HAL_PIXEL_FORMAT_RGBX_8888;
    }

    return ret;
}

int32_t ExynosMPP::setupDst(exynos_mpp_img_info *dstImgInfo)
{
    int ret = NO_ERROR;
    if (mPhysicalType != MPP_G2D) {
        MPP_LOGE("%s:: MPP is not MSC", __func__);
        return -EINVAL;
    }
    bool isComposition = (mMaxSrcLayerNum > 1);
    private_handle_t *dstHandle = dstImgInfo->bufferHandle;

    int bufFds[MAX_HW2D_PLANES];
    size_t bufLength[MAX_HW2D_PLANES];
    uint32_t attribute = 0;
    uint32_t bufferNum = getBufferNumOfFormat(dstImgInfo->format);
    float pixelByte = (float)formatToBpp(dstImgInfo->format)/8;
    if (bufferNum == 0)
    {
        MPP_LOGE("%s:: Fail to get bufferNum(%d), format(0x%8x)", __func__, bufferNum, dstImgInfo->format);
        return -EINVAL;
    }

    android_dataspace_t dataspace = HAL_DATASPACE_UNKNOWN;
    if (isComposition) {
        if (isFormatRgb(dstImgInfo->format))
            dataspace = HAL_DATASPACE_SRGB;
        else
            dataspace = HAL_DATASPACE_BT601_625;
    } else {
        dataspace = mAssignedSources[0]->mMidImg.dataSpace;
    }

    if (dataspace == HAL_DATASPACE_UNKNOWN)
    {
        if (isFormatRgb(dstImgInfo->format))
            dataspace = HAL_DATASPACE_SRGB;
        else
            dataspace = HAL_DATASPACE_BT601_625;
    }

    bufFds[0] = dstHandle->fd;
    bufFds[1] = dstHandle->fd1;
    bufFds[2] = dstHandle->fd2;
    bufLength[0] = dstHandle->stride * dstHandle->vstride * pixelByte;
    bufLength[1] = 0;
    bufLength[2] = 0;

    if (isFormatYUV420(dstImgInfo->format))
    {
        switch (bufferNum) {
        case 1:
            bufLength[0] += (bufLength[0]/2);
            break;
        case 2:
            bufLength[1] = (bufLength[0]/2);
            break;
        case 3:
            bufLength[1] = (bufLength[0]/4);
            bufLength[2] = bufLength[1];
            break;
        default:
            MPP_LOGE("%s:: invalid bufferNum(%d), format(0x%8x)", __func__, bufferNum, dstImgInfo->format);
            return -EINVAL;
        }
    }

    dstImgInfo->bufferType = getBufferType(dstHandle);

    if (dstImgInfo->bufferType == MPP_BUFFER_SECURE_DRM)
        attribute |= AcrylicCanvas::ATTR_PROTECTED;

    mCompositor->setCanvasDimension(dstHandle->stride, dstHandle->vstride);
    /* setup dst */
    if (isComposition && mNeedCompressedTarget)
        attribute |= AcrylicCanvas::ATTR_COMPRESSED;

    if (mPhysicalType == MPP_G2D) {
        setFenceName(dstImgInfo->acrylicAcquireFenceFd, FENCE_G2D_DST_DPP);
        /* Might be closed next frame */
        setFenceInfo(dstImgInfo->acrylicAcquireFenceFd, mAssignedDisplay,
                FENCE_TYPE_DST_ACQUIRE, FENCE_IP_G2D, FENCE_TO);
    } else if (mPhysicalType == MPP_MSC) {
        setFenceName(mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd, FENCE_MSC_DST_DPP);
        /* Might be closed next frame */
        setFenceInfo(mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd, mAssignedDisplay,
                FENCE_TYPE_DST_ACQUIRE, FENCE_IP_MSC, FENCE_TO);
    } else {
        MPP_LOGE("%s:: invalid mPhysicalType(%d)", __func__, mPhysicalType);
    }


    dstImgInfo->acrylicAcquireFenceFd =
        hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_G2D, dstImgInfo->acrylicAcquireFenceFd);
    mCompositor->setCanvasImageType(dstImgInfo->format, dataspace);
    mCompositor->setCanvasBuffer(bufFds, bufLength, bufferNum,
            dstImgInfo->acrylicAcquireFenceFd, attribute);

    MPP_LOGD(eDebugMPP|eDebugFence, "destination configuration:");
    MPP_LOGD(eDebugMPP, "\tImageDimension[%d, %d], ImageType[0x%8x, %d]",
            dstHandle->stride, dstHandle->vstride,
            dstImgInfo->format, dataspace);
    MPP_LOGD(eDebugMPP|eDebugFence, "\tImageBuffer handle: %p, fds[%d, %d, %d], bufLength[%zu, %zu, %zu], bufferNum: %d, acquireFence: %d, attribute: %d",
            dstHandle, bufFds[0], bufFds[1], bufFds[2], bufLength[0], bufLength[1], bufLength[2],
            bufferNum, dstImgInfo->acrylicAcquireFenceFd, attribute);


    dstImgInfo->acrylicAcquireFenceFd = -1;

    return ret;
}

int32_t ExynosMPP::doPostProcessingInternal()
{
    int ret = NO_ERROR;
    size_t sourceNum = mAssignedSources.size();

    if (mCompositor == NULL) {
        MPP_LOGE("%s:: mCompositor is NULL", __func__);
        return -EINVAL;
    }

    /* setup source layers */
    for(size_t i = 0; i < sourceNum; i++) {
        MPP_LOGD(eDebugMPP|eDebugFence, "Setup [%zu] source: %p", i, mAssignedSources[i]);
        if ((ret = setupLayer(&mSrcImgs[i], mAssignedSources[i]->mSrcImg, mAssignedSources[i]->mMidImg)) != NO_ERROR) {
            MPP_LOGE("%s:: fail to setupLayer[%zu], ret %d",
                    __func__, i, ret);
            return ret;
        }
    }
    if (mPrevFrameInfo.srcNum > sourceNum) {
        MPP_LOGD(eDebugMPP, "prev sourceNum(%d), current sourceNum(%zu)",
                mPrevFrameInfo.srcNum, sourceNum);
        for (size_t i = sourceNum; i < mPrevFrameInfo.srcNum; i++)
        {
            MPP_LOGD(eDebugMPP, "Remove mSrcImgs[%zu], %p", i, mSrcImgs[i].mppLayer);
            if (mSrcImgs[i].mppLayer != NULL) {
                delete mSrcImgs[i].mppLayer;
                mSrcImgs[i].mppLayer = NULL;
            }
        }
    }

    if (mCompositor->layerCount() != mAssignedSources.size()) {
        MPP_LOGE("Different layer number, acrylic layers(%d), assigned size(%zu)",
                mCompositor->layerCount(), mAssignedSources.size());
        return -EINVAL;
    }

    MPP_LOGD(eDebugFence, "setupDst ++ mDstImgs[%d] acrylicAcquireFenceFd(%d)",
            mCurrentDstBuf, mDstImgs[mCurrentDstBuf].acrylicAcquireFenceFd);

    setupDst(&mDstImgs[mCurrentDstBuf]);

    MPP_LOGD(eDebugFence, "setupDst -- mDstImgs[%d] acrylicAcquireFenceFd(%d) closed",
            mCurrentDstBuf, mDstImgs[mCurrentDstBuf].acrylicAcquireFenceFd);

    for(size_t i = 0; i < sourceNum; i++) {
        setFenceName(mSrcImgs[i].acrylicAcquireFenceFd, FENCE_G2D_SRC_LAYER);
    }
    setFenceName(mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd, FENCE_G2D_DST_DPP);

    int usingFenceCnt = 1;
#ifndef DISABLE_FENCE
    if (mLogicalType == MPP_LOGICAL_G2D_COMBO)
        usingFenceCnt = sourceNum + 1; // Get and Use src + dst fence
    else
        usingFenceCnt = 1;
    int *releaseFences = new int[sourceNum + 1];
    if (mCompositor->execute(releaseFences, (sourceNum + 1)) == false) {
#else
    usingFenceCnt = 0;                 // Get and Use no fences
    if (mCompositor->execute(NULL, 0) == false) {
#endif
        MPP_LOGE("%s:: fail to excute compositor", __func__);
        for(size_t i = 0; i < sourceNum; i++) {
            mSrcImgs[i].acrylicReleaseFenceFd = -1;
            MPP_LOGE("src[%zu]: ImageDimension[%d, %d], src_rect[%d, %d, %d, %d], dst_rect[%d, %d, %d, %d], transform(0x%4x)",
                    i,
                    mAssignedSources[i]->mSrcImg.fullWidth, mAssignedSources[i]->mSrcImg.fullHeight,
                    mAssignedSources[i]->mSrcImg.x, mAssignedSources[i]->mSrcImg.y,
                    mAssignedSources[i]->mSrcImg.x + mAssignedSources[i]->mSrcImg.w,
                    mAssignedSources[i]->mSrcImg.y + mAssignedSources[i]->mSrcImg.h,
                    mAssignedSources[i]->mMidImg.x, mAssignedSources[i]->mMidImg.y,
                    mAssignedSources[i]->mMidImg.x + mAssignedSources[i]->mMidImg.w,
                    mAssignedSources[i]->mMidImg.y + mAssignedSources[i]->mMidImg.h,
                    mAssignedSources[i]->mSrcImg.transform);
        }
        mDstImgs[mCurrentDstBuf].acrylicReleaseFenceFd = -1;
        ret = -EPERM;
    } else {
#ifdef DISABLE_FENCE
        for(size_t i = 0; i < sourceNum; i++) {
            mSrcImgs[i].acrylicReleaseFenceFd = -1;
        }
        mDstImgs[mCurrentDstBuf].acrylicReleaseFenceFd = -1;
        mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd = -1;
#else
        // set fence informations from acryl
        if (mPhysicalType == MPP_G2D) {
            setFenceInfo(releaseFences[sourceNum], mAssignedDisplay,
                    FENCE_TYPE_DST_ACQUIRE, FENCE_IP_G2D, FENCE_FROM);
            if (usingFenceCnt > 1) {
                for(size_t i = 0; i < sourceNum; i++) {
                    // TODO DPU release fence is tranferred to m2mMPP's source layer fence
                    setFenceInfo(releaseFences[i], mAssignedDisplay,
                            FENCE_TYPE_SRC_RELEASE, FENCE_IP_G2D, FENCE_FROM);
                }
            }
        }

        for(size_t i = 0; i < sourceNum; i++) {
            mSrcImgs[i].acrylicReleaseFenceFd =
                hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_SRC_RELEASE, FENCE_IP_G2D, releaseFences[i]);
            MPP_LOGD(eDebugFence, "mSrcImgs[%zu] acrylicReleaseFenceFd: %d",
                    i, mSrcImgs[i].acrylicReleaseFenceFd);
        }
        mDstImgs[mCurrentDstBuf].acrylicReleaseFenceFd =
            hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_DST_RELEASE, FENCE_IP_G2D, releaseFences[sourceNum]);

        MPP_LOGD(eDebugFence, "mDstImgs[%d] acrylicReleaseFenceFd: %d , releaseFences[%zu]",
                mCurrentDstBuf, mDstImgs[mCurrentDstBuf].acrylicReleaseFenceFd, sourceNum);

        /* This fence is used for manage HWState */
        int fence = hwc_dup(mDstImgs[mCurrentDstBuf].acrylicReleaseFenceFd, mAssignedDisplay,
                FENCE_TYPE_SRC_RELEASE, FENCE_IP_G2D);
        fence = hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_HW_STATE, FENCE_IP_G2D, fence);
        if (fence_valid(fence)) {
            setFenceName(fence, FENCE_G2D_HW_STATE);
            setHWStateFence(fence);
        }

#endif
    }
#ifndef DISABLE_FENCE
    delete [] releaseFences;
#endif
    return ret;
}

bool ExynosMPP::canSkipProcessing()
{
    if ((mAssignedDisplay == NULL) || (mAssignedSources.size() == 0))
        return true;
    ExynosMPPSource *source = mAssignedSources[0];
    exynos_image dst = source->mMidImg;
    if ((mLogicalType == MPP_LOGICAL_G2D_RGB) ||
        (mLogicalType == MPP_LOGICAL_G2D_COMBO)) {
        dst = mAssignedDisplay->mExynosCompositionInfo.mDstImg;
    }
    return ((needDstBufRealloc(dst, mCurrentDstBuf) == false) & canUsePrevFrame());

}

/**
 * @param src
 * @param dst
 * @return int32_t releaseFenceFd of src buffer
 */
int32_t ExynosMPP::doPostProcessing(struct exynos_image &src, struct exynos_image &dst)
{
    MPP_LOGD(eDebugMPP, "total assigned sources (%zu)++++++++", mAssignedSources.size());

    int ret = NO_ERROR;
    bool realloc = false;
    if (mAssignedSources.size() == 0) {
        MPP_LOGE("Assigned source size(%zu) is not valid",
                mAssignedSources.size());
        return -EINVAL;
    }

    // Check whether destination buffer allocation is required
    if (mAllocOutBufFlag) {
        if ((realloc = needDstBufRealloc(dst, mCurrentDstBuf)) == true) {
            //  allocate mDstImgs[mCurrentDstBuf]
            uint32_t bufAlign = getOutBufAlign();
            bool isComposition = (mMaxSrcLayerNum > 1);
            if (isComposition)
                dst.format = DEFAULT_MPP_DST_FORMAT;

            uint32_t allocFormat = dst.format;
            if (mFreeOutBufFlag == false)
                allocFormat = DEFAULT_MPP_DST_FORMAT;

            ret = allocOutBuf(ALIGN_UP(mAssignedDisplay->mXres, bufAlign),
                    ALIGN_UP(mAssignedDisplay->mYres, bufAlign),
                    allocFormat, (uint64_t)dst.handleFlags, mCurrentDstBuf);
        }
        if (ret < 0) {
            MPP_LOGE("%s:: fail to allocate dst buffer[%d]", __func__, mCurrentDstBuf);
            return ret;
        }
        if (mDstImgs[mCurrentDstBuf].format != dst.format) {
            MPP_LOGD(eDebugMPP, "dst format is changed (%d -> %d)",
                    mDstImgs[mCurrentDstBuf].format, dst.format);
            mDstImgs[mCurrentDstBuf].format = dst.format;
        }
    }

    if ((realloc == false) && canUsePrevFrame()) {
        mCurrentDstBuf = (mCurrentDstBuf + NUM_MPP_DST_BUFS(mLogicalType) - 1)% NUM_MPP_DST_BUFS(mLogicalType);
        MPP_LOGD(eDebugMPP|eDebugFence, "Reuse previous frame, dstImg[%d]", mCurrentDstBuf);
        for (uint32_t i = 0; i < mAssignedSources.size(); i++) {
            mAssignedSources[i]->mSrcImg.acquireFenceFd =
                fence_close(mAssignedSources[i]->mSrcImg.acquireFenceFd,
                        mAssignedDisplay, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_G2D);
        }
        return ret;
    }

    if (mPhysicalType == MPP_MSC) {
        /* Scaler case */
        if ((ret = doScalerPostProcessing(src, dst)) < 0) {
            MPP_LOGE("%s:: fail to scaler processing, ret %d",
                    __func__, ret);
            return ret;
        }
        /*
         * mSrcImgs[]/mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd was set by libscaler
         * This should be get by ExynosDisplay for acquireFenceFd of layer and DECON
         */
    } else if (mPhysicalType == MPP_G2D) {
        /* G2D case */
        if ((ret = doPostProcessingInternal()) < 0) {
            MPP_LOGE("%s:: fail to post processing, ret %d",
                    __func__, ret);
            return ret;
        }
    } else {
        MPP_LOGE("%s:: invalid MPP type (%d)",
                __func__, mPhysicalType);
        return -EINVAL;
    }

    /* Save current frame information for next frame*/
    mPrevAssignedState = mAssignedState;
    mPrevAssignedDisplayType = mAssignedDisplay->mType;
    mPrevFrameInfo.srcNum = (uint32_t)mAssignedSources.size();
    for (uint32_t i = 0; i < mPrevFrameInfo.srcNum; i++) {
        mPrevFrameInfo.srcInfo[i] = mAssignedSources[i]->mSrcImg;
        mPrevFrameInfo.dstInfo[i] = mAssignedSources[i]->mMidImg;
    }

    MPP_LOGD(eDebugMPP, "mPrevAssignedState: %d, mPrevAssignedDisplayType: %d--------------",
            mAssignedState, mAssignedDisplay->mType);

    return ret;
}

/*
 * This function should be called after doPostProcessing()
 * because doPostProcessing() sets
 * mSrcImgs[].mppImg.releaseFenceFd
 */
int32_t ExynosMPP::getSrcReleaseFence(uint32_t srcIndex)
{
    if (srcIndex >= NUM_MPP_SRC_BUFS)
        return -EINVAL;

    if (mPhysicalType == MPP_MSC) {
        return mSrcImgs[srcIndex].mppImg.releaseFenceFd;
    } else {
        return mSrcImgs[srcIndex].acrylicReleaseFenceFd;
    }

    return -EINVAL;
}

int32_t ExynosMPP::resetSrcReleaseFence()
{
    MPP_LOGD(eDebugFence, "");
        for (uint32_t i = 0; i < mAssignedSources.size(); i++) {
            if (mPhysicalType == MPP_MSC) {
                mSrcImgs[i].mppImg.releaseFenceFd = -1;
            } else {
                mSrcImgs[i].acrylicReleaseFenceFd = -1;
            }
        }
    return NO_ERROR;
}

int32_t ExynosMPP::getDstImageInfo(exynos_image *img)
{
    if ((mCurrentDstBuf < 0) || (mCurrentDstBuf >= NUM_MPP_DST_BUFS(mLogicalType)) ||
        (mAssignedDisplay == NULL)) {
        MPP_LOGE("mCurrentDstBuf(%d), mAssignedDisplay(%p)", mCurrentDstBuf, mAssignedDisplay);
        return -EINVAL;
    }

    memset(img, 0, sizeof(exynos_image));
    if (mPhysicalType == MPP_MSC) {
        getScalerDstImageInfo(img);
    } else {
        if (mDstImgs[mCurrentDstBuf].bufferHandle == NULL) {
            img->acquireFenceFd = -1;
            img->releaseFenceFd = -1;
            return -EFAULT;
        } else {
            img->bufferHandle = mDstImgs[mCurrentDstBuf].bufferHandle;
            img->fullWidth = mDstImgs[mCurrentDstBuf].bufferHandle->stride;
            img->fullHeight = mDstImgs[mCurrentDstBuf].bufferHandle->vstride;
            if ((mLogicalType == MPP_LOGICAL_G2D_RGB) ||
                (mLogicalType == MPP_LOGICAL_G2D_COMBO)) {
                if (mAssignedSources.size() == 1) {
                    img->x = mAssignedSources[0]->mDstImg.x;
                    img->y = mAssignedSources[0]->mDstImg.y;
                    img->w = mAssignedSources[0]->mDstImg.w;
                    img->h = mAssignedSources[0]->mDstImg.h;
                } else {
                    img->x = 0;
                    img->y = 0;
                    img->w = mAssignedDisplay->mXres;
                    img->h = mAssignedDisplay->mXres;
                }
            } else {
                img->x = mAssignedSources[0]->mMidImg.x;
                img->y = mAssignedSources[0]->mMidImg.y;
                img->w = mAssignedSources[0]->mMidImg.w;
                img->h = mAssignedSources[0]->mMidImg.h;
            }

            img->format = mDstImgs[mCurrentDstBuf].format;
            MPP_LOGD(eDebugFence, "get dstBuf[%d] accquireFence(%d)", mCurrentDstBuf,
                    mDstImgs[mCurrentDstBuf].acrylicAcquireFenceFd);
            img->acquireFenceFd = mDstImgs[mCurrentDstBuf].acrylicAcquireFenceFd;
            img->releaseFenceFd = mDstImgs[mCurrentDstBuf].acrylicReleaseFenceFd;
        }
    }
    return NO_ERROR;
}

int32_t ExynosMPP::getScalerDstImageInfo(exynos_image *img)
{
    img->bufferHandle = mDstImgs[mCurrentDstBuf].bufferHandle;
    if (mPhysicalType == MPP_MSC) {
        img->fullWidth = mDstImgs[mCurrentDstBuf].mppImg.fw;
        img->fullHeight = mDstImgs[mCurrentDstBuf].mppImg.fh;
        img->x = mDstImgs[mCurrentDstBuf].mppImg.x;
        img->y = mDstImgs[mCurrentDstBuf].mppImg.y;
        img->w = mDstImgs[mCurrentDstBuf].mppImg.w;
        img->h = mDstImgs[mCurrentDstBuf].mppImg.h;
        img->format = mDstImgs[mCurrentDstBuf].mppImg.format;
        img->acquireFenceFd = mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd;
        img->releaseFenceFd = mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd;
    }

    return NO_ERROR;
}

/*
 * This function should be called after getDstReleaseFence()
 * by ExynosDisplay
 */
int32_t ExynosMPP::setDstAcquireFence(int acquireFence)
{
    if (mCurrentDstBuf < 0 || mCurrentDstBuf >= NUM_MPP_DST_BUFS(mLogicalType))
        return -EINVAL;

    if (acquireFence < 0)
        return -EINVAL;

    if (mPhysicalType == MPP_MSC) {
        if (mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd >= 0) {
            MPP_LOGD(eDebugFence,"mDstImgs[%d].mppImg.acquireFenceFd: %d is closed", mCurrentDstBuf,
                    mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd);
            fence_close(mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd, mAssignedDisplay,
                    FENCE_TYPE_DST_ACQUIRE, FENCE_IP_MSC);
        }
        mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd =
            hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_MSC, acquireFence);

        MPP_LOGD(eDebugFence,"->mDstImgs[%d].mppImg.acquireFenceFd: %d", mCurrentDstBuf,
                mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd);
    } else {
        if (mDstImgs[mCurrentDstBuf].acrylicAcquireFenceFd >= 0) {
            MPP_LOGD(eDebugFence,"mDstImgs[%d].acrylicAcquireFenceFd: %d is closed", mCurrentDstBuf,
                    mDstImgs[mCurrentDstBuf].acrylicAcquireFenceFd);
            fence_close(mDstImgs[mCurrentDstBuf].acrylicAcquireFenceFd, mAssignedDisplay,
                    FENCE_TYPE_DST_ACQUIRE, FENCE_IP_ALL);
        }
        mDstImgs[mCurrentDstBuf].acrylicAcquireFenceFd =
            hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_G2D, acquireFence);
        MPP_LOGD(eDebugFence,"->mDstImgs[%d].acrylicAcquireFenceFd: %d", mCurrentDstBuf,
                mDstImgs[mCurrentDstBuf].acrylicAcquireFenceFd);
    }

    return NO_ERROR;
}

int32_t ExynosMPP::resetDstReleaseFence()
{
    MPP_LOGD(eDebugFence, "");

    if (mCurrentDstBuf < 0 || mCurrentDstBuf >= NUM_MPP_DST_BUFS(mLogicalType))
        return -EINVAL;
    if (mPhysicalType == MPP_MSC) {
        mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd = -1;
    } else {
        mDstImgs[mCurrentDstBuf].acrylicReleaseFenceFd = -1;
    }

    return NO_ERROR;
}

int32_t ExynosMPP::requestHWStateChange(uint32_t state)
{
    MPP_LOGD(eDebugMPP|eDebugFence|eDebugBuf, "state: %d", state);
    /* Set HW state to running */
    if (mHWState == state) {
        if ((mPhysicalType == MPP_G2D) && (state == MPP_HW_STATE_IDLE) && (mHWBusyFlag == false)) {
            int ret = NO_ERROR;
            if ((ret = prioritize(-1)) != NO_ERROR)
                MPP_LOGE("Fail to set prioritize (%d)", ret);
        }
        return NO_ERROR;
    }

    if (state == MPP_HW_STATE_RUNNING)
        mHWState = MPP_HW_STATE_RUNNING;
    else if (state == MPP_HW_STATE_IDLE) {
        if (mLastStateFenceFd >= 0)
            mResourceManageThread.addStateFence(mLastStateFenceFd);
        else
            mHWState = MPP_HW_STATE_IDLE;
        mLastStateFenceFd = -1;

        if ((mPhysicalType == MPP_G2D) && (mHWBusyFlag == false)) {
            int ret = NO_ERROR;
            if ((ret = prioritize(-1)) != NO_ERROR)
                MPP_LOGE("Fail to set prioritize (%d)", ret);
        }

        /* Free all of output buffers */
        if (mMPPType == MPP_TYPE_M2M) {
            for(uint32_t i = 0; i < NUM_MPP_DST_BUFS(mLogicalType); i++) {
                exynos_mpp_img_info freeDstBuf = mDstImgs[i];
                memset(&mDstImgs[i], 0, sizeof(exynos_mpp_img_info));

                if (mPhysicalType == MPP_MSC) {
                    /* Restore fence info */
                    mDstImgs[i].mppImg.acquireFenceFd = freeDstBuf.mppImg.acquireFenceFd;
                    mDstImgs[i].mppImg.releaseFenceFd = freeDstBuf.mppImg.releaseFenceFd;
                } else {
                    mDstImgs[i].acrylicAcquireFenceFd = freeDstBuf.acrylicAcquireFenceFd;
                    mDstImgs[i].acrylicReleaseFenceFd = freeDstBuf.acrylicReleaseFenceFd;
                }

                if (mFreeOutBufFlag == true) {
                    MPP_LOGD(eDebugMPP|eDebugFence|eDebugBuf, "free outbuf[%d] %p",
                            i, freeDstBuf.bufferHandle);
                    if (freeDstBuf.bufferHandle != NULL && mAllocOutBufFlag) {
                        if (mPhysicalType == MPP_MSC) {
                            if (mDstImgs[i].mppImg.acquireFenceFd >= 0) {
                                freeDstBuf.mppImg.acquireFenceFd =
                                    hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_FREE_ACQUIRE, FENCE_IP_MSC, hwc_dup(mDstImgs[i].mppImg.acquireFenceFd, mAssignedDisplay, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_MSC));
                                setFenceName(freeDstBuf.mppImg.acquireFenceFd, FENCE_MPP_FREE_BUF_ACQUIRE);
                            }
                            if (mDstImgs[i].mppImg.releaseFenceFd >= 0) {
                                freeDstBuf.mppImg.releaseFenceFd =
                                    hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_FREE_RELEASE, FENCE_IP_MSC, hwc_dup(mDstImgs[i].mppImg.releaseFenceFd, mAssignedDisplay, FENCE_TYPE_DST_RELEASE, FENCE_IP_MSC));
                                setFenceName(freeDstBuf.mppImg.releaseFenceFd, FENCE_MPP_FREE_BUF_RELEASE);
                            }
                        } else {
                            if (mDstImgs[i].acrylicAcquireFenceFd >= 0) {
                                freeDstBuf.acrylicAcquireFenceFd =
                                    hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_FREE_ACQUIRE, FENCE_IP_G2D, hwc_dup(mDstImgs[i].acrylicAcquireFenceFd, mAssignedDisplay, FENCE_TYPE_FREE_ACQUIRE, FENCE_IP_G2D));
                                setFenceName(freeDstBuf.acrylicAcquireFenceFd, FENCE_MPP_FREE_BUF_ACQUIRE);
                            }
                            if (mDstImgs[i].acrylicReleaseFenceFd >= 0) {
                                freeDstBuf.acrylicReleaseFenceFd =
                                    hwcCheckFenceDebug(mAssignedDisplay, FENCE_TYPE_FREE_RELEASE, FENCE_IP_G2D, hwc_dup(mDstImgs[i].acrylicReleaseFenceFd, mAssignedDisplay, FENCE_TYPE_FREE_RELEASE, FENCE_IP_G2D));
                                setFenceName(freeDstBuf.acrylicReleaseFenceFd, FENCE_MPP_FREE_BUF_RELEASE);
                            }
                        }
                        freeOutBuf(freeDstBuf);
                    }
                } else {
                    mDstImgs[i].bufferHandle = freeDstBuf.bufferHandle;
                    mDstImgs[i].bufferType = freeDstBuf.bufferType;
                }
            }
        }
        if (mPhysicalType == MPP_G2D)
        {
            for (uint32_t i = 0; i < NUM_MPP_SRC_BUFS; i++)
            {
                if (mSrcImgs[i].mppLayer != NULL) {
                    delete mSrcImgs[i].mppLayer;
                    mSrcImgs[i].mppLayer = NULL;
                }
            }
        }
        memset(&mPrevFrameInfo, 0, sizeof(mPrevFrameInfo));
        for (int i = 0; i < NUM_MPP_SRC_BUFS; i++) {
            mPrevFrameInfo.srcInfo[i].acquireFenceFd = -1;
            mPrevFrameInfo.srcInfo[i].releaseFenceFd = -1;
            mPrevFrameInfo.dstInfo[i].acquireFenceFd = -1;
            mPrevFrameInfo.dstInfo[i].releaseFenceFd = -1;
        }
    }

    return NO_ERROR;
}

int32_t ExynosMPP::setHWStateFence(int32_t fence)
{
    MPP_LOGD(eDebugFence, "Update HWState fence, Close(%d), set(%d)",
            mLastStateFenceFd, fence);
    if (fence_valid(mLastStateFenceFd) >= 0)
        hwcFdClose(mLastStateFenceFd);
    mLastStateFenceFd = fence;

    return NO_ERROR;
}

/**
 * @param ..
 * @return int32_t
 */
int32_t ExynosMPP::setupRestriction() {

    MPP_LOGD(eDebugMPP, "mPhysicalType(%d)", mPhysicalType);
    for (uint32_t i = 0; i < sizeof(restriction_tables)/sizeof(restriction_table_element); i++) {
        const restriction_size_element *restriction_size_table = restriction_tables[i].table;
        MPP_LOGD(eDebugMPP, "[%d] restriction, type: %d, element_size(%d), table(%p)",
                i, restriction_tables[i].classfication_type, restriction_tables[i].table_element_size,
                restriction_tables[i].table);
        for (uint32_t j = 0; j < restriction_tables[i].table_element_size; j++) {
#if 0
            MPP_LOGD(eDebugMPP, "\t[%d] key[hwType: %d, nodeTyp: %d], [%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d]",
                    j, restriction_size_table[j].key.hwType, restriction_size_table[j].key.nodeType,
                    restriction_size_table[j].sizeRestriction.maxDownScale, restriction_size_table[j].sizeRestriction.maxUpScale,
                    restriction_size_table[j].sizeRestriction.maxFullWidth, restriction_size_table[j].sizeRestriction.maxFullHeight,
                    restriction_size_table[j].sizeRestriction.minFullWidth, restriction_size_table[j].sizeRestriction.minFullHeight,
                    restriction_size_table[j].sizeRestriction.fullWidthAlign, restriction_size_table[j].sizeRestriction.fullHeightAlign,
                    restriction_size_table[j].sizeRestriction.maxCropWidth, restriction_size_table[j].sizeRestriction.maxCropHeight,
                    restriction_size_table[j].sizeRestriction.minCropWidth, restriction_size_table[j].sizeRestriction.minCropHeight,
                    restriction_size_table[j].sizeRestriction.cropXAlign, restriction_size_table[j].sizeRestriction.cropYAlign,
                    restriction_size_table[j].sizeRestriction.cropWidthAlign, restriction_size_table[j].sizeRestriction.cropHeightAlign);
#endif
            if (restriction_size_table[j].key.hwType == mPhysicalType) {
                if ((restriction_size_table[j].key.nodeType == NODE_SRC) ||
                    (restriction_size_table[j].key.nodeType == NODE_NONE)) {
                    memcpy(&mSrcSizeRestrictions[i], &restriction_size_table[j].sizeRestriction,
                            sizeof(restriction_size));
                    MPP_LOGD(eDebugMPP, "\tSet mSrcSizeRestrictions[%d], "
                            "[%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d]",
                            i, mSrcSizeRestrictions[i].maxDownScale, mSrcSizeRestrictions[i].maxUpScale,
                            mSrcSizeRestrictions[i].maxFullWidth, mSrcSizeRestrictions[i].maxFullHeight,
                            mSrcSizeRestrictions[i].minFullWidth, mSrcSizeRestrictions[i].minFullHeight,
                            mSrcSizeRestrictions[i].fullWidthAlign, mSrcSizeRestrictions[i].fullHeightAlign,
                            mSrcSizeRestrictions[i].maxCropWidth, mSrcSizeRestrictions[i].maxCropHeight,
                            mSrcSizeRestrictions[i].minCropWidth, mSrcSizeRestrictions[i].minCropHeight,
                            mSrcSizeRestrictions[i].cropXAlign, mSrcSizeRestrictions[i].cropYAlign,
                            mSrcSizeRestrictions[i].cropWidthAlign, mSrcSizeRestrictions[i].cropHeightAlign);
                }
                if ((restriction_size_table[j].key.nodeType == NODE_DST) ||
                    (restriction_size_table[j].key.nodeType == NODE_NONE)) {
                    memcpy(&mDstSizeRestrictions[i], &restriction_size_table[j].sizeRestriction,
                            sizeof(restriction_size));
                    MPP_LOGD(eDebugMPP, "\tSet mDstSizeRestrictions[%d], "
                            "[%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d]",
                            i, mDstSizeRestrictions[i].maxDownScale, mDstSizeRestrictions[i].maxUpScale,
                            mDstSizeRestrictions[i].maxFullWidth, mDstSizeRestrictions[i].maxFullHeight,
                            mDstSizeRestrictions[i].minFullWidth, mDstSizeRestrictions[i].minFullHeight,
                            mDstSizeRestrictions[i].fullWidthAlign, mDstSizeRestrictions[i].fullHeightAlign,
                            mDstSizeRestrictions[i].maxCropWidth, mDstSizeRestrictions[i].maxCropHeight,
                            mDstSizeRestrictions[i].minCropWidth, mDstSizeRestrictions[i].minCropHeight,
                            mDstSizeRestrictions[i].cropXAlign, mDstSizeRestrictions[i].cropYAlign,
                            mDstSizeRestrictions[i].cropWidthAlign, mDstSizeRestrictions[i].cropHeightAlign);
                }
            }
        }
    }
    return NO_ERROR;
}

int32_t ExynosMPP::isSupported(ExynosDisplay *display, struct exynos_image &src, struct exynos_image &dst)
{

    if (!!(src.layerFlags & HWC_DIM_LAYER)) // Dim layer
    {
        if (isDimLayerSupported())
            return NO_ERROR;
        else
            return -eMPPUnsupportedDIMLayer;
    }

    uint32_t maxSrcWidth = getSrcMaxWidth(src);
    uint32_t maxSrcHeight = getSrcMaxHeight(src);
    uint32_t minSrcWidth = getSrcMinWidth(src);
    uint32_t minSrcHeight = getSrcMinHeight(src);
    uint32_t srcWidthAlign = getSrcWidthAlign(src);
    uint32_t srcHeightAlign = getSrcHeightAlign(src);

    uint32_t maxSrcCropWidth = getSrcMaxCropWidth(src);
    uint32_t maxSrcCropHeight = getSrcMaxCropHeight(src);
    uint32_t minSrcCropWidth = getSrcMinCropWidth(src);
    uint32_t minSrcCropHeight = getSrcMinCropHeight(src);
    uint32_t srcCropWidthAlign = getSrcCropWidthAlign(src);
    uint32_t srcCropHeightAlign = getSrcCropHeightAlign(src);
    uint32_t srcXOffsetAlign = getSrcXOffsetAlign(src);
    uint32_t srcYOffsetAlign = getSrcYOffsetAlign(src);

    uint32_t maxDstWidth = getDstMaxWidth(dst);
    uint32_t maxDstHeight = getDstMaxHeight(dst);
    uint32_t minDstWidth = getDstMinWidth(dst);
    uint32_t minDstHeight = getDstMinHeight(dst);
    uint32_t dstWidthAlign = getDstWidthAlign(dst);
    uint32_t dstHeightAlign = getDstHeightAlign(dst);

    uint32_t maxDownscale = getMaxDownscale(src, dst);
    uint32_t maxUpscale = getMaxUpscale(src, dst);

    exynos_image rot_dst = dst;
    bool isPerpendicular = !!(src.transform & HAL_TRANSFORM_ROT_90);
    if (isPerpendicular) {
        rot_dst.w = dst.h;
        rot_dst.h = dst.w;
    }

    if (!isSrcFormatSupported(display, src))
        return -eMPPUnsupportedFormat;
    else if (!isDstFormatSupported(dst))
        return -eMPPUnsupportedFormat;
    else if (!isCSCSupportedByMPP(src, dst))
        return -eMPPUnsupportedCSC;
    else if (!isSupportedBlend(src))
        return -eMPPUnsupportedBlending;
    else if (!isSupportedTransform(src))
        return -eMPPUnsupportedRotation;
    else if (src.fullWidth < minSrcWidth)
        return -eMPPExeedMinSrcWidth;
    else if (src.fullHeight < minSrcHeight)
        return -eMPPExeedMinSrcHeight;
    else if (src.w < minSrcCropWidth)
        return -eMPPExeedSrcWCropMin;
    else if (src.h < minSrcCropHeight)
        return -eMPPExeedSrcHCropMin;
    else if (rot_dst.w > maxDstWidth)
        return -eMPPExeedMaxDstWidth;
    else if (rot_dst.h > maxDstHeight)
        return -eMPPExeedMaxDstHeight;
    else if (rot_dst.w < minDstWidth)
        return -eMPPExeedMinDstWidth;
    else if (rot_dst.h < minDstHeight)
        return -eMPPExeedMinDstWidth;
    else if ((rot_dst.w % dstWidthAlign != 0) || (rot_dst.h % dstHeightAlign != 0))
        return -eMPPNotAlignedDstSize;
    else if (src.w > rot_dst.w * maxDownscale)
        return -eMPPExeedMaxDownScale;
    else if (rot_dst.w > src.w * maxUpscale)
        return -eMPPExeedMaxUpScale;
    else if (src.h > rot_dst.h * maxDownscale)
        return -eMPPExeedMaxDownScale;
    else if (rot_dst.h > src.h * maxUpscale)
        return -eMPPExeedMaxUpScale;
    else if (!isSupportedDRM(src))
        return -eMPPUnsupportedDRM;
    else if (!isSupportedHStrideCrop(src))
        return -eMPPStrideCrop;

    if (getDrmMode(src.handleFlags) == NO_DRM) {
        if (src.fullWidth > maxSrcWidth)
            return -eMPPExceedHStrideMaximum;
        else if (src.fullHeight > maxSrcHeight)
            return -eMPPExceedVStrideMaximum;
        else if (src.fullWidth % srcWidthAlign != 0)
            return -eMPPNotAlignedHStride;
        else if (src.fullHeight % srcHeightAlign != 0)
            return -eMPPNotAlignedVStride;
        else if (src.w > maxSrcCropWidth)
            return -eMPPExeedSrcWCropMax;
        else if (src.h > maxSrcCropHeight)
            return -eMPPExeedSrcHCropMax;
        else if ((src.w % srcCropWidthAlign != 0) || (src.h % srcCropHeightAlign != 0))
            return -eMPPNotAlignedCrop;
        else if ((src.x % srcXOffsetAlign != 0) || (src.y % srcYOffsetAlign != 0))
            return -eMPPNotAlignedOffset;
    }

    if (!isSupportedCompression(src))
        return -eMPPUnsupportedCompression;

    return NO_ERROR;
}

int32_t ExynosMPP::resetMPP()
{
    mAssignedState = MPP_ASSIGN_STATE_FREE;
    mAssignedDisplay = NULL;
    mAssignedSources.clear();
    mReservedDisplay = -1;
    mUsedCapacity = 0;
    mHWBusyFlag = false;

    return NO_ERROR;
}

int32_t ExynosMPP::resetAssignedState()
{
    for (int i = (int)mAssignedSources.size(); i-- > 0;) {
        ExynosMPPSource *mppSource = mAssignedSources[i];
        if (mppSource->mOtfMPP == this) {
            mppSource->mOtfMPP = NULL;
        }
        if (mppSource->mM2mMPP == this) {
            mppSource->mM2mMPP = NULL;
        }
        mAssignedSources.removeItemsAt(i);
    }

    /* Keep status if mAssignedState is MPP_ASSIGN_STATE_RESERVED */
    if ((mAssignedState & MPP_ASSIGN_STATE_ASSIGNED) &&
        (mAssignedSources.size() == 0)) {
        mAssignedState &= ~MPP_ASSIGN_STATE_ASSIGNED;
        mAssignedDisplay = NULL;
    }

    updateUsedCapacity();

    return NO_ERROR;
}

int32_t ExynosMPP::resetAssignedState(ExynosMPPSource *mppSource)
{
    for (int i = (int)mAssignedSources.size(); i-- > 0;) {
        ExynosMPPSource *source = mAssignedSources[i];
        if (source == mppSource) {
            if (mppSource->mM2mMPP == this) {
                mppSource->mM2mMPP = NULL;
            }
            mAssignedSources.removeItemsAt(i);
            break;
        }
    }

    /* Keep status if mAssignedState is MPP_ASSIGN_STATE_RESERVED */
    if ((mAssignedState & MPP_ASSIGN_STATE_ASSIGNED) &&
        (mAssignedSources.size() == 0)) {
        mAssignedState &= ~MPP_ASSIGN_STATE_ASSIGNED;
        mAssignedDisplay = NULL;
    }

    return NO_ERROR;
}

int32_t ExynosMPP::reserveMPP(int32_t displayType)
{
    mAssignedState |= MPP_ASSIGN_STATE_RESERVED;
    mReservedDisplay = displayType;

    return NO_ERROR;
}

int32_t ExynosMPP::assignMPP(ExynosDisplay *display, ExynosMPPSource* mppSource)
{
    mAssignedState |= MPP_ASSIGN_STATE_ASSIGNED;

    if (mMPPType == MPP_TYPE_OTF)
        mppSource->mOtfMPP = this;
    else if (mMPPType == MPP_TYPE_M2M)
        mppSource->mM2mMPP = this;
    else {
        MPP_LOGE("%s:: Invalid mppType(%d)", __func__, mMPPType);
        return -EINVAL;
    }

    mAssignedDisplay = display;
    mAssignedSources.add(mppSource);

    MPP_LOGD(eDebugCapacity|eDebugMPP, "\tassigned to source(%p) type(%d), mAssignedSources(%zu)",
            mppSource, mppSource->mSourceType,
            mAssignedSources.size());

    if (mMaxSrcLayerNum > 1) {
        std::sort(mAssignedSources.begin(), mAssignedSources.end(), exynosMPPSourceComp);
    }

    return NO_ERROR;
}

uint32_t ExynosMPP::getSrcMaxBlendingNum(struct exynos_image &src, struct exynos_image &dst)
{
    uint32_t maxSrcLayerNum = mMaxSrcLayerNum;

    if (mLogicalType == MPP_LOGICAL_G2D_RGB) {
        if (mAssignedSources.size() >= G2D_RESTRICTIVE_SRC_NUM) {
            if (src.zOrder <= mAssignedSources[2]->mSrcImg.zOrder) {
                if (isScaledDown(src, dst) ||
                    isScaledDown(mAssignedSources[0]->mSrcImg, mAssignedSources[0]->mMidImg) ||
                    isScaledDown(mAssignedSources[1]->mSrcImg, mAssignedSources[1]->mMidImg) ||
                    isScaledDown(mAssignedSources[2]->mSrcImg, mAssignedSources[2]->mMidImg)) {
                    maxSrcLayerNum = G2D_RESTRICTIVE_SRC_NUM;
                }
            } else {
                if (isScaledDown(mAssignedSources[1]->mSrcImg, mAssignedSources[1]->mMidImg) ||
                    isScaledDown(mAssignedSources[2]->mSrcImg, mAssignedSources[2]->mMidImg)) {
                    maxSrcLayerNum = G2D_RESTRICTIVE_SRC_NUM;
                }
            }
        }
    }

    return maxSrcLayerNum;
}

/* Based on multi-resolution support */
void ExynosMPP::setDstAllocSize(uint32_t width, uint32_t height)
{
    switch(width) {
    case 720:
        mDstAllocatedSize = ((height >= 1480) ? DST_SIZE_HD_PLUS : DST_SIZE_HD);
        break;
    case 1080:
        mDstAllocatedSize = ((height >= 2220) ? DST_SIZE_FHD_PLUS : DST_SIZE_FHD);
        break;
    case 1440:
        mDstAllocatedSize = ((height >= 2960) ? DST_SIZE_WQHD_PLUS : DST_SIZE_WQHD);
        break;
    default:
        mDstAllocatedSize = DST_SIZE_UNKNOWN;
        break;
    }
}

dst_alloc_buf_size_t ExynosMPP::getDstAllocSize()
{
    return mDstAllocatedSize;
}

bool ExynosMPP::needPreAllocation()
{
    bool ret = false;

    if (((mLogicalType == MPP_LOGICAL_G2D_RGB) ||
                (mLogicalType == MPP_LOGICAL_MSC) ||
                (mLogicalType == MPP_LOGICAL_G2D_YUV)) &&
            (mPreAssignDisplayList == HWC_DISPLAY_PRIMARY_BIT))
        ret = true;

    return ret;
}

uint32_t ExynosMPP::getAssignedSourceNum()
{
    return mAssignedSources.size();
}

bool ExynosMPP::isAssignableState(ExynosDisplay *display, struct exynos_image &src, struct exynos_image &dst)
{
    bool isAssignable = false;

    if (mAssignedState == MPP_ASSIGN_STATE_FREE) {
        if (mHWState == MPP_HW_STATE_IDLE)
            isAssignable = true;
        else {
            if ((mPrevAssignedDisplayType < 0) ||
                ((uint32_t)mPrevAssignedDisplayType == display->mType))
                isAssignable = true;
            else
                isAssignable = false;
        }
    }

    if ((mAssignedState & MPP_ASSIGN_STATE_ASSIGNED) && (mAssignedState & MPP_ASSIGN_STATE_RESERVED))
    {
        if (mReservedDisplay == (int32_t)display->mType) {
            if (mAssignedSources.size() < getSrcMaxBlendingNum(src, dst))
                isAssignable = true;
            else
                isAssignable = false;
        } else {
            isAssignable = false;
        }
    } else if ((mAssignedState & MPP_ASSIGN_STATE_ASSIGNED) && !(mAssignedState & MPP_ASSIGN_STATE_RESERVED)) {
        if (mAssignedSources.size() < getSrcMaxBlendingNum(src, dst))
            isAssignable = true;
        else
            isAssignable = false;
    } else if (mAssignedState & MPP_ASSIGN_STATE_RESERVED) {
        if (mReservedDisplay == (int32_t)display->mType)
            isAssignable = true;
        else
            isAssignable = false;
    }

    MPP_LOGD(eDebugMPP, "\tisAssignableState(%d), assigned size(%zu), getSrcMaxBlendingNum(%d)",
            isAssignable, mAssignedSources.size(), getSrcMaxBlendingNum(src, dst));
    return isAssignable;
}
bool ExynosMPP::isAssignable(ExynosDisplay *display,
        struct exynos_image &src, struct exynos_image &dst)
{
    bool isAssignable = isAssignableState(display, src, dst);
    return (isAssignable & hasEnoughCapa(src, dst));
}

bool ExynosMPP::hasEnoughCapa(struct exynos_image &src, struct exynos_image &dst)
{
    if (mCapacity == -1)
        return true;

    float totalUsedCapacity = ExynosResourceManager::getResourceUsedCapa(*this);
    float requiredCapacity = getRequiredCapacity(src, dst);

    MPP_LOGD(eDebugCapacity|eDebugMPP, "mCapacity(%f), usedCapacity(%f), RequiredCapacity(%f)",
            mCapacity, totalUsedCapacity, requiredCapacity);

    if (mCapacity >= (totalUsedCapacity + requiredCapacity))
        return true;
    else
        return false;
}

float ExynosMPP::getRequiredCapacity(struct exynos_image &src,
        struct exynos_image &dst)
{
    float capacity = 0;
    float cycles = 0;
    if (mPhysicalType == MPP_G2D) {

        float PPC = G2D_BASE_PPC;
        uint32_t srcResolution = src.w * src.h;
        uint32_t dstResolution = dst.w * dst.h;
        uint32_t maxResolution = max(srcResolution, dstResolution);

        if (src.transform != 0)
            PPC = G2D_BASE_PPC_ROT;

        cycles = maxResolution/PPC;
        MPP_LOGD(eDebugCapacity|eDebugMPP, "added cycles: %f, PPC: %f, srcResolution: %d, dstResolution: %d, mUsedCapacity(%f)",
                cycles, PPC, srcResolution, dstResolution, mUsedCapacity);

    }
    capacity = cycles/getMPPClock();

    return capacity;
}

int32_t ExynosMPP::updateUsedCapacity()
{
    int32_t ret = NO_ERROR;
    if (mCapacity == -1)
        return ret;

    float capacity = 0;
    mUsedCapacity = 0;
    if ((mPhysicalType == MPP_G2D) &&
        (mAssignedDisplay != NULL) &&
        (mAssignedSources.size() > 0)) {
        /* nrSrcs includes current source */
        float cycles = 0;
        for (uint32_t i = 0; i < mAssignedSources.size(); i++) {
            float PPC = G2D_BASE_PPC;
            float srcCycles = 0;
            uint32_t srcResolution = mAssignedSources[i]->mSrcImg.w * mAssignedSources[i]->mSrcImg.h;
            uint32_t dstResolution = mAssignedSources[i]->mDstImg.w * mAssignedSources[i]->mDstImg.h;
            uint32_t maxResolution = max(srcResolution, dstResolution);

            if (mAssignedSources[i]->mSrcImg.transform != 0)
                PPC = G2D_BASE_PPC_ROT;

            srcCycles = maxResolution/PPC;
            cycles += srcCycles;
            MPP_LOGD(eDebugMPP, "Src[%d] cycles: %f, total cycles: %f, PPC: %f, srcResolution: %d, dstResolution: %d",
                    i, srcCycles, cycles, PPC, srcResolution, dstResolution);
        }

        if (mLogicalType != MPP_LOGICAL_G2D_YUV) {
            cycles += ((mAssignedDisplay->mXres * mAssignedDisplay->mYres) / G2D_BASE_PPC_COLORFILL);
            MPP_LOGD(eDebugMPP, "colorfill cycles: %f, total cycles: %f",
                    ((mAssignedDisplay->mXres * mAssignedDisplay->mYres) / G2D_BASE_PPC_COLORFILL), cycles);
        }
        capacity = cycles/getMPPClock();

        mUsedCapacity = capacity;
    }
    MPP_LOGD(eDebugCapacity|eDebugMPP, "assigned layer size(%zu), mUsedCapacity: %f", mAssignedSources.size(), mUsedCapacity);
    return mUsedCapacity;
}

uint32_t ExynosMPP::getMPPClock()
{
    if (mPhysicalType == MPP_G2D)
        return 667000;
    else
        return 0;
}

uint32_t ExynosMPP::getRestrictionClassification(struct exynos_image &img)
{
    return !!(isFormatRgb(img.format) == false);
}

int ExynosMPP::prioritize(int priority)
{
    if ((mPhysicalType != MPP_G2D) ||
        (mCompositor == NULL)) {
        MPP_LOGE("invalid function call");
        return -1;
    }
    int ret = NO_ERROR;
    ret = mCompositor->prioritize(priority);

    if ((priority > 0) && (ret == 1))
    {
        /* G2D Driver returned EBUSY */
        mHWBusyFlag = true;
    }
    MPP_LOGD(eDebugMPP, "set resource prioritize (%d), ret(%d), mHWBusyFlag(%d)", priority, ret, mHWBusyFlag);

    return ret;
}

uint32_t ExynosMPP::increaseDstBuffIndex()
{
    if (mAllocOutBufFlag)
        mCurrentDstBuf = (mCurrentDstBuf + 1) % NUM_MPP_DST_BUFS(mLogicalType);
    return mCurrentDstBuf;
}

void ExynosMPP::dump(String8& result)
{
    int32_t assignedDisplayType = -1;
    if (mAssignedDisplay != NULL)
        assignedDisplayType = mAssignedDisplay->mType;

    result.appendFormat("%s: types mppType(%d), (p:%d, l:0x%2x), indexs(p:%d, l:%d), preAssignDisplay(0x%2x)\n",
            mName.string(), mMPPType, mPhysicalType, mLogicalType, mPhysicalIndex, mLogicalIndex, mPreAssignDisplayList);
    result.appendFormat("\tEnable: %d, HWState: %d, AssignedState: %d, assignedDisplay(%d)\n",
            mEnable, mHWState, mAssignedState, assignedDisplayType);
    result.appendFormat("\tPrevAssignedState: %d, PrevAssignedDisplayType: %d, ReservedDisplay: %d\n",
            mPrevAssignedState, mPrevAssignedDisplayType, mReservedDisplay);
    result.appendFormat("\tassinedSourceNum(%zu), Capacity(%f), CapaUsed(%f), mCurrentDstBuf(%d)\n",
            mAssignedSources.size(), mCapacity, mUsedCapacity, mCurrentDstBuf);

}

/*************************************************************************************************************
 * Functions to use MPPFactory
 * ***********************************************************************************************************/
void *ExynosMPP::createMPPLib()
{
    ATRACE_CALL();
    if (mPhysicalType == MPP_MSC) {
        /* For Scaler */
        mMppFact = new MppFactory();
        /* drm mode will be set in configuration */
        mLibmpp = mMppFact->CreateMpp(HW_SCAL0, GSC_M2M_MODE, GSC_DUMMY, 1 /*drm mode*/);
        return reinterpret_cast<void *>(mLibmpp);
    }
    return NULL;
}

void ExynosMPP::destroyMPPLib(void *handle)
{
    ATRACE_CALL();
    if (mPhysicalType == MPP_MSC) {
        mLibmpp->DestroyMpp(handle);
        delete(mMppFact);
        mMppFact = NULL;
        mLibmpp = NULL;
    }
}

int ExynosMPP::configMPP(void *handle, exynos_mpp_img *src, exynos_mpp_img *dst)
{
    ATRACE_CALL();
    return mLibmpp->ConfigMpp(handle, src, dst);
}

int ExynosMPP::runMPP(void *handle, exynos_mpp_img *src, exynos_mpp_img *dst)
{
    ATRACE_CALL();
    return mLibmpp->RunMpp(handle, src, dst);
}

int ExynosMPP::stopMPP(void *handle)
{
    ATRACE_CALL();
    return mLibmpp->StopMpp(handle);
}

int ExynosMPP::setCSCProperty(void *handle, unsigned int eqAuto, unsigned int fullRange, unsigned int colorspace)
{
    return mLibmpp->SetCSCProperty(handle, eqAuto, fullRange, colorspace);
}

void ExynosMPP::closeFences()
{
    for (uint32_t i = 0; i < mAssignedSources.size(); i++)
    {
        if (mPhysicalType == MPP_MSC) {
            mSrcImgs[i].mppImg.acquireFenceFd =
                fence_close(mSrcImgs[i].mppImg.acquireFenceFd, mAssignedDisplay,
                        FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_MSC);
            mSrcImgs[i].mppImg.releaseFenceFd =
                fence_close(mSrcImgs[i].mppImg.releaseFenceFd, mAssignedDisplay,
                        FENCE_TYPE_SRC_RELEASE, FENCE_IP_MSC);
        } else {
            mSrcImgs[i].acrylicAcquireFenceFd =
                fence_close(mSrcImgs[i].acrylicAcquireFenceFd, mAssignedDisplay,
                        FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_G2D);
            mSrcImgs[i].acrylicReleaseFenceFd =
                fence_close(mSrcImgs[i].acrylicReleaseFenceFd, mAssignedDisplay,
                        FENCE_TYPE_SRC_RELEASE, FENCE_IP_G2D);
        }
    }
    if (mLastStateFenceFd > 0)
    {
        mLastStateFenceFd = hwcFdClose(mLastStateFenceFd);
    }

    if (mPhysicalType == MPP_MSC) {
        mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd =
            fence_close(mDstImgs[mCurrentDstBuf].mppImg.acquireFenceFd, mAssignedDisplay,
                    FENCE_TYPE_DST_ACQUIRE, FENCE_IP_MSC);
        mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd =
            fence_close(mDstImgs[mCurrentDstBuf].mppImg.releaseFenceFd, mAssignedDisplay,
                    FENCE_TYPE_DST_RELEASE, FENCE_IP_MSC);
    } else {
        mDstImgs[mCurrentDstBuf].acrylicAcquireFenceFd =
            fence_close(mDstImgs[mCurrentDstBuf].acrylicAcquireFenceFd, mAssignedDisplay,
                    FENCE_TYPE_DST_ACQUIRE, FENCE_IP_G2D);
        mDstImgs[mCurrentDstBuf].acrylicReleaseFenceFd =
            fence_close(mDstImgs[mCurrentDstBuf].acrylicReleaseFenceFd, mAssignedDisplay,
                    FENCE_TYPE_DST_RELEASE, FENCE_IP_G2D);
    }
}
