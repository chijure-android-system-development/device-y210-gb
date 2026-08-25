LOCAL_PATH := $(call my-dir)

# camera.y210.so — built from CAF (Code Aurora Forum) reference source for
# MSM7x27A, instead of wrapping Huawei's closed libcamera.y210.so blob.
# Compiling this C++ layer from source against real ICS headers eliminates
# the whole class of GB->ICS ABI/vtable mismatch bugs (mapMemory calling
# convention, MemoryHeapBase double-free, etc.) that plagued the blob-wrapper
# approach in ../libcamera/. Still dlopens liboemcamera.so (the lower-level
# sensor/ISP/JPEG driver blob) — that layer is a plain C ABI, stable across
# GB/ICS, so no ABI mismatch risk there.
include $(CLEAR_VARS)

LOCAL_CFLAGS := -DMSM_CAMERA_GCC -DMSM_CAMERA_BIONIC
LOCAL_CFLAGS += -DDLOPEN_LIBMMCAMERA=1
LOCAL_CFLAGS += -DHW_ENCODE
LOCAL_CFLAGS += -DNUM_PREVIEW_BUFFERS=6 -D_ANDROID_
LOCAL_CFLAGS += -DUSE_NEON_CONVERSION

LOCAL_SRC_FILES := QualcommCamera.cpp QualcommCameraHardware.cpp

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH) \
    frameworks/base/services/camera/libcameraservice \
    hardware/qcom/display/libgralloc \
    hardware/qcom/display/libgenlock \
    hardware/qcom/media/libstagefrighthw

LOCAL_SHARED_LIBRARIES := libutils libui libcamera_client liblog libcutils libgenlock libbinder libdl libhardware

LOCAL_CFLAGS += -include bionic/libc/kernel/common/linux/socket.h

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE := camera.$(TARGET_BOOTLOADER_BOARD_NAME)
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
