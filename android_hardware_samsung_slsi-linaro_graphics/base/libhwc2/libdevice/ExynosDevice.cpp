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

#include "ExynosDevice.h"
#include "ExynosDisplay.h"
#include "ExynosLayer.h"
#include "ExynosPrimaryDisplayModule.h"
#include "ExynosResourceManagerModule.h"
#include "ExynosExternalDisplayModule.h"
#include "ExynosVirtualDisplayModule.h"
#include "ExynosHWCDebug.h"

/**
 * ExynosDevice implementation
 */

class ExynosDevice;

extern void vsync_callback(hwc2_callback_data_t callbackData,
        hwc2_display_t displayId, int64_t timestamp);
extern uint32_t mFenceLogSize;

int hwcDebug;
int hwcFenceDebug[FENCE_IP_ALL];

struct exynos_hwc_cotrol exynosHWCControl;
char fence_names[FENCE_MAX][32];

#ifdef GRALLOC_VERSION1
GrallocWrapper::Mapper* ExynosDevice::mMapper = NULL;
GrallocWrapper::Allocator* ExynosDevice::mAllocator = NULL;
#endif

void handle_vsync_event(ExynosDevice *dev) {

    int err = 0;

    if ((dev == NULL) || (dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].funcPointer == NULL))
        return;

    hwc2_callback_data_t callbackData =
        dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].callbackData;
    HWC2_PFN_VSYNC callbackFunc =
        (HWC2_PFN_VSYNC)dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].funcPointer;

    err = lseek(dev->mVsyncFd, 0, SEEK_SET);

    if (err < 0 ) {
        ExynosDisplay *display = (ExynosDisplay*)dev->getDisplay(HWC_DISPLAY_PRIMARY);
        if (display != NULL) {
            if (display->mVsyncState == HWC2_VSYNC_ENABLE)
                ALOGE("error seeking to vsync timestamp: %s", strerror(errno));
        }
        return;
    }

    if (callbackData != NULL && callbackFunc != NULL) {
        /** Vsync read **/
        char buf[4096];
        err = read(dev->mVsyncFd , buf, sizeof(buf));
        if (err < 0) {
            ALOGE("error reading vsync timestamp: %s", strerror(errno));
            return;
        }

        if (dev->mExtVsyncEnabled == 1) {
            //ALOGD("Skip primary vsync");
            return;
        }

        dev->mTimestamp = strtoull(buf, NULL, 0);

        /** Vsync callback **/
        callbackFunc(callbackData, HWC_DISPLAY_PRIMARY, dev->mTimestamp);
    }
}

void handle_external_vsync_event(ExynosDevice *dev) {

    int err = 0;

    if ((dev == NULL) || (dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].funcPointer == NULL))
        return;

    hwc2_callback_data_t callbackData =
        dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].callbackData;
    HWC2_PFN_VSYNC callbackFunc =
        (HWC2_PFN_VSYNC)dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].funcPointer;

    err = lseek(dev->mExtVsyncFd, 0, SEEK_SET);

    if(err < 0 ) {
        if (dev->hpd_status) {
            ALOGE("error seeking to vsync timestamp: %s", strerror(errno));
        }
        return;
    }

    if (callbackData != NULL && callbackFunc != NULL) {
        /** Vsync read **/
        char buf[4096];
        err = read(dev->mExtVsyncFd , buf, sizeof(buf));
        if (err < 0) {
            ALOGE("error reading vsync timestamp: %s", strerror(errno));
            return;
        }

        if (dev->mExtVsyncEnabled == 0) {
            //ALOGD("Skip external vsync");
            return;
        }
        dev->mTimestamp = strtoull(buf, NULL, 0);

        /** Vsync callback **/
        callbackFunc(callbackData, HWC_DISPLAY_PRIMARY, dev->mTimestamp);
    }
}

void handle_hpd_uevent(ExynosDevice *dev,
        const char *buff, int len)
{
    if ((dev == NULL) || (dev->mCallbackInfos[HWC2_CALLBACK_HOTPLUG].callbackData == NULL))
        return;

    hwc2_callback_data_t callbackData =
        dev->mCallbackInfos[HWC2_CALLBACK_HOTPLUG].callbackData;
    HWC2_PFN_HOTPLUG callbackFunc =
        (HWC2_PFN_HOTPLUG)dev->mCallbackInfos[HWC2_CALLBACK_HOTPLUG].funcPointer;

    ExynosExternalDisplayModule *display = (ExynosExternalDisplayModule*)dev->getDisplay(HWC_DISPLAY_EXTERNAL);

    const char *s = buff;
    s += strlen(s) + 1;

    while (*s) {
        if (!strncmp(s, "SWITCH_STATE=", strlen("SWITCH_STATE=")))
            dev->hpd_status = atoi(s + strlen("SWITCH_STATE=")) == 1;

        s += strlen(s) + 1;
        if (s - buff >= len)
            break;
    }

    if (dev->hpd_status) {
        if (display->openExternalDisplay() < 0) {
            ALOGE("Error reading DP configuration");
            dev->hpd_status = false;
            return;
        }
        display->mPowerModeState = (hwc2_power_mode_t)HWC_POWER_MODE_NORMAL;
    }
    else {
        display->disable();
        display->closeExternalDisplay();
    }
		ALOGD("mDisplayFd %d", display->mDisplayFd);

    ALOGV("HPD status changed to %s", dev->hpd_status ? "enabled" : "disabled");
    if (dev->hpd_status)
        ALOGI("External Display Resolution changed to %dx%d",
                display->mXres, display->mYres);

    if (callbackData != NULL && callbackFunc != NULL)
        callbackFunc(callbackData, HWC_DISPLAY_EXTERNAL, dev->hpd_status);

    dev->invalidate();
}

void *hwc_dynamicRecomp_thread(void *data) {
    ExynosDevice *dev = (ExynosDevice*)data;
    ExynosDisplay *primary_display = (ExynosDisplay*)dev->getDisplay(HWC_DISPLAY_PRIMARY);
    uint64_t event_cnt = 0;
    android_atomic_inc(&(primary_display->updateThreadStatus));

    while (dev->dynamic_recomp_stat_thread_flag) {
        event_cnt = primary_display->mUpdateEventCnt;
	/*
	 * If there is no update for more than 100ms, favor the 3D composition mode.
	 * If all other conditions are met, mode will be switched to 3D composition.
	 */
        usleep(100000);
        if (event_cnt == primary_display->mUpdateEventCnt) {
            //            dev->dynamicRecompositionThreadLoop();
            if (primary_display->checkDynamicReCompMode() == DEVICE_2_CLIENT) {
                primary_display->mUpdateEventCnt = 0;
                dev->invalidate();
            }
        }
    }
    android_atomic_dec(&(primary_display->updateThreadStatus));

    return NULL;
}

void *hwc_eventHndler_thread(void *data) {

    /** uevent init **/
    char uevent_desc[4096];
    memset(uevent_desc, 0, sizeof(uevent_desc));

    ExynosDevice *dev = (ExynosDevice*)data;

    setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);

    uevent_init();

    /** Vsync init. **/
    char devname[MAX_DEV_NAME + 1];
    devname[MAX_DEV_NAME] = '\0';

    strncpy(devname, VSYNC_DEV_PREFIX, MAX_DEV_NAME);
    strlcat(devname, VSYNC_DEV_NAME, MAX_DEV_NAME);

    dev->mVsyncFd = open(devname, O_RDONLY);

    char devname_ext[MAX_DEV_NAME + 1];
    devname_ext[MAX_DEV_NAME] = '\0';

    strncpy(devname_ext, VSYNC_DEV_PREFIX, MAX_DEV_NAME);
    strlcat(devname_ext, VSYNC_DEV_NAME_EXT, MAX_DEV_NAME);

    dev->mExtVsyncFd = open(devname_ext, O_RDONLY);

    if (dev->mVsyncFd < 0) {
        ALOGI("Failed to open vsync attribute at %s", devname);
        devname[strlen(VSYNC_DEV_PREFIX)] = '\0';
        strlcat(devname, VSYNC_DEV_MIDDLE, MAX_DEV_NAME);
        strlcat(devname, VSYNC_DEV_NAME, MAX_DEV_NAME);
        ALOGI("Retrying with %s", devname);
        dev->mVsyncFd = open(devname, O_RDONLY);
        ALOGI("dev->mVsyncFd %d", dev->mVsyncFd);
    }

    if (dev->mExtVsyncFd < 0) {
        ALOGI("Failed to open vsync attribute at %s", devname_ext);
        devname_ext[strlen(VSYNC_DEV_PREFIX)] = '\0';
        strlcat(devname_ext, VSYNC_DEV_MIDDLE, MAX_DEV_NAME);
        strlcat(devname_ext, VSYNC_DEV_NAME_EXT, MAX_DEV_NAME);
        ALOGI("Retrying with %s", devname_ext);
        dev->mExtVsyncFd = open(devname_ext, O_RDONLY);
        ALOGI("dev->mExtVsyncFd %d", dev->mExtVsyncFd);
    }
    /** Poll definitions **/
    /** TODO : Hotplug here **/

    struct pollfd fds[3];

    fds[0].fd = dev->mVsyncFd;
    fds[0].events = POLLPRI;
    fds[1].fd = uevent_get_fd();
    fds[1].events = POLLIN;
    fds[2].fd = dev->mExtVsyncFd;
    fds[2].events = POLLPRI;

    /** Polling events **/
    while (true) {
        int err = poll(fds, 3, -1);

        if (err > 0) {
            if (fds[0].revents & POLLPRI) {
                handle_vsync_event((ExynosDevice*)dev);
            }
            else if (fds[1].revents & POLLIN) {
                int len = uevent_next_event(uevent_desc,
                        sizeof(uevent_desc) - 2);

                bool dp_status = !strcmp(uevent_desc,
                        "change@/devices/virtual/switch/hdmi");

                if (dp_status)
                    handle_hpd_uevent((ExynosDevice*)dev, uevent_desc, len);
            }
            else if (fds[2].revents & POLLPRI) {
                handle_external_vsync_event((ExynosDevice*)dev);
            }
        }
        else if (err == -1) {
            if (errno == EINTR)
                break;
            ALOGE("error in vsync thread: %s", strerror(errno));
        }
    }
    return NULL;
}

ExynosDevice::ExynosDevice() 
    : mGeometryChanged(0),
    mDynamicRecompositionThread(0),
    mVsyncFd(-1),
    mExtVsyncFd(-1),
    mTimestamp(0),
    hpd_status(false)
{
    mResolutionChanged = false;
    mResolutionHandled = true;

    ALOGD("HWC2 : %s : %d ", __func__, __LINE__);
    int ret = 0;

#ifdef GRALLOC_VERSION1
    mMapper = new GrallocWrapper::Mapper();
    mAllocator = new GrallocWrapper::Allocator(*mMapper);
#else
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                (const struct hw_module_t **)&mGrallocModule)) {
        ALOGE("failed to get gralloc hw module");
        mAllocDevice = NULL;
    } else {
        if (gralloc_open((const hw_module_t *)mGrallocModule,
                    &mAllocDevice)) {
            ALOGE("failed to open gralloc");
            mAllocDevice = NULL;
        }
    }
#endif

    exynosHWCControl.multiResolution = true;
    mResourceManager = (ExynosResourceManager *)new ExynosResourceManagerModule(this);
    ExynosPrimaryDisplayModule *primary_display = new ExynosPrimaryDisplayModule(HWC_DISPLAY_PRIMARY, this);

    primary_display->mPlugState = true;
    primary_display->mUseSecureDMA = true;
    ExynosMPP::mainDisplayWidth = primary_display->mXres;
    if (ExynosMPP::mainDisplayWidth <= 0) {
        ExynosMPP::mainDisplayWidth = 1440;
    }
    ExynosMPP::mainDisplayHeight = primary_display->mYres;
    if (ExynosMPP::mainDisplayHeight <= 0) {
        ExynosMPP::mainDisplayHeight = 2560;
    }

    ExynosExternalDisplayModule *external_display = new ExynosExternalDisplayModule(HWC_DISPLAY_EXTERNAL, this);

    mDisplays.add((ExynosDisplay*) primary_display);
    mDisplays.add((ExynosDisplay*) external_display);

#ifdef USES_VIRTUAL_DISPLAY
    ExynosVirtualDisplayModule *virtual_display = new ExynosVirtualDisplayModule(HWC_DISPLAY_VIRTUAL, this);
    mDisplays.add((ExynosDisplay*) virtual_display);
#endif
    mNumVirtualDisplay = 0;

    memset(mCallbackInfos, 0, sizeof(mCallbackInfos));

    /** Event handler thread creation **/
    /* TODO : Check last argument */
    ret = pthread_create(&mEventHandlerThread, NULL, hwc_eventHndler_thread, this);
    if (ret) {
        ALOGE("failed to start vsync thread: %s", strerror(ret));
        ret = -ret;
    }

    hwcDebug = 0;
    for (uint32_t i = 0; i < FENCE_IP_ALL; i++)
        hwcFenceDebug[i] = 0;

    mExtVsyncEnabled = 0;
    exynosHWCControl.forceGpu = false;
    exynosHWCControl.windowUpdate = true;
    exynosHWCControl.forcePanic = false;
    exynosHWCControl.skipStaticLayers = true;
    exynosHWCControl.skipM2mProcessing = true;
    exynosHWCControl.useMaxG2DSrc = false;
    exynosHWCControl.skipWinConfig = false;
    exynosHWCControl.doFenceFileDump = false;
    exynosHWCControl.fenceTracer = 0;
    exynosHWCControl.sysFenceLogging = false;
    exynosHWCControl.useDynamicRecomp = (primary_display->mPsrMode == PSR_NONE);

    mResourceManager->doPreProcessing();

    for (uint32_t i = 0; i < FENCE_MAX; i++) {
        memset(fence_names[i], 0, sizeof(fence_names[0]));
        sprintf(fence_names[i], "_%2dh", i);
    }

    dynamicRecompositionThreadLoop();

    String8 saveString;
    saveString.appendFormat("ExynosDevice is initialized");
    uint32_t errFileSize = saveErrorLog(saveString);
    ALOGI("Initial errlog size: %d bytes\n", errFileSize);
}

ExynosDevice::~ExynosDevice() {

    ExynosDisplay *primary_display = getDisplay(HWC_DISPLAY_PRIMARY);

    /* TODO kill threads here */
    pthread_kill(mEventHandlerThread, SIGTERM);
    pthread_join(mEventHandlerThread, NULL);
    pthread_kill(mDynamicRecompositionThread, SIGTERM);
    pthread_join(mDynamicRecompositionThread, NULL);

#ifdef GRALLOC_VERSION1
    if (mMapper != NULL)
        delete mMapper;
    if (mAllocator != NULL)
        delete mAllocator;
#endif

    delete primary_display;
}

bool ExynosDevice::isFirstValidate()
{
    for (uint32_t i = 0; i < mDisplays.size(); i++) {
        if ((mDisplays[i]->mPlugState == true) &&
            ((mDisplays[i]->mRenderingState != RENDERING_STATE_NONE) &&
             (mDisplays[i]->mRenderingState != RENDERING_STATE_PRESENTED)))
            return false;
    }
    return true;
}

bool ExynosDevice::isLastValidate(ExynosDisplay *display)
{
    for (uint32_t i = 0; i < mDisplays.size(); i++) {
        if (mDisplays[i] == display)
            continue;
        if ((mDisplays[i]->mPlugState == true) &&
            (mDisplays[i]->mRenderingState != RENDERING_STATE_VALIDATED) &&
            (mDisplays[i]->mRenderingState != RENDERING_STATE_ACCEPTED_CHANGE))
            return false;
    }
    return true;
}

/**
 * @param width
 * @param height
 * @return ExynosDisplay
 */
ExynosDisplay* ExynosDevice::createVirtualDisplay(uint32_t __unused width, uint32_t __unused height) {
    return 0;
}

/**
 * @param *display
 * @return int32_t
 */
int32_t ExynosDevice::destroyVirtualDisplay(ExynosDisplay* __unused display) {
    return 0;
}

/**
 * @return bool
 */
bool ExynosDevice::dynamicRecompositionThreadLoop() {

    /* TODO implementation here */
    /* TODO : Create dynamic recomposition thread here */
    if (exynosHWCControl.useDynamicRecomp == true) {
        /* pthread_create shouldn't have ben failed. But, ignore even if some error */
        if (pthread_create(&mDynamicRecompositionThread, NULL, hwc_dynamicRecomp_thread, this) != 0) {
            ALOGE("%s: failed to start hwc_dynamicRecomp_thread thread:", __func__);
            dynamic_recomp_stat_thread_flag = false;
        } else {
            dynamic_recomp_stat_thread_flag = true;
        }
    }
    return false;
}

/**
 * @param display
 * @return ExynosDisplay
 */
ExynosDisplay* ExynosDevice::getDisplay(uint32_t display) {

    if (!mDisplays.isEmpty() || 0 == display || display > HWC_NUM_DISPLAY_TYPES) {
        for (size_t i = 0; i < mDisplays.size(); i++) {
            if (mDisplays[i]->mDisplayId == display)
                return (ExynosDisplay*)mDisplays[i];
        }
    }

    return NULL;
}

/**
 * Device Functions for HWC 2.0
 */

int32_t ExynosDevice::createVirtualDisplay(
        uint32_t __unused width, uint32_t __unused height, hwc2_display_t* __unused outDisplay) {
    return 0;
}

int32_t ExynosDevice::destoryVirtualDisplay(
        hwc2_display_t __unused display) {
    return 0;
}


void ExynosDevice::dump(uint32_t *outSize, char *outBuffer) {
    /* TODO : Dump here */

    if (outSize == NULL) {
        ALOGE("%s:: outSize is null", __func__);
        return;
    }

    ExynosDisplay *display = mDisplays[HWC_DISPLAY_PRIMARY];
    ExynosDisplay *external_display = mDisplays[HWC_DISPLAY_EXTERNAL];
    ExynosDisplay *virtual_display = mDisplays[HWC_DISPLAY_VIRTUAL];

    android::String8 result;

    result.append("\n\n");
    result.append("Primary device's config information\n");

    {
        Mutex::Autolock lock(display->mDisplayMutex);
        ExynosCompositionInfo clientCompInfo = display->mClientCompositionInfo;
        ExynosCompositionInfo exynosCompInfo = display->mExynosCompositionInfo;

        clientCompInfo.dump(result);
        exynosCompInfo.dump(result);

        for (uint32_t i = 0; i < display->mLayers.size(); i++) {
            ExynosLayer *layer = display->mLayers[i];
            layer->dump(result);
        }
    }
    result.append("\n");


    if (external_display->mPlugState == true) {
        Mutex::Autolock lock(external_display->mDisplayMutex);
        result.append("External device's config information\n");

        ExynosCompositionInfo external_clientCompInfo = external_display->mClientCompositionInfo;
        ExynosCompositionInfo external_exynosCompInfo = external_display->mExynosCompositionInfo;

        external_clientCompInfo.dump(result);
        external_exynosCompInfo.dump(result);

        for (uint32_t i = 0; i < external_display->mLayers.size(); i++) {
            ExynosLayer *external_layer = external_display->mLayers[i];
            external_layer->dump(result);
        }

        result.append("\n");
    }

    if (virtual_display->mPlugState == true) {
        Mutex::Autolock lock(virtual_display->mDisplayMutex);
        result.append("Virtual device's config information\n");

        ExynosCompositionInfo virtual_clientCompInfo = virtual_display->mClientCompositionInfo;
        ExynosCompositionInfo virtual_exynosCompInfo = virtual_display->mExynosCompositionInfo;

        virtual_clientCompInfo.dump(result);
        virtual_exynosCompInfo.dump(result);

        for (uint32_t i = 0; i < virtual_display->mLayers.size(); i++) {
            ExynosLayer *virtual_layer = virtual_display->mLayers[i];
            virtual_layer->dump(result);
        }

        result.append("\n");
    }

    if (outBuffer == NULL) {
        *outSize = (uint32_t)result.length();
    } else {
        if (*outSize == 0) {
            ALOGE("%s:: outSize is 0", __func__);
            return;
        }
        uint32_t copySize = *outSize;
        if (*outSize > result.size())
            copySize = (uint32_t)result.size();
        ALOGI("HWC dump:: resultSize(%zu), outSize(%d), copySize(%d)", result.size(), *outSize, copySize);
        strlcpy(outBuffer, result.string(), copySize);
    }

    return;
}

uint32_t ExynosDevice::getMaxVirtualDisplayCount() {
    return 0;
}

int32_t ExynosDevice::registerCallback (
        int32_t descriptor, hwc2_callback_data_t callbackData,
        hwc2_function_pointer_t point) {
    /* TODO : Implementation here */

    if (callbackData == NULL || point == NULL ||
            descriptor < 0 || descriptor > HWC2_CALLBACK_VSYNC)
        return HWC2_ERROR_BAD_PARAMETER;

    mCallbackInfos[descriptor].callbackData = callbackData;
    mCallbackInfos[descriptor].funcPointer = point;

    return HWC2_ERROR_NONE;
}

void ExynosDevice::handleVirtualHpd()
{
    ALOGV("handle_virtual_hpd");

    if (this->hpd_status)
        ALOGD("hpd_status is true");
    else ALOGD("hpd_status is false");

    if (this->mCallbackInfos[HWC2_CALLBACK_HOTPLUG].callbackData == NULL)
        return;

    hwc2_callback_data_t callbackData =
        this->mCallbackInfos[HWC2_CALLBACK_HOTPLUG].callbackData;
    HWC2_PFN_HOTPLUG callbackFunc =
        (HWC2_PFN_HOTPLUG)this->mCallbackInfos[HWC2_CALLBACK_HOTPLUG].funcPointer;

	ExynosExternalDisplay *display = (ExynosExternalDisplay*)this->getDisplay(HWC_DISPLAY_EXTERNAL);

    if (this->hpd_status) {
        if (display->openExternalDisplay() < 0) {
        //if ((display->openExternalDisplay() > 0) && display->getConfig()) {
            ALOGE("DP driver open fail ");
            this->hpd_status = false;
            return;
        }
        display->mPowerModeState = (hwc2_power_mode_t)HWC_POWER_MODE_NORMAL;
    }
    else {
        display->disable();
        display->closeExternalDisplay();
    }


    ALOGV("HPD status changed to %s", this->hpd_status ? "enabled" : "disabled");
    if (this->hpd_status)
        ALOGI("External Display Resolution changed to %dx%d",
                display->mXres, display->mYres);

    if (callbackData != NULL && callbackFunc != NULL){
        ALOGV("callback func");
        callbackFunc(callbackData, HWC_DISPLAY_EXTERNAL, this->hpd_status);
    }
}

void ExynosDevice::invalidate()
{
    HWC2_PFN_REFRESH callbackFunc =
        (HWC2_PFN_REFRESH)mCallbackInfos[HWC2_CALLBACK_REFRESH].funcPointer;
    if (callbackFunc != NULL)
        callbackFunc(mCallbackInfos[HWC2_CALLBACK_REFRESH].callbackData,
                HWC_DISPLAY_PRIMARY);
    else
        ALOGE("%s:: refresh callback is not registered", __func__);

}

void ExynosDevice::setHWCDebug(unsigned int debug)
{
    hwcDebug = debug;
}

uint32_t ExynosDevice::getHWCDebug()
{
    return hwcDebug;
}

void ExynosDevice::setHWCFenceDebug(uint32_t typeNum, uint32_t ipNum, uint32_t mode)
{
    if (typeNum > FENCE_TYPE_ALL || typeNum < 0 || ipNum > FENCE_IP_ALL || ipNum < 0
            || mode > 1 || mode < 0) {
        ALOGE("%s:: input is not valid type(%u), IP(%u), mode(%d)", __func__, typeNum, ipNum, mode);
        return;
    }

    uint32_t value = 0;

    if (typeNum == FENCE_TYPE_ALL)
        value = (1 << FENCE_TYPE_ALL) - 1;
    else
        value = 1 << typeNum;

    if (ipNum == FENCE_IP_ALL) {
        for (uint32_t i = 0; i < FENCE_IP_ALL; i++) {
            if (mode)
                hwcFenceDebug[i] |= value;
            else
                hwcFenceDebug[i] &= (~value);
        }
    } else {
        if (mode)
            hwcFenceDebug[ipNum] |= value;
        else
            hwcFenceDebug[ipNum] &= (~value);
    }
}

void ExynosDevice::getHWCFenceDebug()
{
    for (uint32_t i = 0; i < FENCE_IP_ALL; i++)
        ALOGE("[HWCFenceDebug] IP_Number(%d) : Debug(%x)", i, hwcFenceDebug[i]);
}

void ExynosDevice::setForceGPU(unsigned int on)
{
    exynosHWCControl.forceGpu = on;
}

void ExynosDevice::setWinUpdate(unsigned int on)
{
    exynosHWCControl.windowUpdate = on;
}

void ExynosDevice::setForcePanic(unsigned int on)
{
    exynosHWCControl.forcePanic = on;
}

void ExynosDevice::setSkipStaticLayer(unsigned int on)
{
    exynosHWCControl.skipStaticLayers = on;
}

void ExynosDevice::setSkipM2mProcessing(unsigned int on)
{
    exynosHWCControl.skipM2mProcessing = on;
}

void ExynosDevice::setDynamicRecomposition(unsigned int on)
{
    exynosHWCControl.useDynamicRecomp = on;
}

void ExynosDevice::setFenceTracer(unsigned int on)
{
    exynosHWCControl.fenceTracer = on;
}

void ExynosDevice::setDoFenceFileDump(unsigned int on)
{
    exynosHWCControl.doFenceFileDump = on;
}

void ExynosDevice::setSysFenceLogging(unsigned int on)
{
    exynosHWCControl.sysFenceLogging = on;
}

void ExynosDevice::setMaxG2dProcessing(unsigned int on)
{
    exynosHWCControl.useMaxG2DSrc = on;
}

uint32_t ExynosDevice::checkConnection(uint32_t display)
{
    int ret = 0;
	ExynosExternalDisplay *external_display = (ExynosExternalDisplay *)mDisplays[HWC_DISPLAY_EXTERNAL];
	ExynosVirtualDisplay *virtual_display = (ExynosVirtualDisplay *)mDisplays[HWC_DISPLAY_VIRTUAL];

    switch(display) {
        case HWC_DISPLAY_PRIMARY:
            return 1;
        case HWC_DISPLAY_EXTERNAL:
            if (external_display->mDisplayFd > 0)
                return 1;
            else
                return 0;
        case HWC_DISPLAY_VIRTUAL:
            if ((virtual_display != NULL) && (virtual_display->mDisplayFd > 0))
                return 1;
            else
                return 0;
        default:
            return 0;
    }
    return ret;
}
#ifdef GRALLOC_VERSION1
void ExynosDevice::getAllocator(GrallocWrapper::Mapper** mapper, GrallocWrapper::Allocator** allocator)
{
    if ((mMapper == NULL) && (mAllocator == NULL)) {
        ALOGI("%s:: Allocator is created", __func__);
        mMapper = new GrallocWrapper::Mapper();
        mAllocator = new GrallocWrapper::Allocator(*mMapper);
    }
    *mapper = mMapper;
    *allocator = mAllocator;
}
#endif

bool ExynosDevice::validateFences(ExynosDisplay *display) {

    return 1;

    if (!validateFencePerFrame(display)) {
        String8 errString;
        errString.appendFormat("You should doubt fence leak!\n");
        return false;
    }

    if (fenceWarn(display, 100)) {
        String8 errString;
        errString.appendFormat("Fence leak!\n");
        printLeakFds(display);
        ALOGE("Fence leak! --");
        exynosHWCControl.fenceTracer = 1;
        saveFenceTrace(display);
        return false;
    }

    if (exynosHWCControl.doFenceFileDump) {
        ALOGE("Fence file dump !");
        if (mFenceLogSize != 0)
            ALOGE("Fence file not empty!");
        saveFenceTrace(display);
        exynosHWCControl.doFenceFileDump = false;
    }

    return true;
}
