RIL / Baseband Notes (Y210 / MSM7x27A, CM7)
==========================================

Estado actual
-------------

- `rild` funcionando (socket + radio stack activo).
- Baseband reporta valor (ej. `gsm.version.baseband=109808`).
- Registro de operador funciona (ej. `gsm.operator.numeric=71610`, `gsm.operator.alpha=Claro`).
- IMEI disponible vía PhoneSubInfo (ver “Validación rápida”).

Si algo de lo anterior vuelve a romperse tras reboot, casi siempre es porque no se flasheó el ZIP correcto
(ver “Verificación de build instalada”).

Arquitectura (qué necesita el Y210)
-----------------------------------

El Y210 usa el stack Qualcomm “legacy” (QCRIL) y depende de:

- `rild` (daemon) + blob RIL Qualcomm (`/system/lib/libril-qc-1.so` o blob equivalente).
- ONCRPC (`/dev/oncrpc/*`) + `oem_rpc_svc`.
- QMI helpers (`qmuxd` + `qmiproxy`) y el directorio `/data/radio` para sockets locales.

Sin QMI helpers, el RIL puede arrancar pero quedarse en `RADIO_UNAVAILABLE` o no poder inicializar UIM/SIM.

Cambios clave que hicieron que levante radio
--------------------------------------------

1) Propiedades RIL stock (selección del blob y dispositivo)

- `rild.libpath=/system/lib/libril-qc-1.so`
- `rild.libargs=-d /dev/smd0`

Fuente: `device/huawei/y210/system.prop` y referencia stock `device/huawei/y210/props.txt`.

2) QMI helpers en el ramdisk + `/data/radio`

El arranque debe asegurar:

- `mkdir /data/radio 0770 radio radio` (en `on post-fs-data`)
- servicios `qmuxd` y `qmiproxy` disponibles (y arrancados para `ro.baseband="msm"`)

Esto evita fallas tipo:

- `Could not register successfully with QMI UIM Service`
- `ASSERTION FAILED ... qcril_uim.qmi_handle >= 0`

3) Compatibilidad con unsolicited Qualcomm (1031–1037)

Algunos blobs Qualcomm envían unsolicited “extra” (1031–1037). Para evitar spam/errores,
se mantienen mapeados en `libril` (C) como entradas numéricas (sin depender de un `ril.h` moderno).

Nota Y210/QCRIL:

- En este dispositivo se observa `RIL_UNSOL_RESPONSE_DATA_NETWORK_STATE_CHANGED (1037)` en `logcat -b radio`.
  Si el framework no lo reconoce/no notifica, la UI puede quedarse en `No service` hasta reiniciar el RIL.

Validación rápida (runtime)
---------------------------

Desde host:

- Estado general RIL:
  - `adb shell getprop | egrep "rild\\.libpath|rild\\.libargs|init\\.svc\\.(ril-daemon|oem_rpc_svc)"`
  - `adb shell ls -l /dev/socket | egrep "rild"`

- Baseband + operador:
  - `adb shell getprop gsm.version.baseband`
  - `adb shell getprop gsm.operator.numeric`
  - `adb shell getprop gsm.operator.alpha`
  - `adb shell getprop gsm.network.type`

- IMEI (binder; útil aunque UI falle):
  - `adb shell service call iphonesubinfo 1`

- Logs radio (modo debug QCRIL):
  - `adb shell setprop persist.radio.adb_log_on 1`
  - `adb logcat -b radio -d | tail -n 200`

Verificación de build instalada (para evitar “me funcionó y luego se rompió”)
----------------------------------------------------------------------------

Cuando radio funciona “a mano” pero se rompe al reiniciar, típicamente el ramdisk no trae los servicios QMI
o falta crear `/data/radio` en `post-fs-data`.

Checklist post-flash:

- `adb shell grep -n "/data/radio\\|service qmuxd\\|service qmiproxy" /init.huawei.rc`
- `adb shell ls -ld /data/radio && adb shell ls -l /data/radio | head`

Si el ZIP que flasheaste incluía un framework/ramdisk distinto, verifica también el jar:

- `adb shell md5sum /system/framework/framework.jar`

Troubleshooting (síntomas -> causa probable)
--------------------------------------------

- `init.svc.ril-daemon=running` pero `gsm.version.baseband` vacío y `RADIO_NOT_AVAILABLE`:
  - falta `qmuxd/qmiproxy` o `/data/radio` en el arranque
  - permisos rotos en `/dev/oncrpc/*` (ver `system/core/rootdir/ueventd.rc` / `ueventd.huawei.rc`)

- `rild` reinicia/“Hit EOS reading message length” repetido:
  - suele indicar que el lado Java se desconecta porque el estado de radio cae a “not available”
    (seguir el `logcat -b radio` con `persist.radio.adb_log_on=1` para ver la causa real).

- `init.svc.ril-daemon=running` pero UI muestra `No service` (`mServiceState=3`) y `gsm.sim.state=UNKNOWN`:
  - workaround inmediato: `adb shell stop ril-daemon; adb shell start ril-daemon`
  - revisar en `logcat -b radio` si el modem está mandando `UNSOL_RESPONSE_DATA_NETWORK_STATE_CHANGED (1037)`
