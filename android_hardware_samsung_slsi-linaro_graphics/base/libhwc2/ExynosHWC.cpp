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
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <hardware/hardware.h>
#include <utils/Errors.h>
#include <utils/Trace.h>
#include <log/log.h>
#include <sys/stat.h>

#include "ExynosHWC.h"
#include "ExynosHWC1Adaptor.h"
#include "ExynosHWCModule.h"
#include "ExynosHWCService.h"
#include "ExynosHWCHelper.h"

class ExynosHWCService;

using namespace android;

uint32_t hwcApiVersion(const hwc_composer_device_1_t* hwc) {
    uint32_t hwcVersion = hwc->common.version;
    return hwcVersion & HARDWARE_API_VERSION_2_MAJ_MIN_MASK;
}

uint32_t hwcHeaderVersion(const hwc_composer_device_1_t* hwc) {
    uint32_t hwcVersion = hwc->common.version;
    return hwcVersion & HARDWARE_API_VERSION_2_HEADER_MASK;
}

bool hwcHasApiVersion(const hwc_composer_device_1_t* hwc, uint32_t version)
{
    return (hwcApiVersion(hwc) >= (version & HARDWARE_API_VERSION_2_MAJ_MIN_MASK));
}

/**************************************************************************************
 * HWC 2.x APIs
 * ************************************************************************************/
void exynos_dump(hwc2_device_t* device, uint32_t* outSize, char* outBuffer)
{
    ExynosDevice *exynosDevice = (ExynosDevice *)device;
    if (exynosDevice != NULL)
        return exynosDevice->dump(outSize, outBuffer);
}

int32_t exynos_registerCallback(hwc2_device_t* device,
        int32_t /*hwc2_callback_descriptor_t*/ descriptor,
        hwc2_callback_data_t callbackData, hwc2_function_pointer_t pointer)
{
    ExynosDevice *exynosDevice = (ExynosDevice *)device;
    if (exynosDevice == NULL)
        return HWC2_ERROR_BAD_PARAMETER;

    switch (descriptor) {
    case HWC2_CALLBACK_HOTPLUG:
    case HWC2_CALLBACK_REFRESH:
    case HWC2_CALLBACK_VSYNC:
         return exynosDevice->registerCallback(descriptor, callbackData, pointer);
    default:
        return HWC2_ERROR_BAD_PARAMETER;
    }
    return HWC2_ERROR_NONE;
}

int32_t exynos_getDisplayConfigs(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        uint32_t* __unused outNumConfigs, hwc2_config_t* __unused outConfigs)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->getDisplayConfigs(outNumConfigs, outConfigs)
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_getDisplayAttribute(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_config_t __unused config, int32_t /*hwc2_attribute_t*/ __unused attribute, int32_t* __unused outValue)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->getDisplayAttribute(config, attribute, outValue);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_getActiveConfig(hwc2_device_t __unused *device, hwc2_display_t __unused display,
                hwc2_config_t* __unused outConfig)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->getActiveConfig(outConfig);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_setActiveConfig(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_config_t __unused config)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->setActiveConfig(config);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_getDisplayName(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        uint32_t* __unused outSize, char* __unused outName)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->getDisplayName(outSize, outName);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_getDisplayType(hwc2_device_t __unused *device, hwc2_display_t __unused display,
                int32_t* /*hwc2_display_type_t*/ __unused outType)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->getDisplayType(outType);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_createLayer(hwc2_device_t __unused *device,
                hwc2_display_t __unused display, hwc2_layer_t* __unused outLayer)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->createLayer(outLayer);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_destroyLayer(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->destroyLayer(layer);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_validateDisplay(hwc2_device_t __unused *device, hwc2_display_t __unused display,
                uint32_t* __unused outNumTypes, uint32_t* __unused outNumRequests)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->validateDisplay(outNumTypes, outNumRequests);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_getChangedCompositionTypes(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        uint32_t* __unused outNumElements, hwc2_layer_t* __unused outLayers,
        int32_t* /*hwc2_composition_t*/ __unused outTypes)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->getChangedCompositionTypes(outNumElements, outLayers, outTypes);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_getDisplayRequests(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        int32_t* /*hwc2_display_request_t*/ __unused outDisplayRequests,
        uint32_t* __unused outNumElements, hwc2_layer_t* __unused outLayers,
        int32_t* /*hwc2_layer_request_t*/ __unused outLayerRequests)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->getDisplayRequests(outDisplayRequests, outNumElements, outLayers, outLayerRequests);
     */
    return HWC2_ERROR_NONE;
}

int32_t acceptDisplayChanges(hwc2_device_t __unused *device, hwc2_display_t __unused display)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->acceptDisplayChanges();
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_setClientTarget(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        buffer_handle_t __unused target, int32_t __unused acquireFence,
        int32_t /*android_dataspace_t*/ __unused dataspace)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->setClientTarget(target, acquireFence, dataspace);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_setOutputBuffer(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        buffer_handle_t __unused buffer, int32_t __unused releaseFence)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * if (exynosDisplay->getDisplayType() != HWC2_DISPLAY_TYPE_VIRTUAL)
     * return HWC2_ERROR_UNSUPPORTED;
     * return exynosDisplay->setClientTarget(buffer, releaseFence);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_presentDisplay(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        int32_t* __unused outRetireFence)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->presentDisplay(outRetireFence);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_getDozeSupport(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        int32_t* __unused outSupport)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->getDozeSupport(outSupport);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_setPowerMode(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        int32_t /*hwc2_power_mode_t*/ __unused mode)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->setPowerMode(mode);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_getReleaseFences(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        uint32_t* __unused outNumElements, hwc2_layer_t* __unused outLayers, int32_t* __unused outFences)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->getReleaseFences(outNumElements, outLayers, outFences);
     */
    return HWC2_ERROR_NONE;
}

int32_t exynos_setVsyncEnabled(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        int32_t /*hwc2_vsync_t*/ __unused enabled)
{
    /* To do
     * ExynosDisplay *exynosDisplay = (ExynosDisplay *)display;
     * if (exynosDisplay->checkHandle())
     * return HWC2_ERROR_BAD_DISPLAY;
     * return exynosDisplay->setVsyncEnabled(enabled);
     */
    return HWC2_ERROR_NONE;
}

int32_t setLayerCompositionType(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, int32_t /*hwc2_composition_t*/ __unused type)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setLayerCompositionType(type);
     */
    return HWC2_ERROR_NONE;
}

int32_t setCursorPosition(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, int32_t __unused x, int32_t __unused y)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setCursorPosition(x, y);
     */
    return HWC2_ERROR_NONE;
}

int32_t setLayerBuffer(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, buffer_handle_t __unused buffer, int32_t __unused acquireFence)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setLayerBuffer(buffer, acquireFence);
     */
    return HWC2_ERROR_NONE;
}

int32_t setLayerSurfaceDamage(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, hwc_region_t __unused damage)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setLayerSurfaceDamage(damage);
     */
    return HWC2_ERROR_NONE;
}

int32_t setLayerBlendMode(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, int32_t /*hwc2_blend_mode_t*/ __unused mode)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setLayerBlendMode(mode);
     */
    return HWC2_ERROR_NONE;
}

int32_t setLayerColor(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, hwc_color_t __unused color)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setLayerColor(color);
     */
    return HWC2_ERROR_NONE;
}

int32_t setLayerDisplayFrame(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, hwc_rect_t __unused frame)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setLayerDisplayFrame(frame);
     */
    return HWC2_ERROR_NONE;
}

int32_t setLayerPlaneAlpha(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, float __unused alpha)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setLayerPlaneAlpha(alpha);
     */
    return HWC2_ERROR_NONE;
}

int32_t setLayerSidebandStream(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, const native_handle_t* __unused stream)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setLayerSidebandStream(stream);
     */
    return HWC2_ERROR_NONE;
}

int32_t setLayerSourceCrop(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, hwc_frect_t __unused crop)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setLayerSourceCrop(crop);
     */
    return HWC2_ERROR_NONE;
}

int32_t setLayerTransform(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, int32_t /*hwc_transform_t*/ __unused transform)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setLayerTransform(transform);
     */
    return HWC2_ERROR_NONE;
}

int32_t setLayerVisibleRegion(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, hwc_region_t __unused visible)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setLayerVisibleRegion(visible);
     */
    return HWC2_ERROR_NONE;
}

int32_t setLayerZOrder(hwc2_device_t __unused *device, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, uint32_t __unused z)
{
    /* To do
     * ExynosLayer *exynosLayer = (ExynosLayer *)layer;
     * if (exynosLayer->checkHandle())
     * return HWC2_ERROR_BAD_LAYER;
     * return exynosLayer->setLayerZOrder(z);
     */
    return HWC2_ERROR_NONE;
}

void exynos_boot_finished(exynos_hwc_composer_device_1_t *dev)
{
    ALOGE("Boot Finished");
    int sw_fd;
    exynos_hwc_composer_device_1_t *pdev =
            (exynos_hwc_composer_device_1_t *)dev;
    if (pdev == NULL) {
        ALOGE("%s:: dev is NULL", __func__);
        return;
    }

    sw_fd = open("/sys/class/switch/hdmi/state", O_RDONLY);
    ALOGE("Boot Finished : %d", sw_fd);

    if (sw_fd >= 0) {
        char val;
        if (read(sw_fd, &val, 1) == 1 && val == '1') {
			if (pdev->device->hpd_status != true){
			    pdev->device->hpd_status = true;
                pdev->device->handleVirtualHpd();
		    }
		}
        hwcFdClose(sw_fd);
    }
}

int exynos_close(hw_device_t* device)
{
    if (device == NULL)
    {
        ALOGE("%s:: device is null", __func__);
        return -EINVAL;
    }

    hw_module_t *module = device->module;
    if (module->module_api_version >= 0x200) {
        /* For HWC2.x version */
    } else {
        /* For HWC1.x version */
        exynos_hwc_composer_device_1_t* dev = (struct exynos_hwc_composer_device_1_t *)device;

        if (dev != NULL) {
            if (dev->device != NULL)
                delete dev->device;
            delete dev;
        }
    }

    return NO_ERROR;
}

int exynos_open(const struct hw_module_t *module, const char *name,
        struct hw_device_t **device)
{
    if (strcmp(name, HWC_HARDWARE_COMPOSER)) {
        return -EINVAL;
    }

    if (module->module_api_version >= 0x200) {
        /* For HWC2.x version */
#if 0
        struct exynos_hwc2_device_t *dev;
        dev->base.getCapabilities = ;
        dev->base.getFunction = ;
#endif

        ALOGE("Invalid hwc module version");
        return -EINVAL;
    } else {
        ALOGD("HWC2 function register start");
        /* For HWC1.x version */
        struct exynos_hwc_composer_device_1_t *dev;
        dev = (struct exynos_hwc_composer_device_1_t *)malloc(sizeof(*dev));
        memset(dev, 0, sizeof(*dev));

        dev->device = new ExynosDevice;

        dev->base.common.tag = HARDWARE_DEVICE_TAG;
        dev->base.common.version = HWC_VERSION;
        dev->base.common.module = const_cast<hw_module_t *>(module);
        dev->base.common.close = exynos_close;
        dev->base.prepare = exynos_prepare;
        dev->base.set = exynos_set;
        dev->base.eventControl = exynos_eventControl;
        dev->base.query = exynos_query;
        dev->base.registerProcs = exynos_registerProcs;
        dev->base.dump = exynos_hwc1_dump;
        dev->base.getDisplayConfigs = exynos_hwc1_getDisplayConfigs;
        dev->base.getDisplayAttributes = exynos_hwc1_getDisplayAttributes;
        if (hwcHasApiVersion((hwc_composer_device_1_t*)dev, HWC_DEVICE_API_VERSION_1_4)) {
            dev->base.getActiveConfig = exynos_hwc1_getActiveConfig;
            dev->base.setActiveConfig = exynos_hwc1_setActiveConfig;
            dev->base.setCursorPositionAsync = exynos_hwc1_setCursorPositionAsync;
            dev->base.setPowerMode = exynos_hwc1_setPowerMode;
        } else {
            dev->base.blank = exynos_blank;
        }
        *device = &dev->base.common;
        ALOGD("HWC2 function register end");

        ALOGD("Start HWCService");
#ifdef USES_HWC_SERVICES
        android::ExynosHWCService   *HWCService;
        HWCService = android::ExynosHWCService::getExynosHWCService();
        HWCService->setExynosHWCCtx(dev);
        HWCService->setBootFinishedCallback(exynos_boot_finished);
#endif
    }

    return NO_ERROR;
}

static struct hw_module_methods_t exynos_hwc_module_methods = {
    .open = exynos_open,
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = HWC_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = HWC_HARDWARE_MODULE_ID,
        .name = "Samsung exynos hwcomposer module",
        .author = "Samsung LSI",
        .methods = &exynos_hwc_module_methods,
        .dso = 0,
        .reserved = {0},
    }
};
