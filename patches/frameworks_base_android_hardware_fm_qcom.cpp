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
#include <sys/socket.h>
#include <unistd.h>

#include <cutils/sockets.h>

#include "jni.h"
#include "JNIHelp.h"
#include "android_runtime/AndroidRuntime.h"

#include <utils/Log.h>

namespace {

static const jint FM_JNI_SUCCESS = 0;
static const jint FM_JNI_FAILURE = -1;

// Matches SOCKET_NAME in device/huawei/y210/fminit/fminit.c. fminit keeps
// /dev/radio0 open forever (radio-tavarua re-runs hardware init and wipes
// the loaded firmware on every open()), and hands out dups of its one fd
// over this socket so callers never call open() on the real device node.
static const char* FMINIT_SOCKET_NAME = "fminit_radio0";

// Receive a dup'd fd for /dev/radio0 from fminit over SCM_RIGHTS. Returns
// the fd, or -1 if fminit isn't reachable (not running, wrong device, etc).
//
// fminit is spawned asynchronously by FMRadioService.onCreate() right
// before the FM activity can call fmOn(), so the socket may not exist yet
// (process not forked, or fork()/exec() still in flight) when we get here.
// Retry briefly instead of giving up on the first attempt -- once the
// socket exists, connect() succeeds immediately (it queues in the backlog)
// even if fminit hasn't reached accept() yet, so this only spins during
// the narrow process-startup window, not through the firmware load.
static int acquireFdFromFminit()
{
    int sock = -1;
    // Widened 2026-07-09 from 20x50ms (1s) to 60x100ms (6s): on cold boot,
    // 1s of margin was not always enough for fminit to fork/exec and bind
    // its socket under full boot-time system load, causing the JNI to fall
    // back to a direct open() that then failed with EBUSY against fminit's
    // already-open fd (see docs/FM_NOTES.md, 2026-07-09). A 10s window
    // (100x100ms) confirmed this was purely a margin issue, not a logic
    // bug; 6s keeps a comfortable margin without a worst-case 10s stall on
    // genuine failure.
    for (int attempt = 0; attempt < 60 && sock < 0; attempt++) {
        if (attempt > 0) {
            usleep(100 * 1000);
        }
        sock = socket_local_client(FMINIT_SOCKET_NAME,
                ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
    }
    if (sock < 0) {
        return -1;
    }

    char dummy;
    struct iovec iov;
    iov.iov_base = &dummy;
    iov.iov_len = 1;

    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);

    ssize_t n = recvmsg(sock, &msg, 0);
    close(sock);
    if (n <= 0 || (msg.msg_flags & MSG_CTRUNC)) {
        return -1;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL || cmsg->cmsg_level != SOL_SOCKET ||
            cmsg->cmsg_type != SCM_RIGHTS ||
            cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
        return -1;
    }

    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    return fd;
}

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

    int fd = -1;
    if (strcmp(nativePath, "/dev/radio0") == 0) {
        fd = acquireFdFromFminit();
        if (fd >= 0) {
            LOGI("acquired radio0 fd=%d from fminit", fd);
        } else {
            LOGW("fminit unreachable, falling back to direct open() "
                    "(firmware will be reset by the driver)");
        }
    }

    if (fd < 0) {
        fd = open(nativePath, O_RDWR);
        if (fd < 0) {
            LOGE("open(%s) failed: %s", nativePath, strerror(errno));
        } else {
            LOGI("opened %s fd=%d", nativePath, fd);
        }
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

// Matches enum tavarua_buf_t in the kernel driver's media/tavarua.h
// (SRCH_LIST, EVENTS, RT_RDS, PS_RDS, RAW_RDS, AF_LIST -- EVENTS is index 1).
// Not shipped as a header anywhere in this tree; confirmed against
// kernel-c660-src/drivers/media/radio/radio-tavarua.c and
// kernel-c660-src/include/media/tavarua.h (same tavarua/MSM7627A driver
// family as this device's kernel, which no longer has its own source
// checked out to compare directly -- see docs/FM_NOTES.md, 2026-07-09).
#define TAVARUA_BUF_EVENTS 1

// This is the FM event listener's poll call (FmRxEventListner.java calls
// it in a loop with index==EVENT_LISTEN==1, never anything else). It used
// to do a plain read(fd, ...), which only ever unblocks on RDS data --
// tavarua_fops_read() (the kernel's read() handler) exclusively serves
// the TAVARUA_BUF_RAW_RDS kfifo/wait-queue. General events (TUNE_EVENT,
// STEREO/MONO, SEEK_COMPLETE, etc.) are queued by the driver's
// tavarua_q_event() into a *different* kfifo/wait-queue
// (TAVARUA_BUF_EVENTS/radio->event_queue) that read() never touches --
// the driver only exposes it via VIDIOC_DQBUF (tavarua_vidioc_dqbuf()),
// selecting the buffer via v4l2_buffer.index. Without this, the listener
// thread could only ever unblock on incidental RDS traffic, misreading
// whatever RDS byte happened to arrive as an event code -- any past
// "TUNE_EVENT received" log was likely coincidental RDS noise, not a
// real event (see docs/FM_NOTES.md, 2026-07-09).
static jint fmGetBufferNative(JNIEnv* env, jobject /*thiz*/, jint fd, jbyteArray buff, jint /*index*/)
{
    if (fd < 0 || buff == NULL) return FM_JNI_FAILURE;
    jsize len = env->GetArrayLength(buff);
    if (len <= 0) return 0;
    jbyte* bytes = env->GetByteArrayElements(buff, NULL);
    if (bytes == NULL) return FM_JNI_FAILURE;

    struct v4l2_buffer v4l2buf;
    memset(&v4l2buf, 0, sizeof(v4l2buf));
    // V4L2_BUF_TYPE_PRIVATE: the generic v4l2-ioctl.c dispatcher checks
    // buffer.type against the driver's registered vidioc_g_fmt_* ops
    // before calling vidioc_dqbuf (see check_fmt() in v4l2-ioctl.c) --
    // tavarua only registers vidioc_g_fmt_type_private, so this is the
    // only type value that passes that check for this driver.
    v4l2buf.type = V4L2_BUF_TYPE_PRIVATE;
    v4l2buf.index = TAVARUA_BUF_EVENTS;
    v4l2buf.memory = V4L2_MEMORY_USERPTR;
    v4l2buf.m.userptr = (unsigned long)bytes;
    v4l2buf.length = (uint32_t)len;

    int r;
    if (ioctl(fd, VIDIOC_DQBUF, &v4l2buf) < 0) {
        LOGE("fmGetBufferNative: VIDIOC_DQBUF failed: %s", strerror(errno));
        r = FM_JNI_FAILURE;
    } else {
        r = (jint)v4l2buf.bytesused;
    }

    env->ReleaseByteArrayElements(buff, bytes, 0);
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
    // count comes straight from Java; read()'s 3rd param is size_t, so a
    // negative count would implicitly convert to a huge unsigned value and
    // overflow past the end of buff.
    if (count <= 0) return count == 0 ? 0 : FM_JNI_FAILURE;
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
