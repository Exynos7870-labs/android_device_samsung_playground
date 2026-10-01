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

#ifndef _EXYNOSDEVICE_H
#define _EXYNOSDEVICE_H

#include <unistd.h>
#include <pthread.h>
#include <poll.h>

#include <hardware/gralloc.h>
#include <hardware_legacy/uevent.h>

#include <utils/Vector.h>
#include <utils/Trace.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <cutils/atomic.h>

#include <hardware/hwcomposer2.h>
#include "ExynosHWC.h"
#include "ExynosHWCModule.h"
#include "ExynosHWCHelper.h"
#ifdef GRALLOC_VERSION1
#include "gralloc1_priv.h"
#include "GrallocWrapper.h"
#else
#include "gralloc_priv.h"
#endif

#define MAX_DEV_NAME 128
#define ERROR_LOG_PATH0 "/data/vendor/log/hwc"
#define ERROR_LOG_PATH1 "/data/log"
#define ERR_LOG_SIZE    (1024*1024)     // 1MB
#define FENCE_ERR_LOG_SIZE    (1024*1024)     // 1MB

#ifdef GRALLOC_VERSION1
namespace android {
namespace GrallocWrapper {
    class Mapper;
    class Allocator;
}
}

using namespace android;
#endif

struct exynos_callback_info_t {
    hwc2_callback_data_t callbackData;
    hwc2_function_pointer_t funcPointer;
};

typedef struct exynos_hwc_cotrol {
    uint32_t forceGpu;
    uint32_t windowUpdate;
    uint32_t forcePanic;
    uint32_t skipStaticLayers;
    uint32_t skipM2mProcessing;
    uint32_t multiResolution;
    uint32_t skipWinConfig;
    uint32_t useDynamicRecomp;
    uint32_t useMaxG2DSrc;     /* Use G2D as much as possible  */
    uint32_t doFenceFileDump;
    uint32_t fenceTracer;
    uint32_t sysFenceLogging;
} exynos_hwc_cotrol_t;

enum {
    GEOMETRY_MSC_RESERVED = 0x1,
    GEOMETRY_MPP_FLAG_CHANGED,
};

class ExynosDisplay;
class ExynosResourceManager;

class ExynosDevice {
    public:
        /**
         * TODO : Should be defined as ExynosDisplay type
         * Display list that managed by Device.
         */
        android::Vector< ExynosDisplay* > mDisplays;

        int mNumVirtualDisplay;

        /**
         * Resource manager object that is used to manage HW resources and assign resources to each layers
         */
        ExynosResourceManager *mResourceManager;

#ifdef GRALLOC_VERSION1
        static GrallocWrapper::Mapper* mMapper;
        static GrallocWrapper::Allocator* mAllocator;
        static void getAllocator(GrallocWrapper::Mapper** mapper, GrallocWrapper::Allocator** allocator);
#else
        const private_module_t   *mGrallocModule;
        alloc_device_t *mAllocDevice;
#endif

        /**
         * Geometry change will be saved by bit map.
         * ex) Display create/destory.
         */
        uint32_t mGeometryChanged;

        /**
         * Kernel event handling thread (e.g.) Vsync, hotplug, TUI enable events.
         */
        pthread_t mEventHandlerThread;

        /**
         * If Panel has not self-refresh feature, dynamic recomposition will be enabled.
         */
        pthread_t mDynamicRecompositionThread;

        /**
         * Callback informations those are used by SurfaceFlinger.
         * - VsyncCallback: Vsync detect callback.
         * - RefreshCallback: Callback by refresh request from HWC.
         * - HotplugCallback: Hot plug event by new display hardware.
         */

        /** TODO : Array size shuld be checked
         * TODO : Is HWC2_CALLBACK_VSYNC max?
         */
        exynos_callback_info_t mCallbackInfos[HWC2_CALLBACK_VSYNC + 1];

        /**
         * Thread variables
         */
        int mVsyncFd;
        int mExtVsyncFd;
        int mExtVsyncEnabled;
        uint64_t mTimestamp;
        bool hpd_status;
        bool mResolutionChanged;
        bool mResolutionHandled;
        bool dynamic_recomp_stat_thread_flag;
        bool mPrimaryBlank;

        // Variable for fence tracer
        hwc_fence_info mFenceInfo[1024];

        // Con/Destructors
        ExynosDevice();
        ~ExynosDevice();

        bool isFirstValidate();
        bool isLastValidate(ExynosDisplay *display);

        /**
         * @param width
         * @param height
         */
        ExynosDisplay* createVirtualDisplay(uint32_t width, uint32_t height);

        /**
         * @param *display
         */
        int32_t destroyVirtualDisplay(ExynosDisplay *display);


        /**
         * @param outSize
         * @param * outBuffer
         */

        bool dynamicRecompositionThreadLoop();

        /**
         * @param display
         */
        ExynosDisplay* getDisplay(uint32_t display);

        /**
         * Device Functions for HWC 2.0
         */

        /**
         * Descriptor: HWC2_FUNCTION_CREATE_VIRTUAL_DISPLAY
         * HWC2_PFN_CREATE_VIRTUAL_DISPLAY
         */
        int32_t createVirtualDisplay(
                uint32_t width, uint32_t height, hwc2_display_t* outDisplay);

        /**
         * Descriptor: HWC2_FUNCTION_DESTROY_VIRTUAL_DISPLAY
         * HWC2_PFN_DESTROY_VIRTUAL_DISPLAY
         */
        int32_t destoryVirtualDisplay(
                hwc2_display_t display);

        /**
         * Descriptor: HWC2_FUNCTION_DUMP
         * HWC2_PFN_DUMP
         */
        void dump(uint32_t *outSize, char *outBuffer);

        /**
         * Descriptor: HWC2_FUNCTION_GET_MAX_VIRTUAL_DISPLAY_COUNT
         * HWC2_PFN_GET_MAX_VIRTUAL_DISPLAY_COUNT
         */
        /* TODO overide check!! */
        uint32_t getMaxVirtualDisplayCount();

        /**
         * Descriptor: HWC2_FUNCTION_REGISTER_CALLBACK
         * HWC2_PFN_REGISTER_CALLBACK
         */
        int32_t registerCallback (
                int32_t descriptor, hwc2_callback_data_t callbackData, hwc2_function_pointer_t point);

        void handleVirtualHpd();

        void invalidate();

        void setHWCDebug(unsigned int debug);
        uint32_t getHWCDebug();
        void setHWCFenceDebug(uint32_t ipNum, uint32_t typeNum, uint32_t mode);
        void getHWCFenceDebug();
        void setForceGPU(unsigned int on);
        void setWinUpdate(unsigned int on);
        void setForcePanic(unsigned int on);
        void setSkipStaticLayer(unsigned int on);
        void setSkipM2mProcessing(unsigned int on);
        uint32_t checkConnection(uint32_t display);
        void setDynamicRecomposition(unsigned int on);
        void setFenceTracer(unsigned int on);
        void setDoFenceFileDump(unsigned int on);
        void setSysFenceLogging(unsigned int on);
        void setMaxG2dProcessing(unsigned int on);
        bool validateFences(ExynosDisplay *display);
};

#endif //_EXYNOSDEVICE_H
