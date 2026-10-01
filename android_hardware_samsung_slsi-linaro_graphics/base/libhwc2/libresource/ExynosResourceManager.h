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

/**
 * Project HWC 2.0 Design
 */

#ifndef _EXYNOSRESOURCEMANAGER_H
#define _EXYNOSRESOURCEMANAGER_H

#include "ExynosDevice.h"
#include "ExynosDisplay.h"
#include "ExynosHWCHelper.h"
#include "ExynosMPP.h"

using namespace android;

class ExynosDevice;
class ExynosDisplay;
class ExynosMPP;

#define ASSIGN_RESOURCE_TRY_COUNT   200
#define M2M_MPP_OUT_IMAGS_COUNT     3

/* Based on multi-resolution feature */
enum dst_realloc_state {
    DST_REALLOC_DONE = 0,
    DST_REALLOC_START,
    DST_REALLOC_GOING,
};

class ExynosMPPVector : public android::SortedVector< ExynosMPP* > {
    public:
        ExynosMPPVector();
        ExynosMPPVector(const ExynosMPPVector& rhs);
        virtual int do_compare(const void* lhs, const void* rhs) const;
};

class ExynosResourceManager {
    private:
    class DstBufMgrThread: public Thread {
        private:
            ExynosResourceManager *mExynosResourceManager;
            Condition mCondition;
        public:
            bool mRunning;
            Mutex mMutex;
            Mutex mStateMutex;
            Mutex mResInfoMutex;
            uint32_t mBufXres;
            uint32_t mBufYres;
            void reallocDstBufs(uint32_t Xres, uint32_t Yres);
            bool needDstRealloc(uint32_t Xres, uint32_t Yres, ExynosMPP *m2mMPP);
            DstBufMgrThread(ExynosResourceManager *exynosResourceManager);
            ~DstBufMgrThread();
            virtual bool threadLoop();
    };

    public:
        uint32_t mForceReallocState;
        ExynosDevice *mDevice;

        ExynosResourceManager(ExynosDevice *device);
        virtual ~ExynosResourceManager();
        int32_t doPreProcessing();
        void doReallocDstBufs(uint32_t Xres, uint32_t Yres);
        int32_t doAllocDstBufs(uint32_t mXres, uint32_t mYres);
        int32_t assignResource(ExynosDisplay *display);
        int32_t assignResourceInternal(ExynosDisplay *display);
        static ExynosMPP* getExynosMPP(uint32_t type);
        static void enableMPP(uint32_t physicalType, uint32_t physicalIndex, uint32_t logicalIndex, uint32_t enable);
        int32_t updateSupportedMPPFlag(ExynosDisplay * display);
        int32_t resetResources();
        int32_t preAssignResources();
        int32_t preProcessLayer(ExynosDisplay *display);
        int32_t resetAssignedResources(ExynosDisplay *display);
        int32_t assignCompositionTarget(ExynosDisplay *display, uint32_t targetType);
        int32_t assignLayer(ExynosDisplay *display, uint32_t priority);
        int32_t validateLayer(uint32_t index, ExynosDisplay *display, ExynosLayer *layer);
        int32_t checkScenario(ExynosDisplay *display);
        int32_t assignWindow(ExynosDisplay *display);
        int32_t stopUnAssignedResource();
        static float getResourceUsedCapa(ExynosMPP &mpp);
        void printDebugInfo(ExynosDisplay *display);
        int32_t updateExynosComposition(ExynosDisplay *display);
        int32_t getCandidateM2mMPPOutImages(ExynosLayer *layer,
                uint32_t *imageNum, exynos_image *image_lists);
        int32_t setResourcePriority(ExynosDisplay *display);
        int32_t deliverPerformanceInfo();
        int32_t prepareResources();
        int32_t finishAssignResourceWork();
        int32_t setDstAllocSize(uint32_t width);

        DstBufMgrThread mDstBufMgrThread;

    protected:
        static ExynosMPPVector mOtfMPPs;
        static ExynosMPPVector mM2mMPPs;
        static ExynosMPP *mSecureMPP;
};

#endif //_EXYNOSRESOURCEMANAGER_H
