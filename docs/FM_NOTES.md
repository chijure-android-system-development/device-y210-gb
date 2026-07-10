# FM Radio (Y210)

## Estado actual (CM7) — actualizado 2026-07-06

- **Sintonía/RF: funciona.** `fm_dl` completa, `hw.fm.init=1`, firmware del
  WCN2243 persiste entre aperturas (fix `fminit` + JNI, ver sección
  "Sesión 2026-07-06"), tune real, RSSI/eventos correctos.
- **Audio: sigue sin sonar la emisora real**, pero ya no es silencio total
  en todas las combinaciones — ver la sección "Sesión 2026-07-06 (cont. 4)"
  al final del archivo para la matriz de resultados (estática/zumbido según
  combinación de `config_dac` + `pinSwitchMode`) y qué falta probar a
  continuación (verificar squelch/RSSI real en el momento de la estática).
- Confirmado con audio normal (no-FM, MP3/ringtone) que el codec/jack de
  auriculares en general **funciona bien** — el problema es específico de
  la ruta de audio FM, no de audio en general.
- Estado del código dejado en el default más seguro: `hw.fm.isAnalog=false`,
  `hw.fm.pinSwitchMode=false` (silencio, sin ruido audible) hasta la
  próxima sesión de investigación.
- Pausa de la investigación acordada con el usuario el 2026-07-06 tras una
  sesión larga; retomar desde "Próximos pasos" en la última sección.

## Fix aplicado (init.qcom.fm.sh)

Para que `fm_dl` sea confiable, `init.qcom.fm.sh` ahora hace:

1) `exec 3</dev/radio0` (power-up del transceiver desde el driver)
2) `fm_qsoc_patches $hw.fm.version 0` (download/calibración)
3) `exec 3<&-` (cierra FD)

Esto evita el error temprano `I2C slave addr:0x2a not connected` en `dmesg` y permite que `setprop ctl.start fm_dl`
termine con `hw.fm.init=1`.

## Audio: V4L2 Mute (causa típica de “no suena”)

En el driver tavarua del Y210, `V4L2_CID_AUDIO_MUTE (0x00980909)` es **boolean**:

- `0` = unmute
- `1` = mute (default)

El framework (CAF) estaba seteando `3/4`, lo que fallaba y dejaba el chip en mute.

Fix: `frameworks/base/core/java/android/hardware/fmradio/FmRxControls.java` ahora usa `on ? 1 : 0`.

## Audio: Routing (si “enciende” pero no suena)

Además del unmute, en Y210 el audio FM (analógico) debe rutearse por los paths normales del codec:

- `SND_DEVICE_HEADSET` cuando hay audífonos (antena)
- `SND_DEVICE_SPEAKER` si se fuerza altavoz

En `device/huawei/y210/libaudio/AudioHardware.cpp` se ajustó el routing de `mFmRadioEnabled` para usar
esos devices (stock-like) y no los endpoints `SND_DEVICE_FM_*`, y se mantiene `/dev/msm_fm` abierto mientras FM está activo.

## ¿Qué device usa `fm_qsoc_patches`?

`fm_qsoc_patches` habla por **I2C userspace**, no por V4L2:

- Abre `/dev/i2c-1` (se ve en `/data/app/fm_dld_enable`: `opened FM i2c device node : /dev/i2c-1`).
- El “power-up” previo con `/dev/radio0` es solo para sacar el transceiver de reset/low-power antes del acceso I2C.

## Props mínimas (stock-like)

En Y210 stock el valor de `hw.fm.version` es **`67240453`** (hex `0x4020205`, coincide con `Chip ID 4020205` que reporta el driver).

Recomendado:

- `hw.fm.version=67240453`
- `hw.fm.mode=normal`
- `hw.fm.isAnalog=false`

## Validación rápida

1) Logs del init:

- `adb shell setprop ctl.start fm_dl`
- `adb shell getprop hw.fm.init` (esperado `1`)
- `adb shell cat /data/app/fm_dld_enable`

2) Kernel:

- `adb shell dmesg | grep -i radio-tavarua`

## Comparación stock vs CM7 (hallazgo clave)

- En stock, `fm_qsoc_patches` completa y escribe `fm_qsoc_patches succeeded: 0`.
- En CM7, con un kernel distinto (misma rama `2.6.38.6-perf` pero build diferente), el proceso se queda en timeout esperando `0x00010000`.

Si quieres, la forma más rápida de aislarlo es bootear CM7 con **kernel stock** + ramdisk CM7 (boot.img mixto) y re-probar `fm_dl`.

## Sesión 2026-07-06: audio sigue sin sonar, descartadas varias hipótesis

**Update importante:** en esta sesión `fm_qsoc_patches` **sí completó** (`succeeded: 0`,
las 35 patches + 9 defaults + thresholds bajaron sin timeout) — el hallazgo de
arriba sobre el timeout de kernel no se reprodujo esta vez. No se investigó
por qué (¿kernel distinto al de mayo? ¿condición de carrera que a veces no
ocurre?), pero confirma que el download/calibración no es (al menos no
siempre) el bloqueante.

### Comparación con otro device tree real (Motorola Zeppelin, msm7627, FM funcionando)

Se comparó `device/huawei/y210/libaudio/AudioHardware.cpp` contra
[`LineageOS/android_device_motorola_zeppelin`](https://github.com/LineageOS/android_device_motorola_zeppelin/blob/gb-release-7.2/libaudio/AudioHardware.cpp)
(mismo SoC, FM confirmado funcionando en ese device tree):

- Zeppelin **no tiene** variable `mFmIsAnalog` — Y210 sí, pero queda hardcodeada
  en `false` en el constructor y **nunca se reasigna** en ningún otro lado. Se
  descarta como causa: siempre resuelve a `SND_DEVICE_FM_HEADSET`/`_SPEAKER`
  (las variantes no-analógicas), nunca a las `_ANALOG_*` sin configurar.
- Zeppelin mutea/desmutea FM chequeando el `device` destino dentro de
  `doAudioRouteOrMute()` (`if (device == SND_DEVICE_FM_HEADSET || ...) ear_mute = 0`).
  Y210 en cambio detecta `mFmRadioEnabled` y hace `return` temprano con
  `mute=0` fijo. Distinto camino, mismo resultado neto (`mute=0`) — no parece
  ser la causa tampoco, aunque no se descartó al 100%.
- Ninguna de las dos usa un ID numérico hardcodeado para el RPC — el
  `dev=26` que se ve en los logs de Y210 sale de la tabla de endpoints
  (`CHECK_FOR()` al iniciar `AudioHardware`), coincide con lo que
  documentaba el comentario del stock (`rpc_snd_set_device(26, 0, 0)`), así
  que la resolución de la tabla está bien.

### Prueba en vivo con la app FM reiniciada limpia (proceso viejo tenía un loop
### raro de `Received <0> event`, se mató y se reabrió fresco)

Log completo del intento limpio — **se ve técnicamente correcto de punta a punta**:

```
fmOn -> /dev/radio0 fd=39 abierto, fmConfigure OK, mReceiver.enable Status:true
startFM -> setDeviceConnectionState OK
tuneRadio 97.4 -> FmRxControls Tune Returned: 0
isAntennaAvailable: true   (con auriculares puestos)
AudioHardwareMSM72XX: fm_on received (devices=0x807)
  Routing FM audio to Headset (dev=26)
  unmute for radio
  doAudioRouteOrMute() device SND_DEVICE_FM_HEADSET, ..., "audio circuit active" (NO muted)
```

Aparece una advertencia benigna repetida (`Hardware does not support requested
route combination (0X1804), picking closest possible route...`) pero igual
resuelve a `SND_DEVICE_FM_HEADSET` correcto.

**Con esto: no suena nada.** El equipo con auriculares puestos, `isAntennaAvailable=true`,
ruteo correcto, sin mute — y silencio total (ni siquiera estática).

### Conclusión de la sesión

Todo lo que se puede verificar **desde el lado Android** (JNI, HAL de audio,
selección de endpoint, mute state, tuning) está correcto. El problema tiene
que estar **debajo** de `do_route_audio_rpc()` — la llamada RPC al DSP del
baseband no está abriendo el path físico de audio, o el codec/PMIC no está
recibiendo la señal, y ninguno de los dos es visible desde logcat.

### Próximos pasos sugeridos (sin probar todavía)

1. Instrumentar `do_route_audio_rpc()` (en `hardware/msm7k` o donde viva la
   implementación real de esa función) para loguear el código de retorno de
   la llamada RPC en sí, no solo si Android la invocó.
2. Comparar contra una captura de logcat + `dmesg` del **stock** haciendo
   exactamente lo mismo (encender FM, ver qué RPC/ADSP logs aparecen que acá
   no aparecen).
3. Revisar si el codec de audio (PMIC/msm7k) necesita algún GPIO o registro
   extra específico para habilitar la ruta física FM que el stock setea en
   otro lado (announce de audio path, no solo el snd_device).
4. Confirmar si hay logs de ADSP/QDSP (`adsp`, `dsp_rpc`, `audio_dsp`) en
   `dmesg` en el momento exacto del `fm_on` que indiquen rechazo silencioso.

## Sesión 2026-07-06 (cont.): causa raíz real encontrada — el JNI nunca usaba el fd de `fminit`

**Contexto:** `fminit.c` (ver `device/huawei/y210/fminit/fminit.c`) fue reescrito en
una sesión anterior como daemon persistente: abre `/dev/radio0` UNA sola vez,
carga firmware, y se queda vivo pasando *duplicados* de ese mismo fd a quien
se conecte a su socket abstracto `fminit_radio0` (via `SCM_RIGHTS`). La razón:
`tavarua_fops_open()` en el kernel re-ejecuta la secuencia de init de hardware
(CTL0/CTL1) **en cada `open()`**, lo que borra el firmware ya cargado por I2C.

**El bug:** ese mecanismo de traspaso de fd nunca se conectó del lado cliente.
`fmAcquireFdNative()` en
[frameworks/base/core/jni/android_hardware_fm_qcom.cpp](../../../../frameworks/base/core/jni/android_hardware_fm_qcom.cpp)
hacía un `open("/dev/radio0", O_RDWR)` directo y plano — exactamente el `open()`
extra que `fminit` fue diseñado para evitar. Resultado: cada vez que la app FM
abría el device (para tunear/controlar), el driver volvía a correr el init de
hardware y **borraba el firmware que `fminit` acababa de cargar**, dejando el
chip en modo ROM (sin demodulación real). Esto se confirmó en `dmesg` con:

```
tavarua_radio: Timeout: No Tune response - assuming ROM mode
```

**Fix aplicado:** `fmAcquireFdNative()` ahora intenta primero conectarse al
socket `fminit_radio0` y recibir el fd compartido vía `SCM_RIGHTS`
(`acquireFdFromFminit()`); solo si `fminit` no está disponible cae al
`open()` directo de siempre. Se agregó además un retry corto (hasta 20
intentos x 50ms) porque `fminit` se lanza de forma asíncrona desde
`FMRadioService.onCreate()` y puede no existir todavía (proceso sin fork/exec
completo) cuando la Activity ya está llamando `fmOn()` — sin el retry, la
primera conexión fallaba, caía al `open()` directo, y chocaba con el fd que
`fminit` ya tenía abierto (`Device or resource busy`).

**Validación post-fix (reboot limpio + apertura fresca del app):**

- `I/fmradio_qcom: acquired radio0 fd=44 from fminit` — el traspaso de fd
  funciona.
- `/proc/<pid app>/fd` y `/proc/<pid fminit>/fd` apuntan al mismo
  `/dev/radio0` con distinto número de fd (mismo `struct file`, sin segundo
  `open()`).
- Ya NO aparece `Timeout: No Tune response - assuming ROM mode` — en su
  lugar se ven `INTSTAT1/2/3` reales y eventos de interrupción normales.
- El loop `Received <0> event` (que aparecía en sesiones anteriores)
  **desapareció por completo** (0 ocurrencias en el log de esta prueba).
- Secuencia de audio completa y limpia: `fm_on` → `Routing FM audio to
  Headset (dev=26)` → `audio circuit active` → `isAntennaAvailable: true`.

**Pendiente:** confirmar audio audible en el dispositivo físico con
auriculares puestos (esto no se puede validar por shell/logcat). Si sigue
sin sonar a pesar de que ahora el chip mantiene firmware real entre
aperturas, el problema restante sí estaría más abajo (RPC/ADSP/codec, ver
sección anterior) — pero esta era una causa raíz real y confirmada a nivel
software que había que descartar primero.

**Nota de mantenimiento:** el cambio está hecho directo en
`frameworks/base/core/jni/android_hardware_fm_qcom.cpp` (no vive en un
`.patch` de `device/huawei/y210/patches/`). Si en algún momento se corre
`revert-patches.sh` + checkout limpio de `frameworks/base`, este fix se
pierde — falta capturarlo en un patch (candidato: extender
`frameworks_base_fm.patch`) para que sobreviva a un reset del árbol.

## Sesión 2026-07-06 (cont. 2): con firmware persistente, sigue sin sonar — digital, analógico y HOST_PCM, los tres agotados

Con el fix de `fminit`/JNI en su lugar (firmware real, sin caer a ROM mode),
se probaron **las tres** formas de habilitar audio FM que expone este árbol,
confirmando que **ninguna produce sonido**:

1. **Digital I2S (dev=26, `SND_DEVICE_FM_HEADSET` / `FM_DIGITAL_STEREO_HEADSET`)**
   — la que estaba activa por defecto. RPC sin errores, ruteo correcto,
   "audio circuit active" — silencio total.
2. **Analógico RXOUT (dev=35, `SND_DEVICE_FM_ANALOG_HEADSET` /
   `FM_ANALOG_STEREO_HEADSET`)** — se agregó la property `hw.fm.isAnalog`
   (antes hardcodeada a `false` sin forma de probarla) para poder cambiar de
   ruta en caliente sin recompilar. Mismo resultado: RPC sin errores, ruteo
   correcto, silencio total.
3. **Sesión `audmgr` HOST_PCM vía `/dev/msm_fm`** — según comentario ya
   existente en `setFmOnOff()` (`AudioHardware.cpp:1527`), esto se probó en
   la sesión del 2026-07-04, **después** de que `fminit` empezó a preservar
   el firmware: `AUDIO_START` succeede (`audmgr_enable() OK`) pero tampoco
   produce audio, y además agrega un stutter bloqueante al encender FM.
   Revertido, confirmado como camino muerto.

Como control negativo/positivo: audio normal (ringtone/MP3, no-FM) **sí
suena correctamente por auriculares** en este mismo build
(`Routing audio to Wired Headset` → `set device to SND_DEVICE_HEADSET
device_id=2`). Esto descarta un problema general del codec/jack — el
hardware de audio del equipo funciona; specificamente la ruta FM no.

### Qué NO se ve en dmesg durante `fm_on`

Con el kernel logueando todo lo del chip tuner (`fm_i2c: ...`, comandos
I2C al WCN2243 en `/dev/i2c-1`), se buscó cualquier rastro de actividad en
el **codec/PMIC de audio** (no el tuner) durante el encendido de FM:
`pmic`, `codec`, `adie`, `marimba`, `timpani`, `wcd`, `gpio`, `snd_soc`,
`hph`, `aux_pga`, `mic_bias`. **No aparece nada** (solo un identificador de
familia de chip `Adie type: Bahama` que imprime el propio driver del
tuner, no evidencia de acceso al codec).

Esto es esperable, no necesariamente un error: en esta arquitectura
(msm7x27a con banda base ARM9 separada), la ruta de audio real para
voz/FM/etc. se controla **dentro del firmware cerrado del procesador ARM9**
vía RPC (`rpc_snd_set_device`, `oncrpc`) — el kernel Linux/ARM11 solo ve
"la llamada RPC fue aceptada", nunca los registros reales del codec/PMIC
que el ARM9 mueve internamente. Es una caja negra completa desde este lado.

### Conclusión de esta sub-investigación

Se agotaron **todos** los mecanismos de audio FM expuestos por el código
fuente disponible (HAL de audio + JNI + driver v4l2 tavarua), en sus tres
variantes conocidas, con firmware del chip real y persistente. Ninguno
suena. El defecto — si es de software — vive dentro del firmware cerrado
del ARM9 (la implementación real de `rpc_snd_set_device` para los IDs
FM), fuera del alcance de este árbol fuente. Alternativas para seguir,
todas fuera del alcance de "solo leer/editar código Android":

1. **Comparar contra stock real**: si se consigue un dump de
   logcat+dmesg+radio del ROM stock de Huawei Y210 haciendo exactamente lo
   mismo (FM + auriculares), buscar si hay alguna llamada RPC/ioctl
   *adicional* que el stock hace y que este árbol no reproduce (candidato:
   alguna secuencia de "audio path enable" para el `dev=26` o `35`
   específica de Huawei, no documentada en el comentario CAF genérico).
2. **Multímetro/hardware**: medir si hay señal analógica de audio en el
   pin RXOUT del codec (o en el jack mismo) durante `fm_on` — determina si
   el problema es puramente de software (RPC no logra activar el DAC) o
   si hay algo roto/no conectado a nivel físico en esta unidad específica.
3. **Descartar variante de hardware**: confirmar que este Y210-0151 en
   particular no tiene alguna variante de PCB/BOM sin el path de audio FM
   soldado (poco probable si el chip FM sí sintoniza y reporta RSSI/eventos
   reales, pero no se puede descartar al 100% sin esquema).

Sin acceso a (1) o (2), este hilo de investigación se considera agotado
por ahora desde el lado puramente software/Android.

## Sesión 2026-07-06 (cont. 3): comparación en vivo contra stock real — hallazgo concreto sin probar aún

Se conectó un Y210 con ROM **stock** genuino (`Huawei/Y210/hwy210-0151:2.3.6/
HuaweiY210-0151/C40B851`) para comparar contra CM7. Confirmado: **en stock,
FM sí suena** con auriculares puestos (control positivo de hardware — el
equipo físicamente puede reproducir FM).

### Lo que resultó IGUAL entre stock y CM7 (se descartan como causa)

- **Cadena de conexión de audio estándar**: `packages/apps/FM` (nuestro
  AOSP) y el stock de Huawei (`com.huawei.android.FMRadio`) ambos llaman
  `AudioSystem.setDeviceConnectionState(DEVICE_OUT_FM, AVAILABLE, "")` al
  encender FM (`FMRadioService.startFM()` en nuestro árbol,
  `frameworks/base/services/audioflinger/AudioPolicyManagerBase.cpp:88-96`
  ya maneja esto genéricamente: bump de `mRefCount[AudioSystem::FM]` +
  `setParameters("fm_on=<devices>")`). Se verificó en logcat de CM7 que esto
  YA se dispara correctamente (`fm_on=2055`) — no hace falta portar nada de
  `AudioPolicyManager` para esto, ya funciona igual que el stock.
- **RPC de ruteo**: stock usa exactamente `rpc_snd_set_device(26, 0, 0)`
  (`AudioHardwareMSM76XXA`, ver logcat) — mismo ID, mismos mute flags que
  nuestro `SND_DEVICE_FM_HEADSET`. Ya lo replicamos bien.
- Los endpoints `FM_ANALOG_STEREO_HEADSET`/`FM_ANALOG_STEREO_HEADSET_CODEC`
  existen igual en el binario stock (`libaudio.so`) — nombres coinciden con
  los nuestros.

### Lo que es DISTINTO (candidato real)

Extrayendo `/system/lib/libaudiopolicy.so` y `/system/lib/libaudio.so` del
stock y corriendo `strings`, aparecen funciones/strings propietarias que
**no existen en ningún lado de nuestro árbol** (confirmado con grep global):

```
android::AudioPolicyManager::getFMModeAdapt()
android::AudioHardware::isFMAnalog()
"Selecting AnlgFM + CODEC device %x"
"Rejctng dev conction:Anlg FM & Dgtl FM Mutuly xclusve"
"FM started in %d Mode"
"BTFMPinSwitching"
"switch mode failed with error:%d"
```

Se encontró una implementación de referencia real y abierta que coincide
**textualmente** con estas strings (mismo código CAF que compiló Huawei):
[LineageOS-era Evervolv `AudioPolicyManager.cpp`](https://github.com/akshay4/Evervolv-4.1.2-Pico/blob/master/libaudio/AudioPolicyManager.cpp)
(líneas ~305-330), que confirma: `getFMMode()`/`setFmMode()` deciden
FM_ANALOG vs FM_DIGITAL leyendo `hw.fm.isAnalog`, y rechazan conectar
ambos modos a la vez. Esa parte de exclusividad no afecta el audio en sí
(nosotros solo probamos un modo a la vez, nunca los dos simultáneos).

`BTFMPinSwitching`/`switch_mode` **no aparece** en la referencia abierta —
es una extensión propietaria de Huawei/Qualcomm encima del código CAF
base, sin equivalente open-source encontrado. No se pudo determinar por
disassembly ligero (`strings`) si es un ioctl, una escritura I2C directa,
o algo más — requeriría desensamblar el ARM real.

### Pista más accionable: `fm_qsoc_patches` ya soporta un modo "DAC config" que nunca usamos

El script stock `/system/etc/init.qcom.fm.sh` (extraído del dispositivo,
idéntico en estructura al nuestro) tiene un caso no usado por nuestro
`fminit`:

```sh
"config_dac")
   /system/bin/fm_qsoc_patches $version 3 $isAnalog
   ;;
```

Nuestro propio binario `fm_qsoc_patches` (el prebuilt que ya tenemos en
`device/huawei/y210/prebuilt/system/bin/`, MD5 distinto al del stock pero
**strings idénticos**: `FmDacCodecAnalogConfig`, `FmDacCodecDigitalConfig`,
`FmDacInit`, `"In DAC config mode"`, `"Configuring DAC : Success"`)
confirma que **sí soporta este modo 3** — nunca lo invocamos. `fminit.c`
solo llama `fm_qsoc_patches <version> 0` (descarga de firmware/calibración
RF), nunca el modo 3 (configuración del DAC/pin de salida de audio del
propio chip WCN2243).

**Cambio aplicado (sin probar en hardware todavía):**
`AudioHardware::setFmOnOff(true)` en
[device/huawei/y210/libaudio/AudioHardware.cpp](../../../../device/huawei/y210/libaudio/AudioHardware.cpp)
ahora además ejecuta `fm_qsoc_patches <hw.fm.version> 3 <hw.fm.isAnalog>`
(función `run_fm_dac_config()`) cuando FM se enciende. Compila limpio.
**No se ha instalado ni probado en un Y210 con CM7** — el único Y210 físico
disponible en esta sesión estaba en modo stock (solo lectura, sin
modificaciones, a pedido del usuario). Pendiente para la próxima vez que
el equipo esté de vuelta en CM7:

1. Flashear/arrancar CM7 con este cambio.
2. Encender FM con auriculares puestos y confirmar en logcat/dmesg que
   `fm_qsoc_patches ... 3 ...` corre y termina con
   `"fm_qsoc_patches config_dac(...) succeeded"`.
3. Confirmar si ahora se escucha audio.
4. Si NO suena: revisar si además hace falta un paso de
   "UnConfiguring DAC"/reset entre encendidos, o si el "BTFMPinSwitching"
   propietario es una pieza *adicional* (no reemplazada por config_dac) —
   en ese caso, la única vía que queda es desensamblar el binario stock.

**Resultado (probado en hardware real, Y210 con CM7, 2026-07-06):**
`fm_qsoc_patches config_dac(...)` corre y **succeeded** en log, tanto en
modo digital (`dev=26`) como analógico (`dev=35`) — probadas ambas
combinaciones. **Ninguna de las dos produce audio.** Se descarta
`config_dac` como LA causa raíz (aunque se deja el cambio en el código:
es lo que hace el stock, no debería hacer daño, y podría ser necesario
aunque no suficiente).

Con esto van **cinco** mecanismos distintos probados sin éxito, todos
"exitosos" a nivel de log/RPC pero mudos en la práctica:
digital RPC, analógico RPC, sesión `audmgr` HOST_PCM, `config_dac`
digital, `config_dac` analógico. La conclusión se sostiene:
**`BTFMPinSwitching` es una pieza propietaria genuinamente separada**,
no cubierta por ninguno de los mecanismos documentados/expuestos por los
binarios que ya tenemos (`fm_qsoc_patches`, `fminit`, RPC estándar). Para
seguir desde aquí, la única vía que queda es desensamblar
`libaudio.so`/`libaudiopolicy.so` del stock (ARM) para ver exactamente
qué instrucciones ejecuta esa función — ioctl, escritura I2C directa, o
algo más — ya no es algo alcanzable por inspección de `strings` o pruebas
de caja negra.

## Sesión 2026-07-06 (cont. 4): desensamblado real de BTFMPinSwitching — primer audio audible de toda la investigación

Con el stock conectado en **solo lectura** (sin modificarlo), se extrajeron
`libaudio.so`/`libaudiopolicy.so`/`libfm_hal.so` de forma confiable
(`su -c 'cat X > /data/local/tmp/Y' && adb pull` — el primer intento con
`adb shell "su -c 'cat X'" > file` corrompe binarios por el pipe de texto
de adb shell) y se desensamblaron con `capstone` (Thumb-2) + `pyelftools`
(script propio `disasm.py`, resuelve constantes de pool PC-relative a
direcciones y strings reales — `objdump` solo no calcula esto).

### `enableFM()`/`disableFM()` (stock): confirmado camino muerto

`AudioHardware::enableFM()` en el stock resulta ser **exactamente** el
mismo mecanismo que ya se había probado y revertido en esta sesión:
`open("/dev/msm_fm", O_RDWR)` + `ioctl(fd, AUDIO_START=0x40046100, 0)`.
Mismo dead-end documentado arriba (2026-07-04). Esto descarta definitivamente
esa vía — el stock tampoco la usa como pieza principal.

### `switch_mode()` / "BTFMPinSwitching": decodificado por completo

`switch_mode(int mode)` (exportada en ambas libs, `libaudio.so:0x18968` y
`libaudiopolicy.so:0x67f0`, mismo tamaño — código compartido compilado dos
veces) hace:

1. `open("/dev/i2c-1", O_RDWR)` — mismo bus que `fm_qsoc_patches`, pero...
2. Transacción `ioctl(fd, I2C_RDWR=0x0707, &i2c_rdwr_ioctl_data{...})` con
   **slave address 0x0C** (no 0x07 — el chip FM/WCN2243 usa slave 7; 0x0C
   es otro dispositivo en el mismo bus, probablemente el códec/PMIC del
   lado compartido del pin de audio).
3. Escribe pares `[registro, valor]` de 1 byte cada uno:
   - `mode == 0`: registros `0x88,0x89,0x8a,0x8b` = `0x15`; registros
     `0x8e,0x8f,0x90` = `0x40`.
   - `mode != 0`: los mismos rangos con los valores **intercambiados**
     (`0x88-0x8b`=`0x40`, `0x8e-0x90`=`0x15`).
4. Logea `"BTFMPinSwitching"` / `"switch mode failed with error:%d"` según
   resultado.

**No se encontró ningún `bl`/`blx` a `switch_mode()`** en el `.text` de
ninguna de las dos libs — se invoca casi seguro por vtable/puntero de
función (mismo estilo que el hack de vtable de `Y210CameraWrapper.cpp`),
por lo que no se pudo determinar desde dónde ni con qué valor de `mode` se
llama en el flujo real de encender FM.

### Reimplementación y prueba en hardware real (Y210 CM7)

Se replicó la secuencia I2C exacta en
[AudioHardware.cpp](../../../../device/huawei/y210/libaudio/AudioHardware.cpp)
(`run_btfm_pin_switch()`, controlable por `hw.fm.pinSwitchMode`) y se
probó la matriz completa de combinaciones con `config_dac`
(digital/analógico) × `pinSwitchMode` (0/1), con resultados **reales,
audibles, distintos entre sí** — primera vez en toda la investigación que
sale *cualquier cosa* que no sea silencio total:

| config_dac | pinSwitchMode | dev  | Resultado escuchado |
|---|---|---|---|
| digital | 0 | 26 | Silencio total |
| digital | 1 | 26 | **Estática** (no cambia con la frecuencia) |
| analógico | 1 | 35 | Zumbido/pitido |
| analógico | 0 | 35 | Zumbido/pitido (igual que con mode=1 → el pin switch no afecta en modo analógico) |

Lectura de la tabla:
- El pin-switch **sí importa en modo digital** (0 vs 1 cambia silencio→estática)
  pero **no importa en modo analógico** (mismo zumbido con 0 y 1) — sugiere
  que el zumbido analógico es una entrada sin terminar/mal polarizada
  (hum/buzz típico de un input flotante), no señal real, independientemente
  del pin switch.
- La estática en modo digital+mode(1) es más prometedora (ruido de banda,
  no zumbido de alimentación) pero **no varía con la frecuencia
  sintonizada** — indica que el camino físico de audio ya está conectado
  (primera vez que se escucha *algo*), pero lo que llega no es la señal
  demodulada real: o el tuner no está realmente enganchando la estación
  pese a reportar éxito, o falta un paso de squelch/mute adicional que dejan
  pasar solo ruido de piso en vez del audio demodulado.

### Estado y próximos pasos

- Valores por defecto dejados en el código: `hw.fm.isAnalog=false`,
  `hw.fm.pinSwitchMode=false` (silencio total, el estado más "seguro" por
  ahora — no genera ruido/zumbido audible mientras se sigue investigando).
- Próximo paso sugerido: con digital+pinSwitchMode(1) (el único caso con
  ruido dependiente de banda, no de alimentación), instrumentar/verificar
  el estado real de squelch y el registro de RSSI del tuner en el momento
  exacto en que se escucha la estática, para descartar que sea simplemente
  ruido de piso sin squelch en vez de estática de sintonía.
- Sigue sin confirmarse el call site real de `switch_mode()` en el stock
  (qué código decide `mode` y cuándo se llama) — sin eso, `0`/`1` en
  nuestra reimplementación son una hipótesis binaria, no una certeza. Si
  hay tiempo para seguir, desensamblar `AudioPolicyManager`/`AudioHardware`
  del stock buscando accesos a una vtable/tabla de punteros que incluya la
  dirección `0x18968`/`0x67f0` sería el siguiente paso de reversing.

## Sesión 2026-07-06 (cont. 5): ¿era un blob de vendor faltante? Revisado — no para el audio de FM

A pedido del usuario se revisó sistemáticamente si el problema era simplemente
un archivo de vendor faltante (en vez de lógica). Comparando el dump completo
de `/system` del stock contra CM7:

- `libqcomfm_if.so` (HAL de sintonía Qualcomm, cargado por `libfm_hal.so` vía
  `ro.config.fm_type=libqcomfm_if`) **ya está en nuestro árbol, MD5 idéntico
  al stock** (`device/huawei/y210/prebuilt/system/lib/hw/libqcomfm_if.so`),
  pero no se usa: nuestra app FM (`com.android.fm`) usa el JNI propio
  (`android_hardware_fm_qcom.cpp` → `/dev/radio0` directo), no este HAL. Se
  desensambló y confirmó que solo maneja sintonía/control (`fm_setControl`,
  `interrupt_thread`, `process_radio_event`), sin tocar I2C del códec — no
  es la pieza de audio que falta.
- `libsnd.so` (cliente RPC `snd_*`/ADIE, falta en CM7) — el propio
  `libaudio.so` del stock **no lo enlaza** (`readelf -d` sin `NEEDED
  libsnd.so`), así que no es parte del camino de audio real.
- Sin firmware externo específico del WCN2243/Bahama en el stock (viene
  embebido compilado dentro de `fm_qsoc_patches`, ver `Fm_qsoc_patches.c`
  strings) — nada que copiar ahí.
- **Sí se encontró un blob real mal puesto, mas sin relación con FM:**
  el stock abre `/system/etc/AudioFilter_%s.csv` (confirmado por strings en
  su `libaudio.so`) **antes** del genérico `AudioFilter.csv` — nuestro
  código solo leía el genérico. El archivo específico del Y210
  (`AudioFilter_MSM7225A_Y210.csv`, copiado del dump stock) tiene
  coeficientes de EQ/MBADRC de **parlante** (A1/D1) distintos a los del
  genérico. Los filtros de **auricular** (A3/C3/D3, la ruta que usa FM) ya
  eran idénticos entre ambos archivos (confirmado en sesión anterior), así
  que esto no explica el silencio de FM, pero es una mejora real de calidad
  de audio para llamadas/multimedia. **Corregido:** `AudioHardware.cpp`
  (`get_audpp_filter()`) ahora intenta primero
  `AudioFilter_MSM7225A_Y210.csv` y cae al genérico si no existe; archivo
  agregado a `PRODUCT_COPY_FILES` en `device_y210.mk`. Compilado, instalado
  y confirmado en logcat: `open /system/etc/AudioFilter_MSM7225A_Y210.csv
  success.`

**Conclusión:** no hay un blob suelto pendiente de copiar para el audio de
FM. La pieza real (`switch_mode`/BTFMPinSwitching) vive dentro de todo el
binario `libaudio.so`/`libaudiopolicy.so` del stock, que es un HAL de audio
completo y distinto al nuestro (reescrito desde cero para CM7) — no es un
archivo aislado que se pueda copiar sin más. Las opciones que quedan siguen
siendo: (a) seguir afinando la reimplementación por ingeniería inversa, o
(b) reemplazar el HAL de audio completo por los binarios reales del stock
(riesgo: podría afectar llamadas/multimedia por incompatibilidad de ABI) —
pendiente de decisión del usuario, no iniciado.

## Sesión 2026-07-06 (cont. 6): `audmgr_enable()` probado como prerrequisito junto a config_dac/switch_mode — descartado definitivamente

Con el stock funcionando de verdad (radio de un usuario conectado en modo
lectura), se capturó `dmesg` de un ciclo real de encendido de FM y se vio
algo que nunca habíamos combinado con nuestros fixes:

```
audmgr_enable(session) -> RPC READY -> RPC CODEC_CONFIG(volume=0x...)
-> snd_set_device(26, 0, 0) -> snd_set_volume(...)
```

`audmgr_enable()`/`AUDIO_START` vía `/dev/msm_fm` (el mecanismo que se
había probado en soledad el 2026-05-24 y el 2026-07-04, y descartado como
"camino muerto") **sí es parte del flujo real** — pero nunca lo habíamos
probado combinado con `config_dac`/`switch_mode` (ninguno de los dos
existía en 2026-07-04). Se restauró `run_fm_audmgr_session()` en
`AudioHardware::setFmOnOff()`, llamado **antes** de `config_dac`/
`switch_mode`, replicando el orden exacto visto en el stock.

**Resultado en hardware real (Y210 CM7):** el log coincide perfectamente
con el del stock (`audmgr: AUDIO_START succeeded` → `config_dac(...)
succeeded` → `switch mode(1) succeeded` → `Routing FM audio to Headset` →
`snd_set_device`/`snd_set_volume` reales en dmesg, incluso con valores de
`CODEC_CONFIG volume=0x...` dinámicos del DSP, igual que en stock) — pero
el audio empeoró: **silencio total**, tanto en digital (antes daba
estática) como en analógico (antes daba zumbido). Agregar `audmgr_enable()`
no habilita nada — **apaga** lo poco que ya sonaba.

**Conclusión definitiva:** la evaluación original de 2026-05-24/07-04 era
correcta. El `audmgr`/`audio_fm.c` de ESTE kernel reclama el códec/DAC
para su propia sesión PCM de host de una forma que choca con la ruta
I2S/analógica de FM (que se maneja por RPC directo, `rpc_snd_set_device`,
no por PCM), en vez de habilitarla. No es una pieza faltante — es un
mecanismo genuinamente incompatible en este kernel/HAL, aunque el binario
stock real sí lo use exitosamente (su kernel/firmware deben manejar la
combinación de forma distinta). **Revertido** — `run_fm_audmgr_session()`
eliminado de nuevo, dispositivo devuelto al estado seguro por defecto
(`hw.fm.isAnalog=false`, `hw.fm.pinSwitchMode=false`, silencio sin ruido).

Con esto, la pista más prometedora conocida sigue siendo digital +
`pinSwitchMode=1` (estática, sin `audmgr`) — el próximo paso documentado
sigue siendo verificar el RSSI/squelch real del tuner en ese momento
exacto, no agregar más pasos de habilitación de audio.

## Hipótesis pendientes (2026-07-09), en orden de qué tan accionables son

1. **RSSI/squelch real durante la estática.** Con `digital +
   pinSwitchMode=1` (la única combinación con ruido de banda, no de
   alimentación), verificar si el tuner realmente engancha la señal o si
   el driver reporta "tune exitoso" mientras el chip sigue sin demodular
   de verdad. La más barata de probar — solo hace falta leer el registro
   de RSSI/estado del tuner mientras suena la estática, no requiere
   recompilar nada nuevo.
2. **GPIO de antena/pin compartido a nivel de kernel (no I2C).** El jack
   de 3.5mm en combos BT+FM suele compartir el mismo pin entre
   "micrófono" y "antena/audio FM", conmutado por un GPIO que maneja el
   driver del kernel (no I2C, no RPC de audio). Si el devicetree/board
   config de nuestro kernel compilado (`phoenix-kernel`) difiere del que
   trae el stock aunque la fuente sea "la misma", ese GPIO podría no
   estar activado. No se revisó en ningún momento de esta investigación —
   todo el esfuerzo fue en I2C (`switch_mode`) y RPC (`snd_set_device`,
   `audmgr`).
3. **Calibración de `fm_qsoc_patches` distinta a pesar de mismos
   strings.** Nuestro binario y el del stock tienen MD5 distinto. Ya se
   comparó que al menos `gFMQSocPatches2243_20_Poke1`/`Poke2` salen
   idénticos en los logs de ambos (`0x0 0x0 0x19 0x22` / `0x0 0x0 0x43
   0x5`), así que es poco probable, pero no se comparó el resto de la
   tabla de calibración — podría haber otro valor específico de audio que
   sí difiera.
4. **Otra llamada RPC completamente distinta**, no `rpc_snd_set_device`
   ni `switch_mode` — un programa/procedimiento RPC separado para
   habilitar el paso físico FM→códec que no aparece en ninguno de los
   binarios ya desensamblados (`libaudio.so`, `libaudiopolicy.so`,
   `libfm_hal.so`) porque vive en otro lugar (¿kernel? ¿otro `.so` no
   revisado?).

### Intento de hipótesis 1 (2026-07-09) — inconcluso, no confiar en el resultado

Se encontró que `device/huawei/y210/prebuilt/system/bin/fmconfig` (ya
presente en nuestro árbol, MD5 idéntico al stock) es una herramienta CLI
interactiva real de Qualcomm para controlar `/dev/radio0` directamente
(`enable`/`setfreq`/`seek`/`scan`/`getconfig`), útil en teoría para leer
RSSI sin pasar por toda la app Java.

**Problema metodológico encontrado:** `fmconfig` hace su propio `open()`
crudo sobre `/dev/radio0`, incompatible con el modelo de `fminit` (que
mantiene el fd abierto para siempre y lo comparte por socket). Para poder
usar `fmconfig` hubo que matar `fminit`, lo cual dejó al dispositivo sin
nadie sirviendo el fd — cada `open()` de `fmconfig` re-ejecuta el init de
hardware del driver (`tavarua_fops_open`), el mismo bug que `fminit` fue
diseñado para evitar. Tras varias invocaciones de `fmconfig`, el RSSI leído
salió **plano en toda la banda** (147-149 sin variar de 88 a 108 MHz) y
`scan`/`searchlist` no encontraron ninguna estación — pero al volver a
abrir la app real después, reapareció brevemente el viejo bug del loop
infinito `Received <0> event` (evidencia de firmware en modo ROM). Esto
indica que el RSSI plano fue muy probablemente un **artefacto del propio
método de prueba** (firmware wipeado a mitad de las pruebas), no evidencia
de un problema real de recepción — que además contradice el hallazgo ya
documentado y confiable de sesiones anteriores (alternancia real de
`STEREO_EVENT`/`MONO_EVENT` sintonizando 98.1 MHz).

**Conclusión:** hipótesis 1 queda **sin resolver** (ni confirmada ni
descartada) — no repetir con `fmconfig` mientras `fminit` esté activo.
Si se quiere retomar, la forma correcta sería instrumentar RSSI dentro del
flujo normal de la app (agregar un log a `FMRadio.java`/`FmReceiver.java`
donde ya se lee `getRSSI()` para la UI, que hoy no lo loguea) en vez de
usar una herramienta externa con su propio ciclo de vida de apertura del
device. Se reinició el equipo para limpiar el estado tras esta prueba.

## Sesión 2026-07-09: descompilando la app FM real del stock — descarta la hipótesis de orquestación Java

A pedido del usuario ("¿y si descompilas la app de radio original?"), se
descompiló `FMRadio.apk`/`.odex` del stock con `apktool`+`baksmali deodex`
(bootclasspath armado con los `.jar` del propio dump de `/system/framework`
del stock; API level 10). Ambos archivos están **odexeados** — el `.apk`
no tiene `classes.dex`, todo el bytecode vive en `FMRadio.odex`, así que
hubo que deodexear en vez de solo `apktool d`.

**Hallazgo:** `FMRadioService2.startFM()`/`stopFM()` (Java) solo mandan un
`sendBroadcast(new Intent("android.intent.action.FM", extra state=1/0))`
— no hay ninguna llamada nativa/JNI adicional, ni referencia a
`switch_mode`, `config_dac`, `audmgr`, ni `isAnalog` en toda la app
(confirmado con grep sobre las ~4300 clases descompiladas). También se
descompiló `services.odex`/`framework.odex` del stock (mismo método,
`AudioService`/`AudioServiceBroadcastReceiver` viven en `framework.jar`,
no en `services.jar`) para ver qué hace el framework Huawei-patcheado al
recibir ese intent:

```java
// android.media.AudioService$AudioServiceBroadcastReceiver, reconstruido del smali
if (action.equals("android.intent.action.FM")) {
    int state = intent.getIntExtra("state", 0);
    if (state == 1) {
        AudioSystem.setDeviceConnectionState(0x800 /* DEVICE_OUT_FM */, 1 /* AVAILABLE */, "");
    } else {
        AudioSystem.setDeviceConnectionState(0x800, 0 /* UNAVAILABLE */, "");
    }
}
```

**Es exactamente la misma llamada que ya hace nuestro propio
`FMRadioService.java`** (`AudioSystem.setDeviceConnectionState(DEVICE_OUT_FM,
...)`), solo que el stock la dispara indirectamente vía broadcast+receiver
en vez de llamarla directo. No hay ninguna orquestación Java oculta, ni
un paso adicional a nivel framework — se descarta por completo la
hipótesis de que faltaba algo en `AudioService`/la app Java. El misterio
sigue enteramente en la capa nativa C++ (`libaudio.so`/`libaudiopolicy.so`)
ya desensamblada en sesiones anteriores — específicamente, en encontrar el
call site real de `switch_mode()` (invocado casi seguro por vtable/puntero
de función, nunca localizado) para saber con certeza cuándo y con qué
"mode" se invoca de verdad.

Herramientas que quedaron listas para reutilizar en `/tmp/.../scratchpad/`
de esta sesión (rutas efímeras, se pierden al cerrar la sesión, pero el
método queda documentado aquí para repetirlo):
`apktool d` + `baksmali deodex -a 10 -b <bootclasspath de los .jar del
dump stock>` sobre cualquier `.apk`/`.odex` del stock.

## Sesión 2026-07-09 (cont.): dos bugs reales encontrados intentando validar RSSI — hipótesis 1 sigue sin resolver

Se intentó retomar correctamente la hipótesis 1 (instrumentar RSSI en la
app real en vez de `fmconfig`). Se agregó un log en
`FMRadioService.java` → `FmRxEvRadioTuneStatus()` que llama a
`mReceiver.getRssi()` inmediatamente y de nuevo 1.5s después (nadie en
este árbol llamaba a `getRssi()` antes; solo existía el método sin uso).
Al probar esto se encontraron **dos bugs reales, independientes entre sí
y de la pregunta original de audio**:

### Bug 1 (encontrado y arreglado): margen de conexión a `fminit` insuficiente en cold boot

Tras un reboot limpio, el JNI (`fmAcquireFdNative`) agotaba sus 20
reintentos (1s total, fix del 2026-07-06) contra el socket de `fminit` y
caía al `open()` directo, que fallaba con `Device or resource busy`
porque `fminit` ya tenía el device abierto — cayendo de nuevo en el bug
viejo del firmware borrado (`Received <0> event` en loop infinito).
Reproducido de forma consistente en cold boot (no en reinicios de la app
sola). Se probó ampliar la ventana a 100 intentos x 100ms (10s) como
diagnóstico: **conectó bien** (`acquired radio0 fd=44 from fminit`),
confirmando que era pura falta de margen (el sistema bajo carga de boot
completo tarda más en fork/exec/bind el socket de lo que tarda un simple
restart de la app). Se dejó en **60 intentos x 100ms (6s)** como balance
entre margen y no bloquear demasiado tiempo en un fallo real. Cambio en
`frameworks/base/core/jni/android_hardware_fm_qcom.cpp`.

### Bug 2 (encontrado, SIN resolver): eventos de sintonía no llegan a Java en algunas sesiones

Con el Bug 1 ya resuelto (fd compartido correctamente), la app llega hasta
`tuneRadio: 98.1` sin error, pero **ningún evento llega nunca a Java**:
ni `Received <N> event`, ni `FmRxEvRadioTuneStatus`, ni por lo tanto el
nuevo log de RSSI. Se confirmó en paralelo por `dmesg` que el **driver del
kernel sí genera eventos reales** (`updating event_q with event 9/a/d/8`,
lecturas I2C reales al chip) — el problema está en la entrega del evento
al listener de Java (`FmReceiverJNI`/thread "Starting listener N"), no en
el chip ni en el driver. Probado dos veces (cold boot y con `fminit` ya
tibio en un segundo lanzamiento de la app) con el mismo resultado: la
app queda "colgada" esperando eventos que nunca llegan, sin excepción ni
error visible en logcat. No se investigó la causa (¿el fd compartido vía
`SCM_RIGHTS` no preserva el estado de suscripción a eventos V4L2 por
alguna razón? ¿el thread del listener nativo muere silenciosamente?).

**Impacto:** esto bloquea la hipótesis 1 (no se pudo leer RSSI en ningún
intento de esta sub-sesión) y **pone en duda el estado "Sintonía/RF:
funciona" documentado como bueno** al principio de este archivo — esa
confirmación fue de sesiones anteriores; no está claro si este bug de
entrega de eventos es nuevo o simplemente no se había notado porque las
pruebas anteriores no dependían de que el evento de tune llegara a Java
(dependían de logs de `dmesg`/logcat nativo, no del callback Java).

**Próximo paso sugerido:** antes de retomar cualquier otra hipótesis de
audio, confirmar si este bug de entrega de eventos es reproducible de
forma consistente, y si aparece incluso SIN el fix del Bug 1 (con un
`open()` directo fresco, sin pasar por el socket de `fminit`) — eso
aislaría si el problema es específico del mecanismo de fd compartido o
algo más general del listener nativo.

### Bug 2, causa raíz encontrada: `read()` en el JNI nunca pudo recibir eventos generales — solo RDS

Se aisló la variable (probado con `open()` directo, matando `fminit` y
fijando `hw.fm.init=1` para que no se relance) — el problema persiste
igual sin `fminit` de por medio. Se revisó el driver del kernel real
(`kernel-c660-src/drivers/media/radio/radio-tavarua.c` — mismo chip/familia
tavarua, MSM7627a; el `phoenix-kernel` mencionado en la memoria del
proyecto ya no existe en el contenedor, se perdió en algún momento al
recrearse) y se encontró la causa raíz exacta:

- `tavarua_fops_read()` (la función detrás de la syscall `read()`, la
  única que usa nuestro JNI en `fmGetBufferNative()`,
  `frameworks/base/core/jni/android_hardware_fm_qcom.cpp:275-286`) **solo
  lee del kfifo `TAVARUA_BUF_RAW_RDS`** y espera en
  `radio->read_queue`. Es *exclusivamente* para datos RDS (texto de
  radio, nombre de programa, etc).
- Los eventos generales (`TUNE_EVENT`, `SEEK_COMPLETE`, `STEREO`/`MONO`,
  `BELOW_TH`/`ABOVE_TH`, etc.) se encolan via `tavarua_q_event()` en un
  kfifo **completamente distinto** (`TAVARUA_BUF_EVENTS`) y despiertan
  una wait-queue **completamente distinta** (`radio->event_queue`) — no
  la misma que usa `read()`.
- El propio comentario de `tavarua_q_event()` en el driver lo dice
  explícitamente: *"Applications call the VIDIOC_QBUF ioctl to enqueue...
  refer tavarua_probe where we register different ioctl's for FM"* — el
  mecanismo correcto para dequeue de eventos generales es **ioctl
  (VIDIOC_DQBUF)**, no `read()`.

**Esto significa que nuestro JNI nunca pudo recibir eventos generales
reales por el camino que usa (`read()`)** — solo puede desbloquearse
cuando llega dato RDS real (que despierta `read_queue`, no
`event_queue`). Cualquier "Received `<N>` event" que coincidiera con un
evento esperado (p.ej. `TUNE_EVENT`=1) en sesiones anteriores es
sospechoso de ser una **coincidencia**: un byte de RDS que por azar cae
en el rango de un código de evento válido, no una entrega real del
evento correspondiente. **Esto pone en duda directamente las
confirmaciones previas de "sintonía real"/"STEREO_EVENT y MONO_EVENT
alternando" documentadas en sesiones anteriores** (2026-07-04 y otras) —
esas lecturas pudieron haber sido ruido de RDS mal interpretado, no
evidencia genuina de recepción.

**FIX IMPLEMENTADO Y CONFIRMADO EN HARDWARE REAL (2026-07-09):**
`fmGetBufferNative()` ahora usa `ioctl(fd, VIDIOC_DQBUF, &v4l2buf)` en vez
de `read()`, con:
- `v4l2buf.type = V4L2_BUF_TYPE_PRIVATE` (requerido por el dispatcher
  genérico `v4l2-ioctl.c`'s `check_fmt()`, que verifica el tipo contra los
  `vidioc_g_fmt_*` que registra el driver — tavarua solo registra
  `vidioc_g_fmt_type_private`, así que **PRIVATE** es el único tipo válido
  para este driver. Sin esto: `EINVAL`.)
- `v4l2buf.memory = V4L2_MEMORY_USERPTR` (sin esto, también `EINVAL`).
- `v4l2buf.index = TAVARUA_BUF_EVENTS` (=1, del enum `tavarua_buf_t` en
  `media/tavarua.h` del driver: SRCH_LIST, EVENTS, RT_RDS, PS_RDS,
  RAW_RDS, AF_LIST).
- `v4l2buf.m.userptr`/`v4l2buf.length` apuntando al buffer Java.

**Resultado en hardware real:** desaparece por completo el loop infinito
`Received <0> event, Int: -1`. Aparecen eventos genuinos y coherentes:
`READY_EVENT` (una sola vez), `TUNE_EVENT` con
`FmRxEvRadioTuneStatus: Tuned Frequency: 98100` disparando el callback
Java correctamente, `RSSI check @ 98100: immediate=100` /
`+1500ms=100` (antes: código muerto, nunca se llamaba a `getRssi()` en
todo el árbol), y eventos reales de umbral/estéreo
(`ABOVE_TH`/`BELOW_TH`/`STEREO`/`MONO`). Probado también con seek
(`KEYCODE_MEDIA_NEXT` vía `input keyevent`, dispara
`FMMediaButtonIntentReceiver`): sintoniza `97400`, mismo patrón de
eventos reales, RSSI=100 también ahí.

**Conclusión de la hipótesis 1, ahora sí cerrada:** con eventos genuinos
(ya no sospechosos de ser ruido RDS), se confirma que el tuner **sí
engancha señal real** (RSSI=100, eventos de umbral/estéreo coherentes)
en las combinaciones probadas — pero el usuario confirmó en el mismo
momento, probando en el equipo, que **sigue sonando estática**, igual
que antes de este fix. Esto descarta definitivamente que el problema de
audio sea "el tuner no sintoniza de verdad" — el tuner funciona
correctamente; el problema sigue siendo 100% el camino de audio
(códec/DAC/`switch_mode`/RPC) ya investigado en sesiones anteriores. La
hipótesis 1 queda **resuelta como "no es la causa"**, no como pendiente.

**Nota aparte, no relacionada al audio pero real y corregida:** este fix
significa que **todas las confirmaciones anteriores de "STEREO_EVENT/
MONO_EVENT alternando" de sesiones previas a hoy eran, con alta
probabilidad, ruido de RDS mal interpretado**, no eventos reales — el
código con el bug (`read()`) es el mismo que estuvo en el árbol desde que
se escribió el JNI. No afecta ninguna conclusión de audio (esas siempre
se basaron en logs nativos/`dmesg`, no en el callback Java), pero sí
significa que cualquier futura referencia a "confirmado por
STEREO_EVENT" de antes del 2026-07-09 debe re-leerse con esta duda.

### Otras hipótesis descartadas hoy (2026-07-09), buscando en los controles V4L2 del chip

Con el pipeline de eventos ya funcionando, se revisaron controles V4L2 a
nivel del CHIP tavarua (no del códec) que pudieran estar mal
configurados y ser la causa de la estática:

- **`V4L2_CID_AUDIO_MUTE`** (mute a nivel del chip, registro `IOCTRL`,
  I2C slave 7 — distinto del mute que maneja `AudioHardware.cpp` a nivel
  RPC/códec): `FMRadioService.fmOn()` ya llama
  `setMuteMode(FM_RX_UNMUTE)` (`=0`) inmediatamente después de
  `enable()`. Revisado el handler en el driver
  (`tavarua_vidioc_s_ctrl`, case `V4L2_CID_AUDIO_MUTE`) — coincide con el
  fix ya documentado en sesiones anteriores (booleano, no 3/4). Ya estaba
  bien, no es la causa.
- **`V4L2_CID_PRIVATE_TAVARUA_EMPHASIS`** (de-énfasis 50us vs 75us —
  un de-énfasis incorrecto podría sonar como ruido/estática áspera):
  nuestro log muestra `Emphasis :1`, que corresponde a
  `FmTransceiver.FM_DE_EMP50` (**50us, correcto para esta región**, no
  la variante US/Japón de 75us que es `FM_DE_EMP75=0`). Confirmado
  correcto, no es la causa.
- **`V4L2_CID_PRIVATE_TAVARUA_ANTENNA`** (selección de antena interna vs
  headset, registro `IOCTRL`): dado que el RSSI genuino confirmado hoy
  es alto (100) y consistente, la selección de antena ya debe estar
  correcta — si estuviera mal, no habría señal real que recibir.

**Conclusión de esta sub-búsqueda:** ningún control V4L2 estándar/privado
del chip tavarua revisado hoy explica la estática. Refuerza que el
problema sigue estando en la ruta códec/DAC (`switch_mode`/
`BTFMPinSwitching`/`config_dac`), ya extensamente investigada, no en la
configuración del chip tuner en sí.

### Experimento: instalar los binarios reales del stock — descartado por incompatibilidad de ABI, no por audio

Se probó instalar `libaudio.so`+`libaudiopolicy.so` **reales** del stock
directamente en el CM7 (solo para la prueba, con backup previo de
nuestros binarios). **Resultado: colgó el boot.** `system_server` nunca
llegó a arrancar (solo `zygote` quedaba corriendo, sin `mediaserver` ni
`system_server`) — indica que `libaudiopolicy.so` del stock es
**binariamente incompatible** con nuestro `frameworks/base` (versión de
`AudioPolicyManagerBase`/ABI de C++ distinta a la que Huawei compiló
contra). Restaurados los backups, reboot, sistema operativo normal de
nuevo (confirmado `mediaserver`/`system_server` corriendo).

**Conclusión:** el swap directo de binarios NO es viable sin más trabajo
(habría que actualizar/parchear nuestro `frameworks/base` para que sea
ABI-compatible con lo que Huawei compiló, lo cual es un proyecto en sí
mismo, no un atajo). Esto no aporta evidencia sobre si el HARDWARE está
sano o no — el experimento nunca llegó a probar audio, falló antes,
por incompatibilidad de software. Se descarta esta vía; volver a la
reimplementación desde el código fuente es el único camino práctico
que queda.

## Sesión 2026-07-09 (cont. 2): se encontró el código fuente REAL de BTFMPinSwitching — implementado correctamente, mismo resultado

Búsqueda web dirigida encontró
[`dzo/hardware_qcomm_media`](https://github.com/dzo/hardware_qcomm_media/tree/master/audio/msm7627a),
un `AudioHardware.cpp` real de CAF para msm7627a que **incluye
`HardwarePinSwitching.c`** — el código fuente real de `switch_mode()`
que solo habíamos podido reversar desde el binario. Coincide
byte-por-byte con lo que ya habíamos decodificado, pero revela dos
cosas que teníamos mal:

1. **Los valores de "mode" están invertidos respecto a como los
   probamos:** `MODE_FM=0` (tristate BT: regs `0x88-0x8b`=`0x15`;
   activa FM I2S: regs `0x8e-0x90`=`0x40`) y `MODE_BTSCO=1` (al revés).
   Nuestra propiedad `hw.fm.pinSwitchMode=true/false` de sesiones
   anteriores tenía la matemática de bits correcta pero la **etiqueta
   invertida** — lo que llamábamos "mode=1" (que daba estática) era en
   realidad `MODE_BTSCO` (tristatea los pines de FM, deja flotando el
   bus I2S hacia el códec — explica el ruido: líneas digitales
   flotantes leídas como PCM suenan a ruido blanco). Lo que llamábamos
   "mode=0" (que daba silencio total) era en realidad el `MODE_FM`
   correcto.
2. **El orden real es RPC primero, `switch_mode()` después**, y solo en
   una transición real de dispositivo (`device != mCurSndDevice`) — no
   en cada `setFmOnOff(true)`. Nuestra implementación anterior lo
   llamaba en el momento equivocado (dentro de `setFmOnOff`, antes de
   que `doAudioRouteOrMute()` hiciera el RPC).

**Fix aplicado:** `run_btfm_pin_switch()` ahora usa `MODE_FM=0`/
`MODE_BTSCO=1` con los nombres correctos, se llama desde
`doAudioRouteOrMute()` **después** de `do_route_audio_rpc()`, condicionado
a una transición real hacia/desde un dispositivo FM (replicando
exactamente el patrón de la referencia real). Además se probó
temporalmente **sin** `config_dac` (que no existe en esta referencia real
en absoluto) para descartarlo como interferencia.

**Resultado en hardware real:** log confirma el orden correcto
(`Routing FM audio to Headset` → `switch mode(0) succeeded`, es decir
`MODE_FM`). **El usuario confirmó que el sonido es idéntico a las
pruebas anteriores** — con o sin `config_dac`, con el modo y el orden
ya verificados contra el código fuente real. Dato cualitativo nuevo e
importante: el usuario describe el ruido como **"ruido blanco/
interferencia", explícitamente NO como estática de radio real** —
sugiere una fuente de ruido genérica (p.ej. líneas digitales sin
terminar correctamente) más que un problema de demodulación de RF.

**Conclusión honesta de esta sub-investigación:** con `switch_mode`
ahora implementado y verificado como **correcto** contra el código
fuente real (no una suposición nuestra), y el resultado audible
**sin cambios** en absoluto entre las variantes probadas (mode correcto
vs incorrecto, con y sin `config_dac`) — esto es evidencia fuerte de
que **el problema no está en el mecanismo de `switch_mode`/pin-switching
en sí**, contradiciendo la hipótesis principal de todo el día. El ruido
parece ser una característica de base del hardware/ruta de audio de
esta unidad que no cambia con ninguna de las variantes de software
probadas hasta ahora.

## Sesión 2026-07-09 (cont. 3): kernel real localizado — dos hallazgos concretos, ninguno cambia el audio

El usuario señaló que el kernel fuente REAL del Y210 sí está disponible
localmente en `/media/chijure/Datos/Desarrollo_Android/y210/phoenix-kernel`
(la nota vieja de memoria sobre `phoenix-kernel` en el contenedor Docker
ya no aplica — se perdió al recrearse el contenedor en algún momento, pero
el código fuente en sí nunca dejó de estar disponible en esta ruta local).
Todo el análisis de kernel de sesiones anteriores había usado
`kernel-c660-src` (de OTRO dispositivo) como proxy — confirmado hoy que
difieren en detalles reales (`config_i2s`/`FM_I2S_ON` no existe siquiera
en `kernel-c660-src`, por ejemplo).

### Hallazgo 1 (arreglado): registro de FPGA inexistente escrito en cada encendido de FM

`msm_bahama_setup_pcm_i2s()` (`board-msm7x27a.c`) llama
`config_pcm_i2s_mode()`, que escribe un registro de **FPGA**
(`FPGA_MSM_CNTRL_REG2 = 0x90008010`) — hardware que solo existe en
placas de referencia/desarrollo de Qualcomm con FPGA para rutear I/O
reconfigurable. El código original tenía un chequeo
(`machine_is_msm7x27a_surf() || machine_is_msm7625a_surf()`) para
correr esto SOLO en esas placas de referencia. El parche de Huawei
**eliminó ese chequeo por completo** (comentario en el código: "delete
the judgement of board_id, this is public platform code"), y se
confirmó que `HUAWEI_BT_WCN2243` está definido en el build real
(`-DHUAWEI_BT_WCN2243` en el comando de compilación) — por lo tanto
esta escritura a un registro de FPGA que no existe en el Y210 corría
**en cada encendido de FM**, sin ningún chequeo de placa.

**Fix:** restaurado el chequeo `machine_is_msm7x27a_surf()`/
`machine_is_msm7625a_surf()` en ambos call sites (`config_i2s()` para
FM, `config_pcm()` para BT), tanto en la rama Huawei como en la
genérica. Kernel recompilado (`make zImage`, incremental, con el
toolchain `arm-eabi-4.4.3`), `boot.img` reconstruido con `mkbootimg`
(reusando el ramdisk/cmdline/base/pagesize del boot.img actual,
extraídos con `unpackbootimg`), flasheado con `flash_image boot` (NO
`dd` — falla en este MTD, ver nota de memoria del proyecto). **Backup
del boot.img original guardado en
`/home/chijure/y210_kernel_backups/boot_backup_2026-07-09_pre_fpga_fix.img`
antes de flashear**, por si hace falta revertir. Arrancó sano
(`system_server`/`mediaserver` corriendo, kernel con el timestamp de
compilación correcto en `dmesg`). **Resultado: mismo ruido blanco de
siempre, sin cambio.** Se descarta como causa del audio, pero es un
bug real y queda arreglado (no debería tener efectos secundarios
negativos — solo elimina una escritura a un registro que no debería
tocarse en este hardware).

### Hallazgo 2 (implementado, sin cambio en el resultado): `V4L2_CID_PRIVATE_TAVARUA_SET_AUDIO_PATH` nunca se llamaba

En `radio-tavarua.c` existe un control V4L2 privado dedicado,
`V4L2_CID_PRIVATE_TAVARUA_SET_AUDIO_PATH` (`V4L2_CID_PRIVATE_BASE +
0x29` = `0x08000029`), que llama a `tavarua_set_audio_path()` — una
función que configura los bits `AUDIORX_DIGITAL`/`AUDIORX_ANALOG`/
`AUDIOTX` del registro `AUDIOCTRL` **del propio chip FM** (WCN2243),
es decir, habilita (o no) que el chip realmente **emita** audio por su
salida digital/analógica — algo distinto y adicional a rutear el
destino de ese audio (que es lo único que hacen `switch_mode`/RPC).
Confirmado con `grep` que **nunca se llamó desde ningún lado de este
árbol** (ni Java, ni JNI, ni antes de hoy).

El comentario del header lo marca como parte de un bloque "IOCTL's
specific to IRIS" (`V4L2_CID_PRIVATE_BASE + 0x1E` a `+ 0x27`) — IRIS es
un chip FM de Qualcomm más nuevo (WCN36xx), no el WCN2243/"Bahama" que
usa el Y210 (confirmado por `"It is Bahama"` en dmesg toda la sesión) —
pero `SET_AUDIO_PATH` está *después* de ese rango marcado, y su handler
en el driver no distingue por tipo de chip, así que se probó igual.

**Fix implementado:** nuevo método público
`FmReceiver.setAudioPath(boolean digital)` (sigue el mismo patrón que
`setInternalAntenna`), llamado desde `FMRadioService.fmOn()`
inmediatamente después de `enable()`. Confirmado no-bloqueante (el
handler del kernel no espera ninguna interrupción, a diferencia de
`setInternalAntenna`/`setLowPowerMode` que sí cuelgan el hilo — por eso
esos se saltean deliberadamente en este mismo método, ver comentario
existente). Probado en hardware real: corre sin error, sin colgar, sin
volver el loop de eventos. **Resultado: mismo ruido blanco de siempre,
sin cambio.**

### Descartado: arquitectura ADIE_CODEC/ACDB no está compilada en este kernel

Se revisó `snddev_data_marimba.c` (que usa IDs `ACDB_ID_LP_FM_HEADSET_SPKR_STEREO_RX`
etc. — reabriendo brevemente la vieja teoría de calibración ACDB de
2026-07-04) y `marimba-codec.c` (tablas `adie_codec_tx_regs[]`/
`adie_codec_rx_regs[]`). Verificado en el `.config` real del kernel:
`CONFIG_MARIMBA_CORE=y` (el driver de bus I2C de bajo nivel, sí se usa)
pero **`# CONFIG_MARIMBA_CODEC is not set`** — la arquitectura
ADIE_CODEC/ACDB completa (`marimba-codec.c`) **no se compila en este
kernel en absoluto**. Es código fuente presente en el árbol pero muerto
para este build — no es lo que realmente corre en el Y210. Se descarta
esta vía por completo, no solo por evidencia indirecta sino por
confirmación directa de la configuración de compilación.

### Verificación adicional: las escrituras I2C de BTFMPinSwitching sí quedan grabadas

Última duda razonable: ¿el driver de kernel (`marimba-core.c`, que SÍ está
compilado — `CONFIG_MARIMBA_CORE=y`) podría estar revirtiendo/ignorando
nuestras escrituras crudas por `/dev/i2c-1` (que bypasean por completo
cualquier driver de kernel, abriendo el bus directo desde userspace)?

Se agregó lectura de verificación (`i2c1_read_reg()`, mismo patrón que
`marimba_read()` de la referencia real) inmediatamente después de cada
escritura, logueando valor escrito vs. leído. Probado en hardware real:
**los 7 registros (`0x88-0x8b`, `0x8e-0x90`) leen exactamente el valor
que se acaba de escribir, en ambos modos, sin ninguna discrepancia.**
Esto descarta definitivamente que el problema sea "la escritura no se
sostiene" — el chip Marimba honra y retiene el valor exacto que le
mandamos, verificado por lectura directa, no solo por el código de
retorno del `ioctl`.

## Sesión 2026-07-09 (cont. 4): comparación en vivo contra hardware que SÍ funciona — hallazgo nuevo real

El usuario señaló la limitación real del día: todo lo anterior fue
ingeniería inversa "a ciegas" (binarios/fuente de otros dispositivos,
sin poder comparar contra un Y210 real funcionando). Con dos unidades
físicas disponibles (una CM7, una stock con FM funcionando de verdad),
se armó una herramienta propia de diagnóstico (`i2cdump.c`, solo lectura,
compilada con el toolchain `arm-eabi-4.4.3` del host — ver comandos en
memoria del proyecto) para leer registros I2C crudos en `/dev/i2c-1`
directamente, sin depender de ningún blob.

### Confirmación total de `switch_mode`/BTFMPinSwitching

Con FM apagado en el stock: `0x88-0x8b=0x40` (BT activo),
`0x8e-0x90=0x15` (FM tristate) — es decir, **`MODE_BTSCO` por defecto**.
Con FM genuinamente encendido y sonando en el stock:
`0x88-0x8b=0x15`, `0x8e-0x90=0x40` — **`MODE_FM`, exactamente los
valores que ya usa nuestra implementación**, byte por byte. Esto
confirma al 100% (no por análisis, por comparación directa contra
hardware funcionando) que nuestra reimplementación de `switch_mode` es
exacta.

### Hallazgo nuevo: otros 6 registros cambian con FM que nunca tocamos

Barrido completo `0x00-0xff` en la misma dirección (`0x0c`), comparando
apagado vs. encendido (reboot limpio entre mediciones para garantizar
estado real):

| Registro | Apagado | Encendido | Delta |
|---|---|---|---|
| `0x02` | `0x7f` | `0x2a` | — |
| `0x04` | `0x00` | `0x40` | bit 6 |
| `0x07` | `0x00` | `0x22` | bits 1,5 |
| `0x0e` | `0x00` | `0x08` | bit 3 |
| `0x88-0x8b` | `0x40` | `0x15` | (ya conocido: pin-switch BT) |
| `0x8e-0x90` | `0x15` | `0x40` | (ya conocido: pin-switch FM) |
| `0xf0` | `0x00` | `0x06` | +0x06 |
| `0xf4` | `0x80` | `0x86` | +0x06 (mismo delta que 0xf0) |

`0xf0`/`0xf4` con el **mismo delta exacto** (+0x06) es sospechoso de ser
un bit de habilitación real (¿enable de salida de audio del códec,
separado del pin-switch de `0x88-0x90`?). `0x02/0x04/0x07/0x0e` están en
el rango bajo de registros (posiblemente configuración del lado
RF/tuner, no necesariamente audio).

**Pendiente inmediato:** repetir el mismo volcado `0x00-0xff` en el Y210
con CM7 (apagado vs. encendido) para ver si estos 6 registros nuevos
quedan sin tocar (confirmando que son la pieza real que falta) o si ya
cambian por alguna vía indirecta que no hemos identificado.

### Balance del día completo

En total, hoy se probaron y verificaron como **correctamente
implementados** (contra fuente real, no suposiciones): RPC de ruteo
(`snd_set_device`), `switch_mode`/pin-switching del códec (ambos modos),
`config_dac`, el registro `AUDIOCTRL` de habilitación de audio del chip
FM, y un bug real de kernel (FPGA) quedó corregido. **Ninguno de estos
cambios, ni individualmente ni combinados, alteró el resultado
audible** — siempre el mismo "ruido blanco/interferencia" descrito por
el usuario, nunca la emisora real, nunca silencio distinto al ya
documentado. Esto es evidencia consistente de que la causa real está
en algo que **no se ha tocado hoy**: posiblemente algo específico del
firmware cerrado del ARM9 (baseband) que stock invoca de una forma que
no deja rastro en ningún mecanismo público que hayamos encontrado, o
— dado que stock SÍ suena en esta misma unidad física — algo en la
extensísima lógica C++ de `AudioPolicyManager`/`AudioHardware` reales
de Huawei que aún no se ha logrado ubicar (el call site de `switch_mode`
sigue sin encontrarse, pese a búsqueda exhaustiva).

## Sesión 2026-07-09 (cont. 5): comparación correcta CM7 FM-on vs stock FM-on — hallazgo real

La comparación anterior (FM-off "limpio" en CM7 vs FM-on en stock) estaba contaminada: en CM7 con FM apagado, **el chip Marimba en 0x0c no responde en el bus I2C en absoluto** (`qup_i2c qup_i2c.1: I2C slave addr:0xc not connected` en dmesg, confirmado con los 256 registros devolviendo ERROR). Esto es normal — el chip está sin reloj/alimentación cuando FM no está activo — pero invalidaba comparar ese estado contra el stock encendido.

Metodología corregida: reboot limpio del Y210 CM7, confirmar 0 procesos FM corriendo, volcado 0x00-0xff (0 errores, chip ya despierto porque se lanzó `com.android.fm/.radio.FMRadio` justo antes), comparado contra `stock_fm_on_clean.txt` ya capturado.

**Los 6 registros "sospechosos" de la sesión anterior (0x02, 0x04, 0x07, 0x0e, 0xf0, 0xf4) YA COINCIDEN exactamente entre CM7 y stock con FM encendido** (0x02=0x2a, 0x04=0x40, 0x07=0x22, 0xf0=0x06, 0xf4=0x86 en ambos; 0x0e difiere ligeramente: CM7=0x0c vs stock=0x08, CM7 tiene un bit extra puesto, no le falta nada). Esa hipótesis queda descartada — no son la pieza faltante.

**Diff real (cm7_fm_on_clean.txt vs stock_fm_on_clean.txt), aparte del ya conocido/implementado par de pin-switch (0x88-0x90):**
```
reg 0x0e: cm7=0x0c  stock=0x08   (bit extra en CM7, no crítico)
reg 0x11: cm7=0x00  stock=0x0c   (CM7 le faltan bits 2,3)
reg 0x13: cm7=0x00  stock=0x01   (CM7 le falta bit 0)
reg 0x81: cm7=0x40  stock=0x00   (CM7 tiene bit6 puesto que stock NO tiene)
reg 0x82: cm7=0x40  stock=0x00   (idem)
reg 0xe6: cm7=0x00  stock=0x38   (CM7 le faltan bits 3,4,5)
reg 0xe7: cm7=0x00  stock=0x06   (CM7 le faltan bits 1,2)
reg 0xe9: cm7=0x00  stock=0x21   (CM7 le faltan bits 0,5)
```

**Identificación de estos registros:** 0x11, 0x13, 0x81, 0x82, 0xe6-0xe9 aparecen en `arch/arm/mach-msm/include/mach/qdsp5v2/marimba_profile.h` (kernel real) como parte de las tablas `ADIE_CODEC_ACTION_ENTRY` de los perfiles de audio con `capability = SNDDEV_CAP_FM` en `snddev_data_marimba.c` (ej. `FM_HEADSET_STEREO_CLASS_D_LEGACY_OSR_64`, `FM_HANDSET_OSR_64`, `FM_SPEAKER_OSR_64`) — es decir, son literalmente el path analógico de audio del codec para reproducir FM.

**Importante:** estos perfiles NO los aplica un driver Linux directo (`adie_marimba.c` no existe en este árbol) — se activan vía RPC (`do_route_audio_rpc` → `snd.c`/`snd_set_device` → firmware ARM9/DSP), confirmado con dmesg: `[snd.c:snd_ioctl] snd_set_device 26 0 1` al encender FM (device 26 = SND_DEVICE_FM_HEADSET, poblado en runtime idénticamente en ambas ROMs porque viene de la misma tabla del kernel). El ARM9 es quien decide qué registros tocar según el "device" que se le pasa por RPC — normalmente invisible/no controlable desde Linux.

**PERO**: ya tenemos probado (con `switch_mode`/pin-switch, sesión anterior) que el registro I2C a 0x0c es perfectamente escribible/legible desde el lado AP vía `/dev/i2c-1` sin pasar por el ARM9. Esto abre una vía directa: escribir nosotros mismos, desde `AudioHardware.cpp`, los valores finales conocidos-correctos de estos 5 registros realmente faltantes (0x11=0x0c, 0x13=0x01, 0xe6=0x38, 0xe7=0x06, 0xe9=0x21) inmediatamente después del pin-switch existente, sin necesidad de replicar la secuencia completa de delays del perfil ADIE (que es solo para evitar "pops" audibles al encender, no debería ser funcionalmente necesaria para que el audio simplemente empiece a sonar).

Nota aparte, no concluyente: dmesg muestra oscilación device 26→3→26 justo al encender FM (`snd_set_device 26 0 1`, `26 0 0`, luego `3 1 1`, luego `26 0 1` de nuevo) — podría ser normal (mute/unmute de transición) o indicar interferencia de otro stream de audio (ej. sonido de click de UI). No se investigó más a fondo por ahora.

**Pendiente inmediato:** implementar la escritura directa de estos 5 registros (0x11, 0x13, 0xe6, 0xe7, 0xe9) vía I2C en `AudioHardware.cpp` (mismo mecanismo ya probado de `i2c1_write_reg`), justo después de `run_btfm_pin_switch(MODE_FM)`, compilar, flashear, probar audio real.

## Sesión 2026-07-09 (cont. 6): registros del codec 100% igualados al stock — SIGUE sin audio limpio (hallazgo negativo importante)

Se implementó `run_fm_audio_path_enable()` en `AudioHardware.cpp`: escritura directa vía I2C (mismo mecanismo probado de `run_btfm_pin_switch`) de los 7 registros que diferían entre CM7 y stock con FM encendido: `0x11=0x0c, 0x13=0x01, 0x81=0x00, 0x82=0x00, 0xe6=0x38, 0xe7=0x06, 0xe9=0x21`. Llamada justo después de `run_btfm_pin_switch(MODE_FM)` en `doAudioRouteOrMute()`.

Compilado, flasheado, probado en dos iteraciones (primero sin 0x81/0x82, luego con ellos). Read-back confirma en ambos casos que los valores se escriben y **persisten** correctamente (no hay nada más pisándolos). Con la segunda iteración, **el volcado completo 0x00-0xff del chip Marimba en CM7 con FM encendido es ahora IDÉNTICO, registro por registro, al volcado del stock con FM encendido** (diff vacío salvo por diferencias esperadas de estado transitorio).

**Resultado: el audio sigue exactamente igual (estática/ruido blanco), sin ningún cambio audible.**

**Conclusión importante:** esto descarta DEFINITIVAMENTE la configuración del codec analógico Marimba (registros I2C en 0x0c) como causa del problema. Ya no queda ningún registro conocido de este chip que difiera entre un dispositivo que funciona y uno que no. La causa debe estar en otro punto de la cadena, probablemente en el dominio **digital** en vez del analógico:

- Posible mismatch de sample rate / formato I2S entre el chip FM (tavarua/Bahama, vía `config_i2s mode = FM_I2S_ON`) y el codec — esto produciría exactamente el síntoma de "estática/ruido blanco" (muestras leídas con clock/alineación incorrecta), y NO se arreglaría con ningún ajuste de ganancia/bias analógico, consistente con el resultado negativo de hoy.
- Revisar `config_i2s()`/`config_pcm()` en `board-msm7x27a.c` (kernel real) y comparar contra la configuración de reloj/formato que espera el codec (sample rate, bits, modo master/slave -- master/slave ya se descartó, pero NO se ha verificado el sample rate/formato exacto en detalle).
- Revisar si hay algún registro de configuración de sample rate/formato I2S en el propio chip FM (tavarua) que no se esté seteando (aparte de audio_path ya implementado).

**Pendiente para la próxima sesión:** investigar configuración I2S (sample rate, ancho de palabra, formato) entre el chip FM y el codec Marimba, comparando ambos lados (kernel FM driver `radio-tavarua.c` y `config_i2s()`/board file) contra lo que hace el stock. Esta es ahora la hipótesis más fuerte dado el patrón de síntomas y la eliminación completa de la vía analógica.

## Sesión 2026-07-09 (cont. 7): más descartes, foco en el desvío "device 3"

- **Device ID de FM_HEADSET confirmado idéntico entre stock y CM7**: ambos usan `snd_set_device 26 0 0` (26 = `FM_DIGITAL_STEREO_HEADSET` según la tabla `snd_endpoints_list[]` de `board-msm7x27a.c`). Descarta mismatch de IDs de endpoint.
- **Modo analógico del chip FM probado** (`setAudioPath(false)`): silencio total (no estática, nada). Confirma que el diseño físico del Y210 es 100% I2S digital -- no hay salida analógica del chip FM cableada a ningún lado. Revertido a digital (`true`) tras la prueba.
- **`tavarua_radio: UNKNOWN XFR = 98` descartado**: aparece en AMBOS dispositivos (stock y CM7) repetidamente mientras FM está encendido -- es benigno/normal, no es la causa.
- **Hallazgo pendiente de explicar**: en CM7, la secuencia de `snd_set_device` al encender FM es `26 0 1` → `26 0 0` → (~330ms después) `3 1 1` (=HEADSET normal, no FM) → `26 0 1` → `26 0 0` de nuevo. En stock es una sola llamada limpia `26 0 0`. Se descartó que sea un sonido de UI (`sound_effects_enabled=0` no lo elimina). El origen de esta llamada a device 3 sigue sin identificarse -- viene de `ApmCommandThrea[d]` (AudioPolicyManager), no directamente de nuestro código FM. Pendiente: activar logs verbosos de audio policy/HAL y capturar logcat completo durante el bring-up para identificar qué componente pide ese cambio de ruta.

## Sesión 2026-07-09 (cont. 8): identificado el "bump" de switchToSpeaker/switchToHeadset -- necesario, no dañino (resultado negativo pero clave)

Con logs verbosos de `AudioService`/`AudioPolicyManagerBase` habilitados, se rastreó el origen exacto del desvío a `snd_set_device 3` (HEADSET normal) que aparecía justo al tunear FM en CM7 (y que el stock NO tiene): es `FMRadio.java` (la Activity), en `enableRadio()`'s `AsyncTask.onPostExecute()`, líneas ~942-948:

```java
if (!radioAlreadyOn) {
    tuneRadio(FmSharedPreferences.getTunedFrequency());
    if (FmSharedPreferences.getSpeaker()) {
        switchToSpeaker();
    } else {
        switchToHeadset();   // <-- se llama automáticamente SIEMPRE al encender FM
    }
}
```

`switchToHeadset()`/`switchToSpeaker()` (líneas ~438-448) hacen:
```java
AudioSystem.setForceUse(AudioSystem.FOR_MEDIA, AudioSystem.FORCE_NONE /* o FORCE_SPEAKER */);
AudioSystem.setDeviceConnectionState(AudioSystem.DEVICE_OUT_FM, AudioSystem.DEVICE_STATE_UNAVAILABLE, "");
AudioSystem.setDeviceConnectionState(AudioSystem.DEVICE_OUT_FM, AudioSystem.DEVICE_STATE_AVAILABLE, "");
```

Este patrón (probablemente heredado del FM app genérico AOSP/QCOM de referencia, no específico de Huawei) se ejecuta automáticamente cada vez que se enciende FM, justo después de `tuneRadio()` -- coincide exactamente con el `fm_off=7`/`fm_on=2055` observado en dmesg. **`BluetoothA2dpService` que aparecía en el mismo instante en logcat era una coincidencia irrelevante** (solo reacciona al broadcast `com.android.music.metachanged` que enviamos nosotros mismos vía `lockscreenBroadcast()`, sin tocar el audio routing).

**Se probaron 2 variantes de fix, ambas con resultado negativo, pero informativo:**
1. Eliminar por completo la llamada a `switchToSpeaker()/switchToHeadset()` en el bring-up automático → **silencio total** (peor que antes).
2. Mantener solo el `setForceUse()` pero sin el bounce `UNAVAILABLE`→`AVAILABLE` → **silencio total también**.

**Conclusión: el bounce `UNAVAILABLE`→`AVAILABLE` de `DEVICE_OUT_FM` es NECESARIO en este hardware** -- sin él, el circuito de audio queda completamente mudo (ni siquiera estática). Con él (comportamiento original, ya revertido y reflasheado), se mantiene el estado ya conocido: estática/ruido blanco, pero al menos hay actividad real en el circuito de audio. La hipótesis de que este bounce "corrompía" el bring-up del ARM9 queda **descartada** -- es lo opuesto, es una pieza necesaria del propio bring-up (probablemente lo que realmente abre/activa el output stream de AudioFlinger hacia el hardware).

**Estado del código al cierre de esta sesión:** `FMRadio.java` revertido exactamente a su estado original (md5 idéntico al de antes de esta sub-sesión). `AudioHardware.cpp` conserva los cambios de la sesión (`run_fm_audio_path_enable()` con los 7 registros del codec igualados al stock, que no revirtieron el problema pero tampoco lo empeoraron y están bien fundamentados con evidencia real). `FMRadioService.java` con `setAudioPath(true /* digital */)` (revertido tras la prueba de modo analógico que dio silencio total).

**Pendiente para la próxima sesión:** dado que el "bump" es necesario y no dañino, el foco debe volver a: ¿qué pasa DESPUÉS de que este bounce fuerza la reapertura del output stream? Posibles próximos pasos:
- Capturar logcat verboso completo (`AudioFlinger`, `AudioPolicyManagerBase`) en el STOCK durante su encendido de FM (no se ha hecho -- solo se comparó dmesg del kernel, no logcat de Android, ya que el stock es closed-source y no tiene los mismos logs, pero SÍ tiene logcat del framework AOSP compartido, que podría revelar diferencias en cómo el output stream HAL se abre/configura).
- Revisar si `AudioHardware::openOutputStream`/`AudioStreamOutMSM72xx` en nuestro `AudioHardware.cpp` hace algo diferente o incompleto comparado con lo que se espera (dado que el audio de FM en este chip no pasa por samples PCM sino por un passthrough de hardware, tal vez el "output stream" que se abre/cierra con el bounce es el que realmente habilita algún reloj/enable a nivel de audio HAL que no hemos identificado).
- Considerar instrumentar más a fondo qué CAMBIA exactamente a nivel de hardware (registros Marimba, o algo más) entre el momento ANTES y DESPUÉS del bounce, ahora que sabemos que el bounce es la pieza que activa el circuito -- comparar volcados I2C 0x0c inmediatamente antes vs después del bounce (no solo al final del bring-up completo como se hizo hoy).

## Sesión 2026-07-09 (cont. 9): descartes rápidos finales

- **Probado y descartado -- carácter del ruido a distintos volúmenes**: bajar/subir el volumen del teléfono NO cambia el carácter del ruido, solo su intensidad. Descarta que sea una señal real saturada/con ganancia mal calibrada (clipping) -- es ruido de banda ancha consistente, más compatible con un problema de formato/reloj digital que con un problema de ganancia analógica.
- **Probado y descartado -- retraso de 2s antes del bounce**: se agregó un `postDelayed(2000ms)` alrededor de `switchToSpeaker()/switchToHeadset()` en `FMRadio.java`, por si el chip FM necesitaba más tiempo para estabilizar su reloj I2S antes del compromiso final de ruta del ARM9. Sin cambio -- descarta timing del bounce como variable relevante. Revertido (código de vuelta a md5 original `fd3323b566ee578c47e9065645e9a694`).

**Estado del dispositivo CM7 al cierre de esta sesión:** todo el código revertido a como estaba antes de esta sub-sesión de "bounce" (FMRadio.java, FMRadioService.java con setAudioPath(true)), EXCEPTO `AudioHardware.cpp` que conserva permanentemente `run_fm_audio_path_enable()` (los 7 registros del codec igualados al stock -- no dañino, bien fundamentado, se mantiene). Estado de audio: estática/ruido blanco constante, sin importar el volumen -- igual que al inicio de la sesión de hoy.

**Resumen de la jornada completa (2026-07-09) para referencia rápida:**
Se avanzó muchísimo en DESCARTAR causas (lo cual reduce el espacio de búsqueda drásticamente), pero la causa raíz real del ruido sigue sin identificarse. Confirmado con evidencia sólida:
- ✅ RF/tuning perfecto (RSSI real, eventos reales)
- ✅ Pin-switch BT/FM (I2C 0x88-0x90) 100% correcto
- ✅ Audio path digital del chip (AUDIOCTRL) habilitado y confirmado necesario (analógico = silencio total, confirma diseño 100% I2S)
- ✅ TODOS los registros del codec Marimba (0x00-0xff en I2C 0x0c) ahora coinciden exactamente con el stock durante FM-on
- ✅ I2S master/slave correcto
- ✅ ID de dispositivo FM (26) coincide entre stock y CM7
- ✅ El "bounce" UNAVAILABLE→AVAILABLE es necesario (no dañino) -- confirmado
- ✅ Ganancia/volumen descartado como causa (mismo carácter de ruido a cualquier volumen)
- ✅ "UNKNOWN XFR=98" descartado (presente en ambos dispositivos, benigno)

**Lo único que queda sin poder inspeccionar/comparar directamente** es la configuración interna del DSP/ARM9 (sample rate, formato I2S, mezclador de audio) que se aplica vía RPC (`snd_set_device`) de forma opaca -- closed-source, sin logs, sin registros I2C visibles. Este es el techo real de lo que se puede investigar sin herramientas adicionales (ej. un analizador lógico en las líneas I2S físicas, o acceso a símbolos de depuración del firmware AMSS).

## Sesión 2026-07-09 (cont. 10): confirmado el hallazgo más importante -- el ruido NO varía con la frecuencia

Se repitió, con el estado actual del código (todos los fixes de hoy aplicados: registros codec 100% igualados, bounce restaurado, digital path habilitado), la prueba que ya se había hecho el 2026-07-06: cambiar entre varias frecuencias (estación real conocida vs dial vacío) mientras suena la estática. **Resultado: el carácter del ruido es idéntico sin importar la frecuencia sintonizada.** Esto reconfirma, con todo el trabajo adicional de hoy, la misma conclusión de la sesión del 06: lo que se escucha NO es la señal demodulada real del tuner llegando con algún defecto de formato/calibración -- es un ruido de piso genuinamente desconectado de la sintonía.

**Implicación importante:** RSSI real + `TUNE_EVENT` real confirman que el chip está tuneando correctamente en el dominio de RF, pero esto NO garantiza que la etapa de demodulación de audio interna del chip (separada del sintetizador de RF) esté realmente activa o conectada a la salida I2S. Son subsistemas distintos dentro del mismo silicio WCN2243/tavarua.

**Revisado y descartado como vía adicional:** los registros internos de demodulación del chip (`DIG_DEMOD`, `DIG_AGC_0-2`, `DIG_AUDIO_0-4`, `DIG_PILOT`, `DIG_MPXDCC`, accesibles vía el mecanismo XFR de `radio-tavarua.c`) **nunca son tocados por el driver Linux** (grep vacío en todo `radio-tavarua.c`). El único control V4L2 expuesto que toca uno de estos rangos (`V4L2_CID_PRIVATE_TAVARUA_MPX_DCC`) es de solo lectura (`peek_MPX_DCC`, diagnóstico de calibración, sin path de escritura). Si estos registros necesitan configurarse para que el DSP de demodulación de audio del chip realmente процes/emita la señal, la única vía conocida sería a través de `fm_qsoc_patches` (que sí es idéntico byte a byte al del stock, confirmado hoy) -- pero como es el MISMO binario con los MISMOS argumentos (`hw.fm.version=67240453`, idéntico en ambos dispositivos), en teoría debería estar aplicando la misma configuración en ambos casos.

**Techo real de la investigación por software/registros alcanzado.** Todo lo inspeccionable desde Linux (registros I2C del codec, GPIOs de pines I2S, ID de dispositivo de audio, binario de calibración, argumentos) ya coincide con el stock. Lo que queda sin poder observarse:
1. El contenido exacto de la configuración de demodulación de audio que aplica `fm_qsoc_patches` internamente (firmware embebido, closed-source, ya confirmado idéntico binario -- pero podría depender de un ORDEN/TIMING de ejecución relativo a otras llamadas (`config_dac`, `switch_mode`, el "bounce") que si difiere del stock, produciría este mismo síntoma sin que el binario en sí sea distinto).
2. Cualquier configuración aplicada por el firmware ARM9/AMSS vía RPC que no deje rastro visible (ya explorado a fondo en sesiones previas).

**Próximos pasos posibles (todos de mayor esfuerzo/incertidumbre que lo hecho hoy):**
- Comparar con dmesg/logcat MUY detallado el ORDEN EXACTO y TIMING de: `fm_qsoc_patches (modo 0)` en fminit → apertura de `/dev/radio0` → `config_dac`/`switch_mode` → tuneo → bounce, contra la secuencia real observada en el stock (ya se tiene el dmesg del stock parcialmente, pero no un trace tan detallado como el de CM7 hoy).
- Desensamblar más a fondo el propio `fm_qsoc_patches`/su firmware embebido para entender qué exactamente configura en modo "0" (calibración RF) vs si existe algún otro modo/argumento que configure explícitamente el DSP de audio (`DIG_AUDIO`/`DIG_DEMOD`) y que el script stock invoque en un punto que nosotros no replicamos.
- Obtener/usar un analizador lógico en las líneas físicas I2S (FM_I2S_SD/WS/SCK, GPIO 68/70/71) para confirmar si hay datos reales viajando por el bus en absoluto, y si cambian con la sintonía -- esto sacaría la investigación del dominio puramente software.

## Sesión 2026-07-09 (cont. 11): fm_qsoc_patches -- log de ejecución completo, byte-a-byte idéntico

Se descubrió que `/data/app/fm_dld_enable` (uno de los archivos de flag que el binario `fm_qsoc_patches` revisa según sus propios strings: `fm_dacconfig_mode`, `fm_wa_mode`, `fm_cal`, `fm_dld_enable`) en realidad **es el log completo de su propia ejecución** (no un flag vacío) -- contiene el detalle de cada "patch"/"default"/"offset threshold" descargado al chip vía I2C durante el modo 0 (calibración RF/firmware), incluyendo los valores hexadecimales exactos poke por poke.

Comparado byte a byte entre stock y CM7 (`diff`, `md5sum`): **archivos 100% idénticos** (8355 bytes, mismo md5 `955319f646c4adafc4a9b8f4df878bee`, 231 líneas, 46 "download success", 70 "Patch [", 18 "Defaults ["). Confirma que la descarga completa de firmware/calibración del chip WCN2243 (35 parches + 9 valores por defecto + 2 umbrales de offset) se ejecuta EXACTAMENTE igual en ambos dispositivos, sin ninguna diferencia.

**Con este resultado, la investigación de hoy llega a un cierre real: absolutamente todo lo inspeccionable desde el lado Linux/AP (registros del codec Marimba, ID de dispositivo de audio, GPIOs I2S, binario y log completo de calibración del chip FM, I2S master/slave) es ahora IDÉNTICO entre el stock (que funciona) y CM7 (que no funciona), y sin embargo el resultado de audio sigue siendo diferente** (estática en CM7 vs audio limpio en stock).

Esto deja solo tres explicaciones posibles restantes, ninguna investigable con las herramientas actuales:
1. **Timing/orden de ejecución** no capturado por logs estáticos -- ej. el momento exacto (relativo a otras operaciones como el "bounce" o `config_dac`) en que el stock invoca estos mismos mecanismos podría diferir sutilmente del nuestro, sin que ningún archivo/registro individual lo revele.
2. **Configuración del DSP/ARM9 (AMSS) vía RPC** que no deja registro alguno visible desde Linux (ya explorado a fondo, sin más vías conocidas de inspección).
3. Diferencia de **hardware real** entre las dos unidades físicas (poco probable pero no descartable sin instrumentación).

**Para seguir desde aquí se necesitaría instrumentación que no tenemos hoy:** un analizador lógico en las líneas físicas I2S (GPIO 68/70/71) para ver si hay datos reales viajando y si cambian con la sintonía, o símbolos de depuración del firmware AMSS del ARM9. Sin eso, este es el techo real de la investigación por software puro.
