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

//#define LOG_NDEBUG 0
//#include "ExynosHWC.h"
//#include "ExynosHWCUtils.h"
//#include "ExynosMPPModule.h"
#include <hardware/hwcomposer.h>
#include "ExynosExternalDisplay.h"
#include "../libdevice/ExynosDevice.h"
#include "displayport_for_hwc.h"
#include <errno.h>
#include "ExynosHWC1Adaptor.h"
#include "ExynosLayer.h"
#include "ExynosHWCHelper.h"
#include "ExynosHWCDebug.h"
// temp library
#include <linux/fb.h>

#define SKIP_FRAME_COUNT 3

extern struct v4l2_dv_timings dv_timings[];

bool is_same_dv_timings(const struct v4l2_dv_timings *t1,
        const struct v4l2_dv_timings *t2)
{
    if (t1->type == t2->type &&
            t1->bt.width == t2->bt.width &&
            t1->bt.height == t2->bt.height &&
            t1->bt.interlaced == t2->bt.interlaced &&
            t1->bt.polarities == t2->bt.polarities &&
            t1->bt.pixelclock == t2->bt.pixelclock &&
            t1->bt.hfrontporch == t2->bt.hfrontporch &&
            t1->bt.vfrontporch == t2->bt.vfrontporch &&
            t1->bt.vsync == t2->bt.vsync &&
            t1->bt.vbackporch == t2->bt.vbackporch &&
            (!t1->bt.interlaced ||
             (t1->bt.il_vfrontporch == t2->bt.il_vfrontporch &&
              t1->bt.il_vsync == t2->bt.il_vsync &&
              t1->bt.il_vbackporch == t2->bt.il_vbackporch)))
        return true;
    return false;
}

int ExynosExternalDisplay::getDVTimingsIndex(int preset)
{
    for (int i = 0; i < SUPPORTED_DV_TIMINGS_NUM; i++) {
        if (preset == preset_index_mappings[i].preset)
            return preset_index_mappings[i].dv_timings_index;
    }
    return -1;
}

ExynosExternalDisplay::ExynosExternalDisplay(uint32_t __unused type, ExynosDevice *device)
    :   ExynosDisplay(HWC_DISPLAY_EXTERNAL, device)
{
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);

    mEnabled = false;
    mBlanked = false;

	mXres = 0;
    mYres = 0;
    mXdpi = 0;
    mYdpi = 0;
    mVsyncPeriod = 0;
    mDisplayId = HWC_DISPLAY_EXTERNAL;
    mSkipStartFrame = 0;
    mSkipFrameCount = -1;
    mIsSkipFrame = false;
    mActiveConfigIndex = 0;
    mVirtualDisplayState = 0;

    //TODO : Hard coded currently
    mNumMaxPriorityAllowed = 1;

    mPowerModeState = (hwc2_power_mode_t)HWC_POWER_MODE_OFF;
    mDisplayName = android::String8("ExternalDisplay");
    mDisplayFd = hwcFdClose(mDisplayFd);
}

ExynosExternalDisplay::~ExynosExternalDisplay()
{

}

void ExynosExternalDisplay::init()
{

}

void ExynosExternalDisplay::deInit()
{

}

void ExynosExternalDisplay::startConnection()
{
    if (mBlanked == false)
        return;

    exynos_displayport_data dp_data;
    dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_RECONNECTION;

    if(ioctl(mDisplayFd, EXYNOS_SET_DISPLAYPORT_CONFIG, &dp_data) < 0)
        ALOGE("failed to DP reconnection");

    mBlanked = false;
}

int ExynosExternalDisplay::openExternalDisplay()
{
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);

    int ret = 0;

    //mPlugState = true;

	if (mDisplayFd > 0){
        ALOGE("already mDisplayFd is opened");
        ret = mDisplayFd;
    }
    else {
        mDisplayFd = open(DECON_EXTERNAL_DEV_NAME, O_RDWR);
        if (mDisplayFd < 0) {
            ALOGE("failed to open framebuffer for externalDisplay");
        }
        ret = mDisplayFd;
    }

    if (setVsyncEnabled(1) == HWC2_ERROR_NONE)
        mDevice->mExtVsyncEnabled = 1;
    else
        DISPLAY_LOGE("Vsync change is failed");

    mSkipFrameCount = SKIP_FRAME_COUNT;
    mSkipStartFrame = 0;
    initDisplay();
    mActiveConfigIndex = 0;

    ALOGV("open fd for External Display(%d)", ret);

    return ret;
}

void ExynosExternalDisplay::closeExternalDisplay()
{
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);

    mDevice->mExtVsyncEnabled = 0;
    setVsyncEnabled(0);

    if (this->mPowerModeState != (hwc2_power_mode_t)HWC_POWER_MODE_OFF) {
        if (ioctl(mDisplayFd, FBIOBLANK, FB_BLANK_POWERDOWN) < 0) {
            ALOGE("%s: set powermode ioctl failed errno : %d", __func__, errno);
            return;
        }
    }

    this->mPowerModeState = (hwc2_power_mode_t)HWC_POWER_MODE_OFF;

    mDisplayFd = hwcFdClose(mDisplayFd);
    DISPLAY_LOGD(eExternalDisplay, "Close fd for External Display");

    mDisplayFd = -1;
    mEnabled = false;
    mBlanked = false;
    mSkipFrameCount = SKIP_FRAME_COUNT;
}


void ExynosExternalDisplay::setResolution()
{
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);
}

bool ExynosExternalDisplay::isPresetSupported(unsigned int __unused preset)
{
    return false;
}

int ExynosExternalDisplay::getConfig()
{
    if (!mDevice->hpd_status)
        return -1;

    exynos_displayport_data dp_data;
    int dv_timings_index = 0;

    dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_PRESET;
    if (ioctl(this->mDisplayFd, EXYNOS_GET_DISPLAYPORT_CONFIG, &dp_data) < 0) {
        ALOGE("%s: g_dv_timings error, %d", __func__, errno);
        return -1;
    }

    mActiveConfigIndex = 0;

    for (int i = 0; i < SUPPORTED_DV_TIMINGS_NUM; i++) {
        dv_timings_index = preset_index_mappings[i].dv_timings_index;
        if (is_same_dv_timings(&dp_data.timings, &dv_timings[dv_timings_index])) {
            float refreshRate = (float)((float)dp_data.timings.bt.pixelclock /
                    ((dp_data.timings.bt.width + dp_data.timings.bt.hfrontporch + dp_data.timings.bt.hsync + dp_data.timings.bt.hbackporch) *
                     (dp_data.timings.bt.height + dp_data.timings.bt.vfrontporch + dp_data.timings.bt.vsync + dp_data.timings.bt.vbackporch)));
            mXres = dp_data.timings.bt.width;
            mYres = dp_data.timings.bt.height;
            mVsyncPeriod = 1000000000 / refreshRate;
            mExternalDisplayResolution = preset_index_mappings[i].preset;
            break;
        }
    }
    ALOGV("DP resolution is (%d x %d)", mXres, mYres);

    return 0;
}
int ExynosExternalDisplay::getDisplayConfigs(uint32_t* outNumConfigs, hwc2_config_t* outConfigs)
{
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);

    int ret = 0;
    int k, mConfigSize;

    hwc2_config_t temp_Config;

    if (!mDevice->hpd_status)
        return -1;

    exynos_displayport_data dp_data;
    size_t index = 0;

    cleanConfigurations();

    /* configs store the index of mConfigurations */
    dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_ENUM_PRESET;
    while (index < (*outNumConfigs)) {
        dp_data.etimings.index = index;
        ret = ioctl(this->mDisplayFd, EXYNOS_GET_DISPLAYPORT_CONFIG, &dp_data);

        if (ret < 0) {
            if (errno == EINVAL) {
                ALOGV("%s:: Unmatched config index %zu", __func__, index);
                index++;
                continue;
            }
            else if (errno == E2BIG) {
                ALOGV("%s:: Total configurations %zu", __func__, index);
                break;
            }
            ALOGE("%s: enum_dv_timings error, %d", __func__, errno);
            return -1;
        }

        for (size_t i = 0; i < SUPPORTED_DV_TIMINGS_NUM; i++) {
            int dv_timings_index = preset_index_mappings[i].dv_timings_index;
            if (is_same_dv_timings(&dp_data.etimings.timings, &dv_timings[dv_timings_index])) {
		        ALOGV("config added. [index : %zu]", index);
                mConfigurations.push_back(dv_timings_index);
                outConfigs[mConfigurations.size() - 1] = dv_timings_index;
                break;
            }
        }
        index++;
    }

    if (mConfigurations.size() == 0){
        ALOGE("%s: do not receivce any configuration info", __func__);
        closeExternalDisplay();
        return -1;
    }
    temp_Config = outConfigs[mConfigurations.size() - 1];
    outConfigs[mConfigurations.size() - 1] = outConfigs[0];
    outConfigs[0] = temp_Config;

    mConfigSize = (int)mConfigurations.size();

    if (!mDevice->mResolutionHandled) {
        for (k = 0; k < mConfigSize-1; k++) {
            if (outConfigs[k] == mActiveConfigIndex)
            break;
        }
        mDevice->mResolutionHandled = true;
        temp_Config = outConfigs[0];
        outConfigs[0] = mActiveConfigIndex;
        outConfigs[k] = temp_Config;
    }

    dp_data.timings = dv_timings[outConfigs[0]];
	dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_PRESET;
    if(ioctl(this->mDisplayFd, EXYNOS_SET_DISPLAYPORT_CONFIG, &dp_data) <0) {
        ALOGE("%s: fail to send selected config data, %d", __func__, errno);
        return -1;
    }

    mXres = dv_timings[outConfigs[0]].bt.width;
    mYres = dv_timings[outConfigs[0]].bt.height;
    ALOGV("DP resolution is (%d x %d)", mXres, mYres);
    *outNumConfigs = mConfigurations.size();
    dumpConfigurations();
    return 0;
}

int32_t ExynosExternalDisplay::getActiveConfig(
        hwc2_config_t* outConfig) {
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);

    if (!mDevice->hpd_status)
        return -1;

    outConfig = &mActiveConfigIndex;

    return HWC2_ERROR_NONE;
}

int32_t ExynosExternalDisplay::setActiveConfig(
        hwc2_config_t config) {
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);

    if (!mDevice->hpd_status)
        return -1;
    /* Find Preset with index*/

    mActiveConfigIndex = config;
    mDevice->mResolutionChanged = true;
    return 0;
}

void ExynosExternalDisplay::hotplug(){
    ALOGE("HWC2 : %s : %d ", __func__, __LINE__);

    hwc2_callback_data_t callbackData =
        mDevice->mCallbackInfos[HWC2_CALLBACK_HOTPLUG].callbackData;
    HWC2_PFN_HOTPLUG callbackFunc =
        (HWC2_PFN_HOTPLUG)mDevice->mCallbackInfos[HWC2_CALLBACK_HOTPLUG].funcPointer;
    if (callbackData != NULL && callbackFunc != NULL)
        callbackFunc(callbackData, HWC_DISPLAY_EXTERNAL, mDevice->hpd_status);
}

bool ExynosExternalDisplay::checkRotate()
{
    for (size_t i = 0; i < mLayers.size(); i++) {
        ExynosLayer *layer = mLayers[i];

#ifdef TARGET_USES_HWC2
        if (layer->mCompositionType == HWC2_COMPOSITION_SCREENSHOT) {
#else
        if (layer->mLayerFlag & EXTERNAL_DISPLAY_SKIP_LAYER) {
#endif
            ALOGV("include rotation animation layer");
            layer->mOverlayInfo = eSkipRotateAnim;
            for (size_t j = 0; j < mLayers.size(); j++) {
                ExynosLayer *skipLayer = mLayers[j];
                skipLayer->mValidateCompositionType = HWC2_COMPOSITION_DEVICE;
            }
            mIsSkipFrame = true;
            return true;
        }
    }
    mIsSkipFrame = false;
    return false;
}

int32_t ExynosExternalDisplay::validateDisplay(
        uint32_t* outNumTypes, uint32_t* outNumRequests) {
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);
    int32_t ret;

    if(mDevice->mResolutionChanged){
        setResolution();
        mDevice->mResolutionChanged = false;
        mDevice->mResolutionHandled = false;
        mDevice->hpd_status = false;

        this->setPowerMode(HWC_POWER_MODE_OFF);

        mSkipStartFrame = 0;
        mSkipFrameCount = SKIP_FRAME_COUNT;

        hwc2_callback_data_t callbackData =
        mDevice->mCallbackInfos[HWC2_CALLBACK_HOTPLUG].callbackData;
        HWC2_PFN_HOTPLUG callbackFunc =
            (HWC2_PFN_HOTPLUG)mDevice->mCallbackInfos[HWC2_CALLBACK_HOTPLUG].funcPointer;

        if (callbackData != NULL && callbackFunc != NULL)
            callbackFunc(callbackData, HWC_DISPLAY_EXTERNAL, mDevice->hpd_status);
        mDevice->invalidate();
    }

    if (checkRotate()) {
        mClientCompositionInfo.initializeInfos(this);
        mExynosCompositionInfo.initializeInfos(this);
        return HWC2_ERROR_NONE;
    }

    ret = ExynosDisplay::validateDisplay(outNumTypes, outNumRequests);

    if ((mSkipStartFrame < SKIP_EXTERNAL_FRAME) || (mVirtualDisplayState)) {
        initDisplay();
        for (size_t i = 0; i < mLayers.size(); i++) {
            ExynosLayer *layer = mLayers[i];
            if (layer && (layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE ||
                layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS)) {
                layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
                layer->mReleaseFence = layer->mAcquireFence;
            }
        }
    }

    return ret;
}

int32_t ExynosExternalDisplay::presentDisplay(
    int32_t* outRetireFence)
{
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);
    int32_t ret;

    if (mIsSkipFrame) {
        *outRetireFence = -1;
        for (size_t i = 0; i < mLayers.size(); i++) {
            ExynosLayer *layer = mLayers[i];
            layer->mAcquireFence = fence_close(layer->mAcquireFence, this,
                    FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
            layer->mReleaseFence = -1;
        }
        mClientCompositionInfo.mAcquireFence =
            fence_close(mClientCompositionInfo.mAcquireFence, this,
                    FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB);
        mClientCompositionInfo.mReleaseFence = -1;

        /* this frame is not presented, but mRenderingState is updated to RENDERING_STATE_PRESENTED */
        mRenderingState = RENDERING_STATE_PRESENTED;
        return HWC2_ERROR_NONE;
    }

    ret = ExynosDisplay::presentDisplay(outRetireFence);

    return ret;
}

void ExynosExternalDisplay::cleanConfigurations()
{
    mConfigurations.clear();
}

void ExynosExternalDisplay::dumpConfigurations()
{
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);

    ALOGI("External display configurations:: total(%zu), active configuration(%d)",
            mConfigurations.size(), mActiveConfigIndex);
    for (size_t i = 0; i <  mConfigurations.size(); i++ ) {
        unsigned int dv_timings_index = preset_index_mappings[mConfigurations[i]].dv_timings_index;
        v4l2_dv_timings configuration = dv_timings[dv_timings_index];
        float refresh_rate = (float)((float)configuration.bt.pixelclock /
                ((configuration.bt.width + configuration.bt.hfrontporch + configuration.bt.hsync + configuration.bt.hbackporch) *
                 (configuration.bt.height + configuration.bt.vfrontporch + configuration.bt.vsync + configuration.bt.vbackporch)));
        uint32_t vsyncPeriod = 1000000000 / refresh_rate;
        ALOGI("%zu : index(%d) type(%d), %d x %d, fps(%f), vsyncPeriod(%d)", i, dv_timings_index, configuration.type, configuration.bt.width,
                configuration.bt.height,
                refresh_rate, vsyncPeriod);
    }
}

int32_t ExynosExternalDisplay::getDisplayAttribute(
        hwc2_config_t config,
        int32_t /*hwc2_attribute_t*/ attribute, int32_t* outValue)
{
    if (config >= SUPPORTED_DV_TIMINGS_NUM) {
        ALOGE("%s:: Invalid config(%d), mConfigurations(%zu)", __func__, config, mConfigurations.size());
        return -EINVAL;
    }

    v4l2_dv_timings dv_timing = dv_timings[config];
    switch(attribute) {
    case HWC_DISPLAY_VSYNC_PERIOD:
        {
            float refreshRate = (float)((float)dv_timing.bt.pixelclock /
                    ((dv_timing.bt.width + dv_timing.bt.hfrontporch + dv_timing.bt.hsync + dv_timing.bt.hbackporch) *
                     (dv_timing.bt.height + dv_timing.bt.vfrontporch + dv_timing.bt.vsync + dv_timing.bt.vbackporch)));
            *outValue = (1000000000/refreshRate);
            break;
        }
    case HWC_DISPLAY_WIDTH:
        *outValue = dv_timing.bt.width;
        break;

    case HWC_DISPLAY_HEIGHT:
        *outValue = dv_timing.bt.height;
        break;

    case HWC_DISPLAY_DPI_X:
        *outValue = this->mXdpi;
        break;

    case HWC_DISPLAY_DPI_Y:
        *outValue = this->mYdpi;
        break;

    case HWC_DISPLAY_NO_ATTRIBUTE:
        return HWC2_ERROR_NONE;

    default:
        ALOGE("unknown display attribute %u", attribute);
        return -1;
    }

    return HWC2_ERROR_NONE;
}

int ExynosExternalDisplay::enable()
{
    if (mEnabled)
        return HWC2_ERROR_NONE;

    if ((mDisplayFd < 0) && (openExternalDisplay() < 0))
        return HWC2_ERROR_UNSUPPORTED;

    if (ioctl(mDisplayFd, FBIOBLANK, FB_BLANK_UNBLANK) < 0){
        ALOGE("%s: set powermode ioctl failed errno : %d", __func__, errno);
        return HWC2_ERROR_UNSUPPORTED;
    }

    clearDisplay();

    mEnabled = true;

    ALOGI("HWC2 : %s : %d ", __func__, __LINE__);

    return HWC2_ERROR_NONE;
}

int ExynosExternalDisplay::disable()
{
    if (!mEnabled)
        return HWC2_ERROR_NONE;

    clearDisplay();

    if (ioctl(mDisplayFd, FBIOBLANK, FB_BLANK_POWERDOWN) < 0){
        ALOGE("%s: set powermode ioctl failed errno : %d", __func__, errno);
        return HWC2_ERROR_UNSUPPORTED;
    }

    this->mPowerModeState = (hwc2_power_mode_t)HWC_POWER_MODE_OFF;

    ALOGI("HWC2 : %s : %d ", __func__, __LINE__);

    return HWC2_ERROR_NONE;
}

int32_t ExynosExternalDisplay::setPowerMode(
        int32_t /*hwc2_power_mode_t*/ mode) {
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);


    /* TODO state check routine should be added */

    int fb_blank = 0;
    int err = 0;
    if (mode == HWC_POWER_MODE_OFF) {
        fb_blank = FB_BLANK_POWERDOWN;
        err = disable();
    } else {
        fb_blank = FB_BLANK_UNBLANK;
        err = enable();
    }

    if (err != 0) {
        ALOGE("set powermode ioctl failed errno : %d", errno);
        return HWC2_ERROR_UNSUPPORTED;
    }

    ALOGV("%s:: mode(%d), blank(%d)", __func__, mode, fb_blank);

    this->mPowerModeState = (hwc2_power_mode_t)mode;

    return HWC2_ERROR_NONE;
}

void ExynosExternalDisplay::setHdcpStatus(int status)
{
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);

    exynos_displayport_data dp_data;
    dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_HDCP;
    dp_data.hdcp = !!status;
    if (ioctl(mDisplayFd, EXYNOS_SET_DISPLAYPORT_CONFIG, &dp_data) < 0) {
        ALOGE("%s: failed to set HDCP status %d", __func__, errno);
    }
}

void ExynosExternalDisplay::setAudioChannel(uint32_t channels)
{
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);

    exynos_displayport_data dp_data;
    dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_AUDIO;
    dp_data.audio_info = channels;
    if (ioctl(mDisplayFd, EXYNOS_SET_DISPLAYPORT_CONFIG, &dp_data) < 0) {
        ALOGE("%s: failed to set audio channels %d", __func__, errno);
    }
}

uint32_t ExynosExternalDisplay::getAudioChannel()
{
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);

    int channels = 0;

    exynos_displayport_data dp_data;
    dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_AUDIO;
    if (ioctl(mDisplayFd, EXYNOS_GET_DISPLAYPORT_CONFIG, &dp_data) < 0) {
        ALOGE("%s: failed to get audio channels %d", __func__, errno);
    }
    channels = dp_data.audio_info;

    return channels;
}

int32_t ExynosExternalDisplay::setVsyncEnabled(
        int32_t /*hwc2_vsync_t*/ enabled) {

    __u32 val = !!enabled;

//    ALOGD("HWC2 : %s : %d %d", __func__, __LINE__, enabled);

    if (ioctl(mDisplayFd, S3CFB_SET_VSYNC_INT, &val) == -1) {
        ALOGE("vsync ioctl failed errno : %d", errno);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    return HWC2_ERROR_NONE;
}

int ExynosExternalDisplay::clearDisplay() {
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
            config[i].idma_type = IDMA_VG1;
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
        ALOGE("ioctl S3CFB_WIN_CONFIG failed to clear screen: %s",
                strerror(errno));

    win_data.fence = fence_close(win_data.fence, this,
            FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);

    memset(mLastWinConfigData, 0, sizeof(*mLastWinConfigData));
    mLastWinConfigData->fence = -1;
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        mLastWinConfigData->config[i].fence_fd = -1;
    }
    mRetireFence = -1;

    return ret;
}
