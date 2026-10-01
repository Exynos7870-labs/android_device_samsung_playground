# Copyright (C) 2012 The Android Open Source Project
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
# HAL module implemenation, not prelinked and stored in
# hw/<COPYPIX_HARDWARE_MODULE_ID>.<ro.product.board>.so

include $(CLEAR_VARS)

ifndef TARGET_SOC_BASE
	TARGET_SOC_BASE := $(TARGET_SOC)
endif

LOCAL_SHARED_LIBRARIES := liblog libcutils libhardware \
	libhardware_legacy libutils libmpp libsync libacryl libui libion
LOCAL_HEADER_LIBRARIES := libhardware_legacy_headers libbinder_headers libexynos_headers

ifeq ($(BOARD_USES_EXYNOS_GRALLOC_VERSION), 1)
LOCAL_SHARED_LIBRARIES += \
    android.hardware.graphics.allocator@2.0 \
    android.hardware.graphics.mapper@2.0 \
    libGrallocWrapper
endif
LOCAL_PROPRIETARY_MODULE := true

LOCAL_C_INCLUDES += \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/include \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libdevice \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libmaindisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libexternaldisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libvirtualdisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libhwchelper \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libresource \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2 \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libmaindisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libexternaldisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libvirtualdisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libresource \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libdevice \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libresource \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libhwcService

LOCAL_SRC_FILES := \
	ExynosHWC1Adaptor.cpp \
	libhwchelper/ExynosHWCHelper.cpp \
	libdevice/ExynosDisplay.cpp \
	libdevice/ExynosDevice.cpp \
	libdevice/ExynosLayer.cpp \
	libmaindisplay/ExynosPrimaryDisplay.cpp \
	libresource/ExynosMPP.cpp \
	libresource/ExynosResourceManager.cpp \
	libexternaldisplay/ExynosExternalDisplay.cpp \
	libvirtualdisplay/ExynosVirtualDisplay.cpp \
	libexternaldisplay/dv_timings.c \
	ExynosHWCDebug.cpp

include $(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/Android.mk

LOCAL_CFLAGS := -DHLOG_CODE=0
LOCAL_CFLAGS += -DLOG_TAG=\"display\"
LOCAL_CFLAGS += -Wno-unused-parameter

LOCAL_MODULE := libexynosdisplay
LOCAL_MODULE_TAGS := optional

include $(TOP)/hardware/samsung_slsi-linaro/graphics/base/BoardConfigCFlags.mk
include $(BUILD_SHARED_LIBRARY)

################################################################################

ifeq ($(BOARD_USES_HWC_SERVICES),true)

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := liblog libcutils libutils libbinder libexynosdisplay libui libmpp libacryl libion
LOCAL_HEADER_LIBRARIES := libhardware_legacy_headers libbinder_headers libexynos_headers
LOCAL_PROPRIETARY_MODULE := true

ifeq ($(BOARD_USES_EXYNOS_GRALLOC_VERSION), 1)
LOCAL_SHARED_LIBRARIES += \
    android.hardware.graphics.allocator@2.0 \
    android.hardware.graphics.mapper@2.0 \
    libGrallocWrapper
endif

LOCAL_C_INCLUDES += \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libdevice \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libmaindisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libexternaldisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libvirtualdisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libhwchelper \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libresource \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2 \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libmaindisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libexternaldisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libvirtualdisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libresource \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libdevice \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libresource \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libhwcService

LOCAL_CFLAGS := -DHLOG_CODE=0
LOCAL_CFLAGS += -DLOG_TAG=\"hwcservice\"

LOCAL_SRC_FILES := \
	libhwcService/IExynosHWC.cpp \
	libhwcService/ExynosHWCService.cpp

LOCAL_MODULE := libExynosHWCService
LOCAL_MODULE_TAGS := optional

include $(TOP)/hardware/samsung_slsi-linaro/graphics/base/BoardConfigCFlags.mk
include $(BUILD_SHARED_LIBRARY)

endif

################################################################################

include $(CLEAR_VARS)

LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SHARED_LIBRARIES := liblog libcutils libutils libexynosdisplay libmpp libion
LOCAL_HEADER_LIBRARIES := libhardware_legacy_headers libbinder_headers libexynos_headers
LOCAL_PROPRIETARY_MODULE := true

ifeq ($(BOARD_USES_EXYNOS_GRALLOC_VERSION), 1)
LOCAL_SHARED_LIBRARIES += \
    android.hardware.graphics.allocator@2.0 \
    android.hardware.graphics.mapper@2.0 \
    libGrallocWrapper
endif

LOCAL_CFLAGS := -DHLOG_CODE=0
LOCAL_CFLAGS += -DLOG_TAG=\"hwcomposer\"

ifeq ($(BOARD_USES_HWC_SERVICES),true)
LOCAL_CFLAGS += -DUSES_HWC_SERVICES
LOCAL_SHARED_LIBRARIES += libExynosHWCService
endif

LOCAL_C_INCLUDES += \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libdevice \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libmaindisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libexternaldisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libvirtualdisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libhwchelper \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libresource \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2 \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libmaindisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libexternaldisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libvirtualdisplay \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libresource \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libdevice \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/$(TARGET_SOC_BASE)/libhwc2/libresource \
	$(TOP)/hardware/samsung_slsi-linaro/graphics/base/libhwc2/libhwcService

LOCAL_SRC_FILES := \
	ExynosHWC.cpp

LOCAL_MODULE := hwcomposer.$(TARGET_BOOTLOADER_BOARD_NAME)
LOCAL_MODULE_TAGS := optional

include $(TOP)/hardware/samsung_slsi-linaro/graphics/base/BoardConfigCFlags.mk
include $(BUILD_SHARED_LIBRARY)


