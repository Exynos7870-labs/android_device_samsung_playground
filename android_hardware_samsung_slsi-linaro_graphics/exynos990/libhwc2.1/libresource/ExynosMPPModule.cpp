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
#include "ExynosResourceRestriction.h"

ExynosMPPModule::ExynosMPPModule(ExynosResourceManager* resourceManager,
        uint32_t physicalType, uint32_t logicalType, const char *name,
        uint32_t physicalIndex, uint32_t logicalIndex, uint32_t preAssignInfo)
    : ExynosMPP(resourceManager, physicalType, logicalType, name, physicalIndex, logicalIndex, preAssignInfo),
    mChipId(0x00)
{
}

ExynosMPPModule::~ExynosMPPModule()
{
}

uint32_t ExynosMPPModule::getSrcXOffsetAlign(struct exynos_image &src)
{
    uint32_t idx = getRestrictionClassification(src);
    return mSrcSizeRestrictions[idx].cropXAlign;
}

uint32_t ExynosMPPModule::getDstWidthAlign(struct exynos_image &dst)
{
    if (((dst.format == HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_S10B) ||
         (dst.format == HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_S10B)) &&
        (mPhysicalType == MPP_MSC))
        return 4;

    return  ExynosMPP::getDstWidthAlign(dst);
}

bool ExynosMPPModule::isSupportedCompression(struct exynos_image &src)
{
    if (src.compressed) {
        if (mPhysicalType == MPP_G2D)
            return true;

        ExynosMPP *sharedMPP = NULL;
        if (mPhysicalType == MPP_DPP_GF) {
            if (mPhysicalIndex == 0) {
                /* GF1 shares memory with VGF0 */
                sharedMPP = mResourceManager->getExynosMPP(MPP_DPP_VGF, 0);
            } else if (mPhysicalIndex == 1) {
                /* GF0 shares memory with VGRFS0 */
                sharedMPP = mResourceManager->getExynosMPP(MPP_DPP_VGRFS, 0);
            } else {
                MPP_LOGE("Invalid MPP_DPP_GF index(%d)", mPhysicalIndex);
                return false;
            }
        } else if (mPhysicalType == MPP_DPP_VGF) {
            /* GF1 shares memory with VGF0 */
            sharedMPP = mResourceManager->getExynosMPP(MPP_DPP_GF, 0);
        } else if (mPhysicalType == MPP_DPP_VGRFS) {
            /* GF0 shares memory with VGRFS0 */
            sharedMPP = mResourceManager->getExynosMPP(MPP_DPP_GF, 1);
        } else {
            /* Other mpp don't support decompression */
            return false;
        }

        if (sharedMPP == NULL) {
            MPP_LOGE("sharedMPP is NULL");
            return false;
        }

        if ((sharedMPP->mAssignedState & MPP_ASSIGN_STATE_ASSIGNED) == 0)
            return true;
        else {
            if (sharedMPP->mAssignedSources.size() == 1) {
                exynos_image checkImg = sharedMPP->mAssignedSources[0]->mSrcImg;
                if ((sharedMPP->mAssignedSources[0]->mSourceType == MPP_SOURCE_COMPOSITION_TARGET) ||
                    (sharedMPP->mAssignedSources[0]->mM2mMPP != NULL)) {
                    checkImg = sharedMPP->mAssignedSources[0]->mMidImg;
                }
                if (checkImg.transform & HAL_TRANSFORM_ROT_90)
                    return false;

                if (checkImg.compressed == 0)
                    return true;

                if (checkImg.w > 2048)
                    return false;
                else {
                    if (src.w < 2048)
                        return true;
                    else
                        return false;
                }
            } else {
                MPP_LOGE("Invalid sharedMPP[%d, %d] mAssignedSources size(%zu)",
                        sharedMPP->mPhysicalType,
                        sharedMPP->mPhysicalIndex,
                        sharedMPP->mAssignedSources.size());
            }
        }
        return false;
    } else {
        return true;
    }
}

bool ExynosMPPModule::isSupportedTransform(struct exynos_image &src)
{
    ExynosMPP *sharedMPP = NULL;
    switch (mPhysicalType)
    {
    case MPP_MSC:
    case MPP_G2D:
        return true;
    case MPP_DPP_G:
    case MPP_DPP_GF:
    case MPP_DPP_VG:
    case MPP_DPP_VGS:
    case MPP_DPP_VGF:
    case MPP_DPP_VGFS:
        if ((src.transform & HAL_TRANSFORM_ROT_90) == 0)
        {
            if ((src.compressed == 1) && (src.transform != 0))
                return false;
            return true;
        } else {
            return false;
        }
    case MPP_DPP_VGRFS:
        if (isFormatYUV420(src.format)) {
            /* HACK */
            if ((src.transform != 0) && isFormat10BitYUV420(src.format)) return false;

            /* GF0 shares memory with VGRFS0 */
            sharedMPP = mResourceManager->getExynosMPP(MPP_DPP_GF, 1);
            if (sharedMPP == NULL) {
                MPP_LOGE("sharedMPP is NULL");
                return false;
            }

            if ((src.transform & HAL_TRANSFORM_ROT_90) &&
                (sharedMPP->mAssignedState & MPP_ASSIGN_STATE_ASSIGNED)) {
                if (sharedMPP->mAssignedSources.size() != 1) {
                    MPP_LOGE("Invalid sharedMPP[%d, %d] mAssignedSources size(%zu)",
                            sharedMPP->mPhysicalType,
                            sharedMPP->mPhysicalIndex,
                            sharedMPP->mAssignedSources.size());
                    return false;
                }
                exynos_image checkImg = sharedMPP->mAssignedSources[0]->mSrcImg;
                if ((sharedMPP->mAssignedSources[0]->mSourceType == MPP_SOURCE_COMPOSITION_TARGET) ||
                    (sharedMPP->mAssignedSources[0]->mM2mMPP != NULL)) {
                    checkImg = sharedMPP->mAssignedSources[0]->mMidImg;
                }
                if (checkImg.compressed == 1)
                    return false;
                }
            return true;
        }
        /* RGB case */
        if ((src.transform & HAL_TRANSFORM_ROT_90) == 0)
        {
            if ((src.compressed == 1) && (src.transform != 0))
                return false;
            return true;
        } else {
            return false;
        }
    default:
            return true;
    }
}

uint32_t ExynosMPPModule::getSrcMaxCropSize(struct exynos_image &src)
{
    if ((mPhysicalType == MPP_DPP_VGRFS) &&
        (src.transform & HAL_TRANSFORM_ROT_90))
        return MAX_DPP_ROT_SRC_SIZE;
    else
        return ExynosMPP::getSrcMaxCropSize(src);
}
