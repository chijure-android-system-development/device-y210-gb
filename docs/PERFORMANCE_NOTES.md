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
