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
- `RENDER_NOTES.md`
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

Estado “base” tomado del resumen de `README.md` y los notes del device tree.
Actualizar esta tabla cada vez que un bug se cierre con evidencia.

### Arranque / sistema

- Boot completo hasta launcher: **OK**
- `system_server` estable: **OK**
- `surfaceflinger` estable: **OK**
- `mediaserver` estable: **OK** (con cámara aún en investigación)
- `adb` (USB debugging): **OK**
- `adb logcat` persistente tras reboot: **OK** (ver `device/huawei/y210/LOGGING_NOTES.md`)

### Pantalla / gráficos

- Framebuffer / UI visible: **OK**
- Gralloc/Copybit: **OK** (ruta `gralloc.y210` + `copybit.y210` validada)
- EGL / Adreno 200 (HW accel): **OK** (ver `RENDER_NOTES.md`)
- Permisos KGSL persistentes: **Pendiente** (revisar `RENDER_NOTES.md`)

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
- Salida auriculares: **Parcial** (funciona, pero volumen percibido bajo vs stock; prueba `persist.sys.headset-postproc` sin cambio)
- Micrófono (grabación): **OK** (ver `device/huawei/y210/AUDIO_NOTES.md`)
- Llamadas (voz): **Parcial** (downlink OK; uplink/mic requiere validar tras el overlay de `send_mic_mute_to_AudioManager`)
- Audio por Bluetooth (A2DP/SCO): **Pendiente**
- Radio FM (app): **No** (crash `UnsatisfiedLinkError: acquireFdNative`; falta stack JNI `android.hardware.fmradio.*`)

### Wi‑Fi

- Encender desde UI: **OK** (validación histórica; ver `device/huawei/y210/WIFI_NOTES.md`)
- Escaneo/Asociación/DHCP: **OK** (según `README.md`)
- Validación post-flash limpio (sin staging manual): **Pendiente** (ver `device/huawei/y210/WIFI_NOTES.md`)
- Wi-Fi tethering (hotspot): **OK** (validado con 2 clientes simultáneos; NAT sobre rmnet0 funcionando)

### Bluetooth

- Encender desde UI: **OK** (ver `device/huawei/y210/BLUETOOTH_NOTES.md`)
- `hci0 UP RUNNING`: **OK**
- Pairing + audio: **Pendiente**

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
- Fix real con satélites: **Pendiente** (requiere prueba al aire libre)

Ver `device/huawei/y210/GPS_NOTES.md` para diagnóstico y bugs resueltos.

### Cámara

- App cámara abre: **OK** (ver `device/huawei/y210/CAMERA_NOTES.md`)
- Preview primera apertura: **Parcial** (estable, pero orientación/rotación aún por confirmar)
- `close -> reopen`: **No**
- Captura de foto: **No**
- Video: **Pendiente**

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

### Energía

- Suspensión / apagar‑encender pantalla: **Parcial** (ver `README.md`)
- Deep sleep real: **Pendiente** (falta evidencia en dmesg / prueba repetible)

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

## Recolección “log general” (un solo comando)

Usar el script `collect_y210_debug.sh` en la raíz de este repo para generar
un bundle completo (propiedades, logcat, dumpsys claves, nodos, etc.).
