#!/system/bin/sh

# Huawei's legacy hostapd reads /dev/random for WPA handshakes. On this kernel
# it can expose too little entropy and reject the first 4-way handshake, which
# clients report as an incorrect password.
if [ ! -L /dev/random ]; then
    mv /dev/random /dev/random.real 2>/dev/null
    ln -s /dev/urandom /dev/random
fi

exec /system/bin/hostapd /data/misc/wifi/hostapd.conf
