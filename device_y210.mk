$(call inherit-product, $(SRC_TARGET_DIR)/product/languages_full.mk)

# The gps config appropriate for this device
$(call inherit-product, device/common/gps/gps_us_supl.mk)

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
   gralloc.y210 \
   copybit.y210

# Audio
PRODUCT_PACKAGES += \
    audio.primary.y210 \
    audio_policy.y210 \
    audio.a2dp.default \
    libaudioutils

# Other
PRODUCT_PACKAGES += \
    lights.y210 \
    gps.y210

ifeq ($(TARGET_PREBUILT_KERNEL),)
	LOCAL_KERNEL := device/huawei/y210/kernel
else
	LOCAL_KERNEL := $(TARGET_PREBUILT_KERNEL)
endif

PRODUCT_COPY_FILES += \
    $(LOCAL_KERNEL):kernel

# Install the features available on this device.
PRODUCT_COPY_FILES += \
    frameworks/base/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
    frameworks/base/data/etc/android.hardware.camera.autofocus.xml:system/etc/permissions/android.hardware.camera.autofocus.xml \
    frameworks/base/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/base/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/base/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/base/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/base/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/base/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml \
    frameworks/base/data/etc/android.hardware.sensor.gyroscope.xml:system/etc/permissions/android.hardware.sensor.gyroscope.xml

PRODUCT_COPY_FILES += \
    device/huawei/y210/prebuilt/init.huawei.rc:root/init.huawei.rc \
    device/huawei/y210/prebuilt/init.target.rc:root/init.target.rc \
    device/huawei/y210/prebuilt/init.y210.rc:root/init.y210.rc \
    device/huawei/y210/prebuilt/init.mem.rc:root/init.mem.rc \
    device/huawei/y210/prebuilt/ueventd.huawei.rc:root/ueventd.huawei.rc \
    device/huawei/y210/prebuilt/init.qcom.sh:root/init.qcom.sh \
    device/huawei/y210/prebuilt/init.huawei.usb.rc:root/init.huawei.usb.rc \
    device/huawei/y210/prebuilt/system/etc/init.qcom.bt.sh:system/etc/init.qcom.bt.sh \
    device/huawei/y210/prebuilt/system/etc/wifi/wpa_supplicant.conf:system/etc/wifi/wpa_supplicant.conf

# Wi-Fi firmware and module (Qualcomm/Atheros)
PRODUCT_COPY_FILES += \
    device/huawei/y210/prebuilt/system/wifi/ar6000.ko:system/wifi/ar6000.ko \
    device/huawei/y210/prebuilt/system/bin/wlan_detect:system/bin/wlan_detect \
    device/huawei/y210/prebuilt/system/etc/init.qcom.wifi.sh:system/etc/init.qcom.wifi.sh \
    $(call find-copy-subdir-files,*,device/huawei/y210/prebuilt/system/wifi/ath6k,system/wifi/ath6k)

# Bluetooth firmware and scripts (Broadcom)
PRODUCT_COPY_FILES += \
    $(call find-copy-subdir-files,*,device/huawei/y210/prebuilt/system/etc/bluetooth,system/etc/bluetooth)

$(call inherit-product, build/target/product/full_base.mk)

PRODUCT_LOCALES += hdpi

PRODUCT_NAME := Y210
PRODUCT_DEVICE := msm7625a
PRODUCT_BRAND := Huawei
PRODUCT_MANUFACTURER := Huawei
PRODUCT_MODEL := HUAWEI Y210-0151
