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

static void ensureY210CameraLibOpened();

// The stock Huawei/Qualcomm blob appears to read a second "camera mode"
// argument from HAL_openCameraHardware(). When we call it as a one-arg
// function, r1 contains garbage and the blob logs a random mode before
// failing startCamera(). Make the mode selectable at runtime so we can
// probe which Huawei/Qualcomm value this blob expects.
static const int kDefaultCameraMode = 4;

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
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.camera.skip_release", value, "1");
    return strcmp(value, "1") != 0;
}

static bool shouldDelegateSetParameters()
{
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.camera.delegate_setparams", value, "0");
    return strcmp(value, "0") != 0;
}

wp<CameraHardwareInterface> Y210CameraWrapper::sSingleton;
static sp<CameraHardwareInterface> gPinnedCameraHardware;

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
    LOGI("createInstance(cameraId=%d) entry", cameraId);
    if (sSingleton != NULL) {
        sp<CameraHardwareInterface> hardware = sSingleton.promote();
        if (hardware != NULL) {
            sp<Y210CameraWrapper> wrapper =
                    static_cast<Y210CameraWrapper*>(hardware.get());
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
    LOGI("createInstance created wrapper %p", hardware.get());
    return hardware;
}

Y210CameraWrapper::Y210CameraWrapper(int cameraId)
    : mCameraId(cameraId),
      mReleased(false),
      mPreviewRunning(false),
      mRecordingRunning(false),
      mHasLastGoodParameters(false)
{
    LOGI("Y210CameraWrapper ctor cameraId=%d", cameraId);
    const int cameraMode = getCameraMode();
    LOGI("Y210CameraWrapper ctor using cameraMode=%d", cameraMode);
    mLibInterface = gOpenCameraHardware
            ? gOpenCameraHardware(cameraId, cameraMode)
            : NULL;
    LOGI("Y210CameraWrapper ctor gOpenCameraHardware returned %p", mLibInterface.get());
}

Y210CameraWrapper::~Y210CameraWrapper()
{
}

CameraParameters Y210CameraWrapper::seedParameters() const
{
    // Seed a conservative parameter set when the proprietary HAL reports an
    // empty or corrupt parameter blob. The wrapper always returns a fresh
    // framework-owned CameraParameters instance so CameraService never needs
    // to flatten the vendor object's internal String8 storage directly.
    CameraParameters params;
    params.setPreviewSize(480, 320);
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
    params.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
            "480x320,432x320,352x288,320x240,240x160,176x144");
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
        mLibInterface->setCallbacks(notify_cb, data_cb, data_cb_timestamp, user);
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
    LOGI("startPreview entering iface=%p", mLibInterface.get());
    status_t rc = mLibInterface->startPreview();
    if (rc == NO_ERROR) {
        mPreviewRunning = true;
    }
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
    // The proprietary blob crashes while probing overlay buffer metadata via
    // getBufferInfo() during CameraService client construction. Force the
    // non-overlay preview path on CM7.
    return false;
}

status_t Y210CameraWrapper::setOverlay(const sp<Overlay> &overlay)
{
    return INVALID_OPERATION;
}

void Y210CameraWrapper::stopPreview()
{
    if (!isUsable()) {
        return;
    }
    mLibInterface->stopPreview();
    mPreviewRunning = false;
    LOGI("stopPreview previewRunning=%d", mPreviewRunning);
}

bool Y210CameraWrapper::previewEnabled()
{
    bool enabled = isUsable() && mPreviewRunning;
    LOGI("previewEnabled returning %d", enabled);
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
    return isUsable() ? mLibInterface->takePicture() : INVALID_OPERATION;
}

status_t Y210CameraWrapper::cancelPicture()
{
    return isUsable() ? mLibInterface->cancelPicture() : INVALID_OPERATION;
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
    logParameterSummary("setParameters request", patched);
    mLastGoodParameters = sanitizeParameters(&patched);
    mHasLastGoodParameters = true;

    if (!shouldDelegateSetParameters()) {
        LOGW("setParameters skipping delegated blob call");
        return NO_ERROR;
    }

    status_t rc = mLibInterface->setParameters(patched);
    LOGI("setParameters delegated rc=%d", rc);
    return rc;
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
    if (!isUsable()) {
        LOGW("release ignored: wrapper already unusable");
        return;
    }
    LOGI("release entering for wrapper %p iface %p", this, mLibInterface.get());
    if (!shouldDelegateRelease()) {
        sSingleton.clear();
        mPreviewRunning = false;
        mRecordingRunning = false;
        mReleased = true;
        LOGW("release skipped delegated blob release to avoid mediaserver crash");
        return;
    }

    CameraHardwareInterface* libInterface = mLibInterface.get();
    mPreviewRunning = false;
    mRecordingRunning = false;
    mReleased = true;
    if (libInterface != NULL) {
        libInterface->release();
    }
    mLibInterface.clear();
    gPinnedCameraHardware.clear();
    LOGI("release completed");
}

}; // namespace android
