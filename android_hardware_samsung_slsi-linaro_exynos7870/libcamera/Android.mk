# Copyright (C) 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

ifeq ($(TARGET_SOC), exynos7870)
    BOARD_CAMERA_GED_FEATURE := true
else
    BOARD_CAMERA_GED_FEATURE := false
endif

LOCAL_PROPRIETARY_MODULE := true
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES:= libutils libcutils libbinder liblog libcamera_metadata_helper libhardware libui
LOCAL_SHARED_LIBRARIES += libexynosutils libhwjpeg libexynosv4l2 libexynosgscaler libion libcsc
LOCAL_SHARED_LIBRARIES += libexpat libc++ libpower libgui_vendor libsensorlistener

# Sony specific StainKiller feature
ifeq ($(BOARD_CAMERA_STAINKILLER_FEATURE), true)
    LOCAL_CFLAGS += -DSTAINKILLER_FEATURE
    LOCAL_SHARED_LIBRARIES += libstainkiller
endif

LOCAL_CFLAGS += -DGAIA_FW_BETA
LOCAL_CFLAGS += -DMAIN_CAMERA_SENSOR_NAME=$(BOARD_BACK_CAMERA_SENSOR)
LOCAL_CFLAGS += -DFRONT_CAMERA_SENSOR_NAME=$(BOARD_FRONT_CAMERA_SENSOR)
LOCAL_CFLAGS += -DUSE_CAMERA_ESD_RESET
LOCAL_CFLAGS += -DBACK_ROTATION=$(BOARD_BACK_CAMERA_ROTATION)
LOCAL_CFLAGS += -DFRONT_ROTATION=$(BOARD_FRONT_CAMERA_ROTATION)
LOCAL_CFLAGS += -DSECURE_CAMERA_SENSOR_NAME=$(BOARD_SECURE_CAMERA_SENSOR)

LOCAL_CFLAGS += -DSENSOR_NAME_GET_FROM_FILE

ifeq ($(BOARD_CAMERA_GED_FEATURE), true)
    LOCAL_CFLAGS += -DCAMERA_GED_FEATURE
endif

# Optional HAL1 vendor QuickShot parameter support.
ifeq ($(BOARD_CAMERA_QUICKSHOT_SUPPORT), true)
    LOCAL_CFLAGS += -DSAMSUNG_QUICKSHOT
endif

LOCAL_CFLAGS += -D$(shell echo $(TARGET_DEVICE) | tr a-z A-Z)_CAMERA

LOCAL_CFLAGS += -Wno-implicit-fallthrough
LOCAL_CFLAGS += -Wno-unused-variable
LOCAL_CFLAGS += -Wno-unused-parameter
LOCAL_CFLAGS += -Wno-overloaded-virtual
LOCAL_CFLAGS += -Wno-format
LOCAL_CFLAGS += -Wno-error=date-time
LOCAL_CFLAGS += -Wno-macro-redefined
LOCAL_CFLAGS += -Wno-tautological-compare
LOCAL_CFLAGS += -Wno-unused-private-field
LOCAL_CFLAGS += -Wno-unused-label
LOCAL_CFLAGS += -Wno-infinite-recursion

LOCAL_CFLAGS += -DUSE_LIB_ION_LEGACY

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/../include \
    $(LOCAL_PATH)/../libcamera \
    $(LOCAL_PATH)/../libcamera/SensorInfos \
    $(TOP)/system/media/camera/include \
    $(TOP)/system/core/libion/include \
    $(TOP)/system/core/libsync/include \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/kernel-$(TARGET_LINUX_KERNEL_VERSION)-headers/kernel-headers \
    $(TOP)/system/memory/libion/kernel-headers \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/34xx \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/34xx/hal1 \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2 \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/SensorInfos \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Pipes2 \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/MCPipes \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Activities \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Buffers \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Ged \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Sec \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/include \
    $(TOP)/hardware/samsung_slsi-linaro/exynos5/include \
    $(TOP)/hardware/samsung_slsi-linaro/graphics/base/libion/include \
    $(TOP)/hardware/samsung_slsi-linaro/$(TARGET_SOC)/include \
    $(TOP)/hardware/samsung_slsi-linaro/$(TARGET_BOARD_PLATFORM)/include \
    $(TOP)/hardware/samsung_slsi-linaro/$(TARGET_BOARD_PLATFORM)/libcamera \
    $(TOP)/hardware/libhardware/include \
    $(TOP)/hardware/libhardware_legacy/include/hardware_legacy \
    $(TOP)/vendor/samsung/feature/CscFeature/libsecnativefeature \
    $(TOP)/bionic \
    $(TOP)/external/expat/lib \
    $(TOP)/frameworks/av/include \
    $(TOP)/frameworks/av/camera/include \
    $(TOP)/frameworks/native/include \
    $(TOP)/hardware/camera/UniPlugin/include \
    $(TOP)/frameworks/native/headers/media_plugin/media/openmax

# Header libraries
LOCAL_HEADER_LIBRARIES += libcutils_headers libsystem_headers libhardware_headers libexynos_headers

# Rebased to common_v2 + 34xx sources for libexynoscamera
LOCAL_SRC_FILES := \
    ../../exynos/libcamera/common_v2/ExynosCameraFrame.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraMemory.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraFrameManager.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraUtils.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraNode.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraNodeJpegHAL.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraFrameSelector.cpp \
    ../../exynos/libcamera/common_v2/ExynosCamera1MetadataConverter.cpp \
    ../../exynos/libcamera/common_v2/SensorInfos/ExynosCameraSensorInfoBase.cpp \
    ../../exynos/libcamera/common_v2/MCPipes/ExynosCameraMCPipe.cpp \
    ../../exynos/libcamera/common_v2/Pipes2/ExynosCameraPipe.cpp \
    ../../exynos/libcamera/common_v2/Pipes2/ExynosCameraPipeFlite.cpp \
    ../../exynos/libcamera/common_v2/Pipes2/ExynosCameraPipeVRA.cpp \
    ../../exynos/libcamera/common_v2/Pipes2/ExynosCameraPipeGSC.cpp \
    ../../exynos/libcamera/common_v2/Pipes2/ExynosCameraPipeJpeg.cpp \
    ../../exynos/libcamera/common_v2/Buffers/ExynosCameraBufferManager.cpp \
    ../../exynos/libcamera/common_v2/Activities/ExynosCameraActivityBase.cpp \
    ../../exynos/libcamera/common_v2/Activities/ExynosCameraActivityAutofocus.cpp \
    ../../exynos/libcamera/common_v2/Activities/ExynosCameraActivityFlash.cpp \
    ../../exynos/libcamera/common_v2/Activities/ExynosCameraActivitySpecialCapture.cpp \
    ../../exynos/libcamera/common_v2/Activities/ExynosCameraActivityUCTL.cpp \
    ../../exynos/libcamera/common_v2/Ged/ExynosCameraActivityAutofocusVendor.cpp \
    ../../exynos/libcamera/common_v2/Ged/ExynosCameraActivityFlashVendor.cpp \
    ../../exynos/libcamera/common_v2/Ged/ExynosCameraFrameSelectorVendor.cpp \
    ../../exynos/libcamera/common_v2/Ged/ExynosJpegEncoderForCameraVendor.cpp \
    ../../exynos/libcamera/74xx/JpegEncoderForCamera/ExynosJpegEncoderForCamera.cpp \
    ../../exynos/libcamera/34xx/ExynosCameraUtilsModule.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCameraSizeControl.cpp \
    ../../exynos/libcamera/34xx/ExynosCameraActivityControl.cpp \
    ../../exynos/libcamera/34xx/ExynosCameraScalableSensor.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCamera.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCamera1Parameters.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCameraFrameFactory.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCameraFrameFactory3aaIspM2M.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCameraFrameFactory3aaIspM2MTpu.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCameraFrameFactory3aaIspOtf.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCameraFrameFactory3aaIspOtfTpu.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCameraFrameFactoryFront.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCameraFrameFactoryPreview.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCameraFrameFactoryVision.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCameraFrameReprocessingFactory.cpp \
    ../../exynos/libcamera/34xx/hal1/ExynosCameraFrameReprocessingFactoryNV21.cpp \
    ../../exynos/libcamera/34xx/hal1/Ged/ExynosCameraVendor.cpp \
    ../../exynos/libcamera/34xx/hal1/Ged/ExynosCamera1ParametersVendor.cpp

LOCAL_SRC_FILES += ../libcamera/SensorInfos/ExynosCameraSensorInfo.cpp

# Ensure local symbol resolution first inside the shared library
LOCAL_LDFLAGS := -Wl,-Bsymbolic

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_TARGET_ARCH := arm
LOCAL_MODULE := libexynoscamera

include $(TOP)/hardware/samsung_slsi-linaro/exynos/BoardConfigCFlags.mk
include $(BUILD_SHARED_LIBRARY)
