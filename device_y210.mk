# languages_full.mk deliberately NOT inherited here (see cm.mk) — this
# device sets PRODUCT_LOCALES := en_US es_US directly to fit its ~186 MiB
# /system partition; this file used to redundantly re-inherit the full
# ~50-locale set, undoing that trim via a separate product-inheritance node.

# Device-specific resource overlays (lockscreen layout, etc.)
PRODUCT_PACKAGE_OVERLAYS := device/huawei/y210/overlay

# The gps config appropriate for this device
$(call inherit-product, device/common/gps/gps_us_supl.mk)

# Adds HuaweiQualcommRIL (extends QualcommSharedRIL) into framework.jar's
# sources, matching device/lge/g300-style JB device trees. Needs cm.mk (the
# top-level product file that inherits this one) to have LOCAL_PATH set to
# device/huawei/y210 at this point, which it does via the normal product
# makefile include mechanism.
FRAMEWORKS_BASE_SUBDIRS += ../../$(LOCAL_PATH)/ril/

$(call inherit-product-if-exists, vendor/huawei/y210/y210-vendor.mk)

DEVICE_PACKAGE_OVERLAYS += device/huawei/y210/overlay

# Video decoding
PRODUCT_PACKAGES += \
    libstagefrighthw \
    libmm-omxcore \
    libOmxCore

# Graphics
PRODUCT_PACKAGES += \
    gralloc.msm7x27a \
    copybit.msm7x27a \
    hwcomposer.msm7x27a

# Audio
PRODUCT_PACKAGES += \
    libaudio \
    libaudiopolicy \
    audio.a2dp.default \
    audio.primary.y210 \
    audio_policy.msm7x27a

# Camera
# libcamera_compat is only built when BUILD_Y210_BLOB_CAMERA_WRAPPER=true
# (see device/huawei/y210/libcamera/Android.mk) — not part of the active
# camera.y210 build, which now comes from libcamera-caf/.
PRODUCT_PACKAGES += \
    camera.y210

# Other
PRODUCT_PACKAGES += \
    lights.y210 \
    gps.y210 \
    FileManager \
    libos_compat

# Telephony — generic_no_telephony.mk (our base, see below) deliberately
# omits these (it's meant for tablets); this is the same pair build/target/
# product/telephony.mk normally adds back for phone products. Without
# `rild`, init.rc's "service ril-daemon /system/bin/rild" silently disables
# itself at boot ("cannot find '/system/bin/rild'") since LOCAL_MODULE_TAGS
# := optional modules aren't installed by default on userdebug/user builds.
PRODUCT_PACKAGES += \
    rild \
    Mms

# Install the features available on this device.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.camera.autofocus.xml:system/etc/permissions/android.hardware.camera.autofocus.xml \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml

PRODUCT_COPY_FILES += \
    device/huawei/y210/init.rc:root/init.rc \
    device/huawei/y210/prebuilt/init.huawei.rc:root/init.huawei.rc \
    device/huawei/y210/prebuilt/init.target.rc:root/init.target.rc \
    device/huawei/y210/prebuilt/init.y210.rc:root/init.y210.rc \
    device/huawei/y210/prebuilt/init.mem.rc:root/init.mem.rc \
    device/huawei/y210/prebuilt/ueventd.huawei.rc:root/ueventd.huawei.rc \
    device/huawei/y210/prebuilt/init.qcom.sh:root/init.qcom.sh \
    device/huawei/y210/prebuilt/init.huawei.usb.rc:root/init.huawei.usb.rc \
    device/huawei/y210/prebuilt/system/etc/init.qcom.bt.sh:system/etc/init.qcom.bt.sh \
    device/huawei/y210/prebuilt/system/etc/init.qcom.fm.sh:system/etc/init.qcom.fm.sh \
    device/huawei/y210/prebuilt/system/etc/vold.fstab:system/etc/vold.fstab \
    device/huawei/y210/prebuilt/system/etc/AutoVolumeControl.txt:system/etc/AutoVolumeControl.txt \
    device/huawei/y210/prebuilt/system/etc/AudioFilter.csv:system/etc/AudioFilter.csv \
    device/huawei/y210/prebuilt/system/etc/wifi/wpa_supplicant.conf:system/etc/wifi/wpa_supplicant.conf \
    device/huawei/y210/prebuilt/system/etc/wifi/hostapd.conf:system/etc/wifi/hostapd.conf \
	    device/huawei/y210/prebuilt/system/bin/hostapd:system/bin/hostapd \
	    device/huawei/y210/prebuilt/system/bin/fm_qsoc_patches:system/bin/fm_qsoc_patches \
	    device/huawei/y210/prebuilt/system/bin/fmconfig:system/bin/fmconfig \
	    device/huawei/y210/prebuilt/system/bin/ds_fmc_appd:system/bin/ds_fmc_appd \
	    device/huawei/y210/prebuilt/system/lib/libfm_hal.so:system/lib/libfm_hal.so \
	    device/huawei/y210/prebuilt/system/lib/hw/libqcomfm_if.so:system/lib/hw/libqcomfm_if.so \
	    device/huawei/y210/prebuilt/system/etc/media_profiles.xml:system/etc/media_profiles.xml \
	    device/huawei/y210/prebuilt/system/etc/media_codecs.xml:system/etc/media_codecs.xml \
	    device/huawei/y210/prebuilt/system/etc/audio_policy.conf:system/etc/audio_policy.conf \
	    device/huawei/y210/prebuilt/system/bin/sleeplogcat:system/bin/sleeplogcat \
	    device/huawei/y210/prebuilt/system/bin/kmsgcat:system/bin/kmsgcat \
	    device/huawei/y210/prebuilt/system/bin/diag_mdlog:system/bin/diag_mdlog

# File manager prebuilt — copy FileManager.apk to prebuilt/system/app/ to include in ROM
# Example: cp ~/EsFileExplorer.apk device/huawei/y210/prebuilt/system/app/FileManager.apk
ifneq ($(wildcard device/huawei/y210/prebuilt/system/app/FileManager.apk),)
PRODUCT_COPY_FILES += \
    device/huawei/y210/prebuilt/system/app/FileManager.apk:system/app/FileManager.apk
endif

# Input device configuration (IDC) — required for touchscreen classification in ICS
PRODUCT_COPY_FILES += \
    device/huawei/y210/prebuilt/system/usr/idc/melfas-touchscreen.idc:system/usr/idc/melfas-touchscreen.idc \
    device/huawei/y210/prebuilt/system/usr/idc/synaptics.idc:system/usr/idc/synaptics.idc \
    device/huawei/y210/prebuilt/system/usr/keylayout/melfas-touchscreen.kl:system/usr/keylayout/melfas-touchscreen.kl \
    device/huawei/y210/prebuilt/system/usr/keylayout/synaptics.kl:system/usr/keylayout/synaptics.kl \
    device/huawei/y210/prebuilt/system/usr/keylayout/surf_keypad.kl:system/usr/keylayout/surf_keypad.kl \
    device/huawei/y210/prebuilt/system/usr/keylayout/7x27a_kp.kl:system/usr/keylayout/7x27a_kp.kl \
    device/huawei/y210/prebuilt/system/usr/keylayout/7k_handset.kl:system/usr/keylayout/7k_handset.kl \
    device/huawei/y210/prebuilt/system/usr/keychars/7x27a_kp.kcm:system/usr/keychars/7x27a_kp.kcm

# Wi-Fi firmware and module (Qualcomm/Atheros)
PRODUCT_COPY_FILES += \
    device/huawei/y210/prebuilt/system/wifi/ar6000.ko:system/wifi/ar6000.ko \
    device/huawei/y210/prebuilt/system/bin/wlan_detect:system/bin/wlan_detect \
    device/huawei/y210/prebuilt/system/bin/wlan_tool:system/bin/wlan_tool \
    device/huawei/y210/prebuilt/system/etc/init.qcom.wifi.sh:system/etc/init.qcom.wifi.sh \
    $(call find-copy-subdir-files,*,device/huawei/y210/prebuilt/system/wifi/ath6k,system/wifi/ath6k)

# Bluetooth firmware and scripts (Broadcom)
PRODUCT_COPY_FILES += \
    $(call find-copy-subdir-files,*,device/huawei/y210/prebuilt/system/etc/bluetooth,system/etc/bluetooth)

# Use generic_no_telephony as base: includes fonts, keyboards, librs_jni,
# RenderScript and other runtime essentials that core.mk omits.
# Avoids the ~20 MB of wallpapers/VideoEditor in full_base.mk.
$(call inherit-product, build/target/product/generic_no_telephony.mk)

# Add back apps needed for a functional phone that core.mk omits
PRODUCT_PACKAGES += \
    Mms \
    Phone \
    Settings \
    LatinIME \
    Calendar \
    DeskClock \
    Email \
    Exchange \
    Music \
    Camera \
    Bluetooth \
    VoiceDialer \
    SoundRecorder \
    SystemUI \
    Trebuchet

PRODUCT_LOCALES += hdpi

# HVGA (320x480, mdpi) phone: drop resource buckets for screen sizes/
# densities this device can never use (e.g. Trebuchet alone ships ~9.5 MB
# of sw600dp/sw720dp tablet wallpaper art under its default packaging).
PRODUCT_AAPT_CONFIG := normal mdpi
PRODUCT_AAPT_PREF_CONFIG := mdpi

# system.img must fit the Y210's real ~199 MB /system partition (same limit
# hit on the CM9 side). vendor/cm/config/common.mk unconditionally adds
# ~19 MB of extra LatinIME language dictionaries plus several apps that
# aren't needed for bring-up. PRODUCT_PACKAGE_OVERLAYS/PRODUCT_PACKAGES/etc.
# can't be subtracted from here: Android's product-inheritance system
# (build/core/node_fns.mk's import-nodes) evaluates each product .mk file's
# variables in an isolated scope and unions the results at the end, so a
# filter-out in this file never sees what an inherited file like common.mk
# already added. The actual trim lives in common.mk itself, gated on
# TARGET_PRODUCT so it only affects this device.

PRODUCT_BUILD_PROP_OVERRIDES += BUILD_UTC_DATE=0
PRODUCT_NAME := full_y210
PRODUCT_DEVICE := y210
PRODUCT_BRAND := Huawei
PRODUCT_MANUFACTURER := HUAWEI
PRODUCT_MODEL := HUAWEI Y210-0151
