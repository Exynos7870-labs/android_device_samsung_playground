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

#include <stdint.h>
#include <sys/types.h>

#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <utils/Timers.h>

#include <binder/Parcel.h>
#include <binder/IInterface.h>

#include "IExynosHWC.h"

namespace android {

enum {
    ADD_VIRTUAL_DISPLAY_DEVICE = 0,
    DESTROY_VIRTUAL_DISPLAY_DEVICE,
    SET_WFD_MODE,
    GET_WFD_MODE,
    GET_WFD_INFO,
    SET_SECURE_VDS_MODE,
    SET_WFD_OUTPUT_RESOLUTION,
    GET_WFD_OUTPUT_RESOLUTION,
    SET_PRESENTATION_MODE,
    GET_PRESENTATION_MODE,
    SET_VDS_GLES_FORMAT,
    HWC_CONTROL,
    SET_BOOT_FINISHED,
    SET_VIRTUAL_HPD,
    GET_EXTERNAL_DISPLAY_CONFIG,
    SET_EXTERNAL_DISPLAY_CONFIG,
    ENABLE_MPP,
    SET_EXTERNAL_VSYNC,
    GET_HDR_CAPABILITIES,
    GET_EXTERNAL_HDR_CAPA,
#if 0
    NOTIFY_PSR_EXIT,
#endif
    SET_HWC_DEBUG = 105,
    GET_HWC_DEBUG = 106,
    SET_HWC_FENCE_DEBUG = 107,
    GET_HWC_FENCE_DEBUG = 108,
};

class BpExynosHWCService : public BpInterface<IExynosHWCService> {
public:
    BpExynosHWCService(const sp<IBinder>& impl)
        : BpInterface<IExynosHWCService>(impl)
    {
    }

    virtual int addVirtualDisplayDevice()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        int result = remote()->transact(ADD_VIRTUAL_DISPLAY_DEVICE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual int destroyVirtualDisplayDevice()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        int result = remote()->transact(DESTROY_VIRTUAL_DISPLAY_DEVICE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual int setWFDMode(unsigned int mode)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(mode);
        int result = remote()->transact(SET_WFD_MODE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual int getWFDMode()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        int result = remote()->transact(GET_WFD_MODE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual int getWFDInfo(int32_t* state, int32_t* compositionType, int32_t* format,
        int64_t* usage, int32_t* width, int32_t* height)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        int result = remote()->transact(GET_WFD_INFO, data, &reply);
        *state = reply.readInt32();
        *compositionType = reply.readInt32();
        *format = reply.readInt32();
        *usage = reply.readInt64();
        *width = reply.readInt32();
        *height = reply.readInt32();
        return result;
    }

    virtual int setSecureVDSMode(unsigned int mode)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(mode);
        int result = remote()->transact(SET_SECURE_VDS_MODE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual int setWFDOutputResolution(unsigned int width, unsigned int height)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(width);
        data.writeInt32(height);
        int result = remote()->transact(SET_WFD_OUTPUT_RESOLUTION, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual void getWFDOutputResolution(unsigned int *width, unsigned int *height)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        remote()->transact(GET_WFD_OUTPUT_RESOLUTION, data, &reply);
        *width  = reply.readInt32();
        *height = reply.readInt32();
    }

    virtual void setPresentationMode(bool use)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(use);
        remote()->transact(SET_PRESENTATION_MODE, data, &reply);
    }

    virtual int getPresentationMode(void)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        int result = remote()->transact(GET_PRESENTATION_MODE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual int setVDSGlesFormat(int format)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(format);
        int result = remote()->transact(SET_VDS_GLES_FORMAT, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual int setVirtualHPD(unsigned int on)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(on);
        int result = remote()->transact(SET_VIRTUAL_HPD, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual int getExternalDisplayConfigs()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        int result = remote()->transact(GET_EXTERNAL_DISPLAY_CONFIG, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual int setExternalDisplayConfig(unsigned int index)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(index);
        int result = remote()->transact(SET_EXTERNAL_DISPLAY_CONFIG, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual int setExternalVsyncEnabled(unsigned int index)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(index);
        int result = remote()->transact(SET_EXTERNAL_VSYNC, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual int getExternalHdrCapabilities()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        int result = remote()->transact(GET_EXTERNAL_HDR_CAPA, data, &reply);
        if (result == NO_ERROR)
            result = reply.readInt32();
        else
            ALOGE("GET_EXTERNAL_HDR_CAPA transact error(%d)", result);

        return result;
    }

    virtual void setBootFinished()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        remote()->transact(SET_BOOT_FINISHED, data, &reply);
    }

    virtual void enableMPP(uint32_t physicalType, uint32_t physicalIndex, uint32_t logicalIndex, uint32_t enable)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(physicalType);
        data.writeInt32(physicalIndex);
        data.writeInt32(logicalIndex);
        data.writeInt32(enable);
        remote()->transact(ENABLE_MPP, data, &reply);
    }

    virtual void setHWCDebug(int debug)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(debug);
        remote()->transact(SET_HWC_DEBUG, data, &reply);
    }

    virtual uint32_t getHWCDebug()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        remote()->transact(GET_HWC_DEBUG, data, &reply);
        int debugFlag = reply.readInt32();
        return debugFlag;
    }

    virtual void setHWCFenceDebug(uint32_t fenceNum, uint32_t ipNum, uint32_t mode)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(fenceNum);
        data.writeInt32(ipNum);
        data.writeInt32(mode);
        remote()->transact(SET_HWC_FENCE_DEBUG, data, &reply);
    }

    virtual void getHWCFenceDebug()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        remote()->transact(GET_HWC_FENCE_DEBUG, data, &reply);
    }

    virtual int setHWCCtl(uint32_t display, uint32_t ctrl, int32_t val) {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(display);
        data.writeInt32(ctrl);
        data.writeInt32(val);
        int result = remote()->transact(HWC_CONTROL, data, &reply);
        result = reply.readInt32();
        return result;
    };

    virtual int getHdrCapabilities(uint32_t type, int32_t* outNum, std::vector<int32_t>* outTypes,
            float* maxLuminance, float* maxAverageLuminance, float* minLuminance)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        data.writeInt32(type);
        int status = *outNum;
        data.writeInt32(*outNum);

        remote()->transact(GET_HDR_CAPABILITIES, data, &reply);
        *outNum = data.readInt32();

        if (status >= 0)
            data.readInt32Vector(outTypes);

        *maxLuminance = data.readFloat();
        *maxAverageLuminance = data.readFloat();
        *minLuminance = data.readFloat();
        status = data.readInt32();

        return status;
    }

    /*
    virtual void notifyPSRExit()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IExynosHWCService::getInterfaceDescriptor());
        remote()->transact(NOTIFY_PSR_EXIT, data, &reply);
    }
    */
};

IMPLEMENT_META_INTERFACE(ExynosHWCService, "android.hal.ExynosHWCService");

status_t BnExynosHWCService::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    switch(code) {
        case ADD_VIRTUAL_DISPLAY_DEVICE: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int res = addVirtualDisplayDevice();
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case DESTROY_VIRTUAL_DISPLAY_DEVICE: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int res = destroyVirtualDisplayDevice();
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_WFD_MODE: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int mode = data.readInt32();
            int res = setWFDMode(mode);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case GET_WFD_MODE: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int res = getWFDMode();
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case GET_WFD_INFO: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int32_t state, compositionType, format, width, height;
            int64_t usage;
            int res = getWFDInfo(&state, &compositionType, &format, &usage, &width, &height);
            reply->writeInt32(state);
            reply->writeInt32(compositionType);
            reply->writeInt32(format);
            reply->writeInt64(usage);
            reply->writeInt32(width);
            reply->writeInt32(height);
            return res;
        } break;
        case SET_SECURE_VDS_MODE: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int mode = data.readInt32();
            int res = setSecureVDSMode(mode);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_WFD_OUTPUT_RESOLUTION: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int width  = data.readInt32();
            int height = data.readInt32();
            int res = setWFDOutputResolution(width, height);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case GET_WFD_OUTPUT_RESOLUTION: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            uint32_t width, height;
            getWFDOutputResolution(&width, &height);
            reply->writeInt32(width);
            reply->writeInt32(height);
            return NO_ERROR;
        } break;
        case SET_PRESENTATION_MODE: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int use = data.readInt32();
            setPresentationMode(use);
            return NO_ERROR;
        } break;
        case GET_PRESENTATION_MODE: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int res = getPresentationMode();
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_VDS_GLES_FORMAT: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int format  = data.readInt32();
            int res = setVDSGlesFormat(format);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
       case HWC_CONTROL: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int display = data.readInt32();
            int ctrl = data.readInt32();
            int value = data.readInt32();
            int res = setHWCCtl(display, ctrl, value);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_VIRTUAL_HPD: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int on = data.readInt32();
            int res = setVirtualHPD(on);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case GET_EXTERNAL_DISPLAY_CONFIG: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int res = getExternalDisplayConfigs();
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_EXTERNAL_DISPLAY_CONFIG: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int index = data.readInt32();
            int res = setExternalDisplayConfig(index);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_EXTERNAL_VSYNC: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int index = data.readInt32();
            int res = setExternalVsyncEnabled(index);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case GET_EXTERNAL_HDR_CAPA: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int res = getExternalHdrCapabilities();
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_BOOT_FINISHED: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            setBootFinished();
            return NO_ERROR;
        } break;
        case ENABLE_MPP: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            uint32_t type = data.readInt32();
            uint32_t physicalIdx = data.readInt32();
            uint32_t logicalIdx = data.readInt32();
            uint32_t enable = data.readInt32();
            enableMPP(type, physicalIdx, logicalIdx, enable);
            return NO_ERROR;
        } break;
        case SET_HWC_DEBUG: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int debug = data.readInt32();
            setHWCDebug(debug);
            reply->writeInt32(debug);
            return NO_ERROR;
        } break;
        case SET_HWC_FENCE_DEBUG: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            uint32_t fenceNum = data.readInt32();
            uint32_t ipNum = data.readInt32();
            uint32_t mode = data.readInt32();
            setHWCFenceDebug(fenceNum, ipNum, mode);
            return NO_ERROR;
        } break;
        case GET_HWC_DEBUG: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int debugFlag = getHWCDebug();
            reply->writeInt32(debugFlag);
            return NO_ERROR;
        } break;
        case GET_HDR_CAPABILITIES: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int type = data.readInt32();
            int status = data.readInt32();
            int outNum, ret;
            float maxLuminance, maxAverageLuminance, minLuminance;
            if (status == -1) {
                ret = getHdrCapabilities(type, &outNum, NULL, &maxLuminance,
                        &maxAverageLuminance, &minLuminance);
                reply->writeInt32(outNum);
                reply->writeFloat(maxLuminance);
                reply->writeFloat(maxAverageLuminance);
                reply->writeFloat(minLuminance);
                reply->writeInt32(ret);
                return NO_ERROR;
            } else {
                std::vector<int32_t> tmpTypes(status);
                ret = getHdrCapabilities(type, &outNum, &tmpTypes, &maxLuminance,
                        &maxAverageLuminance, &minLuminance);
                reply->writeInt32(outNum);
                reply->writeInt32Vector(tmpTypes);
                reply->writeFloat(maxLuminance);
                reply->writeFloat(maxAverageLuminance);
                reply->writeFloat(minLuminance);
                reply->writeInt32(ret);
                return NO_ERROR;
            }
        } break;

#if 0
        case SET_HWC_CTL_MAX_OVLY_CNT: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int val = data.readInt32();
            setHWCCtl(SET_HWC_CTL_MAX_OVLY_CNT, val);
            return NO_ERROR;
        } break;
        case SET_HWC_CTL_VIDEO_OVLY_CNT: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int val = data.readInt32();
            setHWCCtl(SET_HWC_CTL_VIDEO_OVLY_CNT, val);
            return NO_ERROR;
        } break;
         case SET_HWC_CTL_DYNAMIC_RECOMP: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int val = data.readInt32();
            setHWCCtl(SET_HWC_CTL_DYNAMIC_RECOMP, val);
            return NO_ERROR;
        } break;
        case SET_HWC_CTL_SKIP_STATIC: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int val = data.readInt32();
            setHWCCtl(SET_HWC_CTL_SKIP_STATIC, val);
            return NO_ERROR;
        } break;
        case SET_HWC_CTL_SECURE_DMA: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            int val = data.readInt32();
            setHWCCtl(SET_HWC_CTL_SECURE_DMA, val);
            return NO_ERROR;
        } break;
        case NOTIFY_PSR_EXIT: {
            CHECK_INTERFACE(IExynosHWCService, data, reply);
            notifyPSRExit();
            return NO_ERROR;
        }
#endif
        default:
            return BBinder::onTransact(code, data, reply, flags);
    }
}
}
