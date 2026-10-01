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
#ifndef ANDROID_EXYNOS_HWC1_ADAPTOR_H_
#define ANDROID_EXYNOS_HWC1_ADAPTOR_H_
#include <hardware/hwcomposer2.h>
#include <hardware/hwcomposer.h>

int exynos_prepare(hwc_composer_device_1_t *dev,
        size_t numDisplays, hwc_display_contents_1_t **displays);
int exynos_set(struct hwc_composer_device_1 *dev,
        size_t numDisplays, hwc_display_contents_1_t **displays);
int exynos_eventControl(struct hwc_composer_device_1 *dev, int dpy,
        int event, int enabled);
int exynos_query(struct hwc_composer_device_1* dev, int what, int *value);
void exynos_registerProcs(struct hwc_composer_device_1* dev,
        hwc_procs_t const* procs);
void exynos_hwc1_dump(hwc_composer_device_1 *dev, char *buff, int buff_len);

int exynos_hwc1_getDisplayConfigs(struct hwc_composer_device_1 *dev,
        int disp, uint32_t *configs, size_t *numConfigs);
int exynos_hwc1_getDisplayAttributes(struct hwc_composer_device_1 *dev,
        int disp, uint32_t config, const uint32_t *attributes, int32_t *values);
int exynos_hwc1_getActiveConfig(struct hwc_composer_device_1 *dev, int disp);
int exynos_hwc1_setActiveConfig(struct hwc_composer_device_1 *dev, int disp, int index);
int exynos_hwc1_setCursorPositionAsync(struct hwc_composer_device_1 *dev, int disp, int x_pos, int y_pos);
int exynos_hwc1_setPowerMode(struct hwc_composer_device_1 *dev, int disp, int mode);
int exynos_blank(struct hwc_composer_device_1 *dev, int disp, int blank);

#endif
