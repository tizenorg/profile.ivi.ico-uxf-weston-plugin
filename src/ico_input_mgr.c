/*
 * Copyright © 2010-2011 Intel Corporation
 * Copyright © 2008-2011 Kristian Høgsberg
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
 * @brief   Multi Input Manager (Weston(Wayland) PlugIn)
 *
 * @date    Feb-08-2013
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

#include <wayland-server.h>
#include <weston/compositor.h>
#include "ico_ivi_common.h"
#include "ico_ivi_shell.h"
#include "ico_window_mgr.h"
#include "ico_input_mgr-server-protocol.h"

/* degine maximum length                */
#define ICO_MINPUT_DEVICE_LEN           32
#define ICO_MINPUT_SW_LEN               20
#define ICO_MINPUT_MAX_CODES            20

/* structure definition */
/* working table of Multi Input Manager */
struct ico_input_mgr {
    struct weston_compositor *compositor;   /* Weston Compositor                    */
    struct wl_list  ictl_list;              /* Input Controller List                */
    struct wl_list  app_list;               /* application List                     */
    struct wl_resource *inputmgr;
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
    struct wl_resource  *resource;          /* resource                             */
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

/* prototype of static function */
/* bind input manager form manager(ex.HomeScreen)   */
static void ico_control_bind(struct wl_client *client, void *data,
                             uint32_t version, uint32_t id);
/* unbind input manager form manager(ex.HomeScreen) */
static void ico_control_unbind(struct wl_resource *resource);
/* bind input manager form input controller         */
static void ico_device_bind(struct wl_client *client, void *data,
                            uint32_t version, uint32_t id);
/* unbind input manager form input controller       */
static void ico_device_unbind(struct wl_resource *resource);
/* bind input manager (form application)            */
static void ico_exinput_bind(struct wl_client *client, void *data,
                             uint32_t version, uint32_t id);
/* unbind input manager (form application)          */
static void ico_exinput_unbind(struct wl_resource *resource);

/* find ictl manager by device name */
static struct ico_ictl_mgr *find_ictlmgr_by_device(const char *device);
/* find ictl input switch by input Id */
static struct ico_ictl_input *find_ictlinput_by_input(struct ico_ictl_mgr *pIctlMgr,
                                                      const int32_t input);
/* find app manager by application Id */
static struct ico_app_mgr *find_app_by_appid(const char *appid);
/* add input event to application     */
static void ico_mgr_add_input_app(struct wl_client *client, struct wl_resource *resource,
                                  const char *appid, const char *device, int32_t input,
                                  uint32_t fix);
/* delete input event to application  */
static void ico_mgr_del_input_app(struct wl_client *client, struct wl_resource *resource,
                                  const char *appid, const char *device, int32_t input);
/* create and regist Input Controller table */
static void ico_device_configure_input(struct wl_client *client,
                                       struct wl_resource *resource, const char *device,
                                       int32_t type, const char *swname, int32_t input,
                                       const char *codename, int32_t code);
/* add input to from Input Controller table */
static void ico_device_configure_code(struct wl_client *client,
                                      struct wl_resource *resource, const char *device,
                                      int32_t input, const char *codename, int32_t code);
/* device input event                       */
static void ico_device_input_event(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t time, const char *device,
                                   int32_t input, int32_t code, int32_t state);

/* entry finction called by Weston  */
WL_EXPORT int module_init(struct weston_compositor *ec);

/* definition of Wayland protocol */
/* mgr interface */
static const struct ico_input_mgr_control_interface ico_input_mgr_implementation = {
    ico_mgr_add_input_app,
    ico_mgr_del_input_app,
};

/* Input Controller interface */
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
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_mgr_add_input_app(struct wl_client *client, struct wl_resource *resource,
                      const char *appid, const char *device, int32_t input, uint32_t fix)
{
    uifw_trace("ico_mgr_add_input_app: Enter(appid=%s,dev=%s,input=%d,fix=%d)",
               appid, device, input, fix);

    struct ico_ictl_mgr     *pIctlMgr;
    struct ico_ictl_input   *pInput;
    struct ico_app_mgr      *pAppMgr;

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
    uifw_trace("ico_mgr_del_input_app: Enter(appid=%s,dev=%s,input=%d)",
               appid ? appid : "(NULL)", device ? device : "(NULL)", input);

    int     alldev = 0;
    struct ico_ictl_mgr     *pIctlMgr = NULL;
    struct ico_ictl_input   *pInput = NULL;
    struct ico_app_mgr      *pAppMgr;

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
    uifw_trace("ico_device_configure_input: Enter(client=%08x,dev=%s,type=%d,swname=%s,"
               "input=%d,code=%d[%s])", (int)client, device, type,
               swname ? swname : "(NULL)", input, code, codename ? codename : " ");

    struct ico_ictl_mgr     *pIctlMgr;
    struct ico_ictl_input   *pInput;
    struct ico_app_mgr      *pAppMgr;

    pIctlMgr = find_ictlmgr_by_device(device);
    if (! pIctlMgr) {
        /* create ictl mgr table */
        pIctlMgr = (struct ico_ictl_mgr *)malloc(sizeof(struct ico_ictl_mgr));
        if (pIctlMgr == NULL) {
            uifw_error("ico_device_configure_input: Leave(No Memory)");
            return;
        }
        uifw_trace("ico_device_configure_input: create pIctlMgr(mgr=%08x,input=%d)",
                   (int)pIctlMgr, input);
        memset(pIctlMgr, 0, sizeof(struct ico_ictl_mgr));
        wl_list_init(&pIctlMgr->ico_ictl_input);
        strncpy(pIctlMgr->device, device, sizeof(pIctlMgr->device)-1);

        /* add list */
        wl_list_insert(pInputMgr->ictl_list.prev, &pIctlMgr->link);
    }
    pIctlMgr->client = client;
    pIctlMgr->resource = resource;
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
    uifw_trace("ico_device_configure_code: Enter(client=%08x,dev=%s,input=%d,code=%d[%s])",
               (int)client, device, input, code, codename ? codename : " ");

    int     i;
    struct ico_ictl_mgr     *pIctlMgr;
    struct ico_ictl_input   *pInput;
    struct ico_app_mgr      *pAppMgr;

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
    uifw_trace("ico_device_input_event: Enter(time=%d,dev=%s,input=%d,code=%d,state=%d)",
               time, device, input, code, state);

    struct ico_ictl_mgr     *pIctlMgr;
    struct ico_ictl_input   *pInput;

    /* find input devcie by client      */
    pIctlMgr = find_ictlmgr_by_device(device);
    if (! pIctlMgr) {
        uifw_error("ico_device_input_event: Leave(Unknown client(%08x))", (int)client);
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
    appid = ico_window_mgr_appid(client);

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
        pAppMgr->mgr_resource = wl_client_add_object(client,
                                                     &ico_input_mgr_control_interface,
                                                     &ico_input_mgr_implementation,
                                                     id, pInputMgr);
        pAppMgr->mgr_resource->destroy = ico_control_unbind;
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
    struct ico_app_mgr      *pAppMgr;

    uifw_trace("ico_control_unbind: Enter(resource=%08x)", (int)resource);

    wl_list_for_each (pAppMgr, &pInputMgr->app_list, link)  {
        if (pAppMgr->mgr_resource == resource)  {
            uifw_trace("ico_control_unbind: find app.%s", pAppMgr->appid);
            pAppMgr->mgr_resource = NULL;
            break;
        }
    }

    free(resource);
    uifw_trace("ico_control_unbind: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_device_bind: ico_input_mgr_device bind from Device Input Controller
 *
 * @param[in]   client          client(Device Input Controller)
 * @param[in]   data            data(unused)
 * @param[in]   version         protocol version(unused)
 * @param[in]   id              client object id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_device_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *mgr_resource;

    uifw_trace("ico_device_bind: Enter(client=%08x)", (int)client);

    mgr_resource = wl_client_add_object(client, &ico_input_mgr_device_interface,
                                        &input_mgr_ictl_implementation,
                                        id, NULL);
    mgr_resource->destroy = ico_device_unbind;
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
    free(resource);
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

    appid = ico_window_mgr_appid(client);
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
        pAppMgr->resource = wl_client_add_object(client, &ico_exinput_interface,
                                                 NULL, id, pInputMgr);
        pAppMgr->resource->destroy = ico_exinput_unbind;
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

    free(resource);
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
 * @return      result
 * @retval      0           OK
 * @retval      -1          error
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT int
module_init(struct weston_compositor *ec)
{
    uifw_trace("ico_input_mgr: Enter(module_init)");

    /* initialize management table */
    pInputMgr = (struct ico_input_mgr *)malloc(sizeof(struct ico_input_mgr));
    if (pInputMgr == NULL) {
        uifw_trace("ico_input_mgr: malloc failed");
        return -1;
    }
    memset(pInputMgr, 0, sizeof(struct ico_input_mgr));
    pInputMgr->compositor = ec;

    /* interface to desktop manager(ex.HomeScreen)  */
    if (wl_display_add_global(ec->wl_display,
                              &ico_input_mgr_control_interface,
                              pInputMgr,
                              ico_control_bind) == NULL) {
        uifw_trace("ico_input_mgr: wl_display_add_global mgr failed");
        return -1;
    }

    /* interface to Input Controller(ictl) */
    if (wl_display_add_global(ec->wl_display,
                              &ico_input_mgr_device_interface,
                              pInputMgr,
                              ico_device_bind) == NULL) {
        uifw_trace("ico_input_mgr: wl_display_add_global ictl failed");
        return -1;
    }

    /* interface to App(exinput) */
    if (wl_display_add_global(ec->wl_display, &ico_exinput_interface,
                              pInputMgr, ico_exinput_bind) == NULL) {
        uifw_trace("ico_input_mgr: wl_display_add_global exseat failed");
        return -1;
    }

    /* initialize list */
    wl_list_init(&pInputMgr->ictl_list);
    wl_list_init(&pInputMgr->app_list);

    uifw_trace("ico_input_mgr: Leave(module_init)");
    return 0;
}

