# Performance Notes

## Estado validado

En el arbol actual se corrigieron dos regresiones reales de rendimiento del port:

1. El script `init.qcom.post_boot.sh` no aplicaba el perfil esperado al Y210
   porque solo reconocia `msm7627a` y no `y210`.
2. El perfil de memoria especifico de 256 MB no quedaba activo al final del boot
   porque `init.rc` base de Gingerbread reescribia despues los knobs genericos
   de `lowmemorykiller`.

## Cambios aplicados

En [`device/huawei/y210/prebuilt/system/etc/init.qcom.post_boot.sh`](/home/chijure/cm7/device/huawei/y210/prebuilt/system/etc/init.qcom.post_boot.sh):

- se agrego `y210` a los `case` que aplican el tuning Qualcomm de CPU;
- se limpio ruido de shell heredado (`/* ... */`) que generaba errores en boot;
- la ejecucion de `hwvefs` ahora queda protegida con `if [ -x ... ]`.

En [`device/huawei/y210/prebuilt/init.huawei.rc`](/home/chijure/cm7/device/huawei/y210/prebuilt/init.huawei.rc):

- se corrigio el import `init.memm.rc` -> `init.mem.rc`;
- se agrego un override final al completarse `bootanim` para reescribir el
  perfil real del `lowmemorykiller` del Y210.

## Valores esperados en runtime

Despues del `boot-lmk-fix.img`, el telefono debe quedar con:

```text
CPU governor: ondemand
CPU min freq: 300000
ondemand up_threshold: 90
ondemand sampling_rate: 25000
LMK adj: 0,2,4,6,7,15
LMK minfree: 1536,2048,3072,4096,5120,6144
```

## Validacion

```bash
adb shell 'cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; \
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq; \
cat /sys/devices/system/cpu/cpu0/cpufreq/ondemand/up_threshold; \
cat /sys/devices/system/cpu/cpu0/cpufreq/ondemand/sampling_rate; \
cat /sys/module/lowmemorykiller/parameters/adj; \
cat /sys/module/lowmemorykiller/parameters/minfree'
```

Salida esperada:

```text
ondemand
300000
90
25000
0,2,4,6,7,15
1536,2048,3072,4096,5120,6144
```

## Lectura practica

- Antes del fix, el device quedaba mas conservador en CPU y con LMK generico.
- Despues del fix, la respuesta de UI mejora y la presion de memoria queda
  alineada con un equipo de 256 MB.
- Esto no resuelve todos los cuellos de botella del port, pero si corrige dos
  regresiones concretas frente al comportamiento esperado del dispositivo.

## `init.qcom.post_boot.sh` nunca se empaquetaba en el build (2026-07-12) — RESUELTO

Verificando en hardware real (equipo CM7, no el de referencia stock) los
valores esperados de esta misma nota, `up_threshold`/`sampling_rate` seguian
en los defaults del kernel (`80`/`50000`) en vez de `90`/`25000`.

**Causa raiz:** `device/huawei/y210/prebuilt/system/etc/init.qcom.post_boot.sh`
existe en el arbol y `init.huawei.rc` ya tiene el `service qcom-post-boot`
(`disabled`, `oneshot`, disparado por `on property:init.svc.bootanim=stopped`),
pero `device_y210.mk` nunca tuvo la linea `PRODUCT_COPY_FILES` para copiarlo a
`system/etc/`. A diferencia de `init.qcom.bt.sh`/`init.qcom.fm.sh`/`init.qcom.wifi.sh`
(mismo patron, si presentes en el `.mk`), el archivo simplemente no llegaba al
`system.img` — el servicio fallaba al arrancar (`sh: no encuentra el script`)
sin bloquear el boot, por lo que pasaba desapercibido.

**Fix:** agregada la linea faltante en `device_y210.mk`:

```
device/huawei/y210/prebuilt/system/etc/init.qcom.post_boot.sh:system/etc/init.qcom.post_boot.sh \
```

**Verificado en equipo real:** `adb push` del script + ejecucion manual primero
(confirmo valores correctos), despues `adb reboot` completo (confirmo que el
`service qcom-post-boot` lo dispara solo al terminar `bootanim`, sin intervencion
manual). Los otros valores del LMK/`vm.*` de esta misma nota SI estaban activos
desde antes porque se escriben directo en `init.huawei.rc` (no dependen de este
script) — solo el tuning de CPU governor estaba afectado.

## Mejoras adicionales sobre el tuning de Qualcomm (2026-07-12)

Con el script ya empaquetado (fix anterior), se investigo en el equipo real
que otras palancas de rendimiento expone el kernel sin usar (governors
disponibles, scheduler de I/O, sysctls de VM) — el kernel no esta disponible
en este entorno de desarrollo (`phoenix-kernel` referenciado en el README raiz
no esta presente en este host), asi que la unica forma de verificar que existe
de verdad fue consultando `/sys` en el equipo con CM7 flasheado.

Bloque nuevo, aislado a `"y210"` (no toca el resto de targets Qualcomm que
comparten este mismo script):

- **CPU governor `interactive`** en vez de `ondemand`: el kernel Qualcomm de
  este SoC ya trae este governor compilado (confirmado via
  `scaling_available_governors`), con sus propios tunables por defecto
  (`go_maxspeed_load=85`, `min_sample_time=80000`) — no son valores inventados,
  son el default del propio kernel, solo fijados explicitamente por si cambian.
  `interactive` reacciona a picos de carga de inmediato; `ondemand` espera a la
  siguiente ventana de muestreo (`sampling_rate`), lo que se siente como lag
  al tocar la pantalla en un solo core.
- **I/O scheduler `noop`** en los `mtdblock*` (YAFFS2 sobre NAND raw): estaban
  en `cfq`, que asume disco rotacional y solo agrega overhead de CPU sin
  beneficio real en flash.
- **`vm.dirty_ratio` 20→10** (`vm.dirty_background_ratio` ya estaba en 5):
  reduce cuanto buffer sucio se acumula antes de forzar writeback sincrono,
  para evitar stalls largos de UI cuando por fin se descarga a la NAND lenta.

**Bug propio encontrado y corregido durante la verificacion:** la primera
version de este bloque condicionaba TODO (governor + tunables) a que ya
existiera `/sys/devices/system/cpu/cpufreq/interactive/` — pero ese directorio
solo lo crea el kernel DESPUES de seleccionar el governor, asi que la condicion
nunca se cumplia y el bloque completo quedaba en no-op. Fix: primero verificar
disponibilidad en `scaling_available_governors`, escribir el governor, y solo
despues (ya creado el directorio) escribir los tunables.

**Verificado en equipo real:** push manual + ejecucion, despues `adb reboot`
completo confirmando los 3 valores tras boot normal (`interactive` con
tunables correctos, `noop` en `mtdblock4`/`mtdblock6`, `dirty_ratio=10`), LMK
intacto, sin `FATAL`/`ANR` en logcat, `system_server`/`systemui`/`launcher`
corriendo con normalidad.
