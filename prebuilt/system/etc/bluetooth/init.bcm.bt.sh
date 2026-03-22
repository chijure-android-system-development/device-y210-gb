#!/system/bin/sh
# Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
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
#/*< DTS2011081800819 zhangyun 20110818 begin*/

BLUETOOTH_SLEEP_PATH=/proc/bluetooth/sleep/proto
CHIP_POWER_PATH=/sys/class/rfkill/rfkill0/state
LOG_TAG="bcm-bluetooth"
LOG_NAME="${0}:"

hciattach_pid=""

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

start_hciattach ()
{
  echo "start_hciattach +"
  # Power the Broadcom combo chip before patchram; some kernels do not
  # expose the legacy bluetooth_power sysfs path but do provide rfkill.
  if [ -e "$CHIP_POWER_PATH" ]; then
    echo 1 > "$CHIP_POWER_PATH"
  else
    loge "missing chip power path: $CHIP_POWER_PATH"
  fi

  # Older Huawei userspace expected this proc node, but the current kernel
  # may not expose it. Missing it should not block HCI bring-up.
  if [ -e "$BLUETOOTH_SLEEP_PATH" ]; then
    echo 1 > "$BLUETOOTH_SLEEP_PATH"
  else
    logi "skip sleep enable, path missing: $BLUETOOTH_SLEEP_PATH"
  fi
  echo "start_hciattach pid"
  if [ -x /system/bin/hciattach_check ]; then
    /system/bin/hciattach_check /dev/ttyHS0 any 3000000 flow &
  else
    /system/bin/brcm_patchram_plus -d --enable_hci --enable_lpm --baudrate 3000000 --bd_addr 00:18:82:23:76:1d --patchram /system/etc/bluetooth/BCM4330.hcd /dev/ttyHS0 &
  fi

  hciattach_pid=$!
  loge "start_hciattach: pid = $hciattach_pid"
  echo "start_hciattach -"
}

kill_hciattach ()
{
  logi "kill_hciattach: pid = $hciattach_pid"
  ## careful not to kill zero or null!
  kill -TERM $hciattach_pid
  if [ -e "$BLUETOOTH_SLEEP_PATH" ]; then
    echo 0 > "$BLUETOOTH_SLEEP_PATH"
  fi
  if [ -e "$CHIP_POWER_PATH" ]; then
    echo 0 > "$CHIP_POWER_PATH"
  fi
  # this shell doesn't exit now -- wait returns for normal exit
  logi "kill_hciattach  -"
}


# init does SIGTERM on ctl.stop for service
trap "kill_hciattach" TERM INT

start_hciattach

wait $hciattach_pid

logi "Bluetooth stopped"

exit 0
#/* DTS2011081800819 zhangyun 20110818 end >*/
