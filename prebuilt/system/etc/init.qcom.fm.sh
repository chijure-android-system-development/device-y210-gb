#!/system/bin/sh
# Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of Code Aurora nor
#       the names of its contributors may be used to endorse or promote
#       products derived from this software without specific prior written
#       permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

setprop hw.fm.init 0

mode=`getprop hw.fm.mode`
version=`getprop hw.fm.version`
isAnalog=`getprop hw.fm.isAnalog`

#find the transport type
TRANSPORT=`getprop ro.qualcomm.bt.hci_transport`

LOG_TAG="qcom-fm"
LOG_NAME="${0}:"

loge ()
{
  /system/bin/log -t $LOG_TAG -p e "$LOG_NAME $@"
}

logi ()
{
  /system/bin/log -t $LOG_TAG -p i "$LOG_NAME $@"
}

failed ()
{
  loge "$1: exit code $2"
  exit $2
}

logi "In FM shell Script"
logi "mode: $mode"
logi "version: $version"
logi "isAnalog: $isAnalog"
logi "Transport : $TRANSPORT"

#if it is smd transport insert the transport module and exit from the script
case $TRANSPORT in
    "smd")
        echo "inserting the radio transport module"
        insmod /system/lib/modules/radio-iris-transport.ko
        setprop hw.fm.init 1
        exit 0
     ;;
     *)
        logi "not a smd transport case, need patch download"
	     ;;
esac

# Power up the FM transceiver before running the I2C-based firmware download.
# On Y210 we've seen fm_qsoc_patches fail early with
# "I2C slave addr:0x2a not connected" unless /dev/radio0 is opened first
# (this brings the chip out of low-power/reset).
HAVE_RADIO_FD=0
if [ -e /dev/radio0 ]; then
    if exec 3</dev/radio0; then
        HAVE_RADIO_FD=1
        logi "Opened /dev/radio0 to power FM"
        # Give the driver a moment to bring the chip up.
        sleep 1
    else
        logi "Failed to open /dev/radio0 to power FM"
    fi
fi

#$fm_qsoc_patches <fm_chipVersion> <enable/disable WCM>
#
case $mode in
  "normal")
     /system/bin/fm_qsoc_patches $version 0
     ;;
  "wa_enable")
   /system/bin/fm_qsoc_patches $version 1
     ;;
  "wa_disable")
   /system/bin/fm_qsoc_patches $version 2
     ;;
  "config_dac")
   /system/bin/fm_qsoc_patches $version 3 $isAnalog
     ;;
   *)
    logi "Shell: Default case"
    /system/bin/fm_qsoc_patches $version 0
    ;;
esac

exit_code_fm_qsoc_patches=$?

if [ "$HAVE_RADIO_FD" = "1" ]; then
    exec 3<&-
fi

case $exit_code_fm_qsoc_patches in
   0)
		logi "FM QSoC calibration and firmware download succeeded"
   ;;
  *)
	failed "FM QSoC firmware download and/or calibration failed" $exit_code_fm_qsoc_patches
   ;;
esac

setprop hw.fm.init 1

exit 0
