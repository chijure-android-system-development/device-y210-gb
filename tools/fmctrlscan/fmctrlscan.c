#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void query_range(int fd, unsigned int first, unsigned int last)
{
    struct v4l2_queryctrl qc;
    unsigned int id;

    for (id = first; id <= last; id++) {
        memset(&qc, 0, sizeof(qc));
        qc.id = id;
        if (ioctl(fd, VIDIOC_QUERYCTRL, &qc) == 0) {
            printf("id=0x%08x type=%u flags=0x%08x min=%d max=%d step=%d def=%d name=%s\n",
                   id, qc.type, qc.flags, qc.minimum, qc.maximum, qc.step,
                   qc.default_value, qc.name);
        }
    }
}

int main(void)
{
    int fd = open("/dev/radio0", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open(/dev/radio0) failed: %s (%d)\n", strerror(errno), errno);
        return 1;
    }

    printf("fmctrlscan: scanning /dev/radio0 controls\n");
    printf("range: V4L2_CID_BASE..+0x40 (audio/user)\n");
    query_range(fd, 0x00980900, 0x00980940);

    printf("range: V4L2_CID_PRIVATE_BASE..+0x60 (tavarua private)\n");
    query_range(fd, 0x08000000, 0x08000060);

    close(fd);
    return 0;
}
