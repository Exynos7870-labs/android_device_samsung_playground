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

#include "ExynosPrimaryDisplay.h"
#include "ExynosDevice.h"
#include "ExynosHWCDebug.h"

extern struct exynos_hwc_cotrol exynosHWCControl;

ExynosPrimaryDisplay::ExynosPrimaryDisplay(uint32_t __unused type, ExynosDevice *device)
    :   ExynosDisplay(HWC_DISPLAY_PRIMARY, device)
{
    /* TODO: Need this one here? */
    //this->mHwc = pdev;
    //mInternalDMAs.add(IDMA_G1);

    // TODO : Hard coded here
    mNumMaxPriorityAllowed = 5;
}

ExynosPrimaryDisplay::~ExynosPrimaryDisplay()
{
}

int32_t ExynosPrimaryDisplay::setPowerMode(
        int32_t /*hwc2_power_mode_t*/ mode) {

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
        mDevice->mPrimaryBlank = true;
        clearDisplay();
    } else {
        fb_blank = FB_BLANK_UNBLANK;
        mDevice->mPrimaryBlank = false;
    }

    if (android_atomic_acquire_load(&this->updateThreadStatus) != 0) { //check if the thread is alive
        if (fb_blank == FB_BLANK_POWERDOWN) {
            mDevice->dynamic_recomp_stat_thread_flag = false;
            pthread_join(mDevice->mDynamicRecompositionThread, 0);
        } else { // thread is not alive
            if (fb_blank == FB_BLANK_UNBLANK && exynosHWCControl.useDynamicRecomp == true)
                mDevice->dynamicRecompositionThreadLoop();
        }
    }

    if (fb_blank >= 0) {
        if (ioctl(mDisplayFd, FBIOBLANK, fb_blank) == -1) {
            ALOGE("FB BLANK ioctl failed errno : %d", errno);
            return HWC2_ERROR_UNSUPPORTED;
        }
    }
    ALOGD("%s:: FBIOBLANK mode(%d), blank(%d)", __func__, mode, fb_blank);

    this->mPowerModeState = (hwc2_power_mode_t)mode;

    if (ioctl(mDisplayFd, S3CFB_POWER_MODE, &mode) == -1) {
        ALOGE("Need to check S3CFB power mode ioctl : %d", errno);
//        return HWC2_ERROR_UNSUPPORTED;
    }
    ALOGD("%s:: S3CFB_POWER_MODE mode(%d), blank(%d)", __func__, mode, fb_blank);

    return HWC2_ERROR_NONE;
}
