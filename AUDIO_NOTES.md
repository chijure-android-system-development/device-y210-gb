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
