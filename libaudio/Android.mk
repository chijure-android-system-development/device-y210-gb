LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:=               \
    AudioPolicyManager.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libmedia

LOCAL_STATIC_LIBRARIES := libmedia_helper

LOCAL_WHOLE_STATIC_LIBRARIES := libaudiopolicy_legacy

LOCAL_C_INCLUDES += \
    hardware/libhardware_legacy/include

LOCAL_MODULE:= libaudiopolicy

ifeq ($(BOARD_HAVE_BLUETOOTH),true)
  LOCAL_CFLAGS += -DWITH_A2DP
endif

# See device/huawei/y210/libaudio's "libaudio" module block below for why
# this is disabled -- keep every HAVE_FM_RADIO block in this file and in
# hardware/libhardware_legacy/audio/Android.mk in sync (all 4 currently
# commented out together).
#ifeq ($(BOARD_HAVE_FM_RADIO),true)
#  LOCAL_CFLAGS += -DHAVE_FM_RADIO
#endif

include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)

LOCAL_MODULE := libaudio

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libmedia \
    libhardware_legacy

LOCAL_C_INCLUDES += \
    device/huawei/y210/include

ifeq ($TARGET_OS)-$(TARGET_SIMULATOR),linux-true)
LOCAL_LDLIBS += -ldl
endif

ifneq ($(TARGET_SIMULATOR),true)
LOCAL_SHARED_LIBRARIES += libdl
endif

LOCAL_SRC_FILES += AudioHardware.cpp

LOCAL_CFLAGS += -fno-short-enums

# HAVE_FM_RADIO deliberately NOT defined here (or in the other 3 blocks
# across this file and hardware/libhardware_legacy/audio/Android.mk).
# Root cause (2026-07-19, two sessions): a pre-existing init-order race in
# AudioPolicyManagerBase -- Binder calls (setForceUse/setDeviceConnectionState/
# setStreamVolumeIndex/etc, from AudioService.java during early boot) can
# reach AudioPolicyManagerBase methods before its constructor has finished
# opening the hardware output, so mOutputs is still empty and mHardwareOutput
# is still 0. Several call sites do mOutputs.valueFor(output) with no bounds
# check, which reads garbage instead of failing cleanly -> SIGSEGV @
# 0xfffffff4 / SIGBUS @ 0x0 in mediaserver.
# This race has probably always existed, but enabling HAVE_FM_RADIO adds
# real work to AudioHardware's constructor (the FM endpoint-name scan) that
# widens the race window enough to reliably lose it. Guarded 4 confirmed
# crash sites in AudioPolicyManagerBase.cpp (getNewDevice, setOutputDevice,
# the new fm_on/fm_off setParameters call, checkAndSetVolume) across two
# sessions and mediaserver STILL crash-loops until init gives up restarting
# it entirely -- there are more unguarded mOutputs.valueFor()/valueAt() call
# sites (~15+, see issue_fm_no_audio memory for the full list) than we had
# time to patch one-by-one. Next session: either guard all remaining sites
# in one pass, or (better) find why AudioPolicyService becomes reachable via
# Binder before its own AudioPolicyManagerBase is fully constructed and fix
# that ordering instead of patching every call site.
#ifeq ($(BOARD_HAVE_FM_RADIO),true)
#  LOCAL_CFLAGS += -DHAVE_FM_RADIO
#endif

LOCAL_STATIC_LIBRARIES += libaudiohw_legacy libmedia_helper

include $(BUILD_SHARED_LIBRARY)

# ICS audio HAL wrapper: wraps libaudio (legacy AudioHardwareInterface)
# so AudioFlinger can load it as audio.primary.y210.so.
# audio_hw_hal.cpp calls createAudioHardware() which libaudio.so provides.
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    ../../../../hardware/libhardware_legacy/audio/audio_hw_hal.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libmedia \
    liblog \
    libaudio

LOCAL_C_INCLUDES += \
    hardware/libhardware_legacy/include \
    hardware/libhardware_legacy/include/hardware_legacy \
    hardware/libhardware/include

LOCAL_MODULE := audio.primary.y210
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional
# Same GNU_RELRO/HAL_MODULE_INFO_SYM layout hazard fixed for liblights —
# see device/huawei/y210/liblights/Android.mk for the full explanation.
LOCAL_LDFLAGS += -Wl,-z,norelro

ifeq ($(BOARD_HAVE_BLUETOOTH),true)
  LOCAL_CFLAGS += -DWITH_A2DP
endif

# This module compiles audio_hw_hal.cpp separately from libaudio's own
# compilation of AudioHardwareInterface.h (via AudioHardware.h). It MUST see
# HAVE_FM_RADIO exactly like libaudio does: AudioHardwareInterface.h declares
# setFmVolume() as a real vtable slot when
# QCOM_HARDWARE && HAVE_FM_RADIO && !USES_AUDIO_LEGACY (see
# hardware_legacy/AudioHardwareInterface.h). USES_AUDIO_LEGACY is never
# defined anywhere in this whole build (BOARD_USES_AUDIO_LEGACY is unset),
# and QCOM_HARDWARE comes from BoardConfig.mk's COMMON_GLOBAL_CFLAGS, so
# both are always true here -- this module was the one place still missing
# the flag, so it saw a vtable WITHOUT setFmVolume while libaudio.so's real
# AudioHardware object had it, silently shifting every later virtual call
# (setParameters, getParameters, getInputBufferSize, ...) by one slot. That
# mismatch is what was crashing mediaserver with a SIGBUS inside
# adev_set_parameters() on every single call, not the mOutputs/openOutput
# race documented above (that race is real too, but this was the dominant,
# 100%-reproducible crash).
#ifeq ($(BOARD_HAVE_FM_RADIO),true)
#  LOCAL_CFLAGS += -DHAVE_FM_RADIO
#endif

include $(BUILD_SHARED_LIBRARY)
