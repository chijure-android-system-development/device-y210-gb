StatusBar (Y210 / CM7) - notas de debug

## Síntoma

- Ghosting / sobreposición: iconos/texto “se acumulan” en la barra superior al expandir/cerrar o al redibujar.
- A veces se observan 2 fechas (normalmente es configuración/layout; ver más abajo).

## Workaround actual (default)

En este Y210, el path MDP/copybit puede fallar al blittear el strip superior (clip `320x25` en `y=0`), dejando pixels “viejos”.

Workaround: `copybit.y210` devuelve `-EINVAL` cuando detecta el clip exacto del StatusBar, forzando a `SurfaceFlinger` a usar el compositor software para ese frame.

- Código: `device/huawei/y210/libcopybit/copybit.cpp`
- Propiedad: `debug.copybit.disable_statusbar`
  - default: `"1"` (activo)
  - override: `"0"` (desactiva workaround)

Aplicar override runtime:

```bash
adb shell setprop debug.copybit.disable_statusbar 0
adb shell 'stop surfaceflinger; start surfaceflinger'
```

Volver al default:

```bash
adb shell setprop debug.copybit.disable_statusbar 1
adb shell 'stop surfaceflinger; start surfaceflinger'
```

## Doble fecha

Si aparecen 2 fechas:

1) Revisar toggles de CM/SystemUI (“mostrar fecha” + formato).
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
