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

#undef LOG_TAG
#define LOG_TAG "virtualdisplay"
#include <hardware/hwcomposer.h>
#include "ExynosVirtualDisplay.h"
#include "../libdevice/ExynosDevice.h"
#include "../libdevice/ExynosLayer.h"

#include "ExynosHWCHelper.h"
#ifdef GRALLOC_VERSION1
#include "gralloc1_priv.h"
#else
#include "gralloc_priv.h"
#endif

ExynosVirtualDisplay::ExynosVirtualDisplay(uint32_t __unused type, ExynosDevice *device)
    : ExynosDisplay(HWC_DISPLAY_VIRTUAL, device)
{
    /* Initialization */
    mDisplayId = HWC_DISPLAY_VIRTUAL;
    mDisplayName = android::String8("VirtualDisplay");
    mDisplayFd = hwcFdClose(mDisplayFd);

    mOutputBufferAcquireFenceFd = -1;
    mOutputBufferReleaseFenceFd = -1;

    mIsWFDState = false;
    mIsSecureVDSState = false;
    mIsSkipFrame = false;
    mPresentationMode = false;

    mSkipFrameCount = 0;

    // TODO : Hard coded currently
    mNumMaxPriorityAllowed = 1;

    mDisplayWidth = 1920;
    mDisplayHeight = 1080;
    mOutputBuffer = NULL;
    mCompositionType = COMPOSITION_GLES;
    mGLESFormat = HAL_PIXEL_FORMAT_RGBA_8888;
    mSinkUsage = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_VIDEO_ENCODER;
    mIsSecureDRM = false;
    mIsNormalDRM = false;
}

ExynosVirtualDisplay::~ExynosVirtualDisplay()
{

}

void ExynosVirtualDisplay::init()
{
    initDisplay();

    // Virtual Display don't use skip static layer.
    mClientCompositionInfo.mEnableSkipStatic = false;

    mPlugState = true;
}

void ExynosVirtualDisplay::deInit()
{
    mPlugState = false;
}

int ExynosVirtualDisplay::setWFDMode(unsigned int mode)
{
    mIsWFDState = !!mode;
    return HWC2_ERROR_NONE;
}

int ExynosVirtualDisplay::getWFDMode()
{
    return mIsWFDState;
}

int ExynosVirtualDisplay::getWFDInfo(int32_t* state, int32_t* compositionType, int32_t* format,
    int64_t* usage, int32_t* width, int32_t* height)
{
    *state = mIsWFDState;
    *compositionType = mCompositionType;
    if (mIsSkipFrame)
        *format = (int32_t)0xFFFFFFFF;
    else if (mIsSecureDRM && !mIsSecureVDSState)
        *format = (int32_t)HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN;
    else
        *format = (int32_t)mGLESFormat;
    *usage = mSinkUsage;
    *width = mDisplayWidth;
    *height = mDisplayHeight;

    return HWC2_ERROR_NONE;
}

int ExynosVirtualDisplay::setSecureVDSMode(unsigned int mode)
{
    mIsWFDState = mIsSecureVDSState = !!mode;
    return HWC2_ERROR_NONE;
}

int ExynosVirtualDisplay::setWFDOutputResolution(
    unsigned int width, unsigned int height)
{
    mDisplayWidth = width;
    mDisplayHeight = height;
    mXres = width;
    mYres = height;
    return HWC2_ERROR_NONE;
}

void ExynosVirtualDisplay::getWFDOutputResolution(
    unsigned int *width, unsigned int *height)
{
    *width = mDisplayWidth;
    *height = mDisplayHeight;
}

void ExynosVirtualDisplay::setPresentationMode(bool use)
{
    mPresentationMode = use;
}

int ExynosVirtualDisplay::getPresentationMode(void)
{
    return mPresentationMode;
}

int ExynosVirtualDisplay::setVDSGlesFormat(int format)
{
    DISPLAY_LOGD(eDebugVirtualDisplay, "setVDSGlesFormat: 0x%x", format);
    mGLESFormat = format;
    return HWC2_ERROR_NONE;
}

int32_t ExynosVirtualDisplay::getDisplayAttribute(
    hwc2_config_t __unused config, int32_t __unused attribute, int32_t* __unused outValue)
{
    return HWC2_ERROR_NONE;
}

int32_t ExynosVirtualDisplay::setOutputBuffer(
    buffer_handle_t buffer, int32_t releaseFence) {
    mOutputBuffer = buffer;
    mOutputBufferAcquireFenceFd = hwc_dup(releaseFence,
            this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_G2D);
    releaseFence = fence_close(releaseFence, this, FENCE_TYPE_SRC_RELEASE, FENCE_IP_G2D);

    if (!this->mUseDecon && mExynosCompositionInfo.mM2mMPP != NULL) {
        mExynosCompositionInfo.mM2mMPP->setOutBuf(mOutputBuffer, mOutputBufferAcquireFenceFd);
        mOutputBufferAcquireFenceFd = -1;
    }
    DISPLAY_LOGD(eDebugVirtualDisplay, "setOutputBuffer(), mOutputBufferAcquireFenceFd %d", mOutputBufferAcquireFenceFd);
    return HWC2_ERROR_NONE;
}

int32_t ExynosVirtualDisplay::validateDisplay(
    uint32_t* outNumTypes, uint32_t* outNumRequests)
{
    DISPLAY_LOGD(eDebugVirtualDisplay, "validateDisplay");
    int32_t ret = HWC2_ERROR_NONE;

    initPerFrameData();

    mClientCompositionInfo.setCompressed(false);

    /* validateDisplay should be called for preAssignResource */
    ret = ExynosDisplay::validateDisplay(outNumTypes, outNumRequests);

    if (checkSkipFrame()) {
        handleSkipFrame();
    } else {
        setDrmMode();
        setSinkBufferUsage();
        setCompositionType();
    }

    return ret;
}

int32_t ExynosVirtualDisplay::presentDisplay(
    int32_t* outRetireFence)
{
    DISPLAY_LOGD(eDebugVirtualDisplay, "presentDisplay, mClientCompositionInfo.mAcquireFence %d",
        mClientCompositionInfo.mAcquireFence);
    int32_t ret = HWC2_ERROR_NONE;

    if (mIsSkipFrame) {
        handleAcquireFence();
        /* this frame is not presented, but mRenderingState is updated to RENDERING_STATE_PRESENTED */
        mRenderingState = RENDERING_STATE_PRESENTED;
        return ret;
    }

    if (!this->mUseDecon) {
        if (mClientCompositionInfo.mHasCompositionLayer) {
            exynos_image src_img;
            exynos_image dst_img;
            this->setCompositionTargetExynosImage(COMPOSITION_CLIENT, &src_img, &dst_img);
            mClientCompositionInfo.setExynosImage(src_img, dst_img);
            mClientCompositionInfo.setExynosMidImage(dst_img);
            mClientCompositionInfo.mAcquireFence = -1;
            if (mClientCompositionInfo.mLastIndex < mExynosCompositionInfo.mLastIndex)
                mClientCompositionInfo.mSrcImg.zOrder = 0;
            else
                mClientCompositionInfo.mSrcImg.zOrder = 1000;
        }
    }

    ret = ExynosDisplay::presentDisplay(outRetireFence);

    if (mCompositionType == COMPOSITION_GLES && outRetireFence) {
        /* handle clientTarget acquireFence */
        *outRetireFence = mClientCompositionInfo.mAcquireFence;
        mClientCompositionInfo.mAcquireFence = -1;
    }
    /* handle outbuf acquireFence */
    if (mOutputBufferAcquireFenceFd >= 0) {
        mOutputBufferAcquireFenceFd = fence_close(mOutputBufferAcquireFenceFd, this,
                FENCE_TYPE_DST_ACQUIRE, FENCE_IP_G2D);
        DISPLAY_LOGD(eDebugVirtualDisplay, "presentDisplay(), mOutputBufferAcquireFenceFd %d", mOutputBufferAcquireFenceFd);
    }

    return ret;
}

int ExynosVirtualDisplay::setWinConfigData()
{
    return NO_ERROR;
}

int ExynosVirtualDisplay::setDisplayWinConfigData()
{
    return NO_ERROR;
}

int32_t ExynosVirtualDisplay::validateWinConfigData()
{
    return NO_ERROR;
}

int ExynosVirtualDisplay::deliverWinConfigData()
{
    mWinConfigData->fence = -1;
    return 0;
}

int ExynosVirtualDisplay::setReleaseFences()
{
    return NO_ERROR;
}

bool ExynosVirtualDisplay::checkFrameValidation()
{
    if (mOutputBuffer == NULL) {
        handleAcquireFence();
        return false;
    }

    private_handle_t *outbufHandle = private_handle_t::dynamicCast(mOutputBuffer);
    if (outbufHandle == NULL) {
        handleAcquireFence();
        return false;
    }

    return true;

}

void ExynosVirtualDisplay::setSinkBufferUsage()
{
    mSinkUsage = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_VIDEO_ENCODER;
    if (mIsSecureDRM) {
        mSinkUsage |= GRALLOC_USAGE_SW_READ_NEVER |
            GRALLOC_USAGE_SW_WRITE_NEVER |
            GRALLOC_USAGE_PROTECTED;
    } else if (mIsNormalDRM)
        mSinkUsage |= GRALLOC_USAGE_PRIVATE_NONSECURE;
}

void ExynosVirtualDisplay::setCompositionType()
{
    size_t compositionClientLayerCount = 0;
    size_t CompositionDeviceLayerCount = 0;;
    for (size_t i = 0; i < mLayers.size(); i++) {
        ExynosLayer *layer = mLayers[i];
        if (layer->mValidateCompositionType == HWC2_COMPOSITION_CLIENT ||
            layer->mValidateCompositionType == HWC2_COMPOSITION_INVALID) {
            compositionClientLayerCount++;
        }
        if (layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE ||
            layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS) {
            CompositionDeviceLayerCount++;
        }
    }
    if (compositionClientLayerCount > 0 && CompositionDeviceLayerCount > 0) {
        mCompositionType = COMPOSITION_MIXED;
    } else if (compositionClientLayerCount == 0 && CompositionDeviceLayerCount > 0) {
        mCompositionType = COMPOSITION_HWC;
    } else if (compositionClientLayerCount > 0 && CompositionDeviceLayerCount == 0) {
        mCompositionType = COMPOSITION_GLES;
    } else {
#ifdef TARGET_USES_HWC2
        mCompositionType = COMPOSITION_HWC;
#else
        mCompositionType = COMPOSITION_GLES;
#endif
    }
#ifdef USES_DISABLE_COMPOSITIONTYPE_GLES
    if (mCompositionType == COMPOSITION_GLES)
        mCompositionType = COMPOSITION_MIXED;
#endif
    DISPLAY_LOGD(eDebugVirtualDisplay, "setCompositionType(), compositionClientLayerCount %zu, CompositionDeviceLayerCount %zu, mCompositionType %d",
        compositionClientLayerCount, CompositionDeviceLayerCount, mCompositionType);
}

void ExynosVirtualDisplay::initPerFrameData()
{
    mIsSkipFrame = false;
    mIsSecureDRM = false;
    mIsNormalDRM = false;
    mCompositionType = COMPOSITION_HWC;
    mSinkUsage = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_VIDEO_ENCODER;
}

bool ExynosVirtualDisplay::checkSkipFrame()
{
    for (size_t i = 0; i < mLayers.size(); i++) {
        ExynosLayer *layer = mLayers[i];
#ifdef TARGET_USES_HWC2
        if (layer->mCompositionType == HWC2_COMPOSITION_SCREENSHOT) {
#else
        if (layer->mLayerFlag & VIRTUAL_DISLAY_SKIP_LAYER) {
#endif
            DISPLAY_LOGD(eDebugVirtualDisplay, "checkSkipFrame(), layer include VIRTUAL_DISLAY_SKIP_LAYER flag");
            return true;
        }
    }

    if (mSkipFrameCount > 0) {
        DISPLAY_LOGD(eDebugVirtualDisplay, "checkSkipFrame(), mSkipFrameCount %d", mSkipFrameCount);
        return true;
    }

    return false;
}

void ExynosVirtualDisplay::setDrmMode()
{
    mIsSecureDRM = false;
    for (size_t i = 0; i < mLayers.size(); i++) {
        ExynosLayer *layer = mLayers[i];
        if ((layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE ||
            layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS) &&
            layer->mLayerBuffer && getDrmMode(layer->mLayerBuffer->flags) == SECURE_DRM) {
            mIsSecureDRM = true;
            DISPLAY_LOGD(eDebugVirtualDisplay, "include secure drm layer");
        }
        if ((layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE ||
            layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS) &&
            layer->mLayerBuffer && getDrmMode(layer->mLayerBuffer->flags) == NORMAL_DRM) {
            mIsNormalDRM = true;
            DISPLAY_LOGD(eDebugVirtualDisplay, "include normal drm layer");
        }
    }
    DISPLAY_LOGD(eDebugVirtualDisplay, "setDrmMode(), mIsSecureDRM %d", mIsSecureDRM);
}

void ExynosVirtualDisplay::handleSkipFrame()
{
    mIsSkipFrame = true;
    for (size_t i = 0; i < mLayers.size(); i++) {
        ExynosLayer *layer = mLayers[i];
        layer->mValidateCompositionType = HWC2_COMPOSITION_DEVICE;
    }
    mIsSecureDRM = false;
    mIsNormalDRM = false;
    mCompositionType = COMPOSITION_HWC;
    mSinkUsage = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_VIDEO_ENCODER;
    DISPLAY_LOGD(eDebugVirtualDisplay, "handleSkipFrame()");
}

void ExynosVirtualDisplay::handleAcquireFence()
{
    /* handle fence of DEVICE or EXYNOS composition layers */
    for (size_t i = 0; i < mLayers.size(); i++) {
        ExynosLayer *layer = mLayers[i];
        if (layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE ||
            layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS) {
            layer->mReleaseFence = layer->mAcquireFence;
            setFenceInfo(layer->mAcquireFence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER, FENCE_TO);
            layer->mAcquireFence = -1;
        }
    }
    mClientCompositionInfo.mReleaseFence = mClientCompositionInfo.mAcquireFence;
    setFenceInfo(mClientCompositionInfo.mAcquireFence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB, FENCE_TO);
    mClientCompositionInfo.mAcquireFence = -1;

    mOutputBufferReleaseFenceFd = mOutputBufferAcquireFenceFd;
    mOutputBufferAcquireFenceFd = -1;
    DISPLAY_LOGD(eDebugVirtualDisplay, "handleAcquireFence()");
}

