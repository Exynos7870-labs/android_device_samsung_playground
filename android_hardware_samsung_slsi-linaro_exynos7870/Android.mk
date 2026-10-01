#
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
#

ifeq ($(TARGET_SOC), exynos7870)

exynos7870_dirs += \
    libcamera \
    libcamera3 \
	libgatekeeper \
	mobicore

# Allow devices to choose which Exynos camera library is built from
# source.  some devices build HAL1 libexynoscamera from source and keeps
# stock/prebuilt HAL3 libexynoscamera3.
ifeq ($(BOARD_CAMERA_SKIP_HAL3_SOURCE), true)
    exynos7870_dirs := $(filter-out libcamera3,$(exynos7870_dirs))
endif

ifeq ($(BOARD_CAMERA_SKIP_HAL1_SOURCE), true)
    exynos7870_dirs := $(filter-out libcamera,$(exynos7870_dirs))
endif

ifeq ($(BOARD_CAMERA_SKIP_HAL_INTERFACE), true)
    exynos7870_dirs := $(filter-out libcameraInterface,$(exynos7870_dirs))
endif

ifeq ($(TARGET_BOARD_CAMERA_DEVICE_VER), Q)
    # do not include libcameraInterface
else
    exynos7870_dirs += libcameraInterface
endif

include $(call all-named-subdir-makefiles,$(exynos7870_dirs))

endif
