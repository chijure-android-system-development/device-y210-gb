StatusBar (Y210 / CM7) - notas de debug

## Síntoma

Iconos/texto duplicados en la barra superior ("se acumulan"): el mismo contenido
aparece dos veces en el espacio de 25px, con una copia más tenue arriba y otra
más clara abajo. Reproducible al expandir/cerrar el panel de notificaciones o
cuando el ticker de notificaciones se activa.

## Causa raíz (cadena de 3 factores)

### Factor 1 — statusbar_background.9.png tiene alpha=0

`drawable-mdpi/statusbar_background.9.png` es GrayscaleAlpha (ColorType=6) con
píxeles de alpha=0 en la zona del gradiente superior. Cuando el CmStatusBarView
dibuja su background, esas zonas no escriben nada en el buffer PMEM: los píxeles
quedan con el valor anterior.

### Factor 2 — Doble buffer: el back buffer tiene datos de hace 2 frames

Android usa double-buffering para el surface del status bar. Al dibujar en el
buffer B (frame N+2), ese buffer contiene datos del frame N (dos frames antes).
En las zonas que el app NO sobreescribe (las transparentes del background), los
píxeles viejos persisten.

### Factor 3 — Copy-back inestable lo enmascaraba (pero ya no)

El mecanismo de copy-back copiaba el frame N-1 (estado correcto, igual al
actual) hacia el buffer B antes de dárselo al app. Mientras el copy-back
funcionaba, el ghost era invisible (mismo contenido). Cuando se desestabilizaba
(doble-poll del RIL, timing del panel), el buffer B tenía un frame más viejo con
los iconos en posiciones distintas → ghost visible.

## Fixes aplicados (2026-05-31) — todos en patches

### Fix 1: RIL doble-poll (`QualcommNoSimReadyRIL.java`)

**Causa:** El QCRIL del Y210 dispara el evento 1033 (DATA_NETWORK_STATE_CHANGED)
simultáneamente con el 1002 (NETWORK_STATE_CHANGED). La versión original del
patch añadía `mNetworkStateRegistrants.notifyRegistrants()` en los cases 1033 y
1037, causando doble polling de estado de señal → dos actualizaciones rápidas de
iconos por ciclo.

**Fix:** Cases 1033 y 1037 solo hacen `break` (sin notifyRegistrants). El
evento 1002 ya dispara la notificación correcta.

**Archivo:** `frameworks/base/telephony/java/com/android/internal/telephony/QualcommNoSimReadyRIL.java`

### Fix 2: Guard en setIcon (`StatusBarManagerService.java`)

**Causa:** `setIcon()` siempre propagaba a `mBar` aunque el icono no hubiera
cambiado. Además actualizaba `mIcons` con `visible=false` (default del constructor
de StatusBarIcon), lo que desincronizaba el estado de visibilidad y causaba que
`setIconVisibility(false)` fallara silenciosamente (icono fantasma).

**Fix:** Verificar si el icono ya existe con mismo `iconPackage`, `iconId` e
`iconLevel` ANTES de actualizar `mIcons`. Si es idéntico, retornar sin modificar
nada — se preserva el estado de visibilidad y no se llama `mBar.setIcon()`.

**Archivo:** `frameworks/base/services/java/com/android/server/StatusBarManagerService.java`

### Fix 3: Copy-back + buffer zeroing (`Surface.cpp`)

**Causa:** Para superficies de height≤26 (status bar), el copy-back del
frontbuffer hacia el backbuffer dejaba datos de frames anteriores en las zonas
que el status bar no sobreescribía (áreas transparentes del background).

**Fix:** Para `backBuffer->height <= 26`, deshabilitar copy-back Y hacer
`memset(vaddr, 0, ...)` del back buffer tras obtener el puntero. Las zonas
transparentes del background muestran negro en lugar de contenido viejo.
Controlado por `debug.surface.sb_nocopyback` (default `"1"` = activo).

**Archivo:** `frameworks/base/libs/surfaceflinger_client/Surface.cpp`

### Fix 4: Copybit MDP para strip 320×25 (`copybit.cpp`)

**Causa:** El MDP falla al blittear el strip superior (320×25 en y=0) dejando
píxeles viejos cuando SurfaceFlinger intenta usar el compositor hardware.

**Fix:** En `stretch_copybit`, si `dst->h <= 26` retornar `-EINVAL` para forzar
al SurfaceFlinger a usar el compositor software para esa superficie.
Controlado por `debug.copybit.disable_statusbar` (default `"1"` = activo).

**Archivo:** `hardware/msm7k/libcopybit/copybit.cpp`

## Patches donde viven estos fixes

- Fixes 1, 2, 3: `device/huawei/y210/patches/frameworks_base.patch`
- Fix 4: `device/huawei/y210/patches/hardware_msm7k.patch`

## Módulos afectados y cómo recompilar

```bash
# Fix 1 + Fix 2 (Java):
make -j8 services framework
adb remount
adb push out/target/product/y210/system/framework/services.jar /system/framework/services.jar
adb push out/target/product/y210/system/framework/framework.jar /system/framework/framework.jar

# Fix 3 (C++):
make -j8 libsurfaceflinger_client
adb remount
adb push out/target/product/y210/system/lib/libsurfaceflinger_client.so /system/lib/libsurfaceflinger_client.so

# Fix 4 (C++):
mmm hardware/msm7k/libcopybit
adb push out/target/product/y210/system/lib/hw/copybit.msm7k.so /system/lib/hw/copybit.msm7k.so

adb reboot
```

## Override runtime (para debugging)

```bash
# Deshabilitar fix 3 (copy-back):
adb shell setprop debug.surface.sb_nocopyback 0
adb shell 'stop surfaceflinger; start surfaceflinger'

# Deshabilitar fix 4 (copybit):
adb shell setprop debug.copybit.disable_statusbar 0
adb shell 'stop surfaceflinger; start surfaceflinger'

# Restaurar defaults:
adb shell setprop debug.surface.sb_nocopyback 1
adb shell setprop debug.copybit.disable_statusbar 1
adb shell 'stop surfaceflinger; start surfaceflinger'
```

## Doble fecha

Si aparecen 2 fechas:

1) Revisar toggles de CM/SystemUI ("mostrar fecha" + formato).
2) Si persiste, revisar layouts en `packages/SystemUI/res/layout/` (p.ej. `status_bar.xml`, `status_bar_expanded.xml`) por views duplicadas.

## Captura de pantalla (Gingerbread)

En GB no hay `screencap`. Usar el binario `screenshot`:

```bash
adb shell /system/bin/screenshot /mnt/sdcard/tmpshot.bmp
adb pull /mnt/sdcard/tmpshot.bmp .
```

En host, convertir a PNG (si tienes ImageMagick):

```bash
convert tmpshot.bmp tmpshot.png
```

## Nota sobre push de SystemUI.apk

Tras `adb push` de `SystemUI.apk`, puede quedar en loop por `dexopt`/classloader.
Workaround:

```bash
adb shell rm -f /data/dalvik-cache/system@app@SystemUI.apk@classes.dex
adb shell pkill com.android.systemui || true
```
