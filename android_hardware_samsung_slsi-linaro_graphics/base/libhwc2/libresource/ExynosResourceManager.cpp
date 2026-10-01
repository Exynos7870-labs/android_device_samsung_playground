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

#include <cutils/properties.h>
#include "ExynosResourceManager.h"
#include "ExynosMPPModule.h"
#include "ExynosLayer.h"
#include "ExynosHWCDebug.h"
#include "ExynosHWCHelper.h"
#include "hardware/exynos/acryl.h"

using namespace android;

ExynosMPPVector ExynosResourceManager::mOtfMPPs;
ExynosMPPVector ExynosResourceManager::mM2mMPPs;
ExynosMPP* ExynosResourceManager::mSecureMPP;
extern struct exynos_hwc_cotrol exynosHWCControl;

ExynosMPPVector::ExynosMPPVector() {
}

ExynosMPPVector::ExynosMPPVector(const ExynosMPPVector& rhs)
    : android::SortedVector<ExynosMPP* >(rhs) {
}

int ExynosMPPVector::do_compare(const void* lhs, const void* rhs) const
{
    if (lhs == NULL || rhs == NULL)
        return 0;

    const ExynosMPP* l = *((ExynosMPP**)(lhs));
    const ExynosMPP* r = *((ExynosMPP**)(rhs));

    if (l == NULL || r == NULL)
        return 0;

    if (l->mPhysicalType != r->mPhysicalType) {
        return l->mPhysicalType - r->mPhysicalType;
    }

    if (l->mLogicalType != r->mLogicalType) {
        return l->mLogicalType - r->mLogicalType;
    }

    if (l->mPhysicalIndex != r->mPhysicalIndex) {
        return l->mPhysicalIndex - r->mPhysicalIndex;
    }

    return l->mLogicalIndex - r->mLogicalIndex;
}
/**
 * ExynosResourceManager implementation
 *
 */

ExynosResourceManager::DstBufMgrThread::DstBufMgrThread(ExynosResourceManager *exynosResourceManager)
: mExynosResourceManager(exynosResourceManager),
    mRunning(false)
{
}

ExynosResourceManager::DstBufMgrThread::~DstBufMgrThread()
{
}

ExynosResourceManager::ExynosResourceManager(ExynosDevice *device)
: mForceReallocState(DST_REALLOC_DONE),
    mDevice(device),
    mDstBufMgrThread(this)
{
    size_t num_mpp_units = sizeof(AVAILABLE_OTF_MPP_UNITS)/sizeof(exynos_mpp_t);
    for (size_t i = 0; i < num_mpp_units; i++) {
        exynos_mpp_t exynos_mpp = AVAILABLE_OTF_MPP_UNITS[i];
        ALOGI("otfMPP type(%d, %d), physical_index(%d), logical_index(%d)",
                exynos_mpp.physicalType, exynos_mpp.logicalType,
                exynos_mpp.physical_index, exynos_mpp.logical_index);
        ExynosMPP* exynosMPP = new ExynosMPPModule(exynos_mpp.physicalType,
                exynos_mpp.logicalType, exynos_mpp.name, exynos_mpp.physical_index,
                exynos_mpp.logical_index, exynos_mpp.pre_assign_info);
        exynosMPP->mMPPType = MPP_TYPE_OTF;
        mOtfMPPs.add(exynosMPP);
    }

    num_mpp_units = sizeof(AVAILABLE_M2M_MPP_UNITS)/sizeof(exynos_mpp_t);
    for (size_t i = 0; i < num_mpp_units; i++) {
        exynos_mpp_t exynos_mpp = AVAILABLE_M2M_MPP_UNITS[i];
        ALOGI("m2mMPP type(%d, %d), physical_index(%d), logical_index(%d)",
                exynos_mpp.physicalType, exynos_mpp.logicalType,
                exynos_mpp.physical_index, exynos_mpp.logical_index);
        ExynosMPP* exynosMPP = new ExynosMPPModule(exynos_mpp.physicalType,
                exynos_mpp.logicalType, exynos_mpp.name, exynos_mpp.physical_index,
                exynos_mpp.logical_index, exynos_mpp.pre_assign_info);
        exynosMPP->mMPPType = MPP_TYPE_M2M;
#ifdef GRALLOC_VERSION1
        exynosMPP->setAllocDevice(mDevice->mAllocator, mDevice->mMapper);
#else
        exynosMPP->setAllocDevice(mDevice->mAllocDevice);
#endif
        mM2mMPPs.add(exynosMPP);
    }

    mSecureMPP = new ExynosMPPModule(SECURE_MPP.physicalType, SECURE_MPP.logicalType,
                SECURE_MPP.name, SECURE_MPP.physical_index, SECURE_MPP.logical_index,
                SECURE_MPP.pre_assign_info);
    mSecureMPP->mMPPType = MPP_TYPE_OTF;

    ALOGI("mOtfMPPs(%zu), mM2mMPPs(%zu)", mOtfMPPs.size(), mM2mMPPs.size());
    if (hwcCheckDebugMessages(eDebugResourceManager)) {
        for (uint32_t i = 0; i < mOtfMPPs.size(); i++)
        {
            HDEBUGLOGD(eDebugResourceManager, "otfMPP[%d]", i);
            String8 dumpMPP;
            mOtfMPPs[i]->dump(dumpMPP);
            HDEBUGLOGD(eDebugResourceManager, "%s", dumpMPP.string());
        }
        for (uint32_t i = 0; i < mM2mMPPs.size(); i++)
        {
            HDEBUGLOGD(eDebugResourceManager, "m2mMPP[%d]", i);
            String8 dumpMPP;
            mM2mMPPs[i]->dump(dumpMPP);
            HDEBUGLOGD(eDebugResourceManager, "%s", dumpMPP.string());
        }
    }

    mDstBufMgrThread.mRunning = true;
    mDstBufMgrThread.run("DstBufMgrThread");

    ALOGD("mDevice(%p) is initialized", mDevice);
}

ExynosResourceManager::~ExynosResourceManager()
{
    for (int32_t i = mOtfMPPs.size(); i-- > 0;) {
        ExynosMPP *exynosMPP = mOtfMPPs[i];
        delete exynosMPP;
    }
    mOtfMPPs.clear();
    for (int32_t i = mM2mMPPs.size(); i-- > 0;) {
        ExynosMPP *exynosMPP = mM2mMPPs[i];
        delete exynosMPP;
    }
    mM2mMPPs.clear();

    if (mSecureMPP != NULL) {
        delete mSecureMPP;
        mSecureMPP = NULL;
    }

    mDstBufMgrThread.mRunning = false;
    mDstBufMgrThread.requestExitAndWait();
}

int32_t ExynosResourceManager::doPreProcessing()
{
    int32_t ret = NO_ERROR;
    /* Assign m2mMPP's out buffers */
    ExynosDisplay *display = mDevice->getDisplay(HWC_DISPLAY_PRIMARY);
    if (display == NULL)
        return -EINVAL;
    ret = doAllocDstBufs(display->mXres, display->mYres);
    return ret;
}

void ExynosResourceManager::doReallocDstBufs(uint32_t Xres, uint32_t Yres)
{
    HDEBUGLOGD(eDebugBuf, "M2M dst alloc call ");
    mDstBufMgrThread.reallocDstBufs(Xres, Yres);
}

bool ExynosResourceManager::DstBufMgrThread::needDstRealloc(uint32_t Xres, uint32_t Yres, ExynosMPP *m2mMPP)
{
    bool ret = false;
    if (((Xres == 720 && Yres == 1480) && (m2mMPP->getDstAllocSize() != DST_SIZE_HD_PLUS)) ||
            ((Xres == 720 && Yres == 1280) && (m2mMPP->getDstAllocSize() != DST_SIZE_HD)) ||
            ((Xres == 1080 && Yres == 2220) && (m2mMPP->getDstAllocSize() != DST_SIZE_FHD_PLUS)) ||
            ((Xres == 1080 && Yres == 1920) && (m2mMPP->getDstAllocSize() != DST_SIZE_FHD)) ||
            ((Xres == 1440 && Yres == 2960) && (m2mMPP->getDstAllocSize() != DST_SIZE_WQHD_PLUS)) ||
            ((Xres == 1440 && Yres == 2560) && (m2mMPP->getDstAllocSize() != DST_SIZE_WQHD))) {
        ret = true;
    }
    return ret;
}

void ExynosResourceManager::DstBufMgrThread::reallocDstBufs(uint32_t Xres, uint32_t Yres)
{
    bool needRealloc = false;
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->needPreAllocation())
        {
            if (needDstRealloc(Xres, Yres, mM2mMPPs[i])) {
                HDEBUGLOGD(eDebugBuf, "M2M dst alloc : %d Realloc Start ++++++", mM2mMPPs[i]->mLogicalType);
                needRealloc = true;
            }
            else HDEBUGLOGD(eDebugBuf, "M2M dst alloc : %d MPP's DST Realloc is not needed : Size is same", mM2mMPPs[i]->mLogicalType);
        }
    }

    if (needRealloc) {
        Mutex::Autolock lock(mStateMutex);
        if (mExynosResourceManager->mForceReallocState == DST_REALLOC_DONE) {
            mExynosResourceManager->mForceReallocState = DST_REALLOC_START;
            android::Mutex::Autolock lock(mMutex);
            mCondition.signal();
        } else {
            HDEBUGLOGD(eDebugBuf, "M2M dst alloc thread : queue aready.");
        }
    }
}

bool ExynosResourceManager::DstBufMgrThread::threadLoop()
{
    ExynosDevice *device = mExynosResourceManager->mDevice;
    if (device == NULL)
        return false;

    while(mRunning) {
        Mutex::Autolock lock(mMutex);
        mCondition.wait(mMutex);
        ExynosDisplay *display = device->getDisplay(HWC_DISPLAY_PRIMARY);
        if (display == NULL)
            return false;

        do {
            {
                HDEBUGLOGD(eDebugBuf, "M2M dst alloc %d, %d, %d, %d : Realloc On going ----------",
                        mBufXres, display->mXres, mBufYres, display->mYres);
                Mutex::Autolock lock(mResInfoMutex);
                mBufXres = display->mXres;mBufYres = display->mYres;
            }
            mExynosResourceManager->doAllocDstBufs(mBufXres, mBufYres);
        } while (mBufXres != display->mXres || mBufYres != display->mYres);

        {
            Mutex::Autolock lock(mStateMutex);
            mExynosResourceManager->mForceReallocState = DST_REALLOC_DONE;
            HDEBUGLOGD(eDebugBuf, "M2M dst alloc %d, %d, %d, %d : Realloc On Done ----------",
                    mBufXres, display->mXres, mBufYres, display->mYres);
        }
    }
    return true;
}

int32_t ExynosResourceManager::doAllocDstBufs(uint32_t Xres, uint32_t Yres)
{
    int32_t ret = NO_ERROR;
    /* Assign m2mMPP's out buffers */

    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->needPreAllocation())
        {
            mM2mMPPs[i]->mFreeOutBufFlag = false;
            for (uint32_t index = 0; index < NUM_MPP_DST_BUFS(mM2mMPPs[i]->mLogicalType); index++) {
                HDEBUGLOGD(eDebugBuf, "%s allocate dst buffer[%d]%p, x : %d, y : %d",
                        __func__, index, mM2mMPPs[i]->mDstImgs[index].bufferHandle, Xres, Yres);
                uint32_t bufAlign = mM2mMPPs[i]->getOutBufAlign();
                ret = mM2mMPPs[i]->allocOutBuf(ALIGN_UP(Xres, bufAlign),
                        ALIGN_UP(Yres, bufAlign),
                        DEFAULT_MPP_DST_FORMAT, 0x0, index);
                if (ret < 0) {
                    HWC_LOGE(NULL, "%s:: fail to allocate dst buffer[%d]",
                            __func__, index);
                    return ret;
                }
                mM2mMPPs[i]->mPrevAssignedDisplayType = HWC_DISPLAY_PRIMARY;
            }
            mM2mMPPs[i]->setDstAllocSize(Xres, Yres);
        }
    }
    return ret;
}

int32_t ExynosResourceManager::checkScenario(ExynosDisplay __unused *display)
{
#if 0
    /* Check whether camera preview is running */
    ExynosDisplay *exynosDisplay = NULL;
    for (uint32_t display_type = 0; display_type < HWC_NUM_DISPLAY_TYPES; display_type++) {
        exynosDisplay = mDevice->getDisplay(display_type);
        if ((exynosDisplay != NULL) && (exynosDisplay->mPlugState == true)) {
            for (uint32_t i = 0; i < exynosDisplay->mLayers.size(); i++) {
                ExynosLayer *layer = exynosDisplay->mLayers[i];
                if ((layer->mLayerBuffer != NULL) &&
                    (layer->mLayerBuffer->flags & GRALLOC_USAGE_HW_CAMERA_MASK)) {
                    mDevice->mGeometryChanged |= GEOMETRY_MSC_RESERVED;
                    break;
                }
            }
        }
    }

    char value[PROPERTY_VALUE_MAX];
    bool preview;
    property_get("persist.vendor.sys.camera.preview", value, "0");
    preview = !!atoi(value);
    if (preview)
        mDevice->mGeometryChanged |= GEOMETRY_MSC_RESERVED;
#endif

    return NO_ERROR;
}

/**
 * @param * display
 * @return int
 */
int32_t ExynosResourceManager::assignResource(ExynosDisplay *display)
{
    int ret = 0;
    if ((mDevice == NULL) || (display == NULL))
        return -EINVAL;

    /* TEST */
    mDevice->mGeometryChanged = 0x10;

    if ((ret = checkScenario(display)) != NO_ERROR)
    {
        HWC_LOGE(display, "%s:: checkScenario() error (%d)",
                __func__, ret);
        return ret;
    }

    HDEBUGLOGD(eDebugResourceManager, "mGeometryChanged(0x%8x), display(%d)",
            mDevice->mGeometryChanged, display->mType);

    if (mDevice->mGeometryChanged == 0)
        return NO_ERROR;

    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        display->mLayers[i]->resetValidateData();
    }

    display->initializeValidateInfos();

    if ((ret = preProcessLayer(display)) != NO_ERROR) {
        HWC_LOGE(display, "%s:: preProcessLayer() error (%d)",
                __func__, ret);
        return ret;
    }

    if ((ret = updateSupportedMPPFlag(display)) != NO_ERROR) {
        HWC_LOGE(display, "%s:: updateSupportedMPPFlag() error (%d)",
                __func__, ret);
        return ret;
    }

#if 0
    if (mDevice->isFirstValidate()) {
        HDEBUGLOGD(eDebugResourceManager, "This is first validate");
        if ((ret = resetResources()) != NO_ERROR) {
            ALOGE("%s:: resetResources() error (%d)",
                    __func__, ret);
            return ret;
        }
        if ((ret = preAssignResources()) != NO_ERROR) {
            ALOGE("%s:: preAssignResources() error (%d)",
                    __func__, ret);
            return ret;
        }
    }
#endif

    if ((ret = assignResourceInternal(display)) != NO_ERROR) {
        HWC_LOGE(display, "%s:: assignResourceInternal() error (%d)",
                __func__, ret);
        return ret;
    }

    if ((ret = assignWindow(display)) != NO_ERROR) {
        HWC_LOGE(display, "%s:: assignWindow() error (%d)",
                __func__, ret);
        return ret;
    }

    if (hwcCheckDebugMessages(eDebugResourceManager)) {
        HDEBUGLOGD(eDebugResourceManager, "AssignResource result");
        String8 result;
        display->mClientCompositionInfo.dump(result);
        HDEBUGLOGD(eDebugResourceManager, "%s", result.string());
        result.clear();
        display->mExynosCompositionInfo.dump(result);
        HDEBUGLOGD(eDebugResourceManager, "%s", result.string());
        for (uint32_t i = 0; i < display->mLayers.size(); i++) {
            result.clear();
            HDEBUGLOGD(eDebugResourceManager, "%d layer(%p) dump", i, display->mLayers[i]);
            display->mLayers[i]->printLayer();
            HDEBUGLOGD(eDebugResourceManager, "%s", result.string());
        }
    }

#if 0
    if (mDevice->isLastValidate(display)) {
        if ((ret = stopUnAssignedResource()) != NO_ERROR) {
            ALOGE("%s:: stopUnAssignedResource() error (%d)",
                    __func__, ret);
            return ret;
        }

        if ((ret = deliverPerformanceInfo()) != NO_ERROR) {
            ALOGE("%s:: deliverPerformanceInfo() error (%d)",
                    __func__, ret);
            return ret;
        }
    }
#endif

    if (!display->mUseDecon) {
        for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
            if (mM2mMPPs[i]->mLogicalType == MPP_LOGICAL_G2D_COMBO) {
                display->mExynosCompositionInfo.mM2mMPP = mM2mMPPs[i];
                break;
            }
        }

        if (display->mClientCompositionInfo.mHasCompositionLayer) {
            if ((ret = display->mExynosCompositionInfo.mM2mMPP->assignMPP(display, &display->mClientCompositionInfo)) != NO_ERROR)
            {
                ALOGE("%s:: %s MPP assignMPP() error (%d)",
                        __func__, display->mExynosCompositionInfo.mM2mMPP->mName.string(), ret);
                return ret;
            }
            display->mExynosCompositionInfo.mHasCompositionLayer = true;
        }
    }

    return NO_ERROR;
}

int32_t ExynosResourceManager::setResourcePriority(ExynosDisplay *display)
{
    int ret = NO_ERROR;
    int check_ret = NO_ERROR;
    ExynosMPP *m2mMPP = NULL;

    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->mLayers[i];
        if ((layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE) &&
            (layer->mM2mMPP != NULL) &&
            (layer->mM2mMPP->mPhysicalType == MPP_G2D) &&
            ((check_ret = layer->mM2mMPP->prioritize(2)) != NO_ERROR)) {
            if (check_ret < 0) {
                HWC_LOGE(display, "Fail to set exynoscomposition priority(%d)", ret);
            } else {
                m2mMPP = layer->mM2mMPP;
                layer->resetAssignedResource();
                layer->mOverlayInfo |= eResourcePendingWork;
                layer->mValidateCompositionType = HWC2_COMPOSITION_DEVICE;
                ret = EXYNOS_ERROR_CHANGED;
                HDEBUGLOGD(eDebugResourceManager, "\t%s is reserved without display because of panding work",
                        m2mMPP->mName.string());
                m2mMPP->reserveMPP();
                layer->mCheckMPPFlag[m2mMPP->mLogicalType] = eMPPHWBusy;
            }
        }
    }

    m2mMPP = display->mExynosCompositionInfo.mM2mMPP;
    ExynosCompositionInfo &compositionInfo = display->mExynosCompositionInfo;
    if (compositionInfo.mHasCompositionLayer == true)
    {
        if ((m2mMPP == NULL) || (m2mMPP->mCompositor == NULL)) {
            HWC_LOGE(display, "There is exynos composition layers but resource is null (%p)",
                    m2mMPP);
        } else if ((check_ret = m2mMPP->prioritize(2)) != NO_ERROR) {
            HDEBUGLOGD(eDebugResourceManager, "%s setting priority error(%d)", m2mMPP->mName.string(), check_ret);
            if (check_ret < 0) {
                HWC_LOGE(display, "Fail to set exynoscomposition priority(%d)", ret);
            } else {
                uint32_t firstIndex = (uint32_t)display->mExynosCompositionInfo.mFirstIndex;
                uint32_t lastIndex = (uint32_t)display->mExynosCompositionInfo.mLastIndex;
                for (uint32_t i = firstIndex; i <= lastIndex; i++) {
                    ExynosLayer *layer = display->mLayers[i];
                    layer->resetAssignedResource();
                    layer->mOverlayInfo |= eResourcePendingWork;
                    layer->mValidateCompositionType = HWC2_COMPOSITION_DEVICE;
                    layer->mCheckMPPFlag[m2mMPP->mLogicalType] = eMPPHWBusy;
                }
                compositionInfo.initializeInfos(display);
                ret = EXYNOS_ERROR_CHANGED;
                m2mMPP->resetUsedCapacity();
                HDEBUGLOGD(eDebugResourceManager, "\t%s is reserved without display because of pending work",
                        m2mMPP->mName.string());
                m2mMPP->reserveMPP();
            }
        } else {
            HDEBUGLOGD(eDebugResourceManager, "%s setting priority is ok", m2mMPP->mName.string());
        }
    }

    return ret;
}

int32_t ExynosResourceManager::assignResourceInternal(ExynosDisplay *display)
{
    int ret = NO_ERROR;
    int retry_count = 0;

    Mutex::Autolock lock(mDstBufMgrThread.mStateMutex);
    do {
        HDEBUGLOGD(eDebugResourceManager, "%s:: retry_count(%d)", __func__, retry_count);
        if ((ret = resetAssignedResources(display)) != NO_ERROR)
            return ret;
        if ((ret = assignCompositionTarget(display, COMPOSITION_CLIENT)) != NO_ERROR) {
            HWC_LOGE(display, "%s:: Fail to assign resource for compositionTarget",
                    __func__);
            return ret;
        }

        if ((ret = assignLayer(display, ePriorityMax)) != NO_ERROR) {
            if (ret == EXYNOS_ERROR_CHANGED) {
                retry_count++;
                continue;
            } else {
                HWC_LOGE(display, "%s:: Fail to assign resource for ePriorityMax layer",
                        __func__);
                goto err;
            }
        }

        if ((ret = assignCompositionTarget(display, COMPOSITION_EXYNOS)) != NO_ERROR) {
            if (ret == eInsufficientMPP) {
                /*
                 * Change compositionTypes to HWC2_COMPOSITION_CLIENT
                 */
                uint32_t firstIndex = (uint32_t)display->mExynosCompositionInfo.mFirstIndex;
                uint32_t lastIndex = (uint32_t)display->mExynosCompositionInfo.mLastIndex;
                for (uint32_t i = firstIndex; i <= lastIndex; i++) {
                    ExynosLayer *layer = display->mLayers[i];
                    layer->resetAssignedResource();
                    layer->mOverlayInfo |= eInsufficientMPP;
                    layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
                    if (((ret = display->addClientCompositionLayer(i)) != NO_ERROR) &&
                        (ret != EXYNOS_ERROR_CHANGED)) {
                        HWC_LOGE(display, "Change compositionTypes to HWC2_COMPOSITION_CLIENT, but addClientCompositionLayer failed (%d)", ret);
                        goto err;
                    }
                }
                display->mExynosCompositionInfo.initializeInfos(display);
                ret = EXYNOS_ERROR_CHANGED;
            } else {
                goto err;
            }
        }

        if (ret == NO_ERROR) {
            for (int32_t i = ePriorityMax - 1; i > ePriorityNone; i--) {
                if ((ret = assignLayer(display, i)) == EXYNOS_ERROR_CHANGED)
                    break;
                if (ret != NO_ERROR)
                    goto err;
            }
        }

        /* Assignment is done */
        if (ret == NO_ERROR) {
            ret = setResourcePriority(display);
        }
        retry_count++;
    } while((ret == EXYNOS_ERROR_CHANGED) && (retry_count < ASSIGN_RESOURCE_TRY_COUNT));

    if (retry_count == ASSIGN_RESOURCE_TRY_COUNT) {
        HWC_LOGE(display, "%s:: assign resources fail", __func__);
        ret = eUnknown;
        goto err;
    } else {
        ret = updateExynosComposition(display);
    }

    if (hwcCheckDebugMessages(eDebugCapacity)) {
        for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
            if (mM2mMPPs[i]->mPhysicalType == MPP_G2D)
            {
                String8 dumpMPP;
                mM2mMPPs[i]->dump(dumpMPP);
                HDEBUGLOGD(eDebugCapacity, "%s", dumpMPP.string());
            }
        }
    }
    return ret;
err:
    resetAssignedResources(display);
    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->mLayers[i];
        layer->mOverlayInfo |= eResourceAssignFail;
        layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
        display->addClientCompositionLayer(i);
    }
    assignCompositionTarget(display, COMPOSITION_CLIENT);
    display->mExynosCompositionInfo.initializeInfos(display);
    assignWindow(display);
    return ret;
}
int32_t ExynosResourceManager::updateExynosComposition(ExynosDisplay *display)
{
    int ret = NO_ERROR;
    /* Use Exynos composition as many as possible */
    if ((display->mExynosCompositionInfo.mHasCompositionLayer == true) &&
        (display->mExynosCompositionInfo.mM2mMPP != NULL)) {
        if (exynosHWCControl.useMaxG2DSrc == 1) {
            ExynosMPP *m2mMPP = display->mExynosCompositionInfo.mM2mMPP;
            uint32_t lastIndex = display->mExynosCompositionInfo.mLastIndex;
            uint32_t firstIndex = display->mExynosCompositionInfo.mFirstIndex;
            uint32_t remainNum = m2mMPP->mMaxSrcLayerNum - (lastIndex - firstIndex + 1);

            HDEBUGLOGD(eDebugResourceManager, "Update ExynosComposition firstIndex: %d, lastIndex: %d, remainNum: %d++++",
                    firstIndex, lastIndex, remainNum);

            ExynosLayer *layer = NULL;
            exynos_image src_img;
            exynos_image dst_img;
            if (remainNum > 0) {
                for (uint32_t i = (lastIndex + 1); i < display->mLayers.size(); i++)
                {
                    layer = display->mLayers[i];
                    layer->setSrcExynosImage(&src_img);
                    layer->setDstExynosImage(&dst_img);
                    layer->setExynosImage(src_img, dst_img);
                    bool isAssignable = m2mMPP->isAssignable(display, src_img, dst_img);
                    bool canChange = (layer->mValidateCompositionType != HWC2_COMPOSITION_CLIENT) &&
                        (layer->mSupportedMPPFlag & m2mMPP->mLogicalType) && isAssignable;

                    HDEBUGLOGD(eDebugResourceManager, "\tlayer[%d] type: %d, 0x%8x, isAssignable: %d, canChange: %d, remainNum(%d)",
                            i, layer->mValidateCompositionType,
                            layer->mSupportedMPPFlag, isAssignable, canChange, remainNum);
                    if (canChange) {
                        layer->resetAssignedResource();
                        layer->mOverlayInfo |= eUpdateExynosComposition;
                        if ((ret = m2mMPP->assignMPP(display, layer)) != NO_ERROR)
                        {
                            HWC_LOGE(display, "%s:: %s MPP assignMPP() error (%d)",
                                    __func__, m2mMPP->mName.string(), ret);
                            return ret;
                        }
                        layer->setExynosMidImage(dst_img);
                        display->addExynosCompositionLayer(i);
                        layer->mValidateCompositionType = HWC2_COMPOSITION_EXYNOS;
                        remainNum--;
                    }
                    if ((canChange == false) || (remainNum == 0))
                        break;
                }
            }
            if (remainNum > 0) {
                for (int32_t i = (firstIndex - 1); i >= 0; i--)
                {
                    layer = display->mLayers[i];
                    layer->setSrcExynosImage(&src_img);
                    layer->setDstExynosImage(&dst_img);
                    layer->setExynosImage(src_img, dst_img);
                    bool isAssignable = m2mMPP->isAssignable(display, src_img, dst_img);
                    bool canChange = (layer->mValidateCompositionType != HWC2_COMPOSITION_CLIENT) &&
                        (layer->mSupportedMPPFlag & m2mMPP->mLogicalType) && isAssignable;

                    HDEBUGLOGD(eDebugResourceManager, "\tlayer[%d] type: %d, 0x%8x, isAssignable: %d, canChange: %d, remainNum(%d)",
                            i, layer->mValidateCompositionType,
                            layer->mSupportedMPPFlag, isAssignable, canChange, remainNum);
                    if (canChange) {
                        layer->resetAssignedResource();
                        layer->mOverlayInfo |= eUpdateExynosComposition;
                        if ((ret = m2mMPP->assignMPP(display, layer)) != NO_ERROR)
                        {
                            HWC_LOGE(display, "%s:: %s MPP assignMPP() error (%d)",
                                    __func__, m2mMPP->mName.string(), ret);
                            return ret;
                        }
                        layer->setExynosMidImage(dst_img);
                        display->addExynosCompositionLayer(i);
                        layer->mValidateCompositionType = HWC2_COMPOSITION_EXYNOS;
                        remainNum--;
                    }
                    if ((canChange == false) || (remainNum == 0))
                        break;
                }
            }
            HDEBUGLOGD(eDebugResourceManager, "Update ExynosComposition firstIndex: %d, lastIndex: %d, remainNum: %d-----",
                    display->mExynosCompositionInfo.mFirstIndex, display->mExynosCompositionInfo.mLastIndex, remainNum);
        }
    }
    return ret;
}

int32_t ExynosResourceManager::resetAssignedResources(ExynosDisplay * display)
{
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if (mOtfMPPs[i]->mAssignedDisplay != display)
            continue;

        mOtfMPPs[i]->resetAssignedState();
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mAssignedDisplay != display)
            continue;
        /*
         * Reset used capacity
         */
        mM2mMPPs[i]->resetUsedCapacity();
        if ((mM2mMPPs[i]->mLogicalType == MPP_LOGICAL_G2D_RGB) ||
            (mM2mMPPs[i]->mLogicalType == MPP_LOGICAL_G2D_COMBO))
        {
            /*
             * Don't reset assigned state
             */
            continue;
        }
        mM2mMPPs[i]->resetAssignedState();
    }

    if (mSecureMPP->mAssignedDisplay == display)
        mSecureMPP->resetAssignedState();

    display->mWindowNumUsed = 0;

    return NO_ERROR;
}

int32_t ExynosResourceManager::assignCompositionTarget(ExynosDisplay * display, uint32_t targetType)
{
    int32_t ret = NO_ERROR;
    ExynosCompositionInfo *compositionInfo;

    HDEBUGLOGD(eDebugResourceManager, "%s:: display(%d), targetType(%d) +++++",
            __func__, display->mType, targetType);

    if (targetType == COMPOSITION_CLIENT)
        compositionInfo = &(display->mClientCompositionInfo);
    else if (targetType == COMPOSITION_EXYNOS)
        compositionInfo = &(display->mExynosCompositionInfo);
    else
        return -EINVAL;

    if (compositionInfo->mHasCompositionLayer == false)
    {
        HDEBUGLOGD(eDebugResourceManager, "\tthere is no composition layers");
        return NO_ERROR;
    }

    exynos_image src_img;
    exynos_image dst_img;
    display->setCompositionTargetExynosImage(targetType, &src_img, &dst_img);

    if (targetType == COMPOSITION_EXYNOS) {
        for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
            if (((mM2mMPPs[i]->mLogicalType == MPP_LOGICAL_G2D_RGB) ||
                 (mM2mMPPs[i]->mLogicalType == MPP_LOGICAL_G2D_COMBO)) &&
                (mM2mMPPs[i]->isAssignableState(display, src_img, dst_img))) {
                /* assignMPP(display, compositionInfo) is not called hear
                 * assignMPP() was called already during assigning layer
                 * Source of M2mMPP should be Layer, not composition target buffer*/
                compositionInfo->mM2mMPP = mM2mMPPs[i];
            }
        }
        if (compositionInfo->mM2mMPP == NULL) {
            HWC_LOGE(display, "%s:: fail to assign M2mMPP (%d)",__func__, ret);
            return eInsufficientMPP;
        }
        compositionInfo->mM2mMPP->updateUsedCapacity();
    }

    if ((compositionInfo->mFirstIndex < 0) ||
        (compositionInfo->mLastIndex < 0)) {
        HWC_LOGE(display, "%s:: layer index is not valid mFirstIndex(%d), mLastIndex(%d)",
                __func__, compositionInfo->mFirstIndex, compositionInfo->mLastIndex);
        return -EINVAL;
    }

    compositionInfo->setExynosImage(src_img, dst_img);

    if (display->mUseDecon == false) {
        return NO_ERROR;
    }

    bool isTopLayer = false;
    if ((uint32_t)compositionInfo->mLastIndex == display->mLayers.size()-1)
        isTopLayer = true;

    /* Check if secureDMA can be used
     * secureDMA can be mapped with only top layer */
    int32_t isSupported = mSecureMPP->isSupported(display, src_img, dst_img);
    bool isAssignable = mSecureMPP->isAssignable(display, src_img, dst_img);

    HDEBUGLOGD(eDebugResourceManager, "\t\t check secureMPP: mUseSecureDMA(%d), isTopLayer(%d), supportedBit(0x%8x), isAssignable(%d)",
            display->mUseSecureDMA, isTopLayer, -isSupported, isAssignable);
    if ((display->mUseSecureDMA) &&
        ((uint32_t)compositionInfo->mLastIndex == display->mLayers.size()-1) &&
        (isSupported == NO_ERROR) &&
        (isAssignable)) {
        if ((ret = mSecureMPP->assignMPP(display, compositionInfo)) != NO_ERROR)
        {
            HWC_LOGE(display, "%s:: %s MPP assignMPP() error (%d)",
                    __func__, mSecureMPP->mName.string(), ret);
            return ret;
        }
        display->mWindowNumUsed++;

        HDEBUGLOGD(eDebugResourceManager, "%s:: secureMPP is assigned", __func__);
        return NO_ERROR;
    }

    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        isSupported = mOtfMPPs[i]->isSupported(display, src_img, dst_img);
        isAssignable = mOtfMPPs[i]->isAssignable(display, src_img, dst_img);
        HDEBUGLOGD(eDebugResourceManager, "\t\t check %s: supportedBit(0x%8x), isAssignable(%d)",
                mOtfMPPs[i]->mName.string(), -isSupported, isAssignable);
        if ((isSupported == NO_ERROR) && (isAssignable)) {
            if ((ret = mOtfMPPs[i]->assignMPP(display, compositionInfo)) != NO_ERROR)
            {
                HWC_LOGE(display, "%s:: %s MPP assignMPP() error (%d)",
                        __func__, mOtfMPPs[i]->mName.string(), ret);
                return ret;
            }
            compositionInfo->mOtfMPP = mOtfMPPs[i];
            display->mWindowNumUsed++;

            HDEBUGLOGD(eDebugResourceManager, "%s:: %s is assigned", __func__, mOtfMPPs[i]->mName.string());
            return NO_ERROR;
        }
    }

    HDEBUGLOGD(eDebugResourceManager, "%s:: insufficient MPP", __func__);
    return eInsufficientMPP;
}

int32_t ExynosResourceManager::validateLayer(uint32_t index, ExynosDisplay *display, ExynosLayer *layer)
{
    if ((layer == NULL) || (display == NULL))
        return eUnknown;

    if ((layer->mLayerBuffer != NULL) &&
        (getDrmMode(layer->mLayerBuffer->flags) == NO_DRM)&&
        (exynosHWCControl.forceGpu == 1))
        return eForceFbEnabled;

    if ((layer->mLayerBuffer != NULL) &&
            (getDrmMode(layer->mLayerBuffer->flags) == NO_DRM) &&
            (display->mDynamicReCompMode == DEVICE_2_CLIENT))
            return eDynamicRecomposition;

    if (layer->mLayerFlag & HWC_SKIP_LAYER)
        return eSkipLayer;
    if ((layer->mLayerBuffer != NULL) &&
            (display->mDisplayId == HWC_DISPLAY_PRIMARY) &&
            (mForceReallocState != DST_REALLOC_DONE)) {
        ALOGI("Device type assign skipping by dst reallocation...... ");
        return eReallocOnGoingForDDI;
    }

    /* If display is virtual/external and layer has HWC_SKIP_LAYER, HWC skips it */
    if ((layer != NULL) && (layer->mLayerFlag & HWC_SKIP_LAYER) && (display != NULL) &&
        (display->mDisplayId == HWC_DISPLAY_VIRTUAL || display->mDisplayId == HWC_DISPLAY_EXTERNAL))
        return eSkipLayer;

    if(layer->mIsDimLayer && layer->mLayerBuffer == NULL) {
        return eDimLayer;
    }

    /* Process to Source copy layer blending exception */
    if (display->mBlendingNoneIndex != -1 && display->mLayers.size() > 0) {
        ExynosLayer *layer0 = display->mLayers[0];
        if ((layer0 != NULL) && (layer0->mLayerBuffer != NULL)) {
            if (index == ((getDrmMode(layer0->mLayerBuffer->flags) == NO_DRM) ? 0:1))
                return eSourceOverBelow;
        }
        if (display->mBlendingNoneIndex == (int)index)
            return eSourceOverBelow;
    }

    if (layer->mLayerBuffer == NULL)
        return eInvalidHandle;
    if (isSrcCropFloat(layer->mPreprocessedInfo.sourceCrop))
        return eHasFloatSrcCrop;

    return NO_ERROR;
}

int32_t ExynosResourceManager::getCandidateM2mMPPOutImages(ExynosLayer *layer,
        uint32_t *imageNum, exynos_image *image_lists)
{
    uint32_t listSize = *imageNum;
    if (listSize != M2M_MPP_OUT_IMAGS_COUNT)
        return -EINVAL;

    uint32_t index = 0;
    exynos_image src_img;
    exynos_image dst_img;
    layer->setSrcExynosImage(&src_img);
    layer->setDstExynosImage(&dst_img);
    dst_img.transform = 0;
    /* Position is (0, 0) */
    dst_img.x = 0;
    dst_img.y = 0;

    /* Check original source format first */
    dst_img.format = src_img.format;
    dst_img.dataSpace = src_img.dataSpace;

    uint32_t dstW = dst_img.w;
    uint32_t dstH = dst_img.h;
    bool isPerpendicular = !!(src_img.transform & HAL_TRANSFORM_ROT_90);
    if (isPerpendicular) {
        dstW = dst_img.h;
        dstH = dst_img.w;
    }

    uint32_t standard_dataspace = (src_img.dataSpace & HAL_DATASPACE_STANDARD_MASK);
    /* Scale up case */
    if ((dstW > src_img.w) && (dstH > src_img.h))
    {
        /* VGF doesn't rotate image, m2mMPP rotates image */
        src_img.transform = 0;
        ExynosMPP *mppVGF = getExynosMPP(MPP_LOGICAL_DPP_VGF);
        exynos_image dst_scale_img = dst_img;

        if (isFormatYUV(src_img.format)) {
            dst_scale_img.format = DEFAULT_MPP_DST_YUV_FORMAT;
        }
        uint32_t upScaleRatio = mppVGF->getMaxUpscale(src_img, dst_scale_img);
        uint32_t downScaleRatio = mppVGF->getMaxDownscale(src_img, dst_scale_img);
        uint32_t srcCropWidthAlign = mppVGF->getSrcCropWidthAlign(src_img);
        uint32_t srcCropHeightAlign = mppVGF->getSrcCropHeightAlign(src_img);

        dst_scale_img.x = 0;
        dst_scale_img.y = 0;
        if (isPerpendicular) {
            dst_scale_img.w = pixel_align(src_img.h, srcCropWidthAlign);
            dst_scale_img.h = pixel_align(src_img.w, srcCropHeightAlign);
        } else {
            dst_scale_img.w = pixel_align(src_img.w, srcCropWidthAlign);
            dst_scale_img.h = pixel_align(src_img.h, srcCropHeightAlign);
        }

        HDEBUGLOGD(eDebugResourceManager, "index[%d], w: %d, h: %d, ratio(type: %d, %d, %d)", index, dst_scale_img.w, dst_scale_img.h,
                mppVGF->mLogicalType, upScaleRatio, downScaleRatio);
        if (dst_scale_img.w * upScaleRatio < dst_img.w) {
            dst_scale_img.w = pixel_align((uint32_t)ceilf((float)dst_img.w/(float)upScaleRatio), srcCropWidthAlign);
        }
        if (dst_scale_img.h * upScaleRatio < dst_img.h) {
            dst_scale_img.h = pixel_align((uint32_t)ceilf((float)dst_img.h/(float)upScaleRatio), srcCropHeightAlign);
        }
        HDEBUGLOGD(eDebugResourceManager, "\tsrc[%d, %d, %d,%d], dst[%d, %d, %d,%d], mid[%d, %d, %d, %d]",
                src_img.x, src_img.y, src_img.w, src_img.h,
                dst_img.x, dst_img.y, dst_img.w, dst_img.h,
                dst_scale_img.x, dst_scale_img.y, dst_scale_img.w, dst_scale_img.h);
        image_lists[index++] = dst_scale_img;
    }

    if (isFormatYUV(src_img.format)) {
        dst_img.format = DEFAULT_MPP_DST_YUV_FORMAT;
    }
    image_lists[index++] = dst_img;

    if (isFormatYUV(src_img.format) &&
        (standard_dataspace != HAL_DATASPACE_STANDARD_BT2020) &&
        (standard_dataspace != HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE) &&
        (standard_dataspace != HAL_DATASPACE_STANDARD_DCI_P3)) {
        /* Check RGB format */
        dst_img.format = DEFAULT_MPP_DST_FORMAT;
        dst_img.dataSpace = HAL_DATASPACE_SRGB;
        image_lists[index++] = dst_img;
    }

    if (*imageNum < index)
        return -EINVAL;
    else {
        *imageNum = index;
        return (uint32_t)listSize;
    }
}

int32_t ExynosResourceManager::assignLayer(ExynosDisplay * display, uint32_t priority)
{
    HDEBUGLOGD(eDebugResourceManager, "%s:: display(%d), priority(%d) +++++",
            __func__, display->mType, priority);

    int32_t ret = NO_ERROR;
    uint32_t validateFlag = 0;
    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->mLayers[i];
        bool assigned = false;

        if ((layer->mValidateCompositionType == HWC2_COMPOSITION_CLIENT) ||
            (layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS))
            continue;
        if (layer->mOverlayPriority != priority)
            continue;

        exynos_image src_img;
        exynos_image dst_img;
        layer->setSrcExynosImage(&src_img);
        layer->setDstExynosImage(&dst_img);
        layer->setExynosImage(src_img, dst_img);
        layer->setExynosMidImage(dst_img);

        validateFlag = validateLayer(i, display, layer);
        if (display->mWindowNumUsed >= display->mMaxWindowNum)
            validateFlag |= eInsufficientWindow;

        HDEBUGLOGD(eDebugResourceManager, "\t[%d] layer: validateFlag(0x%8x), supportedMPPFlag(0x%8x)",
                i, validateFlag, layer->mSupportedMPPFlag);

        if (hwcCheckDebugMessages(eDebugResourceManager)) {
            layer->printLayer();
        }

        if ((validateFlag == NO_ERROR) || (validateFlag == eInsufficientWindow)
                || (validateFlag == eDimLayer)) {
            bool isTopLayer = false;
            if (i == (display->mLayers.size()-1))
                isTopLayer = true;

            /* 1. Check if secureDMA can be used
             * secureDMA can be mapped with only top layer */
            bool isAssignable = mSecureMPP->isAssignable(display, src_img, dst_img);
            HDEBUGLOGD(eDebugResourceManager, "\t\t check secureMPP: mUseSecureDMA(%d), isTopLayer(%d), supportedBit(%d), isAssignable(%d)",
                    display->mUseSecureDMA, isTopLayer, (layer->mSupportedMPPFlag & mSecureMPP->mLogicalType), isAssignable);
            if ((validateFlag != eInsufficientWindow) &&
                (display->mUseSecureDMA) &&
                (isTopLayer) &&
                (layer->mSupportedMPPFlag & mSecureMPP->mLogicalType) &&
                (isAssignable)) {
                if ((ret = mSecureMPP->assignMPP(display, layer)) != NO_ERROR)
                {
                    HWC_LOGE(display, "%s:: %s MPP assignMPP() error (%d)",
                            __func__, mOtfMPPs[i]->mName.string(), ret);
                    return ret;
                } else {
                    layer->mValidateCompositionType = HWC2_COMPOSITION_DEVICE;
                }
                display->mWindowNumUsed++;
                HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] layer: secureMPP is assigned, mWindowNumUsed(%d)",
                        i, display->mWindowNumUsed);
                assigned = true;
                continue;
            }

            /* 2. Find available otfMPP */
            if (validateFlag != eInsufficientWindow) {
                for (uint32_t j = 0; j < mOtfMPPs.size(); j++) {
                    isAssignable = mOtfMPPs[j]->isAssignable(display, src_img, dst_img);
                    HDEBUGLOGD(eDebugResourceManager, "\t\t check %s: flag (%d) supportedBit(%d), isAssignable(%d)",
                            mOtfMPPs[j]->mName.string(),layer->mSupportedMPPFlag,
                            (layer->mSupportedMPPFlag & mOtfMPPs[j]->mLogicalType), isAssignable);
                    if ((layer->mSupportedMPPFlag & mOtfMPPs[j]->mLogicalType) && (isAssignable)) {
                        if ((ret = mOtfMPPs[j]->assignMPP(display, layer)) != NO_ERROR)
                        {
                            HWC_LOGE(display, "%s:: %s MPP assignMPP() error (%d)",
                                    __func__, mOtfMPPs[i]->mName.string(), ret);
                            return ret;
                        } else {
                            layer->mValidateCompositionType = HWC2_COMPOSITION_DEVICE;
                        }
                        assigned = true;
                        break;
                    }
                }
                /* otfMPP was assigned, go to next layer */
                if (layer->mOtfMPP != NULL) {
                    display->mWindowNumUsed++;
                    HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] layer: %s is assigned, mWindowNumUsed(%d)",
                            i, layer->mOtfMPP->mName.string(), display->mWindowNumUsed);
                    continue;
                }
            }

            /* 3. Find available m2mMPP */
            for (uint32_t j = 0; j < mM2mMPPs.size(); j++) {
                /* Only G2D can be assigned if layer is supported by G2D
                 * when window is not sufficient
                 */
                if ((validateFlag == eInsufficientWindow) &&
                    (mM2mMPPs[j]->mLogicalType != MPP_LOGICAL_G2D_RGB) &&
                    (mM2mMPPs[j]->mLogicalType != MPP_LOGICAL_G2D_COMBO)) {
                    HDEBUGLOGD(eDebugResourceManager, "\t\tInsufficient window but exynosComposition is not assigned");
                    continue;
                }

                isAssignable = mM2mMPPs[j]->isAssignable(display, src_img, dst_img);
                HDEBUGLOGD(eDebugResourceManager, "\t\t check %s: supportedBit(%d), isAssignable(%d)",
                        mM2mMPPs[j]->mName.string(),
                        (layer->mSupportedMPPFlag & mM2mMPPs[j]->mLogicalType), isAssignable);

                if ((layer->mSupportedMPPFlag & mM2mMPPs[j]->mLogicalType) && (isAssignable)) {
                    if ((mM2mMPPs[j]->mLogicalType != MPP_LOGICAL_G2D_RGB) &&
                        (mM2mMPPs[j]->mLogicalType != MPP_LOGICAL_G2D_COMBO)) {
                        exynos_image otf_src_img = dst_img;
                        exynos_image otf_dst_img = dst_img;

                        otf_dst_img.format = DEFAULT_MPP_DST_FORMAT;

                        exynos_image image_lists[M2M_MPP_OUT_IMAGS_COUNT];
                        uint32_t imageNum = M2M_MPP_OUT_IMAGS_COUNT;
                        if ((ret = getCandidateM2mMPPOutImages(layer, &imageNum, image_lists)) < 0)
                        {
                            HWC_LOGE(display, "Fail getCandidateM2mMPPOutImages (%d)", ret);
                            return ret;
                        }
                        HDEBUGLOGD(eDebugResourceManager, "candidate M2mMPPOutImage num: %d", imageNum);
                        for (uint32_t outImg = 0; outImg < imageNum; outImg++)
                        {
                            dumpExynosImage(eDebugResourceManager, image_lists[outImg]);
                            otf_src_img = image_lists[outImg];
                            /* transform is already handled by m2mMPP */
                            otf_src_img.transform = 0;
                            otf_dst_img.transform = 0;

                            uint32_t isSupported = 0;
                            if ((isSupported = mM2mMPPs[j]->isSupported(display, src_img, otf_src_img)) != NO_ERROR)
                            {
                                HDEBUGLOGD(eDebugResourceManager, "\t\t\t check %s: supportedBit(0x%8x)",
                                        mM2mMPPs[j]->mName.string(), -isSupported);
                                continue;
                            }

                            /* 4. Find available OtfMPP for output of m2mMPP */
                            for (uint32_t k = 0; k < mOtfMPPs.size(); k++) {
                                isSupported = mOtfMPPs[k]->isSupported(display, otf_src_img, otf_dst_img);
                                isAssignable = mOtfMPPs[k]->isAssignable(display, otf_src_img, otf_dst_img);
                                HDEBUGLOGD(eDebugResourceManager, "\t\t\t check %s: supportedBit(0x%8x), isAssignable(%d)",
                                        mOtfMPPs[k]->mName.string(), -isSupported, isAssignable);
                                if ((isSupported == NO_ERROR) && isAssignable) {
                                    if (((ret = mM2mMPPs[j]->assignMPP(display, layer)) != NO_ERROR) ||
                                            ((ret = mOtfMPPs[k]->assignMPP(display, layer)) != NO_ERROR))
                                    {
                                        HWC_LOGE(display, "%s:: %s or %s MPP assignMPP() error (%d)",
                                                __func__, mM2mMPPs[j]->mName.string(),
                                                mOtfMPPs[k]->mName.string(), ret);
                                        return ret;
                                    }
                                    layer->mValidateCompositionType = HWC2_COMPOSITION_DEVICE;
                                    layer->setExynosMidImage(otf_src_img);

                                    HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] layer: %s, %s are assigned, mWindowNumUsed(%d)",
                                            i, layer->mOtfMPP->mName.string(), layer->mM2mMPP->mName.string(), display->mWindowNumUsed);
                                    assigned = true;
                                    break;
                                }
                            }

                            if (assigned)
                                break;
                        }
                    } else {
                        if ((ret = mM2mMPPs[j]->assignMPP(display, layer)) != NO_ERROR)
                        {
                            HWC_LOGE(display, "%s:: %s MPP assignMPP() error (%d)",
                                    __func__, mM2mMPPs[j]->mName.string(), ret);
                            return ret;
                        }

                        assigned = true;
                        HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] layer: %s is assigned, mWindowNumUsed(%d)",
                                i, layer->mM2mMPP->mName.string(), display->mWindowNumUsed);
                        layer->mValidateCompositionType = HWC2_COMPOSITION_EXYNOS;
                        break;
                    }
                }

                if (assigned)
                    break;
            }
        }

        if ((layer->mOtfMPP != NULL) && (layer->mM2mMPP != NULL)) {
            /* m2mMPP, otfMPP were assigned, go to next layer */
            display->mWindowNumUsed++;
            HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] layer: mWindowNumUsed(%d)",
                    i, display->mWindowNumUsed);
            if (layer->mOtfMPP != NULL)
                layer->mOtfMPP->updateUsedCapacity();
            if (layer->mM2mMPP != NULL)
                layer->mM2mMPP->updateUsedCapacity();

            continue;
        } else if (layer->mM2mMPP != NULL) {
            HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] layer: exynosComposition", i);
            /* G2D composition */
            if (((ret = display->addExynosCompositionLayer(i)) == EXYNOS_ERROR_CHANGED) ||
                 (ret < 0))
                return ret;
            else {
                /* Update used capacity */
                if (display->mExynosCompositionInfo.mM2mMPP != NULL)
                    display->mExynosCompositionInfo.mM2mMPP->updateUsedCapacity();
                else
                    HWC_LOGE(display, "ExynosCompositionInfo.mM2mMPP for Display[%d] is NULL",
                            display->mType);
            }
        } else {
            /* Fail to assign resource, set HWC2_COMPOSITION_CLIENT */
            if (validateFlag != NO_ERROR)
                layer->mOverlayInfo |= validateFlag;
            else
                layer->mOverlayInfo |= eMPPUnsupported;

            layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
            if (((ret = display->addClientCompositionLayer(i)) == EXYNOS_ERROR_CHANGED) ||
                (ret < 0))
                return ret;
        }

    }
    return ret;
}

int32_t ExynosResourceManager::assignWindow(ExynosDisplay *display)
{
    HDEBUGLOGD(eDebugResourceManager, "%s +++++", __func__);
    int ret = NO_ERROR;
    uint32_t windowIndex = 0;

    if (!display->mUseDecon)
        return ret;

    windowIndex = display->mBaseWindowIndex;

    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->mLayers[i];
        HDEBUGLOGD(eDebugResourceManager, "\t[%d] layer type: %d", i, layer->mValidateCompositionType);

        if (layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE) {
            if (layer->mOtfMPP == mSecureMPP) {
                layer->mWindowIndex = MAX_DECON_WIN - 1;
                HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] layer is assigned to secureMPP, windowIndex: %d",
                        i, layer->mWindowIndex);
            } else {
                layer->mWindowIndex = windowIndex;
                HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] layer windowIndex: %d", i, windowIndex);
            }
        } else if ((layer->mValidateCompositionType == HWC2_COMPOSITION_CLIENT) ||
                   (layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS)) {
            ExynosCompositionInfo *compositionInfo;
            if (layer->mValidateCompositionType == HWC2_COMPOSITION_CLIENT)
                compositionInfo = &display->mClientCompositionInfo;
            else
                compositionInfo = &display->mExynosCompositionInfo;

            if ((compositionInfo->mHasCompositionLayer == false) ||
                (compositionInfo->mFirstIndex < 0) ||
                (compositionInfo->mLastIndex < 0)) {
                HWC_LOGE(display, "%s:: Invalid %s CompositionInfo mHasCompositionLayer(%d), "
                        "mFirstIndex(%d), mLastIndex(%d) ",
                        __func__, compositionInfo->getTypeStr().string(),
                        compositionInfo->mHasCompositionLayer,
                        compositionInfo->mFirstIndex,
                        compositionInfo->mLastIndex);
                continue;
            }
            if (i != (uint32_t)compositionInfo->mLastIndex)
                continue;
            if (compositionInfo->mOtfMPP == mSecureMPP) {
                compositionInfo->mWindowIndex = MAX_DECON_WIN - 1;
                HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] %s Composition is assigned to secureMPP, windowIndex: %d",
                        i, compositionInfo->getTypeStr().string(), compositionInfo->mWindowIndex);
            } else {
                compositionInfo->mWindowIndex = windowIndex;
                HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] %s Composition windowIndex: %d",
                        i, compositionInfo->getTypeStr().string(), windowIndex);
            }
        } else {
            HWC_LOGE(display, "%s:: Invalid layer compositionType layer(%d), compositionType(%d)",
                    __func__, i, layer->mValidateCompositionType);
            continue;
        }
        windowIndex++;
    }
    HDEBUGLOGD(eDebugResourceManager, "%s ------", __func__);
    return ret;
}

/**
 * @param * display
 * @return int
 */
int32_t ExynosResourceManager::updateSupportedMPPFlag(ExynosDisplay * display)
{
    int32_t ret = 0;
    HDEBUGLOGD(eDebugResourceManager, "%s++++++++++", __func__);
    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->mLayers[i];
        HDEBUGLOGD(eDebugResourceManager, "[%d] layer ", i);

        /* TEST */
        layer->mGeometryChanged = 0x10;

        if (layer->mGeometryChanged == 0)
            continue;

        exynos_image src_img;
        exynos_image dst_img;
        layer->setSrcExynosImage(&src_img);
        layer->setDstExynosImage(&dst_img);
        dst_img.format = DEFAULT_MPP_DST_FORMAT;
        HDEBUGLOGD(eDebugResourceManager, "\tsrc_img");
        dumpExynosImage(eDebugResourceManager, src_img);
        HDEBUGLOGD(eDebugResourceManager, "\tdst_img");
        dumpExynosImage(eDebugResourceManager, dst_img);

        /* Initialize flags */
        layer->mSupportedMPPFlag = 0;
        layer->mCheckMPPFlag.clear();

        /* Check OtfMPPs */
        for (uint32_t j = 0; j < mOtfMPPs.size(); j++) {
            if ((ret = mOtfMPPs[j]->isSupported(display, src_img, dst_img)) == NO_ERROR) {
                layer->mSupportedMPPFlag |= mOtfMPPs[j]->mLogicalType;
                HDEBUGLOGD(eDebugResourceManager, "\t%s: supported", mOtfMPPs[j]->mName.string());
            }
            if (ret < 0) {
                HDEBUGLOGD(eDebugResourceManager, "\t%s: unsupported flag(0x%8x)", mOtfMPPs[j]->mName.string(), -ret);
                uint32_t checkFlag = 0x0;
                if (layer->mCheckMPPFlag.count(mOtfMPPs[j]->mLogicalType) != 0) {
                    checkFlag = layer->mCheckMPPFlag.at(mOtfMPPs[j]->mLogicalType);
                }
                checkFlag |= (-ret);
                layer->mCheckMPPFlag[mOtfMPPs[j]->mLogicalType] = checkFlag;
            }
        }

        /* Check M2mMPPs */
        for (uint32_t j = 0; j < mM2mMPPs.size(); j++) {
            if ((ret = mM2mMPPs[j]->isSupported(display, src_img, dst_img)) == NO_ERROR) {
                layer->mSupportedMPPFlag |= mM2mMPPs[j]->mLogicalType;
                HDEBUGLOGD(eDebugResourceManager, "\t%s: supported", mM2mMPPs[j]->mName.string());
            }
            if (ret < 0) {
                HDEBUGLOGD(eDebugResourceManager, "\t%s: unsupported flag(0x%8x)", mM2mMPPs[j]->mName.string(), -ret);
                uint32_t checkFlag = 0x0;
                if (layer->mCheckMPPFlag.count(mM2mMPPs[j]->mLogicalType) != 0) {
                    checkFlag = layer->mCheckMPPFlag.at(mM2mMPPs[j]->mLogicalType);
                }
                checkFlag |= (-ret);
                layer->mCheckMPPFlag[mM2mMPPs[j]->mLogicalType] = checkFlag;
            }
        }
        HDEBUGLOGD(eDebugResourceManager, "[%d] layer mSupportedMPPFlag(0x%8x)", i, layer->mSupportedMPPFlag);
    }
    HDEBUGLOGD(eDebugResourceManager, "%s-------------", __func__);

    return NO_ERROR;
}

int32_t ExynosResourceManager::resetResources()
{
    HDEBUGLOGD(eDebugResourceManager, "%s+++++++++", __func__);

    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        mOtfMPPs[i]->resetMPP();
        if (hwcCheckDebugMessages(eDebugResourceManager)) {
            String8 dumpMPP;
            mOtfMPPs[i]->dump(dumpMPP);
            HDEBUGLOGD(eDebugResourceManager, "%s", dumpMPP.string());
        }
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        mM2mMPPs[i]->resetMPP();
        if (hwcCheckDebugMessages(eDebugResourceManager)) {
            String8 dumpMPP;
            mM2mMPPs[i]->dump(dumpMPP);
            HDEBUGLOGD(eDebugResourceManager, "%s", dumpMPP.string());
        }
    }
    mSecureMPP->resetMPP();

    HDEBUGLOGD(eDebugResourceManager, "%s-----------",  __func__);
    return NO_ERROR;
}

int32_t ExynosResourceManager::preAssignResources()
{
    HDEBUGLOGD(eDebugResourceManager, "%s+++++++++", __func__);
    if (mSecureMPP->mEnable == false)
        mSecureMPP->reserveMPP();

    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if (mOtfMPPs[i]->mEnable == false) {
            mOtfMPPs[i]->reserveMPP();
            continue;
        }

        if (mOtfMPPs[i]->mPreAssignDisplayList != 0) {
            HDEBUGLOGD(eDebugResourceManager, "\t%s check, 0x%8x", mOtfMPPs[i]->mName.string(), mOtfMPPs[i]->mPreAssignDisplayList);
            ExynosDisplay *display = NULL;
            for (uint32_t display_type = 0; display_type < HWC_NUM_DISPLAY_TYPES; display_type++) {
                HDEBUGLOGD(eDebugResourceManager, "\t\tdisplay_type(%d), checkBit(%d)", display_type, (mOtfMPPs[i]->mPreAssignDisplayList & (1<<display_type)));
                if (mOtfMPPs[i]->mPreAssignDisplayList & (1<<display_type)) {
                    display = mDevice->getDisplay(display_type);
                    HDEBUGLOGD(eDebugResourceManager, "\t\tdisplay_type(%d), display(%p)", display_type, display);
                    if ((display != NULL) && (display->mPlugState == true)) {
                        HDEBUGLOGD(eDebugResourceManager, "\t\treserve to display %d", display_type);
                        mOtfMPPs[i]->reserveMPP(display->mType);
                        break;
                    }
                }
            }
        }
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mEnable == false) {
            mM2mMPPs[i]->reserveMPP();
            continue;
        }

        if ((mDevice->mGeometryChanged & GEOMETRY_MSC_RESERVED) &&
            (mM2mMPPs[i]->mPhysicalType == MPP_MSC)) {
            /* MSC can't be used for rendering */
            HDEBUGLOGD(eDebugResourceManager, "\t\tMPP_MSC reserve without display because preview is running");
            mM2mMPPs[i]->reserveMPP();
            continue;
        }
        HDEBUGLOGD(eDebugResourceManager, "\t%s check, 0x%8x", mM2mMPPs[i]->mName.string(), mM2mMPPs[i]->mPreAssignDisplayList);
        if (mM2mMPPs[i]->mPreAssignDisplayList != 0) {
            ExynosDisplay *display = NULL;
            for (uint32_t display_type = HWC_DISPLAY_PRIMARY; display_type < HWC_NUM_DISPLAY_TYPES; display_type++) {
                HDEBUGLOGD(eDebugResourceManager, "\t\tdisplay_type(%d), checkBit(%d)", display_type, (mM2mMPPs[i]->mPreAssignDisplayList & (1<<display_type)));
                if (mM2mMPPs[i]->mPreAssignDisplayList & (1<<display_type)) {
                    display = mDevice->getDisplay(display_type);
                    HDEBUGLOGD(eDebugResourceManager, "\t\tdisplay_type(%d), display(%p)", display_type, display);
                    if ((display != NULL) && (display->mPlugState == true)) {
                        HDEBUGLOGD(eDebugResourceManager, "\t\treserve to display %d", display->mType);
                        mM2mMPPs[i]->reserveMPP(display->mType);
                        break;
                    } else {
                        HDEBUGLOGD(eDebugResourceManager, "\t\treserve without display");
                        mM2mMPPs[i]->reserveMPP();
                    }
                }
            }
        }
    }
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if (hwcCheckDebugMessages(eDebugResourceManager)) {
            String8 dumpMPP;
            mOtfMPPs[i]->dump(dumpMPP);
            HDEBUGLOGD(eDebugResourceManager, "%s", dumpMPP.string());
        }
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (hwcCheckDebugMessages(eDebugResourceManager)) {
            String8 dumpMPP;
            mM2mMPPs[i]->dump(dumpMPP);
            HDEBUGLOGD(eDebugResourceManager, "%s", dumpMPP.string());
        }
    }
    HDEBUGLOGD(eDebugResourceManager, "%s-----------",  __func__);
    return NO_ERROR;
}

int32_t ExynosResourceManager::preProcessLayer(ExynosDisplay * display)
{
    int32_t ret = 0;
    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->mLayers[i];
        if ((ret = layer->doPreProcess()) < 0) {
            HWC_LOGE(display, "%s:: doPreProcess() error, display(%d), layer %d", __func__, display->mType, i);
            return ret;
        }
    }

    // Re-align layer priority for max overlay resources
    uint32_t mNumMaxPriorityLayers = 0;
    for (int i = (display->mLayers.size()-1); i >= 0; i--) {
        ExynosLayer *layer = display->mLayers[i];
        HDEBUGLOGD(eDebugResourceManager, "Priority align: i:%d, layer priority:%d, Max:%d, mNumMaxPriorityAllowed:%d", i,
                layer->mOverlayPriority, mNumMaxPriorityLayers, display->mNumMaxPriorityAllowed);
        if (layer->mOverlayPriority == ePriorityMax) {
            if (mNumMaxPriorityLayers >= display->mNumMaxPriorityAllowed) {
                layer->mOverlayPriority = ePriorityHigh;
            }
            mNumMaxPriorityLayers++;
        }
    }

    return NO_ERROR;
}

ExynosMPP* ExynosResourceManager::getExynosMPP(uint32_t type)
{
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if (mOtfMPPs[i]->mLogicalType == type)
            return mOtfMPPs[i];
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mLogicalType == type)
            return mM2mMPPs[i];
    }

    return NULL;
}

int32_t ExynosResourceManager::stopUnAssignedResource()
{
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if (mOtfMPPs[i]->mAssignedSources.size() == 0)
            mOtfMPPs[i]->requestHWStateChange(MPP_HW_STATE_IDLE);
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mAssignedSources.size() == 0)
            mM2mMPPs[i]->requestHWStateChange(MPP_HW_STATE_IDLE);
    }
    return NO_ERROR;
}

int32_t ExynosResourceManager::deliverPerformanceInfo()
{
    int ret = NO_ERROR;
    for (uint32_t mpp_physical_type = 0; mpp_physical_type < MPP_P_TYPE_MAX; mpp_physical_type++) {
        /* Only G2D gets performance info in current version */
        if (mpp_physical_type != MPP_G2D)
            continue;
        AcrylicPerformanceRequest request;
        uint32_t assignedInstanceNum = 0;
        uint32_t assignedInstanceIndex = 0;
        ExynosMPP *mpp = NULL;
        bool canSkipSetting = true;

        for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
            mpp = mM2mMPPs[i];
            if (mpp->mPhysicalType != mpp_physical_type)
                continue;
            /* Performance setting can be skipped
             * if all of instance's mPrevAssignedState, mAssignedState
             * are MPP_ASSIGN_STATE_FREE
             */
            if ((mpp->mPrevAssignedState != MPP_ASSIGN_STATE_FREE) ||
                (mpp->mAssignedState != MPP_ASSIGN_STATE_FREE))
            {
                canSkipSetting = false;
            }

            if (mpp->canSkipProcessing())
                continue;

            if ((mpp->mAssignedDisplay != NULL) &&
                (mpp->mAssignedSources.size() > 0))
            {
                assignedInstanceNum++;
            }
        }
        if ((canSkipSetting == true) && (assignedInstanceNum != 0)) {
            HWC_LOGE(NULL, "%s:: canSKip true but assignedInstanceNum(%d)",
                    __func__, assignedInstanceNum);
        }
        request.reset(assignedInstanceNum);

        if (canSkipSetting == true)
            continue;

        for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
            mpp = mM2mMPPs[i];
            if ((mpp->mPhysicalType == mpp_physical_type) &&
                (mpp->mAssignedDisplay != NULL) &&
                (mpp->mAssignedSources.size() > 0))
            {
                if (mpp->canSkipProcessing())
                    continue;
                if (assignedInstanceIndex >= assignedInstanceNum) {
                    HWC_LOGE(NULL,"assignedInstanceIndex error (%d, %d)", assignedInstanceIndex, assignedInstanceNum);
                    break;
                }
                AcrylicPerformanceRequestFrame *frame = request.getFrame(assignedInstanceIndex);
                if(frame->reset(mpp->mAssignedSources.size()) == false) {
                    HWC_LOGE(NULL,"%d frame reset fail (%zu)", assignedInstanceIndex, mpp->mAssignedSources.size());
                    break;
                }
                for (uint32_t j = 0; j < mpp->mAssignedSources.size(); j++) {
                    ExynosMPPSource* mppSource = mpp->mAssignedSources[j];
                    frame->setSourceDimension(j,
                            mppSource->mSrcImg.w, mppSource->mSrcImg.h,
                            mppSource->mSrcImg.format);

                    hwc_rect_t src_area;
                    src_area.left = mppSource->mSrcImg.x;
                    src_area.top = mppSource->mSrcImg.y;
                    src_area.right = mppSource->mSrcImg.x + mppSource->mSrcImg.w;
                    src_area.bottom = mppSource->mSrcImg.y + mppSource->mSrcImg.h;

                    hwc_rect_t out_area;
                    out_area.left = mppSource->mMidImg.x;
                    out_area.top = mppSource->mMidImg.y;
                    out_area.right = mppSource->mMidImg.x + mppSource->mMidImg.w;
                    out_area.bottom = mppSource->mMidImg.y + mppSource->mMidImg.h;

                    frame->setTransfer(j, src_area, out_area, mppSource->mSrcImg.transform);
                }
                uint32_t format = mpp->mAssignedSources[0]->mMidImg.format;
                bool hasSolidColorLayer = false;
                if ((mpp->mLogicalType == MPP_LOGICAL_G2D_RGB) ||
                    (mpp->mLogicalType == MPP_LOGICAL_G2D_COMBO)) {
                    format = DEFAULT_MPP_DST_FORMAT;
                    hasSolidColorLayer = true;
                }

                frame->setTargetDimension(mpp->mAssignedDisplay->mXres,
                        mpp->mAssignedDisplay->mYres, format, hasSolidColorLayer);

                assignedInstanceIndex++;
            }
        }
        if ((mpp = getExynosMPP(MPP_LOGICAL_G2D_RGB)) != NULL)
            mpp->mCompositor->requestPerformanceQoS(&request);
        else if ((mpp = getExynosMPP(MPP_LOGICAL_G2D_COMBO)) != NULL)
            mpp->mCompositor->requestPerformanceQoS(&request);
        else
            HWC_LOGE(NULL,"getExynosMPP(MPP_LOGICAL_G2D_RGB) failed");
    }
    return ret;
}

/*
 * Get used capacity of the resource that abstracts same HW resource
 * but it is different instance with mpp
 */
float ExynosResourceManager::getResourceUsedCapa(ExynosMPP &mpp)
{
    float usedCapa = 0;
    if (mpp.mCapacity < 0)
        return usedCapa;

    HDEBUGLOGD(eDebugResourceManager, "%s:: [%s][%d] mpp[%d, %d]",
            __func__, mpp.mName.string(), mpp.mLogicalIndex,
            mpp.mPhysicalType, mpp.mPhysicalIndex);

    if (mpp.mMPPType == MPP_TYPE_OTF) {
        for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
            if ((mpp.mPhysicalType == mOtfMPPs[i]->mPhysicalType) &&
                (mpp.mPhysicalIndex == mOtfMPPs[i]->mPhysicalIndex)) {
                usedCapa += mOtfMPPs[i]->mUsedCapacity;
            }
        }
    } else {
        for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
            if ((mpp.mPhysicalType == mM2mMPPs[i]->mPhysicalType) &&
                (mpp.mPhysicalIndex == mM2mMPPs[i]->mPhysicalIndex)) {
                usedCapa += mM2mMPPs[i]->mUsedCapacity;
            }
        }
    }

    HDEBUGLOGD(eDebugResourceManager, "\t[%s][%d] mpp usedCapa: %f",
            mpp.mName.string(), mpp.mLogicalIndex, usedCapa);
    return usedCapa;
}

void ExynosResourceManager::enableMPP(uint32_t physicalType, uint32_t physicalIndex, uint32_t logicalIndex, uint32_t enable)
{
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if ((mOtfMPPs[i]->mPhysicalType == physicalType) &&
            (mOtfMPPs[i]->mPhysicalIndex == physicalIndex) &&
            (mOtfMPPs[i]->mLogicalIndex == logicalIndex)) {
            mOtfMPPs[i]->mEnable = !!(enable);
            return;
        }
    }

    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if ((mM2mMPPs[i]->mPhysicalType == physicalType) &&
            (mM2mMPPs[i]->mPhysicalIndex == physicalIndex) &&
            (mM2mMPPs[i]->mLogicalIndex == logicalIndex)) {
            mM2mMPPs[i]->mEnable = !!(enable);
            return;
        }
    }

    if ((mSecureMPP->mPhysicalType == physicalType) &&
        (mSecureMPP->mPhysicalIndex == physicalIndex) &&
        (mSecureMPP->mLogicalIndex == logicalIndex)) {
        mSecureMPP->mEnable = !!(enable);
        return;
    }
}

int32_t  ExynosResourceManager::prepareResources()
{
    int ret = NO_ERROR;
    HDEBUGLOGD(eDebugResourceManager, "This is first validate");
    if ((ret = resetResources()) != NO_ERROR) {
        HWC_LOGE(NULL,"%s:: resetResources() error (%d)",
                __func__, ret);
        return ret;
    }
    if ((ret = preAssignResources()) != NO_ERROR) {
        HWC_LOGE(NULL,"%s:: preAssignResources() error (%d)",
                __func__, ret);
        return ret;
    }

	return ret;
}

int32_t ExynosResourceManager::finishAssignResourceWork()
{
	int ret = NO_ERROR;
    if ((ret = stopUnAssignedResource()) != NO_ERROR) {
        HWC_LOGE(NULL,"%s:: stopUnAssignedResource() error (%d)",
                __func__, ret);
        return ret;
    }

    if ((ret = deliverPerformanceInfo()) != NO_ERROR) {
        HWC_LOGE(NULL,"%s:: deliverPerformanceInfo() error (%d)",
                __func__, ret);
        return ret;
    }

	return ret;
}
