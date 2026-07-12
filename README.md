# Device Tree for Huawei Y210 (y210)

Copyright 2026 - chijure.

The Huawei Y210 (codenamed _"y210"_) is an entry-level smartphone from Huawei,
built on the Qualcomm MSM7x27a (Snapdragon S1) platform. It shipped with
Android 2.3 Gingerbread.

This tree is an unofficial CyanogenMod 7 port for the Y210, targeting real
device stability on legacy Qualcomm MSM7x27A hardware.

| Basic                    | Spec Sheet                                             |
| ------------------------:|:------------------------------------------------------- |
| CPU                      | Single-core Qualcomm Scorpion, 1 GHz (1008 MHz)         |
| Chipset                  | Qualcomm MSM7227A / MSM7x27a (Snapdragon S1)             |
| GPU                      | Adreno 200                                               |
| Memory                   | 256 MB RAM (~173 MB usable, resto reservado para radio/GPU) |
| Shipped Android Version  | 2.3 Gingerbread                                          |
| Storage                  | ~512 MB internal, microSD expandable                     |
| Display                  | 320 x 480 pixels (HVGA), ~3.5"                           |
| Camera                   | VGA-class rear camera (JPEG capture ~640x480, no HD video encoder) |
| Connectivity             | Wi-Fi (Atheros AR6000/AR6005, SDIO), Bluetooth + FM (Qualcomm WCN2243), HSPA |

## Estado del port

Todo el port está validado como **OK** en hardware real: arranque, gráficos,
Wi-Fi, Bluetooth (pairing + A2DP), sensores, RIL/telefonía (llamadas, SMS,
datos), audio, Radio FM (sintonía y audio real), cámara completa (foto y
video), GPS, deep sleep.

Fuente de verdad detallada (checklist + comandos de log por área):
[`docs/FUNCTION_MATRIX.md`](docs/FUNCTION_MATRIX.md).

Documentación técnica por área: [`docs/`](docs/) — audio, cámara, gráficos,
Wi-Fi, RIL, FM, GPS, status bar, rendimiento, logging.

## Sistema de patches

`frameworks/base` y varios `packages/apps/*` no son propios de este repo —
son checkouts de CyanogenMod en `detached HEAD`. Las customizaciones del Y210
viven como `.patch` en [`patches/`](patches/), aplicados sobre esos árboles.

```bash
# Aplicar todos los patches sobre un árbol CM7 limpio
cd /path/to/cm7
bash device/huawei/y210/patches/apply-patches.sh

# Revertir todo (ej. antes de portar a otro dispositivo sobre el mismo árbol)
bash device/huawei/y210/patches/revert-patches.sh
```

## Build

```bash
source build/envsetup.sh
breakfast y210
brunch y210
```

## Smoke test

```bash
bash device/huawei/y210/tools/test_y210.sh            # completo
bash device/huawei/y210/tools/test_y210.sh --fast     # omite suspensión
```

Resultado esperado (con SIM real): 54 PASS / 0 FAIL.
