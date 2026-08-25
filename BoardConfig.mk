# Camera HAL wrapper — ICS camera_device_t wrapping the GB Qualcomm blob
USE_CAMERA_STUB := false

# inherit from the proprietary version
-include vendor/huawei/y210/BoardConfigVendor.mk

TARGET_NO_BOOTLOADER := true

TARGET_BOARD_PLATFORM := msm7x27a
TARGET_BOARD_PLATFORM_GPU := qcom-adreno200

TARGET_NO_RADIOIMAGE := true

TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_ARCH_VARIANT := armv7-a-neon
ARCH_ARM_HAVE_TLS_REGISTER := true

TARGET_BOOTLOADER_BOARD_NAME := y210
TARGET_OTA_ASSERT_DEVICE := y210,hwy210

# JB build requires this explicitly (CM9/ICS did not); hardware/qcom/display's
# libtilerenderer is only defined when set, and frameworks/base/core/jni
# links against it whenever BOARD_USES_QCOM_HARDWARE is true.
USE_OPENGL_RENDERER := true

# Host build hygiene: disable SREC grammar generation on modern hosts.
# This avoids building `grxmlcompile` (OpenFst-based) which is not needed
# for the Y210 bring-up and is incompatible with newer host toolchains.
BUILD_SREC_GRAMMARS := false

# Audio
# hardware/msm7k/Android.mk keys off TARGET_PROVIDES_LIBAUDIO to avoid
# building the generic msm7k audio HAL alongside the device-specific one.
TARGET_PROVIDES_LIBAUDIO := true

# hardware/msm7k/Android.mk keys off TARGET_PROVIDES_LIBLIGHTS to avoid
# building the generic msm7k lights HAL alongside the device-specific one.
TARGET_PROVIDES_LIBLIGHTS := true

# GPS
BOARD_USES_QCOM_GPS := true
BOARD_VENDOR_QCOM_GPS_LOC_API_AMSS_VERSION := 50000
BOARD_VENDOR_QCOM_GPS_LOC_API_HARDWARE := y210

TARGET_SPECIFIC_HEADER_PATH := device/huawei/y210/include

#recovery
BOARD_LDPI_RECOVERY := true
BOARD_HAS_JANKY_BACKBUFFER := true
# MSM7x27A's fb driver predates the msmfb_metadata/MSMFB_METADATA_SET alpha
# blend ioctl that hardware/qcom/display/libgralloc now issues unconditionally
# unless this is set; our device/kernel headers don't define it either.
TARGET_NO_HW_VSYNC := true
# This device has no secure/DRM content-protection path (no TrustZone-backed
# playback), and our kernel's msm_rotator_img_info predates the "secure"
# field hardware/qcom/display/liboverlay otherwise references.
COMMON_GLOBAL_CFLAGS += -DQCOM_NO_SECURE_PLAYBACK
# camera.y210 (libcamera-caf) uses android::MemoryHeapPmem for the PMEM-backed
# camera buffer heaps; frameworks/native/libs/binder only builds it when set.
BOARD_NEEDS_MEMORYHEAPPMEM := true
# Same HAL id-mismatch crash already found and fixed for vee4ss (see memory):
# hardware.c's load() does `strcmp(id, hmi->id)` after dlsym'ing
# HAL_MODULE_INFO_SYM, and hmi->id reads as garbage on this toolchain/build,
# crashing (SEGV) instead of just failing the string compare. Hit here via
# LightsService failing to load lights.y210.so during system_server startup.
BOARD_DISABLE_HW_ID_MATCH_CHECK := true
#BOARD_CUSTOM_GRAPHICS           := ../../../device/huawei/y210/recovery/graphics.c


# OpenGL drivers config file path
BOARD_EGL_CFG := device/huawei/y210/prebuilt/system/lib/egl/egl.cfg
BOARD_USES_QCOM_HARDWARE := true
TARGET_USES_GENLOCK := true
COMMON_GLOBAL_CFLAGS += -DQCOM_HARDWARE
BOARD_USES_QCOM_LIBRPC := true
BOARD_USES_QCOM_LIBS := true
BOARD_USE_QCOM_PMEM := true
COMMON_GLOBAL_CFLAGS += -DTARGET_MSM7x27A -DTARGET_MSM7x27
# PARCHE-E: comentar la siguiente línea para desactivar copybit y aislar si
# la repetición horizontal la causa el MDP blit path vs el render directo.
# Reactivar una vez descartado o confirmado.
TARGET_LIBAGL_USE_GRALLOC_COPYBITS := true
TARGET_GRALLOC_USES_ASHMEM := true

# GPU — from Dazzozo G300 CM9 (same MSM7x27A + Adreno200)
BOARD_ADRENO_DECIDE_TEXTURE_TARGET := true
BOARD_AVOID_DRAW_TEXTURE_EXTENSION := true
TARGET_FORCE_CPU_UPLOAD := true
ENABLE_WEBGL := true

WITH_JIT := true
ENABLE_JSC_JIT := true
JS_ENGINE := v8
HTTP := chrome

BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_BCM := true

# FM Radio (Y210 has an FM-capable Qualcomm stack on some variants; keep
# build-time support enabled so the FM app can be built/installed for testing.)
BOARD_HAVE_FM_RADIO := true
BOARD_GLOBAL_CFLAGS += -DHAVE_FM_RADIO
# Qualcomm "tavarua" style V4L2 radio device.
BOARD_FM_DEVICE := qcom

# RIL
# BOARD_PROVIDES_LIBRIL := true

# Wi-Fi
BOARD_WPA_SUPPLICANT_DRIVER := WEXT
BOARD_WLAN_DEVICE           := ath6kl
WIFI_DRIVER_MODULE_PATH     := /system/wifi/ar6000.ko
WIFI_DRIVER_MODULE_ARG      := ""
WIFI_DRIVER_MODULE_NAME     := ar6000
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_ath6kl
WPA_SUPPLICANT_VERSION      := VER_0_8_X
WIFI_PRE_LOADER             := wlan_detect

# Partition sizes from device /proc/mtd (bytes)
BOARD_BOOTIMAGE_PARTITION_SIZE := 0x00500000
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 0x00600000
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 0x0BA00000
BOARD_USERDATAIMAGE_PARTITION_SIZE := 0x0A000000
BOARD_CACHEIMAGE_PARTITION_SIZE := 0x03A00000
BOARD_FLASH_BLOCK_SIZE := 131072

# Kernel 
TARGET_KERNEL_SOURCE := kernel/huawei/y210
TARGET_KERNEL_CONFIG := hw_msm7x27a_defconfig
BOARD_KERNEL_CMDLINE := console=ttyDCC0 androidboot.hardware=huawei
BOARD_KERNEL_BASE := 0x00200000
BOARD_KERNEL_PAGESIZE := 4096

BOARD_HAS_EXTRA_SYS_PROPS := true

# TARGET_RECOVERY_INITRC := device/huawei/y210/recovery/etc/init.rc
# BOARD_DATA_DEVICE := /dev/block/mmcblk0p13
# BOARD_DATA_FILESYSTEM := ext4
# BOARD_DATA_FILESYSTEM_OPTIONS := rw
# BOARD_SYSTEM_DEVICE := /dev/block/mmcblk0p12
# BOARD_SYSTEM_FILESYSTEM := ext4
# BOARD_SYSTEM_FILESYSTEM_OPTIONS := rw
# BOARD_CACHE_DEVICE := /dev/block/mmcblk0p6
# BOARD_CACHE_FILESYSTEM := ext4
# BOARD_CACHE_FILESYSTEM_OPTIONS := rw
# BOARD_USES_MMCUTILS := true
# BOARD_HAS_NO_MISC_PARTITION := true

BOARD_USE_USB_MASS_STORAGE_SWITCH := true
TARGET_USE_CUSTOM_LUN_FILE_PATH := /sys/devices/platform/usb_mass_storage/lun%d/file
TARGET_USE_CUSTOM_SECOND_LUN_NUM := 2
BOARD_VOLD_MAX_PARTITIONS := 19
BOARD_VOLD_EMMC_SHARES_DEV_MAJOR := true
BOARD_UMS_LUNFILE := /sys/devices/platform/usb_mass_storage/lun0/file

COMMON_GLOBAL_CFLAGS += -DREFRESH_RATE=60

# We ship our own root init.rc (device/huawei/y210/init.rc) so it can import
# init.${ro.hardware}.rc / .usb.rc without touching system/core/rootdir/init.rc.
TARGET_PROVIDES_INIT_RC := true
