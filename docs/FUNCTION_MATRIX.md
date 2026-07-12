# Huawei Y210 (CM7) — matriz de funciones y logging

Este documento es una checklist única para:

- tener un inventario completo de funciones del Y210;
- marcar el estado actual del port;
- pedir logs de forma consistente (mismos comandos/archivos por bug).

No reemplaza los notes existentes; los referencia cuando aplica:

- `device/huawei/y210/WIFI_NOTES.md`
- `device/huawei/y210/BLUETOOTH_NOTES.md`
- `device/huawei/y210/AUDIO_NOTES.md`
- `device/huawei/y210/CAMERA_NOTES.md`
- `device/huawei/y210/GPS_NOTES.md`
- `device/huawei/y210/RENDER_NOTES.md`
- `device/huawei/y210/LOGGING_NOTES.md`

## Convención de estado

- **OK**: validado en runtime (uso real).
- **Parcial**: funciona con limitaciones o falta validar un caso clave.
- **No**: falla o no implementado.
- **N/A**: el hardware no existe en este modelo/variante (confirmar si hay duda).
- **Pendiente**: no hay evidencia/logs recientes.

Cuando algo está en **Parcial/No/Pendiente**, adjuntar logs usando
`collect_y210_debug.sh` y/o los comandos puntuales de la sección “Comandos”.

## Matriz (inventario + estado)

Estado actualizado 2026-07-10. Validado en un flasheo limpio (build desde cero)
con `test_y210.sh` completo: 54 PASS / 0 FAIL / 3 SKIP / 26 MANUAL.
Actualizar esta tabla y el script cada vez que un bug se cierre con evidencia.

### Arranque / sistema

- Boot completo hasta launcher: **OK**
- `system_server` estable: **OK**
- `surfaceflinger` estable: **OK**
- `mediaserver` estable: **OK**
- `adb` (USB debugging): **OK**
- `adb logcat` persistente tras reboot: **OK** (ver `device/huawei/y210/LOGGING_NOTES.md`)

### Pantalla / gráficos

- Framebuffer / UI visible: **OK**
- Gralloc/Copybit: **OK** (ruta `gralloc.y210` + `copybit.y210` validada)
- EGL / Adreno 200 (HW accel): **OK** (ver `device/huawei/y210/RENDER_NOTES.md`)
- Ghosting / corrupción de back-buffer: **OK** (fix `fb_setUpdateRect_noop` en gralloc — ver `RENDER_NOTES.md`)
- Bug repetición horizontal (imagen 2.5× / "3 columnas"): **OK** (fix `numBuffers=1` en `libgralloc-qsd8k/framebuffer.cpp` — EGL Adreno detecta fd del framebuffer y fuerza stride=128px; single-buffer fuerza PMEM fd → stride=320px correcto. Ver `RENDER_NOTES.md`)
- Permisos KGSL persistentes: **OK** (`/dev/kgsl-3d0` = `crw-rw-rw- root root` 0666 — regla en `ueventd.huawei.rc` aplicada correctamente)

### Input / UI

- Touchscreen: **OK**
- Multitouch: **OK** (se observa protocolo MT tipo A vía `SYN_MT_REPORT`)
- Botones físicos (power/vol+/vol-): **OK**
- Botones táctiles/capacitivos (atrás/home/menú): **OK**
- Teclas virtuales (en pantalla): **N/A**
- Rotación automática: **OK** (fix de permisos `/dev/accel` en `KERNEL_NOTES.md`)

### Luces / vibración

- Backlight (pantalla / brillo): **OK**
- LED de notificación: **OK** (RGB)
- Vibrador: **OK** (UI “vibrar al tocar” funciona; sysfs: `/sys/class/timed_output/vibrator/enable`)

### Audio

- Salida speaker: **OK** (ver `device/huawei/y210/AUDIO_NOTES.md`)
- Salida auriculares: **OK** (2026-07-09: el volumen bajo percibido era el jack físico sucio, no software — confirmado por el usuario; no es un bug real)
- Micrófono (grabación): **OK** (ver `device/huawei/y210/AUDIO_NOTES.md`)
- Llamadas (voz): **OK** (downlink y uplink/mic confirmados con llamada real entre el CM7 y el Y210 stock el 2026-07-09 — logs muestran `mMicMute` pasando de 1→0 durante `mMode=2` (en llamada), y el otro participante confirmó escuchar audio del CM7)
- Audio por Bluetooth (A2DP): **OK** (ver sección Bluetooth — validado 2026-07-10 con auriculares reales)
- Radio FM (app): **OK** (2026-07-12: audio real de la emisora confirmado, estable, contenido reconocible en 98.1 MHz. Causa raíz final: faltaba abrir `/dev/msm_fm` + `ioctl(AUDIO_START)` — hace que el kernel registre la sesión `audmgr_enable()` con el ARM9 —, llamado ANTES del RPC `rpc_snd_set_device`, igual que hace el stock real. Ya se había probado antes (2026-07-04, 2026-07-06) y descartado como "incompatible", pero la prueba de 2026-07-06 corrió con las etiquetas de `switch_mode` invertidas (bug encontrado el 2026-07-09), es decir con los pines I2S de FM tristateados — nunca fue una prueba limpia. Repetido el 2026-07-12 ya con `switch_mode`/registros del codec corregidos: `run_fm_audmgr_session()` en `AudioHardware.cpp`, llamado desde `setFmOnOff()`. Ver [FM_NOTES.md](FM_NOTES.md) sección "Sesión 2026-07-12" para el detalle completo.)
  - Fixes aplicados (2026-04-26): `FmRxControls.java` (V4L2_CID_AUDIO_MUTE boolean), `AudioHardware.cpp` routing → `SND_DEVICE_HEADSET` + abre `/dev/msm_fm`, `android_hardware_fm_qcom.cpp` controles privados best-effort + `spawnFmInit()` incondicional.
  - Fix aplicado (2026-07-04): `fminit.c` reescrito como daemon persistente (nunca cierra `/dev/radio0`, pasa el fd por socket Unix con SCM_RIGHTS) — elimina el bug de firmware borrado en la segunda apertura del device. `AudioHardware.cpp` `doRouting()`: la rama de FM ahora chequea auricular real conectado antes de elegir endpoint (antes defaulteaba a auricular sin verificar).
  - Fix aplicado (2026-07-06): `android_hardware_fm_qcom.cpp` (`fmAcquireFdNative`) ahora se conecta al socket de `fminit` y usa el fd compartido (con reintentos) en vez de un `open()` propio — el `open()` directo re-disparaba el init de hardware del driver y borraba el firmware recién cargado.
  - Experimental (2026-07-06, sin confirmar): `AudioHardware.cpp` gana `run_fm_dac_config()` (`fm_qsoc_patches ... 3 ...`, modo "config_dac" del script stock) y `run_btfm_pin_switch()` (escritura I2C reversada del stock, property `hw.fm.pinSwitchMode`). Ninguno de los dos por sí solo resuelve el audio; documentado en detalle en FM_NOTES.md.
  - `init.qcom.fm.sh` usa `exec 3</dev/radio0; sleep 1` antes de `fm_qsoc_patches` para evitar race condition IRQ tavarua (XFR=98).
  - Validar con `adb shell getprop hw.fm.init` (esperado `1`) y `adb shell getprop hw.fm.version` (esperado `67240453`).
  - Ver [FM_NOTES.md](FM_NOTES.md) para el diagnóstico completo (incluye desensamblado del stock y la matriz de combinaciones de audio probadas).

### Wi‑Fi

- Encender desde UI: **OK** (validación histórica; ver `device/huawei/y210/WIFI_NOTES.md`)
- Escaneo/Asociación/DHCP: **OK** (según `README.md`)
- Validación post-flash limpio (sin staging manual): **OK** (2026-07-10: build desde cero + flasheo limpio, `test_y210.sh` completo dio 54 PASS / 0 FAIL / 3 SKIP / 26 MANUAL tras corregir 2 falsos negativos del propio script — ver más abajo)
- Wi-Fi tethering (hotspot): **OK** (validado con 2 clientes simultáneos; NAT sobre rmnet0 funcionando)

### Bluetooth

- Encender desde UI: **OK** (ver `device/huawei/y210/BLUETOOTH_NOTES.md`)
- `hci0 UP RUNNING`: **OK**
- Pairing: **OK** (2026-07-09: emparejado con Y210 stock real, bond state 10→11→12 completo, UUIDs descubiertos correctamente)
- Transferencia de archivos (Object Push): **OK** (2026-07-09: foto JPEG de 128440 bytes enviada completa por Bluetooth hacia el Y210 stock; primer intento falló por falta de SD en el receptor, no por bug — confirmado con SD puesta)
- Audio (A2DP): **OK** (2026-07-10: emparejado con auriculares Bluetooth TWS reales, `Headset Service getState()=STATE_CONNECTED`, música reproducida desde `com.android.music` confirmada sonando por los auriculares)

### Sensores

- Acelerómetro (LIS3DH): **OK**
- Gravity sensor: **OK** (virtual / derivado del acelerómetro)
- Linear acceleration: **OK** (virtual / derivado del acelerómetro)
- Proximidad: **N/A** (el Y210 no tiene sensor de proximidad)
- Luz/ALS: **N/A** (el Y210 no tiene sensor de luz)
- Brújula: **N/A**

### GPS

- Motor RPC (`Loc API RPC client initialized`): **OK**
- XTRA (efemérides asistidas): **OK** (41 partes inyectadas al arranque)
- NTP / UTC injection: **OK**
- AGPS (UMTS SLP): **OK**
- Fix real con satélites: **OK** (2026-07-10: prueba al aire libre en Lima, Perú — hasta 9 satélites detectados, coordenadas finales estables `-12.2944, -76.8588` vía `RPC_LOC_EVENT_PARSED_POSITION_REPORT`. Nota menor: las sentencias NMEA de texto (`loc_eng_report_nmea`) llegan vacías (solo `$`), no afecta al LocationManager pero cualquier app que dependa de NMEA crudo no tendría datos — no investigado más a fondo)

Ver `device/huawei/y210/GPS_NOTES.md` para diagnóstico y bugs resueltos.

### Cámara

- App cámara abre: **OK** (ver `device/huawei/y210/CAMERA_NOTES.md`)
- Preview foto `640x480` color portrait: **OK** (NV21→RGB565 SW, orientation=90)
- Captura de foto: **OK** (SHUTTER + RAW_IMAGE 294912 B + COMPRESSED_IMAGE ~55 KB JPEG)
- `close → reopen`: **OK** (fix vtable slot 26 en `release()`)
- Switch foto↔video: **OK** (2026-07-13: `keep()` en `MenuHelper.startCameraActivity()` estaba documentado como eliminado en CAMERA_NOTES.md pero nunca se había aplicado de verdad al árbol -- causaba preview negro/congelado por fd de pmem_adsp reutilizado, rechazado como "invalid heap" por SurfaceFlinger. Eliminado y confirmado en hardware real.)
- Preview video: **OK** (H.263 352×288, mismo pipeline NV21→RGB565)
- Grabación video H.263 352×288 15fps: **OK** (MP4 guardado en galería)
- Video HD (640×480): **N/A** (sin driver kernel `msm_vidc_enc`)

### Almacenamiento / USB

- `/data` / escritura: **OK**
- SDCard (montaje): **OK**
- Modo almacenamiento masivo USB (UMS): **OK**

### Radio / Telefonía (RIL)

- Baseband / operador / señal: **OK** (ver `device/huawei/y210/RIL_NOTES.md`)
- IMEI (`service call iphonesubinfo 1`): **OK**
- Llamadas (voz): **OK** (entrante/saliente)
- SMS: **OK** (enviar/recibir)
- Datos móviles: **OK** (HSPA; rmnet0 con IP real validada en Claro Perú)
- Agitación / ghost de iconos en status bar: **OK** (4 fixes — ver `device/huawei/y210/STATUSBAR_NOTES.md`: doble-poll RIL cases 1033/1037, guard `setIcon()` en StatusBarManagerService, copy-back + buffer zeroing en Surface.cpp, copybit MDP para strip 320×25)

### Energía

- Suspensión / apagar‑encender pantalla: **OK** (`early_suspend`/`late_resume` sin errores; dispositivo entra en suspend correctamente; wakeups periódicos de modem `rpcrouter_smd_xprt` son normales)
- Deep sleep real: **OK** (validado sin USB: 5 min en suspend continuo; WiFi entra en WoW mode; único wakeup periódico es modem `rpcrouter_smd_xprt` cada ~2 min — normal para HSPA)

## Comandos rápidos por área (para acompañar logs)

Ejecutar estos desde host:

- Estado general:
  - `adb shell getprop`
  - `adb logcat -v threadtime -d`
  - `adb shell dmesg`

- Gráficos:
  - `adb shell dumpsys SurfaceFlinger`
  - `adb shell "ls -l /system/lib/egl && cat /system/lib/egl/egl.cfg"`
  - `adb shell "ls -l /dev/kgsl-3d0 /dev/pmem /dev/graphics/fb0 2>/dev/null"`

- Wi‑Fi:
  - `adb shell dumpsys wifi`
  - `adb shell getprop | grep -i wifi`
  - `adb shell "ls -l /data/misc/wifi /system/etc/wifi /system/etc/firmware/wlan 2>/dev/null"`

- Bluetooth:
  - `adb shell getprop | grep -i bt`
  - `adb shell "hciconfig -a; hcitool dev 2>/dev/null"`

- Sensores:
  - `adb shell "ls -l /dev/accel; getevent -lp 2>/dev/null | head -n 80"`

- Cámara:
  - `adb shell dumpsys media.camera`
  - `adb logcat -v threadtime -d | grep -iE \"camera|cameraservice|QualcommCamera|Y210CameraWrapper\"`

- Audio:
  - `adb shell dumpsys media.audio_flinger`
  - `adb shell getprop | grep -i audio`

## Smoke test automatizado

```bash
# Ejecutar desde raíz del árbol CM7 con dispositivo conectado:
bash device/huawei/y210/tools/test_y210.sh            # completo
bash device/huawei/y210/tools/test_y210.sh --fast     # omite suspensión
bash device/huawei/y210/tools/test_y210.sh --section camera

# Resultado esperado (CM7, con SIM): 54 PASS / 0 FAIL / 5 SKIP / 24 MANUAL
# Log detallado: /tmp/test_y210_YYYYMMDD_HHMMSS.log
```

## Recolección “log general” (un solo comando)

Usar el script `collect_y210_debug.sh` en la raíz de este repo para generar
un bundle completo (propiedades, logcat, dumpsys claves, nodos, etc.).
