#!/bin/bash
# revert-patches.sh — deshace todos los parches del Y210 del árbol CM7.
# Usar antes de iniciar un port nuevo (e.g. LG E400) sobre el mismo árbol.
# Ejecutar desde la raíz del árbol de CM7.
#
# Uso:
#   cd /path/to/cm7
#   bash device/huawei/y210/patches/revert-patches.sh

set -e

PATCHES_DIR="$(dirname "$0")"
CM7_ROOT="$(pwd)"

# Revierte un parche aplicado al working tree (no committeado).
revert_wt() {
    local project="$1"
    local patch="$2"
    echo "  Revirtiendo $patch en $project..."
    (cd "$CM7_ROOT/$project" && git apply --reverse "$CM7_ROOT/$PATCHES_DIR/$patch")
}

# Revierte commits Y210 haciendo reset al commit base indicado.
revert_commits() {
    local project="$1"
    local base_commit="$2"
    echo "  Reset $project a $base_commit..."
    git -C "$CM7_ROOT/$project" reset --hard "$base_commit"
}

# Elimina un archivo copiado por apply-patches.sh.
remove_new() {
    local dst="$1"
    echo "  Eliminando archivo copiado: $dst"
    rm -f "$CM7_ROOT/$dst"
}

echo "=== Revirtiendo parches Y210 ==="

# hardware/ril — working tree
revert_wt hardware/ril hardware_ril.patch

# hardware/libhardware_legacy — working tree
revert_wt hardware/libhardware_legacy hardware_libhardware_legacy.patch

# hardware/msm7k — 3 commits Y210 (copybit fixes)
# Base: 5336b50 (msm7k: copybit: fix YUV blit order)
revert_commits hardware/msm7k 5336b50

# system/netd — working tree
revert_wt system/netd system_netd.patch

# frameworks/base — 3 commits Y210 (NV21 preview, cameraservice, StagefrightRecorder)
# Base: aa02ad1445e (frameworks_base : Updated French Translations)
revert_commits frameworks/base aa02ad1445e
# Archivos nuevos y FM Java (working tree, post-reset)
remove_new frameworks/base/core/jni/android_hardware_fm_qcom.cpp
# Nota: los 3 FM Java quedan limpios tras el reset --hard (son working tree sobre HEAD)

# packages/apps/Camera — 3 commits Y210 (CameraHolder, startPreview, log cleanup)
# Base: 434f8777 (Camera : Updated Slovak translation)
revert_commits packages/apps/Camera 434f8777

# packages/apps/FM — working tree
revert_wt packages/apps/FM packages_apps_FM.patch

# packages/apps/Settings — working tree
revert_wt packages/apps/Settings packages_apps_Settings.patch

# vendor/cyanogen — working tree + archivo nuevo
revert_wt vendor/cyanogen vendor_cyanogen.patch
remove_new vendor/cyanogen/products/cyanogen_y210.mk

echo "=== Parches Y210 revertidos ==="
echo ""
echo "Siguiente paso: crear device/<fabricante>/<codename>/ para el nuevo port."
