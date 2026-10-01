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
#define LOG_TAG "ExynosCamera1SensorInfo"
#include <cutils/log.h>

#include "ExynosCameraSensorInfo.h"

namespace android {
struct ExynosSensorInfoBase *createExynosCamera1SensorInfo(int camId)
{
    struct ExynosSensorInfoBase *sensorInfo = NULL;
    int sensorName = getSensorId(camId);
    if (sensorName < 0) {
        ALOGE("ERR(%s[%d]): Inavalid camId, sensor name is nothing", __FUNCTION__, __LINE__);
        sensorName = SENSOR_NAME_NOTHING;
    }
    ALOGI("INFO(%s[%d]):sensor ID(%d)", __FUNCTION__, __LINE__, sensorName);
    switch (sensorName) {
    case SENSOR_NAME_S5K5E2:
        sensorInfo = new ExynosSensorS5K5E2();
        break;
    case SENSOR_NAME_S5K3M2:
        sensorInfo = new ExynosSensorS5K3M2();
        break;
    case SENSOR_NAME_S5K3L2:
        sensorInfo = new ExynosSensorS5K3L2();
        break;
    case SENSOR_NAME_S5K5E3:
        sensorInfo = new ExynosSensorS5K5E3();
        break;
    case SENSOR_NAME_SR544:
        sensorInfo = new ExynosSensorSR544();
        break;
    case SENSOR_NAME_S5K4H5YC:
        sensorInfo = new ExynosSensorS5K4H5YC();
        break;
    case SENSOR_NAME_IMX258:
        sensorInfo = new ExynosSensorIMX258();
        break;
    case SENSOR_NAME_IMX219:
        sensorInfo = new ExynosSensorIMX219();
        break;
    case SENSOR_NAME_S5K3M3:
        /* ExynosSensorS5K3M3Base's usable constructor requires cameraId. */
        sensorInfo = new ExynosSensorS5K3M3();
        break;
    default:
        android_printAssert(NULL, LOG_TAG, "ASSERT(%s[%d]):Unknown sensor(%d), create default sensor, assert!!!!",
            __FUNCTION__, __LINE__, camId);
        break;
    }

#ifdef CALIBRATE_BCROP5_SIZE
    if (sensorInfo->sizeTableSupport) {
        LutType lut[4];
        int maxLut[4];
        lut[0] = sensorInfo->previewSizeLut;
        maxLut[0] = sensorInfo->previewSizeLutMax;
        lut[1] = sensorInfo->videoSizeLut;
        maxLut[1] = sensorInfo->videoSizeLutMax;
        lut[2] = sensorInfo->pictureSizeLut;
        maxLut[2] = sensorInfo->pictureSizeLutMax;
        lut[3] = sensorInfo->videoSizeLutHighSpeed;
        maxLut[3] = 3;

        LutType curLut;
        int max = 0;
        int calc = 0;
        int offset = 0;
        for(int k = 0  ; k < 4 ; k++ ) {
            curLut = lut[k];
            max = maxLut[k];
            for (int i = 0; i < max ; i++) {
                calc = 0;
                ALOGD("DEBUG(%s:%d):SRC LUT BCROP(%d x %d)", __FUNCTION__, __LINE__, curLut[i][BCROP_W], curLut[i][BCROP_H]);
                ALOGE("DEBUG(%s:%d):SRC LUT BDS  (%d x %d)", __FUNCTION__, __LINE__, curLut[i][BDS_W], curLut[i][BDS_H]);
                calc = calibrateB3(curLut[i][BCROP_W], curLut[i][BDS_W]);
                offset = calc-curLut[i][BDS_W];
                curLut[i][BNS_W] += offset;
                curLut[i][BCROP_W] += offset;
                ALOGE("DEBUG(%s:%d): W offset(%d) ", __FUNCTION__, __LINE__, offset);
                calc = calibrateB3(curLut[i][BCROP_H], curLut[i][BDS_H]);
                offset = calc-curLut[i][BDS_H];
                ALOGE("DEBUG(%s:%d): H offset(%d) ", __FUNCTION__, __LINE__, offset);
                curLut[i][BNS_H] += offset;
                curLut[i][BCROP_H] += offset;
                ALOGE("DEBUG(%s:%d):DST LUT BNS(%d x %d)", __FUNCTION__, __LINE__, curLut[i][BNS_W], curLut[i][BNS_H]);
                ALOGE("DEBUG(%s:%d):DST LUT BCROP(%d x %d)", __FUNCTION__, __LINE__, curLut[i][BCROP_W], curLut[i][BCROP_H]);
            }
        }
        ALOGW("WRN(%s[%d]): Calibrate BDB size", __FUNCTION__, __LINE__);
    }else {
        ALOGW("WRN(%s[%d]): Not use Calibrate BDB size", __FUNCTION__, __LINE__);
    }
#endif

    return sensorInfo;
}

ExynosSensorS5K3M2::ExynosSensorS5K3M2()
{
    effectList =
          EFFECT_NONE
        | EFFECT_MONO
        | EFFECT_NEGATIVE
        /* | EFFECT_SOLARIZE */
        | EFFECT_SEPIA
        | EFFECT_POSTERIZE
        | EFFECT_COLD_VINTAGE
        | EFFECT_BLUE
        | EFFECT_RED_YELLOW
        | EFFECT_AQUA
        /* | EFFECT_WHITEBOARD */
        /* | EFFECT_BLACKBOARD */
        ;
};

ExynosSensorS5K5E2::ExynosSensorS5K5E2()
{
    effectList =
          EFFECT_NONE
        | EFFECT_MONO
        | EFFECT_NEGATIVE
        /* | EFFECT_SOLARIZE */
        | EFFECT_SEPIA
        | EFFECT_POSTERIZE
        | EFFECT_COLD_VINTAGE
        | EFFECT_BLUE
        | EFFECT_RED_YELLOW
        | EFFECT_AQUA
        /* | EFFECT_WHITEBOARD */
        /* | EFFECT_BLACKBOARD */
        ;
};

ExynosSensorS5K3L2::ExynosSensorS5K3L2()
{
    effectList =
          EFFECT_NONE
        | EFFECT_MONO
        | EFFECT_NEGATIVE
        | EFFECT_SEPIA
        | EFFECT_POSTERIZE
        | EFFECT_COLD_VINTAGE
        | EFFECT_BLUE
        | EFFECT_RED_YELLOW
        | EFFECT_AQUA;
};

ExynosSensorS5K5E3::ExynosSensorS5K5E3()
{
    effectList =
          EFFECT_NONE
        | EFFECT_MONO
        | EFFECT_NEGATIVE
        | EFFECT_SEPIA
        | EFFECT_POSTERIZE
        | EFFECT_COLD_VINTAGE
        | EFFECT_BLUE
        | EFFECT_RED_YELLOW
        | EFFECT_AQUA;
};

ExynosSensorSR544::ExynosSensorSR544()
{
    effectList =
          EFFECT_NONE
        | EFFECT_MONO
        | EFFECT_NEGATIVE
        | EFFECT_SEPIA
        | EFFECT_POSTERIZE
        | EFFECT_COLD_VINTAGE
        | EFFECT_BLUE
        | EFFECT_RED_YELLOW
        | EFFECT_AQUA;
};


ExynosSensorS5K4H5YC::ExynosSensorS5K4H5YC()
{
    effectList =
          EFFECT_NONE
        | EFFECT_MONO
        | EFFECT_NEGATIVE
        | EFFECT_SEPIA
        | EFFECT_POSTERIZE
        | EFFECT_COLD_VINTAGE
        | EFFECT_BLUE
        | EFFECT_RED_YELLOW
        | EFFECT_AQUA;
};

//ExynosSensorS5K3P8SP::ExynosSensorS5K3P8SP()
//{
//    effectList =
//          EFFECT_NONE
//        | EFFECT_MONO
//        | EFFECT_NEGATIVE
//        | EFFECT_SEPIA
//        | EFFECT_POSTERIZE
//        | EFFECT_COLD_VINTAGE
//        | EFFECT_BLUE
//        | EFFECT_RED_YELLOW
//        | EFFECT_AQUA;
//};

ExynosSensorIMX258::ExynosSensorIMX258()
{
    effectList =
          EFFECT_NONE
        | EFFECT_MONO
        | EFFECT_NEGATIVE
        | EFFECT_SEPIA
        | EFFECT_POSTERIZE
        | EFFECT_COLD_VINTAGE
        | EFFECT_BLUE
        | EFFECT_RED_YELLOW
        | EFFECT_AQUA;
};

ExynosSensorIMX219::ExynosSensorIMX219()
{
    effectList =
          EFFECT_NONE
        | EFFECT_MONO
        | EFFECT_NEGATIVE
        | EFFECT_SEPIA
        | EFFECT_POSTERIZE
        | EFFECT_COLD_VINTAGE
        | EFFECT_BLUE
        | EFFECT_RED_YELLOW
        | EFFECT_AQUA;

    flashModeList = FLASH_MODE_OFF;
    focusModeList = FOCUS_MODE_INFINITY | FOCUS_MODE_FIXED;
    maxNumFocusAreas = 0;

    ALOGI("INFO(%s[%d]):IMX219 front HAL1 wrapper ready (3280x2458, fixed-focus, no flash)",
            __FUNCTION__, __LINE__);
};

ExynosSensorS5K3M3::ExynosSensorS5K3M3()
    : ExynosSensorS5K3M3Base()
{
    effectList =
          EFFECT_NONE
        | EFFECT_MONO
        | EFFECT_NEGATIVE
        | EFFECT_SEPIA
        | EFFECT_POSTERIZE
        | EFFECT_COLD_VINTAGE
        | EFFECT_BLUE
        | EFFECT_RED_YELLOW
        | EFFECT_AQUA;
};


}; /* namespace android */
