# Wi-Fi Notes

## Estado actual

El bug ya no esta en el bring-up basico del radio. El estado actual es:

- `wlan.driver.status=ok`
- `wifi.interface=eth0`
- `init.svc.ath_supplicant=running`
- `ps` muestra `/system/bin/ath_supplicant`
- `dumpsys wifi` llega a `runState=Running`
- `Supplicant state=INACTIVE`

Eso mueve el problema a la entrega de resultados de scan al framework.

## Hallazgo clave del stock

Del telefono stock se confirmo:

- usa `eth0`
- usa `ath_supplicant`, no `wpa_supplicant`
- `wpa_supplicant.conf` stock contiene solo:

```text
update_config=1
ctrl_interface=DIR=/data/misc/wpa_supplicant GROUP=wifi
```

- el stock reporta:
  - `init.svc.ath_supplicant=running`
  - `dumpsys wifi` en `runState=Running`
  - `Supplicant state=COMPLETED`
  - `Latest scan results` poblado

El archivo extraido del stock se guardo en:

- `/home/chijure/cm7/stock-capture/wpa_supplicant.conf`

## Cambios persistidos en el arbol

### Propiedades

En `device/huawei/y210/BoardConfig.mk`:

```make
BOARD_HAS_EXTRA_SYS_PROPS := true
```

Motivo:

- el sistema estaba clavado en `247` props
- `wlan.driver.status` no podia crearse

### Init

En `device/huawei/y210/prebuilt/init.huawei.rc`:

- se crean `/data/misc/wpa_supplicant` y `/data/misc/wifi/sockets`
- se ajustaron al patron stock `wifi:wifi` y `0775`
- se dejo `wifi.interface=eth0`
- ya no se vuelve a crear el symlink legacy a `/data/system/wpa_supplicant`

### Config del supplicant

En `device/huawei/y210/prebuilt/system/etc/wifi/wpa_supplicant.conf`:

- se reemplazo la configuracion basada en `/data/misc/wifi/sockets`
- ahora sigue el stock:

```text
ctrl_interface=DIR=/data/misc/wpa_supplicant GROUP=wifi
update_config=1
```

### libhardware_legacy

En `hardware/libhardware_legacy/wifi/wifi.c`:

- `wifi_load_driver()` para `ar6000` usa el flujo Huawei con `wlan.driver.status=insmod`
- `wifi_start_supplicant()` usa `ath_supplicant` para `ar6000`
- `wifi_start_supplicant()` espera a que exista `/sys/class/net/eth0` antes de arrancar el supplicant
- `wifi_connect_to_supplicant()` ya conoce `/data/misc/wpa_supplicant`
- para `ath_supplicant`, `wifi_connect_to_supplicant()` prioriza `/data/misc/wpa_supplicant/eth0`
- se limpian sockets remotos stale antes de arrancar el supplicant
- se añadieron trazas `WifiHW` para ver:
  - carga del driver
  - espera de `eth0`
  - `ctl.start` del supplicant
  - path usado para abrir el socket de control

## Estado validado tras el ultimo boot.img

Con el `boot.img` nuevo y la limpieza del symlink/socket stale:

- `wlan.driver.status=ok`
- `init.svc.ath_supplicant=running`
- `ps` muestra `/system/bin/ath_supplicant`
- `dumpsys wifi` ahora queda en:

```text
interface eth0 runState=Running
Supplicant state: INACTIVE
Latest scan results:
```

- `wpa_cli -p/data/misc/wpa_supplicant -ieth0 scan_results` devuelve APs reales:
  - `DEPA2`
  - `MOVISTAR_BFE6`

Eso ya descarta:

- fallo basico de `insmod`
- fallo basico de arranque del proceso `ath_supplicant`
- fallo de conexion inicial al socket de control

## Hallazgo nuevo

El telefono conserva sockets `eth0` en dos rutas:

- `/data/misc/wpa_supplicant/eth0`
- `/data/misc/wifi/sockets/eth0`

Y todavia existe un symlink viejo en el dispositivo:

- `/data/system/wpa_supplicant -> /data/misc/wifi/sockets`

Eso no viene ya del arbol actual, sino de pruebas anteriores y estado persistido en `/data`.

Eso explicaba por que antes el framework quedaba en `UNINITIALIZED`.

Despues de priorizar `/data/misc/wpa_supplicant` y borrar el symlink viejo
`/data/system/wpa_supplicant`, el framework ya pasa a `Running/INACTIVE`.

La hipotesis mas fuerte ahora es:

1. `ath_supplicant` ya corre
2. el framework ya se conecta al socket de control
3. pero no esta recibiendo o procesando bien `SCAN_RESULTS`
4. por eso `Latest scan results` sigue vacio aunque `wpa_cli` si vea APs

## Notas de depuracion utiles

- arrancar `ath_supplicant` demasiado pronto despues de `insmod` puede fallar
- despues del ajuste para esperar `eth0`, el proceso ya puede quedar vivo
- los logs de `WifiHW` ahora deberian mostrar exactamente:
  - si el driver llega a `ok`
  - si `eth0` estuvo listo
  - qué servicio intentamos arrancar
  - qué path de socket intenta abrir `wifi_connect_to_supplicant()`

## Siguiente paso

Prioridad inmediata:

1. revisar por que `WifiMonitor` o `WifiStateTracker` no materializan `SCAN_RESULTS`
2. comprobar si `ath_supplicant` emite `CTRL-EVENT-SCAN-RESULTS` por el monitor socket
3. si hace falta, instrumentar la capa Java (`WifiMonitor` / `WifiStateTracker`) para ver si el evento se pierde o si el parseo falla

## Hallazgo posterior

Las pruebas siguientes confirmaron algo mas preciso:

- `wpa_cli -p/data/misc/wpa_supplicant -ieth0 scan_results` sigue devolviendo APs reales
- `dumpsys wifi` sigue en `runState=Running` con `Supplicant state=INACTIVE`
- `Latest scan results` sigue vacio

Eso deja el bug restante entre el monitor/eventos y el framework, no en el radio.

Tambien aparecio una diferencia importante contra stock:

- la `libhardware_legacy.so` stock contiene referencias a `wifi.wpa_supp_ready`
- la `libhardware_legacy.so` actual del port no contiene esa ruta

No hay referencias a `wifi.wpa_supp_ready` en el framework Java del arbol, asi que
esa logica extra vive dentro de la libreria nativa stock.

Siguiente experimento recomendado:

1. probar en vivo la `libhardware_legacy.so` stock con backup de la actual
2. validar si con esa libreria el framework ya publica scan results
3. si mejora, portar solo la diferencia necesaria a [`wifi.c`](/home/chijure/cm7/hardware/libhardware_legacy/wifi/wifi.c)

## Resultado de la prueba con libreria stock

La prueba directa con la `libhardware_legacy.so` stock fue negativa:

- se hizo backup de la libreria CM7 en `/system/lib/libhardware_legacy.cm7.bak`
- se empujo la libreria stock y se reinicio
- el telefono subio parcialmente, pero `service wifi` desaparecio
- conclusion: la libreria stock no es ABI-compatible con este framework CM7

Se restauro la libreria CM7 original en el telefono.

## Fix aplicado en framework

Como `startScan` y `getScanResults()` ya funcionaban pero el cache/broadcast no se
actualizaba solos, se agrego un fallback en [`frameworks/base/services/java/com/android/server/WifiService.java`](/home/chijure/cm7/frameworks/base/services/java/com/android/server/WifiService.java):

- despues de un `scan()` exitoso, se agenda una lectura diferida de `SCAN_RESULTS`
- si la lista no viene vacia, se emite `SCAN_RESULTS_AVAILABLE_ACTION`

Con eso, la validacion actual ya muestra resultados tambien en `dumpsys wifi`:

```text
Latest scan results:
  DEPA2
  MOVISTAR_BFE6
  PENA PAULINO
```

Eso confirma:

- el parser de `SCAN_RESULTS` funciona
- el cache `mScanResults` del framework ya se llena
- el problema practico estaba en la notificacion, no en el escaneo del radio
## Redes Wi-Fi no persistían tras reinicio

Síntoma:
- La red se guarda durante runtime y aparece en `/data/misc/wifi/wpa_supplicant.conf`.
- Tras reiniciar, el archivo vuelve al template mínimo y se pierden las redes.

Causa:
- En [init.huawei.rc](/home/chijure/cm7/device/huawei/y210/prebuilt/init.huawei.rc) había un `copy /system/etc/wifi/wpa_supplicant.conf /data/misc/wifi/wpa_supplicant.conf` dentro de `on boot`.
- Ese `copy` pisaba en cada arranque el archivo persistente generado por `wpa_supplicant`.

Fix:
- Reemplazar el `copy` por una inicialización condicional:
  - crear `/data/misc/wifi/wpa_supplicant.conf` desde `/system/etc/wifi/wpa_supplicant.conf` solo si el archivo no existe.
- Mantener `chown wifi wifi` y `chmod 0660` en cada arranque.

Validación:
1. Guardar una red WPA desde Settings.
2. Confirmar que aparece un bloque `network={...}` en `/data/misc/wifi/wpa_supplicant.conf`.
3. Reiniciar el teléfono.
4. Verificar que el bloque `network={...}` sigue presente después del boot.

## Limitación observada al flashear fixes

Durante la validación de este bug apareció una limitación del device:

- `adb reboot bootloader` no entra a fastboot de forma confiable;
- en varias pruebas el comando solo reinició Android normal;
- el flujo de trabajo más estable fue entrar manualmente a fastboot antes de usar `fastboot flash`.

Implicación práctica:
- no asumir que `adb reboot bootloader` deja el equipo listo para `fastboot flash`;
- siempre comprobar con `fastboot devices` antes de flashear.
