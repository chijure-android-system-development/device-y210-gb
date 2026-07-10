# Inherit AOSP device configuration for Y210.
$(call inherit-product, device/huawei/y210/device_y210.mk)

# Inherit some common cyanogenmod stuff. common_full_no_themes.mk se salta
# themes.mk (paquetes de ejemplo Androidian/Cyanbread), build un poco mas
# liviano. El motor de temas (ThemeManager/ThemeChooser/com.tmobile.themes)
# sigue presente igual: vive en common.mk, compartido por todos los
# dispositivos del arbol, no se puede sacar desde aca.
$(call inherit-product, vendor/cyanogen/products/common_full_no_themes.mk)

# Include GSM stuff
$(call inherit-product, vendor/cyanogen/products/gsm.mk)

# Broadcom FM radio
$(call inherit-product, vendor/cyanogen/products/bcm_fm_radio.mk)

#
# Setup device specific product configuration.
#
PRODUCT_NAME := cyanogen_y210
PRODUCT_BRAND := Huawei
PRODUCT_DEVICE := y210
PRODUCT_MODEL := HUAWEI Y210-0151
PRODUCT_MANUFACTURER := HUAWEI
PRODUCT_PROPERTY_OVERRIDES += \
    ro.build.product=msm7625a
# Some Huawei/Qualcomm camera blobs key off ro.build.product; it must not be
# generated as "y210" in build.prop (ro.* first-writer wins, so the plain
# PRODUCT_PROPERTY_OVERRIDES line above alone is not enough — the auto
# generated "ro.build.product=y210" line from buildinfo.sh comes first and
# wins). Sin esto: "Unable to determine the target type" y crash en
# QualcommCameraHardware/MMCameraDL al abrir la camara.
PRODUCT_BUILD_PROP_OVERRIDES += BUILD_PRODUCT=msm7625a

# RIL / Telephony
PRODUCT_PROPERTY_OVERRIDES += \
    ro.telephony.default_network=0 \
    ro.telephony.ril_class=HuaweiQualcommRIL \
    ril.subscription.types=NV,RUIM
# PRODUCT_BUILD_PROP_OVERRIDES += PRODUCT_NAME=y210 BUILD_ID=GRK39F BUILD_DISPLAY_ID=GWK74 BUILD_FINGERPRINT=Huawei/Y210/hwy210-0151:2.3.6/HuaweiY210-0151/C40B855:user/ota-rel-keys,release-keys PRIVATE_BUILD_DESC="passion-user 2.3.6 GRK39F 189904 release-keys"

# Release name and versioning
PRODUCT_RELEASE_NAME := Y210
PRODUCT_VERSION_DEVICE_SPECIFIC :=
-include vendor/cyanogen/products/common_versions.mk

PRODUCT_COPY_FILES +=  \
     vendor/cyanogen/prebuilt/mdpi/media/bootanimation.zip:system/media/bootanimation.zip
