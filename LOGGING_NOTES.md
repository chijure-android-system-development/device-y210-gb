Huawei Y210 logging notes

Resumen

- El menú Huawei original no solo activa `persist.service.logcat.enable=1`.
- Para que `adb logcat` vuelva a funcionar tras reinicio, también hace una llamada nativa OEM que termina habilitando el logger del kernel.
- En este port, esa llamada se hace a través de `libprojectmenu.so`.

Hallazgos clave

- El stock usa `ProjectMenu.logOnOff(boolean)` desde `ProjectMenuAct`.
- `libprojectmenu.so` no exporta símbolos JNI directos; usa `JNI_OnLoad` + `RegisterNatives`.
- Por eso la clase Java `com.android.huawei.projectmenu.ProjectMenu` debe declarar todas las firmas nativas esperadas por el blob.
- El blob depende de `libhwrpc.so` y usa `huawei_oem_rapi_streaming_function`.
- El kernel crea `/dev/log/main` solo cuando el NV de logger queda habilitado al boot.

Integración del device tree

- Se instalan los binarios stock:
  - `system/bin/sleeplogcat`
  - `system/bin/kmsgcat`
  - `system/bin/diag_mdlog`
- Se instala también:
  - `system/lib/libprojectmenu.so`

Notas

- La app `Y210ProjectMenu` vive fuera de este repo del device tree.
- El device tree solo aporta los prebuilts y su instalación en la ROM.
