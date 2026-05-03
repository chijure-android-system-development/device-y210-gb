LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := fmctrlscan
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := fmctrlscan.c
LOCAL_SHARED_LIBRARIES := libcutils
include $(BUILD_EXECUTABLE)

