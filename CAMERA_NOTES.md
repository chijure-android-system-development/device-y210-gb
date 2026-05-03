# Camera Notes

## Estado actual

La camara ya abre y llega a preview estable en la primera apertura usando el
wrapper `libcamera.so` del arbol.

Estado confirmado:

- la app Camara abre
- el bind con la HAL real funciona
- el preview arranca en `640x480`
- `cameraMode=1` es obligatorio para que el blob abra

Lo que todavia no quedo cerrado:

- el ciclo `close -> reopen`
- la captura de foto usable
- el lifecycle real del blob Qualcomm al liberar la camara

## Hallazgo mas importante de esta ronda

Con `adb logcat` ya funcionando, el punto real de fallo quedo mucho mejor
aislado:

- la ruta estable previa seguia entrando por `CameraHardwareStub`
- al probar `libcameraservice` sin stub junto con el wrapper `libcamera.so`,
  el blob real Qualcomm/Huawei si llega a cargar
- `QualcommCameraHardware` reporta:
  - `camera_id: 0`
  - `modes_supported: 5`
- pero al abrir Camara aparece:
  - `openCameraHardware: camera mode -2141175484`
  - `Invalid camera mode (...) requested`

Eso significa que el bloqueo principal ya no esta en la UI ni en el parser
de parametros, sino en la integracion nativa entre:

- `libcameraservice.so`
- `libcamera_client.so`
- `libcamera.so` wrapper
- `libcamera.y210.so` blob stock

## Wrapper `libcamera.so`

Se creo un wrapper propio en:

- `device/huawei/y210/libcamera/Y210CameraWrapper.cpp`
- `device/huawei/y210/libcamera/Y210CameraWrapper.h`

Su rol es:

- cargar `libcamera.y210.so` con `dlopen`
- delegar `HAL_openCameraHardware`, `HAL_getCameraInfo`,
  `HAL_getNumberOfCameras`
- interceptar `getParameters()` y `setParameters()`
- forzar `cameraMode=1` al abrir la HAL real

Sin ese wrapper, el blob stock directo no sirve en este stack:

- `openCameraHardware: camera mode 0`
- `Invalid camera mode (0) requested`
- `Fail to connect to camera service`

Conclusion: `libcamera.so` stock directo queda descartado como solucion.

## Evolucion reciente del crash nativo

### Fase 1

Al principio, con la variante sin stub, `mediaserver` moria asi:

- `SIGSEGV` / `SIGABRT`
- `__cxa_pure_virtual`
- backtrace en `Y210CameraWrapper::release()`

### Fase 2

Se endurecio el lifetime del wrapper para no destruir inmediatamente la
interfaz delegada. Despues de eso, el sintoma cambio:

- `mediaserver` siguio cayendo, pero ya no con `__cxa_pure_virtual`
- aparecio `SIGBUS` en:
  - `libcutils.so (android_atomic_add)`
  - `libutils.so (RefBase::incStrong)`
  - luego `libcamera.so`

Esto apunta a que el blob o su objeto `CameraHardwareInterface` no tolera
bien alguna operacion de refcount/lifetime alrededor de `release()`.

Conclusion provisional:

- el wrapper simple fue util para acotar el fallo
- pero todavia no replica la semantica exacta que el blob espera

## `gralloc`

Tambien se probo el frente de preview/buffers.

Antes aparecia repetidamente:

- `E/y210.gralloc: cannot invalidate handle ...`

Eso venia de un bloque agregado en:

- `device/huawei/y210/libgralloc/mapper.cpp`

que hacia `PMEM_INV_CACHES` en locks de solo lectura.

Ese bloque se elimino y el error desaparecio de `logcat`. Eso fue una mejora
real, pero no resolvio la camara: la app sigue fallando en `connect` aun sin
ese ruido de `gralloc`.

## Estado actual mas preciso

La evidencia actual dice esto:

1. la app Java no es la causa raiz principal del reopen
2. `gralloc` tenia ruido real pero secundario
3. el bug principal actual esta en el camino nativo de `release()/reopen`
4. el wrapper sigue siendo necesario, pero no alcanza para corregir el
   lifecycle interno del blob

## Hallazgos recientes de lifecycle / reopen

Se hicieron tres experimentos controlados sobre `Y210CameraWrapper`:

### 1. `skip release`

Con el hack actual de no delegar `release()`:

- la primera apertura funciona
- el preview arranca
- la segunda apertura reutiliza el mismo `iface`
- `startPreview()` devuelve `rc=0`
- pero `registerPreviewBuffers` falla con:
  - `cannot map BpMemoryHeap`
  - `invalid heap`
  - `registerBuffers failed with status -19`

Eso demuestra que, sin `release()` real, el blob queda en estado sucio y
reexpone un heap de preview invalido en el reopen.

### 2. `null_iface_on_release`

Se agrego una prueba para hacer:

- `stopPreview()`
- `clear callbacks`
- `mLibInterface.clear()`

sin delegar `release()` al blob.

Resultado:

## Update 2026-04-12 (bring-up funcional de open + preview)

### Causa raiz del crash de `mediaserver` al abrir

Se observaba:

- `Unable to determine the target type. Camera will not work`
- `createInstance: startCamera failed!`
- `FATAL ERROR: could not dlopen liboemcamera.so: (null)`
- Luego `mediaserver` caia con `SIGSEGV` dentro de `~QualcommCameraHardware/MMCameraDL` (PC=0x0)

Hallazgo clave: `libcamera.y210.so` decide el "target type" leyendo `ro.build.product` y comparando
con strings como `msm7625a`, `msm7627a`, etc. En nuestro build, `ro.build.product` quedaba fijado
a `y210` porque `build/tools/buildinfo.sh` lo emite primero en `build.prop` (y al ser `ro.*`,
los overrides posteriores no surten efecto).

Fix:

- Hacer que `ro.build.product` sea `msm7625a` en el boot.
  - Solucion permanente: `build/tools/buildinfo.sh` deja de emitir `ro.build.product` (prop obsoleta),
    permitiendo que el valor del device (`device/huawei/y210/cyanogen_y210.mk`) sea el unico efectivo.
  - Validacion: `adb shell getprop ro.build.product` debe devolver `msm7625a`.

Con eso, el blob pasa de `startCamera failed` a inicializar parametros (y deja de matar `mediaserver`
en `HAL_openCameraHardware`).

### Ajustes de wrapper (runtime toggles)

Para estabilizar el preview en este blob legacy:

- `debug.camera.delegate_setparams=1` (habilita delegar `setParameters()` al blob)
  - sin esto, se observaba `registerBuffers failed -19 / invalid heap` y `startPreview failed`.

Nota: se agrego soporte de `debug.camera.delegate_setparams` en `shouldDelegateSetParameters()`
para poder probar sin reflashear.

### Parches en Camera.apk (evitar crashes de UI)

Se corrigieron crashes que escondian el estado real del HAL:

- NPE en zoom cuando `mParameters` era `null` (`initializeZoom()`).
- `RuntimeException: set display orientation failed` (HAL legacy no soporta el API): se atrapa la excepcion.
- `Invalid rotation=5` al tomar foto: se cuantiza a multiplos de 90 antes de `Parameters.setRotation()`.

### Estado actual (al final de la ronda)

- `open` del HAL y `startPreview` funcionan con:
  - `ro.build.product=msm7625a`
  - `debug.camera.delegate_setparams=1`
- Captura (`takePicture`) aun es inestable:
  - se observa `Error 100` / `media server died` durante `takePicture(13)` en algunos intentos.
  - pendiente: capturar tombstone exacto de `mediaserver` durante `takePicture` y aislar si es
    parametro, formato, heap o callback.

### Comandos de validacion rapida

- Props:
  - `adb shell getprop ro.build.product`
  - `adb shell setprop debug.camera.delegate_setparams 1`
- Logs:
  - `adb logcat -d | egrep 'QualcommCameraHardware|registerBuffers|invalid heap|takePicture\\(' | tail -n 200`

- el wrapper si suelta su `sp<>` local
- pero el siguiente `open` vuelve a recibir el mismo `iface`
- el reopen sigue fallando con `invalid heap -19`

Conclusión:

- el problema no es solo el `sp<>` local del wrapper
- el estado viejo sigue persistiendo dentro del blob o de sus singletons

### 3. `delegated release`

Se habilito una rama controlada de `release()` real del blob.

Resultado:

- el wrapper entra a `libInterface->release()`
- `mediaserver` se cae inmediatamente

Conclusión:

- el `release()` real si toca el estado interno importante del blob
- pero el blob Qualcomm/Huawei no tolera ese lifecycle en este stack actual

## Conclusión técnica actual

El problema del reopen ya esta suficientemente acotado:

- `libcamera.so` stock directo: no sirve, porque entra con `camera mode 0`
- wrapper con `skip release`: abre, pero deja heap invalido para reopen
- wrapper con `delegated release`: crashea `mediaserver`

En otras palabras:

- el wrapper es necesario para abrir
- el lifecycle real del blob sigue siendo incompatible con el stack actual
- el siguiente workaround ya no debe centrarse en `cameraMode`
- debe centrarse en evitar o amortiguar el `close/reopen` real

## Hallazgo importante

En el arbol del dispositivo quedo esta configuracion:

- `device/huawei/y210/BoardConfig.mk`
- `USE_CAMERA_STUB := true`

Eso significa que la build actual de `libcameraservice` se enlaza con
`CameraHardwareStub` en vez de usar la ruta real de `libcamera`.

La evidencia en el telefono fue directa:

- `strings /system/lib/libcameraservice.so` mostraba simbolos de
  `CameraHardwareStub`
- `strings /system/lib/libcamera.so.0` mostraba `QualcommCameraHardware`

## Prueba realizada: quitar el stub

Se probo el camino minimo para activar la camara real:

1. cambiar `USE_CAMERA_STUB := false`
2. recompilar `libcameraservice.so`
3. instalar tambien `libcamera.so` con ese nombre en `/system/lib`
   ademas de `libcamera.so.0`

Resultado:

- `libcameraservice.so` recompilado ya no contenia `CameraHardwareStub`
- apuntaba a `HAL_openCameraHardware` y `libcamera.so`
- pero el telefono no terminaba de arrancar
- `mediaserver` quedaba en `restarting` ya durante el boot

## Logs del fallo

La falla no fue un reboot completo de kernel, pero si un bootloop parcial
de Android por `mediaserver`.

Estado capturado durante el fallo:

```text
init.svc.media=restarting
surfaceflinger running
sys.boot_completed=
```

La ruta "sin stub" rompe `mediaserver` ya desde el arranque al activar la
pila real de camara. Eso indica una incompatibilidad ABI o de runtime entre:

- `libcameraservice` de CM7
- `libcamera.so` / `liboemcamera.so` stock
- y posiblemente `libcamera_client.so` o `libmedia.so`

## Comparaciones utiles

### Blobs stock disponibles

En el dump stock existen:

- `libcamera.so`
- `libcamera_client.so`
- `libcameraservice.so`
- `libmedia.so`
- `liboemcamera.so`

### Dependencias

`libcameraservice.so` stock y el recompilado con `USE_CAMERA_STUB := false`
declaran las mismas dependencias ELF:

- `libui.so`
- `libutils.so`
- `libbinder.so`
- `liblog.so`
- `libcutils.so`
- `libmedia.so`
- `libcamera_client.so`
- `libsurfaceflinger_client.so`
- `libcamera.so`

Eso sugiere que el problema no esta solo en el nombre de las dependencias,
sino en compatibilidad binaria real entre versiones.

## Estado del telefono tras rollback

Para recuperar estabilidad se restauro:

- `/system/lib/libcameraservice.so` desde backup
- se elimino `/system/lib/libcamera.so`

Despues del rollback el sistema volvio a quedar estable:

- `mediaserver` running
- `surfaceflinger` running
- `system_server` running

## Hipotesis actual

El preview blanco que se ve en la app no puede seguir tratandose como
un bug puro de `gralloc` o `copybit` mientras `libcameraservice` siga
en la ruta stub.

La prioridad correcta ahora es resolver una ruta hardware real y estable:

1. `libcameraservice` sin stub
2. combinacion correcta de `libcamera.so`, `libcamera_client.so` y
   posiblemente `libcameraservice.so` stock
3. si esa ruta arranca, recien ahi volver a evaluar el preview blanco

## Siguiente experimento recomendado

Probar una combinacion mas fiel al stock:

- `libcameraservice.so` stock
- `libcamera_client.so` stock
- `libcamera.so` stock
- mantener el resto del sistema CM7 intacto

Si eso tambien rompe `mediaserver`, la siguiente sospecha fuerte pasa a:

- `libmedia.so`
- `libsurfaceflinger_client.so`
- o diferencias de binder/heap entre stock y CM7

## Hallazgo adicional de ABI

La comparacion de simbolos dio una pista mas fuerte:

- `libcamera_client.so` stock y el de CM7 no son intercambiables como si
  fueran equivalentes
- el `libcamera_client.so` stock exporta una gran cantidad de simbolos de
  `CameraParameters` y rutas extendidas de camara Qualcomm/Huawei que no
  aparecen en la variante CM7 actual

Ejemplos visibles en stock:

- `CameraParameters::PIXEL_FORMAT_YV12`
- `CameraParameters::KEY_PREVIEW_FORMAT`
- `CameraParameters::PIXEL_FORMAT_YUV420SP_ADRENO`
- `CameraParameters::unflatten(String8 const&)`

En cambio, al revisar `libmedia.so` del telefono CM7, la presencia de
simbolos `CameraParameters` es mucho mas reducida y no explica por si sola
la incompatibilidad principal.

Conclusion provisional:

- la ruptura mas fuerte ya no apunta primero a `gralloc`
- tampoco apunta primero a `libmedia`
- apunta sobre todo a la pareja:
  - `libcamera_client.so`
  - `libcameraservice.so`

## Ultima prueba realizada

Se probo tambien una combinacion mas stock:

- `libcamera_client.so` stock
- `libcameraservice.so` stock
- `libcamera.so` stock

Resultado:

- el telefono queda en bootloop parcial
- `mediaserver` entra en `restarting`
- el sistema no llega a completar `boot_completed`

Luego se hizo rollback para recuperar el estado usable del telefono.

## Prueba posterior: `libmedia.so` stock

Se probo una combinacion aun mas cercana al stock:

- `libmedia.so` stock
- `libcamera_client.so` stock
- `libcameraservice.so` stock
- `libcamera.so` stock

Resultado:

- esta vez `mediaserver` tampoco sobrevivia al arranque
- `init.svc.media` quedaba en `restarting`
- el telefono volvia a quedar en bootloop parcial
- hubo que hacer rollback inmediato

Esto descarta `libmedia.so` stock como camino simple de integracion.

Conclusion actual mas fuerte:

- `libmedia.so` stock no sirve como reemplazo directo
- `libcamera_client.so` y `libcameraservice.so` stock tampoco alcanzan solos
- la integracion correcta probablemente requiere portar o adaptar la ABI de
  camara sobre las librerias CM7, no reemplazar media completa por blobs stock

## Variante CAF / GET_BUFFER_INFO

Se probo tambien recompilar la pila CM7 de camara con:

- `BOARD_USE_CAF_LIBCAMERA_GB_REL := true`
- `BOARD_CAMERA_USE_GETBUFFERINFO := true`

Eso hizo que los binarios recompilados expusieran rutas mas cercanas a stock:

- `Camera::getBufferInfo()`
- `Camera::encodeData()`
- `CameraService::Client::getBufferInfo()`
- `CameraService::Client::encodeData()`

Pero el resultado practico siguio siendo malo:

- el telefono quedaba otra vez en bootloop parcial
- `mediaserver` pasaba a `restarting`
- hubo que hacer rollback

O sea: estas flags parecen necesarias para acercar la ABI, pero no son
suficientes por si solas para sacar la camara real del loop de arranque.

## ABI adicional portada en framework

Despues de comparar `libcamera_client.so` stock con la variante CAF, se vio
que stock todavia exporta dos simbolos publicos que CM7 no tenia:

- `Camera::connect(int, int)`
- `Camera::takeLiveSnapshot()`

Se porto un parche ABI de bajo riesgo en `frameworks/base` para acercar la
pila CM7 al stock sin tocar `CameraHardwareInterface` ni el blob:

- `ICamera` ahora expone `takeLiveSnapshot()` al final de la interfaz
- `ICameraService` ahora expone `connect(..., int halVersion)` adicional
- `Camera` ahora exporta:
  - `connect(int, int)`
  - `takeLiveSnapshot()`
- `CameraService` implementa:
  - `connect(..., int halVersion)` como wrapper del `connect()` existente
  - `Client::takeLiveSnapshot()` devolviendo `INVALID_OPERATION`

La idea no es afirmar que esto resuelve la camara por si solo, sino cerrar un
gap real de ABI visible frente al stack stock.

La build de validacion paso y los simbolos nuevos quedaron presentes en:

- `out/target/product/y210/system/lib/libcamera_client.so`
- `out/target/product/y210/system/lib/libcameraservice.so`

Simbolos verificados:

- `android::Camera::connect(int, int)`
- `android::Camera::takeLiveSnapshot()`
- `android::CameraService::connect(..., int, int)`
- `android::CameraService::Client::takeLiveSnapshot()`

## Hallazgo clave: `native_getParameters()` vuelve vacio

Despues de dejar de parchear la app "a ciegas", se movio la investigacion a
la ruta real de parametros.

Primero se endurecio:

- `frameworks/base/core/java/android/hardware/Camera.java`
  - `Camera.Parameters.flatten()` ya no rompe si `mMap` esta vacio
  - `Camera.Parameters.unflatten()` ya tolera strings vacios o tokens
    malformados
- `packages/apps/Camera/src/com/android/camera/Camera.java`
  - se agrego un dump controlado de parametros a
    `/data/data/com.android.camera/files/camera_params_dump.txt`

El dump resultante fue concluyente:

```text
stage=updateCameraParametersPreference:start
native_getParameters=
flatten=
picture_size=null
preview_size=null
focus_mode=null
scene_mode=null
flash_mode=null
white_balance=null
supported_* = null
```

Conclusion fuerte:

- el problema ya no esta en `flatten()` / `unflatten()`
- el problema ya no esta primero en la app
- `native_getParameters()` ya llega vacio desde la capa nativa

Eso reduce la causa raiz a:

- `CameraService::Client::getParameters()`
- `mHardware->getParameters()`
- y, al fondo, `libcamera.so`

## Experimento descartado: sembrar defaults en `libcameraservice`

Se probo una inicializacion minima estilo MSM7K en:

- `frameworks/base/services/camera/libcameraservice/CameraService.cpp`

Idea:

- si el HAL devolvia parametros vacios al abrir, sembrar defaults conservadores
  (`480x320`, `640x480`, `yuv420sp`, `jpeg`, etc.) antes de entregar el
  cliente

Resultado:

- el telefono quedaba en bootloop parcial
- `init.svc.media=restarting`
- `surfaceflinger` seguia vivo
- hubo que hacer rollback desde
  `/system/lib/libcameraservice.so.backup-20260329-seeddefaults`

Esto descarta el camino de corregir el problema desde `libcameraservice`.

## Wrapper Y210 para `libcamera.so`

Como siguiente paso se creo un wrapper propio en:

- `device/huawei/y210/libcamera/Android.mk`
- `device/huawei/y210/libcamera/Y210CameraWrapper.h`
- `device/huawei/y210/libcamera/Y210CameraWrapper.cpp`

Y se ajusto vendor para dejar el blob real como:

- `system/lib/libcamera.y210.so`

mientras el wrapper se instala como:

- `system/lib/libcamera.so`

La idea del wrapper es:

- hacer `dlopen("libcamera.y210.so")`
- delegar `HAL_openCameraHardware`
- interceptar solo `getParameters()` / `setParameters()`
- probar adaptaciones puntuales sin tocar `frameworks/base`

El wrapper compilo bien en:

- `out/target/product/y210/system/lib/libcamera.so`

## Prueba en vivo del wrapper

Se hizo una prueba adb en vivo:

1. se instalo el wrapper como `/system/lib/libcamera.so`
2. se copio el blob real a `/system/lib/libcamera.y210.so`
3. se abrio Camara y se releyo el dump

Resultado:

- `mediaserver` siguio `running`
- no hubo bootloop por el wrapper
- pero el dump siguio igual:
  - `native_getParameters=`
  - `flatten=`
  - todos los campos relevantes `null`

Conclusion:

- un wrapper pasivo/simple no cambia el problema
- el blob real sigue devolviendo parametros vacios incluso detras del wrapper

## Estado actual real

Lo ya descartado con evidencia:

- swaps directos de `libcamera_client.so`, `libcameraservice.so` y `libmedia.so`
  stock
- arreglarlo en `CameraService`
- arreglarlo solo en la app
- wrapper pasivo simple para `libcamera.so`

Lo que sigue apuntando a causa raiz:

- el blob `libcamera.so` requiere otra secuencia real de inicializacion
  antes de que `getParameters()` deje de salir vacio
- o espera una ABI/runtime mas cercana al entorno stock de Huawei/Qualcomm

Siguiente paso recomendado cuando se retome:

1. extender el wrapper Y210 para forzar una secuencia de inicializacion mas
   activa alrededor de `setParameters()` / `startPreview()`
2. no volver a tocar `frameworks/base/services/camera/libcameraservice`
   para este bug salvo lectura/observacion
