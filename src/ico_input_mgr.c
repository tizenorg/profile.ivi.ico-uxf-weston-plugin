/*
 * Copyright © 2010-2011 Intel Corporation
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2013-2014 TOYOTA MOTOR CORPORATION.
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
 * @brief   Multi Input Manager (Weston(Wayland) PlugIn)
 *
 * @date    Feb-21-2014
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <assert.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <errno.h>

#include <pixman.h>
#include <wayland-server.h>
#include <weston/compositor.h>
#include "ico_ivi_common_private.h"
#include "ico_input_mgr.h"
#include "ico_window_mgr_private.h"
#include "ico_window_mgr-server-protocol.h"
#include "ico_input_mgr-server-protocol.h"

/* degine maximum length                */
#define ICO_MINPUT_DEVICE_LEN           32
#define ICO_MINPUT_SW_LEN               20
#define ICO_MINPUT_MAX_CODES            20

/* structure definition */
struct uifw_region_mng;

/* working table of Multi Input Manager */
struct ico_input_mgr {
    struct weston_compositor *compositor;   /* Weston Compositor                    */
    struct wl_list  ictl_list;              /* Input Controller List                */
    struct wl_list  app_list;               /* application List                     */
    struct wl_list  free_region;            /* free input region table list         */
    struct weston_seat *seat;               /* input seat                           */
    struct wl_resource *inputmgr;
    int             last_pointer;           /* pointer/touch device flag            */
};

/* Input Switch Table                   */
struct ico_ictl_code {
    uint16_t    code;                       /* input code numner                    */
    char        name[ICO_MINPUT_SW_LEN];    /* input code name                      */
};

struct ico_ictl_input {
    struct wl_list link;                    /* link                                 */
    char        swname[ICO_MINPUT_SW_LEN];  /* input switch name                    */
    int32_t     input;                      /* input Id                             */
    uint16_t    fix;                        /* fixed assign to application          */
    uint16_t    ncode;                      /* number of codes                      */
    struct ico_ictl_code code[ICO_MINPUT_MAX_CODES];   /* codes                     */
    struct ico_app_mgr  *app;               /* send event tagret application        */
};

/* Input Controller Management Table    */
struct ico_ictl_mgr {
    struct wl_list link;                    /* link                                 */
    struct wl_client    *client;            /* client                               */
    struct wl_resource  *mgr_resource;      /* resource as manager                  */
    char    device[ICO_MINPUT_DEVICE_LEN];  /* device name                          */
    int     type;                           /* device type                          */
    struct wl_list ico_ictl_input;          /* list of input switchs                */
};

/* Application Management Table */
struct ico_app_mgr {
    struct wl_list link;                    /* link                                 */
    struct wl_client    *client;            /* client                               */
    struct wl_resource  *resource;          /* resource for send event              */
    struct wl_resource  *mgr_resource;      /* resource as manager(if NULL, client) */
    char    appid[ICO_IVI_APPID_LENGTH];    /* application id                       */
};

/* Pseudo Input Device Control Flags    */
#define EVENT_MOTION        0x01            /* motion event                         */
#define EVENT_BUTTON        0x02            /* button event                         */
#define EVENT_TOUCH         0x03            /* touch event                          */
#define EVENT_KEY           0x04            /* key event                            */
#define EVENT_PENDING       0xff            /* pending event input                  */

#define PENDING_X           0x01            /* pending X coordinate                 */
#define PENDING_Y           0x02            /* pending Y coordinate                 */

/* Input Region Table           */
struct uifw_region_mng  {
    struct wl_list  link;                   /* link pointer                         */
    struct ico_uifw_input_region region;    /* input region                         */
};

/* prototype of static function */
                                            /* bind input manager form manager      */
static void ico_control_bind(struct wl_client *client, void *data,
                             uint32_t version, uint32_t id);
                                            /* unbind input manager form manager    */
static void ico_control_unbind(struct wl_resource *resource);
                                            /* bind input manager form input controller*/
static void ico_device_bind(struct wl_client *client, void *data,
                            uint32_t version, uint32_t id);
                                            /* unbind input manager form input controller*/
static void ico_device_unbind(struct wl_resource *resource);
                                            /* bind input manager(form application) */
static void ico_exinput_bind(struct wl_client *client, void *data,
                             uint32_t version, uint32_t id);
                                            /* unbind input manager(form application)*/
static void ico_exinput_unbind(struct wl_resource *resource);

                                            /* find ictl manager by device name     */
static struct ico_ictl_mgr *find_ictlmgr_by_device(const char *device);
                                            /* find ictl input switch by input Id   */
static struct ico_ictl_input *find_ictlinput_by_input(struct ico_ictl_mgr *pIctlMgr,
                                                      const int32_t input);
                                            /* find app manager by application Id   */
static struct ico_app_mgr *find_app_by_appid(const char *appid);
                                            /* add input event to application       */
static void ico_mgr_add_input_app(struct wl_client *client, struct wl_resource *resource,
                                  const char *appid, const char *device, int32_t input,
                                  int32_t fix, int32_t keycode);
                                            /* delete input event to application    */
static void ico_mgr_del_input_app(struct wl_client *client, struct wl_resource *resource,
                                  const char *appid, const char *device, int32_t input);
                                            /* send key input event from device     */
static void ico_mgr_send_key_event(struct wl_client *client, struct wl_resource *resource,
                                   const char *target, int32_t code, int32_t value);
                                            /* send pointer input event from device */
static void ico_mgr_send_pointer_event(struct wl_client *client,
                                       struct wl_resource *resource,
                                       const char *target, int32_t code, int32_t value);
                                            /* set input region                     */
static void ico_mgr_set_input_region(struct wl_client *client, struct wl_resource *resource,
                                     const char *target, int32_t x, int32_t y,
                                     int32_t width, int32_t height, int32_t hotspot_x,
                                     int32_t hotspot_y, int32_t cursor_x, int32_t cursor_y,
                                     int32_t cursor_width, int32_t cursor_height,
                                     uint32_t attr);
                                            /* unset input region                   */
static void ico_mgr_unset_input_region(struct wl_client *client,
                                       struct wl_resource *resource,
                                       const char *taret, int32_t x, int32_t y,
                                       int32_t width, int32_t height);
                                            /* input region set/unset               */
static void ico_set_input_region(int set, struct uifw_win_surface *usurf,
                                 int32_t x, int32_t y, int32_t width, int32_t height,
                                 int32_t hotspot_x, int32_t hotspot_y, int32_t cursor_x,
                                 int32_t cursor_y, int32_t cursor_width,
                                 int32_t cursor_height, uint32_t attr);
                                            /* create and regist Input Controller table*/
static void ico_device_configure_input(struct wl_client *client,
                                       struct wl_resource *resource, const char *device,
                                       int32_t type, const char *swname, int32_t input,
                                       const char *codename, int32_t code);
                                            /* add input to from Input Controller table*/
static void ico_device_configure_code(struct wl_client *client,
                                      struct wl_resource *resource, const char *device,
                                      int32_t input, const char *codename, int32_t code);
                                            /* device input event                   */
static void ico_device_input_event(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t time, const char *device,
                                   int32_t input, int32_t code, int32_t state);
                                            /* send region event                    */
static void ico_input_send_region_event(struct wl_array *array);

/* definition of Wayland protocol       */
/* Input Manager Control interface      */
static const struct ico_input_mgr_control_interface ico_input_mgr_implementation = {
    ico_mgr_add_input_app,
    ico_mgr_del_input_app,
    ico_mgr_send_key_event,
    ico_mgr_send_pointer_event
};

/* Extended Input interface             */
static const struct ico_exinput_interface ico_exinput_implementation = {
    ico_mgr_set_input_region,
    ico_mgr_unset_input_region
};

/* Input Controller Device interface    */
static const struct ico_input_mgr_device_interface input_mgr_ictl_implementation = {
    ico_device_configure_input,
    ico_device_configure_code,
    ico_device_input_event
};

/* definition of class variable */
struct ico_input_mgr    *pInputMgr = NULL;

/* implementation */
/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_mgr_add_input_app: add input event to application from HomeScreen.
 *
 * @param[in]   client          client(HomeScreen)
 * @param[in]   resource        resource of request
 * @param[in]   appid           target application id
 * @param[in]   device          device name
 * @param[in]   input           input switch number
 * @param[in]   fix             fix to application(1=fix,0=general)
 * @param[in]   keycode         switch map to keyboard operation(0=not map to keyboard)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_mgr_add_input_app(struct wl_client *client, struct wl_resource *resource,
                      const char *appid, const char *device, int32_t input,
                      int32_t fix, int32_t keycode)
{
    struct ico_ictl_mgr     *pIctlMgr;
    struct ico_ictl_input   *pInput;
    struct ico_app_mgr      *pAppMgr;

    uifw_trace("ico_mgr_add_input_app: Enter(appid=%s,dev=%s,input=%d,fix=%d,key=%d)",
               appid, device, input, fix, keycode);

    pIctlMgr = find_ictlmgr_by_device(device);
    if (! pIctlMgr) {
        /* not configure input controller, create   */
        ico_device_configure_input(NULL, NULL, device, 0, NULL, input, NULL, 0);
        pIctlMgr = find_ictlmgr_by_device(device);
        if (! pIctlMgr) {
            uifw_error("ico_mgr_add_input_app: Leave(No Memory)");
            return;
        }
    }
    pInput = find_ictlinput_by_input(pIctlMgr, input);
    if (! pInput)   {
        /* not configure input switch, create   */
        ico_device_configure_input(NULL, NULL, device, 0, NULL, input, NULL, 0);
        pInput = find_ictlinput_by_input(pIctlMgr, input);
        if (! pInput)   {
            uifw_error("ico_mgr_add_input_app: Leave(No Memory)");
            return;
        }
    }

    /* find application         */
    pAppMgr = find_app_by_appid(appid);
    if (! pAppMgr)  {
        /* create Application Management Table  */
        pAppMgr = (struct ico_app_mgr *)malloc(sizeof(struct ico_app_mgr));
        if (! pAppMgr)  {
            uifw_error("ico_mgr_add_input_app: Leave(No Memory)");
            return;
        }
        memset(pAppMgr, 0, sizeof(struct ico_app_mgr));
        strncpy(pAppMgr->appid, appid, sizeof(pAppMgr->appid)-1);
        wl_list_insert(pInputMgr->app_list.prev, &pAppMgr->link);
    }

    pInput->app = pAppMgr;
    pInput->fix = fix;
    uifw_trace("ico_mgr_add_input_app: Leave(%s.%s[%d] assign to %s)",
               pIctlMgr->device, pInput->swname ? pInput->swname : "(NULL)", input,
               pAppMgr->appid);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_mgr_del_input_app: delete input event at application from HomeScreen.
 *
 * @param[in]   client          client(HomeScreen)
 * @param[in]   resource        resource of request
 * @param[in]   appid           target application id,
 *                              if NULL, all applictions without fixed assign switch
 * @param[in]   device          device name
 *                              if NULL, all device without fixed assign switch
 * @param[in]   input           input switch number
 *                              if -1, all input without fixed assign switch
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_mgr_del_input_app(struct wl_client *client, struct wl_resource *resource,
                      const char *appid, const char *device, int32_t input)
{
    int     alldev = 0;
    struct ico_ictl_mgr     *pIctlMgr = NULL;
    struct ico_ictl_input   *pInput = NULL;
    struct ico_app_mgr      *pAppMgr;

    uifw_trace("ico_mgr_del_input_app: Enter(appid=%s,dev=%s,input=%d)",
               appid ? appid : "(NULL)", device ? device : "(NULL)", input);

    if ((device != NULL) && (*device != 0)) {
        pIctlMgr = find_ictlmgr_by_device(device);
        if (! pIctlMgr) {
            /* not configure input controller, NOP  */
            uifw_trace("ico_mgr_del_input_app: Leave(%s dose not exist)", device);
            return;
        }
        if (input >= 0) {
            pInput = find_ictlinput_by_input(pIctlMgr, input);
            if (! pInput)   {
                /* not configure input switch, NOP  */
                uifw_trace("ico_mgr_del_input_app: Leave(%s.%d dose not exist)",
                           device, input);
                return;
            }
        }
    }
    else    {
        alldev = 1;
    }

    /* find application         */
    if ((appid != NULL) && (*appid != 0))   {
        pAppMgr = find_app_by_appid(appid);
        if (! pAppMgr)  {
            /* application dose not exist, NOP  */
            uifw_trace("ico_mgr_del_input_app: Leave(app.%s dose not exist)", appid);
            return;
        }
        if (alldev == 0)    {
            if (input >= 0) {
                if (pInput->app != pAppMgr) {
                    /* not same application, NOP        */
                    uifw_trace("ico_mgr_del_input_app: Leave(%s.%d not app.%s, current %s)",
                               device, input, appid,
                               pInput->app ? pInput->app->appid : "(NULL)");
                    return;
                }
                uifw_trace("ico_mgr_del_input_app: Leave(%s.%d app.%s deleted)",
                           device, input, appid);
                pInput->app = NULL;
                return;
            }
            else    {
                wl_list_for_each (pInput, &pIctlMgr->ico_ictl_input, link)  {
                    if ((pInput->fix == 0) && (pInput->app == pAppMgr))   {
                        uifw_trace("ico_mgr_del_input_app: %s.%d app.%s deleted",
                                   pIctlMgr->device, pInput->input, appid);
                        pInput->app = NULL;
                    }
                }
            }
        }
        else    {
            /* reset all device without fixed assign    */
            wl_list_for_each (pIctlMgr, &pInputMgr->ictl_list, link)    {
                wl_list_for_each (pInput, &pIctlMgr->ico_ictl_input, link)  {
                    if ((pInput->fix == 0) && (pInput->app == pAppMgr))   {
                        uifw_trace("ico_mgr_del_input_app: %s.%d app.%s deleted",
                                   pIctlMgr->device, pInput->input, pInput->app->appid);
                        pInput->app = NULL;
                    }
                }
            }
        }
    }
    else    {
        if (alldev == 0)    {
            if (input >= 0) {
                if ((pInput->fix == 0) && (pInput->app != NULL))    {
                    uifw_trace("ico_mgr_del_input_app: %s.%d app.%s deleted",
                               pIctlMgr->device, pInput->input, pInput->app->appid);
                    pInput->app = NULL;
                }
            }
            else    {
                wl_list_for_each (pInput, &pIctlMgr->ico_ictl_input, link)  {
                    if ((pInput->fix == 0) && (pInput->app != NULL))    {
                        uifw_trace("ico_mgr_del_input_app: %s.%d app.%s deleted",
                               pIctlMgr->device, pInput->input, pInput->app->appid);
                        pInput->app = NULL;
                    }
                }
            }
        }
        else    {
            /* reset all application without fixed assign       */
            wl_list_for_each (pIctlMgr, &pInputMgr->ictl_list, link)    {
                wl_list_for_each (pInput, &pIctlMgr->ico_ictl_input, link)  {
                    if ((pInput->fix == 0) && (pInput->app != NULL))    {
                        uifw_trace("ico_mgr_del_input_app: %s.%d app.%s deleted",
                                   pIctlMgr->device, pInput->input, pInput->app->appid);
                        pInput->app = NULL;
                    }
                }
            }
        }
    }
    uifw_trace("ico_mgr_del_input_app: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_mgr_send_key_event: send key input event from device input controller
 *
 * @param[in]   client          client(HomeScreen)
 * @param[in]   resource        resource of request
 * @param[in]   target          target window name and application id
 * @param[in]   code            event code
 * @param[in]   value           event value
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_mgr_send_key_event(struct wl_client *client, struct wl_resource *resource,
                       const char *target, int32_t code, int32_t value)
{
    struct uifw_win_surface *usurf;         /* UIFW surface                 */
    struct wl_array         dummy_array;    /* dummy array for wayland API  */
    uint32_t    ctime;                      /* current time(ms)             */
    uint32_t    serial;                     /* event serial number          */
    int         keyboard_active;            /* keyborad active surface flag */

    uifw_trace("ico_mgr_send_key_event: Enter(target=%s code=%x value=%d)",
               target ? target : "(NULL)", code, value);

    if (! pInputMgr->seat->keyboard)    {
        uifw_error("ico_mgr_send_key_event: Leave(system has no keyboard)");
        return;
    }

    ctime = weston_compositor_get_time();

    if ((target == NULL) || (*target == 0) || (*target == ' ')) {
        /* send event to surface via weston */
        uifw_trace("ico_mgr_send_key_event: notify_key(%d,%d)", code, value);

        notify_key(pInputMgr->seat, ctime, code,
                   value ? WL_KEYBOARD_KEY_STATE_PRESSED :
                           WL_KEYBOARD_KEY_STATE_RELEASED, STATE_UPDATE_NONE);
    }
    else    {
        /* send event to fixed application  */
        /* get application surface       */
        usurf = ico_window_mgr_get_client_usurf(target);
        if (! usurf)  {
            uifw_trace("ico_mgr_send_key_event: Leave(window=%s dose not exist)",
                       target);
            return;
        }

        /* send event                   */
        if (usurf->uclient->res_keyboard)   {
            if (pInputMgr->seat->keyboard->focus == usurf->surface) {
                keyboard_active = 1;
            }
            else    {
                keyboard_active = 0;
            }
            if (! keyboard_active)  {
                wl_array_init(&dummy_array);
                serial = wl_display_next_serial(pInputMgr->compositor->wl_display);
                wl_keyboard_send_enter(usurf->uclient->res_keyboard, serial,
                                       usurf->surface->resource, &dummy_array);
            }
            serial = wl_display_next_serial(pInputMgr->compositor->wl_display);
            uifw_trace("ico_mgr_send_key_event: send Key (%d, %d) to %08x",
                       code, value, usurf->surfaceid);
            wl_keyboard_send_key(usurf->uclient->res_keyboard, serial, ctime, code,
                                 value ? WL_KEYBOARD_KEY_STATE_PRESSED :
                                         WL_KEYBOARD_KEY_STATE_RELEASED);
           if (! keyboard_active)  {
                serial = wl_display_next_serial(pInputMgr->compositor->wl_display);
                wl_keyboard_send_leave(usurf->uclient->res_keyboard,
                                       serial, usurf->surface->resource);
            }
        }
        else    {
            uifw_trace("ico_mgr_send_key_event: Key client %08x dose not exist",
                       (int)usurf->surface->resource);
        }
    }
    uifw_debug("ico_mgr_send_key_event: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_mgr_send_pointer_event: send pointer input event
 *
 * @param[in]   client          client(HomeScreen)
 * @param[in]   resource        resource of request
 * @param[in]   target          target window name and application id
 * @param[in]   code            event code
 * @param[in]   value           event value
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_mgr_send_pointer_event(struct wl_client *client, struct wl_resource *resource,
                           const char *target, int32_t code, int32_t value)
{
    struct uifw_win_surface *usurf;         /* UIFW surface                 */
    uint32_t    ctime;                      /* current time(ms)             */
    uint32_t    serial;                     /* event serial number          */
    int         fx;                         /* x as wl_fixed                */
    int         fy;                         /* y as wl_fixed                */
    int         pointer_active;             /* pointerborad active surface flag */

    uifw_trace("ico_mgr_send_pointer_event: Enter(target=%s code=%x value=%d)",
               target ? target : "(NULL)", code, value);

    if ((! pInputMgr->seat->touch) && (! pInputMgr->seat->pointer)) {
        uifw_error("ico_mgr_send_pointer_event: Leave(system has no pointer/touch)");
        return;
    }

    ctime = weston_compositor_get_time();
    if ((pInputMgr->seat->touch != NULL) &&
        (pInputMgr->seat->touch->num_tp > 0))   {
        pInputMgr->last_pointer = 0;
    }
    else if ((pInputMgr->seat->pointer != NULL) &&
             (pInputMgr->seat->pointer->button_count > 0))  {
        pInputMgr->last_pointer = 1;
    }

    fx = wl_fixed_from_int((value >> 16) & 0x7fff),
    fy = wl_fixed_from_int(value & 0x7fff);

    if ((target == NULL) || (*target == 0) || (*target == ' ')) {
        /* send event to surface via weston */

        if (pInputMgr->last_pointer)    {
            /* pointer(mouse) event */
            if (code & ICO_INPUT_MGR_CONTROL_EVENT_TOUCH_CANCEL)    {
                for (pointer_active = pInputMgr->seat->pointer->button_count;
                     pointer_active > 0; pointer_active--)  {
                    notify_button(pInputMgr->seat, ctime, BTN_LEFT,
                                  WL_POINTER_BUTTON_STATE_RELEASED);
                }
            }

            /* hide touch-layer */
            ico_window_mgr_set_touch_layer(0);

            switch (code & ICO_INPUT_MGR_CONTROL_EVENT_CODE_MASK)   {
            case ABS_Z:
                uifw_trace("ico_mgr_send_pointer_event: motion_absolute(%d,%d)",
                           fx/256, fy/256);
                notify_motion_absolute(pInputMgr->seat, ctime, fx, fy);
                break;
            case BTN_TOUCH:
            case BTN_LEFT:
            case BTN_MIDDLE:
            case BTN_RIGHT:
                if (code & ICO_INPUT_MGR_CONTROL_EVENT_TOUCH_UP)    {
                    value = WL_POINTER_BUTTON_STATE_RELEASED;
                }
                else if (code & ICO_INPUT_MGR_CONTROL_EVENT_TOUCH_DOWN) {
                    value = WL_POINTER_BUTTON_STATE_PRESSED;
                }
                else if (value != 0)    {
                    value = WL_POINTER_BUTTON_STATE_PRESSED;
                }
                else    {
                    value = WL_POINTER_BUTTON_STATE_PRESSED;
                }
                if ((code & ICO_INPUT_MGR_CONTROL_EVENT_CODE_MASK) == BTN_TOUCH)    {
                    code = BTN_LEFT;
                }
                uifw_trace("ico_mgr_send_pointer_event: button(%d,%d)", code, value);
                notify_button(pInputMgr->seat, ctime, code, value);
                break;
            case 0:
                break;          /*not operation */
            default:
                uifw_trace("ico_mgr_send_pointer_event: unknown pointer code=0x%02x(%d)",
                           code, code);
                break;
            }
        }
        else    {
            /* touch event          */
            if (code & ICO_INPUT_MGR_CONTROL_EVENT_TOUCH_CANCEL)    {
                for (pointer_active = pInputMgr->seat->touch->num_tp;
                     pointer_active > 0; pointer_active--)  {
                    uifw_trace("ico_mgr_send_pointer_event: touch UP for cancel(%d,%d)",
                               fx/256, fy/256);
                    notify_touch(pInputMgr->seat, ctime, 0, fx, fy, WL_TOUCH_UP);
                }
            }

            /* hide touch-layer */
            ico_window_mgr_set_touch_layer(0);

            switch (code & ICO_INPUT_MGR_CONTROL_EVENT_CODE_MASK)   {
            case ABS_Z:
                uifw_trace("ico_mgr_send_pointer_event: touch MOTION(%d,%d)",
                           fx/256, fy/256);
                notify_touch(pInputMgr->seat, ctime, 0, fx, fy, WL_TOUCH_MOTION);
                break;
            case BTN_TOUCH:
            case BTN_LEFT:
            case BTN_MIDDLE:
            case BTN_RIGHT:
                if (code & ICO_INPUT_MGR_CONTROL_EVENT_TOUCH_UP)    {
                    value = WL_TOUCH_UP;
                }
                else    {
                    value = WL_TOUCH_DOWN;
                }
                uifw_trace("ico_mgr_send_pointer_event: touch %s(%d,%d)",
                           (value == WL_TOUCH_UP) ? "UP" : "DOWN", fx/256, fy/256);
                notify_touch(pInputMgr->seat, ctime, 0, fx, fy, value);
                break;
            case 0:
                break;          /*not operation */
            default:
                uifw_trace("ico_mgr_send_pointer_event: unknown touch code=0x%02x(%d)",
                           code, code);
                break;
            }
        }
        /* show touch-layer */
        ico_window_mgr_set_touch_layer(1);
    }
    else    {
        /* send event to fixed application  */
        /* get application surface       */
        usurf = ico_window_mgr_get_client_usurf(target);
        if (! usurf)  {
            uifw_trace("ico_mgr_send_pointer_event: Leave(window=%s dose not exist)",
                       target);
            return;
        }

        /* send event                   */
        if ((pInputMgr->last_pointer != 0) &&
            (usurf->uclient->res_pointer))  {
            /* pointer(mouse) event */
            if ((pInputMgr->seat->pointer->focus != NULL) &&
                (pInputMgr->seat->pointer->focus->surface == usurf->surface))   {
                pointer_active = 1;
            }
            else    {
                pointer_active = 0;
            }
            if (! pointer_active)  {
                serial = wl_display_next_serial(pInputMgr->compositor->wl_display);
                wl_pointer_send_enter(usurf->uclient->res_pointer, serial,
                                      usurf->surface->resource,
                                      fx - wl_fixed_from_int(usurf->x),
                                      fy - wl_fixed_from_int(usurf->y));
            }
            uifw_trace("ico_mgr_send_pointer_event: send pointer event (%d, %d) to %08x",
                       code, value, usurf->surfaceid);

            switch (code & ICO_INPUT_MGR_CONTROL_EVENT_CODE_MASK)   {
            case ABS_Z:
                wl_pointer_send_motion(usurf->uclient->res_pointer, ctime,
                                       fx - wl_fixed_from_int(usurf->x),
                                       fy - wl_fixed_from_int(usurf->y));
                break;
            case BTN_TOUCH:
            case BTN_LEFT:
            case BTN_MIDDLE:
            case BTN_RIGHT:
                if (code & ICO_INPUT_MGR_CONTROL_EVENT_TOUCH_UP)    {
                    value = WL_POINTER_BUTTON_STATE_RELEASED;
                }
                else if (code & ICO_INPUT_MGR_CONTROL_EVENT_TOUCH_DOWN) {
                    value = WL_POINTER_BUTTON_STATE_PRESSED;
                }
                else if (value != 0)    {
                    value = WL_POINTER_BUTTON_STATE_PRESSED;
                }
                else    {
                    value = WL_POINTER_BUTTON_STATE_PRESSED;
                }
                code &= ICO_INPUT_MGR_CONTROL_EVENT_CODE_MASK;
                if (code == BTN_TOUCH)  code = BTN_LEFT;
                serial = wl_display_next_serial(pInputMgr->compositor->wl_display);
                wl_pointer_send_button(usurf->uclient->res_pointer, serial, ctime,
                                       code, value);
                break;
            case 0:
                break;          /*not operation */
            default:
                uifw_trace("ico_mgr_send_pointer_event: unknown pointer code=0x%02x(%d)",
                           code, code);
                break;
            }
           if (! pointer_active)  {
                serial = wl_display_next_serial(pInputMgr->compositor->wl_display);
                wl_pointer_send_leave(usurf->uclient->res_pointer,
                                      serial, usurf->surface->resource);
            }
        }
        else if ((pInputMgr->last_pointer == 0) &&
                 (usurf->uclient->res_touch))   {
            /* touch event          */
            if ((pInputMgr->seat->touch->focus != NULL) &&
                (pInputMgr->seat->touch->focus->surface == usurf->surface)) {
                pointer_active = 1;
            }
            else    {
                pointer_active = 0;
            }
            if (! pointer_active)  {
                serial = wl_display_next_serial(pInputMgr->compositor->wl_display);
                wl_pointer_send_enter(usurf->uclient->res_touch, serial,
                                      usurf->surface->resource,
                                      fx - wl_fixed_from_int(usurf->x),
                                      fy - wl_fixed_from_int(usurf->y));
            }
            uifw_trace("ico_mgr_send_pointer_event: send touch event (%d, %d) to %08x",
                       code, value, usurf->surfaceid);
            switch (code & ICO_INPUT_MGR_CONTROL_EVENT_CODE_MASK)   {
            case ABS_Z:
                wl_touch_send_motion(usurf->uclient->res_touch, ctime, 0, fx, fy);
                break;
            case BTN_TOUCH:
            case BTN_LEFT:
            case BTN_MIDDLE:
            case BTN_RIGHT:
                serial = wl_display_next_serial(pInputMgr->compositor->wl_display);
                if (code & ICO_INPUT_MGR_CONTROL_EVENT_TOUCH_UP)    {
                    wl_touch_send_up(usurf->uclient->res_touch, serial, ctime, 0);
                }
                else    {
                    wl_touch_send_down(usurf->uclient->res_touch, serial, ctime,
                                       usurf->surface->resource, 0, fx, fy);
                }
                break;
            case 0:
                break;          /*not operation */
            default:
                uifw_trace("ico_mgr_send_pointer_event: unknown touch code=0x%02x(%d)",
                           code, code);
                break;
            }
            if (! pointer_active)  {
                serial = wl_display_next_serial(pInputMgr->compositor->wl_display);
                wl_pointer_send_leave(usurf->uclient->res_touch,
                                      serial, usurf->surface->resource);
            }
        }
        else    {
            uifw_trace("ico_mgr_send_pointer_event: pointer client %08x dose not exist",
                       (int)usurf->surface->resource);
        }
    }
    uifw_debug("ico_mgr_send_pointer_event: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_mgr_set_input_region: set input region for haptic devcie
 *
 * @param[in]   client          client(Device Input Controller)
 * @param[in]   resource        resource of request
 * @param[in]   target          target window (winname@appid)
 * @param[in]   x               input region X coordinate
 * @param[in]   y               input region X coordinate
 * @param[in]   width           input region width
 * @param[in]   height          input region height
 * @param[in]   hotspot_x       hotspot of X relative coordinate
 * @param[in]   hotspot_y       hotspot of Y relative coordinate
 * @param[in]   cursor_x        cursor region X coordinate
 * @param[in]   cursor_y        cursor region X coordinate
 * @param[in]   cursor_width    cursor region width
 * @param[in]   cursor_height   cursor region height
 * @param[in]   attr            region attributes(currently unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_mgr_set_input_region(struct wl_client *client, struct wl_resource *resource,
                         const char *target, int32_t x, int32_t y,
                         int32_t width, int32_t height, int32_t hotspot_x,
                         int32_t hotspot_y, int32_t cursor_x, int32_t cursor_y,
                         int32_t cursor_width, int32_t cursor_height, uint32_t attr)
{
    struct uifw_win_surface *usurf;         /* UIFW surface                 */

    uifw_trace("ico_mgr_set_input_region: Enter(%s %d/%d-%d/%d(%d/%d) %d/%d-%d/%d)",
               target, x, y, width, height, hotspot_x, hotspot_y,
               cursor_x, cursor_y, cursor_width, cursor_height);

    /* get target surface           */
    usurf = ico_window_mgr_get_client_usurf(target);
    if (! usurf)    {
        uifw_warn("ico_mgr_set_input_region: Leave(target<%s> dose not exist)", target);
        return;
    }

    ico_set_input_region(1, usurf, x, y, width, height, hotspot_x, hotspot_y,
                         cursor_x, cursor_y, cursor_width, cursor_height, attr);

    uifw_trace("ico_mgr_set_input_region: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_mgr_unset_input_region: unset input region for haptic devcie
 *
 * @param[in]   client          client(Device Input Controller)
 * @param[in]   resource        resource of request
 * @param[in]   target          target window (winname@appid)
 * @param[in]   x               input region X coordinate
 * @param[in]   y               input region X coordinate
 * @param[in]   width           input region width
 * @param[in]   height          input region height
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_mgr_unset_input_region(struct wl_client *client, struct wl_resource *resource,
                           const char *target, int32_t x, int32_t y,
                           int32_t width, int32_t height)
{
    struct uifw_win_surface *usurf;         /* UIFW surface                 */

    uifw_trace("ico_mgr_unset_input_region: Enter(%s %d/%d-%d/%d)",
               target, x, y, width, height);

    /* get target surface           */
    usurf = ico_window_mgr_get_client_usurf(target);
    if (! usurf)    {
        uifw_warn("ico_mgr_unset_input_region: Leave(target<%s> dose not exist)", target);
        return;
    }

    ico_set_input_region(0, usurf, x, y, width, height, 0, 0, 0, 0, 0, 0, 0);

    uifw_trace("ico_mgr_unset_input_region: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_input_hook_region_change: change surface attribute
 *
 * @param[in]   usurf           UIFW surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_input_hook_region_change(struct uifw_win_surface *usurf)
{
    struct uifw_region_mng      *p;         /* input region mamagement table*/
    struct ico_uifw_input_region *rp;       /* input region                 */
    struct wl_array             array;
    int                         chgcount = 0;
    int                         visible;

    uifw_trace("ico_input_hook_region_change: Entery(surf=%08x)", usurf->surfaceid);

    wl_array_init(&array);

    visible = /* get visiblety form weston_layout */ 0;

    wl_list_for_each(p, &usurf->input_region, link) {
        if (((p->region.change > 0) && (visible <= 0)) ||
            ((p->region.change <= 0) && (visible > 0))) {
            /* visible change, send add/remove event    */
            rp = (struct ico_uifw_input_region *)
                 wl_array_add(&array, sizeof(struct ico_uifw_input_region));
            if (rp) {
                chgcount ++;
                memcpy(rp, &p->region, sizeof(struct ico_uifw_input_region));
                if (visible > 0)    {
                    rp->change = ICO_INPUT_MGR_DEVICE_REGION_ADD;
                }
                else    {
                    rp->change = ICO_INPUT_MGR_DEVICE_REGION_REMOVE;
                }
            }
            p->region.change = visible;
            p->region.node = usurf->node_tbl->node;
            p->region.surface_x = usurf->x;
            p->region.surface_y = usurf->y;
        }
        else if ((p->region.node != usurf->node_tbl->node) ||
                 (p->region.surface_x != usurf->x) ||
                 (p->region.surface_y != usurf->y)) {
            /* surface position change, send change event   */
            p->region.node = usurf->node_tbl->node;
            p->region.surface_x = usurf->x;
            p->region.surface_y = usurf->y;

            rp = (struct ico_uifw_input_region *)
                 wl_array_add(&array, sizeof(struct ico_uifw_input_region));
            if (rp) {
                chgcount ++;
                memcpy(rp, &p->region, sizeof(struct ico_uifw_input_region));
                rp->change = ICO_INPUT_MGR_DEVICE_REGION_CHANGE;
            }
        }
    }
    if (chgcount > 0)   {
        /* send region delete to haptic device input controller */
        ico_input_send_region_event(&array);
    }
    uifw_trace("ico_input_hook_region_change: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_input_hook_region_destroy: destory surface
 *
 * @param[in]   usurf           UIFW surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_input_hook_region_destroy(struct uifw_win_surface *usurf)
{
    struct uifw_region_mng      *p;         /* input region mamagement table*/
    struct uifw_region_mng      *np;        /* next region mamagement table */
    struct ico_uifw_input_region *rp;       /* input region                 */
    struct wl_array             array;
    int                         delcount = 0;

    uifw_trace("ico_input_hook_region_destroy: Entery(surf=%08x)", usurf->surfaceid);

    wl_array_init(&array);

    wl_list_for_each_safe(p, np, &usurf->input_region, link)    {
        if (p->region.change > 0)   {
            /* visible, send remove event   */
            rp = (struct ico_uifw_input_region *)
                 wl_array_add(&array, sizeof(struct ico_uifw_input_region));
            if (rp) {
                delcount ++;
                memcpy(rp, &p->region, sizeof(struct ico_uifw_input_region));
                rp->change = ICO_INPUT_MGR_DEVICE_REGION_REMOVE;
            }
        }
        wl_list_remove(&p->link);
        wl_list_insert(pInputMgr->free_region.prev, &p->link);
    }
    if (delcount > 0)   {
        /* send region delete to haptic device input controller */
        ico_input_send_region_event(&array);
    }
    uifw_trace("ico_input_hook_region_destroy: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_set_input_region: input region set/unset
 *
 * @param[in]   set             set(1)/unset(0)
 * @param[in]   usurf           UIFW surface
 * @param[in]   x               input region X coordinate
 * @param[in]   y               input region X coordinate
 * @param[in]   width           input region width
 * @param[in]   height          input region height
 * @param[in]   hotspot_x       hotspot of X relative coordinate
 * @param[in]   hotspot_y       hotspot of Y relative coordinate
 * @param[in]   cursor_x        cursor region X coordinate
 * @param[in]   cursor_y        cursor region X coordinate
 * @param[in]   cursor_width    cursor region width
 * @param[in]   cursor_height   cursor region height
 * @param[in]   attr            region attributes(currently unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_set_input_region(int set, struct uifw_win_surface *usurf,
                     int32_t x, int32_t y, int32_t width, int32_t height,
                     int32_t hotspot_x, int32_t hotspot_y, int32_t cursor_x, int32_t cursor_y,
                     int32_t cursor_width, int32_t cursor_height, uint32_t attr)
{
    struct uifw_region_mng      *p;         /* input region mamagement table*/
    struct uifw_region_mng      *np;        /* next region mamagement table */
    struct ico_uifw_input_region *rp;       /* input region                 */
    struct wl_array             array;
    int                         i;
    int                         alldel;
    int                         delcount;

    uifw_trace("ico_set_input_region: Enter(%s %d/%d-%d/%d(%d/%d) %d/%d-%d/%d)",
               set ? "Set" : "Unset", x, y, width, height, hotspot_x, hotspot_y,
               cursor_x, cursor_y, cursor_width, cursor_height);

    if (set)    {
        if (wl_list_empty(&pInputMgr->free_region)) {
            p = malloc(sizeof(struct uifw_region_mng) * 50);
            if (! p)    {
                uifw_error("ico_set_input_region: No Memory");
                return;
            }
            memset(p, 0, sizeof(struct uifw_region_mng)*50);
            for (i = 0; i < 50; i++, p++)  {
                wl_list_insert(pInputMgr->free_region.prev, &p->link);
            }
        }
        p = container_of(pInputMgr->free_region.next, struct uifw_region_mng, link);
        wl_list_remove(&p->link);
        p->region.node = usurf->node_tbl->node;
        p->region.surfaceid = usurf->surfaceid;
        p->region.surface_x = usurf->x;
        p->region.surface_y = usurf->y;
        p->region.x = x;
        p->region.y = y;
        p->region.width = width;
        p->region.height = height;
        if ((hotspot_x <= 0) && (hotspot_y <= 0))   {
            p->region.hotspot_x = width / 2;
            p->region.hotspot_y = height / 2;
        }
        else    {
            p->region.hotspot_x = hotspot_x;
            p->region.hotspot_y = hotspot_y;
        }
        if ((cursor_width <= 0) && (cursor_height <= 0))    {
            p->region.cursor_x = x;
            p->region.cursor_y = y;
            p->region.cursor_width = width;
            p->region.cursor_height = height;
        }
        else    {
            p->region.cursor_x = cursor_x;
            p->region.cursor_y = cursor_y;
            p->region.cursor_width = cursor_width;
            p->region.cursor_height = cursor_height;
        }
        p->region.change = /* get form weston_layout */ 1;
        wl_list_insert(usurf->input_region.prev, &p->link);

        /* send input region to haptic device input controller  */
        if (p->region.change > 0)   {
            wl_array_init(&array);
            rp = (struct ico_uifw_input_region *)
                     wl_array_add(&array, sizeof(struct ico_uifw_input_region));
            if (rp) {
                memcpy(rp, &p->region, sizeof(struct ico_uifw_input_region));
                rp->change = ICO_INPUT_MGR_DEVICE_REGION_ADD;
                ico_input_send_region_event(&array);
            }
            uifw_trace("ico_set_input_region: Leave(Set)");
        }
        else    {
            uifw_trace("ico_set_input_region: Leave(Set but Unvisible)");
        }
    }
    else    {
        delcount = 0;

        if ((x <= 0) && (y <= 0) && (width <= 0) && (height <= 0))  {
            alldel = 1;
        }
        else    {
            alldel = 0;
        }

        wl_array_init(&array);

        wl_list_for_each_safe(p, np, &usurf->input_region, link)    {
            if ((alldel != 0) ||
                ((x == p->region.x) && (y == p->region.y) &&
                 (width == p->region.width) && (height == p->region.height)))   {
                if (p->region.change > 0)   {
                    /* visible, send remove event   */
                    rp = (struct ico_uifw_input_region *)
                         wl_array_add(&array, sizeof(struct ico_uifw_input_region));
                    if (rp) {
                        delcount ++;
                        memcpy(rp, &p->region, sizeof(struct ico_uifw_input_region));
                        rp->change = ICO_INPUT_MGR_DEVICE_REGION_REMOVE;
                    }
                }
                wl_list_remove(&p->link);
                wl_list_insert(pInputMgr->free_region.prev, &p->link);
            }
        }
        if (delcount > 0)   {
            /* send region delete to haptic device input controller */
            ico_input_send_region_event(&array);
        }
        uifw_trace("ico_set_input_region: Leave(Unset)");
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_input_send_region_event: send region event to Haptic dic
 *
 * @param[in]   usurf           UIFW surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_input_send_region_event(struct wl_array *array)
{
    struct ico_ictl_mgr     *pIctlMgr;

    wl_list_for_each (pIctlMgr, &pInputMgr->ictl_list, link)   {
        if ((pIctlMgr->type == ICO_INPUT_MGR_DEVICE_TYPE_HAPTIC) &&
            (pIctlMgr->mgr_resource != NULL))   {
            uifw_trace("ico_input_send_region_event: send event to Hapfic");
            ico_input_mgr_device_send_input_regions(pIctlMgr->mgr_resource, array);
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_device_configure_input: configure input device and input switch
 *          from Device Input Controller.
 *
 * @param[in]   client          client(Device Input Controller)
 * @param[in]   resource        resource of request
 * @param[in]   device          device name
 * @param[in]   type            device type(saved but unused)
 * @param[in]   swname          input switch name
 * @param[in]   input           input switch number
 * @param[in]   codename        input code name
 * @param[in]   code            input code number
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_device_configure_input(struct wl_client *client, struct wl_resource *resource,
                           const char *device, int32_t type, const char *swname,
                           int32_t input, const char *codename, int32_t code)
{
    struct ico_ictl_mgr     *pIctlMgr;
    struct ico_ictl_mgr     *psameIctlMgr;
    struct ico_ictl_input   *pInput;
    struct ico_app_mgr      *pAppMgr;

    uifw_trace("ico_device_configure_input: Enter(client=%08x,dev=%s,type=%d,swname=%s,"
               "input=%d,code=%d[%s])", (int)client, device, type,
               swname ? swname : "(NULL)", input, code, codename ? codename : " ");

    pIctlMgr = find_ictlmgr_by_device(device);
    if (! pIctlMgr) {
        /* search binded table      */
        psameIctlMgr = NULL;
        wl_list_for_each (pIctlMgr, &pInputMgr->ictl_list, link)    {
            if (pIctlMgr->client == client) {
                uifw_trace("ico_device_configure_input: set pIctlMgr"
                           "(mgr=%08x,input=%d,dev=%s)", (int)pIctlMgr,
                           input, pIctlMgr->device);
                if (pIctlMgr->device[0] != 0)   {
                    /* save same device         */
                    psameIctlMgr = pIctlMgr;
                }
                else    {
                    /* first device             */
                    strncpy(pIctlMgr->device, device, sizeof(pIctlMgr->device)-1);
                    psameIctlMgr = NULL;
                    break;
                }
            }
        }
        if (psameIctlMgr)   {
            /* client(device input controller) exist, but other device  */
            pIctlMgr = (struct ico_ictl_mgr *)malloc(sizeof(struct ico_ictl_mgr));
            if (pIctlMgr == NULL) {
                uifw_error("ico_device_bind: Leave(No Memory)");
                return;
            }
            memset(pIctlMgr, 0, sizeof(struct ico_ictl_mgr));
            wl_list_init(&pIctlMgr->ico_ictl_input);
            pIctlMgr->client = psameIctlMgr->client;
            pIctlMgr->mgr_resource = psameIctlMgr->mgr_resource;

            wl_list_insert(pInputMgr->ictl_list.prev, &pIctlMgr->link);
        }
    }

    if (type)   {
        pIctlMgr->type = type;
    }

    /* search and add input switch  */
    wl_list_for_each (pInput, &pIctlMgr->ico_ictl_input, link)  {
        if (pInput->input == input)     break;
    }
    if (&pInput->link == &pIctlMgr->ico_ictl_input)    {
        uifw_trace("ico_device_configure_input: create %s.%s(%d) switch",
                   device, swname, input);
        pInput = (struct ico_ictl_input *)malloc(sizeof(struct ico_ictl_input));
        if (pInput == NULL) {
            uifw_error("ico_device_configure_input: Leave(No Memory)");
            return;
        }
        memset(pInput, 0, sizeof(struct ico_ictl_input));
        if (swname) {
            strncpy(pInput->swname, swname, sizeof(pInput->swname)-1);
        }
        else    {
            strcpy(pInput->swname, "(Unknown)");
        }
        wl_list_insert(pIctlMgr->ico_ictl_input.prev, &pInput->link);
    }
    if (swname) {
        strncpy(pInput->swname, swname, sizeof(pInput->swname)-1);
    }
    pInput->input = input;
    memset(pInput->code, 0, sizeof(pInput->code));
    pInput->ncode = 1;
    pInput->code[0].code = code;
    if (codename)   {
        strncpy(pInput->code[0].name, codename, sizeof(pInput->code[0].name)-1);
    }

    if (client == NULL) {
        /* internal call for table create   */
        uifw_trace("ico_device_configure_input: Leave(table create)");
        return;
    }
    pIctlMgr->client = client;

    /* send to application and manager(ex.HomeScreen)   */
    wl_list_for_each (pAppMgr, &pInputMgr->app_list, link)  {
        if (pAppMgr->resource == NULL)  continue;
        if ((pInput->app != NULL) && (pInput->app != pAppMgr) && (pInput->fix)) continue;

        uifw_trace("ico_device_configure_input: send capabilities to app(%s) %s.%s[%d]",
                   pAppMgr->appid, device, pInput->swname, input);
        ico_exinput_send_capabilities(pAppMgr->resource, device, pIctlMgr->type,
                                      pInput->swname, input,
                                      pInput->code[0].name, pInput->code[0].code);
    }
    uifw_trace("ico_device_configure_input: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_device_configure_code: add input switch from Device Input Controller.
 *
 * @param[in]   client          client(Device Input Controller)
 * @param[in]   resource        resource of request
 * @param[in]   device          device name
 * @param[in]   input           input switch number
 * @param[in]   codename        input code name
 * @param[in]   code            input code number
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_device_configure_code(struct wl_client *client, struct wl_resource *resource,
                          const char *device, int32_t input,
                          const char *codename, int32_t code)
{
    int     i;
    struct ico_ictl_mgr     *pIctlMgr;
    struct ico_ictl_input   *pInput;
    struct ico_app_mgr      *pAppMgr;

    uifw_trace("ico_device_configure_code: Enter(client=%08x,dev=%s,input=%d,code=%d[%s])",
               (int)client, device, input, code, codename ? codename : " ");

    pIctlMgr = find_ictlmgr_by_device(device);
    if (! pIctlMgr) {
        uifw_warn("ico_device_configure_code: Leave(dev=%s dose not exist)", device);
        return;
    }
    /* search input switch      */
    wl_list_for_each (pInput, &pIctlMgr->ico_ictl_input, link)  {
        if (pInput->input == input)     break;
    }
    if (&pInput->link == &pIctlMgr->ico_ictl_input)    {
        uifw_warn("ico_device_configure_code: Leave(input=%s.%d dose not exist)",
                  device, input);
        return;
    }

    /* search input code        */
    for (i = 0; i < pInput->ncode; i++) {
        if (pInput->code[i].code == code)   break;
    }
    if (i >= pInput->ncode) {
        /* code dose not exist, add */
        if (pInput->ncode >= ICO_MINPUT_MAX_CODES) {
            uifw_warn("ico_device_configure_code: Leave(input=%s.%d code overflow)",
                      device, input);
            return;
        }
        i = pInput->ncode;
        pInput->ncode ++;
        pInput->code[i].code = code;
    }
    memset(pInput->code[i].name, 0, sizeof(pInput->code[i].name));
    strncpy(pInput->code[i].name, codename, sizeof(pInput->code[i].name)-1);

    /* send to application and manager(ex.HomeScreen)   */
    wl_list_for_each (pAppMgr, &pInputMgr->app_list, link)  {
        if (pAppMgr->resource == NULL)  continue;
        if ((pInput->app != NULL) && (pInput->app != pAppMgr) && (pInput->fix)) continue;
        uifw_trace("ico_device_configure_input: send code to app(%s) %s.%s[%d]",
                   pAppMgr->appid, device, pInput->swname, input);
        ico_exinput_send_code(pAppMgr->resource, device, input,
                              pInput->code[i].name, pInput->code[i].code);
    }
    uifw_trace("ico_device_configure_code: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_device_input_event: device input event from Device Input Controller.
 *
 * @param[in]   client          client(Device Input Controller)
 * @param[in]   resource        resource of request
 * @param[in]   time            device input time(miri-sec)
 * @param[in]   device          device name
 * @param[in]   input           input switch number
 * @param[in]   code            input code number
 * @param[in]   state           input state(1=On, 0=Off)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_device_input_event(struct wl_client *client, struct wl_resource *resource,
                       uint32_t time, const char *device,
                       int32_t input, int32_t code, int32_t state)
{
    struct ico_ictl_mgr     *pIctlMgr;
    struct ico_ictl_input   *pInput;

    uifw_trace("ico_device_input_event: Enter(time=%d,dev=%s,input=%d,code=%d,state=%d)",
               time, device, input, code, state);

    /* find input devcie by client      */
    pIctlMgr = find_ictlmgr_by_device(device);
    if (! pIctlMgr) {
        uifw_error("ico_device_input_event: Leave(Unknown device(%s))", device);
        return;
    }
    /* find input switch by input Id    */
    pInput = find_ictlinput_by_input(pIctlMgr, input);
    if (! pInput) {
        uifw_warn("ico_device_input_event: Leave(Unknown input(%s,%d))",
                  pIctlMgr->device, input);
        return;
    }

    if (! pInput->app)  {
        uifw_trace("ico_device_input_event: Leave(%s.%s not assign)",
                  pIctlMgr->device, pInput->swname);
        return;
    }
    if (! pInput->app->resource) {
        uifw_trace("ico_device_input_event: Leave(%s.%s assigned App.%s "
                   "is not running or not interested)",
                   pIctlMgr->device, pInput->swname, pInput->app->appid);
        return;
    }

    /* send event to application        */
    uifw_trace("ico_device_input_event: send event=%s.%s[%d],%d,%d to App.%s",
               pIctlMgr->device, pInput->swname, input, code, state, pInput->app->appid);
    ico_exinput_send_input(pInput->app->resource, time, pIctlMgr->device,
                           input, code, state);

    uifw_trace("ico_device_input_event: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_control_bind: ico_input_mgr_control bind from HomeScreen
 *
 * @param[in]   client          client(HomeScreen)
 * @param[in]   data            data(unused)
 * @param[in]   version         protocol version(unused)
 * @param[in]   id              client object id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_control_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    char                    *appid;
    struct ico_app_mgr      *pAppMgr;

    uifw_trace("ico_control_bind: Enter(client=%08x)", (int)client);
    appid = ico_window_mgr_get_appid(client);

    if (! appid)    {
        /* client dose not exist        */
        uifw_warn("ico_control_bind: Leave(client=%08x dose not exist)", (int)client);
        return;
    }

    /* find application         */
    pAppMgr = find_app_by_appid(appid);
    if (! pAppMgr)  {
        /* create Application Management Table  */
        pAppMgr = (struct ico_app_mgr *)malloc(sizeof(struct ico_app_mgr));
        if (! pAppMgr)  {
            uifw_error("ico_control_bind: Leave(No Memory)");
            return;
        }
        memset(pAppMgr, 0, sizeof(struct ico_app_mgr));
        strncpy(pAppMgr->appid, appid, sizeof(pAppMgr->appid)-1);
        wl_list_insert(pInputMgr->app_list.prev, &pAppMgr->link);
    }
    pAppMgr->client = client;
    if (! pAppMgr->mgr_resource)    {
        pAppMgr->mgr_resource = wl_resource_create(client,
                                                   &ico_input_mgr_control_interface, 1, id);
        if (pAppMgr->mgr_resource)  {
            wl_resource_set_implementation(pAppMgr->mgr_resource,
                                           &ico_input_mgr_implementation,
                                           pInputMgr, ico_control_unbind);
        }
    }
    uifw_trace("ico_control_bind: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_control_unbind: ico_input_mgr_control unbind from HomeScreen
 *
 * @param[in]   resource        client resource(HomeScreen)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_control_unbind(struct wl_resource *resource)
{
    struct ico_app_mgr  *pAppMgr;

    uifw_trace("ico_control_unbind: Enter(resource=%08x)", (int)resource);

    wl_list_for_each (pAppMgr, &pInputMgr->app_list, link)  {
        if (pAppMgr->mgr_resource == resource)  {
            uifw_trace("ico_control_unbind: find app.%s", pAppMgr->appid);
            pAppMgr->mgr_resource = NULL;
            break;
        }
    }
    uifw_trace("ico_control_unbind: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_device_bind: ico_input_mgr_device bind from Device Input Controller
 *
 * @param[in]   client          client(Device Input Controller)
 * @param[in]   data            data(unused)
 * @param[in]   version         protocol version
 * @param[in]   id              client object id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_device_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct ico_ictl_mgr *pIctlMgr;
    struct wl_resource  *mgr_resource;

    uifw_trace("ico_device_bind: Enter(client=%08x)", (int)client);

    /* create ictl mgr table */
    pIctlMgr = (struct ico_ictl_mgr *)malloc(sizeof(struct ico_ictl_mgr));
    if (pIctlMgr == NULL) {
        uifw_error("ico_device_bind: Leave(No Memory)");
        return;
    }
    memset(pIctlMgr, 0, sizeof(struct ico_ictl_mgr));
    wl_list_init(&pIctlMgr->ico_ictl_input);
    pIctlMgr->client = client;

    /* add list */
    wl_list_insert(pInputMgr->ictl_list.prev, &pIctlMgr->link);

    mgr_resource = wl_resource_create(client, &ico_input_mgr_device_interface, 1, id);
    if (mgr_resource)   {
        pIctlMgr->mgr_resource = mgr_resource;
        wl_resource_set_implementation(mgr_resource, &input_mgr_ictl_implementation,
                                       pIctlMgr, ico_device_unbind);
    }
    uifw_trace("ico_device_bind: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_device_unbind: ico_input_mgr_device unbind from Device Input Controller
 *
 * @param[in]   resource        client resource(Device Input Controller)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_device_unbind(struct wl_resource *resource)
{
    uifw_trace("ico_device_unbind: Enter(resource=%08x)", (int)resource);
    uifw_trace("ico_device_unbind: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_exinput_bind: ico_exinput bind from Application
 *
 * @param[in]   client          client(Application)
 * @param[in]   data            data(unused)
 * @param[in]   version         protocol version(unused)
 * @param[in]   id              client object id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_exinput_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    int                     i;
    char                    *appid;
    struct ico_app_mgr      *pAppMgr;
    struct ico_ictl_mgr     *pIctlMgr;
    struct ico_ictl_input   *pInput;

    appid = ico_window_mgr_get_appid(client);
    uifw_trace("ico_exinput_bind: Enter(client=%08x,%s)", (int)client,
               appid ? appid : "(NULL)");

    if (! appid)    {
        /* client dose not exist        */
        uifw_warn("ico_exinput_bind: Leave(client=%08x dose not exist)", (int)client);
        return;
    }

    /* find application         */
    pAppMgr = find_app_by_appid(appid);
    if (! pAppMgr)  {
        /* create Application Management Table  */
        pAppMgr = (struct ico_app_mgr *)malloc(sizeof(struct ico_app_mgr));
        if (! pAppMgr)  {
            uifw_error("ico_exinput_bind: Leave(No Memory)");
            return;
        }
        memset(pAppMgr, 0, sizeof(struct ico_app_mgr));
        strncpy(pAppMgr->appid, appid, sizeof(pAppMgr->appid)-1);
        wl_list_insert(pInputMgr->app_list.prev, &pAppMgr->link);
        uifw_trace("ico_exinput_bind: Create App.%s table", appid);
    }
    pAppMgr->client = client;
    if (! pAppMgr->resource)    {
        pAppMgr->resource = wl_resource_create(client, &ico_exinput_interface, 1, id);
        if (pAppMgr->resource)  {
            wl_resource_set_implementation(pAppMgr->resource, &ico_exinput_implementation,
                                           pInputMgr, ico_exinput_unbind);
        }
    }

    /* send all capabilities    */
    wl_list_for_each (pIctlMgr, &pInputMgr->ictl_list, link)    {
        if (pIctlMgr->client == NULL)   {
            uifw_trace("ico_exinput_bind: Input controller.%s not initialized",
                       pIctlMgr->device);
            continue;
        }

        wl_list_for_each (pInput, &pIctlMgr->ico_ictl_input, link)  {
            if (pInput->swname[0] == 0) {
                uifw_trace("ico_exinput_bind: Input %s not initialized", pIctlMgr->device);
                continue;
            }
            if ((pInput->app != NULL) && (pInput->app != pAppMgr) && (pInput->fix)) {
                uifw_trace("ico_exinput_bind: Input %s.%s fixed assign to App.%s",
                           pIctlMgr->device, pInput->swname, pInput->app->appid);
                continue;
            }
            uifw_trace("ico_exinput_bind: send capabilities to app(%s) %s.%s[%d]",
                       pAppMgr->appid, pIctlMgr->device, pInput->swname, pInput->input);
            ico_exinput_send_capabilities(pAppMgr->resource, pIctlMgr->device,
                                          pIctlMgr->type, pInput->swname, pInput->input,
                                          pInput->code[0].name, pInput->code[0].code);
            for (i = 1; i < pInput->ncode; i++) {
                ico_exinput_send_code(pAppMgr->resource, pIctlMgr->device, pInput->input,
                                      pInput->code[i].name, pInput->code[i].code);
            }
        }
    }
    uifw_trace("ico_exinput_bind: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_exinput_unbind: ico_exinput unbind from Application
 *
 * @param[in]   resource        client resource(Application)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_exinput_unbind(struct wl_resource *resource)
{
    struct ico_app_mgr      *pAppMgr;
    struct ico_app_mgr      *pAppMgrTmp;
    struct ico_ictl_mgr     *pIctlMgr;
    struct ico_ictl_input   *pInput;
    int                     fix = 0;

    uifw_trace("ico_exinput_unbind: Enter(resource=%08x)", (int)resource);

    wl_list_for_each_safe (pAppMgr, pAppMgrTmp, &pInputMgr->app_list, link) {
        if (pAppMgr->resource == resource)  {
            uifw_trace("ico_exinput_unbind: find app.%s", pAppMgr->appid);

            /* release application from input switch    */
            wl_list_for_each (pIctlMgr, &pInputMgr->ictl_list, link)    {
                wl_list_for_each (pInput, &pIctlMgr->ico_ictl_input, link)  {
                    if (pInput->app == pAppMgr) {
                        if (pInput->fix == 0)   {
                            uifw_trace("ico_exinput_unbind: app.%s remove %s.%s",
                                       pAppMgr->appid, pIctlMgr->device, pInput->swname);
                            pInput->app = NULL;
                        }
                        else    {
                            uifw_trace("ico_exinput_unbind: app.%s fix assign %s.%s",
                                       pAppMgr->appid, pIctlMgr->device, pInput->swname);
                            fix ++;
                        }
                    }
                }
            }
            if (fix == 0)   {
                wl_list_remove(&pAppMgr->link);
                free(pAppMgr);
            }
            else    {
                pAppMgr->client = NULL;
                pAppMgr->resource = NULL;
            }
        }
    }
    uifw_trace("ico_exinput_unbind: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   find_ictlmgr_by_device: find Input Controller by device name
 *
 * @param[in]   device          device name
 * @return      Input Controller Manager table address
 * @retval      !=NULL          address
 * @retval      ==NULL          not exist
 */
/*--------------------------------------------------------------------------*/
static struct ico_ictl_mgr *
find_ictlmgr_by_device(const char *device)
{
    struct ico_ictl_mgr     *pIctlMgr;

    wl_list_for_each (pIctlMgr, &pInputMgr->ictl_list, link)    {
        uifw_debug("find_ictlmgr_by_device: <%s> vs <%s>", device, pIctlMgr->device);
        if (strcmp(pIctlMgr->device, device) == 0)  {
            return pIctlMgr;
        }
    }
    return NULL;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   find_ictlinput_by_input: find Input Switch by input Id
 *
 * @param[in]   pIctlMgr        Input Controller device
 * @param[in]   input           Input Id
 * @return      Input Switch table address
 * @retval      !=NULL          address
 * @retval      ==NULL          not exist
 */
/*--------------------------------------------------------------------------*/
static struct ico_ictl_input *
find_ictlinput_by_input(struct ico_ictl_mgr *pIctlMgr, const int32_t input)
{
    struct ico_ictl_input   *pInput;

    wl_list_for_each (pInput, &pIctlMgr->ico_ictl_input, link)  {
        if (pInput->input == input) {
            return pInput;
        }
    }
    return NULL;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   find_app_by_appid: find Application by application Id
 *
 * @param[in]   appid           application Id
 * @return      Application Management table address
 * @retval      !=NULL          address
 * @retval      ==NULL          not exist
 */
/*--------------------------------------------------------------------------*/
static struct ico_app_mgr *
find_app_by_appid(const char *appid)
{
    struct ico_app_mgr      *pAppMgr;

    wl_list_for_each (pAppMgr, &pInputMgr->app_list, link)  {
        if (strcmp(pAppMgr->appid, appid) == 0) {
            return pAppMgr;
        }
    }
    return NULL;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   module_init: initialization of this plugin
 *
 * @param[in]   ec          weston compositor
 * @param[in]   argc        number of arguments(unused)
 * @param[in]   argv        argument list(unused)
 * @return      result
 * @retval      0           OK
 * @retval      -1          error
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT int
module_init(struct weston_compositor *ec, int *argc, char *argv[])
{
    struct uifw_region_mng  *p;
    int     i;

    uifw_info("ico_input_mgr: Enter(module_init)");

    /* initialize management table */
    pInputMgr = (struct ico_input_mgr *)malloc(sizeof(struct ico_input_mgr));
    if (pInputMgr == NULL) {
        uifw_trace("ico_input_mgr: malloc failed");
        return -1;
    }
    memset(pInputMgr, 0, sizeof(struct ico_input_mgr));
    pInputMgr->compositor = ec;

    /* interface to desktop manager(ex.HomeScreen)  */
    if (wl_global_create(ec->wl_display, &ico_input_mgr_control_interface, 1,
                         pInputMgr, ico_control_bind) == NULL) {
        uifw_trace("ico_input_mgr: wl_global_create mgr failed");
        return -1;
    }

    /* interface to Input Controller(ictl) */
    if (wl_global_create(ec->wl_display, &ico_input_mgr_device_interface, 1,
                         pInputMgr, ico_device_bind) == NULL) {
        uifw_trace("ico_input_mgr: wl_global_create ictl failed");
        return -1;
    }

    /* interface to App(exinput) */
    if (wl_global_create(ec->wl_display, &ico_exinput_interface, 1,
                         pInputMgr, ico_exinput_bind) == NULL) {
        uifw_trace("ico_input_mgr: wl_global_create exseat failed");
        return -1;
    }

    /* initialize list */
    wl_list_init(&pInputMgr->ictl_list);
    wl_list_init(&pInputMgr->app_list);
    wl_list_init(&pInputMgr->free_region);
    p = malloc(sizeof(struct uifw_region_mng)*100);
    if (p)  {
        memset(p, 0, sizeof(struct uifw_region_mng)*100);
        for (i = 0; i < 100; i++, p++)  {
            wl_list_insert(pInputMgr->free_region.prev, &p->link);
        }
    }

    /* found input seat */
    pInputMgr->seat = container_of(ec->seat_list.next, struct weston_seat, link);

    /* set hook for input region control    */
    ico_window_mgr_set_hook_change(ico_input_hook_region_change);
    ico_window_mgr_set_hook_destory(ico_input_hook_region_destroy);
    ico_window_mgr_set_hook_inputregion(ico_set_input_region);

    uifw_info("ico_input_mgr: Leave(module_init)");
    return 0;
}
