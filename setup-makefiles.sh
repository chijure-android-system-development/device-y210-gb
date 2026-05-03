#!/bin/sh

# Regenerate the small vendor stubs for the Huawei Y210 tree.
#
# Note: the proprietary blob list is maintained in
# `vendor/huawei/y210/y210-vendor-blobs.mk` (generated elsewhere).

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
# Intentionally empty.
# Vendor-specific board overrides are not needed for Y210 right now.
EOF

cat > "$OUTDIR/AndroidBoardVendor.mk" <<'EOF'
# Intentionally empty.
# Vendor-specific board overrides are not needed for Y210 right now.
EOF

cat > "$OUTDIR/Android.mk" <<'EOF'
# Intentionally empty.
# The Y210 vendor make logic lives in y210-vendor.mk and y210-vendor-blobs.mk.
EOF
