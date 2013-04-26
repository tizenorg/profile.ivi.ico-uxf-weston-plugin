/*
 * Copyright © 2013 TOYOTA MOTOR CORPORATION.
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
 * @brief   System Test Tool for send device input event
 *
 * @date    Feb-20-2013
 */

#include    <stdio.h>
#include    <stdlib.h>
#include    <unistd.h>
#include    <string.h>
#include    <errno.h>
#include    <pthread.h>
#include    <sys/ioctl.h>
#include    <sys/ipc.h>
#include    <sys/msg.h>
#include    <sys/time.h>
#include    <sys/types.h>
#include    <sys/stat.h>
#include    <signal.h>
#include    <fcntl.h>
#include    <linux/input.h>
#include    <linux/uinput.h>
#include    <linux/joystick.h>
#include    "test-common.h"

#define DEV_TOUCH   0
#define DEV_JS      1
#define SPECIALTYPE_XY  9991

static const struct {
    char    *prop;
    short   devtype;
    short   type;
    short   code;
    short   value;
}               event_key[] = {
    { "X", DEV_TOUCH, EV_ABS, ABS_X, -1 },
    { "Y", DEV_TOUCH, EV_ABS, ABS_Y, -1 },
    { "Down", DEV_TOUCH, EV_KEY, BTN_TOUCH, 1 },
    { "Up", DEV_TOUCH, EV_KEY, BTN_TOUCH, 0 },
    { "Touch", DEV_TOUCH, EV_KEY, BTN_TOUCH, -1 },
    { "XY", DEV_TOUCH, SPECIALTYPE_XY, 0, -1 },
    { "SYN", DEV_TOUCH, 0, 0, 0 },
    { "Button", DEV_TOUCH, EV_KEY, BTN_LEFT, -1 },
    { "ButtonOn", DEV_TOUCH, EV_KEY, BTN_LEFT, 1 },
    { "ButtonOff", DEV_TOUCH, EV_KEY, BTN_LEFT, 0 },

    { "UpDown", DEV_JS, 2, 3, 1 },
    { "UD", DEV_JS, 2, 3, 1 },
    { "LeftRight", DEV_JS, 2, 2, 2 },
    { "LR", DEV_JS, 2, 2, 2 },
    { "Cross", DEV_JS, 1, 0, 3 },
    { "Squere", DEV_JS, 1, 1, 4 },
    { "Circle", DEV_JS, 1, 2, 5 },
    { "Triangle", DEV_JS, 1, 3, 6 },
    { "\0", 0, 0, 0, 0 } };

static int  uifd = -1;
static int  mqid = -1;
static int  mDebug = 0;
static int  mRun = 1;
static int  mTouch = 1;

static void
term_signal(const int signo)
{
    mRun = 0;
}

static void
init_mq(const int mqkey)
{
    char    dummy[256];

    if (mqkey == 0) {
        mqid = -1;
    }
    else    {
        mqid = msgget(mqkey, 0);
        if (mqid < 0)   {
            mqid = msgget(mqkey, IPC_CREAT);
        }
        if (mqid < 0)   {
            print_log("Can not create message queue(%d(0x%x))[%d]",
                      mqkey, mqkey, errno);
            fflush(stderr);
            return;
        }
        while (msgrcv(mqid, dummy, sizeof(dummy)-sizeof(long), 0, IPC_NOWAIT) > 0)  ;
    }
}

static void
init_device(const char *device)
{
    int     fd;
    int     ii;
    char    devFile[64];
    char    devName[64];
    struct uinput_user_dev  uinputDevice;
    uifd = open("/dev/uinput", O_RDWR);

    if (uifd < 0)   {
        print_log("/dev/uinput open error[%d]", errno);
        fflush(stderr);
        exit(1);
    }

    memset(&uinputDevice, 0, sizeof(uinputDevice));
    strcpy(uinputDevice.name, device);
    uinputDevice.absmax[ABS_X] = 1920;
    uinputDevice.absmax[ABS_Y] = 1080;

    /* uinput device configuration  */
    if (write(uifd, &uinputDevice, sizeof(uinputDevice)) < (int)sizeof(uinputDevice)) {
        print_log("/dev/uinput regist error[%d]", errno);
        fflush(stderr);
        close(uifd);
        exit(1);
    }

    /* uinput set event bits        */
    ioctl(uifd, UI_SET_EVBIT, EV_SYN);

    if ((mTouch != 0) && (mTouch != 3)) {
        ioctl(uifd, UI_SET_EVBIT, EV_ABS);
        ioctl(uifd, UI_SET_ABSBIT, ABS_X);
        ioctl(uifd, UI_SET_ABSBIT, ABS_Y);
        ioctl(uifd, UI_SET_EVBIT, EV_KEY);
        if (mTouch == 1)    {
            ioctl(uifd, UI_SET_KEYBIT, BTN_LEFT);
        }
        else    {
            ioctl(uifd, UI_SET_KEYBIT, BTN_TOUCH);
            ioctl(uifd, UI_SET_KEYBIT, BTN_TOOL_PEN);
        }
    }
    else    {
        ioctl(uifd, UI_SET_EVBIT, EV_REL);
        ioctl(uifd, UI_SET_RELBIT, REL_X);
        ioctl(uifd, UI_SET_RELBIT, REL_Y);
        ioctl(uifd, UI_SET_RELBIT, REL_Z);
        ioctl(uifd, UI_SET_RELBIT, REL_RX);
        ioctl(uifd, UI_SET_RELBIT, REL_RY);
        ioctl(uifd, UI_SET_RELBIT, REL_RZ);
        ioctl(uifd, UI_SET_EVBIT, EV_KEY);
        ioctl(uifd, UI_SET_KEYBIT, KEY_RESERVED);
        ioctl(uifd, UI_SET_KEYBIT, KEY_ESC);
        ioctl(uifd, UI_SET_KEYBIT, KEY_1);
        ioctl(uifd, UI_SET_KEYBIT, KEY_2);
        ioctl(uifd, UI_SET_KEYBIT, KEY_3);
        ioctl(uifd, UI_SET_KEYBIT, KEY_4);
        ioctl(uifd, UI_SET_KEYBIT, KEY_5);
        ioctl(uifd, UI_SET_KEYBIT, KEY_6);
        ioctl(uifd, UI_SET_KEYBIT, KEY_7);
        ioctl(uifd, UI_SET_KEYBIT, KEY_8);
        ioctl(uifd, UI_SET_KEYBIT, KEY_9);
        ioctl(uifd, UI_SET_KEYBIT, KEY_0);
    }

    ioctl(uifd, UI_SET_EVBIT, EV_MSC);
    ioctl(uifd, UI_SET_MSCBIT, MSC_SCAN);

    /* create event device          */
    if (ioctl(uifd, UI_DEV_CREATE, NULL) < 0)   {
        print_log("/dev/uinput create error[%d]", errno);
        fflush(stderr);
        close(uifd);
        exit(1);
    }
    print_log("## created event device %s", device);

    for (ii = 0; ii < 16; ii++) {
        snprintf(devFile, 64, "/dev/input/event%d", ii);
        fd = open(devFile, O_RDONLY);
        if (fd < 0)     continue;

        memset(devName, 0, sizeof(devName));
        ioctl(fd, EVIOCGNAME(sizeof(devName)), devName);
        close(fd);
        print_log("%d.event device(%s) is %s", ii+1, devFile, devName);
    }
}

static int
convert_value(const char *value, char **errp, int base)
{
    int i;

    for (i = 0; value[i]; i++)  {
        if ((value[i] == ',') || (value[i] == ';') ||
            (value[i] == ';') || (value[i] == ' ')) {
            break;
        }
    }
    if (errp)   {
        *errp = (char *)&value[i];
    }

    if ((strncasecmp(value, "on", i) == 0) ||
        (strncasecmp(value, "true", i) == 0) ||
        (strncasecmp(value, "push", i) == 0) ||
        (strncasecmp(value, "down", i) == 0) ||
        (strncasecmp(value, "right", i) == 0))  {
        return 1;
    }
    else if ((strncasecmp(value, "off", i) == 0) ||
             (strncasecmp(value, "false", i) == 0) ||
             (strncasecmp(value, "pop", i) == 0) ||
             (strncasecmp(value, "up", i) == 0) ||
             (strncasecmp(value, "left", i) == 0))  {
        return 0;
    }
    return strtol(value, (char **)0, 0);
}

static void
send_event(const char *cmd)
{
    int     i, j;
    int     key;
    char    prop[64];
    char    value[128];
    int     sec, msec;
    char    *errp;
    struct input_event  event;
    struct js_event     js;

    j = 0;
    for (i = 0; cmd[i]; i++)    {
        if ((cmd[i] == '=') || (cmd[i] == ' ')) break;
        if (j < (int)(sizeof(prop)-1))  {
            prop[j++] = cmd[i];
        }
    }

    prop[j] = 0;
    j = 0;
    if (cmd[i] != 0)    {
        for (i++; cmd[i]; i++)  {
            if (cmd[i] == ' ')  continue;
            if (j < (int)(sizeof(value)-1)) {
                value[j++] = cmd[i];
            }
        }
    }
    value[j] = 0;

    if (strcasecmp(prop, "sleep") == 0) {
        sec = 0;
        msec = 0;
        for (i = 0; value[i]; i++)  {
            if (value[i] == '.')        break;
            sec = sec * 10 + (value[i] & 0x0f);
        }
        if (value[i] == '.')    {
            i++;
            if (value[i] != 0)  {
                msec = (value[i] & 0x0f) * 100;
                i++;
            }
            if (value[i] != 0)  {
                msec = msec + (value[i] & 0x0f) * 10;
                i++;
            }
            if (value[i] != 0)  {
                msec = msec + (value[i] & 0x0f);
            }
        }
        if (sec > 0)    sleep(sec);
        if (msec > 0)   usleep(msec * 1000);

        return;
    }

    for (key = 0; event_key[key].prop[0]; key++)    {
        if (strcasecmp(prop, event_key[key].prop) == 0) break;
    }
    if (! event_key[key].prop[0])   {
        print_log("UnKnown Event name[%s]", prop);
        return;
    }

    if (mTouch != 0)    {
        memset(&event, 0, sizeof(event));
        gettimeofday(&event.time, NULL);
        if (event_key[key].type == SPECIALTYPE_XY)  {
            event.type = EV_ABS;
            event.code = ABS_X;
            event.value = convert_value(value, &errp, 0);
            if (mDebug) {
                print_log("Send Event ABS_X=%d\t# %d.%03d", event.value,
                          (int)event.time.tv_sec, (int)(event.time.tv_usec/1000));
                fflush(stderr);
            }
            if (write(uifd, &event, sizeof(struct input_event)) < 0)    {
                print_log("event write error 1[%d]", errno);
                fflush(stderr);
                return;
            }
            event.code = ABS_Y;
            if (*errp == ',')   {
                event.value = convert_value(errp + 1, (char **)0, 0);
            }
            else    {
                event.value = 0;
            }
            event.time.tv_usec += 200;
            if (event.time.tv_usec >= 1000000)  {
                event.time.tv_sec ++;
                event.time.tv_usec -= 1000000;
            }
            if (mDebug) {
                print_log("Send Event ABS_Y=%d\t# %d.%03d", event.value,
                          (int)event.time.tv_sec, (int)(event.time.tv_usec/1000));
                fflush(stderr);
            }
        }
        else    {
            event.type = event_key[key].type;

            if (event_key[key].code == -1)   {
                event.code = convert_value(value, (char **)0, 0);
            }
            else    {
                event.code = event_key[key].code;
                event.value = convert_value(value, (char **)0, 0);
            }
            if (mDebug) {
                if ((event.type == EV_ABS) && (event.code == ABS_X))    {
                    print_log("Send Event X=%d\t# %d.%03d", event.value,
                              (int)event.time.tv_sec, (int)(event.time.tv_usec/1000));
                }
                else if ((event.type == EV_ABS) && (event.code == ABS_Y))    {
                    print_log("Send Event Y=%d\t %d.%03d", event.value,
                              (int)event.time.tv_sec, (int)(event.time.tv_usec/1000));
                }
                else if ((event.type == EV_KEY) &&
                         (event.code == BTN_LEFT) && (event.value == 1))    {
                    print_log("Send Event BTN_LEFT=Down\t# %d.%03d",
                              (int)event.time.tv_sec, (int)(event.time.tv_usec/1000));
                }
                else if ((event.type == EV_KEY) &&
                         (event.code == BTN_LEFT) && (event.value == 0))   {
                    print_log("Send Event BTN_LEFT=Up\t# %d.%03d",
                              (int)event.time.tv_sec, (int)(event.time.tv_usec/1000));
                }
                else    {
                    if ((event.type == EV_REL) && (event.value == 0))   {
                        event.value = 9;
                    }
                    else if ((event.type == EV_KEY) && (event.code == 0))   {
                        event.code = 9;
                    }
                    print_log("Send Event type=%d code=%d value=%d\t# %d.%03d",
                              event.type, event.code, event.value,
                              (int)event.time.tv_sec, (int)(event.time.tv_usec/1000));
                }
                fflush(stderr);
            }
        }
        if (write(uifd, &event, sizeof(struct input_event)) < 0)    {
            print_log("event write error 2[%d]", errno);
            fflush(stderr);
        }
        else    {
            /* send EV_SYN */
            memset(&event, 0, sizeof(event));
            gettimeofday(&event.time, NULL);
            event.type = EV_SYN;
            event.code = SYN_REPORT;
            if (write(uifd, &event, sizeof(struct input_event)) < 0)    {
                print_log("syn event write error 3[%d]", errno);
            }
        }
    }
    else    {
        memset(&js, 0, sizeof(js));
        gettimeofday(&event.time, NULL);
        js.time = (event.time.tv_sec * 1000) + (event.time.tv_usec / 1000);
        js.type = event_key[key].type;
        js.number = event_key[key].code;
        js.value = convert_value(value, (char **)0, 0);
        if (mDebug) {
            print_log("Send Event JS=%d,%d,%d\t# %d",
                      (int)js.type, (int)js.number, (int)js.value, (int)js.time);
        }
        if (write(uifd, &js, sizeof(struct js_event)) < 0)  {
            print_log("event write error 4[%d]", errno);
            fflush(stderr);
        }
    }
}

static void
usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-device=device] [{-m/-t/-j}] [-mq[=key]] "
            "[-d] [event=value] [event=value] ...\n", prog);
    exit(0);
}

int
main(int argc, char *argv[])
{
    int     i, j, k;
    int     mqkey = 0;
    struct {
        long    mtype;
        char    buf[240];
    }       mqbuf;
    char    buf[240];

    j = 0;
    strcpy(buf, "ico_test_device");
    for (i = 1; i < argc; i++)  {
        if (argv[i][0] == '-')  {
            if (strncasecmp(argv[i], "-device=", 8) == 0)   {
                strcpy(buf, &argv[i][8]);
            }
            else if (strcasecmp(argv[i], "-m") == 0)   {
                mTouch = 1;                 /* Simulate mouse               */
            }
            else if (strcasecmp(argv[i], "-t") == 0)   {
                mTouch = 2;                 /* Simulate touch-panel         */
            }
            else if (strcmp(argv[i], "-j") == 0)   {
                mTouch = 0;                 /* Simulate joystick            */
            }
            else if (strcmp(argv[i], "-J") == 0)   {
                mTouch = 3;                 /* Simulate joystick, but event is mouse    */
            }
            else if (strncasecmp(argv[i], "-mq", 3) == 0)   {
                if (argv[i][3] == '=')  {
                    mqkey = strtol(&argv[i][4], (char **)0, 0);
                }
                else    {
                    mqkey = 55551;          /* default message queue key    */
                }
            }
            else if (strcasecmp(argv[i], "-d") == 0)   {
                mDebug = 1;
            }
            else    {
                usage(argv[0]);
            }
        }
        else    {
            j++;
        }
    }

    init_mq(mqkey);

    init_device(buf);

    mRun = 1;

    signal(SIGTERM, term_signal);
    signal(SIGINT, term_signal);

    if (mqid >= 0)  {
        while (mRun)  {
            memset(&mqbuf, 0, sizeof(mqbuf));
            if (msgrcv(mqid, &mqbuf, sizeof(mqbuf)-sizeof(long), 0, 0) < 0) {
                if (errno == EINTR) continue;
                print_log("test-send_event: mq(%d) receive error[%d]",
                          mqkey, errno);
                fflush(stderr);
                break;
            }
            k = 0;
            j = -1;
            for (i = 0; mqbuf.buf[i]; i++)    {
                if ((mqbuf.buf[i] == '#') || (mqbuf.buf[i] == '\n')
                    || (mqbuf.buf[i] == '\r'))    break;
                if (mqbuf.buf[i] == '\t') buf[k++] = ' ';
                else                        buf[k++] = mqbuf.buf[i];
                if ((j < 0) && (mqbuf.buf[i] != ' ')) j = i;
            }
            if (j < 0)  continue;
            buf[k] = 0;
            send_event(&buf[j]);
        }
        msgctl(mqid, IPC_RMID, NULL);
    }
    else if (j <= 0) {
        while ((mRun != 0) && (fgets(buf, sizeof(buf), stdin) != NULL))  {
            j = -1;
            for (i = 0; buf[i]; i++)    {
                if ((buf[i] == '#') || (buf[i] == '\n') || (buf[i] == '\r'))    break;
                if (buf[i] == '\t') buf[i] = ' ';
                if ((j < 0) && (buf[i] != ' ')) j = i;
            }
            if (j < 0)  continue;
            buf[i] = 0;
            send_event(&buf[j]);
        }
    }
    else    {
        for (i = 1; i < argc; i++)  {
            if (argv[i][0] == '-')  continue;
            if (mRun == 0)  break;
            send_event(argv[i]);
        }
    }
    exit(0);
}

