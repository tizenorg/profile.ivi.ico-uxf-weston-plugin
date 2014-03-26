/*
 * Copyright © 2014 TOYOTA MOTOR CORPORATION.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/**
 * @brief   Create pseudo input device for Multi Input Manager
 *
 * @date    Feb-21-2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <errno.h>

#include "ico_input_mgr.h"
#include "ico_input_mgr-client-protocol.h"

                                            /* create pseudo input device       */
static int ico_create_pseudo_device(const char *device, int type);

static int  uifd_pointer = -1;              /* file descriptor of pointer       */
static int  uifd_touch = -1;                /* file descriptor of touch-panel   */
static int  uifd_keyboard = -1;             /* file descriptor of keyboard      */
static int  mWidth = 1920;                  /* screen width                     */
static int  mHeight = 1080;                 /* screen height                    */
static int  mRun = 0;                       /* running flag                     */

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_create_pseudo_device: create pseudo input device
 *
 * @param[in]   device          event device name
 * @param[in]   type            device type
 *                              (ICO_INPUT_MGR_DEVICE_TYPE_POINTER/KEYBOARD/TOUCH)
 * @return      uinput device file descriptor
 * @retval      >= 0            file descriptor
 * @retval      -1              error
 */
/*--------------------------------------------------------------------------*/
static int
ico_create_pseudo_device(const char *device, int type)
{
    int     fd;
    int     key;
    char    devFile[64];
    struct uinput_user_dev  uinputDevice;

    /* check if exist           */
    for (key = 0; key < 32; key++)  {
        snprintf(devFile, sizeof(devFile)-1, "/dev/input/event%d", key);
        fd = open(devFile, O_RDONLY);
        if (fd < 0)     continue;

        memset(uinputDevice.name, 0, sizeof(uinputDevice.name));
        ioctl(fd, EVIOCGNAME(sizeof(uinputDevice.name)), uinputDevice.name);
        close(fd);
        if (strcmp(uinputDevice.name, device) == 0) {
            printf("ico_create_pseudo_device: <%s> already exist.\n", device);
            fflush(stdout);
            return -1;
        }
    }

    fd = open("/dev/uinput", O_RDWR);
    if (fd < 0)   {
        printf("ico_create_pseudo_device: <%s> uinput device open Error<%d>\n",
               device, errno);
        fflush(stdout);
        return -1;
    }
    memset(&uinputDevice, 0, sizeof(uinputDevice));
    strncpy(uinputDevice.name, device, UINPUT_MAX_NAME_SIZE-1);
    uinputDevice.absmax[ABS_X] = mWidth;
    uinputDevice.absmax[ABS_Y] = mHeight;

    /* uinput device configuration  */
    if (write(fd, &uinputDevice, sizeof(uinputDevice)) < (int)sizeof(uinputDevice))   {
        printf("ico_create_pseudo_device: <%s> uinput device regist Error<%d>\n",
               device, errno);
        fflush(stdout);
        close(fd);
        return -1;
    }

    /* uinput set event bits        */
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    if (type != ICO_INPUT_MGR_DEVICE_TYPE_KEYBOARD) {
        ioctl(fd, UI_SET_EVBIT, EV_ABS);
        ioctl(fd, UI_SET_ABSBIT, ABS_X);
        ioctl(fd, UI_SET_ABSBIT, ABS_Y);
        if (type == ICO_INPUT_MGR_DEVICE_TYPE_POINTER)  {
            ioctl(fd, UI_SET_EVBIT, EV_REL);
            ioctl(fd, UI_SET_ABSBIT, REL_X);
            ioctl(fd, UI_SET_ABSBIT, REL_Y);
            ioctl(fd, UI_SET_EVBIT, EV_KEY);
            ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
            ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
            ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);
        }
        else    {
            ioctl(fd, UI_SET_EVBIT, EV_KEY);
            ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
            ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_PEN);
        }
    }
    else    {
        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        for (key = 0; key < 255; key++)  {
            ioctl(fd, UI_SET_KEYBIT, key);
        }
        ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
        ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
        ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);
        ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    }
    ioctl(fd, UI_SET_EVBIT, EV_MSC);
    ioctl(fd, UI_SET_MSCBIT, MSC_SCAN);

    if (ioctl(fd, UI_DEV_CREATE, NULL) < 0)   {
        printf("ico_create_pseudo_device: <%s> uinput device create Error<%d>\n",
               device, errno);
        fflush(stdout);
        close(fd);
        return -1;
    }
    return fd;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   signal_int: signal handler
 *
 * @param[in]   signum      signal number(unused)
 * @return      nothing
 */
/*--------------------------------------------------------------------------*/
static void
signal_int(int signum)
{
    printf("ico_pseudo_input_device: terminate signal(%d)\n", signum);
    fflush(stdout);
    mRun = 0;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   main: main routine of ico_create_pseudo_device
 *
 * @param[in]   argc            number of arguments
 * @param[in]   argv            rgument list
 * @return      result
 * @retval      0               success
 * @retval      1               error
 */
/*--------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    int     i;

    printf("ico_pseudo_input_device: Start\n");
    fflush(stdout);

    /* get screen width/height          */
    for (i = 1; i < argc; i++)  {
        if (strncasecmp(argv[i], "-w=", 3) == 0)    {
            mWidth = (int)strtol(&argv[i][3], (char **)0, 0);
            if (mWidth <= 0)    mWidth = 1920;
        }
        else if (strncasecmp(argv[i], "-h=", 3) == 0)   {
            mHeight = (int)strtol(&argv[i][3], (char **)0, 0);
            if (mHeight <= 0)   mHeight = 1080;
        }
        else    {
            printf("usage: ico_pseudo_input_device [-w=width] [-h=height]\n");
            fflush(stdout);
            return 1;
        }
    }

#if 0           /* A daemon cannot do in starting by systemd.   */
    /* change to daemon                 */
    if (daemon(0, 1) < 0) {
        printf("ico_pseudo_input_device: can not create daemon\n");
        fflush(stdout);
        return 1;
    }
#endif

    mRun = 1;
    signal(SIGINT, &signal_int);
    signal(SIGTERM, &signal_int);
    signal(SIGHUP, &signal_int);

    /* create pointer(mouse) device     */
    uifd_pointer = ico_create_pseudo_device(ICO_PSEUDO_INPUT_POINTER,
                                            ICO_INPUT_MGR_DEVICE_TYPE_POINTER);
    /* create touch-panel device        */
    uifd_touch = ico_create_pseudo_device(ICO_PSEUDO_INPUT_TOUCH,
                                          ICO_INPUT_MGR_DEVICE_TYPE_TOUCH);
    /* create keyboard device           */
    uifd_keyboard = ico_create_pseudo_device(ICO_PSEUDO_INPUT_KEY,
                                             ICO_INPUT_MGR_DEVICE_TYPE_KEYBOARD);

    if ((uifd_pointer < 0) && (uifd_touch < 0) && (uifd_keyboard < 0))  {
        printf("ico_pseudo_input_device: can not create pseude input devices\n");
        fflush(stdout);
        return 1;
    }

    while (mRun)    {
        sleep(3600);
    }

    printf("ico_pseudo_input_device: Exit\n");
    fflush(stdout);
    return 0;
}
