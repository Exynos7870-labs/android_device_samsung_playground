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

#include <chrono>

#include "ExynosHWCDebug.h"
#include "ExynosPrimaryDisplay.h"
#include "ExynosDevice.h"
#include "ExynosHWCHelper.h"
#include "ExynosExternalDisplay.h"
#include "ExynosDisplayFbInterfaceModule.h"

extern struct exynos_hwc_control exynosHWCControl;

constexpr auto nsecsPerSec = std::chrono::nanoseconds(std::chrono::seconds(1)).count();
constexpr auto nsecsPerMs = std::chrono::nanoseconds(std::chrono::milliseconds(1)).count();

static constexpr const char* PROPERTY_BOOT_MODE = "persist.vendor.display.primary.boot_config";

ExynosPrimaryDisplay::ExynosPrimaryDisplay(uint32_t index, ExynosDevice *device)
    :   ExynosDisplay(index, device)
{
    // There is no restriction in main display
    mNumMaxPriorityAllowed = MAX_DECON_WIN;

    /* Initialization */
    mType = HWC_DISPLAY_PRIMARY;
    mIndex = index;
    mDisplayId = getDisplayId(mType, mIndex);

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

#ifdef HIBER_EXIT_NODE_NAME
    mHiberState.hiberExitFd = fopen(HIBER_EXIT_NODE_NAME, "r");
    if(mHiberState.hiberExitFd != NULL) {
        ALOGI("%s is opened", HIBER_EXIT_NODE_NAME);
    } else {
        ALOGI("Hibernation exit node is not opened");
    }
#endif

#if defined(MAX_BRIGHTNESS_NODE_BASE) && defined(BRIGHTNESS_NODE_BASE)
    ALOGI("Trying %s open for get max brightness", MAX_BRIGHTNESS_NODE_BASE);
    std::ifstream ifsMaxBrightness(MAX_BRIGHTNESS_NODE_BASE);

    if (!ifsMaxBrightness.fail()) {
        ifsMaxBrightness >> mMaxBrightness;
        ALOGI("Max brightness : %d", mMaxBrightness);

        ifsMaxBrightness.close();

        ALOGI("Trying %s open for brightness control", BRIGHTNESS_NODE_BASE);
        mBrightnessOfs.open(BRIGHTNESS_NODE_BASE, std::ofstream::out);

        if (mBrightnessOfs.fail())
            ALOGI("%s open failed! %s", BRIGHTNESS_NODE_BASE, strerror(errno));

    } else {
        ALOGI("Brightness node is not opened");
    }
#endif

}

ExynosPrimaryDisplay::~ExynosPrimaryDisplay()
{
    if(mHiberState.hiberExitFd != NULL) {
        fclose(mHiberState.hiberExitFd);
        mHiberState.hiberExitFd = NULL;
    }

    if (mBrightnessOfs.is_open()) {
        mBrightnessOfs.close();
    }
}

void ExynosPrimaryDisplay::setDDIScalerEnable(int width, int height) {

    if (exynosHWCControl.setDDIScaler == false) return;

    ALOGI("DDISCALER Info : setDDIScalerEnable(w=%d,h=%d)", width, height);
    mNewScaledWidth = width;
    mNewScaledHeight = height;
    mXres = width;
    mYres = height;
}

int ExynosPrimaryDisplay::getDDIScalerMode(int width, int height) {

    if (exynosHWCControl.setDDIScaler == false) return 1;

    // Check if panel support support resolution or not.
    for (uint32_t i=0; i < mResolutionInfo.nNum; i++) {
        if (mResolutionInfo.nResolution[i].w * mResolutionInfo.nResolution[i].h ==
                static_cast<uint32_t>(width * height))
            return i + 1;
    }

    return 1; // WQHD
}

int32_t ExynosPrimaryDisplay::setBootDisplayConfig(int32_t config) {
    auto hwcConfig = static_cast<hwc2_config_t>(config);

    const auto &it = mDisplayConfigs.find(hwcConfig);
    if (it == mDisplayConfigs.end()) {
        DISPLAY_LOGE("%s: invalid config %d", __func__, config);
        return HWC2_ERROR_BAD_CONFIG;
    }

    const auto &mode = it->second;
    if (mode.vsyncPeriod == 0)
        return HWC2_ERROR_BAD_CONFIG;

    int refreshRate = round(nsecsPerSec / mode.vsyncPeriod * 0.1f) * 10;
    char modeStr[PROPERTY_VALUE_MAX];
    int ret = snprintf(modeStr, sizeof(modeStr), "%dx%d@%d",
             mode.width, mode.height, refreshRate);
    if (ret <= 0)
        return HWC2_ERROR_BAD_CONFIG;

    ALOGD("%s: mode=%s (%d) vsyncPeriod=%d", __func__, modeStr, config,
            mode.vsyncPeriod);
    ret = property_set(PROPERTY_BOOT_MODE, modeStr);

    return !ret ? HWC2_ERROR_NONE : HWC2_ERROR_BAD_CONFIG;
}

int32_t ExynosPrimaryDisplay::clearBootDisplayConfig() {
    auto ret = property_set(PROPERTY_BOOT_MODE, nullptr);

    ALOGD("%s: clearing boot mode", __func__);
    return !ret ? HWC2_ERROR_NONE : HWC2_ERROR_BAD_CONFIG;
}

int32_t ExynosPrimaryDisplay::getPreferredDisplayConfigInternal(int32_t *outConfig) {
    char modeStr[PROPERTY_VALUE_MAX];

    auto ret = property_get(PROPERTY_BOOT_MODE, modeStr, nullptr);
    if (ret < 0) {
        hwc2_config_t activeConfigBoot;
        mDisplayInterface->getActiveConfigBoot(&activeConfigBoot);
        *outConfig = activeConfigBoot;
        return HWC2_ERROR_NONE;
    }

    int width, height;
    int fps = 0;

    ret = sscanf(modeStr, "%dx%d@%d", &width, &height, &fps);
    if ((ret < 3) || !fps) {
        ALOGD("%s: unable to find boot config for mode: %s", __func__, modeStr);
        return HWC2_ERROR_BAD_CONFIG;
    }

    const auto vsyncPeriod = nsecsPerSec / fps;

    for (auto const& [config, mode] : mDisplayConfigs) {
        long delta = abs(vsyncPeriod - mode.vsyncPeriod);
        if ((width == mode.width) && (height == mode.height) &&
            (delta < nsecsPerMs)) {
            ALOGD("%s: found preferred display config for mode: %s=%d",
                  __func__, modeStr, config);
            *outConfig = config;
            return HWC2_ERROR_NONE;
        }
    }
    return HWC2_ERROR_BAD_CONFIG;
}

int32_t ExynosPrimaryDisplay::setPowerMode(
        int32_t /*hwc2_power_mode_t*/ mode) {
    Mutex::Autolock lock(mDisplayMutex);

#ifndef USES_DOZEMODE
    if ((mode == HWC2_POWER_MODE_DOZE) || (mode == HWC2_POWER_MODE_DOZE_SUSPEND))
        return HWC2_ERROR_UNSUPPORTED;
#endif

    /* TODO state check routine should be added */
    int fb_blank = -1;

    if (mode == HWC_POWER_MODE_DOZE ||
        mode == HWC_POWER_MODE_DOZE_SUSPEND) {
        if (this->mPowerModeState != HWC_POWER_MODE_DOZE &&
            this->mPowerModeState != HWC_POWER_MODE_OFF &&
            this->mPowerModeState != HWC_POWER_MODE_DOZE_SUSPEND) {
            fb_blank = FB_BLANK_POWERDOWN;
            clearDisplay();
        } else {
            ALOGE("DOZE or Power off called twice, mPowerModeState : %d", this->mPowerModeState);
        }
    } else if (mode == HWC_POWER_MODE_OFF) {
        fb_blank = FB_BLANK_POWERDOWN;
        clearDisplay();
        /*
         * present will be skipped when display is power off
         * set present flag and clear flags hear
         */
        setPresentAndClearRenderingStatesFlags();
        ALOGV("HWC2: Clear display (power off)");
    } else {
        fb_blank = FB_BLANK_UNBLANK;
    }

    ALOGD("%s:: FBIOBLANK mode(%d), blank(%d)", __func__, mode, fb_blank);

    if (fb_blank == FB_BLANK_POWERDOWN)
        mDREnable = false;
    else if (fb_blank == FB_BLANK_UNBLANK)
        mDREnable = mDRDefault;

    // check the dynamic recomposition thread by following display
    mDevice->checkDynamicRecompositionThread();

    mDisplayInterface->setPowerMode(mode);
    this->mPowerModeState = (hwc2_power_mode_t)mode;

    ALOGD("%s:: S3CFB_POWER_MODE mode(%d), blank(%d)", __func__, mode, fb_blank);

    if (mode == HWC_POWER_MODE_OFF) {
        /* It should be called from validate() when the screen is on */
        mNeedSkipPresent = true;
        setGeometryChanged(GEOMETRY_DISPLAY_POWER_OFF);
        if ((mRenderingState >= RENDERING_STATE_VALIDATED) &&
            (mRenderingState < RENDERING_STATE_PRESENTED))
            closeFencesForSkipFrame(RENDERING_STATE_VALIDATED);
        mRenderingState = RENDERING_STATE_NONE;
    } else {
        setGeometryChanged(GEOMETRY_DISPLAY_POWER_ON);
    }

#if defined(USES_DUAL_DISPLAY)
    mResourceManager->distributeResourceSet();
#endif

    return HWC2_ERROR_NONE;
}

bool ExynosPrimaryDisplay::getHDRException(ExynosLayer* __unused layer)
{
    return false;
}

void ExynosPrimaryDisplay::initDisplayInterface(uint32_t __unused interfaceType)
{
    mDisplayInterface = new ExynosPrimaryDisplayFbInterfaceModule((ExynosDisplay *)this);
    mDisplayInterface->init(this);
}
