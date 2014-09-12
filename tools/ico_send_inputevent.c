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
#include    <stdint.h>
#include    <unistd.h>
#include    <stdarg.h>
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
#include    "ico_input_mgr.h"

#define DEV_TOUCH   0
#define DEV_KEY     2
#define DEV_JS      1
#define SPECIALTYPE_XY  9991

static const struct _event_key  {
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

static const struct _event_keyboard {
    char    *prop;
    short   code;
}               event_keyboard[] = {
    { "ESC", KEY_ESC },
    { "1", KEY_1 },
    { "2", KEY_2 },
    { "3", KEY_3 },
    { "4", KEY_4 },
    { "5", KEY_5 },
    { "6", KEY_6 },
    { "7", KEY_7 },
    { "8", KEY_8 },
    { "9", KEY_9 },
    { "0", KEY_0 },
    { "-", KEY_MINUS },
    { "=", KEY_EQUAL },
    { "BS", KEY_BACKSPACE },
    { "TAB", KEY_TAB },
    { "Q", KEY_Q },
    { "W", KEY_W },
    { "E", KEY_E },
    { "R", KEY_R },
    { "T", KEY_T },
    { "Y", KEY_Y },
    { "U", KEY_U },
    { "I", KEY_I },
    { "O", KEY_O },
    { "P", KEY_P },
    { "[", KEY_LEFTBRACE },
    { "]", KEY_RIGHTBRACE },
    { "ENTER", KEY_ENTER },
    { "CR", KEY_ENTER },
    { "NL", KEY_ENTER },
    { "CTRL", KEY_LEFTCTRL },
    { "CTL", KEY_LEFTCTRL },
    { "LEFTCTRL", KEY_LEFTCTRL },
    { "A", KEY_A },
    { "S", KEY_S },
    { "D", KEY_D },
    { "F", KEY_F },
    { "G", KEY_G },
    { "H", KEY_H },
    { "J", KEY_J },
    { "K", KEY_K },
    { "L", KEY_L },
    { ";", KEY_SEMICOLON },
    { "'", KEY_APOSTROPHE },
    { "~", KEY_GRAVE },
    { "SHIFT", KEY_LEFTSHIFT },
    { "LEFTSHIFT", KEY_LEFTSHIFT },
    { "\\", KEY_BACKSLASH },
    { "Z", KEY_Z },
    { "X", KEY_X },
    { "C", KEY_C },
    { "V", KEY_V },
    { "B", KEY_B },
    { "N", KEY_N },
    { "M", KEY_M },
    { ",", KEY_COMMA },
    { ".", KEY_DOT },
    { "/", KEY_SLASH },
    { "RIGHTSHIFT", KEY_RIGHTSHIFT },
    { "*", KEY_KPASTERISK },
    { "ALT", KEY_LEFTALT },
    { " ", KEY_SPACE },
    { "SP", KEY_SPACE },
    { "SPACE", KEY_SPACE },
    { "CAPS", KEY_CAPSLOCK },
    { "CAPSLOCK", KEY_CAPSLOCK },
    { "F1", KEY_F1 },
    { "F2", KEY_F2 },
    { "F3", KEY_F3 },
    { "F4", KEY_F4 },
    { "F5", KEY_F5 },
    { "F6", KEY_F6 },
    { "F7", KEY_F7 },
    { "F8", KEY_F8 },
    { "F9", KEY_F9 },
    { "F10", KEY_F10 },
    { "NUM", KEY_NUMLOCK },
    { "NUMLOCK", KEY_NUMLOCK },
    { "SCROLL", KEY_SCROLLLOCK },
    { "SCROLLLOCK", KEY_SCROLLLOCK },
    { "F11", KEY_F11 },
    { "F12", KEY_F12 },
    { "\0", -1 } };

static int  uifd = -1;
static int  mqid = -1;
static int  mDebug = 0;
static int  mRun = 1;
static int  mTouch = 1;
static int  mWidth = 1920;
static int  mHeight = 1080;
static int  mRotate = 0;
static int  mConvert = 0;

static void print_log(const char *fmt, ...);

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
    int     ii;
    char    devFile[64];
    char    devName[64];
    char    wkDev[64];
    struct uinput_user_dev  uinputDevice;

    if (device[0])  {
        /* create pseudo input device   */
        uifd = open("/dev/uinput", O_RDWR);

        if (uifd < 0)   {
            print_log("/dev/uinput open error[%d]", errno);
            fflush(stderr);
            exit(1);
        }

        memset(&uinputDevice, 0, sizeof(uinputDevice));
        strncpy(uinputDevice.name, device, UINPUT_MAX_NAME_SIZE-1);
        uinputDevice.absmax[ABS_X] = mWidth;
        uinputDevice.absmax[ABS_Y] = mHeight;

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
    }
    else    {
        if (mTouch == 2)    {
            strcpy(wkDev, ICO_PSEUDO_INPUT_TOUCH);
        }
        else if (mTouch == 4)   {
            strcpy(wkDev, ICO_PSEUDO_INPUT_KEY);
        }
        else    {
            strcpy(wkDev, ICO_PSEUDO_INPUT_TOUCH);
        }

        for (ii = 0; ii < 32; ii++) {
            snprintf(devFile, 64, "/dev/input/event%d", ii);
            uifd = open(devFile, O_RDWR, 0644);
            if (uifd < 0)     continue;

            memset(devName, 0, sizeof(devName));
            ioctl(uifd, EVIOCGNAME(sizeof(devName)), devName);
            if (strcasecmp(devName, wkDev) == 0)    break;
            close(uifd);
        }
        if (ii >= 32)   {
            print_log("<%s> dose not exist.", wkDev);
            fflush(stderr);
            exit(1);
        }
    }
    usleep(200*1000);
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
    static int  keypress = 0;
    int     i, j;
    int     wkpress;
    int     key, key2;
    char    prop[64];
    char    value[128];
    int     sec, msec;
    char    *errp;
    struct input_event  event;
    struct js_event     js;

    j = 0;
    for (i = 0; cmd[i]; i++)    {
        if ((cmd[i] == '=') || (cmd[i] == ' ')) break;
        if (j < (int)(sizeof(prop)-2))  {
            prop[j++] = cmd[i];
        }
    }

    prop[j] = 0;
    j = 0;
    if (cmd[i] != 0)    {
        for (i++; cmd[i]; i++)  {
            if (cmd[i] == ' ')  continue;
            if (j < (int)(sizeof(value)-2)) {
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

    key2 = 0;
    wkpress = -1;
    for (key = 0; event_key[key].prop[0]; key++)    {
        if (strcasecmp(prop, event_key[key].prop) == 0) break;
    }
    if (! event_key[key].prop[0])   {
        for (key2 = 0; event_keyboard[key2].prop[0]; key2++)    {
            if (strcasecmp(prop, event_keyboard[key2].prop) == 0)           break;
            if ((strncasecmp(prop, "key.", 4) == 0) &&
                (strcasecmp(&prop[4], event_keyboard[key2].prop) == 0))     break;
            wkpress = 1;
            if ((strncasecmp(prop, "press.", 6) == 0) &&
                (strcasecmp(&prop[6], event_keyboard[key2].prop) == 0))     break;
            if ((strncasecmp(prop, "keypress.", 9) == 0) &&
                (strcasecmp(&prop[9], event_keyboard[key2].prop) == 0))     break;
            wkpress = 0;
            if ((strncasecmp(prop, "release.", 8) == 0) &&
                (strcasecmp(&prop[8], event_keyboard[key2].prop) == 0))     break;
            if ((strncasecmp(prop, "keyrelease.", 11) == 0) &&
                (strcasecmp(&prop[11], event_keyboard[key2].prop) == 0))    break;
        }
        if (! event_keyboard[key2].prop[0]) {
            print_log("UnKnown Event name[%s]", prop);
            return;
        }
        if (wkpress < 0)    {
            if (keypress == 0)  keypress = 1;
            else                keypress = 0;
            wkpress = keypress;
        }
        key = -1;
    }

    if (mTouch != 0)    {
        memset(&event, 0, sizeof(event));
        gettimeofday(&event.time, NULL);
        if ((key >= 0) &&(event_key[key].type == SPECIALTYPE_XY))   {
            event.type = EV_ABS;
            if (mRotate)    event.code = ABS_Y;
            else            event.code = ABS_X;
            event.value = convert_value(value, &errp, 0);
            if (mDebug) {
                print_log("Send Event ABS_%c=%d\t# %d.%03d", mRotate ? 'Y' : 'X',
                          event.value,
                          (int)event.time.tv_sec, (int)(event.time.tv_usec/1000));
                fflush(stderr);
            }
            if (mConvert != 0)  {
                if ((mRotate == 0) && (mWidth != 1920))    {
                    event.value = event.value * 1920 / mWidth;
                }
                if ((mRotate != 0) && (mHeight != 1080))    {
                    event.value = event.value * 1080 / mHeight;
                }
            }
            if (write(uifd, &event, sizeof(struct input_event)) < 0)    {
                print_log("event write error 1[%d]", errno);
                fflush(stderr);
                return;
            }
            if (mRotate)    event.code = ABS_X;
            else            event.code = ABS_Y;
            if (*errp == ',')   {
                event.value = convert_value(errp + 1, (char **)0, 0);
            }
            else    {
                event.value = 0;
            }
            if (mRotate)    {
                event.value = mWidth - event.value - 1;
            }
            if (mConvert != 0)  {
                if ((mRotate == 0) && (mHeight != 1080))    {
                    event.value = event.value * 1080 / mHeight;
                }
                else if ((mRotate != 0) && (mWidth != 1920))    {
                    event.value = event.value * 1920 / mWidth;
                }
            }
            event.time.tv_usec += 200;
            if (event.time.tv_usec >= 1000000)  {
                event.time.tv_sec ++;
                event.time.tv_usec -= 1000000;
            }
            if (mDebug) {
                print_log("Send Event ABS_%c=%d\t# %d.%03d", mRotate ? 'X' : 'Y',
                          event.value,
                          (int)event.time.tv_sec, (int)(event.time.tv_usec/1000));
                fflush(stderr);
            }
        }
        else    {
            if (key >= 0)   {
                event.type = event_key[key].type;

                if (event_key[key].code == -1)   {
                    event.code = convert_value(value, (char **)0, 0);
                }
                else    {
                    event.code = event_key[key].code;
                    if (value[0] == 0)  {
                        event.value = event_key[key].value;
                    }
                    else    {
                        event.value = convert_value(value, (char **)0, 0);
                    }
                }
            }
            else    {
                event.type = EV_KEY;
                event.code = event_keyboard[key2].code;
                event.value = wkpress ? 1 : 0;
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
                         (event.code == BTN_TOUCH) && (event.value == 1))    {
                    print_log("Send Event BTN_TOUCH=Down\t# %d.%03d",
                              (int)event.time.tv_sec, (int)(event.time.tv_usec/1000));
                }
                else if ((event.type == EV_KEY) &&
                         (event.code == BTN_TOUCH) && (event.value == 0))    {
                    print_log("Send Event BTN_TOUCH=Up\t# %d.%03d",
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
            if (key >= 0)   {
                memset(&event, 0, sizeof(event));
                gettimeofday(&event.time, NULL);
                event.type = EV_SYN;
                event.code = SYN_REPORT;
                if (write(uifd, &event, sizeof(struct input_event)) < 0)    {
                    print_log("syn event write error 3[%d]", errno);
                }
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

void
print_log(const char *fmt, ...)
{
    va_list     ap;
    char        log[128];
    struct timeval  NowTime;
    extern long timezone;
    static int  sTimeZone = (99*60*60);

    va_start(ap, fmt);
    vsnprintf(log, sizeof(log)-2, fmt, ap);
    va_end(ap);

    gettimeofday( &NowTime, (struct timezone *)0 );
    if( sTimeZone > (24*60*60) )    {
        tzset();
        sTimeZone = timezone;
    }
    NowTime.tv_sec -= sTimeZone;
    fprintf(stderr, "[%02d:%02d:%02d.%03d@%d] %s\n", (int)((NowTime.tv_sec/3600) % 24),
            (int)((NowTime.tv_sec/60) % 60), (int)(NowTime.tv_sec % 60),
            (int)NowTime.tv_usec/1000, getpid(), log);
}

static void
usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-device=device] [-w=w] [-h=h] [-r] [-c] [{-m/-t/-k/-j/-J}] "
            "[-mq[=key]] [-d] [event[=value]] [event[=value]] ...\n", prog);
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
    mWidth = 1920;
    mHeight = 1080;
    memset(buf, 0, sizeof(buf));
    for (i = 1; i < argc; i++)  {
        if (argv[i][0] == '-')  {
            if (strncasecmp(argv[i], "-device=", 8) == 0)   {
                strcpy(buf, &argv[i][8]);
            }
            else if (strncasecmp(argv[i], "-w=", 3) == 0)   {
                mWidth = strtol(&argv[i][3], (char **)0, 0);
            }
            else if (strncasecmp(argv[i], "-h=", 3) == 0)   {
                mHeight = strtol(&argv[i][3], (char **)0, 0);
            }
            else if (strcasecmp(argv[i], "-r") == 0)   {
                mRotate = 1;               /* Rotate screen                 */
            }
            else if (strcasecmp(argv[i], "-c") == 0)   {
                mConvert = 1;              /* Convert logical to physical   */
            }
            else if (strcasecmp(argv[i], "-m") == 0)   {
                mTouch = 1;                 /* Simulate mouse               */
            }
            else if (strcasecmp(argv[i], "-t") == 0)   {
                mTouch = 2;                 /* Simulate touch-panel         */
            }
            else if (strcasecmp(argv[i], "-k") == 0)   {
                mTouch = 4;                 /* Simulate keyboard            */
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
                print_log("ico_send_inputevent: mq(%d) receive error[%d]",
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
    usleep(300*1000);
    exit(0);
}

