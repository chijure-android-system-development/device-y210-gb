#!/bin/bash
# apply-patches.sh — aplica todos los patches del Y210 sobre un árbol CM7 limpio.
# Ejecutar desde la raíz del árbol de CM7.
#
# Uso:
#   cd /path/to/cm7
#   bash device/huawei/y210/patches/apply-patches.sh

set -e

PATCHES_DIR="$(dirname "$0")"
CM7_ROOT="$(pwd)"

apply() {
    local project="$1"
    local patch="$2"
    echo "  Aplicando $patch en $project..."
    (cd "$CM7_ROOT/$project" && git apply "$CM7_ROOT/$PATCHES_DIR/$patch")
}

copy_new() {
    local src="$1"
    local dst="$2"
    echo "  Copiando archivo nuevo: $dst"
    cp "$CM7_ROOT/$PATCHES_DIR/$src" "$CM7_ROOT/$dst"
}

echo "=== Y210 patches ==="

# android manifest — hardware/ti/wpan apunta a ChameleonOS (fork compatible)
apply android                         android_manifest.patch

# hardware/ril — libril: RIL_setRilSocketName stub, sanitizeRilString,
#                         version negotiation flexible, unsol 1031-1037
apply hardware/ril                    hardware_ril.patch

# hardware/libhardware_legacy — wifi.c: soporte ath6k/AR6003 completo
apply hardware/libhardware_legacy     hardware_libhardware_legacy.patch

# hardware/msm7k — QualcommCameraHardware: logs spdq: (instrumentación temporal)
apply hardware/msm7k                  hardware_msm7k.patch

# frameworks/base — CameraParameters, RIL/telephony Java, JNI FM
apply frameworks/base                 frameworks_base.patch
copy_new frameworks_base_android_hardware_fm_qcom.cpp \
         frameworks/base/core/jni/android_hardware_fm_qcom.cpp

# packages/apps/Camera — adaptaciones Y210 al lifecycle de cámara
apply packages/apps/Camera            packages_apps_Camera.patch

# packages/apps/Settings — APN editor / APN settings para Claro Perú
apply packages/apps/Settings          packages_apps_Settings.patch

# vendor/cyanogen — integración del producto y210 en AndroidProducts.mk
apply vendor/cyanogen                 vendor_cyanogen.patch
copy_new vendor_cyanogen_y210.mk      vendor/cyanogen/products/cyanogen_y210.mk

echo "=== Todos los patches aplicados ==="
