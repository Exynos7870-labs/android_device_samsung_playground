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
#include <hardware/hardware.h>
#include <utils/Errors.h>
#include <utils/Trace.h>
#include <log/log.h>
#include <vector>
#include <hardware/hwcomposer.h>
#include "ExynosHWC1Adaptor.h"
#include "ExynosHWC.h"
#include "ExynosDevice.h"
#include "ExynosDisplay.h"
#include "ExynosLayer.h"
#include "ExynosHWCDebug.h"
#include "ExynosExternalDisplay.h"
#include "ExynosVirtualDisplay.h"

using namespace android;

/**************************************************************************************
 * HWC 1.x APIs
 * ************************************************************************************/
int exynos_eventControl(struct hwc_composer_device_1 *dev, int __unused disp,
        int event, int enabled)
{
    struct exynos_hwc_composer_device_1_t *pdev =
        (struct exynos_hwc_composer_device_1_t *)dev;

    ExynosDevice *device = (ExynosDevice*)pdev->device;
    if (device == NULL)
        return -EINVAL;
    ExynosDisplay *display = (ExynosDisplay*)device->getDisplay(HWC_DISPLAY_PRIMARY);
    if (display == NULL)
        return -EINVAL;

    switch (event) {
    case HWC_EVENT_VSYNC:
        if(display->setVsyncEnabled(enabled) != HWC2_ERROR_NONE) // Param type : hwc2_vsync_t
            goto error;
        break;
    default:
        ALOGE("HWC2 : %s : %d ", __func__, __LINE__);
        return HWC2_ERROR_NONE;
    }

    return HWC2_ERROR_NONE;

error:
    return -EINVAL;
}

int exynos_query(struct hwc_composer_device_1* dev, int what, int *value)
{
//    ALOGD("HWC2 : %s : %d ", __func__, __LINE__);
    struct exynos_hwc_composer_device_1_t *pdev =
        (struct exynos_hwc_composer_device_1_t*)dev;

    ExynosDevice *device = (ExynosDevice*)pdev->device;
    if (device == NULL)
        return -EINVAL;
    ExynosDisplay *display = (ExynosDisplay*)device->getDisplay(HWC_DISPLAY_PRIMARY);
    if (display == NULL)
        return -EINVAL;

    switch (what) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
        // we support the background layer
        value[0] = 1;
        break;
    case HWC_VSYNC_PERIOD:
        // vsync period in nanosecond
        value[0] = display->mVsyncPeriod;
        break;
    case HWC_DISPLAY_TYPES_SUPPORTED:
        // support virtual display
        value[0] |= HWC_DISPLAY_VIRTUAL_BIT;
        break;
    default:
        // unsupported query
        goto error;
    }

    return HWC2_ERROR_NONE;

error:
    return -EINVAL;
}

HWC2_PFN_VSYNC hotplug_callback(hwc2_callback_data_t callbackData,
        hwc2_display_t displayId, int32_t intConnected)
{
    struct exynos_hwc_composer_device_1_t* pdev =
        (struct exynos_hwc_composer_device_1_t*)callbackData;

    if (pdev->procs && pdev->procs->hotplug)
        pdev->procs->hotplug(pdev->procs, displayId, intConnected);

    return 0;
}

HWC2_PFN_REFRESH refresh_callback(hwc2_callback_data_t callbackData,
         hwc2_display_t __unused displayId)
{
    struct exynos_hwc_composer_device_1_t* pdev =
        (struct exynos_hwc_composer_device_1_t*)callbackData;

    if (pdev->procs && pdev->procs->invalidate)
        pdev->procs->invalidate(pdev->procs);

    return 0;
}

HWC2_PFN_VSYNC vsync_callback(hwc2_callback_data_t callbackData,
        hwc2_display_t displayId, int64_t timestamp)
{
    struct exynos_hwc_composer_device_1_t* pdev =
        (struct exynos_hwc_composer_device_1_t*)callbackData;

//    ALOGD("HWC2 : %s : %d callback : %llu", __func__, __LINE__, pdev);

    if (pdev->procs && pdev->procs->vsync)
        pdev->procs->vsync(pdev->procs, displayId, timestamp);
    else
        ALOGE("HWC2 : No procs! %s : %d ", __func__, __LINE__);

    return 0;
}

void exynos_registerProcs(struct hwc_composer_device_1* dev,
        hwc_procs_t const* procs)
{
//    ALOGD("HWC2 : %s : %d ", __func__, __LINE__);
    struct exynos_hwc_composer_device_1_t* pdev =
        (struct exynos_hwc_composer_device_1_t*)dev;
    pdev->procs = procs;

    ExynosDevice *device = (ExynosDevice *)pdev->device;
    if (device == NULL) {
        ALOGE("%s : device null!!", __func__);
        return;
    }

    device->registerCallback(HWC2_CALLBACK_HOTPLUG,
            static_cast<hwc2_callback_data_t>(pdev),
            reinterpret_cast<hwc2_function_pointer_t>(hotplug_callback));
    device->registerCallback(HWC2_CALLBACK_REFRESH,
            static_cast<hwc2_callback_data_t>(pdev),
            reinterpret_cast<hwc2_function_pointer_t>(refresh_callback));
    device->registerCallback(HWC2_CALLBACK_VSYNC,
            static_cast<hwc2_callback_data_t>(pdev),
            reinterpret_cast<hwc2_function_pointer_t>(vsync_callback));
}

void exynos_hwc1_dump(hwc_composer_device_1 *dev, char *buff, int buff_len)
{
//    ALOGD("HWC2 : %s : %d ", __func__, __LINE__);
    struct exynos_hwc_composer_device_1_t* pdev =
        (struct exynos_hwc_composer_device_1_t*)dev;
    uint32_t numBytes = (uint32_t)buff_len;
    ExynosDevice *device = (ExynosDevice *)pdev->device;
    if (device == NULL) {
        ALOGE("%s : device null!!", __func__);
        return;
    }
    device->dump(&numBytes, buff);
}

int exynos_hwc1_getDisplayConfigs(struct hwc_composer_device_1 *dev,
        int disp, uint32_t *configs, size_t *numConfigs)
{
//    ALOGD("HWC2 : %s : %d !!", __func__, __LINE__);
    struct exynos_hwc_composer_device_1_t *pdev =
               (struct exynos_hwc_composer_device_1_t*)dev;
    int err;
    ExynosDevice *device = (ExynosDevice*)pdev->device;
    if (device == NULL)
        return -EINVAL;
    ExynosDisplay *display = (ExynosDisplay*)device->getDisplay(HWC_DISPLAY_PRIMARY);
    if (display == NULL) {
        ALOGE("HWC2 : display null !! %s : %d", __func__, __LINE__);
        return -EINVAL;
    }
    ExynosExternalDisplay *external_display = (ExynosExternalDisplay *)device->getDisplay(HWC_DISPLAY_EXTERNAL);
    ExynosVirtualDisplay *virtual_display = (ExynosVirtualDisplay *)device->getDisplay(HWC_DISPLAY_VIRTUAL);
//    if (*numConfigs == 0)
//       goto error;

    uint32_t outNumConfigs = 0;
    switch (disp) {
    case HWC_DISPLAY_PRIMARY:
        err = display->getDisplayConfigs(&outNumConfigs, NULL);
        ALOGD("HWC2 : %s : %d Num : %d", __func__, __LINE__, outNumConfigs);
        if((err != HWC2_ERROR_NONE) ||
           (outNumConfigs > *numConfigs) ||
           ((err = display->getDisplayConfigs((uint32_t*)numConfigs,(hwc2_config_t*)configs)) != HWC2_ERROR_NONE)) {
            ALOGD("HWC2 : %s : %d : outNumConfigs(%d), numConfigs(%zu), err(%d)",
                    __func__, __LINE__, outNumConfigs, *numConfigs, err);
            goto error;
        }
        break;
    case HWC_DISPLAY_EXTERNAL:
        if (!device->hpd_status) {
            return -EINVAL;
        }
        err = external_display->getDisplayConfigs((uint32_t*)numConfigs, (hwc2_config_t*)configs);
        if (err) {
            ALOGE("HWC2 : %s : %d : external display getDisplayConfigs fail", __func__, __LINE__);
            return -EINVAL;
        }
        break;
    case HWC_DISPLAY_VIRTUAL:
        if (virtual_display) {
            err = virtual_display->getDisplayConfigs((uint32_t*)numConfigs, (hwc2_config_t*)configs);
            if (err) {
                ALOGE("HWC2 : %s : %d : virtual display getDisplayConfigs fail", __func__, __LINE__);
                return -EINVAL;
            }
        }
        break;
    default:
        goto error;
    }

    return HWC2_ERROR_NONE;

error:
    ALOGE("HWC2 : %s : %d ", __func__, __LINE__);
    return -EINVAL;
}

int exynos_hwc1_getDisplayAttributes(struct hwc_composer_device_1 __unused *dev,
        int __unused disp, uint32_t __unused config, const uint32_t __unused *attributes, int32_t __unused *values)
{
    struct exynos_hwc_composer_device_1_t *pdev =
        (struct exynos_hwc_composer_device_1_t*)dev;

    ExynosDevice *device = (ExynosDevice*)pdev->device;
    if (device == NULL)
        return -EINVAL;

    if (disp == HWC_DISPLAY_PRIMARY) {
        ExynosDisplay *display = (ExynosDisplay*)device->getDisplay(HWC_DISPLAY_PRIMARY);
        if (display == NULL)
            return -EINVAL;

        for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
            if (display->getDisplayAttribute((hwc2_config_t)config,
                (int32_t)attributes[i], (int32_t*)&values[i]) != HWC2_ERROR_NONE) {
                ALOGE("HWC2 : %s : %d ", __func__, __LINE__);
                goto error;
            }
        }
    } else if (disp == HWC_DISPLAY_EXTERNAL) {
        ExynosExternalDisplay *external_display = (ExynosExternalDisplay*)device->getDisplay(HWC_DISPLAY_EXTERNAL);
        if (external_display == NULL)
            return -EINVAL;

        for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
            if (external_display->getDisplayAttribute((hwc2_config_t)config,
                (int32_t)attributes[i], (int32_t*)&values[i]) != HWC2_ERROR_NONE) {
                ALOGE("HWC2 : %s : %d ", __func__, __LINE__);
                goto error;
            }
        }
    }
    else if (disp == HWC_DISPLAY_VIRTUAL) {
        ExynosVirtualDisplay *virtual_display = (ExynosVirtualDisplay*)device->getDisplay(HWC_DISPLAY_VIRTUAL);
        if (virtual_display == NULL)
            return -EINVAL;

        for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
            if (virtual_display->getDisplayAttribute((hwc2_config_t)config,
                (int32_t)attributes[i], (int32_t*)&values[i]) != HWC2_ERROR_NONE) {
                ALOGE("HWC2 : %s : %d ", __func__, __LINE__);
                goto error;
            }
        }
    }
    return HWC2_ERROR_NONE;

error:
    return -EINVAL;
}

int exynos_hwc1_getActiveConfig(struct hwc_composer_device_1 __unused *dev, int disp)
{
//    ALOGD("HWC2 : %s : %d ", __func__, __LINE__);
    struct exynos_hwc_composer_device_1_t *pdev =
        (struct exynos_hwc_composer_device_1_t*)dev;

    hwc2_config_t config = 0;

    ExynosDevice *device = (ExynosDevice*)pdev->device;
    if (device == NULL)
        return -EINVAL;
    ExynosDisplay *display = (ExynosDisplay*)device->getDisplay(HWC_DISPLAY_PRIMARY);
    if (display == NULL)
        return -EINVAL;
    ExynosExternalDisplay *external_display = (ExynosExternalDisplay*)device->getDisplay(HWC_DISPLAY_EXTERNAL);
    if (external_display == NULL)
        return -EINVAL;

	if (disp == HWC_DISPLAY_PRIMARY) {
        if (display->getActiveConfig(&config) != HWC2_ERROR_NONE)
            goto error;
    } else if (disp == HWC_DISPLAY_EXTERNAL) {
        if (external_display->getActiveConfig(&config) != HWC2_ERROR_NONE)
            goto error;
    } else {
        ALOGE("HWC2 : %s : %d ", __func__, __LINE__);
        return -EINVAL;
    }

	return config;
    //return HWC2_ERROR_NONE;

error:
    return -EINVAL;
}

int exynos_hwc1_setActiveConfig(struct hwc_composer_device_1 __unused *dev, int __unused disp, int __unused index)
{
//    ALOGD("HWC2 : %s : %d ", __func__, __LINE__);
    struct exynos_hwc_composer_device_1_t *pdev =
        (struct exynos_hwc_composer_device_1_t*)dev;

    hwc2_config_t config = 0;

    ExynosDevice *device = (ExynosDevice*)pdev->device;
    if (device == NULL)
        return -EINVAL;
    ExynosDisplay *display = (ExynosDisplay*)device->getDisplay(HWC_DISPLAY_PRIMARY);
    if (display == NULL)
        return -EINVAL;

	if (disp == HWC_DISPLAY_PRIMARY) {
        if (display->setActiveConfig(config)!= HWC2_ERROR_NONE)
            goto error;
    } else if (disp == HWC_DISPLAY_EXTERNAL) {
		config = (hwc2_config_t)index;
        if (display->setActiveConfig(config)!= HWC2_ERROR_NONE)
            goto error;
	}
    else {
        ALOGE("HWC2 : %s : %d ", __func__, __LINE__);
        return -EINVAL;
    }

    return HWC2_ERROR_NONE;

error:
    return -EINVAL;
}

int exynos_hwc1_setCursorPositionAsync(struct hwc_composer_device_1 __unused *dev, int __unused disp,
        int __unused x_pos, int __unused y_pos)
{
    struct exynos_hwc_composer_device_1_t *pdev =
        (struct exynos_hwc_composer_device_1_t*)dev;

    ExynosDevice *device = (ExynosDevice*)pdev->device;
    if (device == NULL)
        return -EINVAL;
    ExynosDisplay *display = (ExynosDisplay*)device->getDisplay(HWC_DISPLAY_PRIMARY);
    if (display == NULL)
        return -EINVAL;

    if (display->setCursorPositionAsync(x_pos, y_pos) != HWC2_ERROR_NONE)
        goto error;

    return HWC2_ERROR_NONE;

error:
    return -EINVAL;
}

int exynos_hwc1_setPowerMode(struct hwc_composer_device_1 __unused *dev, int __unused disp, int __unused mode)
{
    ATRACE_CALL();
    ALOGV("HWC2 : %s : %d ", __func__, __LINE__);
    struct exynos_hwc_composer_device_1_t *pdev =
        (struct exynos_hwc_composer_device_1_t*)dev;

    ExynosDevice *device = (ExynosDevice*)pdev->device;
    if (device == NULL)
        return -EINVAL;

    ExynosDisplay *display = (ExynosDisplay*)device->getDisplay(HWC_DISPLAY_PRIMARY);
    if (display == NULL)
        return -EINVAL;

	if (disp == HWC_DISPLAY_PRIMARY)
	    if (display->setPowerMode(mode) != HWC2_ERROR_NONE)
            goto error;

    return HWC2_ERROR_NONE;

error:
    return -EINVAL;
}

int exynos_blank(struct hwc_composer_device_1 *dev, int disp, int blank)
{
    return exynos_hwc1_setPowerMode(dev, disp, blank);
}

int create_layers(ExynosDisplay *display, hwc_display_contents_1_t *contents)
{
    int err = 0;

    if ((display == NULL) || (contents == NULL) || (contents->numHwLayers == 0))
    {
        if (display != NULL)
            display->destroyLayers();
        return -EINVAL;
    }

    hwc_layer_1_t *hwLayer = NULL;

// Test only
#if 0
    for (size_t i = 0; i < contents->numHwLayers; i++) {
        hwLayer= &contents->hwLayers[i];
        ALOGE("HWC2: Surface(prepare) : handle : %p, fence : %d, type : %d",
                hwLayer->handle, (int32_t)hwLayer->acquireFenceFd, hwLayer->compositionType);
    }
#endif

    HDEBUGLOGD(eDebugLayer, "Initial Layer size: numHwLayers-1(%d), mLayers.size(%zu)",
            (int32_t)contents->numHwLayers - 1, display->mLayers.size());

    for (size_t i = 0; i < contents->numHwLayers; i++)
        HDEBUGLOGD(eDebugLayer, "%s, Layer : %zu, type : %d", __func__,
                i, contents->hwLayers[i].compositionType);

    int32_t layerSize = display->mLayers.size();

    if ((int32_t)contents->numHwLayers - 1 > layerSize) {
        for (int32_t i = 0; i < ((int32_t)contents->numHwLayers - layerSize - 1); i++) {
            HDEBUGLOGD(eDebugLayer, "create layer %d", i);
            ExynosLayer *layer = NULL;
            if ((err = display->createLayer((hwc2_layer_t*)layer)) != HWC2_ERROR_NONE) {
                HWC_LOGE(display, "%s:: %d display fail to create layer(%d) (ret: %d)",
                        __func__, display->getDisplayId(), i, err);
                return err;
            }
        }
    }

    if (((int32_t)contents->numHwLayers - 1) < layerSize) {
        for (int32_t i = (layerSize - 1); i >= ((int32_t)contents->numHwLayers-1) ; i--) {
            HDEBUGLOGD(eDebugLayer, "destroy layer %d", i);
            ExynosLayer *layer = display->getLayer(i);
            if (layer != NULL) {
                display->destroyLayer((hwc2_layer_t*)layer);
            }
            else {
                HWC_LOGE(display, "%s:: %d display fail to get layer(%d) (ret: %d)",
                        __func__, display->getDisplayId(), i, err);
            }
        }
    }

    HDEBUGLOGD(eDebugLayer, "Changed Layer size: numHwLayers-1(%d), mLayers.size(%zu)",
            (int32_t)contents->numHwLayers - 1, display->mLayers.size());

    for (int32_t i = 0; i < (int32_t)contents->numHwLayers - 1; i++)
    {
        ExynosLayer *layer = display->getLayer(i);
        hwLayer = &contents->hwLayers[i];

        if(layer == NULL) {
            HDEBUGLOGE(eDebugDefault, "Layer is null!");
            return -EINVAL;
        }

        buffer_handle_t handle = hwLayer->handle;
#ifdef TARGET_USES_HWC2
        if ((hwLayer->flags & HWC_SKIP_LAYER) && (hwLayer->backgroundColor.r == 0) &&
            (hwLayer->backgroundColor.g == 0) && (hwLayer->backgroundColor.b == 0) &&
            (hwLayer->backgroundColor.a == 255)) {
            hwLayer->flags |= HWC_DIM_LAYER;
            hwLayer->handle = NULL;
            handle = NULL;
        }
#endif
        if (hwcCheckDebugMessages(eDebugLayer)) {
            HDEBUGLOGD(eDebugLayer, "Initial layer dump");
            layer->printLayer();
        }

#ifdef TARGET_USES_HWC2
        if (hwLayer->compositionType == HWC2_COMPOSITION_SCREENSHOT &&
            display->mDisplayId != HWC_DISPLAY_PRIMARY)
            layer->setLayerCompositionType(HWC2_COMPOSITION_SCREENSHOT);
        else
            layer->setLayerCompositionType(HWC2_COMPOSITION_INVALID);
#else
        layer->setLayerCompositionType(HWC2_COMPOSITION_INVALID);
#endif
        layer->mExynosCompositionType = HWC2_COMPOSITION_INVALID;
        layer->setLayerBuffer(handle, (int32_t)hwLayer->acquireFenceFd);
        layer->setLayerSurfaceDamage(hwLayer->surfaceDamage);
        layer->setLayerBlendMode(hwLayer->blending);
        layer->setLayerDisplayFrame(hwLayer->displayFrame);
        layer->setLayerPlaneAlpha((float)hwLayer->planeAlpha);
        layer->setLayerSourceCrop(hwLayer->sourceCropf); // TODO Check which crop
        layer->setLayerTransform(hwLayer->transform);
        layer->setLayerVisibleRegion(hwLayer->visibleRegionScreen);
        layer->setLayerZOrder(i);
        layer->setLayerFlag(hwLayer->flags);
        layer->setLayerDataspace(hwLayer->dataSpace);

        if (hwcCheckDebugMessages(eDebugLayer)) {
            HDEBUGLOGD(eDebugLayer, "Changed layer dump");
            layer->printLayer();
        }
    }

    // Test only
#if 0
    for (size_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->getLayer(i);
        ALOGE("HWC2: Layers(prepare): handle : %p, fence : %d, type : %d",
                layer->mLayerBuffer, layer->mAcquireFence, layer->mCompositionType);
    }
#endif

    return HWC2_ERROR_NONE;
}

int do_prepare_sequence(ExynosDisplay *display, hwc_display_contents_1_t *contents)
{
//    ALOGD("HWC2 : %s : %d ", __func__, __LINE__);
    int err = 0;

    if ((contents == NULL) || (contents->numHwLayers == 0))
        return err;

    int changed;
    uint32_t outNumTypes, outNumRequests = 0;
    size_t cnt = contents->numHwLayers;

    display->setHWC1LayerList(contents);

    // TODO check state
    if ((changed = display->validateDisplay(&outNumTypes, &outNumRequests)) != HWC2_ERROR_NONE) {
        if(changed != HWC2_ERROR_HAS_CHANGES) {
            HWC_LOGE(display, "%s:: %d display fail to validate display (ret: %d)",
                    __func__, display->getDisplayId(), err);
            return -err;
        }
    }

    if ((err = display->getChangedCompositionTypes(&outNumTypes, nullptr, nullptr)) != HWC2_ERROR_NONE) {
        HWC_LOGE(display, "%s:: %d display fail to get changed number (ret: %d)",
                __func__, display->getDisplayId(), err);
        return -err;
    }

    std::vector<hwc2_layer_t> layerIds(outNumTypes);
    std::vector<int32_t> types(outNumTypes);

    if ((err = display->getChangedCompositionTypes(&outNumTypes, layerIds.data(), types.data())) != HWC2_ERROR_NONE) {
        HWC_LOGE(display, "%s:: %d display fail to get changed type (ret: %d)",
                __func__, display->getDisplayId(), err);
        return -err;
    }

    for (uint32_t i = 0; i < outNumTypes; i++)
    {
        ExynosLayer *layer = (ExynosLayer *)layerIds[i];
        if(layer->setLayerCompositionType(types[i]) != HWC2_ERROR_NONE)
            HDEBUGLOGD(eDebugHWC, "HWC2 Wrong Type!!");
        /* TEST */
        //layer->setLayerCompositionType(HWC2_COMPOSITION_CLIENT);
    }

    for (uint32_t i = 0; i < cnt-1; i++) {
        ExynosLayer *layer = (ExynosLayer *)display->mLayers[i];
        for (uint32_t j = 0; j < outNumTypes; j++) {
            ExynosLayer *changedLayer = (ExynosLayer *)layerIds[j];
            if (layer == changedLayer) {
                hwc_layer_1_t &hwLayer = contents->hwLayers[i];
                hwLayer.compositionType = getHWC1CompType(types[i]);
                HDEBUGLOGD(eDebugHWC, "HWC2 index : %d, handle : %p, mCompositionType : %d, mValidateCompositionType : %d, compositionType: %d",
                        i, layer, layer->mCompositionType, layer->mValidateCompositionType, getHWC1CompType(layer->mValidateCompositionType));
            }
        }
    }

    if ((err = display->acceptDisplayChanges()) != HWC2_ERROR_NONE) {
        HWC_LOGE(display, "%s:: %d display fail to accept display changes (ret: %d)",
                __func__, display->getDisplayId(), err);
        return -err;
    }

    int32_t outDisplayRequests = 0;
    uint32_t numElements = 0;
    if ((err = display->getDisplayRequests(&outDisplayRequests, &numElements, NULL, NULL))
            != HWC2_ERROR_NONE) {
        HWC_LOGE(display, "%s:: %d display fail to display requests (ret: %d)",
                __func__, display->getDisplayId(), err);
        return -err;
    }
    std::vector<hwc2_layer_t> requestLayerIds(numElements);
    std::vector<int32_t> layerRequests(numElements);
    if ((err = display->getDisplayRequests(&outDisplayRequests, &numElements, requestLayerIds.data(), layerRequests.data()))
            != HWC2_ERROR_NONE) {
        HWC_LOGE(display, "%s:: %d display fail to display requests (ret: %d)",
                __func__, display->getDisplayId(), err);
        return -err;
    }

    hwc_layer_1_t *hwLayer = NULL;
    for (uint32_t requests = 0; requests < numElements; requests++) {
        ExynosLayer *requestLayer = (ExynosLayer *)requestLayerIds[requests];
        for (uint32_t i = 0; i < cnt-1; i++) {
            ExynosLayer *layer = (ExynosLayer *)display->mLayers[i];
            if ((layer == requestLayer) &&
                (layerRequests[requests] == HWC2_LAYER_REQUEST_CLEAR_CLIENT_TARGET)) {
                hwLayer= &contents->hwLayers[i];
                hwLayer->hints = HWC_HINT_CLEAR_FB;
            }
        }
    }

#ifdef TARGET_USES_HWC2
    for (uint32_t i = 0; i < (uint32_t) contents->numHwLayers - 1; i++) {
        hwLayer = &contents->hwLayers[i];
        if ((hwLayer->flags & HWC_DIM_LAYER) && (hwLayer->handle == NULL)) {
            hwLayer->backgroundColor.r = 0;
            hwLayer->backgroundColor.g = 0;
            hwLayer->backgroundColor.b = 0;
            hwLayer->backgroundColor.a = 255;
        }
    }
#endif
    return HWC2_ERROR_NONE;
}

int exynos_prepare(hwc_composer_device_1 __unused *dev,
        size_t __unused numDisplays, hwc_display_contents_1_t __unused **displays)
{
    ATRACE_CALL();
    int err = 0;
    if (!numDisplays || !displays)
        return -EINVAL;

    struct exynos_hwc_composer_device_1_t *pdev =
               (struct exynos_hwc_composer_device_1_t*)dev;
    /* To do */

    ExynosDevice *device = (ExynosDevice *)pdev->device;
    if (device == NULL)
        return -EINVAL;
    /*
       ExynosDisplay *display = device->getDisplay(disp);
       if (display == NULL)
       return -EINVAL;
       */

    hwc_display_contents_1_t *fimd_contents = displays[HWC_DISPLAY_PRIMARY];
    ExynosDisplay *primary_display = device->getDisplay(HWC_DISPLAY_PRIMARY);

    hwc_display_contents_1_t *virtual_contents = displays[HWC_DISPLAY_VIRTUAL];
    ExynosVirtualDisplay *virtual_display = (ExynosVirtualDisplay *)device->getDisplay(HWC_DISPLAY_VIRTUAL);

    hwc_display_contents_1_t *external_contents = displays[HWC_DISPLAY_EXTERNAL];
    ExynosExternalDisplay *external_display = (ExynosExternalDisplay *)device->getDisplay(HWC_DISPLAY_EXTERNAL);

    if (virtual_display->mDisplayFd < 0)
        external_display->mVirtualDisplayState = false;
    else
        external_display->mVirtualDisplayState = true;

    if (!device->mResolutionHandled){
        //dev->mResolutionHandled = true;
        device->hpd_status = true;
		external_display->hotplug();
		device->invalidate();
    }

    // mPlugState have to be updated before prepareResources()
    if (external_contents != NULL) external_display->init();
    else external_display->deInit();

    if (virtual_contents != NULL) virtual_display->init();
    else virtual_display->deInit();

    if ((err = device->mResourceManager->prepareResources()) != NO_ERROR) {
        HWC_LOGE(NULL, "prepareResources fail");
        return -1;
    }

    if (external_contents != NULL) {
        if ((external_display->mSkipStartFrame > (SKIP_EXTERNAL_FRAME - 1)) && (external_display->mEnabled == false) &&
            (external_display->mPowerModeState == (hwc2_power_mode_t)HWC_POWER_MODE_NORMAL)){
            if (external_display->setPowerMode(HWC_POWER_MODE_NORMAL) != HWC2_ERROR_NONE)
                return -1;
        } else if (external_display->mEnabled == false) {
            ALOGI("external display is waiting %d/%d frames (mEnabled : %d)(mPowerMode : %d)",
                    external_display->mSkipStartFrame + 1, SKIP_EXTERNAL_FRAME,
                    external_display->mEnabled, external_display->mPowerModeState);
        }

        if ((err = create_layers(external_display, external_contents)) != HWC2_ERROR_NONE) {
            HWC_LOGE(external_display, "%s:: fail to create layers (ret: %d)", __func__, err);
            return -err;
        }
        if ((err = do_prepare_sequence(external_display, external_contents)) != HWC2_ERROR_NONE) {
            HWC_LOGE(external_display, "%s:: fail to prepare (ret: %d)", __func__, err);
            return -err;
        }
    }

    if (virtual_contents != NULL) {
        if ((err = create_layers(virtual_display, virtual_contents)) != HWC2_ERROR_NONE) {
            HWC_LOGE(virtual_display, "%s:: fail to create layers (ret: %d)", __func__, err);
            return -err;
        }
        if ((err = do_prepare_sequence(virtual_display, virtual_contents)) != HWC2_ERROR_NONE) {
            HWC_LOGE(virtual_display, "%s:: fail to prepare (ret: %d)", __func__, err);
            return -err;
        }
    }

    if (fimd_contents) {
        /* TODO If layer has only FRAMBUFFER_TARGET, Do nothing */
        if (fimd_contents->numHwLayers <= 1)
            return HWC2_ERROR_NONE;

        primary_display = device->getDisplay(HWC_DISPLAY_PRIMARY);
        primary_display->mContentFlags = fimd_contents->flags;
        if ((err = create_layers(primary_display, fimd_contents)) != HWC2_ERROR_NONE) {
            HWC_LOGE(primary_display, "%s:: fail to create layers (ret: %d)", __func__, err);
            return -err;
        }
        if ((err = do_prepare_sequence(primary_display, fimd_contents)) != HWC2_ERROR_NONE) {
            HWC_LOGE(primary_display, "%s:: fail to prepare (ret: %d)", __func__, err);
            return -err;
        }
    }
    if ((err = device->mResourceManager->finishAssignResourceWork()) != NO_ERROR)
    {
        HWC_LOGE(NULL, "finishAssignResourceWork fail");
        return -1;
    }

    return HWC2_ERROR_NONE;
}

int do_set_sequence(ExynosDisplay *display, hwc_display_contents_1_t *contents)
{
    int32_t err = 0;
    uint32_t count = 0;
    int32_t cnt = (int32_t)contents->numHwLayers;
    hwc_layer_1_t *hwLayer = NULL;

    /* Initialize fences */
    contents->retireFenceFd = -1;
    for (int32_t i = 0; i < cnt; i++) {
        hwLayer= &contents->hwLayers[i];
        hwLayer->releaseFenceFd = -1;
    }

    if ((cnt - 1) >= 0) {
        hwc_layer_1_t &fb_target_layer = contents->hwLayers[cnt - 1];
        private_handle_t *handle = NULL;
        if (fb_target_layer.handle!= NULL)
            handle = private_handle_t::dynamicCast(fb_target_layer.handle);
        if (fb_target_layer.compositionType == HWC_FRAMEBUFFER_TARGET) {
            display->setClientTarget(handle, fb_target_layer.acquireFenceFd, HAL_DATASPACE_UNKNOWN);
        }
    }

    for (int32_t i = 0; i < cnt; i++) {
        hwLayer= &contents->hwLayers[i];
#ifdef TARGET_USES_HWC2
        if ((hwLayer->flags & HWC_SKIP_LAYER) && (hwLayer->backgroundColor.r == 0) &&
            (hwLayer->backgroundColor.g == 0) && (hwLayer->backgroundColor.b == 0) &&
            (hwLayer->backgroundColor.a == 255)) {
            hwLayer->flags |= HWC_DIM_LAYER;
            hwLayer->handle = NULL;
        }
#endif
        if (hwLayer->compositionType == HWC_FRAMEBUFFER_TARGET) {
            count++;
            continue;
        }
        for (size_t j = 0; j < display->mLayers.size(); j++) {
            ExynosLayer *layer = display->getLayer(j);
            if (hwLayer->handle == layer->mLayerBuffer) {
                layer->setLayerBuffer(hwLayer->handle, (int32_t)hwLayer->acquireFenceFd);
                if (hwLayer->acquireFenceFd == 0)
                    HWC_LOGE(display, "HWC2: SurfaceFlinger acquire fence is 0");
                count++;
            }
        }
    }

    int32_t outRetireFence = -1;

    display->setHWC1LayerList(contents);

    if ((cnt - 1) != (int)display->mLayers.size()) {
        HWC_LOGE(display, "layer size is different (%d, %zu)",
                cnt, display->mLayers.size());
        return -1;
    }

    if ((err = display->presentDisplay(&outRetireFence)) != HWC2_ERROR_NONE) {
        HWC_LOGE(display, "%s:: %d display fail to present display (ret: %d)",
                __func__, display->getDisplayId(), err);
        return -err;
    }

    contents->retireFenceFd = outRetireFence;

    uint32_t outNumElements = 0;
    if ((err = display->getReleaseFences(&outNumElements, nullptr, nullptr)) != HWC2_ERROR_NONE) {
        HWC_LOGE(display, "%s:: %d display fail to release fence number (ret: %d)",
                __func__, display->getDisplayId(), err);
        return -err;
    }

    std::vector<hwc2_layer_t> layerIds(outNumElements);
    std::vector<int32_t> fences(outNumElements);

    if ((err = display->getReleaseFences(&outNumElements, layerIds.data(), (int32_t*)fences.data())) != HWC2_ERROR_NONE) {
        HWC_LOGE(display, "%s:: %d display fail to release fence(ret: %d)",
                __func__, display->getDisplayId(), err);
        return -err;
    }

    for (int32_t i = 0; i < cnt-1; i++) {
        ExynosLayer *layer = (ExynosLayer *)display->mLayers[i];
        bool found = false;
        for (uint32_t j = 0; j < outNumElements; j++) {
            ExynosLayer *deviceLayer = (ExynosLayer *)layerIds[j];
            if (layer == deviceLayer) {
                found = true;
                hwc_layer_1_t &hwLayer = contents->hwLayers[i];
                hwLayer.releaseFenceFd = fences[j];
            }
        }
        if ((found == false) && (layer->mCompositionType != HWC2_COMPOSITION_CLIENT)) {
            HWC_LOGE(display, "layer[%d] fence is not returned (compositionType: %d) ",
                    i, layer->mCompositionType);
        }

        if ((found == false) && (layer->mReleaseFence > 0)) {
            HWC_LOGE(display, "layer[%d] fence is should be closed (compositionType: %d) ",
                    i, layer->mCompositionType);
            close(layer->mReleaseFence);
            layer->mReleaseFence = -1;
            String8 errString;
            errString.appendFormat("%s::layer[%d] type(%d), fence(%d) should be closed",
                    __func__, i, layer->mCompositionType, layer->mReleaseFence);
            display->printDebugInfos(errString);
        }
    }

    if ((cnt - 1) >= 0) {
        hwc_layer_1_t &fb_target_layer = contents->hwLayers[cnt - 1];
        if (display->mClientCompositionInfo.mHasCompositionLayer == false) {
            fb_target_layer.releaseFenceFd = -1;
            HDEBUGLOGD(eDebugFence, "ClientTarget source is empty, fence(-1)");
        } else if (outRetireFence != -1) {
            fb_target_layer.releaseFenceFd = hwc_dup(outRetireFence, display, FENCE_TYPE_RETIRE, FENCE_IP_DPP);
            HDEBUGLOGD(eDebugFence, "ClientTarget fence(%d)", fb_target_layer.releaseFenceFd);
        } else {
            fb_target_layer.releaseFenceFd = -1;
            HDEBUGLOGD(eDebugFence, "ClientTarget fence is -1, outRetireFence is -1");
        }
    }
#ifdef TARGET_USES_HWC2
    for (uint32_t i = 0; i < (uint32_t) contents->numHwLayers - 1; i++) {
        hwLayer = &contents->hwLayers[i];
        if ((hwLayer->flags & HWC_DIM_LAYER) && (hwLayer->handle == NULL)) {
            hwLayer->backgroundColor.r = 0;
            hwLayer->backgroundColor.g = 0;
            hwLayer->backgroundColor.b = 0;
            hwLayer->backgroundColor.a = 255;
        }
    }
#endif

// Test only
#if 0
    ALOGE("HWC2: set done ------------");
#endif

    return HWC2_ERROR_NONE;
}

void clearFailedDisplays(ExynosDevice *device, hwc_display_contents_1_t **displays,
        uint32_t failedDisplays) {

    hwc_display_contents_1_t *fimd_contents = displays[HWC_DISPLAY_PRIMARY];
    hwc_display_contents_1_t *external_contents = displays[HWC_DISPLAY_EXTERNAL];
#ifdef USES_VIRTUAL_DISPLAY
    hwc_display_contents_1_t *virtual_contents = displays[HWC_DISPLAY_VIRTUAL];
#endif
    String8 errString;

    if ((fimd_contents != NULL) && (failedDisplays & ePrimaryDisplay)) {
        ExynosDisplay *primary_display = device->getDisplay(HWC_DISPLAY_PRIMARY);
        HWC_LOGE(primary_display, "Clear primary display by set error!!");
        errString.clear();
        errString.appendFormat("%s:: primary display set error", __func__);
        primary_display->printDebugInfos(errString);

        for (size_t i = 0; i < fimd_contents->numHwLayers; i++)
        {
            hwc_layer_1_t &hwcLayer = fimd_contents->hwLayers[i];
            if (hwcLayer.acquireFenceFd > 0)
                fence_close(hwcLayer.acquireFenceFd, primary_display, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
            hwcLayer.acquireFenceFd = -1;
            hwcLayer.releaseFenceFd = -1;
        }
        primary_display->clearDisplay();
        fimd_contents->retireFenceFd = -1;
    }
    if ((external_contents != NULL) && (failedDisplays & eExternalDisplay)) {
        ExynosExternalDisplay *external_display = (ExynosExternalDisplay *)device->getDisplay(HWC_DISPLAY_EXTERNAL);
        HWC_LOGE(external_display, "Clear external display by set error!!");
        errString.clear();
        errString.appendFormat("%s:: external display set error", __func__);
        external_display->printDebugInfos(errString);

        for (size_t i = 0; i < external_contents->numHwLayers; i++)
        {
            hwc_layer_1_t &hwcLayer = external_contents->hwLayers[i];
            if (hwcLayer.acquireFenceFd > 0)
                fence_close(hwcLayer.acquireFenceFd, external_display, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
            hwcLayer.acquireFenceFd = -1;
            hwcLayer.releaseFenceFd = -1;
        }
        external_display->clearDisplay();
        external_contents->retireFenceFd = -1;
    }
#ifdef USES_VIRTUAL_DISPLAY
    if ((virtual_contents != NULL) && (failedDisplays & eVirtualDisplay)) {
        ExynosVirtualDisplay *virtual_display = (ExynosVirtualDisplay *)device->getDisplay(HWC_DISPLAY_VIRTUAL);
        HWC_LOGE(virtual_display, "Clear virtual display by set error!!");
        errString.clear();
        errString.appendFormat("%s:: virtual display set error", __func__);
        virtual_display->printDebugInfos(errString);

        for (size_t i = 0; i < virtual_contents->numHwLayers; i++)
        {
            hwc_layer_1_t &hwcLayer = virtual_contents->hwLayers[i];
            if (hwcLayer.acquireFenceFd > 0)
                fence_close(hwcLayer.acquireFenceFd, virtual_display, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
            hwcLayer.acquireFenceFd = -1;
            hwcLayer.releaseFenceFd = -1;
        }
        /* Clear display is skipped here */
        virtual_contents->retireFenceFd = -1;
    }
#endif
}

int exynos_set(struct hwc_composer_device_1 __unused *dev,
       size_t __unused numDisplays, hwc_display_contents_1_t __unused **displays)
{
//    ALOGD("HWC2 : %s : %d ", __func__, __LINE__);
    ATRACE_CALL();
    int err = 0;
    uint32_t failedDisplays = eDisplayNone;

    if (!numDisplays || !displays)
        return -EINVAL;

    struct exynos_hwc_composer_device_1_t *pdev =
               (struct exynos_hwc_composer_device_1_t*)dev;
    /* To do */
    ExynosDevice *device = (ExynosDevice *)pdev->device;
    if (device == NULL)
        return -EINVAL;
    //    ExynosDisplay *display = device->getDisplay(disp);
    //   if (display == NULL)
    //      return -EINVAL;

    hwc_display_contents_1_t *fimd_contents = displays[HWC_DISPLAY_PRIMARY];
    hwc_display_contents_1_t *external_contents = displays[HWC_DISPLAY_EXTERNAL];
    ExynosDisplay *primary_display = device->getDisplay(HWC_DISPLAY_PRIMARY);

    if ((fimd_contents != NULL) && (fimd_contents->numHwLayers <= 1)) {
        if (primary_display->mPowerModeState != HWC_POWER_MODE_OFF) {
            primary_display->clearDisplay();
            fimd_contents->retireFenceFd = -1;
            HWC_LOGE(primary_display, "HWC2: Clear display with FRAMEBUFFER_TARGET");
        }

        if (fimd_contents->numHwLayers == 1) {
            hwc_layer_1_t &hwLayer = fimd_contents->hwLayers[0];
            if (hwLayer.acquireFenceFd >= 0)
                fence_close(hwLayer.acquireFenceFd, primary_display, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
            hwLayer.acquireFenceFd = -1;
            hwLayer.releaseFenceFd = -1;
        }
    }

    hwc_display_contents_1_t *virtual_contents = displays[HWC_DISPLAY_VIRTUAL];
    if (virtual_contents != NULL) {
        ExynosVirtualDisplay *virtual_display = (ExynosVirtualDisplay *)device->getDisplay(HWC_DISPLAY_VIRTUAL);
        virtual_display->setOutputBuffer(virtual_contents->outbuf, virtual_contents->outbufAcquireFenceFd);
        if ((err = do_set_sequence(virtual_display, virtual_contents)) != HWC2_ERROR_NONE) {
            HWC_LOGE(virtual_display, "%s:: fail to set (ret: %d)", __func__, err);
            failedDisplays |= eVirtualDisplay;
        }
        ALOGV("exynos_set(), virtual_contents %p, retireFenceFd %d",
            virtual_contents, virtual_contents->retireFenceFd);
    }

    if ((fimd_contents != NULL) && (fimd_contents->numHwLayers > 1)) {
        fimd_contents->retireFenceFd = -1;
        if ((err = do_set_sequence(primary_display, fimd_contents)) != HWC2_ERROR_NONE) {
            HWC_LOGE(primary_display, "%s:: fail to set (ret: %d)", __func__, err);
            failedDisplays |= ePrimaryDisplay;
        }
    }

    if (external_contents != NULL){
        ExynosExternalDisplay *external_display = (ExynosExternalDisplay *)device->getDisplay(HWC_DISPLAY_EXTERNAL);
        if (external_display->mSkipStartFrame > (SKIP_EXTERNAL_FRAME - 1)) {
            if ((external_contents->numHwLayers == 1) || (device->hpd_status == false)) {
                if (external_display->mPowerModeState != HWC_POWER_MODE_OFF) {
                    external_display->clearDisplay();
                    external_contents->retireFenceFd = -1;
                    HWC_LOGE(external_display, "HWC2: Clear display with FRAMEBUFFER_TARGET");
                }

                for (size_t i = 0; i < external_contents->numHwLayers; i++) {
                    hwc_layer_1_t &hwLayer = external_contents->hwLayers[i];
                    if (hwLayer.acquireFenceFd >= 0)
                        fence_close(hwLayer.acquireFenceFd, external_display, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
                    hwLayer.acquireFenceFd = -1;
                    hwLayer.releaseFenceFd = -1;
                }
                goto done;
            }

            if ((err = do_set_sequence(external_display, external_contents)) != HWC2_ERROR_NONE) {
                HWC_LOGE(external_display, "%s:: fail to set (ret: %d)", __func__, err);
                failedDisplays |= eExternalDisplay;
            }
        } else {
            external_display->mSkipStartFrame++;
            for (size_t i = 0; i < external_contents->numHwLayers; i++) {
                hwc_layer_1_t &hwLayer = external_contents->hwLayers[i];
                if (hwLayer.acquireFenceFd >= 0)
                    fence_close(hwLayer.acquireFenceFd, external_display, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
                hwLayer.acquireFenceFd = -1;
                hwLayer.releaseFenceFd = -1;
            }

            device->invalidate();
        }
        ALOGV("exynos_set(), external_contents %p, retireFenceFd %d",
            external_contents, external_contents->retireFenceFd);
    }

done:
    if (failedDisplays != 0)
        clearFailedDisplays(device, displays, failedDisplays);

    return HWC2_ERROR_NONE;
}
