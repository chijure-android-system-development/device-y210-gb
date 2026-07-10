# patches/archive/

Patches viejos, ya superados, que se conservan solo como referencia histórica.
No se aplican en `apply-patches.sh` — si necesitas ver qué reemplazó a uno de
estos archivos, revisa el patch activo correspondiente en `patches/`.

- `frameworks_base.patch`: patch monolítico original de `frameworks/base`
  (cámara, RIL/telephony, JNI de FM, status bar, Surface, todo junto).
  Dividido en patches específicos por área para que cada cambio sea más fácil
  de revisar/mantener por separado: `frameworks_base_camera.patch`,
  `frameworks_base_telephony.patch`, `frameworks_base_ril_class.patch`,
  `frameworks_base_fm.patch`, `frameworks_base_fm_java.patch`,
  `frameworks_base_statusbar.patch`.
