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
# camera.exynos7870.so

include $(CLEAR_VARS)

BOARD_CAMERA_GED_FEATURE := true

# HAL module implemenation stored in
# hw/<COPYPIX_HARDWARE_MODULE_ID>.<ro.product.board>.so
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true

LOCAL_C_INCLUDES += \
	$(TOP)/system/media/camera/include \
	$(TOP)/system/core/libsync/include \
	$(TOP)/system/memory/libion/kernel-headers \
	$(TOP)/hardware/samsung_slsi-linaro/$(TARGET_SOC)/libcamera \
	$(TOP)/hardware/samsung_slsi-linaro/$(TARGET_SOC)/libcamera3 \
	$(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/34xx \
	$(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/34xx/hal1\
	$(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/34xx/hal3\
	$(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2 \
	$(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Activities \
	$(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Buffers \
	$(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/MCPipes \
	$(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Pipes2 \
	$(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/PostProcessing \
	$(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/SensorInfos \
	$(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/SizeTables \
	$(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Sec \
    $(TOP)/hardware/samsung_slsi-linaro/exynos/libcamera/common_v2/Sec/PPUniPlugin \
	$(TOP)/hardware/samsung_slsi-linaro/exynos/include \
	$(TOP)/hardware/samsung_slsi-linaro/exynos5/include \
	$(TOP)/hardware/samsung_slsi-linaro/$(TARGET_SOC)/include \
	$(TOP)/hardware/interfaces/camera/common/1.0/default/include \
	$(TOP)/bionic \
	$(TOP)/frameworks/native/include \
	$(TOP)/frameworks/native/libs/nativebase/include \
	$(TOP)/frameworks/native/libs/nativewindow/include \
    $(TOP)/frameworks/native/libs/arect/include \
	$(TOP)/frameworks/av/camera/include \
    $(TOP)/frameworks/av/include/camera \
    $(TOP)/frameworks/av/include \
    $(TOP)/system/media/camera/include \
	$(TOP)/frameworks/native/headers/media_plugin/media/openmax/


LOCAL_C_INCLUDES += $(LOCAL_PATH)/../libcamera/SensorInfos

ifeq ($(BOARD_CAMERA_HAL3_FEATURE), true)
LOCAL_SRC_FILES:= \
	../../exynos/libcamera/common_v2/ExynosCamera3Interface.cpp
else
LOCAL_SRC_FILES:= \
	../../exynos/libcamera/common_v2/ExynosCameraInterface.cpp
endif

# misc
LOCAL_CFLAGS += -Wno-gnu-designator
LOCAL_CFLAGS += -Wno-braced-scalar-init
LOCAL_CFLAGS += -Wno-format
LOCAL_CFLAGS += -Wno-unused-parameter
LOCAL_CFLAGS += -Wno-unused-function
LOCAL_CFLAGS += -Wno-macro-redefined
LOCAL_CFLAGS += -Wno-tautological-compare
LOCAL_CFLAGS += -Wno-unused-private-field
LOCAL_CFLAGS += -Wno-unused-label
LOCAL_CFLAGS += -Wno-infinite-recursion

LOCAL_CFLAGS += -Wno-error=date-time
LOCAL_CFLAGS += -Wno-overloaded-virtual
LOCAL_CFLAGS += -DMAIN_CAMERA_SENSOR_NAME=$(BOARD_BACK_CAMERA_SENSOR)
LOCAL_CFLAGS += -DFRONT_CAMERA_SENSOR_NAME=$(BOARD_FRONT_CAMERA_SENSOR)
LOCAL_CFLAGS += -DSECURE_CAMERA_SENSOR_NAME=$(BOARD_SECURE_CAMERA_SENSOR)
LOCAL_CFLAGS += -DBACK_ROTATION=$(BOARD_BACK_CAMERA_ROTATION)
LOCAL_CFLAGS += -DFRONT_ROTATION=$(BOARD_FRONT_CAMERA_ROTATION)
LOCAL_CFLAGS += -DSECURE_ROTATION=$(BOARD_SECURE_CAMERA_ROTATION)

ifeq ($(BOARD_CAMERA_GED_FEATURE), true)
LOCAL_CFLAGS += -DCAMERA_GED_FEATURE
endif
ifeq ($(BOARD_CAMERA_HAL3_FEATURE), true)
LOCAL_CFLAGS += -DUSE_CAMERA2_API_SUPPORT
endif

LOCAL_SHARED_LIBRARIES:= liblog libhardware libion_exynos libhwjpeg libbinder libui
LOCAL_SHARED_LIBRARIES += libutils
LOCAL_SHARED_LIBRARIES += libcutils
LOCAL_SHARED_LIBRARIES += libcamera_metadata_helper
LOCAL_SHARED_LIBRARIES += libexynosutils
LOCAL_SHARED_LIBRARIES += libexynosv4l2
LOCAL_SHARED_LIBRARIES += libcsc
LOCAL_SHARED_LIBRARIES += libion
LOCAL_SHARED_LIBRARIES += libcamera_metadata
LOCAL_SHARED_LIBRARIES += libexynoscamera
LOCAL_SHARED_LIBRARIES += android.hardware.graphics.allocator@2.0
LOCAL_SHARED_LIBRARIES += android.hardware.graphics.mapper@2.0
LOCAL_SHARED_LIBRARIES += libGrallocWrapper
LOCAL_SHARED_LIBRARIES += libgui_vendor

LOCAL_HEADER_LIBRARIES += \
    libnativebase_headers \
	libcamera_metadata_headers \
	libexynos_headers

ifeq ($(BOARD_CAMERA_USES_DUAL_CAMERA), true)
LOCAL_CFLAGS += -DBOARD_CAMERA_USES_DUAL_CAMERA
LOCAL_CFLAGS += -DMAIN_1_CAMERA_SENSOR_NAME=$(BOARD_BACK_1_CAMERA_SENSOR)
LOCAL_CFLAGS += -DFRONT_1_CAMERA_SENSOR_NAME=$(BOARD_FRONT_1_CAMERA_SENSOR)
endif

ifeq ($(BOARD_CAMERA_HAL3_FEATURE), true)
LOCAL_SHARED_LIBRARIES += libexynoscamera3
endif

ifeq ($(BOARD_SECURE_CAMERA_SUPPORT), true)
LOCAL_CFLAGS += -DBOARD_SECURE_CAMERA_SUPPORT
endif

ifeq ($(BOARD_CAMERA_SAMSUNG_TN_FEATURE), true)
LOCAL_CFLAGS += -DSAMSUNG_TN_FEATURE
endif

ifeq ($(BOARD_CAMERA_QUICKSHOT_SUPPORT), true)
LOCAL_CFLAGS += -DSAMSUNG_QUICKSHOT
endif

$(foreach file,$(LOCAL_SRC_FILES),$(shell touch '$(LOCAL_PATH)/$(file)'))

LOCAL_MODULE := camera.vendor.$(TARGET_SOC)
LOCAL_MODULE_TARGET_ARCH:= arm
LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS += -D$(shell echo $(project_camera) | tr a-z A-Z)_CAMERA

include $(TOP)/hardware/samsung_slsi-linaro/exynos/BoardConfigCFlags.mk
include $(BUILD_SHARED_LIBRARY)

$(warning #####################################)
$(warning ########    libcamera I/F    ########)
$(warning #####################################)
