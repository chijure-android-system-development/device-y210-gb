# CyanogenMod 10 para Huawei Ascend Y210

Device tree para portar CyanogenMod 10 / Android 4.1.2 Jelly Bean al
Huawei Ascend Y210, basado en Qualcomm MSM7x27A. Portado desde el árbol
CM9 (ICS) maduro de este mismo dispositivo, reusando `device/`, `vendor/` y
`kernel/huawei/y210` tal cual y adaptando el userland a JB.

Depurado de forma iterativa con ADB/TWRP contra hardware real durante todo
el port — cada fix de este documento fue diagnosticado en vivo (logcat,
dmesg, dumpsys, tombstones) contra el device conectado, no solo inferido
leyendo código.

## Hardware

| Campo | Valor |
| --- | --- |
| Dispositivo | Huawei Ascend Y210 |
| Codename | `y210` |
| SoC | Qualcomm MSM7x27A (ARMv7 Cortex-A5, single core, up to 1.008 GHz — confirmed via `cpuinfo_max_freq`) |
| GPU | Adreno 200 |
| RAM física | 256 MB (~165 MB usables — el resto lo reserva el firmware ARM9/AMSS antes de que arranque Linux) |
| Pantalla | HVGA 320x480, mdpi |
| Baseband | Qualcomm "legacy" QCRIL (`rild` + blob `libril-qc-1.so`) |
| WiFi | Atheros AR6003 (ar6000/ath6kl, SDIO) |
| Bluetooth | Broadcom (`BOARD_HAVE_BLUETOOTH_BCM`) |
| Storage | SD card real vía `mmc0` (no eMMC interna separada) |

## Estado por subsistema (2026-08-25)

| Subsistema | Estado | Notas |
| --- | --- | --- |
| Boot / system_server | ✅ OK | Arranque completo y estable, sin crash-loops |
| RIL / baseband | ✅ OK | `rild` corriendo, baseband `109808`, registro de operador real confirmado ("Claro" en pantalla de bloqueo) |
| Sensores | ✅ OK | Acelerómetro LIS3DH reportando datos reales |
| Audio (salida) | ✅ OK | Confirmado audible tras el fix de `media_codecs.xml` |
| Audio (micrófono) | ❌ Roto (seguro) | `AudioPolicyManagerBase::getInputProfile()` nunca matchea un perfil de entrada, causa raíz sin confirmar — pero ya NO congela/paniquea el sistema (antes sí), falla limpio a nivel de app |
| Cámara (preview) | ✅ OK | Preview en vivo funcional, HAL `camera.y210` carga bien |
| Cámara (captura) | ❌ Roto | Falla en el driver nativo Qualcomm al tomar la foto (`register_buffers`/`native_start_ops`: Operation not permitted) — pendiente |
| Almacenamiento externo | ✅ OK | SD real detectada y montada en `/storage/sdcard0`, confirmada con archivos reales del usuario |
| WiFi | ✅ OK | Confirmado end-to-end: conexión real (WPA2), DHCP, DNS, ping a internet, navegación web real en Browser |
| Bluetooth (JSR-82/rfcomm) | ✅ OK | `BluetoothSocket` ruteado por el path BlueZ real (`FeatureOption.MTK_BT_PROFILE_SPP=false`) — sockets RFCOMM (OPP/PBAP) ya no fallan |
| Bluetooth (descubrimiento real) | ⚠️ Parcial | El chip Broadcom ahora recibe comandos HCI reales (antes: silencio total) tras redirigir a `init.bcm.bt.sh`, pero `brcm_patchram_plus` corta antes de terminar de subir el firmware — `hci0` no queda registrado, el escaneo no encuentra dispositivos todavía |
| Gallery2 / Camera / CMFileManager / Browser | ✅ OK | Reagregados al build (excluidos originalmente por espacio); `system.img` en 170.5 MiB con ~15.5 MiB de margen |
| SIM / datos móviles | ❓ Sin confirmar | Pendiente probar con SIM física insertada |
| Freeze ocasional del sistema | ⚠️ Sin resolver | Reproducido navegando a una carpeta con muchos archivos reales grandes vía CMFileManager — causa raíz no confirmada, a veces autorecupera (kernel panic + reboot), a veces requiere sacar batería |

## Particiones (confirmadas contra `/proc/mtd` en hardware real)

| Montaje | Tamaño real | Notas |
| --- | --- | --- |
| `/system` | ~186 MiB (195,035,136 bytes) | El check de tamaño de `mkyaffs2image` en `make` es más permisivo que esto — validar siempre contra `/proc/mtd`, no confiar en el build |
| `/boot` | 5 MB | |
| `/recovery` | 6 MB | |
| `/cache` | ~58 MB | |
| `/data` | ~160 MB | |

Filesystem: yaffs2 sobre MTD raw (no eMMC/ext4).

## Build

Requiere Java 6 (el host solo tiene versiones más nuevas) → usar el
container Docker `cm10-builder` (Ubuntu 14.04 + `openjdk-6-jdk`).

```bash
docker start cm10-builder   # si está parado
docker exec -u builder cm10-builder bash -lc "
  cd /home/builder/cm10 && source build/envsetup.sh &&
  lunch cm_y210-userdebug && make otapackage -j8
"
```

Solo boot image (mucho más rápido cuando solo cambia `init.rc`/ramdisk/kernel):

```bash
docker exec -u builder cm10-builder bash -lc "
  cd /home/builder/cm10 && source build/envsetup.sh &&
  lunch cm_y210-userdebug && make bootimage -j8
"
```

**Importante**: los cambios hechos con editores de texto normales (no `sed`/`cp` desde shell)
a veces no se reflejan a tiempo dentro del bind mount del container — verificar
siempre `md5sum` del archivo en ambos lados (host y container) antes de compilar
si algo no refleja el cambio esperado. Correr `docker cp <archivo> cm10-builder:<ruta>`
explícito si hay dudas.

## Flash con TWRP

```bash
adb push out/target/product/y210/cm_y210-ota-eng..zip /sdcard/ota.zip
adb shell twrp install /sdcard/ota.zip
adb reboot
```

Para cambios que solo tocan el ramdisk/kernel (`init.rc`, `Makefile` del kernel),
es más rápido armar un zip mínimo de un solo archivo en vez de reflashear el
`system.img` completo:

```
package_extract_file("boot.img", "/tmp/boot.img");
write_raw_image("/tmp/boot.img", "boot");
```

(con `boot.img` + `META-INF/` copiado de cualquier zip completo ya generado).

**Nunca encadenar dos comandos `twrp` sin esperar a que termine el primero**
(`wipe` + `install` en la misma llamada, o dos `install` seguidos) — TWRP
tiene un solo hilo de acción y el segundo comando queda en loop de "Another
threaded action is already running" para siempre. Si pasa: `adb shell kill -9
<pids de twrp>` + `adb reboot recovery`.

## Lecciones clave del port ICS→JB

Esta es la lista de incompatibilidades ICS→JB encontradas y corregidas en
este árbol — útil como checklist si se porta otro dispositivo MSM7x27A
similar desde un árbol CM9:

1. **`FRAMEWORKS_BASE_SUBDIRS`** en `device_y210.mk` para compilar clases
   Java específicas del device (ej. `HuaweiQualcommRIL`) directo dentro de
   `framework.jar`.
2. **`framework2.jar` falta en `BOOTCLASSPATH`** (`init.rc`) — esta rama de
   CM10 separa algunas clases (no solo MTK) en un segundo jar por el límite
   de métodos por dex de Dalvik.
3. **Layout de cgroups cambió**: JB's `libcutils` espera
   `/dev/cpuctl/apps/tasks` y `/dev/cpuctl/apps/bg_non_interactive/tasks`,
   no los paths directos bajo `/dev/cpuctl/` que usaba ICS.
4. **Convención de storage cambió**: JB espera `/storage/sdcard0`, no
   `/mnt/sdcard` — hay que actualizar `vold.fstab`, el `on init` que crea el
   directorio real (`init.huawei.rc`), Y el overlay `storage_list.xml`
   (`MountService` lee cada uno de forma independiente, los tres tienen que
   coincidir).
5. **`media_codecs.xml` es obligatorio en JB** y no existía en absoluto en
   este árbol (ni en CM9) — sin él, `OMXCodec`/`MediaCodecList` fallan para
   CUALQUIER audio/video. Portado desde el árbol JB del Huawei G300 (mismo
   SoC MSM7x27A).
6. **`MultiWaveView` → `GlowPadView`**: el widget de "deslizar para
   desbloquear" cambió de nombre y de atributos entre ICS y JB — el overlay
   de `keyguard_screen_tab_unlock.xml` de este device seguía con la API
   vieja.
7. **`generic_no_telephony.mk` excluye `rild`/`Mms`/`Gallery2`** (perfil
   pensado para tablets) — hay que re-agregarlos explícitamente si el
   dispositivo tiene radio y cámara (la app "Camera" standalone ya no
   existe en esta era de CM10; la UI de cámara vive en Gallery2).
8. **RELRO** (`-Wl,-z,norelro`) en HALs que definen `HAL_MODULE_INFO_SYM`
   — puede caer en el rango protegido por `GNU_RELRO` y crashear al cargar.
9. **`LOGx` → `ALOGx`**: los macros de logging ICS ya no tienen alias hacia
   atrás en este árbol JB.

Ver `/home/chijure/.claude/projects/-home-chijure-cm10/memory/project_cm10_y210_port.md`
para el historial completo, diagnóstico paso a paso, y comandos de debug
usados para encontrar cada uno de estos bugs.

## Debug rápido

```bash
# Estado general
adb wait-for-device
adb shell getprop sys.boot_completed
adb shell dmesg | grep -iE 'cannot find|add_tid_to_cgroup|fatal|panic'
adb logcat -d | grep -iE 'FATAL|ClassNotFoundException|NoClassDefFoundError'

# RIL
adb shell getprop gsm.version.baseband gsm.sim.state gsm.operator.alpha
adb shell ps | grep -E 'rild|qmuxd|qmiproxy'

# Memoria (dispositivo con solo ~165 MB usables)
adb shell cat /proc/meminfo
adb shell cat /sys/module/lowmemorykiller/parameters/minfree

# Audio
adb shell dumpsys media.audio_flinger | grep -E 'total writes|standby'
```

## Fuentes de referencia

```text
/home/chijure/.claude/projects/-home-chijure-cm10/memory/   # memoria detallada de la sesión
/home/chijure/cm9/device/huawei/y210/                        # port CM9/ICS de origen (RIL_NOTES.md, PERFORMANCE_NOTES.md)
github.com/dazjo/android_device_huawei_u8815 (jellybean)      # referencia JB real, mismo SoC (Huawei G300/U8815)
```
