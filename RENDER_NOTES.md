# Y210 — Render / Display Notes

## Hardware

- Display: 320×480 px, 32 bpp (RGBA_8888)
- GPU: Adreno 200 (MSM7225A)
- Framebuffer: double-buffered, 320×960 virtual (`yres_virtual`), page-flip via `FBIOPUT_VSCREENINFO` + `yoffset`
- Stride: 1280 bytes/row (320 px × 4 bytes)
- Framebuffer driver: `msmfb30_90000`

## Stack

```
SurfaceFlinger
  └─ libagl (PixelFlinger — SW GL compositor)
       └─ copybit.y210.so (MDP3 HW blit — used by libagl for texture draws)
  └─ gralloc.y210.so (framebuffer HAL)
       └─ /dev/graphics/fb0
            └─ MSMFB_BLIT ioctl (MDP DMA engine, also via /dev/fb0)
```

SurfaceFlinger uses **software GL** (`SLOW_CONFIG` flag set). Copybit is used by libagl
to accelerate individual texture blits (e.g., app surfaces drawn to the FB), not for
the SF compositor pass itself.

## Bug: Statusbar/lockscreen "ghosting" corruption — FIXED (2026-05-02)

### Symptom

Persistent solid-color blocks covering parts of the lockscreen and statusbar area.
The blocks matched boot-animation colors (green, red, blue) and never disappeared.
Visible on every boot; only went away if the entire screen was forced to redraw.

### Root cause

`SWAP_RECTANGLE` optimization in SurfaceFlinger:

1. Adreno 200 EGL advertises `EGL_ANDROID_swap_rectangle` (confirmed in logcat).
2. `DisplayHardware::init()` detects the extension, calls
   `eglSetSwapRectangleANDROID`, and sets the `SWAP_RECTANGLE` flag.
3. `SurfaceFlinger::handleRepaint()` checks `SWAP_RECTANGLE`:
   ```cpp
   if ((flags & SWAP_RECTANGLE) || (flags & BUFFER_PRESERVED)) {
       mDirtyRegion.set(mInvalidRegion.bounds()); // dirty rect only
   } else {
       mDirtyRegion.set(hw.bounds()); // full screen
   }
   ```
4. SF only redraws the dirty region per frame. Both framebuffer pages start with
   boot-animation content; the non-dirty areas are never overwritten → permanent
   colored-block corruption.

Key evidence:
- Disabling ALL copybit blits (`stretch_copybit` returning `-EINVAL`) did **not** fix
  the corruption — proves the issue is not in the MDP/copybit path.
- Corruption was consistently solid-color blocks matching boot animation palette.
- `BUFFER_PRESERVED` is not set (Adreno 200 EGL default is `EGL_BUFFER_DESTROYED`).
- Previous incomplete fix: `dev->device.setUpdateRect = 0` in `framebuffer.cpp` was
  intended to prevent this, but it only disabled `PARTIAL_UPDATES`, which does **not**
  affect the `SWAP_RECTANGLE` code path (they are independent flags).

### Fix

`device/huawei/y210/libgralloc/framebuffer.cpp`:

Assign a no-op `fb_setUpdateRect_noop` (non-NULL function pointer) instead of `0`.
`FramebufferNativeWindow::isUpdateOnDemand()` returns `(fbDev->setUpdateRect != 0)`.
When non-NULL:
- `DisplayHardware` sets `PARTIAL_UPDATES` (0x00020000).
- `if (mFlags & PARTIAL_UPDATES) mFlags &= ~SWAP_RECTANGLE;` clears SWAP_RECTANGLE.
- `handleRepaint()` falls into the `else` branch → `mDirtyRegion = hw.bounds()` → **full-screen redraw every frame**.
- `flip()` calls `setUpdateRectangle()` → our no-op (does nothing).

The no-op intentionally does **not** write `reserved[0] = 0x54445055` ("UPDT"), which
would trigger the MSM kernel's partial-scan path via `FBIOPUT_VSCREENINFO`.

### Verification

After push of `gralloc.y210.so` + `copybit.y210.so` and `stop; start`:

```
I/SurfaceFlinger: extensions: ... EGL_ANDROID_swap_rectangle ...
I/SurfaceFlinger: flags = 00060000
```

`0x00060000` = `PARTIAL_UPDATES (0x00020000)` | `SLOW_CONFIG (0x00040000)`.  
`SWAP_RECTANGLE (0x00080000)` is not set. ✓

## KGSL permissions

`/dev/kgsl-3d0` requires group `graphics` access. On some boots the node comes up
with permissions that exclude the graphics group — verify with:

```sh
adb shell ls -l /dev/kgsl-3d0
```

Expected: `crw-rw---- root graphics`. If wrong, the ueventd rule in
`device/huawei/y210/ueventd.y210.rc` should cover this; check that it is included
in the ramdisk.

## Useful logcat filters

```sh
# SF startup flags and EGL info
adb logcat -v time -d | grep -E "SurfaceFlinger|flags ="

# Copybit activity (errors or format mismatches)
adb logcat -v time -d | grep -iE "copybit|libagl.*copy"

# Framebuffer open + geometry
adb logcat -v time -d | grep -i "y210.gralloc"
```
