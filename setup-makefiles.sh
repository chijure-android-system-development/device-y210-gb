#!/bin/sh

# Regenerate the small vendor stubs for the Huawei Y210 tree.
# The blob list itself is kept in vendor/huawei/y210/y210-vendor-blobs.mk.

set -eu

VENDOR=huawei
DEVICE=y210
OUTDIR="../../../vendor/$VENDOR/$DEVICE"

mkdir -p "$OUTDIR"

cat > "$OUTDIR/$DEVICE-vendor.mk" <<'EOF'
# Auto-generated vendor makefile
$(call inherit-product, vendor/huawei/y210/y210-vendor-blobs.mk)
EOF

cat > "$OUTDIR/BoardConfigVendor.mk" <<'EOF'
# Auto-generated BoardConfigVendor for y210
USE_CAMERA_STUB := false
EOF

cat > "$OUTDIR/Android.mk" <<'EOF'
# Intentionally empty.
# The Y210 vendor make logic lives in y210-vendor.mk and y210-vendor-blobs.mk.
EOF
