#!/system/bin/sh

# Compatibility wrapper for init.huawei.rc hciattach service.
# The Y210 uses Broadcom firmware via brcm_patchram_plus.

exec /system/etc/bluetooth/init.bcm.bt.sh
