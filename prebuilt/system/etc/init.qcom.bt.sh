#!/system/bin/sh
# Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.

BLUETOOTH_SLEEP_PATH=/proc/bluetooth/sleep/proto
LOG_TAG="qcom-bluetooth"
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
  /system/bin/hciattach -n $BTS_DEVICE $BTS_TYPE $BTS_BAUD &
  hciattach_pid=$!
  logi "start_hciattach: pid = $hciattach_pid"
}

kill_hciattach ()
{
  logi "kill_hciattach: pid = $hciattach_pid"
  kill -TERM $hciattach_pid
}

USAGE="hciattach [-n] [-p] [-b] [-t timeout] [-s initial_speed] <tty> <type | id> [speed] [flow|noflow] [bdaddr]"

while getopts "blnpt:s:" f
do
  case $f in
  b | l | n | p) opt_flags="$opt_flags -$f" ;;
  t) timeout=$OPTARG ;;
  s) initial_speed=$OPTARG ;;
  \?) echo $USAGE; exit 1 ;;
  esac
done
shift $(($OPTIND-1))

POWER_CLASS=`getprop qcom.bt.dev_power_class`
TRANSPORT=`getprop ro.qualcomm.bt.hci_transport`

# Stock init.qcom.sh seeds this to class 2. Keep that behavior here locally
# because this port does not currently set it elsewhere.
if [ -z "$POWER_CLASS" ]; then
  POWER_CLASS=2
fi

# This target only works if the BT block is powered before hci_qcomm_init.
if [ -e /sys/class/rfkill/rfkill0/state ]; then
  echo 1 > /sys/class/rfkill/rfkill0/state
fi

if [ -e /sys/module/bluetooth_power/parameters/power ]; then
  echo 1 > /sys/module/bluetooth_power/parameters/power
fi

# Give the controller time to power up before vendor init.
sleep 1

logi "Transport : $TRANSPORT"

case $POWER_CLASS in
  1) PWR_CLASS="-p 0"
     logi "Power Class: 1" ;;
  2) PWR_CLASS="-p 1"
     logi "Power Class: 2" ;;
  3) PWR_CLASS="-p 2"
     logi "Power Class: CUSTOM" ;;
  *) PWR_CLASS=""
     logi "Power Class: Ignored. Default(1) used (1-CLASS1/2-CLASS2/3-CUSTOM)"
     logi "Power Class: To override, Before turning BT ON; setprop qcom.bt.dev_power_class <1 or 2 or 3>" ;;
esac

eval $(/system/bin/hci_qcomm_init -e $PWR_CLASS && echo "exit_code_hci_qcomm_init=0" || echo "exit_code_hci_qcomm_init=1")

case $exit_code_hci_qcomm_init in
  0) logi "Bluetooth QSoC firmware download succeeded, $BTS_DEVICE $BTS_TYPE $BTS_BAUD $BTS_ADDRESS" ;;
  *) failed "Bluetooth QSoC firmware download failed" $exit_code_hci_qcomm_init ;;
esac

trap "kill_hciattach" TERM INT

case $TRANSPORT in
  "smd")
    logi "Seting property to insert the hci smd transport module"
    setprop bt.hci_smd.driver.load 1
    ;;
  *)
    logi "start hciattach"
    start_hciattach
    wait $hciattach_pid
    logi "Bluetooth stopped"
    ;;
esac

exit 0
