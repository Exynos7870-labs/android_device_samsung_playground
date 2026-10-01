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

//#include <linux/fb.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <cutils/properties.h>

#include "ExynosHWCDebug.h"
#include "ExynosDisplay.h"
#include "ExynosLayer.h"
#include "ExynosHWCHelper.h"
#include "exynos_format.h"

/**
 * ExynosDisplay implementation
 */

using namespace android;
extern struct exynos_hwc_cotrol exynosHWCControl;

ExynosCompositionInfo::ExynosCompositionInfo(uint32_t type)
    : ExynosMPPSource(MPP_SOURCE_COMPOSITION_TARGET, this),
    mType(type),
    mHasCompositionLayer(false),
    mFirstIndex(-1),
    mLastIndex(-1),
    mTargetBuffer(NULL),
    mDataSpace(HAL_DATASPACE_UNKNOWN),
    mAcquireFence(-1),
    mReleaseFence(-1),
    mEnableSkipStatic(false),
    mSkipStaticInitFlag(false),
    mSkipFlag(false),
    mWindowIndex(-1)
{
    /* If AFBC compression of mTargetBuffer is changed, */
    /* mCompressed should be set properly before resource assigning */

	char value[256];
	int afbc_prop;
	property_get("ro.vendor.ddk.set.afbc", value, "0");
	afbc_prop = atoi(value);

    if (afbc_prop == 0)
        mCompressed = false;
    else
        mCompressed = true;

    memset(&mSkipSrcInfo, 0, sizeof(mSkipSrcInfo));

    if(type == COMPOSITION_CLIENT)
        mEnableSkipStatic = true;

    memset(&mLastConfig, 0x0, sizeof(mLastConfig));
    mLastConfig.fence_fd = -1;
}

void ExynosCompositionInfo::initializeInfos(ExynosDisplay *display)
{
    mHasCompositionLayer = false;
    mFirstIndex = -1;
    mLastIndex = -1;
    mTargetBuffer = NULL;
    mDataSpace = HAL_DATASPACE_UNKNOWN;
    if (mAcquireFence >= 0) {
        HWC_LOGE(NULL, "ExynosCompositionInfo(%d):: mAcquire is not initialized(%d)", mType, mAcquireFence);
        if (display != NULL)
            fence_close(mAcquireFence, display, FENCE_TYPE_UNDEFINED, FENCE_IP_UNDEFINED);
    }
    mAcquireFence = -1;
    if (mReleaseFence >= 0) {
        HWC_LOGE(NULL, "ExynosCompositionInfo(%d):: mReleaseFence is not initialized(%d)", mType, mReleaseFence);
        if (display != NULL)
            fence_close(mReleaseFence, display, FENCE_TYPE_UNDEFINED, FENCE_IP_UNDEFINED);
    }
    mReleaseFence = -1;
    mWindowIndex = -1;
    mOtfMPP = NULL;
    mM2mMPP = NULL;
}

void ExynosCompositionInfo::setTargetBuffer(ExynosDisplay *display, private_handle_t *handle,
        int32_t acquireFence, android_dataspace dataspace)
{
    mTargetBuffer = handle;
    if (mType == COMPOSITION_CLIENT) {
        if (display != NULL)
            mAcquireFence = hwcCheckFenceDebug(display, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_FB, acquireFence);
    } else {
        if (display != NULL)
            mAcquireFence = hwcCheckFenceDebug(display, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_G2D, acquireFence);
    }

    mDataSpace = dataspace;
}

void ExynosCompositionInfo::setCompressed(bool compressed)
{
    mCompressed = compressed;
}

bool ExynosCompositionInfo::getCompressed()
{
    return mCompressed;
}

void ExynosCompositionInfo::dump(String8& result)
{
    result.appendFormat("CompositionInfo (%d)\n", mType);
    result.appendFormat("mHasCompositionLayer(%d)\n", mHasCompositionLayer);
    if (mHasCompositionLayer) {
        result.appendFormat("\tfirstIndex: %d, lastIndex: %d, dataSpace: 0x%8x, compressed: %d, windowIndex: %d\n",
                mFirstIndex, mLastIndex, mDataSpace, mCompressed, mWindowIndex);
        result.appendFormat("\thandle: %p, acquireFence: %d, releaseFence: %d, skipFlag: %d",
                mTargetBuffer, mAcquireFence, mReleaseFence, mSkipFlag);
        if ((mOtfMPP == NULL) && (mM2mMPP == NULL))
            result.appendFormat("\tresource is not assigned\n\n");
        if (mOtfMPP != NULL)
            result.appendFormat("\tassignedMPP: %s\n", mOtfMPP->mName.string());
        if (mM2mMPP != NULL)
            result.appendFormat("\t%s\n\n", mM2mMPP->mName.string());
    }
    if (mTargetBuffer != NULL) {
        uint64_t internal_format = 0;
        internal_format = mTargetBuffer->internal_format;
        result.appendFormat("\tinternal_format: 0x%" PRIx64 ", compressed: %d\n\n",
                internal_format, isCompressed(mTargetBuffer));
    }
}

String8 ExynosCompositionInfo::getTypeStr()
{
    switch(mType) {
    case COMPOSITION_NONE:
        return String8("COMPOSITION_NONE");
    case COMPOSITION_CLIENT:
        return String8("COMPOSITION_CLIENT");
    case COMPOSITION_EXYNOS:
        return String8("COMPOSITION_EXYNOS");
    default:
        return String8("InvalidType");
    }
}

ExynosDisplay::ExynosDisplay(uint32_t type, ExynosDevice *device)
: mType(type),
    mXres(1440),
    mYres(2960),
    mXdpi(25400),
    mYdpi(25400),
    mVsyncPeriod(16666666),
    mDSCHSliceNum(1),
    mDSCYSliceSize(mYres),
    mDevice(device),
    mPlugState(false),
    mHasSingleBuffer(false),
    mEnableFBCrop(false),
    mResourceManager(NULL),
    mClientCompositionInfo(COMPOSITION_CLIENT),
    mExynosCompositionInfo(COMPOSITION_EXYNOS),
    mGeometryChanged(0x0),
    mRenderingState(RENDERING_STATE_NONE),
    mDisplayBW(0),
    mDynamicReCompMode(NO_MODE_SWITCH),
    mLastFpsTime(0),
    mFrameCount(0),
    mLastFrameCount(0),
    mErrorFrameCount(0),
    mUpdateEventCnt(0),
    mUpdateCallCnt(0),
    mFps(0),
    mWinConfigData(NULL),
    mLastWinConfigData(NULL),
    mRetireFence(-1),
    mUseSecureDMA(false),
    mContentFlags(0x0),
    mMaxWindowNum(NUM_HW_WINDOWS),
    mWindowNumUsed(0),
    mBaseWindowIndex(0),
    mBlendingNoneIndex(-1),
    mNumMaxPriorityAllowed(1),
    updateThreadStatus(0),
    mHWC1LayerList(NULL)
{
//    ALOGD("HWC2 : %s : %d ", __func__, __LINE__);
    int refreshRate = 0;

    /* get PSR info */
    FILE *psrInfoFd;
    int psrMode;
    int panelType, panelModeCnt = 0;
    uint64_t refreshCalcFactor = 0;

    /* Initialization */
    this->mDisplayId = HWC_DISPLAY_PRIMARY;
    mDisplayName = android::String8("PrimaryDisplay");

    this->mPowerModeState = HWC2_POWER_MODE_OFF;
    this->mVsyncState = HWC2_VSYNC_DISABLE;

    /* TODO : Exception handling here */

    if (device == NULL) {
        ALOGE("Display creation failed!");
        return;
    }

    /* TODO : Is this hard-coding?? */
    mDisplayFd = open("/dev/graphics/fb0", O_RDWR);

    if (mDisplayFd < 0) {
        ALOGE("failed to open framebuffer");
        goto err_open_fb;
    }

    /* Get screen info from Display DD */
    struct fb_var_screeninfo info;

    if (ioctl(mDisplayFd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("FBIOGET_VSCREENINFO ioctl failed: %s", strerror(errno));
        goto err_ioctl;
    }

    if (info.reserved[0] == 0 && info.reserved[1] == 0) {
        info.reserved[0] = info.xres;
        info.reserved[1] = info.yres;

        if (ioctl(mDisplayFd, FBIOPUT_VSCREENINFO, &info) == -1) {
            ALOGE("FBIOPUT_VSCREENINFO ioctl failed: %s", strerror(errno));
            goto err_ioctl;
        }
    }

    mXres = info.reserved[0];
    mYres = info.reserved[1];

    refreshCalcFactor = uint64_t( info.upper_margin + info.lower_margin + mYres + info.vsync_len )
                        * ( info.left_margin  + info.right_margin + mXres + info.hsync_len )
                        * info.pixclock;

    if (refreshCalcFactor)
        refreshRate = 1000000000000LLU / refreshCalcFactor;

    if (refreshRate == 0) {
        ALOGW("invalid refresh rate, assuming 60 Hz");
        refreshRate = 60;
    }

    mXdpi = 1000 * (mXres * 25.4f) / info.width;
    mYdpi = 1000 * (mYres * 25.4f) / info.height;
    mVsyncPeriod = 1000000000 / refreshRate;

    ALOGD("using\n"
            "xres         = %d px\n"
            "yres         = %d px\n"
            "width        = %d mm (%f dpi)\n"
            "height       = %d mm (%f dpi)\n"
            "refresh rate = %d Hz\n",
            mXres, mYres, info.width, mXdpi / 1000.0,
            info.height, mYdpi / 1000.0, refreshRate);

    // Prepare multi resolution
    // Will be exynosHWCControl.multiResoultion
    mResolutionInfo.nNum = 1;
    mResolutionInfo.nResolution[0].w = 1440;
    mResolutionInfo.nResolution[0].h = 2960;
    mResolutionInfo.nDSCYSliceSize[0] = 40;
    mResolutionInfo.nDSCXSliceSize[0] = 1440 / 2;
    mResolutionInfo.nPanelType[0] = PANEL_DSC;
    mResolutionInfo.nResolution[1].w = 1080;
    mResolutionInfo.nResolution[1].h = 2220;
    mResolutionInfo.nDSCYSliceSize[1] = 30;
    mResolutionInfo.nDSCXSliceSize[1] = 1080 / 2;
    mResolutionInfo.nPanelType[1] = PANEL_DSC;
    mResolutionInfo.nResolution[2].w = 720;
    mResolutionInfo.nResolution[2].h = 1480;
    mResolutionInfo.nDSCYSliceSize[2] = 74;
    mResolutionInfo.nDSCXSliceSize[2] = 720;
    mResolutionInfo.nPanelType[2] = PANEL_LEGACY;

    /* get PSR info */
    psrInfoFd = NULL;
    mPsrMode = psrMode = PSR_NONE;
    panelType = PANEL_LEGACY;

    char devname[MAX_DEV_NAME + 1];
    devname[MAX_DEV_NAME] = '\0';

    strncpy(devname, VSYNC_DEV_PREFIX, MAX_DEV_NAME);
    strlcat(devname, PSR_DEV_NAME, MAX_DEV_NAME);

    char psrDevname[MAX_DEV_NAME + 1];
    memset(psrDevname, 0, MAX_DEV_NAME + 1);

    strncpy(psrDevname, devname, strlen(devname) - 5);
    strlcat(psrDevname, "psr_info", MAX_DEV_NAME);
    ALOGI("PSR info devname = %s\n", psrDevname);

    psrInfoFd = fopen(psrDevname, "r");

    if (psrInfoFd == NULL) {
        ALOGW("HWC needs to know whether LCD driver is using PSR mode or not\n");
        devname[strlen(VSYNC_DEV_PREFIX)] = '\0';
        strlcat(devname, VSYNC_DEV_MIDDLE, MAX_DEV_NAME);
        strlcat(devname, PSR_DEV_NAME, MAX_DEV_NAME);
        ALOGI("Retrying with %s", devname);
        psrInfoFd = fopen(devname, "r");
    }

    if (psrInfoFd != NULL) {
        char val[4] = {0};
        if (fread(&val, 1, 1, psrInfoFd) == 1) {
            mPsrMode = psrMode = (0x03 & atoi(val));
        }
    } else {
        ALOGW("HWC needs to know whether LCD driver is using PSR mode or not (2nd try)\n");
    }

    ALOGI("PSR mode   = %d (0: video mode, 1: DP PSR mode, 2: MIPI-DSI command mode)\n",
            psrMode);

    if (psrInfoFd != NULL) {
        /* get DSC info */
        if (exynosHWCControl.multiResolution == true) {
            uint32_t sliceXSize = mXres;
            uint32_t sliceYSize = mYres;
            uint32_t xSize = mXres;
            uint32_t ySize = mYres;
            uint32_t panelType = PANEL_LEGACY;

            if (fscanf(psrInfoFd, "%d\n", &panelModeCnt) < 0) {
                ALOGE("Fail to read panel mode count");
            } else {
                ALOGI("res count : %d", panelModeCnt);
                mResolutionInfo.nNum = panelModeCnt;
                for(int i = 0; i < panelModeCnt; i++) {
                    if (fscanf(psrInfoFd, "%d\n%d\n%d\n%d\n%d\n", &xSize, &ySize, &sliceXSize, &sliceYSize, &panelType) < 0) {
                        ALOGE("Fail to read slice information");
                    } else {
                        mResolutionInfo.nResolution[i].w = xSize;
                        mResolutionInfo.nResolution[i].h = ySize;
                        mResolutionInfo.nDSCXSliceSize[i] = sliceXSize;
                        mResolutionInfo.nDSCYSliceSize[i] = sliceYSize;
                        mResolutionInfo.nPanelType[i] = panelType;
                        ALOGI("mode no. : %d, Width : %d, Height : %d, X_Slice_Size : %d, Y_Slice_Size : %d, Panel type : %d\n", i,
                                mResolutionInfo.nResolution[i].w, mResolutionInfo.nResolution[i].h,
                                mResolutionInfo.nDSCXSliceSize[i], mResolutionInfo.nDSCYSliceSize[i], mResolutionInfo.nPanelType[i]);
                    }
                }
                mDSCHSliceNum = mXres / mResolutionInfo.nDSCXSliceSize[0];
                mDSCYSliceSize = mResolutionInfo.nDSCYSliceSize[0];
            }
        } else {
            uint32_t sliceNum = 1;
            uint32_t sliceSize = mYres;
            if (fscanf(psrInfoFd, "\n%d\n%d\n", &sliceNum, &sliceSize) < 0) {
                ALOGE("Fail to read slice information");
            } else {
                mDSCHSliceNum = sliceNum;
                mDSCYSliceSize = sliceSize;
            }
        }
        fclose(psrInfoFd);
    }

    ALOGI("DSC H_Slice_Num: %d, Y_Slice_Size: %d (for window partial update)", mDSCHSliceNum, mDSCYSliceSize);

    mWinConfigData = (struct decon_win_config_data *)malloc(sizeof(*mWinConfigData));
    if (mWinConfigData == NULL)
        ALOGE("Fail to allocate mWinConfigData");
    else {
        memset(mWinConfigData, 0, sizeof(*mWinConfigData));
        mWinConfigData->fence = -1;
        for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
            mWinConfigData->config[i].fence_fd = -1;
        }
    }

    mLastWinConfigData = (struct decon_win_config_data *)malloc(sizeof(*mLastWinConfigData));
    if (mLastWinConfigData == NULL)
        ALOGE("Fail to allocate mLastWinConfigData");
    else {
        memset(mLastWinConfigData, 0, sizeof(*mLastWinConfigData));
        mLastWinConfigData->fence = -1;
    }

    mResourceManager = device->mResourceManager;

    mUseDecon = true;

    return;

err_ioctl:
    hwcFdClose(mDisplayFd);
err_open_fb:
    return;
}

ExynosDisplay::~ExynosDisplay()
{
    if (mLastWinConfigData != NULL)
        free(mLastWinConfigData);

    if (mWinConfigData != NULL)
        free(mWinConfigData);

    if (mDisplayFd >= 0)
        fence_close(mDisplayFd, this, FENCE_TYPE_UNDEFINED, FENCE_IP_UNDEFINED);
    mDisplayFd = -1;
}

/**
 * Member function for Dynamic AFBC Control solution.
 */
bool ExynosDisplay::comparePreferedLayers() {
    return false;
}

int ExynosDisplay::getDisplayId() {
    return mDisplayId;
}

void ExynosDisplay::initDisplay() {
    mClientCompositionInfo.initializeInfos(this);
    mClientCompositionInfo.mEnableSkipStatic = true;
    mClientCompositionInfo.mSkipStaticInitFlag = false;
    mClientCompositionInfo.mSkipFlag = false;
    memset(&mClientCompositionInfo.mSkipSrcInfo, 0x0, sizeof(mClientCompositionInfo.mSkipSrcInfo));
    memset(&mClientCompositionInfo.mLastConfig, 0x0, sizeof(mClientCompositionInfo.mLastConfig));
    mClientCompositionInfo.mLastConfig.fence_fd = -1;

    mExynosCompositionInfo.initializeInfos(this);
    mExynosCompositionInfo.mEnableSkipStatic = false;
    mExynosCompositionInfo.mSkipStaticInitFlag = false;
    mExynosCompositionInfo.mSkipFlag = false;
    memset(&mExynosCompositionInfo.mSkipSrcInfo, 0x0, sizeof(mExynosCompositionInfo.mSkipSrcInfo));
    memset(&mExynosCompositionInfo.mLastConfig, 0x0, sizeof(mExynosCompositionInfo.mLastConfig));
    mExynosCompositionInfo.mLastConfig.fence_fd = -1;

    mGeometryChanged = 0x0;
    mRenderingState = RENDERING_STATE_NONE;
    mDisplayBW = 0;
    mDynamicReCompMode = NO_MODE_SWITCH;

    memset(mWinConfigData, 0, sizeof(*mWinConfigData));
    mWinConfigData->fence = -1;
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        mWinConfigData->config[i].fence_fd = -1;
    }

    memset(mLastWinConfigData, 0, sizeof(*mLastWinConfigData));
    mLastWinConfigData->fence = -1;
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        mLastWinConfigData->config[i].fence_fd = -1;
    }
}

/**
 * @param *outLayer
 * @return int32_t
 */
int32_t ExynosDisplay::destroyLayer(hwc2_layer_t *outLayer) {

    /* TODO : Implemenatation here */
    if (outLayer == NULL)
        return HWC2_ERROR_BAD_LAYER;

    mLayers.remove((ExynosLayer*)outLayer);

    delete (ExynosLayer*)outLayer;

    return HWC2_ERROR_NONE;
}

/**
 * @return void
 *
 */
void ExynosDisplay::destroyLayers() {
    while (!mLayers.isEmpty()) {
        ExynosLayer *layer = mLayers[0];
        if (layer != NULL) {
            mLayers.remove(layer);
            delete layer;
        }
    }
}

/**
 * @param index
 * @return ExynosLayer
 */
ExynosLayer *ExynosDisplay::getLayer(uint32_t index) {
    /* TODO : Imeplementation here */
    if(mLayers[index]!=NULL) {
        return mLayers[index];
    }
    else {
        HWC_LOGE(this, "HWC2 : %s : %d, wrong layer request!", __func__, __LINE__);
        return NULL;
    }
}

/**
 * @return void
 */
void ExynosDisplay::doPreProcessing() {
    /* Low persistence setting */
    int ret = 0;
    uint32_t selfRefresh = 0;
    unsigned int skipProcessing = 1;
    bool hasSingleBuffer = false;
    mBlendingNoneIndex = -1;

    for (size_t i=0; i < mLayers.size(); i++) {
        private_handle_t *handle = mLayers[i]->mLayerBuffer;
        if ((handle != NULL) &&
#ifdef GRALLOC_VERSION1
            (handle->consumer_usage & GRALLOC1_CONSUMER_USAGE_DAYDREAM_SINGLE_BUFFER_MODE))
#else
            (handle->flags & GRALLOC_USAGE_DAYDREAM_SINGLE_BUFFER_MODE))
#endif
        {
            hasSingleBuffer = true;
        }
        /* Prepare to Source copy layer blending exception */
        if ((i != 0) && (mLayers[i]->mBlending == HWC_BLENDING_NONE)) {
            if (handle == NULL)
                mBlendingNoneIndex = i;
            else if ((getDrmMode(handle) == NO_DRM) &&
                     isFormatRgb(handle->format) &&
                     ((mLayers[i]->mLayerFlag & HWC_SET_OPAQUE) == 0))
                mBlendingNoneIndex = i;
        }
    }

    if (mHasSingleBuffer != hasSingleBuffer) {
        if (hasSingleBuffer) {
            selfRefresh = 1;
            skipProcessing = 0;
        } else {
            selfRefresh = 0;
            skipProcessing = 1;
        }
        if ((ret = ioctl(mDisplayFd, S3CFB_DECON_SELF_REFRESH, &selfRefresh)) < 0)
            DISPLAY_LOGE("ioctl S3CFB_LOW_PERSISTENCE failed: %s ret(%d)", strerror(errno), ret);
        mHasSingleBuffer = hasSingleBuffer;
        mDevice->setSkipM2mProcessing(skipProcessing);
        mDevice->setSkipStaticLayer(skipProcessing);
    }

    return;
}

/**
 * @return int
 */
int ExynosDisplay::checkDynamicReCompMode() {
    unsigned int updateFps = 0;
    unsigned int lcd_size = this->mXres * this->mYres;
    uint64_t TimeStampDiff;
    uint64_t w = 0, h = 0, incomingPixels = 0;
    uint64_t maxFps = 0 , layerFps = 0;

    if (!exynosHWCControl.useDynamicRecomp) {
        mLastModeSwitchTimeStamp = 0;
        mDynamicReCompMode = NO_MODE_SWITCH;
        return 0;
    }

    /* initialize the Timestamps */
    if (!mLastModeSwitchTimeStamp) {
        mLastModeSwitchTimeStamp = mLastUpdateTimeStamp;
        mDynamicReCompMode = NO_MODE_SWITCH;
        return 0;
    }

    /* If video layer is there, skip the mode switch */
    for (size_t i=0; i < mLayers.size(); i++) {
        if ((mLayers[i]->mOverlayPriority >= ePriorityHigh) ||
                mLayers[i]->mPreprocessedInfo.preProcessed) {
            if (mDynamicReCompMode != DEVICE_2_CLIENT) {
                return 0;
            } else {
                mDynamicReCompMode = CLIENT_2_DEVICE;
                mUpdateCallCnt = 0;
                mLastModeSwitchTimeStamp = mLastUpdateTimeStamp;
                DISPLAY_LOGD(eDebugDynamicRecomp, "[DYNAMIC_RECOMP] GLES_2_HWC by video layer");
                return CLIENT_2_DEVICE;
            }
        }
    }

    for (size_t i=0; i < mLayers.size(); i++) {
        w = WIDTH(mLayers[i]->mPreprocessedInfo.displayFrame);
        h = HEIGHT(mLayers[i]->mPreprocessedInfo.displayFrame);
        incomingPixels += w * h;
    }

    /* Mode Switch is not required if total pixels are not more than the threshold */
    if (incomingPixels <= lcd_size) {
        if (mDynamicReCompMode != DEVICE_2_CLIENT) {
            return 0;
        } else {
            mDynamicReCompMode = CLIENT_2_DEVICE;
            mUpdateCallCnt = 0;
            mLastModeSwitchTimeStamp = mLastUpdateTimeStamp;
            DISPLAY_LOGD(eDebugDynamicRecomp, "[DYNAMIC_RECOMP] GLES_2_HWC by BW check");
            return CLIENT_2_DEVICE;
        }
    }

    /*
     * There will be at least one composition call per one minute (because of time update)
     * To minimize the analysis overhead, just analyze it once in a second
     */
    TimeStampDiff = systemTime(SYSTEM_TIME_MONOTONIC) - mLastModeSwitchTimeStamp;

    /*
     * previous CompModeSwitch was CLIENT_2_DEVICE: check fps every 250ms from mLastModeSwitchTimeStamp
     * previous CompModeSwitch was DEVICE_2_CLIENT: check immediately
     */
    if ((mDynamicReCompMode != DEVICE_2_CLIENT) && (TimeStampDiff < (VSYNC_INTERVAL * 15))) {
        return 0;
    }
    mLastModeSwitchTimeStamp = mLastUpdateTimeStamp;
    if ((mUpdateEventCnt != 1) && // This is not called by hwc_update_stat_thread
            (mDynamicReCompMode == DEVICE_2_CLIENT) && (mUpdateCallCnt == 1)) {
        uint32_t t_maxFps = 0, t_layerFps = 0;
        for (uint32_t i = 0; i < mLayers.size(); i++) {
            t_layerFps = mLayers[i]->checkFps();
            if (t_maxFps < t_layerFps)
                t_maxFps = t_layerFps;
        }
        DISPLAY_LOGD(eDebugDynamicRecomp, "[DYNAMIC_RECOMP] first frame after DEVICE_2_CLIENT, max fps(%d), mUpdateEvent(%" PRIx64 ")", t_maxFps, mUpdateEventCnt);

        updateFps = HWC_FPS_TH;
    } else {
        for (uint32_t i = 0; i < mLayers.size(); i++) {
            layerFps = mLayers[i]->checkFps();
            if (maxFps < layerFps)
                maxFps = layerFps;
        }
        updateFps = maxFps;
    }
    mUpdateCallCnt = 0;

    /*
     * FPS estimation.
     * If FPS is lower than HWC_FPS_TH, try to switch the mode to GLES
     */
    if (updateFps < HWC_FPS_TH) {
        if (mDynamicReCompMode != DEVICE_2_CLIENT) {
            mDynamicReCompMode = DEVICE_2_CLIENT;
            DISPLAY_LOGD(eDebugDynamicRecomp, "[DYNAMIC_RECOMP] DEVICE_2_CLIENT by low FPS(%d)", updateFps);
            return DEVICE_2_CLIENT;
        } else {
            return 0;
        }
    } else {
        if (mDynamicReCompMode == DEVICE_2_CLIENT) {
            mDynamicReCompMode = CLIENT_2_DEVICE;
            DISPLAY_LOGD(eDebugDynamicRecomp, "[DYNAMIC_RECOMP] GLES_2_HWC by high FPS(%d)", updateFps);
            return CLIENT_2_DEVICE;
        } else {
            return 0;
        }
    }
#if 0
    this->setGeometryChanged();
#endif
    return 0;
}

/**
 * @return int
 */
int ExynosDisplay::handleDynamicReCompMode() {
    return 0;
}

/**
 * @return int
 */
int ExynosDisplay::handleLowFpsLayers() {
    return 0;
}

/**
 * @param changedBit
 * @return int
 */
int ExynosDisplay::setGeometryChanged(uint32_t __unused changedBit) {
    return 0;
}

int ExynosDisplay::handleStaticLayers(ExynosCompositionInfo& compositionInfo)
{
    if (compositionInfo.mType != COMPOSITION_CLIENT)
        return -EINVAL;

    if (compositionInfo.mEnableSkipStatic == false) {
        return NO_ERROR;
    }

    if (compositionInfo.mHasCompositionLayer == false) {
        return NO_ERROR;
    }
    if ((compositionInfo.mWindowIndex < 0) ||
        (compositionInfo.mWindowIndex >= (int32_t)NUM_HW_WINDOWS))
    {
        DISPLAY_LOGE("invalid mWindowIndex(%d)", compositionInfo.mWindowIndex);
        return -EINVAL;
    }

    /* Store configuration of client target configuration */
    if (compositionInfo.mSkipFlag == false) {
        memcpy(&compositionInfo.mLastConfig,
            &mWinConfigData->config[compositionInfo.mWindowIndex],
            sizeof(compositionInfo.mLastConfig));
        DISPLAY_LOGD(eDebugSkipStaicLayer, "config[%d] is stored",
                compositionInfo.mWindowIndex);
    } else {
        for (size_t i = (size_t)compositionInfo.mFirstIndex; i <= (size_t)compositionInfo.mLastIndex; i++) {
            if ((mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_CLIENT) &&
                (mLayers[i]->mAcquireFence >= 0))
                fence_close(mLayers[i]->mAcquireFence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_ALL);
            mLayers[i]->mAcquireFence = -1;
            mLayers[i]->mReleaseFence = -1;
        }

        if (compositionInfo.mTargetBuffer == NULL) {
            if (mWinConfigData->config[compositionInfo.mWindowIndex].fence_fd > 0)
                fence_close(mWinConfigData->config[compositionInfo.mWindowIndex].fence_fd, this,
                        FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_ALL);

            memcpy(&mWinConfigData->config[compositionInfo.mWindowIndex],
                    &compositionInfo.mLastConfig, sizeof(compositionInfo.mLastConfig));
            /* Assigned otfMPP for client target can be changed  */
            mWinConfigData->config[compositionInfo.mWindowIndex].idma_type = getDeconDMAType(compositionInfo.mOtfMPP);
            /* fence_fd was closed by DPU driver in the previous frame */
            mWinConfigData->config[compositionInfo.mWindowIndex].fence_fd = -1;
        } else {
            /* Check target buffer is same with previous frame */
            if ((mWinConfigData->config[compositionInfo.mWindowIndex].fd_idma[0] !=
                        compositionInfo.mLastConfig.fd_idma[0]) ||
                    (mWinConfigData->config[compositionInfo.mWindowIndex].fd_idma[1] !=
                     compositionInfo.mLastConfig.fd_idma[1]) ||
                    (mWinConfigData->config[compositionInfo.mWindowIndex].fd_idma[2] !=
                     compositionInfo.mLastConfig.fd_idma[2])) {
                DISPLAY_LOGE("Current config [%d][%d, %d, %d]",
                        compositionInfo.mWindowIndex,
                        mWinConfigData->config[compositionInfo.mWindowIndex].fd_idma[0],
                        mWinConfigData->config[compositionInfo.mWindowIndex].fd_idma[1],
                        mWinConfigData->config[compositionInfo.mWindowIndex].fd_idma[2]);
                DISPLAY_LOGE("=============================  dump last win configs  ===================================");
                struct decon_win_config *config = mLastWinConfigData->config;
                for (size_t i = 0; i <= NUM_HW_WINDOWS; i++) {
                    android::String8 result;
                    result.appendFormat("config[%zu]\n", i);
                    dumpConfig(result, config[i]);
                    DISPLAY_LOGE("%s", result.string());
                }
                DISPLAY_LOGE("compositionInfo.mLastConfig config [%d, %d, %d]",
                        compositionInfo.mLastConfig.fd_idma[0],
                        compositionInfo.mLastConfig.fd_idma[1],
                        compositionInfo.mLastConfig.fd_idma[2]);
                return -EINVAL;
            }
        }

        DISPLAY_LOGD(eDebugSkipStaicLayer, "skipStaticLayer config[%d]", compositionInfo.mWindowIndex);
        dumpConfig(mWinConfigData->config[compositionInfo.mWindowIndex]);
    }

    return NO_ERROR;
}

/**
 * @param compositionType
 * @return int
 */
int ExynosDisplay::skipStaticLayers(ExynosCompositionInfo& compositionInfo)
{
    compositionInfo.mSkipFlag = false;

    if (compositionInfo.mType != COMPOSITION_CLIENT)
        return -EINVAL;

    if ((exynosHWCControl.skipStaticLayers == 0) ||
        (compositionInfo.mEnableSkipStatic == false))
        return NO_ERROR;

#if 0
    /* TODO: check mGeometryChanged instead of mContentFlags */
    if ((mContentFlags & HWC_GEOMETRY_CHANGED) == 0) {
        DISPLAY_LOGD(eDebugSkipStaicLayer, "Geometry is changed");
        return NO_ERROR;
    }
#endif

    if ((compositionInfo.mHasCompositionLayer == false) ||
        (compositionInfo.mFirstIndex < 0) ||
        (compositionInfo.mLastIndex < 0) ||
        ((compositionInfo.mLastIndex - compositionInfo.mFirstIndex + 1) > NUM_SKIP_STATIC_LAYER)) {
        DISPLAY_LOGD(eDebugSkipStaicLayer, "mHasCompositionLayer(%d), mFirstIndex(%d), mLastIndex(%d)",
                compositionInfo.mHasCompositionLayer,
                compositionInfo.mFirstIndex, compositionInfo.mLastIndex);
        compositionInfo.mSkipStaticInitFlag = false;
        return NO_ERROR;
    }

    if (compositionInfo.mSkipStaticInitFlag) {
        if ((int)compositionInfo.mSkipSrcInfo.srcNum !=
            (compositionInfo.mLastIndex - compositionInfo.mFirstIndex + 1)) {
            DISPLAY_LOGD(eDebugSkipStaicLayer, "Client composition number is changed (%d -> %d)",
                    compositionInfo.mSkipSrcInfo.srcNum,
                    compositionInfo.mLastIndex - compositionInfo.mFirstIndex + 1);

            compositionInfo.mSkipStaticInitFlag = false;
            return NO_ERROR;
        }

        bool isChanged = false;
        for (size_t i = (size_t)compositionInfo.mFirstIndex; i <= (size_t)compositionInfo.mLastIndex; i++) {
            ExynosLayer *layer = mLayers[i];
            size_t index = i - compositionInfo.mFirstIndex;
            if ((layer->mLayerBuffer == NULL) ||
                (layer->mLayerFlag & HWC_SKIP_LAYER) ||
                (compositionInfo.mSkipSrcInfo.srcInfo[index].bufferHandle != layer->mLayerBuffer))
            {
                isChanged = true;
                DISPLAY_LOGD(eDebugSkipStaicLayer, "layer[%zu] handle is changed"\
                        " handle(%p -> %p), layerFlag(0x%8x)",
                        i, compositionInfo.mSkipSrcInfo.srcInfo[index].bufferHandle,
                        layer->mLayerBuffer, layer->mLayerFlag);
                break;
            } else if ((compositionInfo.mSkipSrcInfo.srcInfo[index].x != layer->mSrcImg.x) ||
                       (compositionInfo.mSkipSrcInfo.srcInfo[index].y != layer->mSrcImg.y) ||
                       (compositionInfo.mSkipSrcInfo.srcInfo[index].w != layer->mSrcImg.w) ||
                       (compositionInfo.mSkipSrcInfo.srcInfo[index].h != layer->mSrcImg.h) ||
                       (compositionInfo.mSkipSrcInfo.srcInfo[index].dataSpace != layer->mSrcImg.dataSpace) ||
                       (compositionInfo.mSkipSrcInfo.srcInfo[index].blending != layer->mSrcImg.blending) ||
                       (compositionInfo.mSkipSrcInfo.srcInfo[index].transform != layer->mSrcImg.transform) ||
                       (compositionInfo.mSkipSrcInfo.srcInfo[index].planeAlpha != layer->mSrcImg.planeAlpha))
            {
                isChanged = true;
                DISPLAY_LOGD(eDebugSkipStaicLayer, "layer[%zu] source info is changed, "\
                        "x(%d->%d), y(%d->%d), w(%d->%d), h(%d->%d), dataSpace(%d->%d), "\
                        "blending(%d->%d), transform(%d->%d), planeAlpha(%3.1f->%3.1f)", i,
                        compositionInfo.mSkipSrcInfo.srcInfo[index].x, layer->mSrcImg.x,
                        compositionInfo.mSkipSrcInfo.srcInfo[index].y, layer->mSrcImg.y,
                        compositionInfo.mSkipSrcInfo.srcInfo[index].w,  layer->mSrcImg.w,
                        compositionInfo.mSkipSrcInfo.srcInfo[index].h,  layer->mSrcImg.h,
                        compositionInfo.mSkipSrcInfo.srcInfo[index].dataSpace, layer->mSrcImg.dataSpace,
                        compositionInfo.mSkipSrcInfo.srcInfo[index].blending, layer->mSrcImg.blending,
                        compositionInfo.mSkipSrcInfo.srcInfo[index].transform, layer->mSrcImg.transform,
                        compositionInfo.mSkipSrcInfo.srcInfo[index].planeAlpha, layer->mSrcImg.planeAlpha);
                break;
            } else if ((compositionInfo.mSkipSrcInfo.dstInfo[index].x != layer->mDstImg.x) ||
                       (compositionInfo.mSkipSrcInfo.dstInfo[index].y != layer->mDstImg.y) ||
                       (compositionInfo.mSkipSrcInfo.dstInfo[index].w != layer->mDstImg.w) ||
                       (compositionInfo.mSkipSrcInfo.dstInfo[index].h != layer->mDstImg.h))
            {
                isChanged = true;
                DISPLAY_LOGD(eDebugSkipStaicLayer, "layer[%zu] dst info is changed, "\
                        "x(%d->%d), y(%d->%d), w(%d->%d), h(%d->%d)", i,
                        compositionInfo.mSkipSrcInfo.dstInfo[index].x, layer->mDstImg.x,
                        compositionInfo.mSkipSrcInfo.dstInfo[index].y, layer->mDstImg.y,
                        compositionInfo.mSkipSrcInfo.dstInfo[index].w, layer->mDstImg.w,
                        compositionInfo.mSkipSrcInfo.dstInfo[index].h, layer->mDstImg.h);
                break;
            }
        }

        if (isChanged == true) {
            compositionInfo.mSkipStaticInitFlag = false;
            return NO_ERROR;
        }

        compositionInfo.mSkipFlag = true;
        for (size_t i = (size_t)compositionInfo.mFirstIndex; i <= (size_t)compositionInfo.mLastIndex; i++) {
            ExynosLayer *layer = mLayers[i];
            if (layer->mValidateCompositionType == COMPOSITION_CLIENT) {
                layer->mOverlayInfo |= eSkipStaticLayer;
            } else {
                DISPLAY_LOGD(eDebugLayer, "[%zu] layer can't skip because of layer type(%d)",
                        i, layer->mValidateCompositionType);
                compositionInfo.mSkipStaticInitFlag = false;
                return -EINVAL;
            }
        }

        DISPLAY_LOGD(eDebugSkipStaicLayer, "SkipStaicLayer is enabled");
        return NO_ERROR;
    }

    compositionInfo.mSkipStaticInitFlag = true;
    memset(&compositionInfo.mSkipSrcInfo, 0, sizeof(compositionInfo.mSkipSrcInfo));

    for (size_t i = (size_t)compositionInfo.mFirstIndex; i <= (size_t)compositionInfo.mLastIndex; i++) {
        ExynosLayer *layer = mLayers[i];
        size_t index = i - compositionInfo.mFirstIndex;
        compositionInfo.mSkipSrcInfo.srcInfo[index] = layer->mSrcImg;
        compositionInfo.mSkipSrcInfo.dstInfo[index] = layer->mDstImg;
        DISPLAY_LOGD(eDebugSkipStaicLayer, "mSkipSrcInfo.srcInfo[%zu] is initialized, %p",
                index, layer->mSrcImg.bufferHandle);
    }
    compositionInfo.mSkipSrcInfo.srcNum = (compositionInfo.mLastIndex - compositionInfo.mFirstIndex + 1);
    return NO_ERROR;
}

/**
 * @return int
 */
int ExynosDisplay::doPostProcessing() {

    for (size_t i=0; i < mLayers.size(); i++) {
        /* Layer handle back-up */
        mLayers[i]->mLastLayerBuffer = mLayers[i]->mLayerBuffer;
    }

    return 0;
}

bool ExynosDisplay::validateExynosCompositionLayer()
{
    bool isValid = true;
    ExynosMPP *m2mMpp = mExynosCompositionInfo.mM2mMPP;

    int maxPriorityNum = 0;
    int sourceSize = (int)m2mMpp->mAssignedSources.size();
    if ((mExynosCompositionInfo.mFirstIndex >= 0) &&
        (mExynosCompositionInfo.mLastIndex >= 0)) {
        if (mUseDecon) {
            for (uint32_t i = (uint32_t)mExynosCompositionInfo.mFirstIndex + 1; i < (uint32_t)mExynosCompositionInfo.mLastIndex; i++) {
                ExynosLayer *layer = mLayers[i];
                if (layer->mOverlayPriority >= ePriorityHigh) {
                    maxPriorityNum++;
                }
            }
        }
        sourceSize = mExynosCompositionInfo.mLastIndex - mExynosCompositionInfo.mFirstIndex + 1 + maxPriorityNum;

        if (!mUseDecon && mClientCompositionInfo.mHasCompositionLayer)
            sourceSize++;
    }

    if (m2mMpp->mAssignedSources.size() == 0) {
        DISPLAY_LOGE("No source images");
        isValid = false;
    } else if ((mUseDecon && ((mExynosCompositionInfo.mFirstIndex < 0) ||
               (mExynosCompositionInfo.mLastIndex < 0))) ||
               (sourceSize != (int)m2mMpp->mAssignedSources.size())) {
        DISPLAY_LOGE("Invalid index (%d, %d), maxPriorityNum(%d), or size(%zu), sourceSize(%d)",
                mExynosCompositionInfo.mFirstIndex,
                mExynosCompositionInfo.mLastIndex,
                maxPriorityNum,
                m2mMpp->mAssignedSources.size(),
                sourceSize);
        isValid = false;
    } else {
        if (mHWC1LayerList == NULL) {
            DISPLAY_LOGE("mHWC1LayerList is not set");
            return -EINVAL;
        }
        for (size_t i = 0; i < m2mMpp->mAssignedSources.size(); i++) {
            bool found = false;
            private_handle_t *srcHandle = NULL;
            if (m2mMpp->mAssignedSources[i]->mSrcImg.bufferHandle != NULL)
                srcHandle = private_handle_t::dynamicCast(m2mMpp->mAssignedSources[i]->mSrcImg.bufferHandle);
            for (size_t j = 0; j <= mHWC1LayerList->numHwLayers; j++) {
                hwc_layer_1_t &layer = mHWC1LayerList->hwLayers[j];
                private_handle_t *handle = NULL;
                if (layer.handle != NULL)
                    handle = private_handle_t::dynamicCast(layer.handle);
                else
                    continue;

                if (srcHandle == handle) {
                    if (layer.acquireFenceFd != m2mMpp->mAssignedSources[i]->mSrcImg.acquireFenceFd) {
                        HWC_LOGE(this, "[%zu] layer acquire fence is not matched, mAssignedSource[%zu], [%d, %d]",
                                j, i, layer.acquireFenceFd, m2mMpp->mAssignedSources[i]->mSrcImg.acquireFenceFd);
                        String8 errString;
                        errString.appendFormat("[%zu] layer acquire fence is not matched, mAssignedSource[%zu], [%d, %d]\n",
                                j, i, layer.acquireFenceFd, m2mMpp->mAssignedSources[i]->mSrcImg.acquireFenceFd);
                        printDebugInfos(errString);
                        usleep(20000);
                    }
                    found = true;
                    break;
                }
            }

            if (found == false)
            {
                DISPLAY_LOGE("Exynos composition source[%zu] is not valid", i);
                isValid = false;
                break;
            }
        }
    }
    if (isValid == false) {
        for (int32_t i = mExynosCompositionInfo.mFirstIndex; i <= mExynosCompositionInfo.mLastIndex; i++) {
            /* break when only framebuffer target is assigned on ExynosCompositor */
            if (i == -1)
                break;

            if (mLayers[i]->mAcquireFence >= 0)
                fence_close(mLayers[i]->mAcquireFence, this,
                        FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_ALL);
            mLayers[i]->mAcquireFence = -1;
        }
        mExynosCompositionInfo.mM2mMPP->requestHWStateChange(MPP_HW_STATE_IDLE);
        mExynosCompositionInfo.initializeInfos(this);
    }
    return isValid;
}

/**
 * @return int
 */
int ExynosDisplay::doExynosComposition() {
    int ret = NO_ERROR;

    if (mExynosCompositionInfo.mHasCompositionLayer) {
        if (mExynosCompositionInfo.mM2mMPP == NULL) {
            DISPLAY_LOGE("mExynosCompositionInfo.mM2mMPP is NULL");
            return -EINVAL;
        }
        mExynosCompositionInfo.mM2mMPP->requestHWStateChange(MPP_HW_STATE_RUNNING);
        /* mAcquireFence is updated, Update image info */
        for (int32_t i = mExynosCompositionInfo.mFirstIndex; i <= mExynosCompositionInfo.mLastIndex; i++) {
            /* break when only framebuffer target is assigned on ExynosCompositor */
            if (i == -1)
                break;

            struct exynos_image srcImg, dstImg;
            mLayers[i]->setSrcExynosImage(&srcImg);
            dumpExynosImage(eDebugFence, srcImg);
            mLayers[i]->setDstExynosImage(&dstImg);
            mLayers[i]->setExynosImage(srcImg, dstImg);
            /* This should be closed by resource lib (libmpp or libacryl) */
            mLayers[i]->mAcquireFence = -1;
        }

        /* For debugging */
        if (validateExynosCompositionLayer() == false) {
            DISPLAY_LOGE("mExynosCompositionInfo is not valid");
            return -EINVAL;
        }

        if ((ret = mExynosCompositionInfo.mM2mMPP->doPostProcessing(mExynosCompositionInfo.mSrcImg,
                mExynosCompositionInfo.mDstImg)) != NO_ERROR) {
            DISPLAY_LOGE("exynosComposition doPostProcessing fail ret(%d)", ret);
            return ret;
        }

        exynos_image outImage;
        if ((ret = mExynosCompositionInfo.mM2mMPP->getDstImageInfo(&outImage)) != NO_ERROR) {
            DISPLAY_LOGE("exynosComposition getDstImageInfo fail ret(%d)", ret);
            return ret;
        }
        mExynosCompositionInfo.setTargetBuffer(this, outImage.bufferHandle,
                outImage.releaseFenceFd, HAL_DATASPACE_UNKNOWN);
        DISPLAY_LOGD(eDebugFence, "mExynosCompositionInfo acquireFencefd(%d)",
                mExynosCompositionInfo.mAcquireFence);

        if ((ret =  mExynosCompositionInfo.mM2mMPP->resetDstReleaseFence()) != NO_ERROR)
        {
            DISPLAY_LOGE("exynosComposition resetDstReleaseFence fail ret(%d)", ret);
            return ret;
        }
    }

    return ret;
}

enum decon_blending halBlendingToS3CBlending(int32_t blending)
{
    switch (blending) {
    case HWC_BLENDING_NONE:
        return DECON_BLENDING_NONE;
    case HWC_BLENDING_PREMULT:
        return DECON_BLENDING_PREMULT;
    case HWC_BLENDING_COVERAGE:
        return DECON_BLENDING_COVERAGE;

    default:
        return DECON_BLENDING_MAX;
    }
}

uint32_t halDataSpaceToDisplayParam(exynos_image srcImg, android_dataspace dataspace)
{
    uint32_t cscEQ = 0;
    uint32_t standard = (dataspace & HAL_DATASPACE_STANDARD_MASK);
    uint32_t range = (dataspace & HAL_DATASPACE_RANGE_MASK);

    switch(standard) {
    case HAL_DATASPACE_STANDARD_BT709:
        cscEQ = CSC_BT_709;
        break;
    case HAL_DATASPACE_STANDARD_BT601_625:
    case HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED:
    case HAL_DATASPACE_STANDARD_BT601_525:
    case HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED:
        cscEQ = CSC_BT_601;
        break;
    case HAL_DATASPACE_STANDARD_BT2020:
        cscEQ = CSC_BT_2020;
        break;
    case HAL_DATASPACE_STANDARD_DCI_P3:
        cscEQ = CSC_DCI_P3;
        break;
    case HAL_DATASPACE_STANDARD_UNSPECIFIED:
        if (isFormatRgb(srcImg.format)) {
            cscEQ = CSC_BT_709;
        } else {
            cscEQ = CSC_BT_601;
        }
        break;
    default:
        break;
    }

    switch(range) {
    case HAL_DATASPACE_RANGE_FULL:
        cscEQ |= (cscEQ | (CSC_RANGE_FULL << CSC_RANGE_SHIFT));
        break;
    case HAL_DATASPACE_RANGE_LIMITED:
        cscEQ |= (cscEQ | (CSC_RANGE_LIMITED << CSC_RANGE_SHIFT));
        break;
    case HAL_DATASPACE_RANGE_UNSPECIFIED:
        if (isFormatRgb(srcImg.format))
            cscEQ |= (cscEQ | (CSC_RANGE_FULL << CSC_RANGE_SHIFT));
        else
            cscEQ |= (cscEQ | (CSC_RANGE_LIMITED << CSC_RANGE_SHIFT));
        break;
    }
    return cscEQ;
}

decon_idma_type ExynosDisplay::getDeconDMAType(ExynosMPP *otfMPP)
{
    if (otfMPP == NULL)
        return MAX_DECON_DMA_TYPE;

    /* To do: should be modified for Exynos8895 */
    if (otfMPP->mPhysicalType == MPP_DPP_VG)
        return (decon_idma_type)((uint32_t)IDMA_VG0 + otfMPP->mPhysicalIndex);
    else if (otfMPP->mPhysicalType == MPP_DPP_VGF)
        return (decon_idma_type)((uint32_t)IDMA_VGF0 + otfMPP->mPhysicalIndex);
    else if (otfMPP->mPhysicalType == MPP_DPP_G) {
        switch (otfMPP->mPhysicalIndex) {
        case 0:
            return IDMA_G0;
        case 1:
            return IDMA_G1;
        default:
            return MAX_DECON_DMA_TYPE;
        }
    } else
        return MAX_DECON_DMA_TYPE;
}

int32_t ExynosDisplay::configureHandle(ExynosLayer &layer,  int fence_fd, decon_win_config &cfg)
{
//    ALOGD("HWC2 : %s : %d ", __func__, __LINE__);
    /* TODO : this is hardcoded */
    int32_t ret = NO_ERROR;
    private_handle_t *handle = NULL;
    int32_t blending = 0x0100;
    int32_t planeAlpha = 0;
    uint32_t x = 0, y = 0;
    uint32_t w = WIDTH(layer.mPreprocessedInfo.displayFrame);
    uint32_t h = HEIGHT(layer.mPreprocessedInfo.displayFrame);
    ExynosMPP* otfMPP = NULL;
    ExynosMPP* m2mMPP = NULL;

    blending = layer.mBlending;
    planeAlpha = layer.mPlaneAlpha;
    otfMPP = layer.mOtfMPP;
    m2mMPP = layer.mM2mMPP;

    cfg.compression = layer.mCompressed;
    if (layer.mCompressed) {
        cfg.dpp_parm.comp_src = DPP_COMP_SRC_GPU;
    }
    if (otfMPP == NULL) {
        HWC_LOGE(this, "%s:: otfMPP is NULL", __func__);
        return -EINVAL;
    }
    if (m2mMPP != NULL)
        handle = m2mMPP->mDstImgs[m2mMPP->mCurrentDstBuf].bufferHandle;
    else
        handle = layer.mLayerBuffer;

    if ((!layer.mIsDimLayer) && handle == NULL) {
        HWC_LOGE(this, "%s:: invalid handle", __func__);
        return -EINVAL;
    }

    if (layer.mPreprocessedInfo.displayFrame.left < 0) {
        unsigned int crop = -layer.mPreprocessedInfo.displayFrame.left;
        DISPLAY_LOGD(eDebugWinConfig, "layer off left side of screen; cropping %u pixels from left edge",
                crop);
        x = 0;
        w -= crop;
    } else {
        x = layer.mPreprocessedInfo.displayFrame.left;
    }

    if (layer.mPreprocessedInfo.displayFrame.right > (int)mXres) {
        unsigned int crop = layer.mPreprocessedInfo.displayFrame.right - this->mXres;
        DISPLAY_LOGD(eDebugWinConfig, "layer off right side of screen; cropping %u pixels from right edge",
                crop);
        w -= crop;
    }

    if (layer.mPreprocessedInfo.displayFrame.top < 0) {
        unsigned int crop = -layer.mPreprocessedInfo.displayFrame.top;
        DISPLAY_LOGD(eDebugWinConfig, "layer off top side of screen; cropping %u pixels from top edge",
                crop);
        y = 0;
        h -= crop;
    } else {
        y = layer.mPreprocessedInfo.displayFrame.top;
    }

    if (layer.mPreprocessedInfo.displayFrame.bottom > (int)mYres) {
        int crop = layer.mPreprocessedInfo.displayFrame.bottom - this->mYres;
        DISPLAY_LOGD(eDebugWinConfig, "layer off bottom side of screen; cropping %u pixels from bottom edge",
                crop);
        h -= crop;
    }

    cfg.state = cfg.DECON_WIN_STATE_BUFFER;
    cfg.dst.x = x;
    cfg.dst.y = y;
    cfg.dst.w = w;
    cfg.dst.h = h;
    cfg.dst.f_w = mXres;
    cfg.dst.f_h = mYres;

    cfg.plane_alpha = 255;
    if ((planeAlpha >= 0) && (planeAlpha < 255)) {
        cfg.plane_alpha = planeAlpha;
    }
    cfg.blending = halBlendingToS3CBlending(blending);
    cfg.idma_type = getDeconDMAType(otfMPP);

    if (layer.mIsDimLayer && handle == NULL) {
        cfg.state = cfg.DECON_WIN_STATE_COLOR;
        cfg.color = 0x0;
        if (!((planeAlpha >= 0) && (planeAlpha <= 255)))
            cfg.plane_alpha = 0;
        DISPLAY_LOGD(eDebugWinConfig, "HWC2: DIM layer is enabled, alpha : %d", cfg.plane_alpha);
        return ret;
    }

    cfg.format = halFormatToS3CFormat(handle->format);

    cfg.fd_idma[0] = handle->fd;
    cfg.fd_idma[1] = handle->fd1;
    cfg.fd_idma[2] = handle->fd2;
    cfg.protection = (getDrmMode(handle) == SECURE_DRM) ? 1 : 0;

    exynos_image src_img = layer.mSrcImg;

    if (cfg.fence_fd >= 0) {
        String8 errString;
        errString.appendFormat("%s:: fence_fd is already set, window_index(%d)",
                __func__, layer.mWindowIndex);
        printDebugInfos(errString);
        fence_close(cfg.fence_fd, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP);
        cfg.fence_fd = -1;
    }

    if (m2mMPP != NULL)
    {
        DISPLAY_LOGD(eDebugWinConfig, "\tUse m2mMPP, bufIndex: %d", m2mMPP->mCurrentDstBuf);
        dumpExynosImage(eDebugWinConfig, m2mMPP->mAssignedSources[0]->mMidImg);
        exynos_image mpp_dst_img;
        if (m2mMPP->getDstImageInfo(&mpp_dst_img) == NO_ERROR) {
            dumpExynosImage(eDebugWinConfig, mpp_dst_img);
            cfg.src.f_w = mpp_dst_img.fullWidth;
            cfg.src.f_h = mpp_dst_img.fullHeight;
            cfg.src.x = mpp_dst_img.x;
            cfg.src.y = mpp_dst_img.y;
            cfg.src.w = mpp_dst_img.w;
            cfg.src.h = mpp_dst_img.h;
            cfg.format = halFormatToS3CFormat(mpp_dst_img.format);
            cfg.fence_fd =
                hwcCheckFenceDebug(this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP, mpp_dst_img.releaseFenceFd);
            if (m2mMPP->mPhysicalType == MPP_MSC) {
                setFenceName(cfg.fence_fd, FENCE_DPP_SRC_MSC);
            } else if (m2mMPP->mPhysicalType == MPP_G2D) {
                setFenceName(cfg.fence_fd, FENCE_DPP_SRC_G2D);
            } else {
                setFenceName(cfg.fence_fd, FENCE_DPP_SRC_MPP);
            }
            m2mMPP->resetDstReleaseFence();
        } else {
            HWC_LOGE(this, "%s:: Failed to get dst info of m2mMPP", __func__);
        }
        cfg.dpp_parm.eq_mode = (dpp_csc_eq)halDataSpaceToDisplayParam(layer.mMidImg, layer.mMidImg.dataSpace);
        src_img = layer.mMidImg;
    } else {
        cfg.src.f_w = src_img.fullWidth;
        cfg.src.f_h = src_img.fullHeight;
        cfg.src.x = layer.mPreprocessedInfo.sourceCrop.left;
        cfg.src.y = layer.mPreprocessedInfo.sourceCrop.top;
        cfg.src.w = WIDTH(layer.mPreprocessedInfo.sourceCrop) - (cfg.src.x - (uint32_t)layer.mPreprocessedInfo.sourceCrop.left);
        cfg.src.h = HEIGHT(layer.mPreprocessedInfo.sourceCrop) - (cfg.src.y - (uint32_t)layer.mPreprocessedInfo.sourceCrop.top);
        cfg.fence_fd = hwcCheckFenceDebug(this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP, fence_fd);
        setFenceName(cfg.fence_fd, FENCE_DPP_SRC_LAYER);

        if (isFormatRgb(handle->format))
            cfg.dpp_parm.eq_mode = (dpp_csc_eq)0;
        else
            cfg.dpp_parm.eq_mode = (dpp_csc_eq)halDataSpaceToDisplayParam(src_img, src_img.dataSpace);
    }

    /* Adjust configuration */
    uint32_t srcMaxWidth, srcMaxHeight, srcWidthAlign, srcHeightAlign = 0;
    uint32_t srcXAlign, srcYAlign, srcMaxCropWidth, srcMaxCropHeight, srcCropWidthAlign, srcCropHeightAlign = 0;
    srcMaxWidth = otfMPP->getSrcMaxWidth(src_img);
    srcMaxHeight = otfMPP->getSrcMaxHeight(src_img);
    srcWidthAlign = otfMPP->getSrcWidthAlign(src_img);
    srcHeightAlign = otfMPP->getSrcHeightAlign(src_img);
    srcXAlign = otfMPP->getSrcXOffsetAlign(src_img);
    srcYAlign = otfMPP->getSrcYOffsetAlign(src_img);
    srcMaxCropWidth = otfMPP->getSrcMaxCropWidth(src_img);
    srcMaxCropHeight = otfMPP->getSrcMaxCropHeight(src_img);
    srcCropWidthAlign = otfMPP->getSrcCropWidthAlign(src_img);
    srcCropHeightAlign = otfMPP->getSrcCropHeightAlign(src_img);

    if (cfg.src.x < 0)
        cfg.src.x = 0;
    if (cfg.src.y < 0)
        cfg.src.y = 0;

    if (otfMPP != NULL) {
        if (cfg.src.f_w > srcMaxWidth)
            cfg.src.f_w = srcMaxWidth;
        if (cfg.src.f_h > srcMaxHeight)
            cfg.src.f_h = srcMaxHeight;
        cfg.src.f_w = pixel_align_down((unsigned int)cfg.src.f_w, srcWidthAlign);
        cfg.src.f_h = pixel_align_down((unsigned int)cfg.src.f_h, srcHeightAlign);

        cfg.src.x = pixel_align(cfg.src.x, srcXAlign);
        cfg.src.y = pixel_align(cfg.src.y, srcYAlign);
    }

    if (cfg.src.x + cfg.src.w > cfg.src.f_w)
        cfg.src.w = cfg.src.f_w - cfg.src.x;
    if (cfg.src.y + cfg.src.h > cfg.src.f_h)
        cfg.src.h = cfg.src.f_h - cfg.src.y;

    if (otfMPP != NULL) {
        if (cfg.src.w > srcMaxCropWidth)
            cfg.src.w = srcMaxCropWidth;
        if (cfg.src.h > srcMaxCropHeight)
            cfg.src.h = srcMaxCropHeight;
        cfg.src.w = pixel_align_down(cfg.src.w, srcCropWidthAlign);
        cfg.src.h = pixel_align_down(cfg.src.h, srcCropHeightAlign);
    }

    if ((layer.mLayerFlag & HWC_SET_OPAQUE) &&
        (cfg.format == DECON_PIXEL_FORMAT_RGBA_8888))
        cfg.format = DECON_PIXEL_FORMAT_RGBX_8888;

    uint64_t bufSize = handle->size * formatToBpp(handle->format);
    uint64_t srcSize = cfg.src.f_w * cfg.src.f_h * DeconFormatToBpp(cfg.format);

    if (bufSize < srcSize) {
        DISPLAY_LOGE("%s:: buffer size is smaller than source size, buf(size: %d, format: %d), src(w: %d, h: %d, format: %d)",
                __func__, handle->size, handle->format, cfg.src.f_w, cfg.src.f_h, cfg.format);
        return -EINVAL;
    }

    return ret;
}


int32_t ExynosDisplay::configureOverlay(ExynosLayer *layer, decon_win_config &cfg)
{
    int32_t ret = NO_ERROR;
    if(layer != NULL) {
        if (layer->mExynosCompositionType == HWC2_COMPOSITION_SOLID_COLOR) {
            hwc_color_t color = layer->mColor;
            cfg.state = cfg.DECON_WIN_STATE_COLOR;
            cfg.color = (color.r << 16) | (color.g << 8) | color.b;
            cfg.dst.x = 0;
            cfg.dst.y = 0;
            cfg.dst.w = this->mXres;
            cfg.dst.h = this->mYres;
            return ret;
        }

        if ((ret = configureHandle(*layer, layer->mAcquireFence, cfg)) != NO_ERROR)
            return ret;

        /* This will be closed by setReleaseFences() using config.fence_fd */
        layer->mAcquireFence = -1;
    }
    return ret;
}
int32_t ExynosDisplay::configureOverlay(ExynosCompositionInfo &compositionInfo)
{
    struct decon_win_config *config = mWinConfigData->config;
    int32_t windowIndex = compositionInfo.mWindowIndex;
    private_handle_t *handle = compositionInfo.mTargetBuffer;

    if ((windowIndex < 0) || (handle == NULL))
    {
        /* config will be set by handleStaticLayers */
        if ((handle == NULL) && compositionInfo.mSkipFlag)
            return NO_ERROR;

        HWC_LOGE(this, "%s:: ExynosCompositionInfo(%d) has invalid data, "
                "windowIndex(%d), handle(%p)",
                __func__, compositionInfo.mType, windowIndex, handle);
        return -EINVAL;
    }

    config[windowIndex].fd_idma[0] = handle->fd;
    config[windowIndex].fd_idma[1] = handle->fd1;
    config[windowIndex].fd_idma[2] = handle->fd2;
    config[windowIndex].protection = (getDrmMode(handle) == SECURE_DRM) ? 1 : 0;
    config[windowIndex].state = config[windowIndex].DECON_WIN_STATE_BUFFER;
    config[windowIndex].idma_type = getDeconDMAType(compositionInfo.mOtfMPP);
    config[windowIndex].dst.f_w = mXres;
    config[windowIndex].dst.f_h = mYres;
    config[windowIndex].format = halFormatToS3CFormat(handle->format);
    config[windowIndex].src.f_w = handle->stride;
    config[windowIndex].src.f_h = handle->vstride;
    if (compositionInfo.mCompressed) {
        if (compositionInfo.mType == COMPOSITION_EXYNOS)
            config[windowIndex].dpp_parm.comp_src = DPP_COMP_SRC_G2D;
        else if (compositionInfo.mType == COMPOSITION_CLIENT)
            config[windowIndex].dpp_parm.comp_src = DPP_COMP_SRC_GPU;
        else
            HWC_LOGE(this, "unknown composition type: %d", compositionInfo.mType);
    }

    if (mEnableFBCrop) {
        hwc_rect merged_rect, src_rect;
        merged_rect.left = mXres;
        merged_rect.top = mYres;
        merged_rect.right = 0;
        merged_rect.bottom = 0;

        for (size_t i = 0; i < mLayers.size(); i++) {
            ExynosLayer *layer = mLayers[i];
            if (layer->mCompositionType == HWC2_COMPOSITION_CLIENT) {
                DISPLAY_LOGD(eDebugWinConfig, "FB crop (before): l : %d, t : %d, r : %d, b : %d",
                        layer->mDisplayFrame.left,
                        layer->mDisplayFrame.top,
                        layer->mDisplayFrame.right,
                        layer->mDisplayFrame.bottom);
                src_rect.left = layer->mDisplayFrame.left;
                src_rect.top = layer->mDisplayFrame.top;
                src_rect.right = layer->mDisplayFrame.right;
                src_rect.bottom = layer->mDisplayFrame.bottom;
                merged_rect = expand(merged_rect, src_rect);
            }
        }

        config[windowIndex].src.x = merged_rect.left;
        config[windowIndex].src.y = merged_rect.top;
        config[windowIndex].src.w = merged_rect.right - merged_rect.left;
        config[windowIndex].src.h = merged_rect.bottom - merged_rect.top;
        config[windowIndex].dst.x = merged_rect.left;
        config[windowIndex].dst.y = merged_rect.top;
        config[windowIndex].dst.w = merged_rect.right - merged_rect.left;
        config[windowIndex].dst.h = merged_rect.bottom - merged_rect.top;

        DISPLAY_LOGD(eDebugWinConfig, "FB crop (config): l : %d, r : %d, t : %d, b : %d",
                config[windowIndex].dst.x, config[windowIndex].dst.y,
                config[windowIndex].dst.w, config[windowIndex].dst.h);
    }
    else {
        config[windowIndex].src.x = 0;
        config[windowIndex].src.y = 0;
        config[windowIndex].src.w = mXres;
        config[windowIndex].src.h = mYres;
        config[windowIndex].dst.x = 0;
        config[windowIndex].dst.y = 0;
        config[windowIndex].dst.w = mXres;
        config[windowIndex].dst.h = mYres;
        config[windowIndex].compression = compositionInfo.mCompressed;
    }

    /* TODO: blending value should be checked */
    config[windowIndex].blending = DECON_BLENDING_PREMULT;

    config[windowIndex].fence_fd =
        hwcCheckFenceDebug(this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP, compositionInfo.mAcquireFence);
    config[windowIndex].plane_alpha = 255;

    /* This will be closed by setReleaseFences() using config.fence_fd */
    compositionInfo.mAcquireFence = -1;
    DISPLAY_LOGD(eDebugSkipStaicLayer, "Configure composition target, config[%d]!!!!", windowIndex);
    dumpConfig(config[windowIndex]);

    uint64_t bufSize = handle->size * formatToBpp(handle->format);
    uint64_t srcSize = config[windowIndex].src.f_w * config[windowIndex].src.f_h * DeconFormatToBpp(config[windowIndex].format);
    if (bufSize < srcSize) {
        DISPLAY_LOGE("%s:: buffer size is smaller than source size, buf(size: %d, format: %d), src(w: %d, h: %d, format: %d)",
                __func__, handle->size, handle->format, config[windowIndex].src.f_w, config[windowIndex].src.f_h, config[windowIndex].format);
        return -EINVAL;
    }

    return NO_ERROR;
}

/**
 * @return int
 */
int ExynosDisplay::setWinConfigData() {
    int ret = NO_ERROR;
    struct decon_win_config *config = mWinConfigData->config;
    memset(config, 0, sizeof(mWinConfigData->config));
    mWinConfigData->fence = -1;

    /* init */
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        config[i].fence_fd = -1;
        config[i].protection = 0;
    }

    if (mClientCompositionInfo.mHasCompositionLayer) {
        if ((ret = configureOverlay(mClientCompositionInfo)) != NO_ERROR)
            return ret;
    }
    if (mExynosCompositionInfo.mHasCompositionLayer) {
        if ((ret = configureOverlay(mExynosCompositionInfo)) != NO_ERROR) {
            /* TEST */
            //return ret;
            HWC_LOGE(this, "configureOverlay(ExynosCompositionInfo) is failed");
        }
    }

    /* TODO loop for number of layers */
    for (size_t i = 0; i < mLayers.size(); i++) {
        if ((mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_EXYNOS) ||
                (mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_CLIENT))
            continue;
        int32_t windowIndex =  mLayers[i]->mWindowIndex; 
        DISPLAY_LOGD(eDebugWinConfig, "%zu layer, config[%d]", i, windowIndex);
        if ((ret = configureOverlay(mLayers[i], config[windowIndex])) != NO_ERROR)
            return ret;
    }

    return 0;
}

void ExynosDisplay::printDebugInfos(String8 &reason)
{
    bool writeFile = true;
    FILE *pFile = NULL;
    struct timeval tv;
    struct tm* localTime;
    gettimeofday(&tv, NULL);
    localTime = (struct tm*)localtime((time_t*)&tv.tv_sec);
    reason.appendFormat("errFrameNumber: %" PRId64 " time:%02d-%02d %02d:%02d:%02d.%03lu(%lu)\n",
            mErrorFrameCount,
            localTime->tm_mon+1, localTime->tm_mday,
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, tv.tv_usec/1000,
            ((tv.tv_sec * 1000) + (tv.tv_usec / 1000)));
    ALOGD("%s", reason.string());

    if (mErrorFrameCount >= HWC_PRINT_FRAME_NUM)
        writeFile = false;
    else {
        char filePath[128];
        sprintf(filePath, "%s/%s_hwc_debug%d.dump", ERROR_LOG_PATH0, mDisplayName.string(), (int)mErrorFrameCount);
        pFile = fopen(filePath, "wb");
        if (pFile == NULL) {
            ALOGE("Fail to open file %s, error: %s", filePath, strerror(errno));
            sprintf(filePath, "%s/%s_hwc_debug%d.dump", ERROR_LOG_PATH1, mDisplayName.string(), (int)mErrorFrameCount);
            pFile = fopen(filePath, "wb");
        }
        if (pFile == NULL) {
            ALOGE("Fail to open file %s, error: %s", filePath, strerror(errno));
        } else {
            ALOGI("%s was created", filePath);
            fwrite(reason.string(), 1, reason.size(), pFile);
        }
    }
    mErrorFrameCount++;

    android::String8 result;
    if (mHWC1LayerList != NULL) {
        result.appendFormat("=======================  dump hwc layers (%zu) ================================\n",
                mHWC1LayerList->numHwLayers);
        for (size_t i = 0; i < mHWC1LayerList->numHwLayers; i++)
        {
            hwc_layer_1_t &hwLayer = mHWC1LayerList->hwLayers[i];
            result.appendFormat("[%zu] layer\n", i);
            dumpLayer(result, hwLayer);
        }
    }
    ALOGD("%s", result.string());
    if (pFile != NULL) {
        fwrite(result.string(), 1, result.size(), pFile);
    }
    result.clear();

    result.appendFormat("=======================  dump composition infos  ================================\n");
    ExynosCompositionInfo clientCompInfo = mClientCompositionInfo;
    ExynosCompositionInfo exynosCompInfo = mExynosCompositionInfo;
    clientCompInfo.dump(result);
    exynosCompInfo.dump(result);
    ALOGD("%s", result.string());
    if (pFile != NULL) {
        fwrite(result.string(), 1, result.size(), pFile);
    }
    result.clear();

    result.appendFormat("=======================  dump exynos layers (%zu)  ================================\n",
            mLayers.size());
    ALOGD("%s", result.string());
    if (pFile != NULL) {
        fwrite(result.string(), 1, result.size(), pFile);
    }
    result.clear();
    for (uint32_t i = 0; i < mLayers.size(); i++) {
        ExynosLayer *layer = mLayers[i];
        layer->printLayer();
        if (pFile != NULL) {
            layer->dump(result);
            fwrite(result.string(), 1, result.size(), pFile);
            result.clear();
        }
    }

    if (mWinConfigData != NULL) {
        result.appendFormat("=============================  dump win configs  ===================================\n");
        ALOGD("%s", result.string());
        if (pFile != NULL) {
            fwrite(result.string(), 1, result.size(), pFile);
        }
        result.clear();
        struct decon_win_config *config = mWinConfigData->config;
        for (size_t i = 0; i <= NUM_HW_WINDOWS; i++) {
            ALOGD("config[%zu]", i);
            printConfig(config[i]);
            if (pFile != NULL) {
                result.appendFormat("config[%zu]\n", i);
                dumpConfig(result, config[i]);
                fwrite(result.string(), 1, result.size(), pFile);
                result.clear();
            }
        }
    }
    if (pFile != NULL) {
        fclose(pFile);
    }
}

int32_t ExynosDisplay::validateWinConfigData()
{
    struct decon_win_config *config = mWinConfigData->config;
    bool flagValidConfig = true;
    for (size_t i = 0; i < MAX_DECON_WIN; i++) {
        if (config[i].state == config[i].DECON_WIN_STATE_BUFFER) {
            bool configInvalid = false;
            /* multiple dma mapping */
            for (size_t j = (i+1); j < MAX_DECON_WIN; j++) {
                if ((config[i].state == config[i].DECON_WIN_STATE_BUFFER) &&
                    (config[j].state == config[j].DECON_WIN_STATE_BUFFER)) {
                    if (config[i].idma_type == config[j].idma_type) {
                        DISPLAY_LOGE("WIN_CONFIG error: duplicated dma(%d) between win%zu, win%zu",
                                config[i].idma_type, i, j);
                        config[j].state = config[j].DECON_WIN_STATE_DISABLED;
                        flagValidConfig = false;
                        continue;
                    }
                }
            }
            if ((config[i].src.x < 0) || (config[i].src.y < 0)||
                (config[i].dst.x < 0) || (config[i].dst.y < 0)||
                (config[i].src.w <= 0) || (config[i].src.h <= 0)||
                (config[i].dst.w <= 0) || (config[i].dst.h <= 0)||
                (config[i].dst.x + config[i].dst.w > (uint32_t)mXres) ||
                (config[i].dst.y + config[i].dst.h > (uint32_t)mYres)) {
                DISPLAY_LOGE("WIN_CONFIG error: invalid pos or size win%zu", i);
                configInvalid = true;
            }

            if (i >= NUM_HW_WINDOWS) {
                DISPLAY_LOGE("WIN_CONFIG error: invalid window number win%zu", i);
                configInvalid = true;
            }

            if ((config[i].idma_type >= MAX_DECON_DMA_TYPE) ||
                (config[i].format >= DECON_PIXEL_FORMAT_MAX) ||
                (config[i].blending >= DECON_BLENDING_MAX)) {
                DISPLAY_LOGE("WIN_CONFIG error: invalid configuration, dma_type(%d) "
                        "format(%d), blending(%d)", config[i].idma_type,
                        config[i].format, config[i].blending);
                configInvalid = true;
            }

            if ((config[i].src.w > config[i].src.f_w) ||
                    (config[i].src.h > config[i].src.f_h)) {
                DISPLAY_LOGE("WIN_CONFIG error: invalid size %zu, %d, %d, %d, %d", i,
                        config[i].src.w, config[i].src.f_w, config[i].src.h, config[i].src.f_h);
                configInvalid = true;
            }

            if ((config[i].src.w != config[i].dst.w) ||
                (config[i].src.h != config[i].dst.h)) {
                if ((config[i].idma_type == IDMA_G0) ||
                    (config[i].idma_type == IDMA_G1) ||
                    (config[i].idma_type == IDMA_VG0) ||
                    (config[i].idma_type == IDMA_VG1)) {
                    DISPLAY_LOGE("WIN_CONFIG error: invalid assign id : %zu,  s_w : %d, d_w : %d, s_h : %d, d_h : %d, type : %d", i,
                            config[i].src.w, config[i].dst.w, config[i].src.h, config[i].dst.h,
                            config[i].idma_type);
                    configInvalid = true;
                }
            }

            /* Source alignment check */
            ExynosMPP* exynosMPP = getExynosMPPForDma(config[i].idma_type);
            if (exynosMPP == NULL) {
                DISPLAY_LOGE("WIN_CONFIG error: %zu invalid idma_type(%d)", i, config[i].idma_type);
                configInvalid = true;
            } else {
                uint32_t restrictionIdx = getRestrictionIndex(config[i].format);
                uint32_t srcXAlign = exynosMPP->getSrcXOffsetAlign(restrictionIdx);
                uint32_t srcYAlign = exynosMPP->getSrcYOffsetAlign(restrictionIdx);
                uint32_t srcWidthAlign = exynosMPP->getSrcCropWidthAlign(restrictionIdx);
                uint32_t srcHeightAlign = exynosMPP->getSrcCropHeightAlign(restrictionIdx);
                if ((config[i].src.x % srcXAlign != 0) ||
                    (config[i].src.y % srcYAlign != 0) ||
                    (config[i].src.w % srcWidthAlign != 0) ||
                    (config[i].src.h % srcHeightAlign != 0))
                {
                    DISPLAY_LOGE("WIN_CONFIG error: invalid src alignment : %zu, "\
                            "idma: %d, mppType:%d, format(%d), s_x: %d(%d), s_y: %d(%d), s_w : %d(%d), s_h : %d(%d)", i,
                            config[i].idma_type, exynosMPP->mLogicalType, config[i].format, config[i].src.x, srcXAlign,
                            config[i].src.y, srcYAlign, config[i].src.w, srcWidthAlign, config[i].src.h, srcHeightAlign);
                    configInvalid = true;
                }
            }

            if (configInvalid) {
                config[i].state = config[i].DECON_WIN_STATE_DISABLED;
                flagValidConfig = false;
            }
        }
    }

    if (flagValidConfig)
        return NO_ERROR;
    else
        return -EINVAL;
}

/**
 * @return int
 */
int ExynosDisplay::setDisplayWinConfigData() {
    return 0;
}

bool ExynosDisplay::checkConfigChanged(struct decon_win_config_data &lastConfigData, struct decon_win_config_data &newConfigData)
{
    if (exynosHWCControl.skipWinConfig == 0)
        return true;

    /* HWC doesn't skip WIN_CONFIG if other display is connected */
    if (((mDevice->checkConnection(HWC_DISPLAY_EXTERNAL) == 1) ||
         (mDevice->checkConnection(HWC_DISPLAY_VIRTUAL) == 1)) &&
        (mDisplayId == HWC_DISPLAY_PRIMARY))
        return true;

    for (size_t i = 0; i <= MAX_DECON_WIN; i++) {
        if ((lastConfigData.config[i].state != newConfigData.config[i].state) ||
                (lastConfigData.config[i].fd_idma[0] != newConfigData.config[i].fd_idma[0]) ||
                (lastConfigData.config[i].fd_idma[1] != newConfigData.config[i].fd_idma[1]) ||
                (lastConfigData.config[i].fd_idma[2] != newConfigData.config[i].fd_idma[2]) ||
                (lastConfigData.config[i].dst.x != newConfigData.config[i].dst.x) ||
                (lastConfigData.config[i].dst.y != newConfigData.config[i].dst.y) ||
                (lastConfigData.config[i].dst.w != newConfigData.config[i].dst.w) ||
                (lastConfigData.config[i].dst.h != newConfigData.config[i].dst.h) ||
                (lastConfigData.config[i].src.x != newConfigData.config[i].src.x) ||
                (lastConfigData.config[i].src.y != newConfigData.config[i].src.y) ||
                (lastConfigData.config[i].src.w != newConfigData.config[i].src.w) ||
                (lastConfigData.config[i].src.h != newConfigData.config[i].src.h) ||
                (lastConfigData.config[i].format != newConfigData.config[i].format) ||
                (lastConfigData.config[i].blending != newConfigData.config[i].blending) ||
                (lastConfigData.config[i].plane_alpha != newConfigData.config[i].plane_alpha))
            return true;
    }

    /* To cover buffer payload changed case */
    for (size_t i = 0; i < mLayers.size(); i++) {
        if (mLayers[i]->mLastLayerBuffer != mLayers[i]->mLayerBuffer)
            return true;
    }

    return false;
}

int ExynosDisplay::checkConfigDstChanged(struct decon_win_config_data &lastConfigData, struct decon_win_config_data &newConfigData, uint32_t index)
{
    if ((lastConfigData.config[index].state != newConfigData.config[index].state) ||
        (lastConfigData.config[index].fd_idma[0] != newConfigData.config[index].fd_idma[0]) ||
        (lastConfigData.config[index].fd_idma[1] != newConfigData.config[index].fd_idma[1]) ||
        (lastConfigData.config[index].fd_idma[2] != newConfigData.config[index].fd_idma[2]) ||
        (lastConfigData.config[index].format != newConfigData.config[index].format) ||
        (lastConfigData.config[index].blending != newConfigData.config[index].blending) ||
        (lastConfigData.config[index].plane_alpha != newConfigData.config[index].plane_alpha)) {
        DISPLAY_LOGD(eDebugWindowUpdate, "damage region is skip, but other configuration except dst was changed");
        DISPLAY_LOGD(eDebugWindowUpdate, "\tstate[%d, %d], fd[%d, %d], format[0x%8x, 0x%8x], blending[%d, %d], plane_alpha[%d, %d]",
                lastConfigData.config[index].state, newConfigData.config[index].state,
                lastConfigData.config[index].fd_idma[0], newConfigData.config[index].fd_idma[0],
                lastConfigData.config[index].format, newConfigData.config[index].format,
                lastConfigData.config[index].blending, newConfigData.config[index].blending,
                lastConfigData.config[index].plane_alpha, newConfigData.config[index].plane_alpha);
        return -1;
    }
    if ((lastConfigData.config[index].dst.x != newConfigData.config[index].dst.x) ||
        (lastConfigData.config[index].dst.y != newConfigData.config[index].dst.y) ||
        (lastConfigData.config[index].dst.w != newConfigData.config[index].dst.w) ||
        (lastConfigData.config[index].dst.h != newConfigData.config[index].dst.h) ||
        (lastConfigData.config[index].src.x != newConfigData.config[index].src.x) ||
        (lastConfigData.config[index].src.y != newConfigData.config[index].src.y) ||
        (lastConfigData.config[index].src.w != newConfigData.config[index].src.w) ||
        (lastConfigData.config[index].src.h != newConfigData.config[index].src.h))
        return 1;

    else
        return 0;
}

/**
 * @return int
 */
int ExynosDisplay::deliverWinConfigData() {

#if 0
    skipWinConfig();
    if(skipWinConfig() == false) { }
#endif
    String8 errString;
    int ret = NO_ERROR;
    struct decon_win_config *config = mWinConfigData->config;

    ret = validateWinConfigData();
    if (ret != NO_ERROR) {
        errString.appendFormat("Invalid WIN_CONFIG\n");
        goto err;
    }

    for (size_t i = 0; i <= NUM_HW_WINDOWS; i++) {
        if (i == DECON_WIN_UPDATE_IDX) {
            DISPLAY_LOGD(eDebugWinConfig|eDebugSkipStaicLayer, "window update config[%zu]", i);
        } else {
            DISPLAY_LOGD(eDebugWinConfig|eDebugSkipStaicLayer, "deliver config[%zu]", i);
        }
        dumpConfig(config[i]);
    }

    if (checkConfigChanged(*mWinConfigData, *mLastWinConfigData) == false) {
        DISPLAY_LOGD(eDebugWinConfig, "Winconfig : same");
#ifndef DISABLE_FENCE
        if (mRetireFence > 0) {
            mWinConfigData->fence =
                hwcCheckFenceDebug(this, FENCE_TYPE_RETIRE, FENCE_IP_DPP,
                        hwc_dup(mRetireFence, this, FENCE_TYPE_RETIRE, FENCE_IP_DPP));
        } else
            mWinConfigData->fence = -1;
#endif
        ret = 0;
    } else {
        for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
            setFenceInfo(mWinConfigData->config[i].fence_fd, this,
                    FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP, FENCE_TO);
        }

        if ((ret = ioctl(mDisplayFd, S3CFB_WIN_CONFIG, mWinConfigData)) < 0) {
            errString.appendFormat("ioctl S3CFB_WIN_CONFIG failed: %s ret(%d)\n", strerror(errno), ret);
            goto err;
        } else
            memcpy(mLastWinConfigData, mWinConfigData, sizeof(*mWinConfigData));

        for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
            setFenceInfo(mWinConfigData->config[i].fence_fd, this,
                    FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP, FENCE_FROM);
        }
        setFenceInfo(mWinConfigData->fence, this,
                FENCE_TYPE_RETIRE, FENCE_IP_DPP, FENCE_FROM);
    }

    return ret;
err:
    printDebugInfos(errString);
    usleep(20000);
    closeFences();
    if (mDisplayId != HWC_DISPLAY_VIRTUAL) {
        clearDisplay();
    }
    if (exynosHWCControl.forcePanic == 1)
        ioctl(mDisplayFd, S3CFB_FORCE_PANIC, 0);

    return ret;
}

/**
 * @return int
 */
int ExynosDisplay::setReleaseFences() {

    struct decon_win_config *config = mWinConfigData->config;
    int dup_release_fd = -1;
    String8 errString;

    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        if (config[i].fence_fd != -1)
            fence_close(config[i].fence_fd, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP);
        config[i].fence_fd = -1;
    }

    for (size_t i = 0; i < mLayers.size(); i++) {
        if ((mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_CLIENT) ||
            (mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_EXYNOS))
            continue;
        if (mLayers[i]->mOtfMPP != NULL) {
#ifdef DISABLE_FENCE
            mLayers[i]->mOtfMPP->setHWStateFence(-1);
#else
            if (mWinConfigData->fence >= 0) {
                dup_release_fd = hwcCheckFenceDebug(this, FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP,
                        hwc_dup(mWinConfigData->fence, this, FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP));
                if (dup_release_fd >= 0) {
                    setFenceName(dup_release_fd, FENCE_DPP_HW_STATE);
                    mLayers[i]->mOtfMPP->setHWStateFence(dup_release_fd);
                } else {
                    DISPLAY_LOGE("fail to dup, ret(%d, %s)", errno, strerror(errno));
                    mLayers[i]->mOtfMPP->setHWStateFence(-1);
                }
            } else {
                mLayers[i]->mOtfMPP->setHWStateFence(-1);
            }
#endif
        }
        if (mLayers[i]->mM2mMPP != NULL) {
            mLayers[i]->mReleaseFence = mLayers[i]->mM2mMPP->getSrcReleaseFence(0);
            mLayers[i]->mM2mMPP->resetSrcReleaseFence();
#ifdef DISABLE_FENCE
            mLayers[i]->mM2mMPP->setDstAcquireFence(-1);
#else
            if (mWinConfigData->fence >= 0) {
                dup_release_fd = hwcCheckFenceDebug(this, FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP,
                        hwc_dup(mWinConfigData->fence, this, FENCE_TYPE_SRC_RELEASE, FENCE_IP_LAYER));
                setFenceName(mWinConfigData->fence, this, FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP, FENCE_DUP, true);
                if (dup_release_fd >= 0)
                    mLayers[i]->mM2mMPP->setDstAcquireFence(dup_release_fd);
                else {
                    DISPLAY_LOGE("fail to dup, ret(%d, %s)", errno, strerror(errno));
                    mLayers[i]->mM2mMPP->setDstAcquireFence(-1);
                }
            } else {
                mLayers[i]->mM2mMPP->setDstAcquireFence(-1);
            }
            DISPLAY_LOGD(eDebugFence, "mM2mMPP is used, layer[%zu].releaseFencefd(%d)",
                    i, mLayers[i]->mReleaseFence);
#endif
        } else {
#ifdef DISABLE_FENCE
            mLayers[i]->mReleaseFence = -1;
#else
            if (mWinConfigData->fence >= 0) {
                dup_release_fd = hwcCheckFenceDebug(this, FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP,
                        hwc_dup(mWinConfigData->fence, this, FENCE_TYPE_SRC_RELEASE, FENCE_IP_LAYER));
                if (dup_release_fd >= 0)
                    mLayers[i]->mReleaseFence = dup_release_fd;
                else {
                    DISPLAY_LOGE("fail to dup, ret(%d, %s)", errno, strerror(errno));
                    mLayers[i]->mReleaseFence = -1;
                }
            } else {
                mLayers[i]->mReleaseFence = -1;
            }
            DISPLAY_LOGD(eDebugFence, "Direct overlay layer[%zu].releaseFencefd(%d)",
                i, mLayers[i]->mReleaseFence);
#endif
        }
    }

    if (mExynosCompositionInfo.mHasCompositionLayer) {
        if (mExynosCompositionInfo.mM2mMPP == NULL)
        {
            errString.appendFormat("There is exynos composition, but m2mMPP is NULL\n");
            goto err;
        }
        for (int i = mExynosCompositionInfo.mFirstIndex; i <= mExynosCompositionInfo.mLastIndex; i++) {
            /* break when only framebuffer target is assigned on ExynosCompositor */
            if (i == -1)
                break;

            if (mLayers[i]->mExynosCompositionType != HWC2_COMPOSITION_EXYNOS) {
                errString.appendFormat("%d layer compositionType is not exynos(%d)\n", i, mLayers[i]->mExynosCompositionType);
                goto err;
            }
            mLayers[i]->mReleaseFence =
                mExynosCompositionInfo.mM2mMPP->getSrcReleaseFence(i-mExynosCompositionInfo.mFirstIndex);
            DISPLAY_LOGD(eDebugFence, "exynos composition layer[%d].releaseFencefd(%d)",
                    i, mLayers[i]->mReleaseFence);
        }
        mExynosCompositionInfo.mM2mMPP->resetSrcReleaseFence();
#ifdef DISABLE_FENCE
        mExynosCompositionInfo.mM2mMPP->setDstAcquireFence(-1);
#else
        if (mWinConfigData->fence >= 0)
            mExynosCompositionInfo.mM2mMPP->setDstAcquireFence(hwc_dup(mWinConfigData->fence, this,
                        FENCE_TYPE_RETIRE, FENCE_IP_DPP));
        else
            mExynosCompositionInfo.mM2mMPP->setDstAcquireFence(-1);
#endif
    }

    return 0;

err:
    printDebugInfos(errString);
    usleep(20000);
    closeFences();
    if (exynosHWCControl.forcePanic == 1)
        ioctl(mDisplayFd, S3CFB_FORCE_PANIC, 0);
    return -EINVAL;
}

/**
 * @return bool
 */
bool ExynosDisplay::skipWinConfig() {
    return false;
}

/**
 * If display uses outbuf and outbuf is invalid, this function return false.
 * Otherwise, this function return true.
 * If outbuf is invalid, display should handle fence of layers.
 */
bool ExynosDisplay::checkFrameValidation() {
    return true;
}

int32_t ExynosDisplay::acceptDisplayChanges() {
    /* TODO : Implemenation here */
    for (size_t i = 0; i < mLayers.size(); i++) {
        if (mLayers[i] != NULL) {
            HDEBUGLOGD(eDebugDefault, "%s, Layer %zu : %d, %d", __func__, i,
                    mLayers[i]->mExynosCompositionType, mLayers[i]->mValidateCompositionType);
            mLayers[i]->mExynosCompositionType = mLayers[i]->mValidateCompositionType;
        }
        else {
            HDEBUGLOGE(eDebugDefault, "Layer %zu is NULL", i);
        }
    }
    mRenderingState = RENDERING_STATE_ACCEPTED_CHANGE;
    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::createLayer(hwc2_layer_t* outLayer) {

    /* TODO : Implementation here */
    ExynosLayer *layer = new ExynosLayer(this);

    /* TODO : Sort sequence should be added to somewhere */
    mLayers.add((ExynosLayer*)layer);

    /* TODO : Set z-order to max, check outLayer address? */
    layer->setLayerZOrder(1000);

    outLayer = (hwc2_layer_t*)layer;

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::getActiveConfig(
        hwc2_config_t* outConfig) {
    /* Check done */

    outConfig = 0;

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::getChangedCompositionTypes(
        uint32_t* outNumElements, hwc2_layer_t* outLayers,
        int32_t* /*hwc2_composition_t*/ outTypes) {

    uint32_t count = 0;
    int32_t type = 0;

    for (size_t i = 0; i < mLayers.size(); i++) {
        if (mLayers[i]->mExynosCompositionType != mLayers[i]->mValidateCompositionType) {

            if ((mLayers[i]->mValidateCompositionType == HWC2_COMPOSITION_CLIENT) &&
                (mClientCompositionInfo.mSkipFlag) &&
                (mClientCompositionInfo.mFirstIndex <= (int32_t)i) &&
                ((int32_t)i <= mClientCompositionInfo.mLastIndex)) {
                type = HWC2_COMPOSITION_DEVICE;
            } else if (mLayers[i]->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS) {
                type = HWC2_COMPOSITION_DEVICE;
            } else {
                type = mLayers[i]->mValidateCompositionType;
            }

            if (type != mLayers[i]->mCompositionType) {
                if (outLayers == NULL || outTypes == NULL) {
                    count++;
                }
                else {
                    if (count < *outNumElements) {
                        outLayers[count] = (hwc2_layer_t)mLayers[i];
                        outTypes[count] = type;
                        count++;
                    } else
                        return -1;
                }
            }
        }
    }

    if ((outLayers == NULL) || (outTypes == NULL))
        *outNumElements = count;

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::getClientTargetSupport(
        uint32_t __unused width,
        uint32_t __unused height, int32_t /*android_pixel_format_t*/ __unused format,
        int32_t /*android_dataspace_t*/ __unused dataspace) {
    return 0;
}

int32_t ExynosDisplay::getColorModes(
        uint32_t* __unused outNumModes,
        int32_t* /*android_color_mode_t*/ __unused outModes) {
    return 0;
}

int32_t ExynosDisplay::getDisplayAttribute(
        hwc2_config_t __unused config,
        int32_t /*hwc2_attribute_t*/ attribute, int32_t* outValue) {

    /* TODO : Exception handling here */

    /* TODO : outValue return is fine ? */
    switch (attribute) {
    case HWC_DISPLAY_VSYNC_PERIOD: //HWC2_ATTRIBUTE_VSYNC_PERIOD:
        *outValue = mVsyncPeriod;
        break;

    case HWC_DISPLAY_WIDTH: //HWC2_ATTRIBUTE_WIDTH:
        *outValue = mXres;
        break;

    case HWC_DISPLAY_HEIGHT: //HWC2_ATTRIBUTE_HEIGHT:
        *outValue = mYres;
        break;

    case HWC_DISPLAY_DPI_X: //HWC2_ATTRIBUTE_DPI_X:
        *outValue = mXdpi;
        break;

    case HWC_DISPLAY_DPI_Y: //HWC2_ATTRIBUTE_DPI_Y:
        *outValue = mYdpi;
        break;

    /** TODO should be defined 
     * case HWC_DISPLAY_COLOR_TRANSFORM:
     */

    case HWC_DISPLAY_NO_ATTRIBUTE:
        return HWC2_ERROR_NONE;

//    case HWC2_ATTRIBUTE_INVALID:
    default:
        ALOGE("unknown display attribute %u", attribute);
        return -1;
    }

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::getDisplayConfigs(
        uint32_t* outNumConfigs,
        hwc2_config_t* outConfigs) {

    /* TODO Check NULL conditions */
    if (outConfigs == NULL)
        *outNumConfigs = 1;
    else if (*outNumConfigs >= 1)
        outConfigs[0] = 0;

    *outNumConfigs = 1;

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::getDisplayName(
        uint32_t* __unused outSize,
        char* __unused outName) {
    return 0;
}

int32_t ExynosDisplay::getDisplayRequests(
        int32_t* /*hwc2_display_request_t*/ outDisplayRequests,
        uint32_t* outNumElements, hwc2_layer_t* outLayers,
        int32_t* /*hwc2_layer_request_t*/ outLayerRequests) {

    String8 errString;
    if (mRenderingState < RENDERING_STATE_VALIDATED)
        return HWC2_ERROR_NOT_VALIDATED;

    *outDisplayRequests = 0;

    uint32_t requestNum = 0;
    if (mClientCompositionInfo.mHasCompositionLayer == true) {
        if ((mClientCompositionInfo.mFirstIndex < 0) ||
            (mClientCompositionInfo.mFirstIndex >= (int)mLayers.size()) ||
            (mClientCompositionInfo.mLastIndex < 0) ||
            (mClientCompositionInfo.mLastIndex >= (int)mLayers.size())) {
            errString.appendFormat("%s:: mClientCompositionInfo.mHasCompositionLayer is true "
                    "but index is not valid (firstIndex: %d, lastIndex: %d)\n",
                    __func__, mClientCompositionInfo.mFirstIndex,
                    mClientCompositionInfo.mLastIndex);
            goto err;
        }

        for (int32_t i = mClientCompositionInfo.mFirstIndex; i < mClientCompositionInfo.mLastIndex; i++) {
            ExynosLayer *layer = mLayers[i];
            if (layer->mOverlayPriority >= ePriorityHigh) {
                if ((outLayers != NULL) && (outLayerRequests != NULL)) {
                    if (requestNum >= *outNumElements)
                        return -1;
                    outLayers[requestNum] = (hwc2_layer_t)layer;
                    outLayerRequests[requestNum] = HWC2_LAYER_REQUEST_CLEAR_CLIENT_TARGET;
                }
                requestNum++;
            }
        }
    }
    if ((outLayers == NULL) || (outLayerRequests == NULL))
        *outNumElements = requestNum;

    return HWC2_ERROR_NONE;

err:
    printDebugInfos(errString);
    usleep(20000);
    *outNumElements = 0;
    if (exynosHWCControl.forcePanic == 1)
        ioctl(mDisplayFd, S3CFB_FORCE_PANIC, 0);
    return -EINVAL;
}

int32_t ExynosDisplay::getDisplayType(
        int32_t* /*hwc2_display_type_t*/ __unused outType) {
    return 0;
}

int32_t ExynosDisplay::getDozeSupport(
        int32_t* __unused outSupport) {
    return 0;
}

int32_t ExynosDisplay::getReleaseFences(
        uint32_t* outNumElements,
        hwc2_layer_t* outLayers, int32_t* outFences) {

    if (outLayers == NULL || outFences == NULL)
    {
        uint32_t deviceLayerNum = 0;
        for (size_t i = 0; i < mLayers.size(); i++) {
            if (mLayers[i]->mCompositionType != HWC2_COMPOSITION_CLIENT)
                deviceLayerNum++;
        }
        *outNumElements = deviceLayerNum;
    } else {
        uint32_t deviceLayerNum = 0;
        for (size_t i = 0; i < mLayers.size(); i++) {
            if (mLayers[i]->mCompositionType != HWC2_COMPOSITION_CLIENT)
            {
                outLayers[deviceLayerNum] = (hwc2_layer_t)mLayers[i];
                outFences[deviceLayerNum] = mLayers[i]->mReleaseFence;
                deviceLayerNum++;

                if (mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_EXYNOS) {
                    setFenceName(mLayers[i]->mReleaseFence, FENCE_LAYER_RELEASE_G2D);
                } else {
                    if (mLayers[i]->mM2mMPP != NULL) {
                        if (mLayers[i]->mM2mMPP->mPhysicalType == MPP_MSC) {
                            setFenceName(mLayers[i]->mReleaseFence, FENCE_LAYER_RELEASE_MSC);
                        } else if (mLayers[i]->mM2mMPP->mPhysicalType == MPP_G2D) {
                            setFenceName(mLayers[i]->mReleaseFence, FENCE_LAYER_RELEASE_G2D);
                        } else {
                            setFenceName(mLayers[i]->mReleaseFence, FENCE_LAYER_RELEASE_MPP);
                        }
                    } else {
                        setFenceName(mLayers[i]->mReleaseFence, FENCE_LAYER_RELEASE_DPP);
                    }
                }
            } else {
                if (mLayers[i]->mReleaseFence > 0) {
                    DISPLAY_LOGE("layer[%zu] type(%d), fence(%d) should be closed",
                            i, mLayers[i]->mExynosCompositionType, mLayers[i]->mReleaseFence);
                    close(mLayers[i]->mReleaseFence);
                    mLayers[i]->mReleaseFence = -1;

                    String8 errString;
                    errString.appendFormat("%s::layer[%zu] type(%d), fence(%d) should be closed",
                            __func__, i, mLayers[i]->mExynosCompositionType, mLayers[i]->mReleaseFence);
                    printDebugInfos(errString);
                }
            }
        }
    }
    return 0;
}

int32_t ExynosDisplay::presentDisplay(int32_t* outRetireFence) {
    Mutex::Autolock lock(mDisplayMutex);

    int ret = 0;
    String8 errString;
    if (!checkFrameValidation())
        return ret;

    if ((ret = doExynosComposition()) != NO_ERROR) {
        errString.appendFormat("exynosComposition fail (%d)\n", ret);
        goto err;
    }

#if defined(TARGET_USES_HWC2)
    if ((mLayers.size() == 0) &&
        (mDisplayId != HWC_DISPLAY_VIRTUAL)) {
        clearDisplay();
        mRetireFence = fence_close(mRetireFence, this,
                FENCE_TYPE_RETIRE, FENCE_IP_DPP);
        mRenderingState = RENDERING_STATE_PRESENTED;
        return ret;
    }
#endif

    // loop for all layer
    for (size_t i=0; i < mLayers.size(); i++) {
        if (mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_CLIENT) {
            mLayers[i]->mReleaseFence = -1;
        } else if (mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_EXYNOS) {
            continue;
        } else {
            if (mLayers[i]->mOtfMPP != NULL)
                mLayers[i]->mOtfMPP->requestHWStateChange(MPP_HW_STATE_RUNNING);
            if(mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_DEVICE
                    && mLayers[i]->mM2mMPP != NULL) {
                /* mAcquireFence is updated, Update image info */
                struct exynos_image srcImg, dstImg, midImg;
                mLayers[i]->setSrcExynosImage(&srcImg);
                mLayers[i]->setDstExynosImage(&dstImg);
                mLayers[i]->setExynosImage(srcImg, dstImg);
                ExynosMPP *m2mMpp = mLayers[i]->mM2mMPP;
                srcImg = mLayers[i]->mSrcImg;
                midImg = mLayers[i]->mMidImg;
                m2mMpp->requestHWStateChange(MPP_HW_STATE_RUNNING);
                if ((ret = m2mMpp->doPostProcessing(srcImg, midImg)) != NO_ERROR) {
                    HWC_LOGE(this, "%s:: doPostProcessing() failed, layer(%zu), ret(%d)",
                            __func__, i, ret);
                    errString.appendFormat("%s:: doPostProcessing() failed, layer(%zu), ret(%d)\n",
                            __func__, i, ret);
                    goto err;
                } else {
                    /* This should be closed by lib for each resource */
                    mLayers[i]->mAcquireFence = -1;
                }
            }
        }
    }

    if ((ret = setWinConfigData()) != NO_ERROR) {
        errString.appendFormat("setWinConfigData fail (%d)\n", ret);
        goto err;
    }

    if ((ret = handleStaticLayers(mClientCompositionInfo)) != NO_ERROR) {
        errString.appendFormat("handleStaticLayers error\n");
        goto err;
    }

    handleWindowUpdate();

    setDisplayWinConfigData();

    if ((ret = deliverWinConfigData()) != NO_ERROR) {
        HWC_LOGE(this, "%s:: fail to deliver win_config (%d)", __func__, ret);
        if (mWinConfigData->fence > 0)
            fence_close(mWinConfigData->fence, this, FENCE_TYPE_RETIRE, FENCE_IP_DPP);
        mWinConfigData->fence = -1;
    }

    setReleaseFences();

    if (mWinConfigData->fence != -1) {
#ifdef DISABLE_FENCE
        if (mWinConfigData->fence >= 0)
            fence_close(mWinConfigData->fence, this, FENCE_TYPE_RETIRE, FENCE_IP_DPP);
        *outRetireFence = -1;
#else
        *outRetireFence =
            hwcCheckFenceDebug(this, FENCE_TYPE_RETIRE, FENCE_IP_DPP, mWinConfigData->fence);
#endif
        setFenceInfo(mWinConfigData->fence, this,
                FENCE_TYPE_RETIRE, FENCE_IP_LAYER, FENCE_TO);
    } else
        *outRetireFence = -1;

    mRetireFence = *outRetireFence;
    setFenceName(mRetireFence, FENCE_RETIRE);

    for (size_t i=0; i < mLayers.size(); i++) {
        if((mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_DEVICE) &&
           (mLayers[i]->mM2mMPP != NULL)) {
            mLayers[i]->mM2mMPP->increaseDstBuffIndex();
        }
    }

    if ((mExynosCompositionInfo.mHasCompositionLayer) &&
        (mExynosCompositionInfo.mM2mMPP != NULL)) {
        mExynosCompositionInfo.mM2mMPP->increaseDstBuffIndex();
    }

    /* Check all of acquireFence are closed */
    for (size_t i=0; i < mLayers.size(); i++) {
        if (mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_CLIENT)
            continue;
        if (mLayers[i]->mAcquireFence != -1) {
            DISPLAY_LOGE("layer[%zu] fence(%d) is not closed", i, mLayers[i]->mAcquireFence);
            if (mLayers[i]->mAcquireFence > 0)
                fence_close(mLayers[i]->mAcquireFence, this,
                        FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
            mLayers[i]->mAcquireFence = -1;
        }
    }
    if (mExynosCompositionInfo.mAcquireFence >= 0) {
        DISPLAY_LOGE("mExynosCompositionInfo mAcquireFence(%d) is not initialized", mExynosCompositionInfo.mAcquireFence);
        fence_close(mExynosCompositionInfo.mAcquireFence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_G2D);
        mExynosCompositionInfo.mAcquireFence = -1;
    }
    if (mClientCompositionInfo.mAcquireFence >= 0) {
        DISPLAY_LOGE("mClientCompositionInfo mAcquireFence(%d) is not initialized", mClientCompositionInfo.mAcquireFence);
        fence_close(mClientCompositionInfo.mAcquireFence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB);
        mClientCompositionInfo.mAcquireFence = -1;
    }

    /* All of release fences are tranferred */
    for (size_t i=0; i < mLayers.size(); i++) {
        setFenceInfo(mLayers[i]->mReleaseFence, this,
                FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER, FENCE_TO);
    }

    doPostProcessing();

    if (!mDevice->validateFences(this)){
        String8 errString;
        errString.appendFormat("%s:: validate fence failed. \n", __func__);
        printDebugInfos(errString);
    }

    mRenderingState = RENDERING_STATE_PRESENTED;

    return ret;
err:
    printDebugInfos(errString);
    usleep(20000);
    closeFences();
    *outRetireFence = -1;
    mRetireFence = *outRetireFence;
    if (exynosHWCControl.forcePanic == 1)
        ioctl(mDisplayFd, S3CFB_FORCE_PANIC, 0);

    if (!mDevice->validateFences(this)){
        errString.appendFormat("%s:: validate fence failed. \n", __func__);
        printDebugInfos(errString);
    }
    return -EINVAL;
}

int32_t ExynosDisplay::setActiveConfig(
        hwc2_config_t __unused config) {
    /* TODO Do nothing like HWC 1.5 ? */
    return 0;
}

int32_t ExynosDisplay::setClientTarget(
        buffer_handle_t target,
        int32_t acquireFence, int32_t /*android_dataspace_t*/ dataspace) {

// TODO : Need to check Client tareget is?
#if 0
    if (mCompositionType != HWC_FRAMEBUFFER_TARGET
            || mCompositionType != HWC2_COMPOSITION_CLIENT)
        ALOGW("Client target type isn't matched.");
#endif

    // HWC2_COMPOSITION_CLIENT
    /* TODO : Implementation here */
    private_handle_t *handle = NULL;
    if (target != NULL)
        handle = private_handle_t::dynamicCast(target);

    if (mClientCompositionInfo.mHasCompositionLayer == false) {
        if (acquireFence >= 0)
            fence_close(acquireFence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB);
    } else {
#ifdef DISABLE_FENCE
        if (acquireFence >= 0)
            fence_close(acquireFence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB);
        acquireFence = -1;
#endif
        acquireFence = hwcCheckFenceDebug(this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB, acquireFence);
        if (handle == NULL) {
            DISPLAY_LOGD(eDebugOverlaySupported, "ClientTarget is NULL");
        } else {
            DISPLAY_LOGD(eDebugOverlaySupported, "ClientTarget handle: %p [fd: %d, %d, %d]",
                    handle, handle->fd, handle->fd1, handle->fd2);
        }
        mClientCompositionInfo.setTargetBuffer(this, handle, acquireFence, (android_dataspace)dataspace);
        setFenceInfo(acquireFence, this,
                FENCE_TYPE_SRC_RELEASE, FENCE_IP_FB, FENCE_FROM);
    }

    return 0;
}

int32_t ExynosDisplay::setColorTransform(
        float* __unused matrix,
        int32_t /*android_color_transform_t*/ __unused hint) {
    return 0;
}

int32_t ExynosDisplay::setColorMode(
        int32_t /*android_color_mode_t*/ __unused mode) {
    return 0;
}

int32_t ExynosDisplay::setOutputBuffer(
        buffer_handle_t __unused buffer,
        int32_t __unused releaseFence) {
    return 0;
}

int ExynosDisplay::clearDisplay() {

    int ret = 0;

    struct decon_win_config_data win_data;
    memset(&win_data, 0, sizeof(win_data));
    struct decon_win_config *config = win_data.config;

    for (size_t i = 0; i < NUM_HW_WINDOWS; i++)
        config[i].fence_fd = -1;

#if defined(HWC_CLEARDISPLAY_WITH_COLORMAP)
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        if (i == mBaseWindowIndex) {
            config[i].state = config[i].DECON_WIN_STATE_COLOR;
            config[i].idma_type = IDMA_G1;
            config[i].color = 0x0;
            config[i].dst.x = 0;
            config[i].dst.y = 0;
            config[i].dst.w = this->mXres;
            config[i].dst.h = this->mYres;
            config[i].dst.f_w = this->mXres;
            config[i].dst.f_h = this->mYres;
        }
        else
            config[i].state = config[i].DECON_WIN_STATE_DISABLED;
    }
#endif

    win_data.fence = -1;

    ret = ioctl(mDisplayFd, S3CFB_WIN_CONFIG, &win_data);
    if (ret < 0)
        HWC_LOGE(this, "ioctl S3CFB_WIN_CONFIG failed to clear screen: %s",
                strerror(errno));

    if (win_data.fence > 0)
        fence_close(win_data.fence, this, FENCE_TYPE_RETIRE, FENCE_IP_DPP);

    mClientCompositionInfo.mSkipStaticInitFlag = false;
    mClientCompositionInfo.mSkipFlag = false;

    memset(mLastWinConfigData, 0, sizeof(*mLastWinConfigData));
    mLastWinConfigData->fence = -1;
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        mLastWinConfigData->config[i].fence_fd = -1;
    }
    mRetireFence = -1;

    return ret;
}

int32_t ExynosDisplay::setPowerMode(
        int32_t /*hwc2_power_mode_t*/ mode) {

    /* TODO state check routine should be added */

    int fb_blank = 0;

    if (mode == HWC_POWER_MODE_OFF) {
        fb_blank = FB_BLANK_POWERDOWN;
        mDevice->mPrimaryBlank = true;
        clearDisplay();
        ALOGV("HWC2: Clear display (power off)");
    } else {
        fb_blank = FB_BLANK_UNBLANK;
        mDevice->mPrimaryBlank = false;
    }
    android_atomic_acquire_load(&this->updateThreadStatus);

    if (this->updateThreadStatus != 0) { //check if the thread is alive
        if (fb_blank == FB_BLANK_POWERDOWN) {
            mDevice->dynamic_recomp_stat_thread_flag = false;
            pthread_join(mDevice->mDynamicRecompositionThread, 0);
        } else { // thread is not alive
            if (fb_blank == FB_BLANK_UNBLANK && exynosHWCControl.useDynamicRecomp == true)
                mDevice->dynamicRecompositionThreadLoop();
        }
    }

    if (ioctl(mDisplayFd, FBIOBLANK, fb_blank) == -1) {
        HWC_LOGE(this, "set powermode ioctl failed errno : %d", errno);
        return HWC2_ERROR_UNSUPPORTED;
    }
    ALOGD("%s:: mode(%d), blank(%d)", __func__, mode, fb_blank);

    this->mPowerModeState = (hwc2_power_mode_t)mode;

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::setVsyncEnabled(
        int32_t /*hwc2_vsync_t*/ enabled) {

    __u32 val = !!enabled;

//    ALOGD("HWC2 : %s : %d %d", __func__, __LINE__, enabled);

    if (enabled < 0 || enabled > HWC2_VSYNC_DISABLE)
        return HWC2_ERROR_BAD_PARAMETER;

    if (ioctl(mDisplayFd, S3CFB_SET_VSYNC_INT, &val) == -1) {
        HWC_LOGE(this, "vsync ioctl failed errno : %d", errno);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    mVsyncState = enabled ? HWC2_VSYNC_ENABLE : HWC2_VSYNC_DISABLE;

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::validateDisplay(
        uint32_t* outNumTypes, uint32_t* outNumRequests) {

    Mutex::Autolock lock(mDisplayMutex);
    int ret = NO_ERROR;
    mUpdateEventCnt++;
    mUpdateCallCnt++;
    mLastUpdateTimeStamp = systemTime(SYSTEM_TIME_MONOTONIC);
    // Reset current frame flags for Fence Tracer
    resetFenceCurFlag(this);
    doPreProcessing();

#if defined(TARGET_USES_HWC2)
    if (mLayers.size() == 0)
        DISPLAY_LOGI("%s:: validateDisplay layer size is 0", __func__);
#endif

    if (exynosHWCControl.useDynamicRecomp == true)
        checkDynamicReCompMode();
// TODO Dynamic recomposition
    if (exynosHWCControl.useDynamicRecomp == true &&
            mDevice->dynamic_recomp_stat_thread_flag == false &&
            mDevice->mPrimaryBlank == false) {
        mDevice->dynamicRecompositionThreadLoop();
    }
#if 0
    if(dynamic remoposition enable)
    {
        checkDynamicReCompMode();
        handleDynamicReCompMode();
    }
#endif

    /* TODO : dynamic recomposition check */

//    if (/*dynamic recomposition is disabled || */ mDynamicReCompMode != DEVICE_2_CLIENT) {
//      handleLowFpsLayers();

    for (size_t i=0; i < mLayers.size(); i++) {
        // Layer's acquire fence from SF
        setFenceInfo(mLayers[i]->mAcquireFence, this,
                FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER, FENCE_FROM);
    }

    if ((ret = mResourceManager->assignResource(this)) != NO_ERROR) {
        HWC_LOGE(this, "%s:: assignResource() fail, display(%d), ret(%d)", __func__, mDisplayId, ret);
        String8 errString;
        errString.appendFormat("%s:: assignResource() fail, display(%d), ret(%d)\n",
                __func__, mDisplayId, ret);
        printDebugInfos(errString);
        usleep(20000);
        if (exynosHWCControl.forcePanic == 1)
            ioctl(mDisplayFd, S3CFB_FORCE_PANIC, 0);
    }
    skipStaticLayers(mClientCompositionInfo);

    mRenderingState = RENDERING_STATE_VALIDATED;

    int32_t displayRequests = 0;
    getChangedCompositionTypes(outNumTypes, NULL, NULL);
    getDisplayRequests(&displayRequests, outNumRequests, NULL, NULL);

    if ((*outNumTypes == 0) && (*outNumRequests == 0))
        return HWC2_ERROR_NONE;

    return HWC2_ERROR_HAS_CHANGES;
}

int32_t ExynosDisplay::setCursorPositionAsync(uint32_t __unused x_pos, uint32_t __unused y_pos) {
    /* TOOD : Implementation */
    return HWC2_ERROR_NONE;
}

void ExynosDisplay::dumpConfig(decon_win_config &c)
{
    DISPLAY_LOGD(eDebugWinConfig|eDebugSkipStaicLayer, "\tstate = %u", c.state);
    if (c.state == c.DECON_WIN_STATE_COLOR) {
        DISPLAY_LOGD(eDebugWinConfig|eDebugSkipStaicLayer, "\t\tcolor = %u", c.color);
    } else/* if (c.state != c.DECON_WIN_STATE_DISABLED) */{
        DISPLAY_LOGD(eDebugWinConfig|eDebugSkipStaicLayer, "\t\tfd = (%d, %d, %d), dma = %u, fence = %d, "
                "src_f_w = %u, src_f_h = %u, src_x = %d, src_y = %d, src_w = %u, src_h = %u, "
                "dst_f_w = %u, dst_f_h = %u, dst_x = %d, dst_y = %d, dst_w = %u, dst_h = %u, "
                "format = %u, eq_mode = 0x%4x, blending = %u, protection = %u, compression = %d, compression_src = %d, transparent(x:%d, y:%d, w:%d, h:%d), "
                "block(x:%d, y:%d, w:%d, h:%d)",
                c.fd_idma[0], c.fd_idma[1], c.fd_idma[2],
                c.idma_type, c.fence_fd,
                c.src.f_w, c.src.f_h, c.src.x, c.src.y, c.src.w, c.src.h,
                c.dst.f_w, c.dst.f_h, c.dst.x, c.dst.y, c.dst.w, c.dst.h,
                c.format, c.dpp_parm.eq_mode, c.blending, c.protection, c.compression, c.dpp_parm.comp_src,
                c.transparent_area.x, c.transparent_area.y, c.transparent_area.w, c.transparent_area.h,
                c.opaque_area.x, c.opaque_area.y, c.opaque_area.w, c.opaque_area.h);
    }
}

void ExynosDisplay::dumpConfig(String8 &result, decon_win_config &c)
{
    result.appendFormat("\tstate = %u\n", c.state);
    if (c.state == c.DECON_WIN_STATE_COLOR) {
        result.appendFormat("\t\tcolor = %u\n", c.color);
    } else/* if (c.state != c.DECON_WIN_STATE_DISABLED) */{
        result.appendFormat("\t\tfd = (%d, %d, %d), dma = %u, fence = %d, "
                "src_f_w = %u, src_f_h = %u, src_x = %d, src_y = %d, src_w = %u, src_h = %u, "
                "dst_f_w = %u, dst_f_h = %u, dst_x = %d, dst_y = %d, dst_w = %u, dst_h = %u, "
                "format = %u, eq_mode = 0x%4x, blending = %u, protection = %u, compression = %d, compression_src = %d, transparent(x:%d, y:%d, w:%d, h:%d), "
                "block(x:%d, y:%d, w:%d, h:%d)\n",
                c.fd_idma[0], c.fd_idma[1], c.fd_idma[2],
                c.idma_type, c.fence_fd,
                c.src.f_w, c.src.f_h, c.src.x, c.src.y, c.src.w, c.src.h,
                c.dst.f_w, c.dst.f_h, c.dst.x, c.dst.y, c.dst.w, c.dst.h,
                c.format, c.dpp_parm.eq_mode, c.blending, c.protection, c.compression, c.dpp_parm.comp_src,
                c.transparent_area.x, c.transparent_area.y, c.transparent_area.w, c.transparent_area.h,
                c.opaque_area.x, c.opaque_area.y, c.opaque_area.w, c.opaque_area.h);
    }
}

void ExynosDisplay::printConfig(decon_win_config &c)
{
    ALOGD("\tstate = %u", c.state);
    if (c.state == c.DECON_WIN_STATE_COLOR) {
        ALOGD("\t\tcolor = %u", c.color);
    } else/* if (c.state != c.DECON_WIN_STATE_DISABLED) */{
        ALOGD("\t\tfd = %d, dma = %u, fence = %d, "
                "src_f_w = %u, src_f_h = %u, src_x = %d, src_y = %d, src_w = %u, src_h = %u, "
                "dst_f_w = %u, dst_f_h = %u, dst_x = %d, dst_y = %d, dst_w = %u, dst_h = %u, "
                "format = %u, eq_mode = 0x%4x, blending = %u, protection = %u, compression = %d, compression_src = %d, transparent(x:%d, y:%d, w:%d, h:%d), "
                "block(x:%d, y:%d, w:%d, h:%d)",
                c.fd_idma[0], c.idma_type, c.fence_fd,
                c.src.f_w, c.src.f_h, c.src.x, c.src.y, c.src.w, c.src.h,
                c.dst.f_w, c.dst.f_h, c.dst.x, c.dst.y, c.dst.w, c.dst.h,
                //c.format, c.blending, c.protection, c.compression,
                c.format, c.dpp_parm.eq_mode, c.blending, 0, c.compression, c.dpp_parm.comp_src,
                c.transparent_area.x, c.transparent_area.y, c.transparent_area.w, c.transparent_area.h,
                c.opaque_area.x, c.opaque_area.y, c.opaque_area.w, c.opaque_area.h);
    }
}

int32_t ExynosDisplay::setCompositionTargetExynosImage(uint32_t targetType, exynos_image *src_img, exynos_image *dst_img)
{
    ExynosCompositionInfo compositionInfo;

    if (targetType == COMPOSITION_CLIENT)
        compositionInfo = mClientCompositionInfo;
    else if (targetType == COMPOSITION_EXYNOS)
        compositionInfo = mExynosCompositionInfo;
    else
        return -EINVAL;

    src_img->fullWidth = mXres;
    src_img->fullHeight = mYres;
    /* To do */
    /* Fb crop should be set hear */
    src_img->x = 0;
    src_img->y = 0;
    src_img->w = mXres;
    src_img->h = mYres;

    if (compositionInfo.mTargetBuffer != NULL) {
        src_img->bufferHandle = compositionInfo.mTargetBuffer;
        src_img->format = compositionInfo.mTargetBuffer->format;
#ifdef GRALLOC_VERSION1
        src_img->handleFlags = compositionInfo.mTargetBuffer->producer_usage;
#else
        src_img->handleFlags = compositionInfo.mTargetBuffer->flags;
#endif
    } else {
        src_img->bufferHandle = NULL;
        src_img->format = HAL_PIXEL_FORMAT_RGBA_8888;
        src_img->handleFlags = 0;
    }
    src_img->layerFlags = 0x0;
    src_img->acquireFenceFd = compositionInfo.mAcquireFence;
    src_img->releaseFenceFd = -1;
    src_img->dataSpace = compositionInfo.mDataSpace;
    src_img->blending = HWC_BLENDING_PREMULT;
    src_img->transform = 0;
    src_img->compressed = compositionInfo.mCompressed;
    src_img->planeAlpha = 255;

    dst_img->fullWidth = mXres;
    dst_img->fullHeight = mYres;
    /* To do */
    /* Fb crop should be set hear */
    dst_img->x = 0;
    dst_img->y = 0;
    dst_img->w = mXres;
    dst_img->h = mYres;

    dst_img->bufferHandle = NULL;
    dst_img->format = HAL_PIXEL_FORMAT_RGBA_8888;
    dst_img->handleFlags = 0;

    dst_img->layerFlags = 0x0;
    dst_img->acquireFenceFd = -1;
    dst_img->releaseFenceFd = -1;
    dst_img->dataSpace = src_img->dataSpace;
    dst_img->blending = HWC_BLENDING_NONE;
    dst_img->transform = 0;
    dst_img->compressed = 0;
    dst_img->planeAlpha = 255;

    return NO_ERROR;
}

int32_t ExynosDisplay::initializeValidateInfos()
{
    for (uint32_t i = 0; i < mLayers.size(); i++) {
        ExynosLayer *layer = mLayers[i];
        layer->mValidateCompositionType = HWC2_COMPOSITION_INVALID;
        layer->mOverlayInfo = 0;
    }

    mClientCompositionInfo.initializeInfos(this);
    mExynosCompositionInfo.initializeInfos(this);
    return NO_ERROR;
}

int32_t ExynosDisplay::addClientCompositionLayer(uint32_t layerIndex)
{
    bool exynosCompositionChanged = false;
    int32_t ret = NO_ERROR;

    DISPLAY_LOGD(eDebugResourceManager, "[%d] layer is added to client composition", layerIndex);

    if (mClientCompositionInfo.mHasCompositionLayer == false) {
        mClientCompositionInfo.mFirstIndex = layerIndex;
        mClientCompositionInfo.mLastIndex = layerIndex;
        mClientCompositionInfo.mHasCompositionLayer = true;
        return EXYNOS_ERROR_CHANGED;
    } else {
        mClientCompositionInfo.mFirstIndex = min(mClientCompositionInfo.mFirstIndex, (int32_t)layerIndex);
        mClientCompositionInfo.mLastIndex = max(mClientCompositionInfo.mLastIndex, (int32_t)layerIndex);
    }
    DISPLAY_LOGD(eDebugResourceManager, "\tClient composition range [%d] - [%d]",
            mClientCompositionInfo.mFirstIndex, mClientCompositionInfo.mLastIndex);

    if ((mClientCompositionInfo.mFirstIndex < 0) || (mClientCompositionInfo.mLastIndex < 0))
    {
        HWC_LOGE(this, "%s:: mClientCompositionInfo.mHasCompositionLayer is true "
                "but index is not valid (firstIndex: %d, lastIndex: %d)",
                __func__, mClientCompositionInfo.mFirstIndex,
                mClientCompositionInfo.mLastIndex);
        return -EINVAL;
    }

    /* handle sandwiched layers */
    for (uint32_t i = (uint32_t)mClientCompositionInfo.mFirstIndex + 1; i < (uint32_t)mClientCompositionInfo.mLastIndex; i++) {
        ExynosLayer *layer = mLayers[i];
        if (layer->mOverlayPriority >= ePriorityHigh) {
            DISPLAY_LOGD(eDebugResourceManager, "\t[%d] layer has high or max priority (%d)", i, layer->mOverlayPriority);
            continue;
        }
        if (layer->mValidateCompositionType != HWC2_COMPOSITION_CLIENT)
        {
            DISPLAY_LOGD(eDebugResourceManager, "\t[%d] layer changed", i);
            if (layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS)
                exynosCompositionChanged = true;
            layer->resetAssignedResource();
            layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
            layer->mOverlayInfo |= eSandwitchedBetweenGLES;
        }
    }

    /* Check Exynos Composition info is changed */
    if (exynosCompositionChanged) {
        DISPLAY_LOGD(eDebugResourceManager, "exynos composition [%d] - [%d] is changed",
                mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex);
        uint32_t newFirstIndex = ~0;
        int32_t newLastIndex = -1;

        if ((mExynosCompositionInfo.mFirstIndex < 0) || (mExynosCompositionInfo.mLastIndex < 0))
        {
            HWC_LOGE(this, "%s:: mExynosCompositionInfo.mHasCompositionLayer should be true(%d) "
                    "but index is not valid (firstIndex: %d, lastIndex: %d)",
                    __func__, mExynosCompositionInfo.mHasCompositionLayer,
                    mExynosCompositionInfo.mFirstIndex,
                    mExynosCompositionInfo.mLastIndex);
            return -EINVAL;
        }

        for (uint32_t i = 0; i < mLayers.size(); i++)
        {
            ExynosLayer *exynosLayer = mLayers[i];
            if (exynosLayer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS) {
                newFirstIndex = min(newFirstIndex, i);
                newLastIndex = max(newLastIndex, (int32_t)i);
            }
        }

        DISPLAY_LOGD(eDebugResourceManager, "changed exynos composition [%d] - [%d]",
                newFirstIndex, newLastIndex);

        /* There is no exynos composition layer */
        if (newFirstIndex == (uint32_t)~0)
        {
            mExynosCompositionInfo.initializeInfos(this);
            ret = EXYNOS_ERROR_CHANGED;
        } else {
            mExynosCompositionInfo.mFirstIndex = newFirstIndex;
            mExynosCompositionInfo.mLastIndex = newLastIndex;
        }
    }

    DISPLAY_LOGD(eDebugResourceManager, "\tresult changeFlag(0x%8x)", ret);
    DISPLAY_LOGD(eDebugResourceManager, "\tClient composition(%d) range [%d] - [%d]",
            mClientCompositionInfo.mHasCompositionLayer,
            mClientCompositionInfo.mFirstIndex, mClientCompositionInfo.mLastIndex);
    DISPLAY_LOGD(eDebugResourceManager, "\tExynos composition(%d) range [%d] - [%d]",
            mExynosCompositionInfo.mHasCompositionLayer,
            mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex);

    return ret;
}

int32_t ExynosDisplay::addExynosCompositionLayer(uint32_t layerIndex)
{
    bool invalidFlag = false;
    int32_t changeFlag = NO_ERROR;
    int ret = 0;
    int32_t startIndex;
    int32_t endIndex;

    DISPLAY_LOGD(eDebugResourceManager, "[%d] layer is added to exynos composition", layerIndex);

    if (mExynosCompositionInfo.mHasCompositionLayer == false) {
        mExynosCompositionInfo.mFirstIndex = layerIndex;
        mExynosCompositionInfo.mLastIndex = layerIndex;
        mExynosCompositionInfo.mHasCompositionLayer = true;
        return EXYNOS_ERROR_CHANGED;
    } else {
        mExynosCompositionInfo.mFirstIndex = min(mExynosCompositionInfo.mFirstIndex, (int32_t)layerIndex);
        mExynosCompositionInfo.mLastIndex = max(mExynosCompositionInfo.mLastIndex, (int32_t)layerIndex);
    }

    DISPLAY_LOGD(eDebugResourceManager, "\tExynos composition range [%d] - [%d]",
            mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex);

    ExynosMPP *m2mMPP = mExynosCompositionInfo.mM2mMPP;

    if (m2mMPP == NULL) {
        DISPLAY_LOGE("exynosComposition m2mMPP is NULL");
        return -EINVAL;
    }

    if (m2mMPP->mLogicalType != MPP_LOGICAL_G2D_COMBO) {
        startIndex = mExynosCompositionInfo.mFirstIndex + 1;
        endIndex = mExynosCompositionInfo.mLastIndex - 1;
    } else {
        startIndex = mExynosCompositionInfo.mFirstIndex;
        endIndex = mExynosCompositionInfo.mLastIndex;
    }

    if ((startIndex < 0) || (endIndex < 0) ||
            (startIndex >= (int32_t)mLayers.size()) || (endIndex >= (int32_t)mLayers.size())) {
        DISPLAY_LOGE("exynosComposition invalid index (%d), (%d)", startIndex, endIndex);
        return -EINVAL;
    }

    int32_t maxPriorityIndex = -1;
    uint32_t highPriorityIndex = 0;
    uint32_t highPriorityNum = 0;
    int32_t highPriorityCheck = 0;
    int32_t highPriority[MAX_DECON_WIN];
    /* handle sandwiched layers */
    for (int32_t i = startIndex; i <= endIndex; i++) {
        ExynosLayer *layer = mLayers[i];
        if (layer == NULL) {
            DISPLAY_LOGE("layer[%d] layer is null", i);
            continue;
        }
        exynos_image src_img;
        exynos_image dst_img;
        layer->setSrcExynosImage(&src_img);
        layer->setDstExynosImage(&dst_img);
        bool isAssignable = m2mMPP->isAssignable(this, src_img, dst_img);

        if (layer->mOverlayPriority == ePriorityMax &&
                m2mMPP->mLogicalType == MPP_LOGICAL_G2D_COMBO) {
            DISPLAY_LOGD(eDebugResourceManager, "\tG2D will be assgined for only [%d] layer", i);
            invalidFlag = true;
            maxPriorityIndex = i;
            continue;
        }

        if (layer->mOverlayPriority >= ePriorityHigh)
        {
            DISPLAY_LOGD(eDebugResourceManager, "\t[%d] layer has high priority", i);
            highPriority[highPriorityIndex++] = i;
            highPriorityNum++;
            continue;
        }

        layer->setExynosMidImage(dst_img);
        if (layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS)
            continue;

        if (layer->mValidateCompositionType == HWC2_COMPOSITION_CLIENT)
        {
            DISPLAY_LOGD(eDebugResourceManager, "\t[%d] layer is client composition", i);
            invalidFlag = true;
        } else if (((layer->mSupportedMPPFlag & mExynosCompositionInfo.mM2mMPP->mLogicalType) == 0) ||
                   (isAssignable == false))
        {
            DISPLAY_LOGD(eDebugResourceManager, "\t[%d] layer is not supported by G2D", i);
            invalidFlag = true;
            layer->resetAssignedResource();
            layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
            if ((ret = addClientCompositionLayer(i)) < 0)
                return ret;
            changeFlag |= ret;
        } else if (layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE) {
            DISPLAY_LOGD(eDebugResourceManager, "\t[%d] layer changed", i);
            layer->mValidateCompositionType = HWC2_COMPOSITION_EXYNOS;
            layer->mOverlayInfo |= eSandwitchedBetweenEXYNOS;
            layer->resetAssignedResource();
            if ((ret = m2mMPP->assignMPP(this, layer)) != NO_ERROR)
            {
                HWC_LOGE(this, "%s:: %s MPP assignMPP() error (%d)",
                        __func__, m2mMPP->mName.string(), ret);
                return ret;
            }
            mExynosCompositionInfo.mFirstIndex = min(mExynosCompositionInfo.mFirstIndex, (int32_t)i);
            mExynosCompositionInfo.mLastIndex = max(mExynosCompositionInfo.mLastIndex, (int32_t)i);
        }
    }

    if (invalidFlag) {
        DISPLAY_LOGD(eDebugResourceManager, "\tClient composition range [%d] - [%d]",
                mClientCompositionInfo.mFirstIndex, mClientCompositionInfo.mLastIndex);
        DISPLAY_LOGD(eDebugResourceManager, "\tExynos composition range [%d] - [%d], highPriorityNum[%d]",
                mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex, highPriorityNum);

        if (m2mMPP->mLogicalType == MPP_LOGICAL_G2D_COMBO && maxPriorityIndex >= 0) {
            startIndex = mExynosCompositionInfo.mFirstIndex;
            endIndex = mExynosCompositionInfo.mLastIndex;

            for (int32_t i = startIndex; i <= endIndex; i++) {
                if (mLayers[i]->mOverlayPriority == ePriorityMax ||
                        mLayers[i]->mValidateCompositionType == HWC2_COMPOSITION_CLIENT)
                    continue;
                mLayers[i]->resetAssignedResource();
                mLayers[i]->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
                if ((ret = addClientCompositionLayer(i)) < 0)
                    return ret;
                changeFlag |= ret;
            }

            if (mLayers[maxPriorityIndex]->mValidateCompositionType
                    != HWC2_COMPOSITION_EXYNOS) {
                mLayers[maxPriorityIndex]->mValidateCompositionType = HWC2_COMPOSITION_EXYNOS;
                mLayers[maxPriorityIndex]->resetAssignedResource();
                if ((ret = m2mMPP->assignMPP(this, mLayers[maxPriorityIndex])) != NO_ERROR)
                {
                    ALOGE("%s:: %s MPP assignMPP() error (%d)",
                            __func__, m2mMPP->mName.string(), ret);
                    return ret;
                }
            }

            mExynosCompositionInfo.mFirstIndex = maxPriorityIndex;
            mExynosCompositionInfo.mLastIndex = maxPriorityIndex;
        }

        /* Check if exynos comosition nests GLES composition */
        if ((mClientCompositionInfo.mHasCompositionLayer) &&
            (mExynosCompositionInfo.mFirstIndex < mClientCompositionInfo.mFirstIndex) &&
            (mClientCompositionInfo.mFirstIndex < mExynosCompositionInfo.mLastIndex) &&
            (mExynosCompositionInfo.mFirstIndex < mClientCompositionInfo.mLastIndex) &&
            (mClientCompositionInfo.mLastIndex < mExynosCompositionInfo.mLastIndex)) {

            if ((mClientCompositionInfo.mFirstIndex - mExynosCompositionInfo.mFirstIndex) <
                (mExynosCompositionInfo.mLastIndex - mClientCompositionInfo.mLastIndex)) {
                mLayers[mExynosCompositionInfo.mFirstIndex]->resetAssignedResource();
                mLayers[mExynosCompositionInfo.mFirstIndex]->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
                if ((ret = addClientCompositionLayer(mExynosCompositionInfo.mFirstIndex)) < 0)
                    return ret;
                mExynosCompositionInfo.mFirstIndex = mClientCompositionInfo.mLastIndex + 1;
                changeFlag |= ret;
            } else {
                mLayers[mExynosCompositionInfo.mLastIndex]->resetAssignedResource();
                mLayers[mExynosCompositionInfo.mLastIndex]->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
                if ((ret = addClientCompositionLayer(mExynosCompositionInfo.mLastIndex)) < 0)
                    return ret;
                mExynosCompositionInfo.mLastIndex = (mClientCompositionInfo.mFirstIndex - 1);
                changeFlag |= ret;
            }
        }
    }

    if (highPriorityNum > 0 && (m2mMPP->mLogicalType != MPP_LOGICAL_G2D_COMBO)) {
        for (uint32_t i = 0; i < highPriorityNum; i++) {
            if (highPriority[i] == mExynosCompositionInfo.mFirstIndex)
                mExynosCompositionInfo.mFirstIndex++;
            else if (highPriority[i] == mExynosCompositionInfo.mLastIndex)
                mExynosCompositionInfo.mLastIndex--;
        }
    }

    if ((mExynosCompositionInfo.mFirstIndex < 0) ||
        (mExynosCompositionInfo.mFirstIndex >= (int)mLayers.size()) ||
        (mExynosCompositionInfo.mLastIndex < 0) ||
        (mExynosCompositionInfo.mLastIndex >= (int)mLayers.size()) ||
        (mExynosCompositionInfo.mFirstIndex > mExynosCompositionInfo.mLastIndex))
    {
        DISPLAY_LOGD(eDebugResourceManager, "\texynos composition is disabled, because of invalid index (%d, %d), size(%zu)",
                mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex, mLayers.size());
        mExynosCompositionInfo.initializeInfos(this);
        changeFlag = EXYNOS_ERROR_CHANGED;
    }

    for (uint32_t i = 0; i < highPriorityNum; i++) {
        if ((mExynosCompositionInfo.mFirstIndex < highPriority[i]) &&
            (highPriority[i] < mExynosCompositionInfo.mLastIndex)) {
            highPriorityCheck = 1;
            break;
        }
    }


    if (highPriorityCheck && (m2mMPP->mLogicalType != MPP_LOGICAL_G2D_COMBO)) {
        startIndex = mExynosCompositionInfo.mFirstIndex;
        endIndex = mExynosCompositionInfo.mLastIndex;
        DISPLAY_LOGD(eDebugResourceManager, "\texynos composition is disabled because of sandwitched max priority layer (%d, %d)",
                mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex);
        mExynosCompositionInfo.initializeInfos(this);
        changeFlag = EXYNOS_ERROR_CHANGED;

        for (int32_t i = startIndex; i <= endIndex; i++) {
            int32_t checkPri = 0;
            for (uint32_t j = 0; j < highPriorityNum; j++) {
                if (i == highPriority[j]) {
                    checkPri = 1;
                    break;
                }
            }

            if (checkPri)
                continue;

            mLayers[i]->resetAssignedResource();
            mLayers[i]->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
            if ((ret = addClientCompositionLayer(i)) < 0)
                HWC_LOGE(this, "%d layer: addClientCompositionLayer() fail", i);
        }
    }

    DISPLAY_LOGD(eDebugResourceManager, "\tresult changeFlag(0x%8x)", changeFlag);
    DISPLAY_LOGD(eDebugResourceManager, "\tClient composition range [%d] - [%d]",
            mClientCompositionInfo.mFirstIndex, mClientCompositionInfo.mLastIndex);
    DISPLAY_LOGD(eDebugResourceManager, "\tExynos composition range [%d] - [%d]",
            mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex);

    /*
     * assignCompositionTarget() calls updateUsedCapacity()
     * if changeFlag is EXYNOS_ERROR_CHANGED
     * */
    if (changeFlag != EXYNOS_ERROR_CHANGED)
        m2mMPP->updateUsedCapacity();

    return changeFlag;
}

bool ExynosDisplay::windowUpdateExceptions()
{

    if (mExynosCompositionInfo.mHasCompositionLayer) {
        DISPLAY_LOGD(eDebugWindowUpdate, "has exynos composition");
        return true;
    }
    if (mClientCompositionInfo.mHasCompositionLayer) {
        DISPLAY_LOGD(eDebugWindowUpdate, "has client composition");
        return true;
    }

    for (size_t i = 0; i < mLayers.size(); i++) {
        if (mLayers[i]->mM2mMPP != NULL) return true;
        if (mLayers[i]->mLayerBuffer == NULL) return true;
        if (mLayers[i]->mTransform != 0) return true;
    }

    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        struct decon_win_config config = mWinConfigData->config[i];
        if (config.state == config.DECON_WIN_STATE_BUFFER) {
            if (config.src.w/config.dst.w != 1 || config.src.h/config.dst.h != 1) {
                DISPLAY_LOGD(eDebugWindowUpdate, "Skip reason : scaled");
                return true;
            }
        }
    }

    return false;
}

int ExynosDisplay::handleWindowUpdate()
{
    int ret = NO_ERROR;

    // TODO will be implemented
    size_t winUpdateInfoIdx = DECON_WIN_UPDATE_IDX;
    struct decon_win_config *config = mWinConfigData->config;
    unsigned int excp;

    config[winUpdateInfoIdx].state = config[winUpdateInfoIdx].DECON_WIN_STATE_DISABLED;

    if (exynosHWCControl.windowUpdate != 1) return 0;

    /* exceptions */
    if (windowUpdateExceptions())
        return 0;

    if (mContentFlags & HWC_GEOMETRY_CHANGED) {
        DISPLAY_LOGD(eDebugWindowUpdate, "HWC2: GEOMETRY chnaged");
        return 0;
    }

    hwc_rect mergedRect = {(int)mXres, (int)mYres, 0, 0};
    hwc_rect damageRect = {(int)mXres, (int)mYres, 0, 0};

    for (size_t i = 0; i < mLayers.size(); i++) {
        excp = getLayerRegion(mLayers[i], &damageRect, eDamageRegionByDamage);
        if (excp == eDamageRegionPartial) {
            DISPLAY_LOGD(eDebugWindowUpdate, "layer(%zu) partial : %d, %d, %d, %d", i,
                    damageRect.left, damageRect.top, damageRect.right, damageRect.bottom);
            mergedRect = expand(mergedRect, damageRect);
        }
        else if (excp == eDamageRegionSkip) {
            if ((ret = checkConfigDstChanged(*mWinConfigData, *mLastWinConfigData, i)) < 0) {
                return 0;
            } else if (ret > 0) {
                damageRect.left = mLayers[i]->mDisplayFrame.left;
                damageRect.right = mLayers[i]->mDisplayFrame.right;
                damageRect.top = mLayers[i]->mDisplayFrame.top;
                damageRect.bottom = mLayers[i]->mDisplayFrame.bottom;
                DISPLAY_LOGD(eDebugWindowUpdate, "Skip layer (origin) : %d, %d, %d, %d",
                        damageRect.left, damageRect.top, damageRect.right, damageRect.bottom);
                mergedRect = expand(mergedRect, damageRect);
                hwc_rect prevDst = {mLastWinConfigData->config[i].dst.x, mLastWinConfigData->config[i].dst.y,
                    mLastWinConfigData->config[i].dst.x + (int)mLastWinConfigData->config[i].dst.w,
                    mLastWinConfigData->config[i].dst.y + (int)mLastWinConfigData->config[i].dst.h};
                mergedRect = expand(mergedRect, prevDst);
            } else {
                DISPLAY_LOGD(eDebugWindowUpdate, "layer(%zu) skip", i);
                continue;
            }
        }
        else if (excp == eDamageRegionFull) {
            damageRect.left = mLayers[i]->mDisplayFrame.left;
            damageRect.top = mLayers[i]->mDisplayFrame.top;
            damageRect.right = mLayers[i]->mDisplayFrame.left + mLayers[i]->mDisplayFrame.right;
            damageRect.bottom = mLayers[i]->mDisplayFrame.top + mLayers[i]->mDisplayFrame.bottom;
            DISPLAY_LOGD(eDebugWindowUpdate, "Full layer update : %d, %d, %d, %d", mLayers[i]->mDisplayFrame.left,
                    mLayers[i]->mDisplayFrame.top, mLayers[i]->mDisplayFrame.right, mLayers[i]->mDisplayFrame.bottom);
            mergedRect = expand(mergedRect, damageRect);
        }
        else {
            DISPLAY_LOGD(eDebugWindowUpdate, "Partial canceled, Skip reason (layer %zu) : %d", i, excp);
            return 0;
        }
    }

    if (mergedRect.left != (int32_t)mXres || mergedRect.right != 0 ||
        mergedRect.top != (int32_t)mYres || mergedRect.bottom != 0) {
        DISPLAY_LOGD(eDebugWindowUpdate, "Partial(origin) : %d, %d, %d, %d",
                mergedRect.left, mergedRect.top, mergedRect.right, mergedRect.bottom);
    } else {
        return 0;
    }

    unsigned int blockWidth, blockHeight;

    if (mDSCHSliceNum != 0 && mDSCYSliceSize != 0) {
        blockWidth = mXres/mDSCHSliceNum;
        blockHeight = mDSCYSliceSize;
    } else {
        blockWidth = 2;
        blockHeight = 2;
    }

    DISPLAY_LOGD(eDebugWindowUpdate, "DSC block size (for align) : %d, %d", blockWidth, blockHeight);

    if (mergedRect.left%blockWidth != 0)
        mergedRect.left = pixel_align_down(mergedRect.left, blockWidth);
    if (mergedRect.left < 0) mergedRect.left = 0;

    if (mergedRect.right%blockWidth != 0)
        mergedRect.right = pixel_align(mergedRect.right, blockWidth);
    if (mergedRect.right > (int32_t)mXres) mergedRect.right = mXres;

    if (mergedRect.top%blockHeight != 0)
        mergedRect.top = pixel_align_down(mergedRect.top, blockHeight);
    if (mergedRect.top < 0) mergedRect.top = 0;

    if (mergedRect.bottom%blockHeight != 0)
        mergedRect.bottom = pixel_align(mergedRect.bottom, blockHeight);
    if (mergedRect.bottom > (int32_t)mYres) mergedRect.bottom = mYres;

    if (mergedRect.left == 0 && mergedRect.right == (int32_t)mXres &&
            mergedRect.top == 0 && mergedRect.bottom == (int32_t)mYres) {
        DISPLAY_LOGD(eDebugWindowUpdate, "Partial(aligned) : Full size");
        config[winUpdateInfoIdx].state = config[winUpdateInfoIdx].DECON_WIN_STATE_UPDATE;
        config[winUpdateInfoIdx].dst.x = 0;
        config[winUpdateInfoIdx].dst.w = mXres; 
        config[winUpdateInfoIdx].dst.y = 0;
        config[winUpdateInfoIdx].dst.h = mYres;
        DISPLAY_LOGD(eDebugWindowUpdate, "window update end ------------------");
        return 0;
    }

    if (mergedRect.left != (int32_t)mXres && mergedRect.right != 0 &&
            mergedRect.top != (int32_t)mYres && mergedRect.bottom != 0) {
        DISPLAY_LOGD(eDebugWindowUpdate, "Partial(aligned) : %d, %d, %d, %d",
                mergedRect.left, mergedRect.top, mergedRect.right, mergedRect.bottom);

        config[winUpdateInfoIdx].state = config[winUpdateInfoIdx].DECON_WIN_STATE_UPDATE;
        config[winUpdateInfoIdx].dst.x = mergedRect.left;
        config[winUpdateInfoIdx].dst.w = WIDTH(mergedRect);
        config[winUpdateInfoIdx].dst.y = mergedRect.top;
        config[winUpdateInfoIdx].dst.h = HEIGHT(mergedRect);
    }
    else {
        DISPLAY_LOGD(eDebugWindowUpdate, "Partial canceled, All layer skiped" );
    }

    DISPLAY_LOGD(eDebugWindowUpdate, "window update end ------------------");
    return 0;
}

unsigned int ExynosDisplay::getLayerRegion(ExynosLayer *layer, hwc_rect *rect_area, uint32_t regionType) {
    hwc_rect_t const *hwcRects = NULL;
    size_t numRects = 0;

    rect_area->left = INT_MAX;
    rect_area->top = INT_MAX;
    rect_area->right = rect_area->bottom = 0;

    hwcRects = layer->mSurfaceDamage.rects;
    numRects = layer->mSurfaceDamage.numRects;

    if ((numRects == 0) || (hwcRects == NULL))
        return eDamageRegionFull;

    if ((numRects == 1) && (hwcRects[0].left == 0) && (hwcRects[0].top == 0) &&
            (hwcRects[0].right == 0) && (hwcRects[0].bottom == 0))
        return eDamageRegionSkip;

    switch (regionType) {
    case eDamageRegionByDamage:
        if (hwcRects != NULL) {
            for (size_t j = 0; j < numRects; j++) {
                hwc_rect_t rect;

                if ((hwcRects[j].left < 0) || (hwcRects[j].top < 0) ||
                    (hwcRects[j].right < 0) || (hwcRects[j].bottom < 0) ||
                    (hwcRects[j].left >= hwcRects[j].right) || (hwcRects[j].top >= hwcRects[j].bottom) ||
                    (hwcRects[j].right - hwcRects[j].left > WIDTH(layer->mSourceCrop)) ||
                    (hwcRects[j].bottom - hwcRects[j].top > HEIGHT(layer->mSourceCrop))) {
                    rect_area->left = INT_MAX;
                    rect_area->top = INT_MAX;
                    rect_area->right = rect_area->bottom = 0;
                    return eDamageRegionFull;
                }

                rect.left = layer->mDisplayFrame.left + hwcRects[j].left - layer->mSourceCrop.left;
                rect.top = layer->mDisplayFrame.top + hwcRects[j].top - layer->mSourceCrop.top;
                rect.right = layer->mDisplayFrame.left + hwcRects[j].right - layer->mSourceCrop.left;
                rect.bottom = layer->mDisplayFrame.top + hwcRects[j].bottom - layer->mSourceCrop.top;
                DISPLAY_LOGD(eDebugWindowUpdate, "Display frame : %d, %d, %d, %d", layer->mDisplayFrame.left,
                        layer->mDisplayFrame.top, layer->mDisplayFrame.right, layer->mDisplayFrame.bottom);
                DISPLAY_LOGD(eDebugWindowUpdate, "hwcRects : %d, %d, %d, %d", hwcRects[j].left,
                        hwcRects[j].top, hwcRects[j].right, hwcRects[j].bottom);
                adjustRect(rect, INT_MAX, INT_MAX);
                /* Get sums of rects */
                *rect_area = expand(*rect_area, rect);
            }
        }
        return eDamageRegionPartial;
        break;
    case eDamageRegionByLayer:
        if (layer->mLastLayerBuffer != layer->mLayerBuffer)
            return eDamageRegionFull;
        else
            return eDamageRegionSkip;
        break;
    default:
        HWC_LOGE(this, "%s:: Invalid regionType (%d)", __func__, regionType);
        return eDamageRegionError;
        break;
    }

    return eDamageRegionFull;
}

ExynosMPP* ExynosDisplay::getExynosMPPForDma(decon_idma_type idma)
{
    switch(idma) {
        case IDMA_G0:
        case IDMA_G1:
            return ExynosResourceManager::getExynosMPP(MPP_LOGICAL_DPP_G);
        case IDMA_VG0:
        case IDMA_VG1:
            return ExynosResourceManager::getExynosMPP(MPP_LOGICAL_DPP_VG);
        case IDMA_VGF0:
        case IDMA_VGF1:
            return ExynosResourceManager::getExynosMPP(MPP_LOGICAL_DPP_VGF);
        default:
            return NULL;
    }
}

uint32_t ExynosDisplay::getRestrictionIndex(int format)
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
        case DECON_PIXEL_FORMAT_RGBA_5551:
        case DECON_PIXEL_FORMAT_RGB_565:
            return RESTRICTION_RGB;
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
            return RESTRICTION_YUV;
        default:
            return RESTRICTION_MAX;
    }
}

void ExynosDisplay::closeFences()
{
    struct decon_win_config *config = mWinConfigData->config;
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        if (config[i].fence_fd != -1)
            fence_close(config[i].fence_fd, this,
                    FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP);
        config[i].fence_fd = -1;
    }
    for (size_t i = 0; i < mLayers.size(); i++) {
        if (mLayers[i]->mReleaseFence > 0) {
            fence_close(mLayers[i]->mReleaseFence, this,
                    FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP);
            mLayers[i]->mReleaseFence = -1;
        }
        if ((mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_DEVICE) &&
            (mLayers[i]->mM2mMPP != NULL)) {
            mLayers[i]->mM2mMPP->closeFences();
        }
    }
    if (mExynosCompositionInfo.mHasCompositionLayer) {
        if (mExynosCompositionInfo.mM2mMPP == NULL)
        {
            DISPLAY_LOGE("There is exynos composition, but m2mMPP is NULL");
            return;
        }
        mExynosCompositionInfo.mM2mMPP->closeFences();
    }

    for (size_t i=0; i < mLayers.size(); i++) {
        if (mLayers[i]->mAcquireFence != -1) {
            mLayers[i]->mAcquireFence = fence_close(mLayers[i]->mAcquireFence, this,
                    FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
        }
    }

    mExynosCompositionInfo.mAcquireFence = fence_close(mExynosCompositionInfo.mAcquireFence, this,
            FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_G2D);
    mClientCompositionInfo.mAcquireFence = fence_close(mClientCompositionInfo.mAcquireFence, this,
            FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB);

    if (mWinConfigData->fence > 0)
        fence_close(mWinConfigData->fence, this, FENCE_TYPE_RETIRE, FENCE_IP_DPP);
    mWinConfigData->fence = -1;
}

int32_t ExynosDisplay::getHdrCapabilities(int* outNum, int* outTypes, float* maxLuminance,
        float* maxAverageLuminance, float* minLuminance)
{
    if (outTypes == NULL) {
        struct decon_hdr_capabilities_info outInfo;
        memset(&outInfo, 0, sizeof(outInfo));

        if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES_NUM, &outInfo) < 0) {
            ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES_NUM ioctl failed");
            return -1;
        }

        *maxLuminance = (float)outInfo.max_luminance / (float)10000;
        *maxAverageLuminance = (float)outInfo.max_average_luminance / (float)10000;
        *minLuminance = (float)outInfo.min_luminance / (float)10000;
        *outNum = outInfo.out_num;
        return 0;
    }

    struct decon_hdr_capabilities outData;
    memset(&outData, 0, sizeof(outData));

    for (int i = 0; i < *outNum ; i += SET_HDR_CAPABILITIES_NUM) {
        if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES, &outData) < 0) {
            ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES ioctl Failed");
            return -1;
        }
        for (int j = 0; j < *outNum - i; j++)
            outTypes[i+j] = outData.out_types[j];
    }

    return 0;
}
