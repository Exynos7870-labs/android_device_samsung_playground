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
#include "ExynosHWCService.h"
#include "exynos_v4l2.h"
#include "ExynosHWCDebug.h"
#include "ExynosVirtualDisplayModule.h"
#include "ExynosVirtualDisplay.h"
#include "ExynosExternalDisplay.h"
#define HWC_SERVICE_DEBUG 0

namespace android {

ANDROID_SINGLETON_STATIC_INSTANCE(ExynosHWCService);

ExynosHWCService::ExynosHWCService() :
    mHWCService(NULL),
    mHWCCtx(NULL),
    bootFinishedCallback(NULL),
    doPSRExit(NULL)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "ExynosHWCService Constructor is called");
}

ExynosHWCService::~ExynosHWCService()
{
   ALOGD_IF(HWC_SERVICE_DEBUG, "ExynosHWCService Destructor is called");
}

int ExynosHWCService::addVirtualDisplayDevice()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);

    mHWCCtx->device->mNumVirtualDisplay++;

    return NO_ERROR;
}

int ExynosHWCService::destroyVirtualDisplayDevice()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);

    mHWCCtx->device->mNumVirtualDisplay--;

    return NO_ERROR;
}

int ExynosHWCService::setWFDMode(unsigned int mode)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::mode=%d", __func__, mode);

    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->setWFDMode(mode);
        }
    }

    return INVALID_OPERATION;
}

int ExynosHWCService::getWFDMode()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);

    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->getWFDMode();
        }
    }

    return INVALID_OPERATION;
}

int ExynosHWCService::getWFDInfo(int32_t* state, int32_t* compositionType, int32_t* format,
    int64_t *usage, int32_t* width, int32_t* height)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->getWFDInfo(state, compositionType, format,
                       usage, width, height);
        }
    }

    return INVALID_OPERATION;
}

int ExynosHWCService::setSecureVDSMode(unsigned int mode)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::mode=%d", __func__, mode);

    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->setSecureVDSMode(mode);
        }
    }

    return INVALID_OPERATION;
}

int ExynosHWCService::setWFDOutputResolution(unsigned int width, unsigned int height)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::width=%d, height=%d", __func__, width, height);

    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->setWFDOutputResolution(width, height);
        }
    }

    return INVALID_OPERATION;
}

void ExynosHWCService::getWFDOutputResolution(unsigned int *width, unsigned int *height)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);

    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            virtualdisplay->getWFDOutputResolution(width, height);
            return;
        }
    }
}

void ExynosHWCService::setPresentationMode(bool use)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::PresentationMode=%s", __func__, use == false ? "false" : "true");

    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            virtualdisplay->setPresentationMode(!!use);
            return;
        }
    }
}

int ExynosHWCService::getPresentationMode()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);

    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->getPresentationMode();
        }
    }

    return INVALID_OPERATION;
}

int ExynosHWCService::setVDSGlesFormat(int format)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::format=%d", __func__, format);

    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->setVDSGlesFormat(format);
        }
    }

    return INVALID_OPERATION;
}

int ExynosHWCService::setVirtualHPD(unsigned int on)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::on/off=%d", __func__, on);
    if (on) {
	    ALOGD("virtual hotplug enabled");
		mHWCCtx->device->hpd_status = true;
		mHWCCtx->device->handleVirtualHpd();
	} else if (on == 0) {
        ALOGD("virtual hotplug diabled");
        mHWCCtx->device->hpd_status = false;
        mHWCCtx->device->handleVirtualHpd();
    }
    mHWCCtx->device->invalidate();
    return NO_ERROR;
}

int ExynosHWCService::getExternalDisplayConfigs()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);

    if (mHWCCtx->device->hpd_status == true) {
        ExynosExternalDisplay *external_display =
            (ExynosExternalDisplay *)mHWCCtx->device->getDisplay(HWC_DISPLAY_EXTERNAL);
        external_display->dumpConfigurations();
    }

    return NO_ERROR;
}

int ExynosHWCService::setExternalDisplayConfig(unsigned int index)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::config=%d", __func__, index);

    if (mHWCCtx->device->hpd_status == true) {
        ExynosExternalDisplay *external_display =
            (ExynosExternalDisplay *)mHWCCtx->device->getDisplay(HWC_DISPLAY_EXTERNAL);
        external_display->setActiveConfig(index);
    }

    return NO_ERROR;
}

int ExynosHWCService::setExternalVsyncEnabled(unsigned int index)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::config=%d", __func__, index);

    mHWCCtx->device->mExtVsyncEnabled = index;
    ExynosExternalDisplay *external_display =
        (ExynosExternalDisplay *)mHWCCtx->device->getDisplay(HWC_DISPLAY_EXTERNAL);
    external_display->setVsyncEnabled(index);

    return NO_ERROR;
}

int ExynosHWCService::getExternalHdrCapabilities()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    return 0;
}

void ExynosHWCService::setBootFinishedCallback(void (*callback)(exynos_hwc_composer_device_1_t *))
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, callback %p", __func__, callback);
    bootFinishedCallback = callback;
}

void ExynosHWCService::setBootFinished() {
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    if (bootFinishedCallback != NULL)
        bootFinishedCallback(mHWCCtx);
}

void ExynosHWCService::enableMPP(uint32_t physicalType, uint32_t physicalIndex, uint32_t logicalIndex, uint32_t enable)
{
    ALOGD("%s:: type(%d), index(%d, %d), enable(%d)",
            __func__, physicalType, physicalIndex, logicalIndex, enable);
    ExynosResourceManager::enableMPP(physicalType, physicalIndex, logicalIndex, enable);
    mHWCCtx->device->invalidate();
}

void ExynosHWCService::setHWCDebug(int debug)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, debug %d", __func__, debug);
    mHWCCtx->device->setHWCDebug(debug);
}

uint32_t ExynosHWCService::getHWCDebug()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    return mHWCCtx->device->getHWCDebug();
}

void ExynosHWCService::setHWCFenceDebug(uint32_t fenceNum, uint32_t ipNum, uint32_t mode)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    mHWCCtx->device->setHWCFenceDebug(fenceNum, ipNum, mode);
}

void ExynosHWCService::getHWCFenceDebug()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    mHWCCtx->device->getHWCFenceDebug();
}

int ExynosHWCService::setHWCCtl(uint32_t display, uint32_t ctrl, int32_t val)
{
    int err = 0;
    switch (ctrl) {
    case HWC_CTL_FORCE_GPU:
        ALOGI("%s::HWC_CTL_FORCE_GPU on/off=%d", __func__, val);
        mHWCCtx->device->setForceGPU((unsigned int)val);
        mHWCCtx->device->invalidate();
        break;
    case HWC_CTL_WINDOW_UPDATE:
        ALOGI("%s::HWC_CTL_WINDOW_UPDATE on/off=%d", __func__, val);
        mHWCCtx->device->setWinUpdate((unsigned int)val);
        mHWCCtx->device->invalidate();
        break;
    case HWC_CTL_FORCE_PANIC:
        ALOGI("%s::HWC_CTL_FORCE_PANIC on/off=%d", __func__, val);
        mHWCCtx->device->setForcePanic((unsigned int)val);
        break;
    case HWC_CTL_SKIP_STATIC:
        ALOGI("%s::HWC_CTL_SKIP_STATIC on/off=%d", __func__, val);
        mHWCCtx->device->setSkipStaticLayer((unsigned int)val);
        break;
    case HWC_CTL_SKIP_M2M_PROCESSING:
        ALOGI("%s::HWC_CTL_SKIP_M2M_PROCESSING on/off=%d", __func__, val);
        mHWCCtx->device->setSkipM2mProcessing((unsigned int)val);
        break;
    case HWC_CTL_DYNAMIC_RECOMP:
        ALOGI("%s::HWC_CTL_DYNAMIC_RECOMP on/off=%d", __func__, val);
        mHWCCtx->device->setDynamicRecomposition((unsigned int)val);
        break;
    case HWC_CTL_MAX_G2D_PROCESSING:
        ALOGI("%s::HWC_CTL_MAX_G2D_PROCESSING on/off=%d", __func__, val);
        mHWCCtx->device->setMaxG2dProcessing((unsigned int)val);
        break;
    case HWC_CTL_ENABLE_FENCE_TRACER:
        ALOGI("%s::HWC_CTL_ENABLE_FENCE_TRACER on/off=%d", __func__, val);
        mHWCCtx->device->setFenceTracer((unsigned int)val);
        break;
    case HWC_CTL_DO_FENCE_FILE_DUMP:
        ALOGI("%s::HWC_CTL_DO_FENCE_FILE_DUMP on/off=%d", __func__, val);
        mHWCCtx->device->setDoFenceFileDump((unsigned int)val);
        break;
    case HWC_CTL_SYS_FENCE_LOGGING:
        ALOGI("%s::HWC_CTL_SYS_FENCE_LOGGING on/off=%d", __func__, val);
	mHWCCtx->device->setSysFenceLogging((unsigned int)val);
        break;
    default:
        ALOGE("%s(%d): unsupported HWC_CTL", __func__, display);
        err = -1;
        break;
    }
    return err;
}

#if 0
void ExynosHWCService::setPSRExitCallback(void (*callback)(exynos_hwc_composer_device_1_t *))
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, callback %p", __func__, callback);
    doPSRExit = callback;
}

void ExynosHWCService::notifyPSRExit()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, doPSRExit %p", __func__, doPSRExit);
    if (doPSRExit != NULL) {
        ALOGD_IF(HWC_SERVICE_DEBUG, "%s, line %d", __func__, __LINE__);
        doPSRExit(mHWCCtx);
    }
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, line %d", __func__, __LINE__);
}

#endif

int ExynosHWCService::createServiceLocked()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::", __func__);
    sp<IServiceManager> sm = defaultServiceManager();
    sm->addService(String16("Exynos.HWCService"), mHWCService, false);
    if (sm->checkService(String16("Exynos.HWCService")) != NULL) {
        ALOGD_IF(HWC_SERVICE_DEBUG, "adding Exynos.HWCService succeeded");
        return 0;
    } else {
        ALOGE_IF(HWC_SERVICE_DEBUG, "adding Exynos.HWCService failed");
        return -1;
    }
}

ExynosHWCService *ExynosHWCService::getExynosHWCService()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::", __func__);
    ExynosHWCService& instance = ExynosHWCService::getInstance();
    Mutex::Autolock _l(instance.mLock);
    if (instance.mHWCService == NULL) {
        instance.mHWCService = &instance;
        int status = ExynosHWCService::getInstance().createServiceLocked();
        if (status != 0) {
            ALOGE_IF(HWC_SERVICE_DEBUG, "getExynosHWCService failed");
        }
    }
    return instance.mHWCService;
}

void ExynosHWCService::setExynosHWCCtx(ExynosHWCCtx *HWCCtx)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, HWCCtx=%p", __func__, HWCCtx);
    if(HWCCtx) {
        mHWCCtx = HWCCtx;
    }
}

int ExynosHWCService::getHdrCapabilities(uint32_t type, int32_t *outNum, std::vector<int32_t>* outTypes,
        float* maxLuminance, float* maxAverageLuminance, float* minLuminance)
{
    int ret;
    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == type) {
            if (outTypes == NULL) {
                ret = mHWCCtx->device->mDisplays[i]->getHdrCapabilities(outNum, NULL, maxLuminance,
                    maxAverageLuminance, minLuminance);

                if (*outNum <= 0 || ret)
                    return -1;
                else
                    return 0;
            } else {
                ret = mHWCCtx->device->mDisplays[i]->getHdrCapabilities(outNum, outTypes->data(),
                        maxLuminance, maxAverageLuminance, minLuminance);
                if (ret)
                    return -1;
                else
                    return 0;
            }
        }
    }
    return -1;
}

}
