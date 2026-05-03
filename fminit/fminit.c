/*
 * fminit — load WCN2243 firmware BEFORE the FM app opens /dev/radio0.
 *
 * Called from fmAcquireFdNative BEFORE open(radio0).  At that point nobody
 * else has the device open, so this process can open it, power the chip,
 * run fm_qsoc_patches, then close it.  The app's subsequent open finds a
 * chip that already has firmware and can complete TUNE_DONE on first tune.
 *
 * Sequence:
 *   1. open /dev/radio0  (brings chip out of reset)
 *   2. sleep 1 s          (XFR=98 interrupt fires and settles)
 *   3. exec fm_qsoc_patches  (writes firmware over I2C)
 *   4. close /dev/radio0
 *   5. setprop hw.fm.init=1 on success
 *
 * Installed setuid-root (android_filesystem_config.h).
 */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cutils/properties.h>

int main(void)
{
    char version[PROP_VALUE_MAX];
    property_get("hw.fm.version", version, "");
    if (version[0] == '\0') {
        return 1;
    }

    property_set("hw.fm.init", "0");

    int radiofd = open("/dev/radio0", O_RDONLY);
    if (radiofd < 0) {
        /* Device already in use — firmware cannot be loaded this way. */
        return 1;
    }

    sleep(1);

    char * const argv[] = {
        (char *)"/system/bin/fm_qsoc_patches",
        version,
        (char *)"0",
        NULL
    };

    pid_t pid = fork();
    if (pid < 0) {
        close(radiofd);
        return 1;
    }
    if (pid == 0) {
        execv("/system/bin/fm_qsoc_patches", argv);
        _exit(1);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    close(radiofd);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        property_set("hw.fm.init", "1");
        return 0;
    }
    return 1;
}
