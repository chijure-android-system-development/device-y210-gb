# FM Radio (Y210)

## Estado actual (CM7)

- `fm_dl` ya completa y deja `hw.fm.init=1` (download/calibración OK).
- Pendiente: audio (en algunos casos la radio “enciende” pero no suena).

## Fix aplicado (init.qcom.fm.sh)

Para que `fm_dl` sea confiable, `init.qcom.fm.sh` ahora hace:

1) `exec 3</dev/radio0` (power-up del transceiver desde el driver)
2) `fm_qsoc_patches $hw.fm.version 0` (download/calibración)
3) `exec 3<&-` (cierra FD)

Esto evita el error temprano `I2C slave addr:0x2a not connected` en `dmesg` y permite que `setprop ctl.start fm_dl`
termine con `hw.fm.init=1`.

## Audio: V4L2 Mute (causa típica de “no suena”)

En el driver tavarua del Y210, `V4L2_CID_AUDIO_MUTE (0x00980909)` es **boolean**:

- `0` = unmute
- `1` = mute (default)

El framework (CAF) estaba seteando `3/4`, lo que fallaba y dejaba el chip en mute.

Fix: `frameworks/base/core/java/android/hardware/fmradio/FmRxControls.java` ahora usa `on ? 1 : 0`.

## Audio: Routing (si “enciende” pero no suena)

Además del unmute, en Y210 el audio FM (analógico) debe rutearse por los paths normales del codec:

- `SND_DEVICE_HEADSET` cuando hay audífonos (antena)
- `SND_DEVICE_SPEAKER` si se fuerza altavoz

En `device/huawei/y210/libaudio/AudioHardware.cpp` se ajustó el routing de `mFmRadioEnabled` para usar
esos devices (stock-like) y no los endpoints `SND_DEVICE_FM_*`, y se mantiene `/dev/msm_fm` abierto mientras FM está activo.

## ¿Qué device usa `fm_qsoc_patches`?

`fm_qsoc_patches` habla por **I2C userspace**, no por V4L2:

- Abre `/dev/i2c-1` (se ve en `/data/app/fm_dld_enable`: `opened FM i2c device node : /dev/i2c-1`).
- El “power-up” previo con `/dev/radio0` es solo para sacar el transceiver de reset/low-power antes del acceso I2C.

## Props mínimas (stock-like)

En Y210 stock el valor de `hw.fm.version` es **`67240453`** (hex `0x4020205`, coincide con `Chip ID 4020205` que reporta el driver).

Recomendado:

- `hw.fm.version=67240453`
- `hw.fm.mode=normal`
- `hw.fm.isAnalog=false`

## Validación rápida

1) Logs del init:

- `adb shell setprop ctl.start fm_dl`
- `adb shell getprop hw.fm.init` (esperado `1`)
- `adb shell cat /data/app/fm_dld_enable`

2) Kernel:

- `adb shell dmesg | grep -i radio-tavarua`

## Comparación stock vs CM7 (hallazgo clave)

- En stock, `fm_qsoc_patches` completa y escribe `fm_qsoc_patches succeeded: 0`.
- En CM7, con un kernel distinto (misma rama `2.6.38.6-perf` pero build diferente), el proceso se queda en timeout esperando `0x00010000`.

Si quieres, la forma más rápida de aislarlo es bootear CM7 con **kernel stock** + ramdisk CM7 (boot.img mixto) y re-probar `fm_dl`.
