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

#include "ExynosMPPModule.h"
#include "ExynosHWCDebug.h"
#include "ExynosResourceManager.h"
#include "ExynosVirtualDisplay.h"

ExynosMPPModule::ExynosMPPModule(ExynosResourceManager* resourceManager,
        uint32_t physicalType, uint32_t logicalType, const char *name,
        uint32_t physicalIndex, uint32_t logicalIndex, uint32_t preAssignInfo)
    : ExynosMPP(resourceManager, physicalType, logicalType, name, physicalIndex, logicalIndex, preAssignInfo),
    mChipId(0x00)
{
    if (mPhysicalType == MPP_MSC && mMaxSrcLayerNum > 1)
        mNeedSolidColorLayer = false;
}

ExynosMPPModule::~ExynosMPPModule()
{
}

uint32_t ExynosMPPModule::getSrcXOffsetAlign(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].cropXAlign;
}

uint32_t ExynosMPPModule::getSrcMaxBlendingNum(struct exynos_image __unused &src, struct exynos_image __unused &dst)
{
    if (mLogicalType == MPP_LOGICAL_MSC_COMBO)
        return mMaxSrcLayerNum - 1;

    return mMaxSrcLayerNum;
}

bool ExynosMPPModule::hasEnoughCapa(ExynosDisplay *display, struct exynos_image &src, struct exynos_image &dst)
{
    bool ret = ExynosMPP::hasEnoughCapa(display, src, dst);

    if (!ret) {
        if ((mLogicalType == MPP_LOGICAL_MSC_COMBO) &&
                (mPreAssignDisplayInfo & HWC_DISPLAY_VIRTUAL_BIT) &&
                (getDrmMode(src.usageFlags) != NO_DRM)) {
            return true;
        }
    }

    return ret;
}

bool ExynosMPPModule::isSrcFormatSupported(struct exynos_image &src)
{
    if ((mLogicalType == MPP_LOGICAL_MSC_COMBO) &&
            (mPreAssignDisplayInfo & HWC_DISPLAY_VIRTUAL_BIT)) {

        if (isFormatRgb(src.format)) return false;

    }
    return ExynosMPP::isSrcFormatSupported(src);
}
