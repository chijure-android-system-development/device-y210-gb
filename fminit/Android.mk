LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE           := fminit
LOCAL_MODULE_TAGS      := optional
LOCAL_SRC_FILES        := fminit.c
LOCAL_SHARED_LIBRARIES := libcutils
LOCAL_MODULE_PATH      := $(TARGET_OUT)/bin
include $(BUILD_EXECUTABLE)
