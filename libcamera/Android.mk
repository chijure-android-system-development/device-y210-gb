LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libcamera
LOCAL_MODULE_TAGS := optional
LOCAL_PRELINK_MODULE := false
ifeq ($(BOARD_USE_CAF_LIBCAMERA_GB_REL),true)
LOCAL_CFLAGS += -DCAF_CAMERA_GB_REL
endif
ifeq ($(BOARD_CAMERA_USE_GETBUFFERINFO),true)
LOCAL_CFLAGS += -DUSE_GETBUFFERINFO
endif
LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libcamera_client \
    libui \
    libbinder \
    libdl
LOCAL_SRC_FILES := Y210CameraWrapper.cpp

include $(BUILD_SHARED_LIBRARY)
