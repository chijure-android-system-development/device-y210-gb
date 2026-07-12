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
CHECK_ONLY=1

apply() {
    local project="$1"
    local patch="$2"
    if [ "$CHECK_ONLY" = "1" ]; then
        (cd "$CM7_ROOT/$project" && git apply --check "$CM7_ROOT/$PATCHES_DIR/$patch")
    else
        echo "  Aplicando $patch en $project..."
        (cd "$CM7_ROOT/$project" && git apply "$CM7_ROOT/$PATCHES_DIR/$patch")
    fi
}

copy_new() {
    local src="$1"
    local dst="$2"
    if [ "$CHECK_ONLY" = "1" ]; then
        [ -f "$CM7_ROOT/$PATCHES_DIR/$src" ]
    else
        echo "  Copiando archivo nuevo: $dst"
        cp "$CM7_ROOT/$PATCHES_DIR/$src" "$CM7_ROOT/$dst"
    fi
}

run_all() {

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

# frameworks/base — panel expandido: title_bar_portrait.9.png tenía la región
#                   de estiramiento horizontal degenerada (1px), dejaba una
#                   franja gris fija a la izquierda de power_and_carrier;
#                   status_bar_expanded.xml sin paddingLeft explícito heredaba
#                   el padding del mismo nine-patch; StatusBarView.onLayout()
#                   caía a ancho completo (tapando iconos) cuando la fecha no
#                   podía "encajar" contra ningún ícono. Análisis completo con
#                   capturas reales del equipo, sesión 2026-07-13.
apply frameworks/base                 frameworks_base_statusbar_expanded.patch

# frameworks/base — title_bar_medium/portrait/shadow/tall (core/res): mismo
#                   defecto de nine-patch que arriba, en todos los archivos
#                   title_bar_* del framework (todas las densidades). Cada
#                   uno verificado antes de tocarlo: contenido uniforme por
#                   fila (solo degradado vertical, seguro ensanchar la región
#                   de estiramiento horizontal). Visible en PackageInstaller,
#                   probablemente en más apps que usan estos drawables
#                   públicos. Sesión 2026-07-13.
apply frameworks/base                 frameworks_base_titlebar_ninepatch.patch

# packages/apps/Camera — adaptaciones Y210 al lifecycle de cámara
apply packages/apps/Camera            packages_apps_Camera.patch

# packages/apps/PackageInstaller — mismo defecto de nine-patch en su propia
#                                   copia de title_bar_medium (ver arriba).
apply packages/apps/PackageInstaller  packages_apps_PackageInstaller.patch

# packages/apps/Tag — mismo defecto de nine-patch en su propia copia de
#                      title_bar_medium (ver arriba).
apply packages/apps/Tag               packages_apps_Tag.patch

# packages/apps/Contacts — mismo defecto de nine-patch en su propia copia de
#                           title_bar_shadow (ver arriba).
apply packages/apps/Contacts          packages_apps_Contacts.patch

# packages/apps/FM — app FM radio: config, FMRadio, FMRadioService, FmSharedPreferences
apply packages/apps/FM                packages_apps_FM.patch

# packages/apps/Settings — APN editor / APN settings para Claro Perú
apply packages/apps/Settings          packages_apps_Settings.patch

# vendor/cyanogen — APN Claro Perú (recortado: se saco un hunk que borraba
# ~60 entradas de products/AndroidProducts.mk y rompia breakfast/lunch de
# otros dispositivos del arbol sin dar ningun beneficio a y210)
apply vendor/cyanogen                 vendor_cyanogen.patch
copy_new vendor_cyanogen_y210.mk      vendor/cyanogen/products/cyanogen_y210.mk
}

echo "=== Verificando que todos los patches aplican limpio (sin tocar el árbol) ==="
run_all
echo "=== Verificación OK, aplicando ==="
CHECK_ONLY=0
run_all
echo "=== Todos los patches aplicados ==="
