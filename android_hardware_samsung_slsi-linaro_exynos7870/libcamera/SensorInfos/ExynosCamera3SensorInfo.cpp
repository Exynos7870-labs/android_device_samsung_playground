/*
**
** Copyright 2015, Samsung Electronics Co. LTD
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

/*#define LOG_NDEBUG 0 */
#define LOG_TAG "ExynosCamera3SensorInfo"
#include <cutils/log.h>

#include "ExynosCamera3SensorInfo.h"

namespace android {

struct ExynosSensorInfoBase *createExynosCamera3SensorInfo(int camId)
{
    struct ExynosSensorInfoBase *sensorInfo = NULL;
    int sensorId = getSensorId(camId);
    if (sensorId < 0) {
        ALOGE("ERR(%s[%d]): Inavalid camId, sensor name is nothing", __FUNCTION__, __LINE__);
        sensorId = SENSOR_NAME_NOTHING;
    }

    switch (sensorId) {
    case SENSOR_NAME_S5K3L2:
        sensorInfo = new ExynosCamera3SensorS5K3L2();
        break;
    case SENSOR_NAME_S5K5E2:
        sensorInfo = new ExynosCamera3SensorS5K5E2();
        break;
    case SENSOR_NAME_S5K5E3:
        sensorInfo = new ExynosCamera3SensorS5K5E3();
        break;
    case SENSOR_NAME_S5K3P3:
        sensorInfo = new ExynosCamera3SensorS5K3P3();
        break;
    case SENSOR_NAME_SR544:
        sensorInfo = new ExynosCamera3SensorSR544();
        break;
    case SENSOR_NAME_S5K4H5YC:
        sensorInfo = new ExynosCamera3SensorS5K4H5YC();
        break;
//    case SENSOR_NAME_S5K3P8SP:
//        sensorInfo = new ExynosCamera3SensorS5K3P8SP(camId);
//        break;
    case SENSOR_NAME_IMX258:
        sensorInfo = new ExynosCamera3SensorIMX258();
        break;
    case SENSOR_NAME_IMX219:
        sensorInfo = new ExynosCamera3SensorIMX219();
        break;
    case SENSOR_NAME_S5K3M3:
        sensorInfo = new ExynosCamera3SensorS5K3M3(camId);
        break;
    default:
        android_printAssert(NULL, LOG_TAG, "ASSERT(%s[%d]):Unknown sensor(%d), create default sensor, assert!!!!",
            __FUNCTION__, __LINE__, camId);
        break;
    }

    return sensorInfo;
}

// Existing concrete sensor classes
ExynosCamera3SensorS5K3L2::ExynosCamera3SensorS5K3L2() : ExynosCamera3SensorS5K3L2Base()
{
    // hyperFocalDistance = 1.0f / 3.6f;
}

ExynosCamera3SensorS5K5E2::ExynosCamera3SensorS5K5E2() : ExynosCamera3SensorS5K5E2Base()
{
}

ExynosCamera3SensorS5K5E3::ExynosCamera3SensorS5K5E3() : ExynosCamera3SensorS5K5E3Base()
{
}

ExynosCamera3SensorS5K3P3::ExynosCamera3SensorS5K3P3() : ExynosCamera3SensorS5K3P3Base()
{
}

ExynosCamera3SensorSR544::ExynosCamera3SensorSR544() : ExynosCamera3SensorSR544Base()
{
}

ExynosCamera3SensorS5K4H5YC::ExynosCamera3SensorS5K4H5YC() : ExynosCamera3SensorS5K4H5YCBase()
{
}

//ExynosCamera3SensorS5K3P8SP::ExynosCamera3SensorS5K3P8SP(int cameraId) : ExynosCamera3SensorS5K3P8SPBase(cameraId)
//{
//}

ExynosCamera3SensorIMX258::ExynosCamera3SensorIMX258() : ExynosCamera3SensorIMX258Base()
{
}

ExynosCamera3SensorIMX219::ExynosCamera3SensorIMX219() : ExynosCamera3SensorIMX219Base()
{
    
    /*
     * A3Y17 IMX219 front rotation override
     */
    orientation = FRONT_ROTATION;

    /* A3Y17 front IMX219: no AF actuator, no flash. Kernel modes:
     * 3280x2458@30, 3280x1846@30, 1640x924@60/15, 1640x1228@60/30/15/7,
     * 816x604@118, 816x460@120. Only last is advertised as 120 fps. */
    flashAvailable = ANDROID_FLASH_INFO_AVAILABLE_FALSE;
    flashModeList = FLASH_MODE_OFF;
    focusModeList = FOCUS_MODE_INFINITY | FOCUS_MODE_FIXED;
    maxNumFocusAreas = 0;
    max3aRegions[AF] = 0;
    minimumFocusDistance = 0.0f;

    videoSizeLutHighSpeed60Max  = sizeof(VIDEO_SIZE_LUT_60FPS_HIGH_SPEED_IMX219) / (sizeof(int) * SIZE_OF_LUT);
    videoSizeLutHighSpeed120Max = sizeof(VIDEO_SIZE_LUT_120FPS_HIGH_SPEED_IMX219) / (sizeof(int) * SIZE_OF_LUT);
    videoSizeLutHighSpeed60     = VIDEO_SIZE_LUT_60FPS_HIGH_SPEED_IMX219;
    videoSizeLutHighSpeed120    = VIDEO_SIZE_LUT_120FPS_HIGH_SPEED_IMX219;

    ALOGI("INFO(%s[%d]):IMX219 front HAL3 wrapper ready (3280x2458, fixed-focus, no flash)",
            __FUNCTION__, __LINE__);
}

ExynosCamera3SensorS5K3M3::ExynosCamera3SensorS5K3M3(int cameraId) : ExynosCamera3SensorS5K3M3Base(cameraId)
{
}

};
