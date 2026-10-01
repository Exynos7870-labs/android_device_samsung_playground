#
# build TuiService
#

# ExySp: Choice TUI availability
#_SUPPORT_TUI := true
ifdef _SUPPORT_TUI
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

# Module name (sets name of output binary / library)
LOCAL_MODULE := libTui
LOCAL_PROPRIETARY_MODULE := true

# Add your source files here (relative paths)
LOCAL_SRC_FILES += \
	jni/tlcTui.cpp \
	jni/tlcTuiJni.cpp

# Enable logging to logcat per default
LOCAL_CFLAGS += -DLOG_ANDROID
LOCAL_LDLIBS += -llog

# Undefine NDEBUG to enable LOG_D in log
LOCAL_CFLAGS += -UNDEBUG
LOCAL_CFLAGS += -Wno-unused-function

# Needed to use Trustonic logging macros
LOCAL_SHARED_LIBRARIES := libMcClient
LOCAL_ALLOW_UNDEFINED_SYMBOLS := true
LOCAL_HEADER_LIBRARIES := jni_headers

include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(call all-java-files-under, src)

LOCAL_PACKAGE_NAME := TuiService
LOCAL_MODULE_TAGS := optional
LOCAL_CERTIFICATE := platform
LOCAL_DEX_PREOPT := false
LOCAL_PRIVATE_PLATFORM_APIS := true

LOCAL_PROGUARD_FLAGS := -include $(LOCAL_PATH)/proguard-project.txt

include $(BUILD_PACKAGE)

# =============================================================================

# adding the root folder to the search path appears to make absolute paths
# work for import-module - lets see how long this works and what surprises
# future developers get from this.
$(call import-add-path,/)
$(call import-module,$(COMP_PATH_MobiCoreClientLib_module))
endif
