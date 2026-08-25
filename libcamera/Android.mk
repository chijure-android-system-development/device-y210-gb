LOCAL_PATH := $(call my-dir)

# DISABLED 2026-08-01: replaced by ../libcamera-caf/ (camera.y210 built from
# CAF reference source instead of wrapping the closed libcamera.y210.so blob).
# Files kept on disk for reference/revert; not built while this guard is off.
ifeq ($(BUILD_Y210_BLOB_CAMERA_WRAPPER),true)

# libcamera_compat.so — GB→ICS symbol stubs, loaded RTLD_GLOBAL before the blob
include $(CLEAR_VARS)
LOCAL_MODULE_TAGS        := optional
LOCAL_MODULE             := libcamera_compat
LOCAL_SRC_FILES          := libcamera_compat.cpp
LOCAL_SHARED_LIBRARIES   := liblog
include $(BUILD_SHARED_LIBRARY)

# camera.y210.so — ICS HAL wrapper
include $(CLEAR_VARS)
LOCAL_MODULE_TAGS    := optional
LOCAL_MODULE_PATH    := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE         := camera.$(TARGET_BOOTLOADER_BOARD_NAME)
LOCAL_SRC_FILES      := Y210CameraWrapper.cpp camera_compat.cpp camera_compat_asm.S

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libdl \
    libutils \
    libcutils \
    libhardware \
    libbinder \
    libcamera_client \
    libui \
    libsurfaceflinger_client \
    libcamera_compat

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH) \
    frameworks/base/services/ \
    frameworks/base/include \
    hardware/libhardware/include \
    hardware/libhardware/modules/gralloc

include $(BUILD_SHARED_LIBRARY)

endif # BUILD_Y210_BLOB_CAMERA_WRAPPER
