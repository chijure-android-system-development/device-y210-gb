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

- `init.svc.qmuxd=restarting` + `RADIO_UNAVAILABLE` (baseband vacío) aunque `rild` corra:
  - causa típica: falta `/data/radio` (qmuxd/qmiproxy no pueden crear/bindear sus sockets locales).
  - fix inmediato (runtime):
    - `adb shell "mkdir -p /data/radio; chown radio.radio /data/radio; chmod 0770 /data/radio"`
    - `adb shell "stop qmuxd; stop qmiproxy; stop ril-daemon; start qmuxd; start qmiproxy; start ril-daemon"`
  - fix permanente: asegurar `mkdir /data/radio 0770 radio radio` en el ramdisk (`device/huawei/y210/prebuilt/init.huawei.rc`)
    tanto en `on post-fs-data` como en el trigger `on property:ro.baseband="msm"` (por si el orden de triggers cambia tras wipe).
  - validación:
    - `adb shell getprop init.svc.qmuxd` -> `running`
    - `adb shell getprop gsm.version.baseband` -> valor (ej. `109808`)

- `init.svc.ril-daemon=running` pero `gsm.version.baseband` vacío y `RADIO_NOT_AVAILABLE`:
  - falta `qmuxd/qmiproxy` o `/data/radio` en el arranque
  - permisos rotos en `/dev/oncrpc/*` (ver `system/core/rootdir/ueventd.rc` / `ueventd.huawei.rc`)

- `rild` reinicia/“Hit EOS reading message length” repetido:
  - suele indicar que el lado Java se desconecta porque el estado de radio cae a “not available”
    (seguir el `logcat -b radio` con `persist.radio.adb_log_on=1` para ver la causa real).

- `init.svc.ril-daemon=running` pero UI muestra `No service` (`mServiceState=3`) y `gsm.sim.state=UNKNOWN`:
  - workaround inmediato: `adb shell stop ril-daemon; adb shell start ril-daemon`
  - revisar en `logcat -b radio` si el modem está mandando `UNSOL_RESPONSE_DATA_NETWORK_STATE_CHANGED (1037)`

Datos móviles (fix 2026-04-12)
------------------------------

Síntoma:

- Operador/registros OK (`gsm.operator.alpha/numeric`, `HSPA`), `rild` activo.
- APN existe (ej. Claro PE `claro.pe`), `rmnet0` puede levantar IP por DHCP.
- Aun así, la UI queda en `CONNECTING` o se cae `com.android.phone` y termina en `No service`.

Evidencia (logcat main):

- Crash del hilo `GsmDataConnection-*`:
  - `java.lang.NumberFormatException: unable to parse 'null' as integer`
  - `at com.android.internal.telephony.DataConnection.onSetupConnectionCompleted(DataConnection.java:422)`

Causa real:

- El blob `libril-qc-1.so` devuelve una respuesta de `RIL_REQUEST_SETUP_DATA_CALL` que no coincide con lo que
  espera CM7 en `DataConnection`:
  - En vez de `response[0]=cid`, `response[1]=ifname`, `response[2]=ip`, el `cid` llega como `null` y los campos
    útiles aparecen más adelante (se observó `rmnet0` e IP en los índices 4 y 5).
- Esto dispara el `parseInt(null)` y mata el proceso Phone, cortando datos y a veces la señal.

Fix aplicado:

1) Normalización en framework (evita el crash y toma `ifname/ip` correctos)

- Archivo: `frameworks/base/telephony/java/com/android/internal/telephony/DataConnection.java`
- Lógica:
  - Detecta la forma "malformada" (`response[0]==null` y `response[4]/[5]` presentes)
  - Fuerza `cid=0` y usa `response[4]` como `ifname` y `response[5]` como `ip`
  - DNS/gateway preferidos desde `net.<ifname>.dns1/dns2/gw` y fallback a strings de la respuesta si vienen
    poblados.

2) Hardening en `libril` (evita SEGV por punteros basura)

- Archivo: `hardware/ril/libril/ril.cpp`
- Lógica:
  - `sanitizeRilString()` descarta punteros sospechosos (valores muy pequeños como `0x2`) antes de
    `writeStringToParcel()`.
  - Se aplica en `responseStrings()`, `responseString()` y `responseDataCallList()`.

Validación (post-boot)

- Estado datos:
  - `adb shell dumpsys connectivity | grep -n \"Active network\\|type: mobile\\|state\\|extra\"`
- Interfaz:
  - `adb shell ip addr show rmnet0`
  - `adb shell ip route show`
- Props red:
  - `adb shell getprop net.rmnet0.gw`
  - `adb shell getprop net.rmnet0.dns1`
  - `adb shell getprop net.rmnet0.dns2`
- Prueba conectividad:
  - `adb shell ping -c 2 -w 8 google.com`
  - Nota: algunos carriers filtran ICMP a `8.8.8.8`, no usarlo como prueba unica.

Clase RIL Java: HuaweiQualcommRIL nunca se usaba (fix 2026-07-06)
------------------------------------------------------------------

Síntoma:

- `rild` y el registro de red funcionan bien (confirmado en equipo real:
  `mServiceState=0 home Claro Claro 71610 HSPA`), pero `gsm.sim.state` queda
  siempre en `UNKNOWN`, y `gsm.sim.operator.numeric`/`.alpha` quedan vacíos
  (rompía el llenado automático de APN y el filtro de la lista de APNs en
  Settings, ver `packages_apps_Settings.patch`).

Causa real (root cause, confirmada leyendo `PhoneFactory.java`):

- `cyanogen_y210.mk` fija `ro.telephony.ril_class=HuaweiQualcommRIL`, pero
  `PhoneFactory.java` (el que decide qué clase Java de RIL instanciar) es una
  cadena fija de `if/else` con strings literales (`"samsung"`, `"htc"`,
  `"lgestar"`, `"semc"`, `"lgeqcom"`, `"mototegra"`, etc.) — **no existía
  ninguna rama para `"HuaweiQualcommRIL"`**, así que caía al `else` final:
  `new RIL(...)`, la clase genérica.
- Ya existía además un archivo real
  `device/huawei/y210/ril/telephony/java/com/android/internal/telephony/HuaweiQualcommRIL.java`
  (idéntico al de `device/huawei/u8815-gingerbread/ril/...`, ambos claramente
  copiados de un device tree Huawei real de CyanogenMod 2011) con overrides de
  protocolo específicos de Huawei (manejo de `mAid` en `getIMSI`/`iccIO`/
  `queryFacilityLock`/`setFacilityLock`, `responseIccCardStatus` con layout de
  parcel distinto al genérico). Ese archivo **ya se compilaba** dentro de
  `framework.jar` gracias a `device_y210.mk:7`:
  ```
  FRAMEWORKS_BASE_SUBDIRS += ../../$(LOCAL_PATH)/ril/
  ```
  (mecanismo de `build/core/pathmap.mk` para que un device tree inyecte
  fuentes Java al framework sin tocar `frameworks/base`). O sea: la clase
  existía y se compilaba, pero **nunca se instanciaba** — quedó a medio
  conectar, probablemente por la misma persona/sesión que dejó el `ril_class`
  puesto sin terminar el resto.

Fix aplicado:

- Archivo: `frameworks/base/telephony/java/com/android/internal/telephony/PhoneFactory.java`
- Se agregó la rama que faltaba, justo antes del `else` final:
  ```java
  } else if ("HuaweiQualcommRIL".equals(sRILClassname)) {
      Log.i(LOG_TAG, "Using Huawei Qualcomm RIL");
      sCommandsInterface = new HuaweiQualcommRIL(context, networkMode, cdmaSubscription);
  } else {
  ```
- **No** se tocó `HuaweiQualcommRIL.java` ni `QualcommNoSimReadyRIL.java` (la
  clase padre) — quedan tal cual estaban en el device tree.
- Capturado en `device/huawei/y210/patches/frameworks_base_ril_class.patch`
  (3 líneas, verificado que aplica limpio sobre el árbol base) y agregado a
  `apply-patches.sh`.

Resultado validado en equipo real:

- Confirmado que la clase queda enlazada en el build (`strings` sobre el
  `classes.dex` de `framework.jar` muestra la referencia directa a
  `Lcom/android/internal/telephony/HuaweiQualcommRIL;`).
- Arranque estable, sin crashes de `system_server`/`phone`.
- Registro de red sigue funcionando igual de bien: `mServiceState=0 home
  Claro Claro 71610 HSPA`.

Pendiente (no resuelto todavía):

- `gsm.sim.state` **sigue en `UNKNOWN`** después del fix. `QualcommNoSimReadyRIL`
  ajusta el `RadioState` interno del RIL (a partir de
  `RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED`), pero esa property la actualiza
  la máquina de estados de `IccCard`/`SimCard` en otra capa, que no toca esta
  clase. Siguiente paso: revisar `IccCard.java`/`SIMRecords.java` (o
  equivalente en este árbol) para encontrar qué evento/unsolicited necesita
  llegar bien para que esa máquina de estados marque `SIM_STATE_READY`.
- Validación rápida sugerida para la próxima sesión:
  - `adb logcat -b radio -d | grep -i "IccCard\\|SIM_STATE\\|CommandsInterface.RadioState"`
  - Revisar si `QualcommNoSimReadyRIL.IccHandler` (el hilo interno que
    consulta el estado del ICC) realmente se dispara y con qué resultado.
