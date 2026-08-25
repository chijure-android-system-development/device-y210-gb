/*
 * Y210CameraWrapper.cpp — ICS camera HAL for Huawei Y210
 *
 * Bridges the GB Qualcomm blob (libcamera.y210.so) to the ICS
 * camera_device_t / camera_module_t C HAL interface.
 *
 * Pattern based on LG e400 CM9 cameraHal.cpp (same MSM7225A SoC).
 * Y210-specific additions:
 *  - takePicture dispatched manually (blob slot 21, arg sp<ISurface>)
 *  - startPreview retry after 250ms (ISP busy after close/reopen)
 *  - Parameter sanitization (blob rejects 432x320, antibanding, etc.)
 *  - storeTargetType() primed via getCameraInfo() before first open
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "Y210Camera"

#include "Y210CameraInterface.h"

#include <hardware/camera.h>
#include <hardware/hardware.h>
#include <cutils/properties.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/msm_mdp.h>
#include <gralloc_priv.h>
#include <ui/GraphicBufferMapper.h>
#include <cutils/log.h>
#include <ui/Rect.h>
#include <utils/Vector.h>

/* ICS CM9 may not define ALOG* macros yet — map to old LOG* */
#ifndef ALOGE
#define ALOGE LOGE
#define ALOGI LOGI
#define ALOGW LOGW
#define ALOGD LOGD
#define ALOGV LOGV
#endif
#include <dlfcn.h>

#define NO_ERROR 0

struct blitreq {
    unsigned int count;
    struct mdp_blit_req req;
};

using namespace android;

/* Forward declaration of our mapMemory stub (defined in camera_compat.cpp) */
extern "C" void y210_fixed_mapMemory(void);

/* ---------------------------------------------------------------------------
 * Global state (single camera device — Y210 has one camera)
 * ------------------------------------------------------------------------- */
static camera_notify_callback         gNotifyCb    = NULL;
static camera_data_callback           gDataCb      = NULL;
static camera_data_timestamp_callback gDataTsCb    = NULL;
static camera_request_memory          gGetMemory   = NULL;
static bool                           gPreviewFramesRequested = false;

static preview_stream_ops_t*          gWindow      = NULL;
static sp<CameraHardwareInterface>    gCamera;
static CameraParameters               gCamParams;

static Vector<camera_memory_t*>       gSentFrames;

/* ---------------------------------------------------------------------------
 * Blob library — entry points resolved via dlsym (not linked at build time)
 * ------------------------------------------------------------------------- */
typedef sp<CameraHardwareInterface> (*OpenCamFn)(int cameraId, int mode);
typedef void (*GetCamInfoFn)(int cameraId, CameraInfo* info);
typedef int  (*GetNumCamsFn)(void);

static OpenCamFn    gOpenCamera = NULL;
static GetCamInfoFn gGetCamInfo = NULL;
static GetNumCamsFn gGetNumCams = NULL;

static void ensureLibLoaded()
{
    if (gOpenCamera) return;
    /* Expose compat stubs globally so the blob's relocations can find them */
    void* compat = dlopen("libcamera_compat.so", RTLD_NOW | RTLD_GLOBAL);
    if (!compat) LOGE("Y210: dlopen libcamera_compat.so: %s", dlerror());
    else         LOGI("Y210: libcamera_compat.so loaded globally");
    /* Preload liboemcamera.so globally (blob dlopen-s it internally) */
    dlopen("liboemcamera.so", RTLD_NOW | RTLD_GLOBAL);

    void* handle = dlopen("libcamera.y210.so", RTLD_NOW);
    if (!handle) { LOGE("Y210: dlopen libcamera.y210.so: %s", dlerror()); return; }

    gOpenCamera = (OpenCamFn)   dlsym(handle, "HAL_openCameraHardware");
    gGetCamInfo = (GetCamInfoFn)dlsym(handle, "HAL_getCameraInfo");
    gGetNumCams = (GetNumCamsFn)dlsym(handle, "HAL_getNumberOfCameras");

    if (!gOpenCamera || !gGetCamInfo || !gGetNumCams)
        LOGE("Y210: failed to resolve camera entry points");
    else
        LOGI("Y210: camera blob loaded OK");
}

/* ---------------------------------------------------------------------------
 * takePicture — manual vtable dispatch (blob slot 21, ISurface arg)
 * All other methods use normal C++ virtual dispatch via Y210CameraInterface.h
 * ------------------------------------------------------------------------- */
static inline void** blobVptr(const sp<CameraHardwareInterface>& cam)
{
    return *reinterpret_cast<void***>(cam.get());
}

static status_t blobTakePicture()
{
    /* Blob slot 21: takePicture(const sp<ISurface>&)
     * Pass a null ISurface — blob skips postview but delivers JPEG callbacks. */
    typedef status_t (*TakePicFn)(void*, const sp<ISurface>&);
    TakePicFn fn = reinterpret_cast<TakePicFn>(blobVptr(gCamera)[21]);
    sp<ISurface> nullSurface;
    return fn(gCamera.get(), nullSurface);
}

/* ---------------------------------------------------------------------------
 * Parameter helpers (from Y210 CM7 wrapper)
 * ------------------------------------------------------------------------- */
static CameraParameters seedParameters()
{
    CameraParameters p;
    p.setPreviewSize(640, 480);
    p.setPreviewFrameRate(15);
    p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.setPictureSize(640, 480);
    p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    p.set(CameraParameters::KEY_JPEG_QUALITY, "100");
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "512");
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "384");
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "90");
    p.set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);
    p.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);
    p.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_NONE);
    p.set(CameraParameters::KEY_ROTATION, "0");
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
          "640x480,480x320,352x288,240x160,176x144");
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
          "640x480,512x384,320x240");
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "15");
    p.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "5000,31000");
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(5000,31000)");
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
          CameraParameters::PIXEL_FORMAT_JPEG);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS,
          CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE,
          CameraParameters::WHITE_BALANCE_AUTO);
    p.set(CameraParameters::KEY_SUPPORTED_EFFECTS, CameraParameters::EFFECT_NONE);
    p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
          CameraParameters::FOCUS_MODE_AUTO);
    p.setVideoSize(352, 288);
    p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT,
          CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.set("video-hfr", "off");
    p.set("video-hfr-values", "off");
    p.set("supported-video-sizes", "352x288,320x240,176x144");
    p.set("preferred-preview-size-for-video", "352x288");
    /* Disable sharpness/ASF filter to avoid vfe_util_update_asf_5x5 crash
     * when the VFE coefficient table isn't initialized for this sensor. */
    p.set("sharpness", "0");
    p.set("sharpness-min", "0");
    p.set("sharpness-max", "0");
    return p;
}

static void fixupParameters(CameraParameters& p)
{
    /* Force YUV420SP for preview and video — required by TextureManager NV21 path */
    p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT,
          CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.set(CameraParameters::KEY_PREVIEW_FORMAT,
          CameraParameters::PIXEL_FORMAT_YUV420SP);

    /* Ensure supported sizes are present */
    if (!p.get(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES))
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
              "640x480,480x320,352x288,240x160,176x144");
    if (!p.get(CameraParameters::KEY_SUPPORTED_VIDEO_SIZES))
        p.set(CameraParameters::KEY_SUPPORTED_VIDEO_SIZES,
              "352x288,320x240,176x144");
    if (!p.get(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES))
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "15");
    if (!p.get(CameraParameters::KEY_PREVIEW_FRAME_RATE))
        p.set(CameraParameters::KEY_PREVIEW_FRAME_RATE, "15");
    if (!p.get(CameraParameters::KEY_VIDEO_SIZE))
        p.set(CameraParameters::KEY_VIDEO_SIZE, "352x288");
    if (!p.get("record-size"))
        p.set("record-size", "352x288");
    if (!p.get(CameraParameters::KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO))
        p.set(CameraParameters::KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO, "352x288");
}

/* ---------------------------------------------------------------------------
 * Preview — push NV21 frames to ANativeWindow via MDP blit (HW path)
 * Falls back to software copy if MDP blit fails.
 * Same approach as e400 cameraHal.cpp (same MSM7225A SoC).
 * ------------------------------------------------------------------------- */
static bool copyBuffersMDP(int srcFd, int dstFd,
                            size_t srcOffset, size_t dstOffset,
                            int srcFormat, int dstFormat,
                            int w, int h)
{
    struct blitreq blit;
    int fb_fd = open("/dev/graphics/fb0", O_RDWR);
    if (fb_fd < 0) {
        ALOGD("Y210: MDP blit: cannot open fb0: %s", strerror(errno));
        return false;
    }

    memset(&blit, 0, sizeof(blit));
    blit.count = 1;
    blit.req.flags       = 0;
    blit.req.alpha       = 0xff;
    blit.req.transp_mask = 0xffffffff;

    blit.req.src.width     = blit.req.dst.width     = w;
    blit.req.src.height    = blit.req.dst.height    = h;
    blit.req.src.format    = srcFormat;
    blit.req.dst.format    = dstFormat;
    blit.req.src.memory_id = srcFd;
    blit.req.dst.memory_id = dstFd;
    blit.req.src.offset    = srcOffset;
    blit.req.dst.offset    = dstOffset;
    blit.req.src_rect.w    = blit.req.dst_rect.w = w;
    blit.req.src_rect.h    = blit.req.dst_rect.h = h;

    bool ok = (ioctl(fb_fd, MSMFB_BLIT, &blit) == 0);
    if (!ok) ALOGV("Y210: MDP blit failed: %s", strerror(errno));
    close(fb_fd);
    return ok;
}

static void handlePreviewFrame(const sp<IMemory>& dataPtr,
                                preview_stream_ops_t* win,
                                camera_request_memory getMemory,
                                int previewW, int previewH)
{
    if (!win || !getMemory) return;

    ssize_t offset;
    size_t  size;
    sp<IMemoryHeap> heap = dataPtr->getMemory(&offset, &size);

    /* Request an ANativeWindow buffer in RGBX_8888 (MDP HW blit from NV21) */
    win->set_usage(win, GRALLOC_USAGE_PRIVATE_0 | GRALLOC_USAGE_SW_READ_OFTEN);
    if (win->set_buffers_geometry(win, previewW, previewH,
                                   HAL_PIXEL_FORMAT_RGBX_8888) != 0) {
        ALOGW("Y210: set_buffers_geometry failed");
        return;
    }

    int32_t stride;
    buffer_handle_t* bufHandle = NULL;
    if (win->dequeue_buffer(win, &bufHandle, &stride) != 0) return;
    if (win->lock_buffer(win, bufHandle) != 0) {
        win->cancel_buffer(win, bufHandle);
        return;
    }

    const private_handle_t* priv =
        reinterpret_cast<const private_handle_t*>(*bufHandle);

    bool blitOk = copyBuffersMDP(heap->getHeapID(), priv->fd,
                                  (size_t)offset, (size_t)priv->offset,
                                  MDP_Y_CBCR_H2V2, MDP_BGRA_8888,
                                  previewW, previewH);

    if (!blitOk) {
        /* Software fallback: memcpy NV21 into the gralloc buffer.
         * The gralloc buffer is RGBX_8888 so this will look wrong, but
         * keeps the pipeline alive until MDP blit is confirmed working. */
        GraphicBufferMapper& mapper = GraphicBufferMapper::get();
        android::Rect bounds(previewW, previewH);
        void* dst;
        if (mapper.lock(*bufHandle, GRALLOC_USAGE_SW_WRITE_OFTEN,
                         bounds, &dst) == 0) {
            memcpy(dst, (uint8_t*)heap->base() + offset,
                   (size < (size_t)(previewW * previewH * 4))
                   ? size : (size_t)(previewW * previewH * 4));
            mapper.unlock(*bufHandle);
        }
    }

    win->enqueue_buffer(win, bufHandle);
}

/* ---------------------------------------------------------------------------
 * GB callback shims
 * ------------------------------------------------------------------------- */
static void notifyShim(int32_t msg, int32_t ext1, int32_t ext2, void* user)
{
    if (gNotifyCb) gNotifyCb(msg, ext1, ext2, user);
}

static camera_memory_t* genClientData(const sp<IMemory>& dataPtr, void* user)
{
    ssize_t offset;
    size_t  size;
    sp<IMemoryHeap> heap = dataPtr->getMemory(&offset, &size);
    camera_memory_t* mem = gGetMemory(-1, size, 1, user);
    if (mem) memcpy(mem->data, (uint8_t*)heap->base() + offset, size);
    return mem;
}

static void dataShim(int32_t msg, const sp<IMemory>& data, void* user)
{
    /* For non-preview msgs (or when preview frames are externally requested),
     * deliver to framework via data callback */
    if ((msg != CAMERA_MSG_PREVIEW_FRAME || gPreviewFramesRequested)
            && gDataCb && gGetMemory) {
        camera_memory_t* mem = genClientData(data, user);
        if (mem) {
            gDataCb(msg, mem, 0, NULL, user);
            mem->release(mem);
        }
    }

    if (msg == CAMERA_MSG_PREVIEW_FRAME) {
        int pw, ph;
        gCamParams.getPreviewSize(&pw, &ph);
        if (pw > 0 && ph > 0)
            handlePreviewFrame(data, gWindow, gGetMemory, pw, ph);
    }
}

static void dataTsShim(nsecs_t ts, int32_t msg,
                        const sp<IMemory>& data, void* user)
{
    if (!gDataTsCb || !gGetMemory) return;
    camera_memory_t* mem = genClientData(data, user);
    if (mem) {
        gSentFrames.push_back(mem);
        gDataTsCb(ts, msg, gSentFrames.top(), 0, user);
        gCamera->releaseRecordingFrame(data);
    }
}

/* ---------------------------------------------------------------------------
 * camera_device_ops_t implementations
 * ------------------------------------------------------------------------- */
static int y210_set_preview_window(camera_device_t* /*dev*/,
                                    preview_stream_ops_t* window)
{
    gWindow = window;
    return 0;
}

static void y210_set_callbacks(camera_device_t* /*dev*/,
                                camera_notify_callback         notify_cb,
                                camera_data_callback           data_cb,
                                camera_data_timestamp_callback data_cb_ts,
                                camera_request_memory          get_memory,
                                void*                          user)
{
    gNotifyCb  = notify_cb;
    gDataCb    = data_cb;
    gDataTsCb  = data_cb_ts;
    gGetMemory = get_memory;
    gCamera->setCallbacks(notifyShim, dataShim, dataTsShim, user);
}

static void y210_enable_msg_type(camera_device_t* /*dev*/, int32_t msg)
{
    if (msg & CAMERA_MSG_PREVIEW_FRAME)
        gPreviewFramesRequested = true;
    gCamera->enableMsgType(msg);
}

static void y210_disable_msg_type(camera_device_t* /*dev*/, int32_t msg)
{
    if (msg & CAMERA_MSG_PREVIEW_FRAME)
        gPreviewFramesRequested = false;
    if (msg == CAMERA_MSG_VIDEO_FRAME) {
        /* Release any stale recording frames */
        for (size_t i = 0; i < gSentFrames.size(); i++)
            gSentFrames[i]->release(gSentFrames[i]);
        gSentFrames.clear();
    }
    gCamera->disableMsgType(msg);
}

static int y210_msg_type_enabled(camera_device_t* /*dev*/, int32_t msg)
{
    return gCamera->msgTypeEnabled(msg) ? 1 : 0;
}

static bool sMapMemoryVtablePatched = false;

static int y210_start_preview(camera_device_t* /*dev*/)
{
    LOGI("Y210: start_preview");

    /* Patch MemoryHeapPmem vtable[7] to fix the mapMemory calling-convention mismatch.
     *
     * Root cause: the GB blob calls mapMemory with GB's convention:
     *   r0 = MemoryHeapPmem* (this),  r1 = offset,  r2 = size
     * but at the call site the blob passes:
     *   r0 = MemoryHeapPmem* (correct), r1 = vptr (garbage), r2 = fn_ptr (garbage)
     * because it doesn't explicitly set r1/r2 before blx r2 at blob:0x16e30.
     *
     * ICS's mapMemory then does r4 = r1 (= vptr), [r4, #0] (= vtable_entry[0] = destructor),
     * and vtable[11] of that = bytes from code section = garbage → crash.
     *
     * Fix: replace vtable[7] (mapMemory slot) with our stub that:
     *   1. Receives correct this in r0
     *   2. Calls ICS's createMemory directly via vtable[11] with proper args
     *
     * Vtable file offsets in this libbinder.so build:
     *   Primary vtable base: file 0x27208 (destructor1 at vtable[0])
     *   vtable[7]  = mapMemory = file 0x27224 → our patch target
     *   vtable[11] = createMemory = file 0x27234
     *   MemoryHeapPmemD1Ev at file 0x1dbd4
     *
     * This only needs to run ONCE per process: libbinder.so's load address and
     * vtable layout are fixed for the lifetime of mediaserver, so redoing the
     * dlsym()+mprotect()+vtable-write on every start_preview() (i.e. on every
     * photo, since the app calls startPreview() again right after each capture)
     * just re-races the linker's non-thread-safe dlopen/dlsym/dlerror path
     * against whatever the blob's own snapshot/jpeg worker threads are doing
     * at that exact moment (dlclose of libimageprocess.so etc during postview
     * teardown) — see [[issue_camera_capture_crash]] Bug 3 (SIGSEGV in
     * __dl_format_buffer). Guard it so it only executes on the first call. */
    if (!sMapMemoryVtablePatched) {
        void* mhpd1 = dlsym(RTLD_DEFAULT, "_ZN7android14MemoryHeapPmemD1Ev");
        if (mhpd1) {
            uintptr_t libbinder_base = ((uintptr_t)mhpd1 & ~1u) - 0x1dbd4u;
            LOGI("Y210: libbinder base = 0x%x", (unsigned)libbinder_base);

            volatile uintptr_t* vtable =
                (volatile uintptr_t*)(libbinder_base + 0x27208);

            LOGI("Y210: vtable[7]=mapMemory=0x%x  vtable[11]=createMemory=0x%x",
                 (unsigned)vtable[7], (unsigned)vtable[11]);

            /* GCC ARM already encodes the THUMB bit in function pointers.
             * Do NOT add 1 — the pointer already has bit 0 set for THUMB. */
            uintptr_t stub = (uintptr_t)y210_fixed_mapMemory;

            uintptr_t page = (libbinder_base + 0x27208 + 7*4) & ~0xfffu;
            mprotect((void*)page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
            vtable[7] = stub; /* replace mapMemory */
            mprotect((void*)page, 4096, PROT_READ | PROT_EXEC);
            LOGI("Y210: vtable[7] patched → our mapMemory stub 0x%x",
                 (unsigned)stub);
            sMapMemoryVtablePatched = true;
        } else {
            LOGE("Y210: MemoryHeapPmemD1 not found: %s", dlerror());
        }
    }

    /* Call setOverlay(NULL) so the blob initializes its VFE pipeline (ASF tables,
     * sharpness, etc.).  In ICS there is no Overlay API but the GB blob runs
     * its VFE init code inside setOverlay even with a null sp<>.  Without this
     * the blob leaves the ASF coefficient pointer uninitialized → SIGSEGV in
     * vfe_util_update_asf_5x5 on the first VFE output frame. */
    gCamera->setOverlay(sp<Overlay>());
    LOGI("Y210: setOverlay(null) called to prime VFE init");

    gCamera->enableMsgType(CAMERA_MSG_PREVIEW_FRAME);

    status_t rc = gCamera->startPreview();
    LOGI("Y210: start_preview blob returned rc=%d (0=OK)", rc);
    if (rc != 0) {
        LOGW("Y210: start_preview rc=%d, retry...", rc);
        usleep(250000);
        rc = gCamera->startPreview();
        LOGI("Y210: start_preview retry rc=%d", rc);
    }
    return rc;
}

static void y210_stop_preview(camera_device_t* /*dev*/)
{
    gCamera->disableMsgType(CAMERA_MSG_PREVIEW_FRAME);
    gCamera->stopPreview();
}

static int y210_preview_enabled(camera_device_t* /*dev*/)
{
    /* Always report NOT enabled so CameraService always calls start_preview.
     * The blob marks mCameraRunning=1 after getCameraInfo()->startCamera(),
     * which would cause startPreviewMode() to return early without calling
     * our HAL. The blob's startPreview() is idempotent — safe to call again. */
    return 0;
}

static int y210_store_meta_data_in_buffers(camera_device_t* /*dev*/, int /*en*/)
{
    return (int)INVALID_OPERATION;
}

static int y210_start_recording(camera_device_t* /*dev*/)
{
    gCamera->enableMsgType(CAMERA_MSG_VIDEO_FRAME);
    gCamera->startRecording();
    return NO_ERROR;
}

static void y210_stop_recording(camera_device_t* /*dev*/)
{
    gCamera->disableMsgType(CAMERA_MSG_VIDEO_FRAME);
    gCamera->stopRecording();
}

static int y210_recording_enabled(camera_device_t* /*dev*/)
{
    return gCamera->recordingEnabled() ? 1 : 0;
}

static void y210_release_recording_frame(camera_device_t* /*dev*/,
                                          const void* opaque)
{
    if (!opaque) return;
    for (size_t i = 0; i < gSentFrames.size(); i++) {
        if (gSentFrames[i]->data == opaque) {
            gSentFrames[i]->release(gSentFrames[i]);
            gSentFrames.removeAt(i);
            return;
        }
    }
    ALOGW("Y210: release_recording_frame: unknown opaque %p", opaque);
}

static int y210_auto_focus(camera_device_t* /*dev*/)
{
    gCamera->autoFocus();
    return NO_ERROR;
}

static int y210_cancel_auto_focus(camera_device_t* /*dev*/)
{
    gCamera->cancelAutoFocus();
    return NO_ERROR;
}

static int y210_take_picture(camera_device_t* /*dev*/)
{
    /* Enable all capture callbacks before taking the picture */
    gCamera->enableMsgType(CAMERA_MSG_SHUTTER |
                            CAMERA_MSG_POSTVIEW_FRAME |
                            CAMERA_MSG_RAW_IMAGE |
                            CAMERA_MSG_COMPRESSED_IMAGE);
    /* Manual dispatch to blob slot 21: takePicture(const sp<ISurface>&) */
    return blobTakePicture();
}

static int y210_cancel_picture(camera_device_t* /*dev*/)
{
    return gCamera->cancelPicture();
}

static int y210_set_parameters(camera_device_t* /*dev*/, const char* parms)
{
    ALOGV("Y210: set_parameters: %s", parms ? parms : "(null)");
    if (!parms) return (int)BAD_VALUE;

    String8 str(parms);
    gCamParams.unflatten(str);

    /* Remap 432x320 → 480x320: blob rejects 432x320 internally */
    int pw, ph;
    gCamParams.getPreviewSize(&pw, &ph);
    if (pw == 432 && ph == 320) {
        gCamParams.setPreviewSize(480, 320);
        ALOGD("Y210: remapped preview 432x320 → 480x320");
    }

    /* Strip parameters the blob rejects to avoid blocking the whole call */
    gCamParams.remove(CameraParameters::KEY_SCENE_MODE);
    gCamParams.remove(CameraParameters::KEY_ANTIBANDING);
    gCamParams.remove(CameraParameters::KEY_FLASH_MODE);
    gCamParams.remove("focus-areas");
    gCamParams.remove("metering-areas");
    gCamParams.remove(CameraParameters::KEY_EXPOSURE_COMPENSATION);
    gCamParams.remove(CameraParameters::KEY_PREVIEW_FPS_RANGE);

    gCamera->setParameters(gCamParams);
    return NO_ERROR;
}

static char* y210_get_parameters(camera_device_t* /*dev*/)
{
    ALOGV("Y210: get_parameters");
    gCamParams = gCamera->getParameters();
    fixupParameters(gCamParams);
    String8 str = gCamParams.flatten();
    char* rc = strdup(str.string());
    ALOGV("Y210: get_parameters returning %p: %s", rc, rc ? rc : "");
    return rc;
}

static void y210_put_parameters(camera_device_t* /*dev*/, char* parms)
{
    free(parms);
}

static int y210_send_command(camera_device_t* /*dev*/,
                              int32_t cmd, int32_t arg0, int32_t arg1)
{
    return gCamera->sendCommand(cmd, arg0, arg1);
}

static void y210_release(camera_device_t* /*dev*/)
{
    ALOGI("Y210: release");
    for (size_t i = 0; i < gSentFrames.size(); i++)
        gSentFrames[i]->release(gSentFrames[i]);
    gSentFrames.clear();
    gCamera->release();
}

static int y210_dump(camera_device_t* /*dev*/, int fd)
{
    Vector<String16> args;
    return gCamera->dump(fd, args);
}

static int camera_device_close(hw_device_t* device)
{
    ALOGI("Y210: camera_device_close");
    camera_device_t* cam = reinterpret_cast<camera_device_t*>(device);
    if (gCamera != NULL) gCamera.clear();
    if (cam) {
        if (cam->ops) free(cam->ops);
        free(cam);
    }
    return NO_ERROR;
}

static camera_device_ops_t y210_ops = {
    set_preview_window:          y210_set_preview_window,
    set_callbacks:               y210_set_callbacks,
    enable_msg_type:             y210_enable_msg_type,
    disable_msg_type:            y210_disable_msg_type,
    msg_type_enabled:            y210_msg_type_enabled,
    start_preview:               y210_start_preview,
    stop_preview:                y210_stop_preview,
    preview_enabled:             y210_preview_enabled,
    store_meta_data_in_buffers:  y210_store_meta_data_in_buffers,
    start_recording:             y210_start_recording,
    stop_recording:              y210_stop_recording,
    recording_enabled:           y210_recording_enabled,
    release_recording_frame:     y210_release_recording_frame,
    auto_focus:                  y210_auto_focus,
    cancel_auto_focus:           y210_cancel_auto_focus,
    take_picture:                y210_take_picture,
    cancel_picture:              y210_cancel_picture,
    set_parameters:              y210_set_parameters,
    get_parameters:              y210_get_parameters,
    put_parameters:              y210_put_parameters,
    send_command:                y210_send_command,
    release:                     y210_release,
    dump:                        y210_dump,
};

/* ---------------------------------------------------------------------------
 * camera_module_t ops
 * ------------------------------------------------------------------------- */
int CameraHAL_GetCamInfo(int camera_id, struct camera_info* info)
{
    /* Don't load the blob here — it may crash if RPC services aren't up yet.
     * Return fixed info; orientation is corrected at open time anyway. */
    (void)camera_id;
    info->facing      = CAMERA_FACING_BACK;
    info->orientation = 90; /* sensor mounted 90° in portrait phone */
    return NO_ERROR;
}

static int get_number_of_cameras_impl(void)
{
    return 1; /* Y210 has one rear camera; don't load blob at startup */
}

static int camera_device_open(const hw_module_t* module, const char* name,
                               hw_device_t** device)
{
    int camera_id = atoi(name);
    LOGI("Y210: camera_device_open id=%d", camera_id);

    ensureLibLoaded();
    if (!gOpenCamera) { LOGE("Y210: blob not loaded"); return -ENOSYS; }

    /* Prime the blob: set gCamTargetType = 1 (TARGET_MSM7625A) directly.
     *
     * The blob's storeTargetType() reads ro.build.product and uses strncmp(n=7)
     * to categorize the platform, then strncmp(n=8) against a hardcoded
     * "msm7625a" literal to set the final type=1.  Since ro.build.product=y210
     * never matches either comparison, gCamTargetType stays at TARGET_INVALID=7.
     *
     * Fix: resolve storeTargetType via dlsym, compute the fixed offset to
     * gCamTargetType (0x133d6 bytes ahead in the same .so), and write 1 directly.
     * The offset is constant for this specific blob version (md5 verified).
     *
     * Offset derivation (from disassembly):
     *   storeTargetType VA = 0xc968 (LOAD1)
     *   gCamTargetType  VA = 0x1fd3e (LOAD2)
     *   delta            = 0x1fd3e - 0xc968 = 0x133d6
     */
    {
        /* Prime the blob BSS fields that HAL_openCameraHardware checks.
         *
         * getCameraInfo() would normally fill these, but it opens the camera
         * control FD and leaves it open.  When startPreviewInternal() later
         * tries to open preview PMEM buffers, the driver rejects the second
         * open → MemoryHeapPmem::mapMemory crashes at a bad function pointer.
         *
         * Instead, write the required BSS fields directly:
         *
         *   storeTargetType VA = 0xc968
         *   gCamTargetType  VA = 0x1fd3e  delta = 0x133d6  value = 1
         *   camera_count    VA = 0x1fe3c  delta = 0x134d4  value = 1
         *   modes_supported VA = 0x1fe40  delta = 0x134d8  value = 5
         *   camera_id[0]    VA = 0x1fe44  delta = 0x134dc  value = 0 (BSS)
         *
         * modes_supported=5 is read from getCameraInfo log ("modes_supported: 5")
         * and passed to the mode-check in HAL_openCameraHardware:
         *   tst camera_mode, modes_supported → must be != 0.
         */
        void* storeTypeSym = dlsym(RTLD_DEFAULT,
            "_ZN7android22QualcommCameraHardware15storeTargetTypeEv");
        if (storeTypeSym) {
            uintptr_t fn = (uintptr_t)storeTypeSym & ~(uintptr_t)1;

            volatile int* gCamTargetType =
                reinterpret_cast<volatile int*>(fn + 0x133d6);
            volatile int* camera_count =
                reinterpret_cast<volatile int*>(fn + 0x134d4);
            volatile int* modes_supported =
                reinterpret_cast<volatile int*>(fn + 0x134d8);

            *gCamTargetType   = 1; /* TARGET_MSM7625A */
            *camera_count     = 1; /* one camera     */
            *modes_supported  = 5; /* sensor modes_supported from getCameraInfo */

            LOGI("Y210: BSS primed: targetType=1 count=1 modes=5 (fn=%p)", (void*)fn);
        } else {
            LOGE("Y210: storeTargetType symbol not found: %s", dlerror());
        }
    }

    /* Read camera mode from property (default 1 = normal) */
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.camera.mode", value, "1");
    int mode = atoi(value);

    gCamera = gOpenCamera(camera_id, mode);
    if (gCamera == NULL) {
        LOGE("Y210: HAL_openCameraHardware returned NULL (id=%d mode=%d)",
              camera_id, mode);
        return -ENODEV;
    }

    /* Seed parameter cache */
    gCamParams = seedParameters();

    camera_device_t* cam_dev = (camera_device_t*)malloc(sizeof(*cam_dev));
    camera_device_ops_t* cam_ops = (camera_device_ops_t*)malloc(sizeof(*cam_ops));
    if (!cam_dev || !cam_ops) {
        if (cam_dev)  free(cam_dev);
        if (cam_ops)  free(cam_ops);
        gCamera.clear();
        return -ENOMEM;
    }
    memset(cam_dev,  0, sizeof(*cam_dev));
    memcpy(cam_ops, &y210_ops, sizeof(y210_ops));

    cam_dev->common.tag     = HARDWARE_DEVICE_TAG;
    cam_dev->common.version = 0;
    cam_dev->common.module  = const_cast<hw_module_t*>(module);
    cam_dev->common.close   = camera_device_close;
    cam_dev->ops            = cam_ops;

    *device = &cam_dev->common;
    LOGI("Y210: camera_device_open OK camera=%p", gCamera.get());
    return NO_ERROR;
}

static hw_module_methods_t camera_module_methods = {
    open: camera_device_open,
};

camera_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag:            HARDWARE_MODULE_TAG,
        version_major:  1,
        version_minor:  0,
        id:             CAMERA_HARDWARE_MODULE_ID,
        name:           "Huawei Y210 Camera HAL (ICS)",
        author:         "CM9 Y210 port",
        methods:        &camera_module_methods,
        dso:            NULL,
        reserved:       {0},
    },
    get_number_of_cameras: get_number_of_cameras_impl,
    get_camera_info:       CameraHAL_GetCamInfo,
};
