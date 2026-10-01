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

#################
# libexynoscamera3

include $(CLEAR_VARS)

BOARD_CAMERA_GED_FEATURE := true

LOCAL_PROPRIETARY_MODULE := true
LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libcutils \
    libbinder \
    liblog \
    libcamera_metadata_helper \
    libhardware \
    libui \
    libexynosutils \
    libhwjpeg \
    libexynosv4l2 \
    libexynosgscaler \
    libion \
    libcsc \
    libexpat \
    libc++ \
    libpower \
    libgui_vendor \
    libsensorlistener

ifeq ($(BOARD_CAMERA_STAINKILLER_FEATURE), true)
    LOCAL_CFLAGS += -DSTAINKILLER_FEATURE
    LOCAL_SHARED_LIBRARIES += libstainkiller
endif

ifeq ($(BOARD_CAMERA_SW_VDIS), true)
LOCAL_SHARED_LIBRARIES += libvdis
endif

# Compiler flags & warnings suppression
LOCAL_CFLAGS += \
    -Wno-implicit-fallthrough \
    -Wno-unused-parameter \
    -Wno-unused-variable \
    -Wno-format \
    -Wno-integer-overflow \
    -Wno-mismatched-tags \
    -Wno-unused-private-field \
    -Wno-unused-label \
    -Wno-error=date-time \
    -Wno-overloaded-virtual \
    -DUSE_LIB_ION_LEGACY \
    -DGAIA_FW_BETA \
    -DMAIN_CAMERA_SENSOR_NAME=$(BOARD_BACK_CAMERA_SENSOR) \
    -DFRONT_CAMERA_SENSOR_NAME=$(BOARD_FRONT_CAMERA_SENSOR) \
    -DUSE_CAMERA_ESD_RESET \
    -DBACK_ROTATION=$(BOARD_BACK_CAMERA_ROTATION) \
    -DFRONT_ROTATION=$(BOARD_FRONT_CAMERA_ROTATION) \
    -DSENSOR_NAME_GET_FROM_FILE \
    -DCAMERA_GED_FEATURE \
    -DUSE_CAMERA2_API_SUPPORT

LOCAL_CFLAGS += -Wno-tautological-compare
LOCAL_CFLAGS += -Wno-infinite-recursion

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/../include \
    $(LOCAL_PATH)/../libcamera3 \
    $(TOP)/system/media/camera/include \
    $(TOP)/system/core/libion/include \
    $(TOP)/system/core/libsync/include \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/kernel-$(TARGET_LINUX_KERNEL_VERSION)-headers/kernel-headers \
    $(TOP)/system/memory/libion/kernel-headers \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2 \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/SensorInfos \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Pipes2 \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/MCPipes \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Activities \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Buffers \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Ged \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/PostProcessing \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/SizeTables \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Sec \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/34xx \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/34xx/hal3 \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/include \
    $(TOP)/hardware/samsung_slsi-linaro/exynos5/include \
    $(TOP)/hardware/samsung_slsi-linaro/$(TARGET_SOC)/include \
    $(TOP)/hardware/samsung_slsi-linaro/$(TARGET_BOARD_PLATFORM)/include \
    $(TOP)/hardware/samsung_slsi-linaro/$(TARGET_BOARD_PLATFORM)/libcamera3 \
    $(TOP)/hardware/libhardware/include \
    $(TOP)/hardware/libhardware_legacy/include/hardware_legacy \
    $(TOP)/hardware/interfaces/camera/common/1.0/default/include \
    $(TOP)/vendor/samsung/feature/CscFeature/libsecnativefeature \
    $(TOP)/bionic \
    $(TOP)/external/expat/lib \
    $(TOP)/frameworks/native/include \
    $(TOP)/frameworks/native/libs/nativebase/include \
    $(TOP)/frameworks/native/libs/nativewindow/include \
    $(TOP)/frameworks/native/libs/arect/include \
    $(TOP)/frameworks/native/headers/media_plugin/media/openmax \
    $(TOP)/frameworks/av/include \
    $(TOP)/frameworks/av/include/camera \
    $(TOP)/frameworks/av/camera/include \
    $(TOP)/hardware/camera/SensorListener \
    $(TOP)/hardware/camera/UniPlugin/include

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../libcamera/SensorInfos

LOCAL_HEADER_LIBRARIES += libcutils_headers libsystem_headers libhardware_headers libexynos_headers

# Core 34xx + common_v2 Sources
LOCAL_SRC_FILES := \
    ../../exynos/libcamera/common_v2/ExynosCameraFrame.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraMemory.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraFrameManager.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraUtils.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraNode.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraNodeJpegHAL.cpp \
    ../../exynos/libcamera/common_v2/SensorInfos/ExynosCameraSensorInfoBase.cpp \
    ../../exynos/libcamera/common_v2/SensorInfos/ExynosCamera3SensorInfoBase.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraFrameSelector.cpp \
    ../../exynos/libcamera/common_v2/MCPipes/ExynosCameraMCPipe.cpp \
    ../../exynos/libcamera/common_v2/Pipes2/ExynosCameraPipe.cpp \
    ../../exynos/libcamera/common_v2/Pipes2/ExynosCameraPipeFlite.cpp \
    ../../exynos/libcamera/common_v2/Pipes2/ExynosCameraPipeVRA.cpp \
    ../../exynos/libcamera/common_v2/Pipes2/ExynosCameraPipeGSC.cpp \
    ../../exynos/libcamera/common_v2/Pipes2/ExynosCameraPipeJpeg.cpp \
    ../../exynos/libcamera/common_v2/Buffers/ExynosCameraBufferManager.cpp \
    ../../exynos/libcamera/common_v2/Buffers/ExynosCameraBufferLocker.cpp \
    ../../exynos/libcamera/common_v2/Activities/ExynosCameraActivityBase.cpp \
    ../../exynos/libcamera/common_v2/Activities/ExynosCameraActivityAutofocus.cpp \
    ../../exynos/libcamera/common_v2/Activities/ExynosCameraActivityFlash.cpp \
    ../../exynos/libcamera/common_v2/Activities/ExynosCameraActivitySpecialCapture.cpp \
    ../../exynos/libcamera/common_v2/Activities/ExynosCameraActivityUCTL.cpp \
    ../../exynos/libcamera/common_v2/Ged/ExynosCameraActivityAutofocusVendor.cpp \
    ../../exynos/libcamera/common_v2/Ged/ExynosCameraActivityFlashVendor.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraRequestManager.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraStreamManager.cpp \
    ../../exynos/libcamera/common_v2/ExynosCameraMetadataConverter.cpp \
    ../../exynos/libcamera/common_v2/Ged/ExynosCameraFrameSelectorVendor.cpp \
    ../../exynos/libcamera/common_v2/Ged/ExynosJpegEncoderForCameraVendor.cpp \
    ../../exynos/libcamera/74xx/JpegEncoderForCamera/ExynosJpegEncoderForCamera.cpp \
    ../../exynos/libcamera/34xx/ExynosCameraActivityControl.cpp \
    ../../exynos/libcamera/34xx/ExynosCameraScalableSensor.cpp \
    ../../exynos/libcamera/34xx/ExynosCameraUtilsModule.cpp

# HAL 3.0 34xx source implementation
LOCAL_SRC_FILES += \
    ../../exynos/libcamera/34xx/hal3/ExynosCameraSizeControl.cpp \
    ../../exynos/libcamera/34xx/hal3/ExynosCamera3.cpp \
    ../../exynos/libcamera/34xx/hal3/ExynosCamera3Parameters.cpp \
    ../../exynos/libcamera/34xx/hal3/ExynosCamera3FrameFactory.cpp \
    ../../exynos/libcamera/34xx/hal3/ExynosCamera3FrameFactoryPreview.cpp \
    ../../exynos/libcamera/34xx/hal3/ExynosCamera3FrameReprocessingFactory.cpp

LOCAL_SRC_FILES += ../libcamera/SensorInfos/ExynosCamera3SensorInfo.cpp

# Ensure local symbol resolution first inside the shared library
LOCAL_LDFLAGS := -Wl,-Bsymbolic

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_TARGET_ARCH := arm
LOCAL_MODULE := libexynoscamera3

include $(TOP)/hardware/samsung_slsi-linaro/exynos/BoardConfigCFlags.mk
include $(BUILD_SHARED_LIBRARY)
