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

#ifndef _EXYNOSMPP_H
#define _EXYNOSMPP_H

#include <utils/Thread.h>
#include <utils/Mutex.h>
#include <utils/Condition.h>
#include <utils/String8.h>
#include <utils/List.h>
#include <hardware/exynos/acryl.h>
#include "ExynosHWCHelper.h"
#include "ExynosResourceRestriction.h"

#ifdef GRALLOC_VERSION1
#include "GrallocWrapper.h"

namespace android {
namespace GrallocWrapper {
    class Mapper;
    class Allocator;
}
}
#endif

class ExynosDisplay;
class ExynosMPP;

#ifndef NUM_MPP_SRC_BUFS
#define NUM_MPP_SRC_BUFS 7
#endif
#ifndef NUM_MPP_DST_BUFS
#define NUM_MPP_DST_BUFS_DEFAULT 3
#define NUM_MPP_DST_BUFS(type) ((type == MPP_LOGICAL_G2D_RGB) ? 3:3)
#endif

#ifndef G2D_MAX_SRC_NUM
#define G2D_MAX_SRC_NUM 7
#endif

#ifndef G2D_RESTRICTIVE_SRC_NUM
#define G2D_RESTRICTIVE_SRC_NUM   5
#endif

#ifndef G2D_BASE_PPC_ROT
#define G2D_BASE_PPC_ROT  2.3
#endif
#ifndef G2D_BASE_PPC
#define G2D_BASE_PPC  2.8
#endif
#ifndef G2D_BASE_PPC_COLORFILL
#define G2D_BASE_PPC_COLORFILL  3.8
#endif

#ifndef VPP_CLOCK
#define VPP_CLOCK       400000000
#endif
#ifndef VPP_VCLK
#define VPP_VCLK        133
#endif
#ifndef VPP_MIC_FACTOR
#define VPP_MIC_FACTOR  2
#endif

#ifndef VPP_TE_PERIOD
#define VPP_TE_PERIOD 63
#endif
#ifndef VPP_MARGIN
#define VPP_MARGIN 1.1
#endif

#define VPP_RESOL_CLOCK_FACTOR (VPP_TE_PERIOD * VPP_MARGIN)
#ifndef VPP_DISP_FACTOR
#define VPP_DISP_FACTOR 1.0
#endif
#ifndef VPP_PIXEL_PER_CLOCK
#define VPP_PIXEL_PER_CLOCK 2
#endif

#ifndef MPP_G2D_CAPACITY
#define MPP_G2D_CAPACITY    8
#endif

#ifndef MPP_G2D_SRC_SCALED_WEIGHT
#define MPP_G2D_SRC_SCALED_WEIGHT   1.125
#endif

#ifndef MPP_G2D_DST_ROT_WEIGHT
#define MPP_G2D_DST_ROT_WEIGHT  2.0
#endif

using namespace android;

enum {
    eMPPUnsupportedDownScale      =     0x00000001,
    eMPPStrideCrop                =     0x00000002,
    eMPPUnsupportedRotation       =     0x00000004,
    eMPPHWBusy                    =     0x00000008,
    eMPPUnsupportedBlending       =     0x00000040,
    eMPPUnsupportedFormat         =     0x00000080,
    eMPPNotAlignedDstSize         =     0x00000100,
    eMPPNotAlignedSrcCropPosition =     0x00000200,
    eMPPNotAlignedHStride         =     0x00000400,
    eMPPNotAlignedVStride         =     0x00000800,
    eMPPExceedHStrideMaximum      =     0x00001000,
    eMPPExceedVStrideMaximum      =     0x00002000,
    eMPPExeedMaxDownScale         =     0x00004000,
    eMPPExeedMaxDstWidth          =     0x00008000,
    eMPPExeedMaxDstHeight         =     0x00010000,
    eMPPExeedMinSrcWidth          =     0x00020000,
    eMPPExeedMinSrcHeight         =     0x00040000,
    eMPPExeedMaxUpScale           =     0x00080000,
    eMPPExeedSrcWCropMax          =     0x00100000,
    eMPPExeedSrcHCropMax          =     0x00200000,
    eMPPExeedSrcWCropMin          =     0x00400000,
    eMPPExeedSrcHCropMin          =     0x00800000,
    eMPPNotAlignedCrop            =     0x01000000,
    eMPPNotAlignedOffset          =     0x02000000,
    eMPPExeedMinDstWidth          =     0x04000000,
    eMPPExeedMinDstHeight         =     0x08000000,
    eMPPUnsupportedCompression    =     0x10000000,
    eMPPUnsupportedCSC            =     0x20000000,
    eMPPUnsupportedDIMLayer       =     0x40000000,
    eMPPUnsupportedDRM            =     0x80000000,
};

enum {
    MPP_TYPE_NONE,
    MPP_TYPE_OTF,
    MPP_TYPE_M2M
};

enum {
    MPP_ASSIGN_STATE_FREE       =     0x00000000,
    MPP_ASSIGN_STATE_RESERVED   =     0x00000001,
    MPP_ASSIGN_STATE_ASSIGNED   =     0x00000002,
};

enum {
    MPP_HW_STATE_IDLE,
    MPP_HW_STATE_RUNNING
};

enum {
    MPP_BUFFER_NORMAL = 0,
    MPP_BUFFER_NORMAL_DRM,
    MPP_BUFFER_SECURE_DRM,
    MPP_BUFFER_DUMP,
};

enum {
    MPP_MEM_MMAP = 1,
    MPP_MEM_USERPTR,
    MPP_MEM_OVERLAY,
    MPP_MEM_DMABUF,
};

enum {
    MPP_SOURCE_NO_TYPE = 0,
    MPP_SOURCE_COMPOSITION_TARGET,
    MPP_SOURCE_LAYER
};

/* Based on multi-resolution feature */
typedef enum {
    DST_SIZE_HD = 0,
    DST_SIZE_HD_PLUS,
    DST_SIZE_FHD,
    DST_SIZE_FHD_PLUS,
    DST_SIZE_WQHD,
    DST_SIZE_WQHD_PLUS,
    DST_SIZE_UNKNOWN,
} dst_alloc_buf_size_t;

#ifndef DEFAULT_MPP_DST_FORMAT
#define DEFAULT_MPP_DST_FORMAT HAL_PIXEL_FORMAT_RGBA_8888
#endif
#ifndef DEFAULT_MPP_DST_YUV_FORMAT
#define DEFAULT_MPP_DST_YUV_FORMAT HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN
#endif

typedef struct exynos_mpp_img_info {
    private_handle_t *bufferHandle;
    uint32_t bufferType;
    uint32_t format;
    union {
        exynos_mpp_img mppImg; /* For scaler */
        struct { /* For G2D */
            AcrylicLayer *mppLayer;
            int acrylicAcquireFenceFd;
            int acrylicReleaseFenceFd;
        };
    };
} exynos_mpp_img_info_t;

void dumpExynosMPPImgInfo(uint32_t type, exynos_mpp_img_info &imgInfo);

struct ExynosMPPFrameInfo
{
    uint32_t srcNum;
    exynos_image srcInfo[NUM_MPP_SRC_BUFS];
    exynos_image dstInfo[NUM_MPP_SRC_BUFS];
};

class ExynosMPPSource {
    public:
        ExynosMPPSource();
        ExynosMPPSource(uint32_t sourceType, void *source);
        ~ExynosMPPSource(){};
        void setExynosImage(exynos_image src_img, exynos_image dst_img);
        void setExynosMidImage(exynos_image mid_img);

        uint32_t mSourceType;
        void *mSource;
        exynos_image mSrcImg;
        exynos_image mDstImg;
        exynos_image mMidImg;

        ExynosMPP *mOtfMPP;
        ExynosMPP *mM2mMPP;
};
bool exynosMPPSourceComp(const ExynosMPPSource* l, const ExynosMPPSource* r);

class ExynosMPP {
private:
    class ResourceManageThread: public Thread {
        private:
            ExynosMPP *mExynosMPP;
            Condition mCondition;
            List<exynos_mpp_img_info > mFreedBuffers;
            List<int> mStateFences;

            void freeBuffers();
            bool checkStateFences();
        public:
            bool mRunning;
            Mutex mMutex;
            ResourceManageThread(ExynosMPP *exynosMPP);
            ~ResourceManageThread();
            virtual bool threadLoop();
            void addFreedBuffer(exynos_mpp_img_info freedBuffer);
            void addStateFence(int fence);
    };

public:
    /**
     * Resource type
     * Ex: MPP_DPP_VGF, MPP_DPP_VG, MPP_MSC, MPP_G2D
     */
    uint32_t mMPPType;
    uint32_t  mPhysicalType;
    uint32_t  mLogicalType;
    String8 mName;
    uint32_t mPhysicalIndex;
    uint32_t mLogicalIndex;
    uint32_t mPreAssignDisplayList;
    static int mainDisplayWidth;
    static int mainDisplayHeight;

    uint32_t mHWState;
    int mLastStateFenceFd;
    uint32_t mAssignedState;
    bool    mEnable;

    ExynosDisplay *mAssignedDisplay;

    /* Some resource can support blending feature
     * then source can be multiple layers */
    Vector <ExynosMPPSource* > mAssignedSources;
    uint32_t mMaxSrcLayerNum;

    uint32_t mPrevAssignedState;
    int32_t mPrevAssignedDisplayType;
    int32_t mReservedDisplay;

#ifdef GRALLOC_VERSION1
    GrallocWrapper::Allocator* mAllocator;
    GrallocWrapper::Mapper* mMapper;
#else
    alloc_device_t *mAllocDevice;
#endif
    ResourceManageThread mResourceManageThread;
    float mCapacity;
    float mUsedCapacity;
    bool mAllocOutBufFlag;
    bool mFreeOutBufFlag;
    bool mHWBusyFlag;
    /* For reuse previous frame */
    ExynosMPPFrameInfo mPrevFrameInfo;
    struct exynos_mpp_img_info mSrcImgs[NUM_MPP_SRC_BUFS];
    struct exynos_mpp_img_info mDstImgs[NUM_MPP_DST_BUFS_DEFAULT];
    int32_t mCurrentDstBuf;
    bool mNeedCompressedTarget;
    struct restriction_size mSrcSizeRestrictions[RESTRICTION_MAX];
    struct restriction_size mDstSizeRestrictions[RESTRICTION_MAX];

    // Force Dst buffer reallocation
    dst_alloc_buf_size_t mDstAllocatedSize;

    void *mMPPHandle;
    union {
        struct {
            /* For Scaler */
            MppFactory *mMppFact;
            LibMpp *mLibmpp;
        };
        struct {
            /* For G2D */
            Acrylic *mCompositor;
        };
    };

    ExynosMPP(uint32_t physicalType, uint32_t logicalType, const char *name,
            uint32_t physicalIndex, uint32_t logicalIndex, uint32_t preAssignInfo);
    virtual ~ExynosMPP();

    int32_t allocOutBuf(uint32_t w, uint32_t h, uint32_t format, uint64_t usage, uint32_t index);
#ifdef GRALLOC_VERSION1
    void setAllocDevice(GrallocWrapper::Allocator* allocator, GrallocWrapper::Mapper* mapper);
#else
    void setAllocDevice(alloc_device_t* allocDevice);
#endif

    int32_t setOutBuf(buffer_handle_t outbuf, int32_t fence);
    int32_t freeOutBuf(exynos_mpp_img_info dst);
    int32_t doPostProcessing(struct exynos_image &src, struct exynos_image &dst);
    int32_t doPostProcessing(uint32_t totalImags, uint32_t imageIndex, struct exynos_image &src, struct exynos_image &dst);
    int32_t setupRestriction();
    int32_t getSrcReleaseFence(uint32_t srcIndex);
    int32_t resetSrcReleaseFence();
    int32_t getDstImageInfo(exynos_image *img);
    int32_t setDstAcquireFence(int releaseFence);
    int32_t resetDstReleaseFence();
    int32_t requestHWStateChange(uint32_t state);
    int32_t setHWStateFence(int32_t fence);
    virtual int32_t isSupported(ExynosDisplay *display, struct exynos_image &src, struct exynos_image &dst);

    bool isCSCSupportedByMPP(struct exynos_image &src, struct exynos_image &dst);
    bool isSupportedBlend(struct exynos_image &src);
    bool isSupportedTransform(struct exynos_image &src);
    bool isSupportedDRM(struct exynos_image &src);
    virtual bool isSupportedHStrideCrop(struct exynos_image &src);
    uint32_t getMaxDownscale(struct exynos_image &src, struct exynos_image &dst);
    uint32_t getMaxUpscale(struct exynos_image &src, struct exynos_image &dst);
    uint32_t getSrcMaxWidth(struct exynos_image &src);
    uint32_t getSrcMaxHeight(struct exynos_image &src);
    uint32_t getSrcMinWidth(struct exynos_image &src);
    uint32_t getSrcMinHeight(struct exynos_image &src);
    uint32_t getSrcWidthAlign(struct exynos_image &src);
    uint32_t getSrcHeightAlign(struct exynos_image &src);
    uint32_t getSrcMaxCropWidth(struct exynos_image &src);
    uint32_t getSrcMaxCropHeight(struct exynos_image &src);
    uint32_t getSrcMinCropWidth(struct exynos_image &src);
    uint32_t getSrcMinCropHeight(struct exynos_image &src);
    uint32_t getSrcXOffsetAlign(struct exynos_image &src);
    uint32_t getSrcXOffsetAlign(uint32_t idx);
    uint32_t getSrcYOffsetAlign(struct exynos_image &src);
    uint32_t getSrcYOffsetAlign(uint32_t idx);
    uint32_t getSrcCropWidthAlign(struct exynos_image &src);
    uint32_t getSrcCropWidthAlign(uint32_t idx);
    uint32_t getSrcCropHeightAlign(struct exynos_image &src);
    uint32_t getSrcCropHeightAlign(uint32_t idx);
    bool isSrcFormatSupported(ExynosDisplay *display, struct exynos_image &src);
    bool isDimLayerSupported();

    uint32_t getDstMaxWidth(struct exynos_image &dst);
    uint32_t getDstMaxHeight(struct exynos_image &dst);
    uint32_t getDstMinWidth(struct exynos_image &dst);
    uint32_t getDstMinHeight(struct exynos_image &dst);
    uint32_t getDstWidthAlign(struct exynos_image &dst);
    uint32_t getDstHeightAlign(struct exynos_image &dst);
    uint32_t getOutBufAlign();
    bool isDstFormatSupported(struct exynos_image &src);
    uint32_t getSrcMaxBlendingNum(struct exynos_image &src, struct exynos_image &dst);
    uint32_t getAssignedSourceNum();

    /* Based on multi-resolution support */
    void setDstAllocSize(uint32_t width, uint32_t height);
    dst_alloc_buf_size_t getDstAllocSize();
    bool needPreAllocation();

    int32_t resetMPP();
    int32_t resetAssignedState();
    int32_t resetAssignedState(ExynosMPPSource *mppSource);
    int32_t reserveMPP(int32_t displayType = -1);

    bool isAssignableState(ExynosDisplay *display, struct exynos_image &src, struct exynos_image &dst);
    bool isAssignable(ExynosDisplay *display,
            struct exynos_image &src, struct exynos_image &dst);
    int32_t assignMPP(ExynosDisplay *display, ExynosMPPSource* mppSource);

    bool hasEnoughCapa(struct exynos_image &src, struct exynos_image &dst);
    float getRequiredCapacity(struct exynos_image &src, struct exynos_image &dst);
    int32_t updateUsedCapacity();
    void resetUsedCapacity() {mUsedCapacity = 0;};
    int prioritize(int priority);

    uint32_t getMPPClock();

    void dump(String8& result);
    uint32_t increaseDstBuffIndex();
    bool canSkipProcessing();

    virtual bool isSupportedCompression(struct exynos_image &src);

    void closeFences();

protected:
    uint32_t getBufferType(const private_handle_t *handle);
    uint32_t getBufferType(uint64_t usage);
    uint64_t getBufferUsage(uint64_t usage);

    bool needDstBufRealloc(struct exynos_image &dst, uint32_t index);
    bool needReConfig(struct exynos_image &src, struct exynos_image &dst);
    bool needReCreate(struct exynos_image &src, struct exynos_image &dst);
    bool canUsePrevFrame();
    int32_t doScalerPostProcessing(struct exynos_image &src, struct exynos_image &dst);
    int32_t setupScalerSrc(exynos_mpp_img_info *srcImgInfo, struct exynos_image &src);
    int32_t setupScalerDst(exynos_mpp_img_info *dstImgInfo, struct exynos_image &dst);
    int32_t getScalerDstImageInfo(exynos_image *img);
    int32_t setupDst(exynos_mpp_img_info *dstImgInfo);
    virtual int32_t doPostProcessingInternal();
    virtual int32_t setupLayer(exynos_mpp_img_info *srcImgInfo,
            struct exynos_image &src, struct exynos_image &dst);

    void *createMPPLib();
    void destroyMPPLib(void *handle);
    int configMPP(void *handle, exynos_mpp_img *src, exynos_mpp_img *dst);
    int runMPP(void *handle, exynos_mpp_img *src, exynos_mpp_img *dst);
    int stopMPP(void *handle);
    void destroyMPP(void *handle);
    int setCSCProperty(void *handle, unsigned int eqAuto, unsigned int fullRange, unsigned int colorspace);

    uint32_t getRestrictionClassification(struct exynos_image &img);
};

#endif //_EXYNOSMPP_H
