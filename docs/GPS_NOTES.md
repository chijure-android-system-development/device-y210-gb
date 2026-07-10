# Huawei Y210 (CM7) — GPS Notes

## Hardware

- Chipset: Qualcomm MSM7225A integrado (mismo die que el modem)
- Interfaz: ONCRPC sobre `/dev/oncrpc/` — el GPS corre como servicio en el modem ARM9
- Programa RPC: `LOC_APIPROG = 0x3000008c`, versión `0x00050001`
- HAL: `gps.y210.so` — `hardware/qcom/gps`, generado con `gen-50000`/`inc-50000`

## Estado actual

- Motor GPS iniciado (`Loc API RPC client initialized`): **OK**
- XTRA (efemérides asistidas): **OK** — 41 partes descargadas e inyectadas al arranque
- NTP / UTC injection: **OK** — servidor SNTP responde y se inyecta con éxito
- AGPS (UMTS SLP): **OK** — servidor configurado vía `RPC_LOC_IOCTL_SET_UMTS_SLP_SERVER_ADDR`
- Fix real en exteriores: **Pendiente** — requiere prueba a cielo abierto

## Bugs resueltos

### Bug 1 — `cannot create RPC client` (GID 3006 faltante)

**Síntoma:** `gps.y210` fallaba al inicio con `Error: cannot create RPC client.`

**Causa:** `system_server` no tenía el grupo suplementario `qcom_oncrpc` (GID 3006).
Los nodos `/dev/oncrpc/` tienen modo `0660` con grupo `qcom_oncrpc`, por lo que
`clnt_create()` fallaba al abrir el socket RPC.

**Fix:** `frameworks/base/core/java/com/android/internal/os/ZygoteInit.java` — agregar
GID 3006 a la línea `--setgroups=` del lanzamiento de `system_server`:

```java
// Antes:
"--setgroups=1001,1002,...,3001,3002,3003",
// Después:
"--setgroups=1001,1002,...,3001,3002,3003,3006",
```

Este fix va en `device/huawei/y210/patches/frameworks_base.patch`.

**Verificación:**
```bash
adb shell "ls -l /dev/oncrpc/ | grep 3000008c"
# crw-rw---- root     qcom_oncrpc ...  3000008c:00050001
adb logcat -d | grep "Loc API RPC client initialized"
```

### Bug 2 — versión `0x00050000` vs `0x00050001` (FALSA PISTA — descartada)

Durante el debug se observó que el nodo del modem aparecía como
`/dev/oncrpc/3000008c:00050000` en algunos dumps, lo que llevó a pensar que
`clnt_create()` necesitaba la versión `0x00050000`.

Se parcheó `gen-50000/loc_api_rpc_glue.c` para usar `0x00050000` y se creó un
mecanismo `BOARD_VENDOR_QCOM_GPS_LOC_API_GLUE_VERSION` en `Android.mk`.

**Conclusión:** el fix de versión **no era necesario**. Una vez resuelto el Bug 1
(GID 3006), el stack GPS funciona con `LOC_APIVERS = 0x00050001` sin cambio alguno
en `hardware/qcom/gps`. El nodo del kernel acepta la conexión independientemente
del sufijo de versión en el nombre del device node.

El parche a `gen-50000` fue revertido; `hardware/qcom/gps` queda sin modificar.

## Configuración en BoardConfig.mk

```makefile
BOARD_USES_QCOM_GPS := true
BOARD_VENDOR_QCOM_GPS_LOC_API_AMSS_VERSION := 50000
BOARD_VENDOR_QCOM_GPS_LOC_API_HARDWARE := y210
```

## Configuración en device_y210.mk / gps.conf

- `gps.conf` leído desde `/etc/gps.conf` al arranque
- Servidor AGPS UMTS SLP configurado por el framework desde `gps.conf`

## Comandos de diagnóstico

```bash
# Estado del motor GPS
adb logcat -v time -d | grep -iE "loc_api_rpc|Loc API RPC|cannot create"

# XTRA y NTP
adb logcat -v time -d | grep -iE "xtra|inject_xtra|INJECT_UTC"

# Nodos ONCRPC del modem
adb shell "ls -l /dev/oncrpc/ | grep 3000008c"

# Capacidades reportadas al framework
adb logcat -v time -d | grep "set_capabilities_callback"

# Grupo qcom_oncrpc del system_server (debe aparecer 3006)
adb shell "cat /proc/$(adb shell pidof system_server)/status | grep Groups"
```

## Pendiente

- Fix real (satélites) en exteriores — prueba a cielo abierto sin obstáculos.
  Cuando se obtenga fix, actualizar `FUNCTION_MATRIX.md`:
  `GPS → Fix + ubicación real: OK`
