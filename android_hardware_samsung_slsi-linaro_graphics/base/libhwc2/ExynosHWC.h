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

#ifndef ANDROID_EXYNOS_HWC_H_
#define ANDROID_EXYNOS_HWC_H_
#include <hardware/hwcomposer2.h>
#include <hardware/hwcomposer.h>
#include <cutils/atomic.h>
#include "ExynosHWCModule.h"
#include "ExynosDevice.h"
#include "DeconHeader.h"

//#define DISABLE_FENCE

#define HWC_FPS_TH          5    /* valid range 1 to 60 */
#define VSYNC_INTERVAL (1000000000.0 / 60)

const size_t NUM_HW_WINDOWS = MAX_DECON_WIN;
class ExynosDevice;

enum {
    NO_DRM = 0,
    NORMAL_DRM,
    SECURE_DRM,
};

struct exynos_hwc2_device_t {
    //hwc2_device_t base;
	//ExynosDevice			*device;
};

struct exynos_hwc_composer_device_1_t {
    hwc_composer_device_1_t base;

    const hwc_procs_t       *procs;
    ExynosDevice            *device;
};
#endif
