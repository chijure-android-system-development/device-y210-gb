Y210 CM7 Audio Notes
====================

Current status
--------------

- Playback works.
- Voice recorder works.
- `mediaserver` and `AudioFlinger` stay alive in the validated playback/record path.

What was fixed
--------------

- The active HAL remains the device implementation in `device/huawei/y210/libaudio/AudioHardware.cpp`.
- A real parser bug was fixed in `get_sample_index()`.
- The product now installs the runtime data the HAL expects:
  - `system/etc/AudioFilter.csv`
  - `system/etc/AutoVolumeControl.txt`
- The missing stock blob required by the EQ parser was restored:
  - `system/lib/libaudioeq.so`
- The Y210 audio build no longer depends on headers from `device/huawei/u8160-gb`.
  Local copies now live in:
  - `device/huawei/y210/include/linux/msm_audio.h`
  - `device/huawei/y210/include/linux/msm_audio_voicememo.h`

Confirmed findings
------------------

- `get_audpp_filter()` now completes successfully during `mediaserver` startup.
- The parser reaches and accepts the stock `AudioFilter.csv` records:
  - `IIR flag[0..2] = 00`
  - `EQ flag[0..2] = 00`
  - `MBADRC flag[0..2] = 00`
- The earlier `Parsing error in AudioFilter.csv.` was caused by missing `libaudioeq.so`, not by a bad CSV.
- The earlier `failed to open AUTO_VOLUME_CONTROL` warning was caused by `AutoVolumeControl.txt` not being copied into the product.
- Playback through `AudioFlinger -> libaudio` was validated with:
  - `adb shell stagefright -a -o /system/media/audio/ui/Lock.ogg`

Notes
-----

- A specific `com.android.music/.AudioPreview` test showed `empty cursor`; that looks like an app/intent issue, not the core HAL path.
- No additional framework/audioflinger patches were needed to get the base playback and recording path working.

Recommended next checks
-----------------------

- in-call audio
- headset / speaker switching
- Bluetooth audio
- long recording / long playback stability

In-call microphone (uplink) note
--------------------------------

The QCOM `msm_snd` RPC path uses an explicit `mic_mute` flag (`SND_SET_DEVICE`),
and this HAL defaults `mMicMute=true` until the framework toggles it.

For Y210, the Phone app must route mute/unmute through `AudioManager`
(instead of `Phone.setMute()`), otherwise call audio can work *downlink-only*
while the uplink stays muted.

- Overlay: `device/huawei/y210/overlay/packages/apps/Phone/res/values/config.xml`
- Key: `<bool name="send_mic_mute_to_AudioManager">true</bool>`

Headset volume tuning (post-proc)
--------------------------------

If headset volume is noticeably lower than stock (same file, same headset),
the Y210 HAL supports a runtime knob to reduce post-processing on headset routes.

- Property: `persist.sys.headset-postproc`
- Values:
  - `lite` (default): `EQ + RX_IIR`
  - `full`: `ADRC + EQ + RX_IIR + MBADRC`
  - `off`: disable post-proc on headset
  - `0xNN`: hex mask (only known bits are kept)

Apply:

```bash
adb shell setprop persist.sys.headset-postproc lite
adb shell stop media; adb shell start media
```

Status: initial on-device test did not change perceived loudness vs stock yet.

**2026-07-06 follow-up — post-proc chain ruled out entirely as the cause:**

Retested systematically with a real Y210 stock dump available for comparison
(`/media/chijure/Datos/Desarrollo_Android/y210/ori y210/system`):

- Confirmed stock's `AudioFilter_%s.csv` (device-specific, opened before the
  generic file — stock's `libaudio.so` strings show `/system/etc/AudioFilter_%s.csv`)
  has the SAME A3/C3/D3 (headset IIR/EQ/MBADRC) coefficients as the generic
  file we were already using. Not the cause (separate real bug found and
  fixed for the *speaker* filters A1/D1, unrelated to headset volume — see
  FM_NOTES.md 2026-07-06 for that fix).
- Tested `persist.sys.headset-postproc=0x7` (ADRC+EQ+IIR, no MBADRC — a
  combination never tried before, in between the existing `lite`/`full`
  presets): **no change in perceived loudness.**
- Tested `full` (ADRC+EQ+IIR+MBADRC, matching the hypothesis that stock runs
  MBADRC on by default and that's the real loudness difference): **no
  change in perceived loudness either.**
- Confirmed via disassembly of stock's `libaudio.so` that `AudioStreamOutMSM72xx`
  does **not** export a `setVolume()` method — same as our implementation.
  Media/music volume is applied entirely as AudioFlinger *software* gain
  upstream of the HAL on both stock and CM7 (same generic frameworks/base
  code path in both) — this architecture is not the difference either.

**Conclusion:** the entire QDSP5 AUDPP post-processing chain (MBADRC/ADRC/EQ/IIR)
demonstrably has **no effect** on perceived headset loudness in this build —
ruled out completely, not just "lite" vs "full". The real cause is
somewhere else: most likely a headphone amplifier analog gain register
(PMIC/codec HPH_PA gain) set once at a lower level (kernel driver default,
or a userspace RPC/ADIE config call outside `AudioHardware.cpp` — e.g.
`libsnd.so`'s `snd_adie_svc_config_adie_block`, present on stock, NOT linked
by stock's own `libaudio.so` per `readelf -d`, so it would have to be called
from somewhere else entirely, not yet identified) — or a difference at the
kernel/board-config level (e.g. a `msm_snd`/codec driver default gain value)
that never goes through any of the userspace code paths compared so far.
Reverted to `lite` (the known-safe default) after these negative results.
Next step, if resumed: look for gain-related ioctl/RPC calls made *outside*
`AudioHardware.cpp`/`AudioPolicyManager` entirely (kernel codec driver
defaults, or another userspace daemon), since everything reachable from the
audio HAL itself has now been ruled out.

**2026-07-06, RESUELTO — era hardware, no bug de CM7:** el usuario confirmó
tras probar en la unidad física: el parlante suena bajito por desgaste de
uso, y el jack de auriculares necesita presionarse bien para hacer buen
contacto. No es una regresión de CM7 ni algo relacionado a
MBADRC/ADRC/EQ/AudioFilter — todo lo de esa sección de arriba (post-proc,
`persist.sys.headset-postproc`) queda como referencia por si en el futuro
se sospecha algo similar, pero **no perseguir este síntoma de nuevo como
bug de software** salvo que cambie la evidencia.

FM Radio bring-up note
----------------------

The `FM` app depends on the framework `android.hardware.fmradio.*` stack (JNI in
`libandroid_runtime`) plus access to the radio device nodes (`/dev/radio0`,
`/dev/msm_fm`).

Common failure modes:

- `UnsatisfiedLinkError: acquireFdNative`: FM JNI layer missing.
- `open(/dev/radio0) failed: Permission denied`: fix device node permissions.
