/*
 * fminit — load WCN2243 firmware and hand the live /dev/radio0 fd to the FM
 * app instead of closing it.
 *
 * Background: radio-tavarua's tavarua_fops_open() unconditionally re-runs
 * the CTL0/CTL1 hardware init sequence on every open(), which wipes any
 * firmware already loaded over I2C. The old design (open -> load firmware
 * -> close, then let the FM app open() again) hit that path on the app's
 * open and always lost the firmware, leaving the chip in ROM-firmware mode
 * (XFR=98 heartbeat, no real demodulation, no audio).
 *
 * Fix: never let the fd go to zero references. This process opens
 * /dev/radio0 ONCE, loads firmware, and keeps the fd open for as long as
 * the phone is on. The FM app gets a *duplicate* of that same fd (same
 * underlying struct file) over a local socket via SCM_RIGHTS, so the
 * kernel driver's .open() is only ever invoked this one time — passing an
 * fd across processes does not call .open() again.
 *
 * Sequence:
 *   1. bind an abstract-namespace socket "fminit_radio0" (fails ->
 *      another instance is already serving; exit quietly).
 *   2. open /dev/radio0 O_RDWR (brings chip out of reset).
 *   3. sleep 1 s (XFR=98 interrupt fires and settles).
 *   4. exec fm_qsoc_patches (writes firmware over I2C).
 *   5. setprop hw.fm.init=1 on success (best-effort signal, kept for
 *      compatibility with anything still reading it).
 *   6. loop forever: accept a connection, send the radio0 fd via
 *      SCM_RIGHTS, close the connection, go back to accept().
 *
 * Installed setuid-root (android_filesystem_config.h) — needed for
 * fm_qsoc_patches' I2C access, not for opening /dev/radio0 itself.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/system_properties.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cutils/properties.h>
#include <cutils/sockets.h>

#define SOCKET_NAME "fminit_radio0"

static int load_firmware(const char *version)
{
    property_set("hw.fm.init", "0");

    char * const argv[] = {
        (char *)"/system/bin/fm_qsoc_patches",
        (char *)version,
        (char *)"0",
        NULL
    };

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execv("/system/bin/fm_qsoc_patches", argv);
        _exit(1);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        property_set("hw.fm.init", "1");
        return 0;
    }
    return -1;
}

static void send_fd(int conn, int fd)
{
    struct msghdr msg;
    struct iovec iov;
    char dummy = 'F';
    char cmsgbuf[CMSG_SPACE(sizeof(int))];

    memset(&msg, 0, sizeof(msg));
    iov.iov_base = &dummy;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    sendmsg(conn, &msg, 0);
}

int main(void)
{
    char version[PROP_VALUE_MAX];
    property_get("hw.fm.version", version, "");
    if (version[0] == '\0') {
        return 1;
    }

    int srv = socket_local_server(SOCKET_NAME,
            ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
    if (srv < 0) {
        /* Another fminit instance is already serving radio0. Nothing to
         * do — FMRadioService re-execs us on every onCreate(). */
        return 0;
    }
    fcntl(srv, F_SETFD, FD_CLOEXEC);

    int radiofd = open("/dev/radio0", O_RDWR);
    if (radiofd < 0) {
        close(srv);
        return 1;
    }
    fcntl(radiofd, F_SETFD, FD_CLOEXEC);

    sleep(1);

    load_firmware(version);
    /* Serve the fd regardless of firmware-load success: the JNI side
     * falls back to a direct open() if it can't reach us at all, and a
     * device with ROM-default firmware still behaves better than one
     * this process refuses to hand out. */

    for (;;) {
        int conn = accept(srv, NULL, NULL);
        if (conn < 0) {
            if (errno == EINTR) continue;
            break;
        }
        send_fd(conn, radiofd);
        close(conn);
    }

    close(radiofd);
    close(srv);
    return 0;
}
