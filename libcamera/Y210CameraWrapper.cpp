/*
 * Copyright (C) 2026
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define LOG_TAG "Y210CameraWrapper"

#include <assert.h>
#include <dlfcn.h>

#include <cutils/properties.h>
#include <utils/Log.h>

#include "Y210CameraWrapper.h"

namespace android {

typedef sp<CameraHardwareInterface> (*OpenCamFunc)(int, int);
typedef void (*GetCamInfoFunc)(int, struct CameraInfo*);
typedef int (*GetNumCamerasFunc)();

static OpenCamFunc gOpenCameraHardware = NULL;
static GetCamInfoFunc gGetCameraInfo = NULL;
static GetNumCamerasFunc gGetNumberOfCameras = NULL;
static void *gLibHandle = NULL;
static void *gOemCameraHandle = NULL;

static void ensureY210CameraLibOpened();

// The stock Huawei/Qualcomm blob appears to read a second "camera mode"
// argument from HAL_openCameraHardware(). When we call it as a one-arg
// function, r1 contains garbage and the blob logs a random mode before
// failing startCamera(). Make the mode selectable at runtime so we can
// probe which Huawei/Qualcomm value this blob expects.
static const int kDefaultCameraMode = 1;

static int getCameraMode()
{
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.camera.mode", value, "");
    if (value[0] == '\0') {
        return kDefaultCameraMode;
    }

    char *end = NULL;
    long mode = strtol(value, &end, 0);
    if (end == value || *end != '\0') {
        LOGW("Invalid persist.camera.mode value '%s', falling back to %d",
                value, kDefaultCameraMode);
        return kDefaultCameraMode;
    }

    return static_cast<int>(mode);
}

static bool shouldDelegateRelease()
{
    char gated[PROPERTY_VALUE_MAX];
    property_get("debug.camera.delegate_release", gated, "");
    if (gated[0] != '\0') {
        return strcmp(gated, "0") != 0;
    }

    property_get("camera.delegate_release", gated, "");
    if (gated[0] != '\0') {
        return strcmp(gated, "0") != 0;
    }

    property_get("persist.camera.delegate_release", gated, "");
    if (!strcmp(gated, "1")) {
        return true;
    }

    char value[PROPERTY_VALUE_MAX];
    property_get("persist.camera.skip_release", value, "1");
    return strcmp(value, "1") != 0;
}

static bool shouldDelegateSetParameters()
{
    char value[PROPERTY_VALUE_MAX];
    property_get("debug.camera.delegate_setparams", value, "");
    if (value[0] != '\0') {
        return strcmp(value, "0") != 0;
    }

    property_get("camera.delegate_setparams", value, "");
    if (value[0] != '\0') {
        return strcmp(value, "0") != 0;
    }

    property_get("persist.camera.delegate_setparams", value, "1");
    return strcmp(value, "0") != 0;
}

static bool shouldDelegateTakePicture()
{
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.camera.delegate_takepicture", value, "0");
    return strcmp(value, "0") != 0;
}

static bool shouldNullIfaceOnRelease()
{
    char value[PROPERTY_VALUE_MAX];
    property_get("debug.camera.null_iface_on_release", value, "");
    if (value[0] != '\0') {
        return strcmp(value, "0") != 0;
    }

    property_get("camera.null_iface_on_release", value, "");
    if (value[0] != '\0') {
        return strcmp(value, "0") != 0;
    }

    property_get("persist.camera.null_iface_on_release", value, "0");
    return strcmp(value, "0") != 0;
}

static const char* cameraMsgName(int32_t msgType)
{
    switch (msgType) {
    case CAMERA_MSG_ERROR: return "ERROR";
    case CAMERA_MSG_SHUTTER: return "SHUTTER";
    case CAMERA_MSG_FOCUS: return "FOCUS";
    case CAMERA_MSG_ZOOM: return "ZOOM";
    case CAMERA_MSG_PREVIEW_FRAME: return "PREVIEW_FRAME";
    case CAMERA_MSG_VIDEO_FRAME: return "VIDEO_FRAME";
    case CAMERA_MSG_POSTVIEW_FRAME: return "POSTVIEW_FRAME";
    case CAMERA_MSG_RAW_IMAGE: return "RAW_IMAGE";
    case CAMERA_MSG_COMPRESSED_IMAGE: return "COMPRESSED_IMAGE";
    default: return "UNKNOWN";
    }
}

static bool isDelegatableParameterKey(const char* key)
{
    if (key == NULL) {
        return false;
    }

    return !strcmp(key, CameraParameters::KEY_PREVIEW_SIZE) ||
            !strcmp(key, CameraParameters::KEY_PICTURE_SIZE) ||
            !strcmp(key, CameraParameters::KEY_PREVIEW_FORMAT) ||
            !strcmp(key, CameraParameters::KEY_PICTURE_FORMAT) ||
            !strcmp(key, CameraParameters::KEY_JPEG_QUALITY) ||
            !strcmp(key, CameraParameters::KEY_ROTATION);
}

wp<CameraHardwareInterface> Y210CameraWrapper::sSingleton;
static sp<CameraHardwareInterface> gPinnedCameraHardware;
static void* gLastCtorIface = NULL;
static void* gLastReleasedIface = NULL;
static void* gLastReleasedWrapper = NULL;
static bool gLastReleasedPreviewRunning = false;
static bool gLastReleasedReleased = false;

static void ensureY210CameraLibOpened()
{
    if (gLibHandle != NULL) {
        return;
    }

    gLibHandle = ::dlopen("libcamera.y210.so", RTLD_NOW);
    if (gLibHandle == NULL) {
        LOGE("dlopen(libcamera.y210.so) failed: %s", dlerror());
        return;
    }

    // The proprietary QualcommCameraHardware in libcamera.y210.so attempts to dlopen
    // liboemcamera.so during createInstance(). On this stack we observe dlopen failures
    // with an empty dlerror(), leading to a broken construction and a crash during
    // cleanup. Preload the OEM library globally so the blob sees it as already loaded.
    gOemCameraHandle = ::dlopen("liboemcamera.so", RTLD_NOW | RTLD_GLOBAL);
    if (gOemCameraHandle == NULL) {
        const char *error = dlerror();
        LOGE("dlopen(liboemcamera.so) failed: %s", error ? error : "(null)");
    }

    gOpenCameraHardware = reinterpret_cast<OpenCamFunc>(
            ::dlsym(gLibHandle, "HAL_openCameraHardware"));
    gGetCameraInfo = reinterpret_cast<GetCamInfoFunc>(
            ::dlsym(gLibHandle, "HAL_getCameraInfo"));
    gGetNumberOfCameras = reinterpret_cast<GetNumCamerasFunc>(
            ::dlsym(gLibHandle, "HAL_getNumberOfCameras"));

    if (gOpenCameraHardware == NULL || gGetCameraInfo == NULL
            || gGetNumberOfCameras == NULL) {
        LOGE("Failed to resolve camera HAL entry points");
    } else {
        LOGI("Resolved camera HAL entry points from libcamera.y210.so");
        // The blob's startCamera() reads a global targetType set by storeTargetType(),
        // which is called from getCameraInfo(). Android 2.3 CameraService does not
        // guarantee getCameraInfo is called before the first HAL_openCameraHardware,
        // so we prime it here to avoid the "Unable to determine target type" failure.
        struct CameraInfo info;
        gGetCameraInfo(0, &info);
        LOGI("Target type primed via getCameraInfo (facing=%d orientation=%d)",
                info.facing, info.orientation);
    }
}

extern "C" int HAL_getNumberOfCameras()
{
    ensureY210CameraLibOpened();
    return gGetNumberOfCameras ? gGetNumberOfCameras() : 1;
}

extern "C" void HAL_getCameraInfo(int cameraId, struct CameraInfo* cameraInfo)
{
    ensureY210CameraLibOpened();
    if (gGetCameraInfo) {
        gGetCameraInfo(cameraId, cameraInfo);
    } else if (cameraInfo) {
        cameraInfo->facing = CAMERA_FACING_BACK;
        cameraInfo->orientation = 90;
    }
}

extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId)
{
    return Y210CameraWrapper::createInstance(cameraId);
}

sp<CameraHardwareInterface> Y210CameraWrapper::createInstance(int cameraId)
{
    LOGI("createInstance(cameraId=%d) entry lastReleasedWrapper=%p lastReleasedIface=%p lastReleasedPreview=%d lastReleasedFlag=%d",
            cameraId, gLastReleasedWrapper, gLastReleasedIface,
            gLastReleasedPreviewRunning, gLastReleasedReleased);
    if (sSingleton != NULL) {
        sp<CameraHardwareInterface> hardware = sSingleton.promote();
        if (hardware != NULL) {
            sp<Y210CameraWrapper> wrapper =
                    static_cast<Y210CameraWrapper*>(hardware.get());
            LOGI("createInstance singleton promote hardware=%p wrapper=%p usable=%d released=%d preview=%d iface=%p",
                    hardware.get(), wrapper.get(),
                    wrapper != NULL ? wrapper->isUsable() : 0,
                    wrapper != NULL ? wrapper->mReleased : 0,
                    wrapper != NULL ? wrapper->mPreviewRunning : 0,
                    wrapper != NULL ? wrapper->mLibInterface.get() : NULL);
            if (wrapper != NULL && wrapper->isUsable()) {
                LOGI("createInstance returning existing singleton %p", hardware.get());
                return hardware;
            }
            LOGW("createInstance dropping stale singleton %p", hardware.get());
        }
        sSingleton.clear();
    }

    ensureY210CameraLibOpened();
    if (gOpenCameraHardware == NULL) {
        LOGE("createInstance failed: gOpenCameraHardware is NULL");
        return NULL;
    }

    sp<Y210CameraWrapper> hardware(new Y210CameraWrapper(cameraId));
    if (hardware == NULL) {
        return NULL;
    }

    if (!hardware->isUsable()) {
        LOGE("createInstance aborting: delegated camera HAL rejected mode %d",
                getCameraMode());
        return NULL;
    }

    sSingleton = hardware;
    if (!shouldDelegateRelease()) {
        gPinnedCameraHardware = hardware;
        LOGW("createInstance pinning wrapper to avoid release-time refcount crash");
    }
    LOGI("createInstance created wrapper=%p iface=%p reusedLastIface=%d lastCtorIface=%p",
            hardware.get(), hardware->mLibInterface.get(),
            hardware->mLibInterface.get() != NULL
                    && hardware->mLibInterface.get() == gLastReleasedIface,
            gLastCtorIface);
    return hardware;
}

Y210CameraWrapper::Y210CameraWrapper(int cameraId)
    : mCameraId(cameraId),
      mReleased(false),
      mPreviewRunning(false),
      mRecordingRunning(false),
      mNotifyCb(NULL),
      mDataCb(NULL),
      mDataCbTimestamp(NULL),
      mCallbackUser(NULL),
      mHasLastGoodParameters(false)
{
    LOGI("Y210CameraWrapper ctor cameraId=%d", cameraId);
    const int cameraMode = getCameraMode();
    LOGI("Y210CameraWrapper ctor using cameraMode=%d", cameraMode);
    mLibInterface = gOpenCameraHardware
            ? gOpenCameraHardware(cameraId, cameraMode)
            : NULL;
    LOGI("Y210CameraWrapper ctor gOpenCameraHardware returned iface=%p reusedLastCtor=%d reusedLastReleased=%d",
            mLibInterface.get(),
            mLibInterface.get() != NULL && mLibInterface.get() == gLastCtorIface,
            mLibInterface.get() != NULL && mLibInterface.get() == gLastReleasedIface);
    gLastCtorIface = mLibInterface.get();
}

Y210CameraWrapper::~Y210CameraWrapper()
{
    LOGI("Y210WRAP: dtor wrapper=%p released=%d iface=%p preview=%d recording=%d",
            this, mReleased, mLibInterface.get(), mPreviewRunning, mRecordingRunning);
    gLastReleasedWrapper = this;
    gLastReleasedIface = mLibInterface.get();
    gLastReleasedPreviewRunning = mPreviewRunning;
    gLastReleasedReleased = mReleased;
}

void Y210CameraWrapper::notifyCallbackShim(int32_t msgType, int32_t ext1,
        int32_t ext2, void* user)
{
    Y210CameraWrapper* self = static_cast<Y210CameraWrapper*>(user);
    if (self == NULL) return;
    LOGI("Y210WRAP: notify msg=%s(%d) ext1=%d ext2=%d wrapper=%p",
            cameraMsgName(msgType), msgType, ext1, ext2, self);
    if (self->mNotifyCb != NULL) {
        self->mNotifyCb(msgType, ext1, ext2, self->mCallbackUser);
    }
}

void Y210CameraWrapper::dataCallbackShim(int32_t msgType,
        const sp<IMemory>& dataPtr, void* user)
{
    Y210CameraWrapper* self = static_cast<Y210CameraWrapper*>(user);
    if (self == NULL) return;
    ssize_t size = dataPtr != NULL ? dataPtr->size() : -1;
    if (msgType != CAMERA_MSG_PREVIEW_FRAME) {
        LOGI("Y210WRAP: data msg=%s(%d) size=%ld wrapper=%p",
                cameraMsgName(msgType), msgType, static_cast<long>(size), self);
    }
    if (self->mDataCb != NULL) {
        self->mDataCb(msgType, dataPtr, self->mCallbackUser);
    }
}

void Y210CameraWrapper::dataCallbackTimestampShim(nsecs_t timestamp,
        int32_t msgType, const sp<IMemory>& dataPtr, void* user)
{
    Y210CameraWrapper* self = static_cast<Y210CameraWrapper*>(user);
    if (self == NULL) return;
    ssize_t size = dataPtr != NULL ? dataPtr->size() : -1;
    LOGI("Y210WRAP: data-ts msg=%s(%d) ts=%lld size=%ld wrapper=%p",
            cameraMsgName(msgType), msgType, static_cast<long long>(timestamp),
            static_cast<long>(size), self);
    if (self->mDataCbTimestamp != NULL) {
        self->mDataCbTimestamp(timestamp, msgType, dataPtr, self->mCallbackUser);
    }
}

CameraParameters Y210CameraWrapper::seedParameters() const
{
    // Seed a conservative parameter set when the proprietary HAL reports an
    // empty or corrupt parameter blob. The wrapper always returns a fresh
    // framework-owned CameraParameters instance so CameraService never needs
    // to flatten the vendor object's internal String8 storage directly.
    CameraParameters params;
    // Keep the synthetic framework-owned preview defaults aligned with the
    // size that actually produces a visible preview on this legacy blob.
    params.setPreviewSize(640, 480);
    params.setPreviewFrameRate(15);
    params.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);
    params.setPictureSize(640, 480);
    params.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    params.set(CameraParameters::KEY_JPEG_QUALITY, "100");
    params.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "512");
    params.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "384");
    params.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "90");
    params.set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);
    params.set(CameraParameters::KEY_WHITE_BALANCE,
            CameraParameters::WHITE_BALANCE_AUTO);
    params.set(CameraParameters::KEY_ANTIBANDING,
            CameraParameters::ANTIBANDING_AUTO);
    params.set(CameraParameters::KEY_ROTATION, "0");
    // 432x320 is NOT in the blob's internal valid-preview-size list;
    // the blob rejects it with "Invalid preview size requested: 432x320"
    // and falls back to 640x480, causing a buffer-size mismatch in registerBuffers.
    params.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
            "640x480,480x320,352x288,240x160,176x144");
    params.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
            "640x480,512x384,320x240");
    params.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "15");
    params.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
            CameraParameters::PIXEL_FORMAT_JPEG);
    params.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS,
            CameraParameters::PIXEL_FORMAT_YUV420SP);
    params.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE,
            CameraParameters::WHITE_BALANCE_AUTO);
    params.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING,
            CameraParameters::ANTIBANDING_AUTO);
    params.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
            CameraParameters::FOCUS_MODE_AUTO);
    return params;
}

bool Y210CameraWrapper::copyParameterIfPresent(CameraParameters* dst,
        const CameraParameters& src, const char* key) const
{
    if (dst == NULL || key == NULL) {
        return false;
    }

    const char* value = src.get(key);
    if (value == NULL || value[0] == '\0') {
        return false;
    }

    // Copy immediately into wrapper-owned storage. src.get() returns a
    // pointer backed by the vendor CameraParameters object and must not be
    // allowed to escape this frame.
    String8 stable(value);
    dst->set(key, stable.string());
    return true;
}

CameraParameters Y210CameraWrapper::sanitizeParameters(
        const CameraParameters* raw) const
{
    CameraParameters safe = seedParameters();
    if (raw == NULL) {
        return safe;
    }

    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_PREVIEW_SIZE);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES);
    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_PREVIEW_FORMAT);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_PREVIEW_FRAME_RATE);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_PREVIEW_FPS_RANGE);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_PREVIEW_FRAME_RATE_MODE);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATE_MODES);
    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_PICTURE_SIZE);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_SUPPORTED_PICTURE_SIZES);
    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_PICTURE_FORMAT);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_JPEG_QUALITY);
    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_ROTATION);
    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_FOCUS_MODE);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_SUPPORTED_FOCUS_MODES);
    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_WHITE_BALANCE);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_SUPPORTED_WHITE_BALANCE);
    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_ANTIBANDING);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_SUPPORTED_ANTIBANDING);
    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_ZOOM);
    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_MAX_ZOOM);
    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_ZOOM_RATIOS);
    copyParameterIfPresent(&safe, *raw,
            CameraParameters::KEY_ZOOM_SUPPORTED);
    copyParameterIfPresent(&safe, *raw, CameraParameters::KEY_VIDEO_SIZE);

    return safe;
}

CameraParameters Y210CameraWrapper::buildDelegatedParameters(
        const CameraParameters& in) const
{
    // Start from seedParameters() so capability keys (preview-size-values, etc.)
    // are present in the delegated set.  The blob validates KEY_PREVIEW_SIZE against
    // KEY_SUPPORTED_PREVIEW_SIZES in the same setParameters call; without the latter
    // it rejects the entire call before applying the size, leaving the blob's internal
    // preview heap at its previous size and causing registerBuffers ENODEV.
    CameraParameters out(seedParameters());
    static const char* kKeys[] = {
        CameraParameters::KEY_PREVIEW_SIZE,
        CameraParameters::KEY_PICTURE_SIZE,
        CameraParameters::KEY_PREVIEW_FORMAT,
        CameraParameters::KEY_PICTURE_FORMAT,
        CameraParameters::KEY_JPEG_QUALITY,
        CameraParameters::KEY_ROTATION,
        CameraParameters::KEY_WHITE_BALANCE,
        CameraParameters::KEY_FOCUS_MODE,
    };

    for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); ++i) {
        if (!isDelegatableParameterKey(kKeys[i])) {
            continue;
        }
        copyParameterIfPresent(&out, in, kKeys[i]);
    }

    return out;
}

const char* Y210CameraWrapper::findFirstPresentParameterKey(
        const CameraParameters& params,
        const char* const* keys, size_t keyCount) const
{
    for (size_t i = 0; i < keyCount; ++i) {
        const char* key = keys[i];
        const char* value = params.get(key);
        if (value != NULL && value[0] != '\0') {
            return key;
        }
    }
    return NULL;
}

void Y210CameraWrapper::logParameterSummary(const char* prefix,
        const CameraParameters& params) const
{
    int previewW = -1;
    int previewH = -1;
    int pictureW = -1;
    int pictureH = -1;
    params.getPreviewSize(&previewW, &previewH);
    params.getPictureSize(&pictureW, &pictureH);

    const char* previewFormat = params.get(CameraParameters::KEY_PREVIEW_FORMAT);
    const char* pictureFormat = params.get(CameraParameters::KEY_PICTURE_FORMAT);
    const char* focusMode = params.get(CameraParameters::KEY_FOCUS_MODE);

    LOGI("%s preview=%dx%d picture=%dx%d pfmt=%s pictfmt=%s focus=%s",
            prefix ? prefix : "params",
            previewW, previewH, pictureW, pictureH,
            previewFormat ? previewFormat : "<null>",
            pictureFormat ? pictureFormat : "<null>",
            focusMode ? focusMode : "<null>");
}

bool Y210CameraWrapper::isUsable() const
{
    return mLibInterface != NULL && !mReleased;
}

sp<IMemoryHeap> Y210CameraWrapper::getPreviewHeap() const
{
    return isUsable() ? mLibInterface->getPreviewHeap() : NULL;
}

sp<IMemoryHeap> Y210CameraWrapper::getRawHeap() const
{
    return isUsable() ? mLibInterface->getRawHeap() : NULL;
}

void Y210CameraWrapper::setCallbacks(notify_callback notify_cb,
        data_callback data_cb,
        data_callback_timestamp data_cb_timestamp,
        void *user)
{
    if (isUsable()) {
        mNotifyCb = notify_cb;
        mDataCb = data_cb;
        mDataCbTimestamp = data_cb_timestamp;
        mCallbackUser = user;
        LOGI("Y210WRAP: setCallbacks wrapper=%p notify=%p data=%p dataTs=%p user=%p",
                this, notify_cb, data_cb, data_cb_timestamp, user);
        mLibInterface->setCallbacks(
                notify_cb ? notifyCallbackShim : NULL,
                data_cb ? dataCallbackShim : NULL,
                data_cb_timestamp ? dataCallbackTimestampShim : NULL,
                this);
    }
}

void Y210CameraWrapper::enableMsgType(int32_t msgType)
{
    if (isUsable()) mLibInterface->enableMsgType(msgType);
}

void Y210CameraWrapper::disableMsgType(int32_t msgType)
{
    if (isUsable()) mLibInterface->disableMsgType(msgType);
}

bool Y210CameraWrapper::msgTypeEnabled(int32_t msgType)
{
    return isUsable() ? mLibInterface->msgTypeEnabled(msgType) : false;
}

status_t Y210CameraWrapper::startPreview()
{
    if (!isUsable()) {
        LOGE("spdq: wrapper startPreview unusable");
        return INVALID_OPERATION;
    }

    CameraParameters params = getParameters();
    int pw = 0, ph = 0;
    params.getPreviewSize(&pw, &ph);
    const char* pf = params.get(CameraParameters::KEY_PREVIEW_FORMAT);
    LOGI("spdq: wrapper startPreview enter iface=%p preview=%dx%d format=%s",
         mLibInterface.get(), pw, ph, pf ? pf : "(null)");
    LOGI("Y210WRAP: startPreview enter wrapper=%p iface=%p previewRunning=%d recording=%d",
            this, mLibInterface.get(), mPreviewRunning, mRecordingRunning);
    LOGI("startPreview entering iface=%p", mLibInterface.get());
    status_t rc = mLibInterface->startPreview();
    if (rc == NO_ERROR) {
        mPreviewRunning = true;
    }
    LOGI("Y210WRAP: startPreview exit rc=%d previewRunning=%d iface=%p",
            rc, mPreviewRunning, mLibInterface.get());
    LOGI("spdq: wrapper startPreview exit rc=%d previewRunning=%d", rc, mPreviewRunning);
    LOGI("startPreview delegated rc=%d previewRunning=%d", rc, mPreviewRunning);
    return rc;
}

#ifdef USE_GETBUFFERINFO
status_t Y210CameraWrapper::getBufferInfo(sp<IMemory>& frame, size_t *alignedSize)
{
    return isUsable() ? mLibInterface->getBufferInfo(frame, alignedSize) : INVALID_OPERATION;
}
#endif

#ifdef CAF_CAMERA_GB_REL
void Y210CameraWrapper::encodeData()
{
    if (isUsable()) mLibInterface->encodeData();
}
#endif

bool Y210CameraWrapper::useOverlay()
{
    // Default to non-overlay: the proprietary blob has been observed crashing
    // while probing overlay metadata on some lifecycles. Keep an explicit
    // runtime escape hatch for bring-up/testing.
    char value[PROPERTY_VALUE_MAX];
    property_get("debug.camera.use_overlay", value, "0");
    if (strcmp(value, "1") == 0) {
        return true;
    }
    return false;
}

status_t Y210CameraWrapper::setOverlay(const sp<Overlay> &overlay)
{
    if (!isUsable()) {
        return INVALID_OPERATION;
    }

    char value[PROPERTY_VALUE_MAX];
    property_get("debug.camera.use_overlay", value, "0");
    if (strcmp(value, "1") != 0) {
        return INVALID_OPERATION;
    }

    return mLibInterface->setOverlay(overlay);
}

void Y210CameraWrapper::stopPreview()
{
    if (!isUsable()) {
        LOGW("Y210WRAP: stopPreview ignored unusable wrapper=%p released=%d iface=%p",
                this, mReleased, mLibInterface.get());
        return;
    }
    if (!mPreviewRunning) {
        LOGI("Y210WRAP: stopPreview skip already-stopped wrapper=%p iface=%p",
                this, mLibInterface.get());
        return;
    }
    LOGI("Y210WRAP: stopPreview enter wrapper=%p iface=%p previewRunning=%d recording=%d",
            this, mLibInterface.get(), mPreviewRunning, mRecordingRunning);
    mLibInterface->stopPreview();
    mPreviewRunning = false;
    LOGI("Y210WRAP: stopPreview delegated done previewRunning=%d", mPreviewRunning);
    LOGI("stopPreview exit previewRunning=%d", mPreviewRunning);
}

bool Y210CameraWrapper::previewEnabled()
{
    bool enabled = isUsable() && mPreviewRunning;
    return enabled;
}

status_t Y210CameraWrapper::startRecording()
{
    if (!isUsable()) {
        return INVALID_OPERATION;
    }

    status_t rc = mLibInterface->startRecording();
    if (rc == NO_ERROR) {
        mRecordingRunning = true;
    }
    LOGI("startRecording delegated rc=%d recordingRunning=%d",
            rc, mRecordingRunning);
    return rc;
}

void Y210CameraWrapper::stopRecording()
{
    if (!isUsable()) {
        return;
    }
    mLibInterface->stopRecording();
    mRecordingRunning = false;
    LOGI("stopRecording recordingRunning=%d", mRecordingRunning);
}

bool Y210CameraWrapper::recordingEnabled()
{
    bool enabled = isUsable() && mRecordingRunning;
    LOGI("recordingEnabled returning %d", enabled);
    return enabled;
}

void Y210CameraWrapper::releaseRecordingFrame(const sp<IMemory> &mem)
{
    if (isUsable()) mLibInterface->releaseRecordingFrame(mem);
}

status_t Y210CameraWrapper::autoFocus()
{
    return isUsable() ? mLibInterface->autoFocus() : INVALID_OPERATION;
}

status_t Y210CameraWrapper::cancelAutoFocus()
{
    return isUsable() ? mLibInterface->cancelAutoFocus() : INVALID_OPERATION;
}

status_t Y210CameraWrapper::takePicture()
{
    if (!isUsable()) {
        LOGE("Y210WRAP: takePicture ignored unusable wrapper=%p released=%d iface=%p",
                this, mReleased, mLibInterface.get());
        return INVALID_OPERATION;
    }
    logParameterSummary("takePicture cached", getParameters());
    LOGI("Y210WRAP: takePicture enter wrapper=%p iface=%p previewRunning=%d recording=%d notify=%p data=%p dataTs=%p user=%p",
            this, mLibInterface.get(), mPreviewRunning, mRecordingRunning,
            mNotifyCb, mDataCb, mDataCbTimestamp, mCallbackUser);
    if (!shouldDelegateTakePicture()) {
        LOGW("Y210WRAP: takePicture blocked by persist.camera.delegate_takepicture=0");
        return INVALID_OPERATION;
    }
    status_t rc = mLibInterface->takePicture();
    LOGI("Y210WRAP: takePicture exit rc=%d previewRunningBefore=%d",
            rc, mPreviewRunning);
    return rc;
}

status_t Y210CameraWrapper::cancelPicture()
{
    if (!isUsable()) {
        LOGW("Y210WRAP: cancelPicture ignored unusable wrapper=%p released=%d iface=%p",
                this, mReleased, mLibInterface.get());
        return INVALID_OPERATION;
    }
    LOGI("Y210WRAP: cancelPicture enter wrapper=%p iface=%p preview=%d recording=%d",
            this, mLibInterface.get(), mPreviewRunning, mRecordingRunning);
    status_t rc = mLibInterface->cancelPicture();
    LOGI("Y210WRAP: cancelPicture exit rc=%d", rc);
    return rc;
}

status_t Y210CameraWrapper::dump(int fd, const Vector<String16> &args) const
{
    return isUsable() ? mLibInterface->dump(fd, args) : INVALID_OPERATION;
}

status_t Y210CameraWrapper::setParameters(const CameraParameters& params)
{
    if (!isUsable()) {
        return INVALID_OPERATION;
    }
    static const char* kBlockedKeys[] = {
        CameraParameters::KEY_SCENE_MODE,
        CameraParameters::KEY_ANTIBANDING,
        CameraParameters::KEY_FLASH_MODE,
        CameraParameters::KEY_EXPOSURE_COMPENSATION,
        CameraParameters::KEY_WHITE_BALANCE,
        CameraParameters::KEY_PREVIEW_FPS_RANGE,
        "recording-hint",
        "focus-areas",
        "metering-areas",
    };
    CameraParameters patched(params);
    if (patched.get(CameraParameters::KEY_PREVIEW_SIZE) == NULL) {
        patched.setPreviewSize(480, 320);
    }
    if (patched.get(CameraParameters::KEY_PICTURE_SIZE) == NULL) {
        patched.setPictureSize(640, 480);
    }
    if (patched.get(CameraParameters::KEY_PREVIEW_FORMAT) == NULL) {
        patched.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);
    }
    if (patched.get(CameraParameters::KEY_PICTURE_FORMAT) == NULL) {
        patched.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    }
    LOGI("Y210WRAP: setParameters enter wrapper=%p iface=%p preview=%d recording=%d",
            this, mLibInterface.get(), mPreviewRunning, mRecordingRunning);
    logParameterSummary("setParameters incoming", patched);
    LOGI("Y210WRAP: setParameters incoming-flat=%s", patched.flatten().string());
    CameraParameters safe = sanitizeParameters(&patched);
    mLastGoodParameters = safe;
    mHasLastGoodParameters = true;

    if (!shouldDelegateSetParameters()) {
        LOGW("setParameters skipping delegated blob call");
        return NO_ERROR;
    }

    CameraParameters delegated = buildDelegatedParameters(safe);

    // The blob's ISP does not support 432x320 ("Invalid preview size requested: 432x320");
    // it rejects the entire setParameters call and stays at 640x480, causing a buffer-size
    // mismatch when registerBuffers is called with 432x320 surface buffers.
    // Remap 432x320 -> 480x320 (a standard size the blob accepts) both in the delegated
    // params sent to the blob AND in the cache returned by getParameters(), so the camera
    // service allocates matching-size buffers for registerBuffers.
    int delegatedPrevW = 0, delegatedPrevH = 0;
    delegated.getPreviewSize(&delegatedPrevW, &delegatedPrevH);
    bool remappedPreviewSize = false;
    if (delegatedPrevW == 432 && delegatedPrevH == 320) {
        delegated.setPreviewSize(480, 320);
        remappedPreviewSize = true;
        LOGI("Y210WRAP: setParameters remapping preview 432x320->480x320 (blob rejects 432x320)");
    }

    logParameterSummary("setParameters delegate-pre", delegated);
    LOGI("Y210WRAP: setParameters delegate-flat=%s", delegated.flatten().string());
    // The Y210 blob's QualcommCameraHardware vtable has 2 extra virtual methods inserted
    // between cancelAutoFocus (slot 18) and takePicture (slot 21 in blob vs slot 19 in CM7),
    // shifting setParameters from CM7's vtable slot 21 to blob's slot 23.
    // Calling through mLibInterface->setParameters() dispatches to blob's takePicture(sp<ISurface>&)
    // at slot 21 -> SIGSEGV.  We bypass the C++ vtable and call the correct slot directly.
    typedef status_t (*BlobSetParamFn)(void*, const CameraParameters&);
    void** vptr = *reinterpret_cast<void***>(mLibInterface.get());
    BlobSetParamFn blobSetParams = reinterpret_cast<BlobSetParamFn>(vptr[23]);
    LOGI("Y210WRAP: setParameters calling blob slot 23 directly (vtable offset 0x5c)");
    status_t rc = blobSetParams(mLibInterface.get(), delegated);
    LOGI("Y210WRAP: setParameters delegate-post rc=%d previewRunning=%d recordingRunning=%d",
            rc, mPreviewRunning, mRecordingRunning);

    // If we remapped the preview size, update the cache regardless of blob rc so that
    // getParameters() returns the actual size the blob was configured for.  This ensures
    // CameraService allocates matching-size surface buffers for registerBuffers.
    if (remappedPreviewSize) {
        mLastGoodParameters.setPreviewSize(480, 320);
        LOGI("Y210WRAP: setParameters updated cache preview to 480x320 to match blob");
    }

    if (rc != NO_ERROR) {
        const char* suspect = findFirstPresentParameterKey(
                patched, kBlockedKeys, sizeof(kBlockedKeys) / sizeof(kBlockedKeys[0]));
        LOGW("Y210WRAP: setParameters fallback-to-cache rc=%d suspect=%s",
                rc, suspect ? suspect : "<allowlist-or-vendor-key>");
        LOGW("Y210WRAP: setParameters blob rejected request, preserving cached parameters");
        return NO_ERROR;
    }
    return NO_ERROR;
}

CameraParameters Y210CameraWrapper::getParameters() const
{
    if (!isUsable()) {
        LOGW("getParameters called on unusable wrapper");
        if (mHasLastGoodParameters) {
            LOGW("getParameters returning cached parameters from unusable wrapper");
            return mLastGoodParameters;
        }
        return seedParameters();
    }

    if (mHasLastGoodParameters) {
        logParameterSummary("getParameters cached", mLastGoodParameters);
        return mLastGoodParameters;
    }

    // The proprietary Huawei/Qualcomm blob crashes inside its getParameters()
    // path (via cancelPicture()/mutex handling) before the framework can even
    // flatten the returned map. Seed a stable framework-owned parameter set
    // and let subsequent successful setParameters() calls refresh the cache.
    CameraParameters safe = seedParameters();
    mLastGoodParameters = safe;
    mHasLastGoodParameters = true;
    logParameterSummary("getParameters seeded", safe);
    return safe;
}

status_t Y210CameraWrapper::sendCommand(int32_t command, int32_t arg1, int32_t arg2)
{
    if (!isUsable()) {
        return INVALID_OPERATION;
    }

    LOGI("sendCommand command=%d arg1=%d arg2=%d", command, arg1, arg2);
    status_t rc = mLibInterface->sendCommand(command, arg1, arg2);
    LOGI("sendCommand delegated rc=%d for command=%d", rc, command);
    return rc;
}

void Y210CameraWrapper::release()
{
    const bool delegateRelease = shouldDelegateRelease();
    const bool nullIfaceOnRelease = shouldNullIfaceOnRelease();
    if (!isUsable()) {
        LOGW("Y210WRAP: release skip double-release wrapper=%p released=%d iface=%p",
                this, mReleased, mLibInterface.get());
        return;
    }
    LOGI("Y210WRAP: release enter wrapper=%p iface=%p preview=%d recording=%d released=%d",
            this, mLibInterface.get(), mPreviewRunning, mRecordingRunning, mReleased);
    LOGI("Y210WRAP: release mode delegate=%d null_iface=%d pinned=%p singleton=%p",
            delegateRelease, nullIfaceOnRelease, gPinnedCameraHardware.get(),
            sSingleton.promote().get());
    if (mPreviewRunning) {
        stopPreview();
    }
    if (mLibInterface != NULL) {
        LOGI("Y210WRAP: release clear-callbacks enter iface=%p notify=%p data=%p dataTs=%p user=%p",
                mLibInterface.get(), mNotifyCb, mDataCb, mDataCbTimestamp, mCallbackUser);
        mLibInterface->setCallbacks(NULL, NULL, NULL, NULL);
        LOGI("Y210WRAP: release clear-callbacks exit iface=%p", mLibInterface.get());
    }
    mNotifyCb = NULL;
    mDataCb = NULL;
    mDataCbTimestamp = NULL;
    mCallbackUser = NULL;
    if (!delegateRelease) {
        if (nullIfaceOnRelease && mLibInterface != NULL) {
            LOGW("Y210WRAP: release forcing local iface detach iface=%p pinned=%p",
                    mLibInterface.get(), gPinnedCameraHardware.get());
            mLibInterface.clear();
            LOGI("Y210WRAP: release local iface detached iface=%p", mLibInterface.get());
        }
        sSingleton.clear();
        mPreviewRunning = false;
        mRecordingRunning = false;
        mReleased = true;
        gLastReleasedWrapper = this;
        gLastReleasedIface = mLibInterface.get();
        gLastReleasedPreviewRunning = mPreviewRunning;
        gLastReleasedReleased = mReleased;
        LOGW("Y210WRAP: release skipped delegated blob release to avoid mediaserver crash");
        LOGI("Y210WRAP: release exit skipped iface=%p released=%d wrapper=%p pinned=%p",
                mLibInterface.get(), mReleased, this, gPinnedCameraHardware.get());
        return;
    }

    CameraHardwareInterface* libInterface = mLibInterface.get();
    mPreviewRunning = false;
    mRecordingRunning = false;
    mReleased = true;
    LOGI("Y210WRAP: release delegated enter iface=%p", libInterface);
    if (nullIfaceOnRelease) {
        LOGI("Y210WRAP: release delegated path ignoring null_iface request");
    }
    if (libInterface != NULL) {
        LOGI("Y210WRAP: release delegated call iface=%p", libInterface);
        libInterface->release();
        LOGI("Y210WRAP: release delegated return iface=%p", libInterface);
    }
    mLibInterface.clear();
    gPinnedCameraHardware.clear();
    gLastReleasedWrapper = this;
    gLastReleasedIface = mLibInterface.get();
    gLastReleasedPreviewRunning = mPreviewRunning;
    gLastReleasedReleased = mReleased;
    LOGI("Y210WRAP: release exit delegated iface=%p pinned=%p released=%d",
            mLibInterface.get(), gPinnedCameraHardware.get(), mReleased);
}

}; // namespace android
