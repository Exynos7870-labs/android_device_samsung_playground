MOBICORE_PROJECT_PATH := $(call my-dir)

MC_INCLUDE_DIR := $(MOBICORE_PROJECT_PATH)/Daemon/include \
    $(MOBICORE_PROJECT_PATH)/ClientLib/include \
    $(MOBICORE_PROJECT_PATH)/tlcm \
    $(MOBICORE_PROJECT_PATH)/tlcm/TlCm \
    $(MOBICORE_PROJECT_PATH)/tlcm/TlCm/2.0 \
    $(MOBICORE_PROJECT_PATH)/TuiService
    
MC_DEBUG := _DEBUG
#SYSTEM_LIB_DIR=/system/lib
GDM_PROVLIB_SHARED_LIBS=libMcClient

# Some things are specific to Android 6.0 and later (use stlport absence as indicator)
ifneq ($(wildcard external/stlport/libstlport.mk),)
# Up to Lollipop
TRUSTONIC_ANDROID_LEGACY_SUPPORT = yes
else
# Since Marshmallow
TRUSTONIC_ANDROID_LEGACY_SUPPORT =
endif

include $(call all-subdir-makefiles)
