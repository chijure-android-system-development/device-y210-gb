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

# hardware/ril — libril: RIL_setRilSocketName stub, sanitizeRilString,
#                         version negotiation flexible, unsol 1031-1037
apply hardware/ril                    hardware_ril.patch

# hardware/libhardware_legacy — wifi.c: soporte ath6k/AR6003 completo
apply hardware/libhardware_legacy     hardware_libhardware_legacy.patch

# hardware/msm7k — copybit: libcutils, stdlib.h, disable MDP blit para status bar
apply hardware/msm7k                  hardware_msm7k.patch

# system/netd — SoftAP ATH: declarar RSN pairwise para WPA2/CCMP
apply system/netd                     system_netd.patch

# frameworks/base — cámara: CameraParameters (HFR/denoise/redeye/YV12),
#                   StagefrightRecorder, CameraService (pmem_adsp restart),
#                   TextureManager (conversión NV21->RGB565 por software)
apply frameworks/base                 frameworks_base_camera.patch

# frameworks/base — RIL/telephony: DataConnection (respuesta QCRIL malformada),
#                   RIL.java/RILConstants (unsol 1037), GsmDataConnectionTracker
#                   (fallback de operator numeric), Zygote (GID AID_QCOM_ONCRPC)
apply frameworks/base                 frameworks_base_telephony.patch

# frameworks/base — PhoneFactory: rama faltante para instanciar HuaweiQualcommRIL
#                   (ya vive en device/huawei/y210/ril/, inyectado via
#                   FRAMEWORKS_BASE_SUBDIRS en device_y210.mk, pero nunca se
#                   seleccionaba). Ver device/huawei/y210/docs/RIL_NOTES.md.
apply frameworks/base                 frameworks_base_ril_class.patch

# frameworks/base — FM: hookup JNI (Android.mk, BOARD_FM_DEVICE=qcom) +
#                   implementación nativa + stack Java (FmRxControls, etc.)
apply frameworks/base                 frameworks_base_fm.patch
copy_new frameworks_base_android_hardware_fm_qcom.cpp \
         frameworks/base/core/jni/android_hardware_fm_qcom.cpp
apply frameworks/base                 frameworks_base_fm_java.patch

# frameworks/base — status bar: dedup de setIcon + workaround de ghosting
#                   (copy-back + zero back buffer) en superficies <=26px alto
apply frameworks/base                 frameworks_base_statusbar.patch

# packages/apps/Camera — adaptaciones Y210 al lifecycle de cámara
apply packages/apps/Camera            packages_apps_Camera.patch

# packages/apps/FM — app FM radio: config, FMRadio, FMRadioService, FmSharedPreferences
apply packages/apps/FM                packages_apps_FM.patch

# packages/apps/Settings — APN editor / APN settings para Claro Perú
apply packages/apps/Settings          packages_apps_Settings.patch

# vendor/cyanogen — APN Claro Perú (recortado: se saco un hunk que borraba
# ~60 entradas de products/AndroidProducts.mk y rompia breakfast/lunch de
# otros dispositivos del arbol sin dar ningun beneficio a y210)
apply vendor/cyanogen                 vendor_cyanogen.patch
copy_new vendor_cyanogen_y210.mk      vendor/cyanogen/products/cyanogen_y210.mk

echo "=== Todos los patches aplicados ==="
