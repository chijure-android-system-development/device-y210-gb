# Camera Notes — Huawei Y210

## Estado (2026-05-31) — ESTABLE

Ciclo completo de cámara confirmado en dispositivo:

- App Cámara abre — OK
- Preview foto `640x480` en **color**, orientación portrait — OK
- Captura de foto — OK: SHUTTER + RAW_IMAGE (294912 B) + COMPRESSED_IMAGE (~55 KB JPEG)
- Preview se reinicia automáticamente tras la foto — OK
- `close → reopen` — OK
- `mediaserver` sobrevive todo el ciclo — OK
- Preview de VideoCamera (lanzamiento directo) — OK
- Switch foto→video — OK (preview visible en modo video)
- Reabrir cámara foto tras ciclo video — OK (retry doble wrapper + Handler)
- **Grabación de video H.263 352×288 15fps — OK** (archivo MP4/3GP guardado en galería)
- Video HD / M4V 640×480 — N/A (sin driver kernel `msm_vidc_enc`)
- Grabación de video — **Pendiente**

## Props requeridos

```
ro.build.product=msm7625a   # primer escritor gana en ro.* — build.prop no debe tener ro.build.product=y210 antes
persist.camera.delegate_setparams=1
persist.camera.mode=1
```

El blob `libcamera.y210.so` detecta el target leyendo `ro.build.product` y comparando
con strings internos (`msm7625a`, `msm7627a`, etc.). Si lee `y210` muere con
`Unable to determine the target type`.

## Regresión: BUILD_PRODUCT perdido en revert/apply-patches (2026-07-06)

Síntoma: cámara volvió a crashear (`Unable to determine the target type. Camera
will not work` → `createInstance: startCamera failed!` → SIGSEGV en el
destructor `QualcommCameraHardware::MMCameraDL::~MMCameraDL()`, pc=0, tumba
`mediaserver` entero). Sobrevivió incluso a un `wipe data` completo — no era
estado persistido en `/data`, era config de build.

Causa real: la línea que realmente arregla esto (ver "Props requeridos" arriba)
**no es** `PRODUCT_PROPERTY_OVERRIDES += ro.build.product=msm7625a` sola — esa
queda pisada por la línea auto-generada `ro.build.product=y210` de
`build/tools/buildinfo.sh` (las properties `ro.*` son "primer escritor gana",
y la auto-generada sale primero en `build.prop`). Lo que realmente funciona es:

```makefile
PRODUCT_BUILD_PROP_OVERRIDES += BUILD_PRODUCT=msm7625a
```

Esto alimenta directo la variable de shell `$BUILD_PRODUCT` que
`buildinfo.sh` chequea (`if [ -n "$BUILD_PRODUCT" ]`), generando
`ro.build.product=msm7625a` como única línea, sin conflicto de orden.

Esa línea `PRODUCT_BUILD_PROP_OVERRIDES` existía como edición manual directa en
`vendor/cyanogen/products/cyanogen_y210.mk`, pero **nunca se guardó** en la
plantilla `device/huawei/y210/patches/vendor_cyanogen_y210.mk` que usa
`apply-patches.sh` para regenerar ese archivo. Al correr `revert-patches.sh`
(para probar otro dispositivo en el mismo árbol) y después `apply-patches.sh`,
la regeneración salió de la plantilla vieja — sin el fix — y la regresión
volvió en silencio, sin ningún error de compilación que lo delatara.

Fix aplicado (2026-07-06): se agregó `PRODUCT_BUILD_PROP_OVERRIDES +=
BUILD_PRODUCT=msm7625a` en las tres copias del product mk:
`device/huawei/y210/cyanogen_y210.mk`,
`device/huawei/y210/patches/vendor_cyanogen_y210.mk` (la plantilla — importante
que quede ahí para que sobreviva un futuro `apply-patches.sh`), y
`vendor/cyanogen/products/cyanogen_y210.mk` (la copia activa).

Nota de build: si tocás esta línea, `mka bacon` incremental **no** regenera
`build.prop` solo (el target no depende de los `.mk` de producto, solo de
`buildinfo.sh`) — hace falta `make installclean` antes de recompilar, o vas a
seguir viendo el valor viejo aunque el `.mk` ya esté arreglado. Verificar
siempre con:

```bash
grep -n "ro.build.product" out/target/product/y210/system/build.prop
```

La primera aparición debe decir `msm7625a`, no `y210`.

Validación post-fix (equipo real, 2026-07-06): `mediaserver` no crashea al
abrir Cámara (mismo PID antes/después), `startPreview exit rc=0
previewRunning=1`, sin tombstones nuevos.

## Causa raíz resuelta: vtable misalignment

El blob stock (`QualcommCameraHardware`) fue compilado contra una versión de
`CameraHardwareInterface` con 2 métodos Qualcomm extra insertados entre
`cancelAutoFocus` (slot 18) y `takePicture`. Eso desplaza todos los métodos
siguientes +2 respecto al layout de CM7.

Tabla de slots:

```
CM7 slot  método CM7          blob slot
18        cancelAutoFocus     18   (sin desplazamiento)
19        takePicture         21   (+2 por los 2 extras en 19/20)
20        cancelPicture       22
21        setParameters       23
22        getParameters       24   (no se llama directamente; se usa cache)
23        sendCommand         25
24        release             26
25        dump                27
```

## Implementación del wrapper

`libcamera/Y210CameraWrapper.cpp` usa despacho directo de vtable para todos
los métodos con desplazamiento:

```cpp
static inline void** blobVptr(const sp<CameraHardwareInterface>& iface) {
    return *reinterpret_cast<void***>(iface.get());
}
```

### takePicture — firma distinta

El blob espera `takePicture(const sp<ISurface>&)`, no `takePicture()`.
Se pasa `sp<ISurface> nullSurface` para que el blob omita el postview pero
entregue los callbacks JPEG igualmente:

```cpp
typedef status_t (*BlobTakePictureFn)(void*, const sp<ISurface>&);
BlobTakePictureFn fn = reinterpret_cast<BlobTakePictureFn>(blobVptr(mLibInterface)[21]);
sp<ISurface> nullSurface;
return fn(mLibInterface.get(), nullSurface);
```

Requiere `#include <surfaceflinger/ISurface.h>` y `libsurfaceflinger_client` en `Android.mk`.

### release — slot 26

El slot 24 del blob (al que apunta el puntero virtual de CM7 para `release()`)
es en realidad `getParameters` del blob → crash inmediato. Se fuerza slot 26:

```cpp
typedef void (*BlobReleaseFn)(void*);
BlobReleaseFn fn = reinterpret_cast<BlobReleaseFn>(blobVptr(mLibInterface)[26]);
fn(mLibInterface.get());
```

### Orientación de preview

El sensor está montado físicamente a 90° (sensor landscape en teléfono portrait).
Se fuerza `orientation=90` en `HAL_getCameraInfo()` para que la app de cámara
rote el preview correctamente:

```cpp
if (cameraInfo) {
    cameraInfo->orientation = 90;
}
```

### Retry doble en startPreview tras ciclo video (2026-05-31)

Cuando Camera.java reabre la cámara tras un ciclo video (con `keep()` eliminado),
el firmware msm7x27 ISP puede no haberse liberado del hardware anterior →
`startPreview` retorna `UNKNOWN_ERROR` (0x80000000 = rc=-2147483648).

**Capa 1 — HAL (Y210CameraWrapper.cpp):** retry con delay de 250ms al nivel del
wrapper, antes de que el error llegue a Java:

```cpp
status_t rc = mLibInterface->startPreview();
if (rc != NO_ERROR && rc != INVALID_OPERATION) {
    usleep(250000);  // esperar a que el firmware libere el ISP
    rc = mLibInterface->startPreview();
}
```

**Capa 2 — App (Camera.java):** si el retry del wrapper falla igual, Camera.java
no crashea. Inspirado en el ROM stock (que tiene `mStartPreviewFail` y usa el
handler `RESTART_PREVIEW = 3` para reintentos asincrónicos):

```java
} catch (Throwable ex) {
    if (isY210CameraWorkaroundEnabled()) {
        mStartPreviewFail = true;
        closeCamera();
        if (!mHandler.hasMessages(RESTART_PREVIEW)) {
            mHandler.sendEmptyMessageDelayed(RESTART_PREVIEW, 500);
        }
        return;  // no crash
    }
    closeCamera();
    throw new RuntimeException("startPreview failed", ex);
}
mStartPreviewFail = false;
```

El Handler `RESTART_PREVIEW` llama `restartPreview()` 500ms después, cuando el
ISP ya debe estar libre. El ROM stock también usa `mStartPreviewFail` + Handler
retry — la diferencia es que CM7 no lo usaba para este caso específico.

**Análisis del ROM stock (2026-05-31):** El `.odex` descompilado confirmó:
- `VideoCamera` stock tiene `DELAYED_ONRESUME_FUNCTION = 7` (Handler delay en resume)
- Ambas actividades tienen `mStartPreviewFail` (manejo gracioso del fallo)
- La stock no crashea en startPreview: retry asincrónico vía Handler

---

## Pipeline de display del preview

El blob produce frames NV21 (`HAL_PIXEL_FORMAT_YCrCb_420_SP`, format 17).
El path de overlay está deshabilitado (el blob crasheaba al probar metadatos de
overlay en algunos ciclos de vida). SurfaceFlinger usa el path GL de software.

### copybit — `hardware/msm7k/libcopybit/copybit.cpp`

Dos bugs que producían `EINVAL` del ioctl `MSMFB_BLIT`:

1. **Orden de llamadas**: `set_image(&req->src)` debe llamarse **antes** de
   `set_infos()` para que `req->src.format` esté inicializado cuando se evalúa
   la condición YUV en `set_infos`.
2. **MDP_DITHER**: `set_infos()` suprime `MDP_DITHER` para fuentes YUV — el
   driver msm7x27 retorna `EINVAL` de `MSMFB_BLIT` si ese flag está presente.

Los buffers de preview son de `pmem_adsp` (dominio ISP); `MSMFB_BLIT` solo
acepta `pmem` del dominio display → el copybit sigue fallando, pero el path GL
actúa como fallback correcto.

### TextureManager — `frameworks/base/services/surfaceflinger/TextureManager.cpp`

- `isSupportedYuvFormat()` incluye `HAL_PIXEL_FORMAT_YCrCb_420_SP` (NV21).
- En `loadTexture()`, NV21 se convierte a RGB565 por software (BT.601, función
  `convertNV21toRGB565`) antes de subirlo como textura GL.
- Procesa ~307 K píx/frame a ~7 fps (~30 ms/frame en Cortex-A5 a 600 MHz).

---

## Pipeline de display del preview de VideoCamera

### Problema raíz: pmem_adsp fd cross-process

El blob asigna los buffers de preview en `/dev/pmem_adsp`. El ioctl
`SurfaceFlinger::registerBuffers` intenta `mmap` este fd desde el proceso
SurfaceFlinger vía Binder. El kernel PMEM del msm7x27 solo permite ese mmap
**una vez**: después de `munmap` (por `unregisterBuffers`), un segundo `mmap`
del mismo fd falla con `EINVAL`.

Consecuencia: si la foto Camera ya hizo `registerBuffers` (primer mmap del fd) y
luego VideoCamera intenta `registerBuffers` con el **mismo fd** (caso del switch
con `CameraHolder.keep()` activo → hardware reutilizado vía `reconnect()`),
el segundo mmap falla → `BufferSource.mStatus = error` → `postBuffer` ignorado
→ preview negro.

### Fix 1: CameraService — `setPreviewDisplay` non-fatal + stop/restart

`frameworks/base/services/camera/libcameraservice/CameraService.cpp`:

- `registerPreviewBuffers()` es no-fatal en `setPreviewDisplay()` y
  `startPreviewMode()` (el resultado ya no se propaga como error).
- Cuando `setPreviewDisplay()` se llama con preview corriendo (`previewEnabled()`
  = true), en lugar de llamar directamente `registerPreviewBuffers()` (que
  fallaría con el fd antiguo), se hace:
  ```cpp
  mHardware->stopPreview();
  if (mHardware->startPreview() == NO_ERROR) {
      registerPreviewBuffers();
  }
  ```
  El stop+restart hace que el HAL cree un nuevo fd pmem_adsp que aún **no** fue
  mapeado en SurfaceFlinger → `registerBuffers` tiene éxito.

### Fix 2: MenuHelper — eliminar `keep()` para obtener fd fresco

`packages/apps/Camera/src/com/android/camera/MenuHelper.java`:

`startCameraActivity()` llamaba `CameraHolder.instance().keep()` que mantenía
vivo el hardware 3 segundos y forzaba `reconnect()` (mismo fd). Se elimina ese
`keep()` para que al hacer el switch el hardware se libere completamente y
VideoCamera obtenga una instancia nueva con fd fresco → `registerBuffers` del
primer mmap tiene éxito.

**Trade-off**: el switch foto→video tarda ~100-500ms más (re-init de hardware).

### Fix 3: Wrapper + App — retry doble en startPreview

Ver sección anterior "Retry doble en startPreview tras ciclo video".

---

## Grabación de video

### Diagnóstico del encoder (2026-05-31)

El kernel precompilado del Y210 **no incluye el driver `msm_vidc_enc`** (codec de
video hardware de Qualcomm). Sin `/dev/msm_vidc_enc`, el encoder OMX
`OMX.qcom.video.encoder.mpeg4` (en `libOmxVidEnc.so`) no puede inicializarse.
Stagefright cae al encoder **software** `M4vH263Encoder`.

El perfil "high" original (`m4v 640×480 30fps 2Mbps`) excede la capacidad del
encoder software → `Failed to initialize the encoder` → `start failed: -2147483648`.

### Fix 1 — media_profiles.xml: perfiles ajustados al encoder software

`prebuilt/system/etc/media_profiles.xml`:

```xml
<!-- Antes -->
<EncoderProfile quality="high" fileFormat="mp4" duration="60">
    <Video codec="m4v" bitRate="2000000" width="640" height="480" frameRate="30" />

<!-- Después -->
<EncoderProfile quality="high" fileFormat="mp4" duration="60">
    <Video codec="h263" bitRate="512000" width="352" height="288" frameRate="15" />
```

H.263 a 352×288 15fps 512Kbps es el máximo que el encoder software puede manejar
de forma estable en el Cortex-A5 @ 600MHz. El blob también reporta
`preferred-preview-size-for-video=352x288` y `supported-video-sizes=352x288,...`.

### Fix 2 — StagefrightRecorder: size check no-fatal para parámetros vacíos

`frameworks/base/media/libmediaplayerservice/StagefrightRecorder.cpp`:

```cpp
// Antes: -1x-1 (parámetros vacíos) fallaba el check y abortaba la grabación
if (frameWidth  < 0 || frameWidth  != mVideoWidth || ...)

// Después: solo falla si el tamaño ES CONOCIDO y NO coincide
if (frameWidth > 0 && frameHeight > 0 &&
        (frameWidth != mVideoWidth || frameHeight != mVideoHeight))
```

Cuando `getParameters()` devuelve vacío (caso del stop/restart de
`setPreviewDisplay`), el check devuelve (-1,-1). La condición original lo
trataba como error. Con la nueva condición, (-1,-1) es "desconocido" y se
permite continuar — la cámara sigue funcionando correctamente.

### Resultado confirmado en dispositivo

- Preview de video: 352×288, color — OK
- Grabación inicia: `startRecording exit rc=0 recordingRunning=1`
- Ambas pistas (video H.263 + audio AMR-NB): iniciadas
- Archivo MP4 guardado en galería — OK

---

## Notas de build

```bash
# Wrapper (orientación, retry startPreview)
make -j$(nproc) libcamera

# Pipeline de display
make -j$(nproc) libsurfaceflinger copybit.msm7k

# CameraService (registerBuffers non-fatal, stop+restart en setPreviewDisplay)
make -j$(nproc) libcameraservice

# Camera APK (keep() eliminado + retry Handler en startPreview)
# DEBE compilarse en Docker, no en el host Ubuntu 24:
docker exec cm7-builder bash -c "cd /home/builder/cm7 && . build/envsetup.sh && lunch cyanogen_y210-eng 2>/dev/null && make -j\$(nproc) libcamera Camera"
docker cp cm7-builder:/home/builder/cm7/out/target/product/y210/system/app/Camera.apk /tmp/Camera_new.apk

# Push todo
adb remount
adb push out/target/product/y210/system/lib/libcamera.so /system/lib/
adb push out/target/product/y210/system/lib/libsurfaceflinger.so /system/lib/
adb push out/target/product/y210/system/lib/hw/copybit.msm7k.so /system/lib/hw/
adb push out/target/product/y210/system/lib/libcameraservice.so /system/lib/
adb push /tmp/Camera_new.apk /system/app/Camera.apk
adb push device/huawei/y210/prebuilt/system/etc/media_profiles.xml /system/etc/

# libmediaplayerservice (StagefrightRecorder size check relajado)
make -j$(nproc) libmediaplayerservice
# o desde Docker:
docker exec cm7-builder bash -c "cd /home/builder/cm7 && . build/envsetup.sh && lunch cyanogen_y210-eng 2>/dev/null && make -j\$(nproc) libmediaplayerservice"
docker cp cm7-builder:/home/builder/cm7/out/target/product/y210/system/lib/libmediaplayerservice.so /tmp/
adb push /tmp/libmediaplayerservice.so /system/lib/
adb shell sync && adb reboot
```

El setParameters puede emitir `rc=-22` para `antibanding` — el blob no soporta
ese parámetro en este sensor (OV5640) y lo ignora de forma benigna.
