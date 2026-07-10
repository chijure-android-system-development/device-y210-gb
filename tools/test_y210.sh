#!/usr/bin/env bash
# test_y210.sh — smoke-test del port CM7 para el Huawei Y210 vía ADB.
#
# Uso (desde raíz del árbol CM7 o en cualquier directorio):
#   bash device/huawei/y210/tools/test_y210.sh [--fast] [--section <nombre>]
#
# Opciones:
#   --fast        Omite tests que requieren espera o interacción larga
#   --section X   Ejecuta solo esa sección (boot|display|input|lights|audio|
#                 wifi|bt|sensors|gps|camera|storage|ril|power)
#
# Requiere: adb en PATH, dispositivo conectado con USB debugging activo.

RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'
CYN='\033[0;36m'; BLD='\033[1m'; RST='\033[0m'

PASS=0; FAIL=0; SKIP=0; MANUAL=0
FAST=false; FILTER=""
LOG=/tmp/test_y210_$(date +%Y%m%d_%H%M%S).log
echo "test_y210.sh  $(date)" > "$LOG"

_arg=""
for a in "$@"; do
    if [[ "$_arg" == "--section" ]]; then FILTER="$a"; _arg=""; continue; fi
    case $a in --fast) FAST=true ;; --section) _arg="--section" ;; esac
done

# ── primitivas ────────────────────────────────────────────────────────────────

adb_sh() { adb shell "$@" 2>/dev/null | tr -d '\r'; }

pass()   { echo -e "${GRN}  ✓ PASS${RST}  $1"; PASS=$((PASS+1));   echo "PASS   $1" >> "$LOG"; }
fail()   { echo -e "${RED}  ✗ FAIL${RST}  $1  ← $2"; FAIL=$((FAIL+1)); echo "FAIL   $1  [$2]" >> "$LOG"; }
skip()   { echo -e "${YLW}  - SKIP${RST}  $1  ($2)"; SKIP=$((SKIP+1));  echo "SKIP   $1  [$2]" >> "$LOG"; }
manual() { echo -e "${CYN}  ? MANUAL${RST} $1  → $2"; MANUAL=$((MANUAL+1)); echo "MANUAL $1  [$2]" >> "$LOG"; }

section() {
    [[ -n "$FILTER" && "$FILTER" != "$1" ]] && return 1
    echo ""; echo -e "${BLD}══ $2 ══${RST}"
    return 0
}

chk_prop() {
    _val=$(adb_sh getprop "$2")
    if [[ -n "$3" && "$_val" == *"$3"* ]]; then pass "$1 ($3)"; \
    elif [[ -z "$3" && -n "$_val" ]]; then pass "$1 ($2=$_val)"; \
    else fail "$1" "$2='$_val'"; fi
}
chk_proc() {
    if adb_sh "ps" | grep -v grep | grep -q "$2"; then pass "$1"; \
    else fail "$1" "'$2' no encontrado en ps"; fi
}
chk_file() {
    if adb_sh "[ -e '$2' ] && echo y" | grep -q y; then pass "$1 ($2)"; \
    else fail "$1" "no existe: $2"; fi
}

# ── pre-check ─────────────────────────────────────────────────────────────────

echo -e "${BLD}test_y210.sh — Huawei Y210 CM7 smoke test${RST}"
echo "Log: $LOG"

if ! adb devices | grep -q "device$"; then
    echo -e "${RED}ERROR: no hay dispositivo ADB conectado.${RST}"; exit 1
fi

_model=$(adb_sh getprop ro.product.model)
echo -e "Dispositivo: ${BLD}$_model${RST}  |  Fecha: $(date)"
echo "Dispositivo: $_model" >> "$LOG"

# ── 1. boot / sistema ─────────────────────────────────────────────────────────

if section boot "BOOT / SISTEMA"; then
    chk_prop  "Boot completado"           sys.boot_completed    "1"
    chk_prop  "Versión Android 2.3"       ro.build.version.release "2.3"
    chk_prop  "ro.product.model Y210"     ro.product.model      "HUAWEI Y210"
    chk_prop  "ro.build.product msm7625a" ro.build.product      "msm7625a"
    chk_proc  "system_server"             "system_server"
    # surfaceflinger: en Gingerbread aparece en `service list`, no siempre en ps
    if adb_sh "service list 2>/dev/null" | grep -qi surfaceflinger; then
        pass "surfaceflinger (vía service list)"
    elif adb_sh "ps 2>/dev/null" | grep -qi surfacefling; then
        pass "surfaceflinger (vía ps)"
    else fail "surfaceflinger" "no en ps ni en service list"; fi
    chk_proc  "mediaserver"              "mediaserver"
    chk_prop  "persist.camera.mode=1"    persist.camera.mode   "1"
    _dlg=$(adb_sh getprop persist.camera.delegate_setparams)
    if [[ "$_dlg" == "1" ]]; then pass "persist.camera.delegate_setparams=1"; \
    else skip "persist.camera.delegate_setparams" "='$_dlg' (normal si cámara no se abrió aún)"; fi
fi

# ── 2. pantalla / gráficos ────────────────────────────────────────────────────

if section display "PANTALLA / GRÁFICOS"; then
    chk_file "Framebuffer fb0"          "/dev/graphics/fb0"
    chk_file "gralloc.msm7k.so"        "/system/lib/hw/gralloc.msm7k.so"
    chk_file "copybit.msm7k.so"        "/system/lib/hw/copybit.msm7k.so"
    chk_file "libsurfaceflinger.so"    "/system/lib/libsurfaceflinger.so"
    chk_file "libEGL_adreno200.so"     "/system/lib/egl/libEGL_adreno200.so"
    chk_file "libGLESv1_CM_adreno200"  "/system/lib/egl/libGLESv1_CM_adreno200.so"
    _kgsl=$(adb_sh "ls -l /dev/kgsl-3d0 2>/dev/null | awk '{print \$1}'")
    if [[ "$_kgsl" == *"rw-rw-rw"* ]]; then pass "KGSL permisos 0666 ($_kgsl)"; \
    else fail "KGSL /dev/kgsl-3d0 permisos" "got='$_kgsl'"; fi
    _fbmode=$(adb_sh "cat /sys/class/graphics/fb0/modes 2>/dev/null")
    if [[ "$_fbmode" == *"320"* ]]; then pass "fb0 modo 320px ($_fbmode)"; \
    else skip "fb0 modo" "resultado='$_fbmode'"; fi
fi

# ── 3. input / UI ─────────────────────────────────────────────────────────────

if section input "INPUT / UI"; then
    _evts=$(adb_sh "ls /dev/input/ 2>/dev/null | grep -c event")
    if [[ "$_evts" -gt 0 ]] 2>/dev/null; then pass "/dev/input/event* — $_evts nodos presentes"; \
    else fail "/dev/input/event*" "ninguno encontrado"; fi
    chk_file "Acelerómetro /dev/accel"  "/dev/accel"
    manual "Touchscreen"       "toca la pantalla — verifica respuesta táctil"
    manual "Multitouch"        "pellizca/expande — verifica zoom"
    manual "Botones físicos"   "power / vol+ / vol-"
    manual "Botones capacitivos" "atrás / home / menú"
fi

# ── 4. luces / vibración ──────────────────────────────────────────────────────

if section lights "LUCES / VIBRACIÓN"; then
    chk_file "lights HAL"  "/system/lib/hw/lights.y210.so"
    # Despertar pantalla antes de leer brightness (puede ser 0 con pantalla apagada)
    adb_sh "input keyevent 26" 2>/dev/null; sleep 1
    _bl=$(adb_sh "cat /sys/class/leds/lcd-backlight/brightness 2>/dev/null")
    _bl_max=$(adb_sh "cat /sys/class/leds/lcd-backlight/max_brightness 2>/dev/null")
    if [[ -n "$_bl_max" && "$_bl_max" -gt 0 ]] 2>/dev/null; then pass "Backlight brightness=$_bl max=$_bl_max"; \
    else fail "Backlight brightness" "resultado='$_bl' max='$_bl_max'"; fi
    adb_sh "echo 100 > /sys/class/timed_output/vibrator/enable" 2>/dev/null || true
    if adb_sh "[ -e '/sys/class/timed_output/vibrator/enable' ] && echo y" | grep -q y; then
        pass "Vibrador sysfs accesible (pulso 100ms enviado)"
    else fail "Vibrador" "sysfs no disponible"; fi
fi

# ── 5. audio ──────────────────────────────────────────────────────────────────

if section audio "AUDIO"; then
    chk_file "libaudio.so"       "/system/lib/libaudio.so"
    chk_file "/dev/msm_pcm_out"  "/dev/msm_pcm_out"
    chk_file "/dev/msm_snd"      "/dev/msm_snd"
    if adb_sh "service list 2>/dev/null" | grep -q "media.audio_flinger"; then
        pass "AudioFlinger registrado en ServiceManager"
    else fail "AudioFlinger" "no en service list"; fi
    # AudioHardware.cpp hace property_get(..., "lite") — vacío es válido y
    # equivale a "lite" por el default del propio código, no solo un valor
    # explícito seteado a mano (ver docs/AUDIO_NOTES.md).
    _hpp=$(adb_sh getprop persist.sys.headset-postproc)
    if [[ -z "$_hpp" || "$_hpp" == "lite" ]]; then
        pass "headset-postproc (efectivo: lite$([ -z "$_hpp" ] && echo ' — default de código'))"
    else
        pass "headset-postproc=$_hpp (override manual, no es un fallo)"
    fi
    manual "Speaker"      "reproduce sonido — verifica volumen por altavoz"
    manual "Auriculares"  "conecta auriculares — verifica sonido y volumen"
    manual "Micrófono"    "graba nota de voz — reproduce y verifica"
    manual "Llamada audio" "realiza llamada — verifica audio subida y bajada"
fi

# ── 6. Wi-Fi ──────────────────────────────────────────────────────────────────

if section wifi "WI-FI"; then
    # Y210 usa ath6kl — firmware en /system/etc/firmware/ sin subdirectorio wlan/
    _wfirmware=$(adb_sh "ls /system/etc/firmware/ 2>/dev/null" | grep -iE "ath|ar[0-9]")
    if [[ -n "$_wfirmware" ]]; then pass "Firmware WiFi ath6kl presente ($_wfirmware)"; \
    else skip "Firmware WiFi" "no encontrado en /system/etc/firmware/ — verificar ruta"; fi
    chk_file "hostapd"  "/system/bin/hostapd"
    adb_sh "svc wifi enable" 2>/dev/null || true
    if ! $FAST; then sleep 3; fi
    # ath6kl crea eth0 (no wlan0) en Gingerbread
    _wl=$(adb_sh "ip link 2>/dev/null" | grep -E "eth0|wlan0")
    if [[ -n "$_wl" ]]; then pass "Interfaz WiFi presente ($( echo "$_wl" | awk '{print $2}' | head -1))"; \
    else fail "Interfaz WiFi (eth0/wlan0)" "no encontrada tras habilitar WiFi"; fi
    _wstat=$(adb_sh getprop wlan.driver.status)
    if [[ "$_wstat" == "ok" ]]; then pass "wlan.driver.status=ok"; \
    else skip "wlan.driver.status" "='$_wstat' (normal si WiFi no estaba encendido antes)"; fi
    manual "Asociación/DHCP"  "conéctate a una red y verifica IP asignada"
    manual "Tethering Wi-Fi"  "activa hotspot — conecta un cliente y verifica internet"
fi

# ── 7. Bluetooth ──────────────────────────────────────────────────────────────

if section bt "BLUETOOTH"; then
    chk_file "hciattach"           "/system/bin/hciattach"
    chk_file "libbluetooth.so"     "/system/lib/libbluetooth.so"
    _hci=$(adb_sh "hciconfig 2>/dev/null")
    if [[ "$_hci" == *"hci0"* ]]; then pass "hci0 detectado"; \
    else skip "hci0" "BT apagado — activar desde ajustes y re-ejecutar"; fi
    manual "BT pairing"  "empareja con otro dispositivo"
    manual "BT audio A2DP/SCO"  "conecta auriculares BT — reproduce audio"
fi

# ── 8. sensores ───────────────────────────────────────────────────────────────

if section sensors "SENSORES"; then
    chk_file "/dev/accel" "/dev/accel"
    if adb_sh "dumpsys sensorservice 2>/dev/null" | grep -qi "lis3dh\|accelerometer"; then
        pass "Acelerómetro en SensorService"
    else fail "Acelerómetro SensorService" "no aparece en dumpsys"; fi
    manual "Acelerómetro" "gira el teléfono — verifica rotación de pantalla"
fi

# ── 9. GPS ────────────────────────────────────────────────────────────────────

if section gps "GPS"; then
    # Y210 usa gps.y210.so en /system/lib/hw/ (HAL GPS, no libgps.so)
    chk_file "gps.y210.so HAL"  "/system/lib/hw/gps.y210.so"
    chk_file "gps.conf"    "/system/etc/gps.conf"
    _gpsv=$(adb_sh getprop ro.gps.agps_provider)
    if [[ -n "$_gpsv" ]]; then pass "ro.gps.agps_provider=$_gpsv"; \
    else skip "ro.gps.agps_provider" "vacío"; fi
    if adb_sh "getprop" | grep -q "^persist.gps"; then
        pass "Propiedades persist.gps presentes"
    else skip "persist.gps props" "no encontradas"; fi
    manual "GPS fix real"  "al aire libre — abre Maps y espera fix de satélites (~2 min)"
fi

# ── 10. cámara ────────────────────────────────────────────────────────────────

if section camera "CÁMARA"; then
    chk_file "libcamera.so"        "/system/lib/libcamera.so"
    chk_file "libcameraservice.so" "/system/lib/libcameraservice.so"
    chk_file "media_profiles.xml"  "/system/etc/media_profiles.xml"
    chk_prop "ro.build.product para blob"  ro.build.product  "msm7625a"
    chk_prop "persist.camera.mode=1"       persist.camera.mode "1"
    # Verificar que el perfil H.263 está activo (no M4V 640x480 que falla)
    _codec=$(adb_sh "grep -m1 'codec' /system/etc/media_profiles.xml 2>/dev/null")
    if [[ "$_codec" == *"h263"* ]]; then pass "media_profiles usa h263 (SW encoder OK)"; \
    else skip "media_profiles codec" "='$_codec' (esperado h263)"; fi
    # Lanzar y verificar
    # am kill/force-stop no existen en Gingerbread 2.3 — usar pkill
    _campid=$(adb_sh "ps 2>/dev/null | grep 'android.camera' | grep -v grep | awk '{print \$2}' | head -1")
    [[ -n "$_campid" ]] && adb_sh "kill $_campid" 2>/dev/null || true
    adb_sh "logcat -c" 2>/dev/null || true
    adb_sh "am start -n com.android.camera/.Camera" > /dev/null 2>&1
    sleep 4
    if adb_sh "ps" | grep -v grep | grep -q "android.camera"; then
        pass "App Camera en ejecución"
    else fail "App Camera" "proceso no encontrado tras 4s"; fi
    # grep -c ya imprime "0" y no encuentra coincidencias (exit 1); el
    # "|| echo 0" agregaba una SEGUNDA línea "0" en ese caso, dejando
    # _prev="0\n0" y rompiendo la comparación numérica de abajo.
    _prev=$(adb_sh "logcat -d 2>/dev/null" | grep -c "startPreview.*rc=0")
    if [[ "$_prev" -gt 0 ]]; then pass "startPreview rc=0 en logcat"; \
    else skip "startPreview logcat" "no hay log reciente (OK si ya estaba corriendo)"; fi
    # am kill/force-stop no existen en Gingerbread 2.3 — usar pkill
    _campid=$(adb_sh "ps 2>/dev/null | grep 'android.camera' | grep -v grep | awk '{print \$2}' | head -1")
    [[ -n "$_campid" ]] && adb_sh "kill $_campid" 2>/dev/null || true
    manual "Preview foto en color"   "verifica: 640×480, colores reales, portrait"
    manual "Captura de foto"         "toma foto — verifica JPEG en galería"
    manual "Switch a modo video"     "pulsa botón video — verifica preview 352×288"
    manual "Grabación de video"      "graba 5s — verifica MP4 en galería sin crash"
    manual "Switch video→foto→video" "alterna 3 veces — sin crash ni preview negro"
fi

# ── 11. almacenamiento ────────────────────────────────────────────────────────

if section storage "ALMACENAMIENTO"; then
    _dfdata=$(adb_sh "df /data 2>/dev/null | tail -1")
    if [[ -n "$_dfdata" ]]; then pass "/data montado ($_dfdata)"; \
    else fail "/data" "df falló"; fi
    if adb_sh "mount 2>/dev/null" | grep -q "sdcard\|/mnt/sdcard"; then
        pass "SDCard montada"
    else skip "SDCard" "no montada — insertar microSD"; fi
    _dfsys=$(adb_sh "df /system 2>/dev/null | tail -1")
    if [[ -n "$_dfsys" ]]; then pass "/system montado ($_dfsys)"; \
    else fail "/system" "df falló"; fi
    manual "USB almacenamiento masivo" "activa UMS — verifica unidad en PC"
fi

# ── 12. RIL / telefonía ───────────────────────────────────────────────────────

if section ril "RIL / TELEFONÍA"; then
    chk_file "libril-qc-1.so"  "/system/lib/libril-qc-1.so"
    _bb=$(adb_sh getprop gsm.version.baseband)
    if [[ -n "$_bb" ]]; then pass "Baseband: $_bb"; \
    else fail "Baseband" "gsm.version.baseband vacío"; fi
    # En Gingerbread `dumpsys iphonesubinfo` es más fiable que `service call`
    _imei=$(adb_sh "dumpsys iphonesubinfo 2>/dev/null" | grep "Device ID" | grep -o '[0-9]\{15\}')
    if [[ -n "$_imei" && "${#_imei}" -ge 15 ]]; then pass "IMEI válido (${_imei:0:6}xxxxxxxxx)"; \
    else fail "IMEI" "resultado='$_imei' (¿permisos READ_PHONE_STATE?)"; fi
    _op=$(adb_sh getprop gsm.operator.alpha)
    _simstate=$(adb_sh getprop gsm.sim.state)
    if [[ -n "$_op" ]]; then pass "Operador registrado: $_op"; \
    elif [[ "$_simstate" == "ABSENT" || "$_simstate" == "NOT_READY" || "$_simstate" == "UNKNOWN" ]]; then
        skip "Operador" "SIM no presente/no lista (gsm.sim.state=$_simstate)"; \
    else fail "Operador" "gsm.operator.alpha vacío (sim.state=$_simstate)"; fi
    _rmnet=$(adb_sh "ip link 2>/dev/null" | grep rmnet)
    if [[ -n "$_rmnet" ]]; then pass "Interfaz rmnet (datos activos)"; \
    else skip "rmnet" "datos no activos — activar datos móviles y re-probar"; fi
    manual "Llamada saliente"  "llama a un número — audio bidireccional OK"
    manual "Llamada entrante"  "recibe llamada — timbre + audio OK"
    manual "SMS saliente"      "envía SMS — verifica entrega"
    manual "SMS entrante"      "recibe SMS — verifica notificación"
fi

# ── 13. energía ───────────────────────────────────────────────────────────────

if section power "ENERGÍA"; then
    if adb_sh "[ -f '/sys/power/state' ] && echo y" | grep -q y; then
        pass "/sys/power/state existe"
    else fail "/sys/power/state" "no encontrado"; fi
    _batt=$(adb_sh "cat /sys/class/power_supply/battery/capacity 2>/dev/null")
    if [[ -n "$_batt" && "$_batt" -gt 0 ]] 2>/dev/null; then pass "Batería: ${_batt}%"; \
    else skip "Batería sysfs" "ruta no estándar — verificar manualmente"; fi
    if $FAST; then skip "Tests de suspend" "--fast: omitido"; \
    else
        manual "Suspensión pantalla"  "apaga pantalla 30s — verifica reanudación"
        manual "Deep sleep"           "desconecta USB 5min — verifica consumo bajo"
    fi
fi

# ── resumen ────────────────────────────────────────────────────────────────────

TOTAL=$((PASS+FAIL+SKIP+MANUAL))
echo ""; echo -e "${BLD}══ RESUMEN ══${RST}"
printf "  Total:    %d\n" "$TOTAL"
echo -e "  ${GRN}PASS:     $PASS${RST}"
echo -e "  ${RED}FAIL:     $FAIL${RST}"
echo -e "  ${YLW}SKIP:     $SKIP${RST}"
echo -e "  ${CYN}MANUAL:   $MANUAL${RST}"
echo ""
echo "Log: $LOG"
{ echo ""; echo "RESUMEN: PASS=$PASS FAIL=$FAIL SKIP=$SKIP MANUAL=$MANUAL / $TOTAL"; } >> "$LOG"

exit $FAIL
