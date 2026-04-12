/*
 * Minimal FM radio JNI for Qualcomm "tavarua" style V4L2 radio devices.
 *
 * This registers natives for android.hardware.fmradio.FmReceiverJNI so the
 * framework FM API can open /dev/radio0 and issue basic V4L2 ioctls.
 *
 * Target: legacy Gingerbread/CM7 bring-up (Huawei Y210, msm7x27a).
 */

#define LOG_TAG "fmradio_qcom"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "jni.h"
#include "JNIHelp.h"
#include "android_runtime/AndroidRuntime.h"

#include <utils/Log.h>

namespace {

static const jint FM_JNI_SUCCESS = 0;
static const jint FM_JNI_FAILURE = -1;

static bool tunerUsesLowUnits(int fd)
{
    struct v4l2_tuner tuner;
    memset(&tuner, 0, sizeof(tuner));
    tuner.index = 0;
    if (ioctl(fd, VIDIOC_G_TUNER, &tuner) < 0) {
        // Default to "LOW" units; most radio tuners expose this.
        return true;
    }
    return (tuner.capability & V4L2_TUNER_CAP_LOW) != 0;
}

static uint32_t khzToV4l2(jint freqKhz, bool lowUnits)
{
    if (freqKhz <= 0) {
        return 0;
    }
    // V4L2: if V4L2_TUNER_CAP_LOW is set, frequency units are 62.5 Hz.
    // kHz -> (kHz*1000)/62.5 == kHz*16
    if (lowUnits) {
        return static_cast<uint32_t>(freqKhz) * 16;
    }
    return static_cast<uint32_t>(freqKhz);
}

static jint v4l2ToKhz(uint32_t freq, bool lowUnits)
{
    if (lowUnits) {
        // 62.5 Hz units -> kHz: units/16
        return static_cast<jint>(freq / 16);
    }
    return static_cast<jint>(freq);
}

static jint fmAcquireFdNative(JNIEnv* env, jobject /*thiz*/, jstring path)
{
    if (path == NULL) {
        return FM_JNI_FAILURE;
    }
    const char* nativePath = env->GetStringUTFChars(path, NULL);
    if (nativePath == NULL) {
        return FM_JNI_FAILURE;
    }
    int fd = open(nativePath, O_RDWR);
    if (fd < 0) {
        LOGE("open(%s) failed: %s", nativePath, strerror(errno));
    } else {
        LOGI("opened %s fd=%d", nativePath, fd);
    }
    env->ReleaseStringUTFChars(path, nativePath);
    return fd;
}

static jint fmCloseFdNative(JNIEnv* /*env*/, jobject /*thiz*/, jint fd)
{
    if (fd < 0) return FM_JNI_FAILURE;
    return close(fd);
}

static jint fmGetFreqNative(JNIEnv* /*env*/, jobject /*thiz*/, jint fd)
{
    if (fd < 0) return FM_JNI_FAILURE;
    struct v4l2_frequency f;
    memset(&f, 0, sizeof(f));
    f.tuner = 0;
    f.type = V4L2_TUNER_RADIO;
    if (ioctl(fd, VIDIOC_G_FREQUENCY, &f) < 0) {
        LOGE("VIDIOC_G_FREQUENCY failed: %s", strerror(errno));
        return FM_JNI_FAILURE;
    }
    return v4l2ToKhz(f.frequency, tunerUsesLowUnits(fd));
}

static jint fmSetFreqNative(JNIEnv* /*env*/, jobject /*thiz*/, jint fd, jint freqKhz)
{
    if (fd < 0) return FM_JNI_FAILURE;
    struct v4l2_frequency f;
    memset(&f, 0, sizeof(f));
    f.tuner = 0;
    f.type = V4L2_TUNER_RADIO;
    f.frequency = khzToV4l2(freqKhz, tunerUsesLowUnits(fd));
    if (ioctl(fd, VIDIOC_S_FREQUENCY, &f) < 0) {
        LOGE("VIDIOC_S_FREQUENCY(%d kHz -> %u) failed: %s",
                freqKhz, f.frequency, strerror(errno));
        return FM_JNI_FAILURE;
    }
    return FM_JNI_SUCCESS;
}

static jint fmGetControlNative(JNIEnv* /*env*/, jobject /*thiz*/, jint fd, jint id)
{
    if (fd < 0) return FM_JNI_FAILURE;
    struct v4l2_control control;
    memset(&control, 0, sizeof(control));
    control.id = static_cast<uint32_t>(id);
    if (ioctl(fd, VIDIOC_G_CTRL, &control) < 0) {
        return FM_JNI_FAILURE;
    }
    return control.value;
}

static jint fmSetControlNative(JNIEnv* /*env*/, jobject /*thiz*/, jint fd, jint id, jint value)
{
    if (fd < 0) return FM_JNI_FAILURE;
    struct v4l2_control control;
    memset(&control, 0, sizeof(control));
    control.id = static_cast<uint32_t>(id);
    control.value = value;
    if (ioctl(fd, VIDIOC_S_CTRL, &control) < 0) {
        LOGW("VIDIOC_S_CTRL id=0x%x value=%d failed: %s",
                control.id, value, strerror(errno));
        return FM_JNI_FAILURE;
    }
    return FM_JNI_SUCCESS;
}

static jint fmStartSearchNative(JNIEnv* /*env*/, jobject /*thiz*/, jint fd, jint dir)
{
    // Java configures search parameters via setControlNative(). Here we just
    // try to kick off search using the private SRCHON control commonly used
    // by tavarua stacks (V4L2_CID_PRIVATE_BASE + 3).
    const jint V4L2_CID_PRIVATE_TAVARUA_SRCHON = V4L2_CID_PRIVATE_BASE + 3;
    if (fmSetControlNative(NULL, NULL, fd, V4L2_CID_PRIVATE_TAVARUA_SRCHON, dir ? 1 : 0) == FM_JNI_SUCCESS) {
        return FM_JNI_SUCCESS;
    }
    // Fallback: some implementations expect 1/2 instead of 0/1.
    return fmSetControlNative(NULL, NULL, fd, V4L2_CID_PRIVATE_TAVARUA_SRCHON, dir ? 1 : 2);
}

static jint fmCancelSearchNative(JNIEnv* /*env*/, jobject /*thiz*/, jint fd)
{
    const jint V4L2_CID_PRIVATE_TAVARUA_SRCHON = V4L2_CID_PRIVATE_BASE + 3;
    return fmSetControlNative(NULL, NULL, fd, V4L2_CID_PRIVATE_TAVARUA_SRCHON, 0);
}

static jint fmGetRSSINative(JNIEnv* /*env*/, jobject /*thiz*/, jint /*fd*/)
{
    // Optional; keep framework running even if not supported.
    return 0;
}

static jint fmAudioControlNative(JNIEnv* /*env*/, jobject /*thiz*/, jint fd, jint control, jint field)
{
    // Treat 'control' as a V4L2 control id and 'field' as the value.
    // Commonly used for V4L2_CID_AUDIO_MUTE.
    return fmSetControlNative(NULL, NULL, fd, control, field);
}

static jint fmSetBandNative(JNIEnv* /*env*/, jobject /*thiz*/, jint /*fd*/, jint /*low*/, jint /*high*/)
{
    return FM_JNI_SUCCESS;
}

static jint fmGetLowerBandNative(JNIEnv* /*env*/, jobject /*thiz*/, jint /*fd*/)
{
    // Default to 87.5 MHz.
    return 87500;
}

static jint fmGetBufferNative(JNIEnv* env, jobject /*thiz*/, jint fd, jbyteArray buff, jint /*index*/)
{
    if (fd < 0 || buff == NULL) return FM_JNI_FAILURE;
    jsize len = env->GetArrayLength(buff);
    if (len <= 0) return 0;
    jbyte* bytes = env->GetByteArrayElements(buff, NULL);
    if (bytes == NULL) return FM_JNI_FAILURE;
    int r = read(fd, bytes, len);
    env->ReleaseByteArrayElements(buff, bytes, 0);
    if (r < 0) return FM_JNI_FAILURE;
    return r;
}

static jint fmSetMonoStereoNative(JNIEnv* /*env*/, jobject /*thiz*/, jint /*fd*/, jint /*val*/)
{
    return FM_JNI_SUCCESS;
}

static jint fmGetRawRdsNative(JNIEnv* env, jobject /*thiz*/, jint fd, jbyteArray buff, jint count)
{
    if (fd < 0 || buff == NULL) return FM_JNI_FAILURE;
    jsize len = env->GetArrayLength(buff);
    if (len <= 0) return 0;
    if (count > len) count = len;
    jbyte* bytes = env->GetByteArrayElements(buff, NULL);
    if (bytes == NULL) return FM_JNI_FAILURE;
    int r = read(fd, bytes, count);
    env->ReleaseByteArrayElements(buff, bytes, 0);
    if (r < 0) return FM_JNI_FAILURE;
    return r;
}

static const JNINativeMethod gMethods[] = {
    { "acquireFdNative", "(Ljava/lang/String;)I", (void*)fmAcquireFdNative },
    { "audioControlNative", "(III)I", (void*)fmAudioControlNative },
    { "closeFdNative", "(I)I", (void*)fmCloseFdNative },
    { "getFreqNative", "(I)I", (void*)fmGetFreqNative },
    { "setFreqNative", "(II)I", (void*)fmSetFreqNative },
    { "getControlNative", "(II)I", (void*)fmGetControlNative },
    { "setControlNative", "(III)I", (void*)fmSetControlNative },
    { "startSearchNative", "(II)I", (void*)fmStartSearchNative },
    { "cancelSearchNative", "(I)I", (void*)fmCancelSearchNative },
    { "getRSSINative", "(I)I", (void*)fmGetRSSINative },
    { "setBandNative", "(III)I", (void*)fmSetBandNative },
    { "getLowerBandNative", "(I)I", (void*)fmGetLowerBandNative },
    { "getBufferNative", "(I[BI)I", (void*)fmGetBufferNative },
    { "setMonoStereoNative", "(II)I", (void*)fmSetMonoStereoNative },
    { "getRawRdsNative", "(I[BI)I", (void*)fmGetRawRdsNative },
};

} // namespace

int register_android_hardware_fm_fmradio(JNIEnv* env)
{
    return jniRegisterNativeMethods(env,
            "android/hardware/fmradio/FmReceiverJNI",
            gMethods, NELEM(gMethods));
}
