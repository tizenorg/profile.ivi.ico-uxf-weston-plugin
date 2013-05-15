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
 * @brief   Multi Window Manager (Weston(Wayland) PlugIn)
 *
 * @date    Feb-08-2013
 */

#define _GNU_SOURCE

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
#include <wayland-server.h>
#include <aul/aul.h>
#include <bundle.h>

#include <weston/compositor.h>
#include "ico_ivi_common.h"
#include "ico_ivi_shell.h"
#include "ico_window_mgr.h"
#include "ico_ivi_shell-server-protocol.h"
#include "ico_window_mgr-server-protocol.h"

/* SurfaceID                        */
#define INIT_SURFACE_IDS    1024            /* SurfaceId table initiale size        */
#define ADD_SURFACE_IDS     512             /* SurfaceId table additional size      */
#define SURCAFE_ID_MASK     0x0ffff         /* SurfaceId bit mask pattern           */
#define UIFW_HASH    64                     /* Hash value (2's compliment)          */

/* Cleint management table          */
struct uifw_client  {
    struct wl_client *client;               /* Wayland client                       */
    int     pid;                            /* ProcessId (pid)                      */
    char    appid[ICO_IVI_APPID_LENGTH];    /* ApplicationId(from AppCore AUL)      */
    int     manager;                        /* Manager flag (Need send event)       */
    struct wl_resource *resource;
    struct wl_list  link;
};

/* UIFW Surface                     */
struct shell_surface;
struct uifw_win_surface {
    uint32_t id;                            /* UIFW SurfaceId                       */
    int     layer;                          /* LayerId                              */
    struct weston_surface *surface;         /* Weston surface                       */
    struct shell_surface  *shsurf;          /* Shell(IVI-Shell) surface             */
    struct uifw_client    *uclient;         /* Client                               */
    int     x;                              /* X-axis                               */
    int     y;                              /* Y-axis                               */
    int     width;                          /* Width                                */
    int     height;                         /* Height                               */
    int     transition;                     /* Transition                           */
    struct wl_list link;                    /*                                      */
    struct uifw_win_surface *next_idhash;   /* UIFW SurfaceId hash list             */
    struct uifw_win_surface *next_wshash;   /* Weston SurfaceId hash list           */
};

/* Manager table                    */
struct uifw_manager {
    struct wl_resource *resource;           /* Manager resource                     */
    int     eventcb;                        /* Event send flag                      */
    struct wl_list link;                    /* link to next manager                 */
};

/* Multi Windiw Manager                           */
struct ico_win_mgr {
    struct weston_compositor *compositor;   /* Weston compositor                    */
    int32_t surface_head;                   /* (HostID << 24) | (DisplayNo << 16)   */

    struct wl_list  client_list;            /* Clients                              */
    struct wl_list  manager_list;           /* Manager(ex.HomeScreen) list          */
    int             num_manager;            /* Number of managers                   */
    struct wl_list  surface_list;           /* Surface list                         */
    struct uifw_win_surface *active_pointer_surface;    /* Active Pointer Surface   */
    struct uifw_win_surface *active_keyboard_surface;   /* Active Keyboard Surface  */

    struct uifw_win_surface *idhash[UIFW_HASH];  /* UIFW SerfaceID                  */
    struct uifw_win_surface *wshash[UIFW_HASH];  /* Weston Surface                  */

    uint32_t surfaceid_count;               /* Number of surface id                 */
    uint32_t surfaceid_max;                 /* Maximum number of surface id         */
    uint16_t *surfaceid_map;                /* SurfaceId assign bit map             */
};

/* Internal macros                      */
/* UIFW SurfaceID                       */
#define MAKE_IDHASH(v)  (((uint32_t)v) & (UIFW_HASH-1))
/* Weston Surface                       */
#define MAKE_WSHASH(v)  ((((uint32_t)v) >> 5) & (UIFW_HASH-1))

/* function prototype                   */
                                            /* weston compositor interface          */
int module_init(struct weston_compositor *ec);
                                            /* get surface table from surfece id    */
static struct uifw_win_surface* find_uifw_win_surface_by_id(uint32_t surfaceid);
                                            /* get surface table from weston surface*/
static struct uifw_win_surface* find_uifw_win_surface_by_ws(
                    struct weston_surface *wsurf);
                                            /* get client table from weston client  */
static struct uifw_client* find_client_from_client(struct wl_client* client);
                                            /* assign new surface id                */
static uint32_t generate_id(void);
                                            /* bind shell client                    */
static void bind_shell_client(struct wl_client *client);
                                            /* unind shell client                   */
static void unbind_shell_client(struct wl_client *client);
                                            /* create new surface                   */
static void client_register_surface(
                    struct wl_client *client, struct wl_resource *resource,
                    struct weston_surface *surface, struct shell_surface *shsurf);
                                            /* map new surface                      */
static void win_mgr_map_surface(struct weston_surface *surface, int32_t *width,
                                int32_t *height, int32_t *sx, int32_t *sy);
                                            /* set applicationId for RemoteUI       */
static void uifw_set_user(struct wl_client *client, struct wl_resource *resource,
                          int pid, const char *appid);
                                            /* set/reset event flag                 */
static void uifw_set_eventcb(struct wl_client *client, struct wl_resource *resource,
                             int eventcb);
                                            /* set window layer                     */
static void uifw_set_window_layer(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t surfaceid, int layer);
                                            /* set surface size and position        */
static void uifw_set_positionsize(struct wl_client *client,
                                  struct wl_resource *resource, uint32_t surfaceid,
                                  int32_t x, int32_t y, int32_t width, int32_t height);
                                            /* show/hide and raise/lower surface    */
static void uifw_set_visible(struct wl_client *client, struct wl_resource *resource,
                             uint32_t surfaceid, int32_t visible, int32_t raise);
                                            /* set surface transition               */
static void uifw_set_transition(struct wl_client *client, struct wl_resource *resource,
                                uint32_t surfaceid, int32_t transition);
                                            /* set active surface (form HomeScreen) */
static void uifw_set_active(struct wl_client *client, struct wl_resource *resource,
                            uint32_t surfaceid, uint32_t target);
                                            /* layer visibility control             */
static void uifw_set_layer_visible(struct wl_client *client, struct wl_resource *resource,
                                   int32_t layer, int32_t visible);
                                            /* send surface change event to manager */
static void win_mgr_surface_change(struct weston_surface *surface,
                                   const int to, const int manager);
                                            /* surface change from manager          */
static int win_mgr_surface_change_mgr(struct weston_surface *surface, const int x,
                                      const int y, const int width, const int height);
                                            /* surface destory                      */
static void win_mgr_surface_destroy(struct weston_surface *surface);
                                            /* bind manager                         */
static void bind_ico_win_mgr(struct wl_client *client,
                             void *data, uint32_t version, uint32_t id);
                                            /* unbind manager                       */
static void unbind_ico_win_mgr(struct wl_resource *resource);
                                            /* convert surfaceId to nodeId          */
static int ico_winmgr_usurf_2_node(const int surfaceid);
                                            /* send event to manager                */
static int ico_win_mgr_send_to_mgr(const int event, const int surfaceid,
                                   const char *appid, const int param1,
                                   const int param2, const int param3, const int param4,
                                   const int param5, const int param6);
                                            /* hook for set user                    */
static void (*win_mgr_hook_set_user)
                (struct wl_client *client, const char *appid) = NULL;
                                            /* hook for surface create              */
static void (*win_mgr_hook_create)
                (struct wl_client *client, struct weston_surface *surface,
                 int surfaceId, const char *appid) = NULL;
                                            /* hook for surface destory             */
static void (*win_mgr_hook_destroy)(struct weston_surface *surface) = NULL;

/* static tables                        */
/* Multi Window Manager interface       */
static const struct ico_window_mgr_interface ico_window_mgr_implementation = {
    uifw_set_user,
    uifw_set_eventcb,
    uifw_set_window_layer,
    uifw_set_positionsize,
    uifw_set_visible,
    uifw_set_transition,
    uifw_set_active,
    uifw_set_layer_visible
};

/* static management table              */
static struct ico_win_mgr *_ico_win_mgr = NULL;


/*--------------------------------------------------------------------------*/
/**
 * @brief   find_uifw_win_surface_by_id: find UIFW surface by surface id
 *
 * @param[in]   surfaceid   UIFW surface id
 * @return      UIFW surface table address
 * @retval      !=NULL      success(surface table address)
 * @retval      NULL        error(surface id dose not exist)
 */
/*--------------------------------------------------------------------------*/
static struct uifw_win_surface*
find_uifw_win_surface_by_id(uint32_t surfaceid)
{
    struct uifw_win_surface* usurf;

    usurf = _ico_win_mgr->idhash[MAKE_IDHASH(surfaceid)];

    while (usurf)   {
        if (usurf->id == surfaceid) {
            return usurf;
        }
        usurf = usurf->next_idhash;
    }
    uifw_trace("find_uifw_win_surface_by_id: NULL");
    return NULL;
}


/*--------------------------------------------------------------------------*/
/**
 * @brief   find_uifw_win_surface_by_ws: find UIFW srurace by weston surface
 *
 * @param[in]   wsurf       Weston surface
 * @return      UIFW surface table address
 * @retval      !=NULL      success(surface table address)
 * @retval      NULL        error(surface dose not exist)
 */
/*--------------------------------------------------------------------------*/
static struct uifw_win_surface*
find_uifw_win_surface_by_ws(struct weston_surface *wsurf)
{
    struct uifw_win_surface* usurf;

    usurf = _ico_win_mgr->wshash[MAKE_WSHASH(wsurf)];

    while (usurf)   {
        if (usurf->surface == wsurf) {
            return usurf;
        }
        usurf = usurf->next_wshash;
    }
    uifw_trace("find_uifw_win_surface_by_ws: NULL");
    return NULL;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   find_client_from_client: find UIFW client by wayland client
 *
 * @param[in]   client      Wayland client
 * @return      UIFW client table address
 * @retval      !=NULL      success(client table address)
 * @retval      NULL        error(client dose not exist)
 */
/*--------------------------------------------------------------------------*/
static struct uifw_client*
find_client_from_client(struct wl_client* client)
{
    struct uifw_client  *uclient;

    wl_list_for_each (uclient, &_ico_win_mgr->client_list, link)    {
        if (uclient->client == client)  {
            return(uclient);
        }
    }
    uifw_trace("find_client_from_client: NULL");
    return NULL;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_appid: find application id by wayland client
 *
 * @param[in]   client      Wayland client
 * @return      application id
 * @retval      !=NULL      success(application id)
 * @retval      NULL        error(client dose not exist)
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   char *
ico_window_mgr_appid(struct wl_client* client)
{
    struct uifw_client  *uclient;

    uclient = find_client_from_client(client);

    if (! uclient)  {
        return NULL;
    }
    return uclient->appid;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   generate_id: generate uniq id for UIFW surface id
 *
 * @param       none
 * @return      uniq id for UIFW surface id
 */
/*--------------------------------------------------------------------------*/
static uint32_t
generate_id(void)
{
    int     rep;
    int     i;
    int     map;
    uint16_t *new_map;
    uint32_t surfaceId;

    /* next assign id                           */
    _ico_win_mgr->surfaceid_count ++;

    /* serach free id from bitmap               */
    for (rep = 0; rep < (int)(_ico_win_mgr->surfaceid_max/16); rep++)   {
        if (_ico_win_mgr->surfaceid_count >= _ico_win_mgr->surfaceid_max)   {
            _ico_win_mgr->surfaceid_count = 0;
        }
        if (_ico_win_mgr->surfaceid_map[_ico_win_mgr->surfaceid_count/16] != 0xffff)    {
            /* find free id from bitmap         */
            map = 1 << (_ico_win_mgr->surfaceid_count % 16);
            for (i = (_ico_win_mgr->surfaceid_count % 16); i < 16; i++) {
                if ((_ico_win_mgr->surfaceid_map[_ico_win_mgr->surfaceid_count/16] & map)
                        == 0) {
                    _ico_win_mgr->surfaceid_map[_ico_win_mgr->surfaceid_count/16] |= map;
                    _ico_win_mgr->surfaceid_count
                        = (_ico_win_mgr->surfaceid_count/16)*16 + i;

                    surfaceId = (_ico_win_mgr->surfaceid_count + 1)
                                | _ico_win_mgr->surface_head;
                    uifw_trace("generate_id: SurfaceId=%08x", surfaceId);
                    return(surfaceId);
                }
                map = map << 1;
            }
        }
        _ico_win_mgr->surfaceid_count += 16;
    }

    /* no free id in bitmap, extend bitmap      */
    if ((_ico_win_mgr->surfaceid_max + ADD_SURFACE_IDS) > SURCAFE_ID_MASK)  {
        /* too many surfaces, system error      */
        uifw_trace("generate_id: SurffaceId Overflow(%d, Max=%d), Abort",
                   _ico_win_mgr->surfaceid_max + ADD_SURFACE_IDS, SURCAFE_ID_MASK);
        fprintf(stderr, "generate_id: SurffaceId Overflow(%d, Max=%d), Abort\n",
                _ico_win_mgr->surfaceid_max + ADD_SURFACE_IDS, SURCAFE_ID_MASK);
        abort();
    }

    new_map = (uint16_t *) malloc((_ico_win_mgr->surfaceid_max + ADD_SURFACE_IDS) / 8);
    memcpy(new_map, _ico_win_mgr->surfaceid_map, _ico_win_mgr->surfaceid_max/8);
    memset(&new_map[_ico_win_mgr->surfaceid_max/16], 0, ADD_SURFACE_IDS/8);
    _ico_win_mgr->surfaceid_count = _ico_win_mgr->surfaceid_max;
    new_map[_ico_win_mgr->surfaceid_count/16] |= 1;
    _ico_win_mgr->surfaceid_max += ADD_SURFACE_IDS;
    free(_ico_win_mgr->surfaceid_map);
    _ico_win_mgr->surfaceid_map = new_map;

    uifw_trace("generate_id: Extent SurfaceId=%d(Max.%d)",
               _ico_win_mgr->surfaceid_count+1, _ico_win_mgr->surfaceid_max);
    surfaceId = (_ico_win_mgr->surfaceid_count + 1) | _ico_win_mgr->surface_head;

    uifw_trace("generate_id: SurfaceId=%08x", surfaceId);
    return(surfaceId);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   bind_shell_client: ico_ivi_shell from client
 *
 * @param[in]   client          Wayland client
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
bind_shell_client(struct wl_client *client)
{
    struct uifw_client  *uclient;
    pid_t   pid;
    uid_t   uid;
    gid_t   gid;
    int     fd;
    int     size;
    int     i;
    int     j;
    char    procpath[128];

    uifw_trace("bind_shell_client: Enter(client=%08x)", (int)client);

    /* set client                           */
    uclient = find_client_from_client(client);
    if (! uclient)  {
        /* client not exist, create client management table             */
        uifw_trace("bind_shell_client: Create Client");
        uclient = (struct uifw_client *)malloc(sizeof(struct uifw_client));
        if (!uclient)   {
            uifw_error("bind_shell_client: Error, No Memory");
            return;
        }
        memset(uclient, 0, sizeof(struct uifw_client));
        uclient->client = client;
        wl_list_insert(&_ico_win_mgr->client_list, &uclient->link);
    }
    wl_client_get_credentials(client, &pid, &uid, &gid);
    uifw_trace("bind_shell_client: client=%08x pid=%d uid=%d gid=%d",
               (int)client, (int)pid, (int)uid, (int)gid);
    if (pid > 0)    {
        uclient->pid = (int)pid;
        /* get applicationId from AppCore(AUL)  */
        if (aul_app_get_appid_bypid(uclient->pid, uclient->appid, ICO_IVI_APPID_LENGTH)
                        == AUL_R_OK)    {
            uifw_trace("bind_shell_client: client=%08x pid=%d appid=<%s>",
                       (int)client, uclient->pid, uclient->appid);
        }
        else    {
            /* client dose not exist in AppCore, search Linux process table */
            uifw_trace("bind_shell_client: pid=%d dose not exist in AppCore(AUL)",
                       uclient->pid);

            memset(uclient->appid, 0, ICO_IVI_APPID_LENGTH);
            snprintf(procpath, sizeof(procpath)-1, "/proc/%d/cmdline", uclient->pid);
            fd = open(procpath, O_RDONLY);
            if (fd >= 0)    {
                size = read(fd, procpath, sizeof(procpath));
                for (; size > 0; size--)    {
                    if (procpath[size-1])   break;
                }
                if (size > 0)   {
                    /* get program base name    */
                    i = 0;
                    for (j = 0; j < size; j++)  {
                        if (procpath[j] == 0)   break;
                        if (procpath[j] == '/') i = j + 1;
                    }
                    j = 0;
                    for (; i < size; i++)   {
                        uclient->appid[j] = procpath[i];
                        if ((uclient->appid[j] == 0) ||
                            (j >= (ICO_IVI_APPID_LENGTH-1)))    break;
                        j++;
                    }
                    /* search application number in apprication start option    */
                    if ((uclient->appid[j] == 0) && (j < (ICO_IVI_APPID_LENGTH-2))) {
                        for (; i < size; i++)   {
                            if ((procpath[i] == 0) &&
                                (procpath[i+1] == '@')) {
                                strncpy(&uclient->appid[j], &procpath[i+1],
                                        ICO_IVI_APPID_LENGTH - j - 2);
                            }
                        }
                    }
                }
                close(fd);
            }
            if (uclient->appid[0])  {
                uifw_trace("bind_shell_client: client=%08x pid=%d appid=<%s> from "
                           "Process table", (int)client, uclient->pid, uclient->appid);
            }
            else    {
                uifw_trace("bind_shell_client: pid=%d dose not exist in Process table",
                           uclient->pid);
                sprintf(uclient->appid, "?%d?", uclient->pid);
            }
        }
    }
    else    {
        uifw_trace("bind_shell_client: client=%08x pid dose not exist", (int)client);
    }
    uifw_trace("bind_shell_client: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   unbind_shell_client: unbind ico_ivi_shell from client
 *
 * @param[in]   client          Wayland client
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
unbind_shell_client(struct wl_client *client)
{
    struct uifw_client  *uclient;

    uifw_trace("unbind_shell_client: Enter(client=%08x)", (int)client);

    uclient = find_client_from_client(client);
    if (uclient)    {
        /* Client exist, Destory client management table             */
        wl_list_remove(&uclient->link);
        free(uclient);
    }
    uifw_trace("unbind_shell_client: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   client_register_surface: create UIFW surface
 *
 * @param[in]   client          Wayland client
 * @param[in]   resource        client resource
 * @param[in]   surface         Weston surface
 * @param[in]   shsurf          shell surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
client_register_surface(struct wl_client *client, struct wl_resource *resource,
                        struct weston_surface *surface, struct shell_surface *shsurf)
{
    struct uifw_win_surface *us;
    struct uifw_win_surface *phash;
    struct uifw_win_surface *bhash;
    uint32_t    hash;

    uifw_trace("client_register_surface: Enter(surf=%08x,client=%08x,res=%08x)",
               (int)surface, (int)client, (int)resource);

    /* check new surface                    */
    if (find_uifw_win_surface_by_ws(surface))   {
        /* surface exist, NOP               */
        uifw_trace("client_register_surface: Leave(Already Exist)");
        return;
    }

    /* create UIFW surface management table */
    us = malloc(sizeof(struct uifw_win_surface));
    if (!us)    {
        uifw_error("client_register_surface: No Memory");
        return;
    }

    memset(us, 0, sizeof(struct uifw_win_surface));

    us->id = generate_id();
    us->surface = surface;
    us->shsurf = shsurf;
    if (_ico_win_mgr->num_manager <= 0) {
        uifw_trace("client_register_surface: No Manager, Force visible");
        ivi_shell_set_visible(shsurf, 1);   /* NOT exist HomeScreen     */
    }
    else    {
        uifw_trace("client_register_surface: Manager exist, Not visible");
        ivi_shell_set_visible(shsurf, -1);  /* Exist HomeScreen         */
    }

    /* set client                           */
    us->uclient = find_client_from_client(client);
    if (! us->uclient)  {
        /* client not exist, create client management table */
        uifw_trace("client_register_surface: Create Client");
        bind_shell_client(client);
        us->uclient = find_client_from_client(client);
        if (! us->uclient)  {
            uifw_error("client_register_surface: No Memory");
            return;
        }
    }
    wl_list_insert(&_ico_win_mgr->surface_list, &us->link);

    /* make surface id hash table       */
    hash = MAKE_IDHASH(us->id);
    phash = _ico_win_mgr->idhash[hash];
    bhash = NULL;
    while (phash)   {
        bhash = phash;
        phash = phash->next_idhash;
    }
    if (bhash)  {
        bhash->next_idhash = us;
    }
    else    {
        _ico_win_mgr->idhash[hash] = us;
    }

    /* make weston surface hash table   */
    hash = MAKE_WSHASH(us->surface);
    phash = _ico_win_mgr->wshash[hash];
    bhash = NULL;
    while (phash)   {
        bhash = phash;
        phash = phash->next_wshash;
    }
    if (bhash)  {
        bhash->next_wshash = us;
    }
    else    {
        _ico_win_mgr->wshash[hash] = us;
    }
    /* set default layer id             */
    ivi_shell_set_layer(shsurf, 0);

    /* send event to manager            */
    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_CREATED,
                            us->id, us->uclient->appid, us->uclient->pid, 0,0,0,0,0);

    if (win_mgr_hook_create) {
        /* call surface create hook for ico_window_mgr  */
        (void) (*win_mgr_hook_create)(client, surface, us->id, us->uclient->appid);
    }
    uifw_trace("client_register_surface: Leave(surfaceId=%08x)", us->id);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_map_surface: map surface
 *
 * @param[in]   surface         Weston surface
 * @param[in]   width           surface width
 * @param[in]   height          surface height
 * @param[in]   sx              X coordinate on screen
 * @param[in]   sy              Y coordinate on screen
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_map_surface(struct weston_surface *surface, int32_t *width, int32_t *height,
                    int32_t *sx, int32_t *sy)
{
    struct uifw_win_surface *usurf;

    uifw_trace("win_mgr_map_surface: Enter(%08x, x/y=%d/%d w/h=%d/%d)",
               (int)surface, *sx, *sy, *width, *height);

    usurf = find_uifw_win_surface_by_ws(surface);

    if (usurf) {
        uifw_trace("win_mgr_map_surface: surf=%08x w/h=%d/%d vis=%d",
                   usurf->id, usurf->width, usurf->height,
                   ivi_shell_is_visible(usurf->shsurf));

        if ((usurf->width > 0) && (usurf->height > 0)) {
            uifw_trace("win_mgr_map_surface: HomeScreen registed, PositionSize"
                       "(surf=%08x x/y=%d/%d w/h=%d/%d vis=%d",
                       usurf->id, usurf->x, usurf->y, usurf->width, usurf->height,
                       ivi_shell_is_visible(usurf->shsurf));
            *width = usurf->width;
            *height = usurf->height;
            surface->geometry.x = usurf->x;
            surface->geometry.y = usurf->y;
        }
        else    {
            uifw_trace("win_mgr_map_surface: HomeScreen not regist Surface, "
                       "Change PositionSize(surf=%08x x/y=%d/%d w/h=%d/%d)",
                       usurf->id, *sx, *sy, *width, *height);
            usurf->width = *width;
            usurf->height = *height;
            usurf->x = *sx;
            usurf->y = *sy;
            if (usurf->x < 0)   usurf->x = 0;
            if (usurf->y < 0)   usurf->y = 0;

            if (_ico_win_mgr->num_manager > 0)  {
                /* HomeScreen exist, coodinate set by HomeScreen                */
                surface->geometry.x = 0;
                surface->geometry.y = 0;

                /* change surface size, because HomeScreen change surface size  */
                *width = 1;
                *height = 1;
                uifw_trace("win_mgr_map_surface: Change size and position");
            }
            else    {
                uifw_trace("win_mgr_map_surface: Np HomeScreen, chaneg to Visible");
                ivi_shell_set_visible(usurf->shsurf, 1);
            }
        }
        ivi_shell_set_positionsize(usurf->shsurf,
                                   usurf->x, usurf->y, usurf->width, usurf->height);
        uifw_trace("win_mgr_map_surface: Leave");
    }
    else    {
        uifw_trace("win_mgr_map_surface: Leave(No Window Manager Surface)");
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @briefdwi    uifw_set_user: set user id (for RemoteUI)
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   pid         client process id
 * @param[in]   appid       application id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_user(struct wl_client *client, struct wl_resource *resource,
              int pid, const char *appid)
{
    struct uifw_client  *uclient;

    uifw_trace("uifw_set_user: Enter(client=%08x pid=%d appid=%s)",
                (int)client, pid, appid);

    uclient = find_client_from_client(client);
    if (uclient)    {
        uclient->pid = pid;
        memset(uclient->appid, 0, ICO_IVI_APPID_LENGTH);
        strncpy(uclient->appid, appid, ICO_IVI_APPID_LENGTH-1);
        uclient->resource = resource;
        uifw_trace("uifw_set_user: Leave(Client Exist, change PID/AppId)");
        return;
    }

    uclient = (struct uifw_client *)malloc(sizeof(struct uifw_client));
    if (! uclient)  {
        uifw_trace("uifw_set_user: Leave(Error, No Memory)");
        return;
    }

    memset(uclient, 0, sizeof(struct uifw_client));
    uclient->client = client;
    uclient->pid = pid;
    memset(uclient->appid, 0, ICO_IVI_APPID_LENGTH);
    strncpy(uclient->appid, appid, ICO_IVI_APPID_LENGTH-1);
    uclient->resource = resource;

    wl_list_insert(&_ico_win_mgr->client_list, &uclient->link);

    if (win_mgr_hook_set_user) {
        (void) (*win_mgr_hook_set_user) (client, uclient->appid);
    }
    uifw_trace("uifw_set_user: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_set_eventcb: set event callback flag for HomeScreen
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   eventcb     event callback flag(1=callback, 0=no callback)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_eventcb(struct wl_client *client, struct wl_resource *resource, int eventcb)
{
    struct uifw_manager* mgr;
    struct uifw_win_surface *usurf;
    struct uifw_client *uclient;

    uifw_trace("uifw_set_eventcb: Enter client=%08x eventcb=%d",
               (int)client, eventcb);

    uclient = find_client_from_client(client);
    if (uclient)    {
        uclient->manager = eventcb;
    }

    /* client set to manager            */
    _ico_win_mgr->num_manager = 0;
    wl_list_for_each (mgr, &_ico_win_mgr->manager_list, link)   {
        if (mgr->resource == resource)  {
            if (mgr->eventcb != eventcb)    {
                uifw_trace("uifw_set_eventcb: Event Callback %d=>%d",
                           mgr->eventcb, eventcb);
                mgr->eventcb = eventcb;

                if (eventcb)    {
                    wl_list_for_each (usurf, &_ico_win_mgr->surface_list, link) {
                        /* send window create event to manager  */
                        uifw_trace("uifw_set_eventcb: Send manager(%08x) WINDOW_CREATED"
                                   "(surf=%08x,pid=%d,appid=%s)", (int)resource, usurf->id,
                                   usurf->uclient->pid, usurf->uclient->appid);
                        ico_window_mgr_send_window_created(resource,
                            usurf->id, usurf->uclient->pid, usurf->uclient->appid);
                    }
                }
            }
        }
        if (mgr->eventcb)   {
            _ico_win_mgr->num_manager++;
        }
    }
    uifw_trace("uifw_set_eventcb: Leave(managers=%d)", _ico_win_mgr->num_manager);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_set_window_layer: set layer id to surface
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   surfaceid   UIFW surface id
 * @param[in]   layer       layer id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_window_layer(struct wl_client *client, struct wl_resource *resource,
                      uint32_t surfaceid, int32_t layer)
{
    uifw_trace("uifw_set_window_layer: Enter res=%08x surfaceid=%08x layer=%d",
               (int)resource, surfaceid, layer);

    struct uifw_win_surface *usurf = find_uifw_win_surface_by_id(surfaceid);

    if (! usurf)    {
        uifw_trace("uifw_set_window_layer: Leave(No Surface(id=%08x)", surfaceid);
        return;
    }
    else if (usurf->layer != layer) {
        usurf->layer = layer;
        uifw_trace("uifw_set_window_layer: Set Layer(%d) to Shell Surface", layer);
        ivi_shell_set_layer(usurf->shsurf, layer);

        win_mgr_surface_change(usurf->surface, 1, 1);
    }
    uifw_trace("uifw_set_window_layer: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_set_weston_surface: set weston surface from UIFW surface
 *
 * @param[in]   usurf       UIFW surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_weston_surface(struct uifw_win_surface *usurf)
{
    struct weston_surface *es = usurf->surface;
    int     width = usurf->width;
    int     height = usurf->height;
    int     x = usurf->x;
    int     y = usurf->y;

    if ((es != NULL) && (es->buffer != NULL))   {
        if (usurf->width > es->buffer->width)   {
            width = es->buffer->width;
            x += (usurf->width - es->buffer->width)/2;
        }
        if (usurf->height > es->buffer->height) {
            height = es->buffer->height;
            y += (usurf->height - es->buffer->height)/2;
        }
    }
    ivi_shell_set_positionsize(usurf->shsurf,
                               usurf->x, usurf->y, usurf->width, usurf->height);
    uifw_trace("uifw_set_weston_surface: w/h=%d/%d->%d/%d x/y=%d/%d->%d/%d",
               usurf->width, usurf->height, width, height, usurf->x, usurf->y, x, y);
    ivi_shell_surface_configure(usurf->shsurf, x, y, width, height);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_set_positionsize: set surface position and size
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   surfaceid   UIFW surface id
 * @param[in]   x           X coordinate on screen(if bigger than 16383, no change)
 * @param[in]   y           Y coordinate on screen(if bigger than 16383, no change)
 * @param[in]   width       surface width(if bigger than 16383, no change)
 * @param[in]   height      surface height(if bigger than 16383, no change)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_positionsize(struct wl_client *client, struct wl_resource *resource,
                      uint32_t surfaceid,
                      int32_t x, int32_t y, int32_t width, int32_t height)
{
    struct uifw_client *uclient;

    uifw_trace("uifw_set_positionsize: Enter res=%08x surf=%08x x/y/w/h=%d/%d/%d/%d",
               (int)resource, surfaceid, x, y, width, height);

    struct uifw_win_surface* usurf = find_uifw_win_surface_by_id(surfaceid);

    if (usurf && (usurf->surface))  {
        /* weston surface exist             */
        struct weston_surface *es = usurf->surface;

        /* if x,y,width,height bigger then ICO_IVI_MAX_COORDINATE, no change    */
        if (x > ICO_IVI_MAX_COORDINATE)         x = usurf->x;
        if (y > ICO_IVI_MAX_COORDINATE)         y = usurf->y;
        if (width > ICO_IVI_MAX_COORDINATE)     width = usurf->width;
        if (height > ICO_IVI_MAX_COORDINATE)    height = usurf->height;

        uclient = find_client_from_client(client);
        if (uclient)    {
            if (! uclient->manager) uclient = NULL;
        }
        if (! uclient)  {
            if ((usurf->width > 0) && (usurf->height > 0))  {
                win_mgr_surface_change_mgr(es, x, y, width, height);
                uifw_trace("uifw_set_positionsize: Leave(Request from App)");
                return;
            }

            uifw_trace("uifw_set_positionsize: Initial Position/Size visible=%d",
                       ivi_shell_is_visible(usurf->shsurf));
            /* Initiale position is (0,0)   */
            es->geometry.x = 0;
            es->geometry.y = 0;
        }

        uifw_trace("uifw_set_positionsize: Old geometry x/y=%d/%d,w/h=%d/%d",
                   (int)es->geometry.x, (int)es->geometry.y,
                   (int)es->geometry.width, (int)es->geometry.height);

        usurf->x = x;
        usurf->y = y;
        usurf->width = width;
        usurf->height = height;
        ivi_shell_set_positionsize(usurf->shsurf, x, y, width, height);
        if (_ico_win_mgr->num_manager <= 0) {
            /* no manager(HomeScreen), set geometory    */
            es->geometry.x = x;
            es->geometry.y = y;
        }
        if ((es->output) && (es->buffer) &&
            (es->geometry.width > 0) && (es->geometry.height > 0)) {
            uifw_trace("uifw_set_positionsize: Fixed Geometry, Change(Vis=%d)",
                       ivi_shell_is_visible(usurf->shsurf));
            uifw_set_weston_surface(usurf);
            weston_surface_damage_below(es);
            weston_surface_damage(es);
            weston_compositor_schedule_repaint(_ico_win_mgr->compositor);
        }
        win_mgr_surface_change(es, 0, 1);

        uifw_trace("uifw_set_positionsize: Leave(OK,output=%x)", (int)es->output);
    }
    else    {
        uifw_trace("uifw_set_positionsize: Leave(surf=%08x NOT Found)", surfaceid);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_set_visible: surface visible/raise control
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   surfaceid   UIFW surface id
 * @param[in]   visible     visible(1=show/0=hide/other=no change)
 * @param[in]   raise       raise(1=raise/0=lower/other=no change)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_visible(struct wl_client *client, struct wl_resource *resource,
                 uint32_t surfaceid, int32_t visible, int32_t raise)
{
    struct uifw_win_surface* usurf;
    struct uifw_client *uclient;

    uifw_trace("uifw_set_visible: Enter(surf=%08x,%d,%d)", surfaceid, visible, raise);

    uclient = find_client_from_client(client);
    if (uclient)    {
        if (! uclient->manager) {
            uifw_trace("uifw_set_visible: Request from App(%s), not Manager",
                       uclient->appid);
            uclient = NULL;
        }
        else    {
            uifw_trace("uifw_set_visible: Request from Manager(%s)", uclient->appid);
        }
    }
    else    {
        uifw_trace("uifw_set_visible: Request from Unknown App, not Manager");
    }

    usurf = find_uifw_win_surface_by_id(surfaceid);

    if ((! usurf) || (! usurf->surface))    {
        uifw_trace("uifw_set_visible: Leave(Surface Not Exist)");
        return;
    }

    if (visible == 1) {
        if ((usurf->width <= 0) || (usurf->height <= 0))    {
            /* not declare surface geometry, initialize     */
            usurf->width = usurf->surface->geometry.width;
            usurf->height = usurf->surface->geometry.height;
            uifw_trace("uifw_set_visible: Set w/h=%d/%d", usurf->width, usurf->height);
        }
        if (! ivi_shell_is_visible(usurf->shsurf))  {
            if (uclient)    {
                /* manager exist, change to visible         */
                ivi_shell_set_visible(usurf->shsurf, 1);
            }
            uifw_trace("uifw_set_visible: Change to Visible");

            ivi_shell_set_toplevel(usurf->shsurf);

            /* Weston surface configure                     */
            uifw_trace("uifw_set_visible: Visible to Weston WSurf=%08x,%d/%d/%d/%d",
                       (int)usurf->surface, usurf->x, usurf->y,
                       usurf->width, usurf->height);
            uifw_set_weston_surface(usurf);
            ivi_shell_set_surface_type(usurf->shsurf);
        }
        else if ((raise != 0) && (raise != 1))  {
            uifw_trace("uifw_set_visible: Leave(No Change)");
            return;
        }
    }
    else if (visible == 0)  {

        if (ivi_shell_is_visible(usurf->shsurf))    {
            ivi_shell_set_visible(usurf->shsurf, 0);
            uifw_trace("uifw_set_visible: Change to UnVisible");

            /* Weston surface configure                     */
            uifw_set_weston_surface(usurf);
        }
        else if ((raise != 0) && (raise != 1))  {
            uifw_trace("uifw_set_visible: Leave(No Change)");
            return;
        }
    }
    else if ((raise != 0) && (raise != 1))  {
        uifw_trace("uifw_set_visible: Leave(No Change)");
        return;
    }

    /* raise/lower                              */
    if ((raise == 1) || (raise == 0))   {
        ivi_shell_set_raise(usurf->shsurf, raise);
    }

    if ((usurf->surface) && (usurf->surface->buffer) && (usurf->surface->output))   {
        weston_surface_damage_below(usurf->surface);
        weston_surface_damage(usurf->surface);
        weston_compositor_schedule_repaint(_ico_win_mgr->compositor);
    }
    /* send event(VISIBLE) to manager           */
    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_VISIBLE,
                            surfaceid, NULL, visible, raise, uclient ? 0 : 1, 0,0,0);

    uifw_trace("uifw_set_visible: Leave(OK)");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_set_transition: set transition of surface visible/unvisible
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   surfaceid   UIFW surface id
 * @param[in]   transition  transiton id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_transition(struct wl_client *client, struct wl_resource *resource,
                    uint32_t surfaceid, int32_t transition)
{
    struct uifw_win_surface* usurf = find_uifw_win_surface_by_id(surfaceid);

    uifw_trace("uifw_set_transition: Enter(surf=%08x, transition=%d)",
                surfaceid, transition);

    if (usurf) {
        usurf->transition = transition;
        uifw_trace("uifw_set_transition: Leave(OK)");
    }
    else    {
        uifw_trace("uifw_set_transition: Leave(Surface(%08x) Not exist)", surfaceid);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_set_active: set active surface
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   surfaceid   UIFW surface id
 * @param[in]   target      target device
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_active(struct wl_client *client, struct wl_resource *resource,
                uint32_t surfaceid, uint32_t target)
{
    struct uifw_win_surface* usurf;

    uifw_trace("uifw_set_active: Enter(surf=%08x,target=%x)", surfaceid, target);

    if ((surfaceid > 0) &&
        ((target & (ICO_IVI_SHELL_ACTIVE_POINTER|ICO_IVI_SHELL_ACTIVE_KEYBOARD)) != 0)) {
        usurf = find_uifw_win_surface_by_id(surfaceid);
    }
    else    {
        usurf = NULL;
    }
    if (usurf) {
        switch (target & (ICO_IVI_SHELL_ACTIVE_POINTER|ICO_IVI_SHELL_ACTIVE_KEYBOARD)) {
        case ICO_IVI_SHELL_ACTIVE_POINTER:
            if (usurf != _ico_win_mgr->active_pointer_surface)  {
                if (_ico_win_mgr->active_pointer_surface)   {
                    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                            _ico_win_mgr->active_pointer_surface->id, NULL,
                                            (_ico_win_mgr->active_keyboard_surface ==
                                             _ico_win_mgr->active_pointer_surface) ?
                                                ICO_IVI_SHELL_ACTIVE_KEYBOARD :
                                                ICO_IVI_SHELL_ACTIVE_NONE,
                                            0,0,0,0,0);
                }
                _ico_win_mgr->active_pointer_surface = usurf;
                ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                        surfaceid, NULL,
                                        ICO_IVI_SHELL_ACTIVE_POINTER |
                                        (_ico_win_mgr->active_keyboard_surface == usurf) ?
                                            ICO_IVI_SHELL_ACTIVE_KEYBOARD : 0,
                                        0,0,0,0,0);
                ivi_shell_set_active(usurf->shsurf, ICO_IVI_SHELL_ACTIVE_POINTER);
            }
            break;
        case ICO_IVI_SHELL_ACTIVE_KEYBOARD:
            if (usurf != _ico_win_mgr->active_keyboard_surface) {
                if (_ico_win_mgr->active_keyboard_surface)   {
                    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                            _ico_win_mgr->active_keyboard_surface->id, NULL,
                                            (_ico_win_mgr->active_keyboard_surface ==
                                             _ico_win_mgr->active_pointer_surface) ?
                                                ICO_IVI_SHELL_ACTIVE_POINTER :
                                                ICO_IVI_SHELL_ACTIVE_NONE,
                                            0,0,0,0,0);
                }
                _ico_win_mgr->active_keyboard_surface = usurf;
                ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                        surfaceid, NULL,
                                        ICO_IVI_SHELL_ACTIVE_KEYBOARD |
                                        (_ico_win_mgr->active_pointer_surface == usurf) ?
                                            ICO_IVI_SHELL_ACTIVE_POINTER : 0,
                                        0,0,0,0,0);
                ivi_shell_set_active(usurf->shsurf, ICO_IVI_SHELL_ACTIVE_KEYBOARD);
            }
            break;
        default:
            if ((usurf != _ico_win_mgr->active_pointer_surface) ||
                (usurf != _ico_win_mgr->active_keyboard_surface))   {
                if (_ico_win_mgr->active_pointer_surface)   {
                    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                            _ico_win_mgr->active_pointer_surface->id,
                                            NULL, ICO_IVI_SHELL_ACTIVE_NONE,
                                            0,0,0,0,0);
                    if (_ico_win_mgr->active_keyboard_surface ==
                        _ico_win_mgr->active_pointer_surface)   {
                        _ico_win_mgr->active_keyboard_surface = NULL;
                    }
                }
                if (_ico_win_mgr->active_keyboard_surface)   {
                    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                            _ico_win_mgr->active_keyboard_surface->id,
                                            NULL, ICO_IVI_SHELL_ACTIVE_NONE,
                                            0,0,0,0,0);
                }
                _ico_win_mgr->active_pointer_surface = usurf;
                _ico_win_mgr->active_keyboard_surface = usurf;
                ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                        surfaceid, NULL,
                                        ICO_IVI_SHELL_ACTIVE_POINTER |
                                            ICO_IVI_SHELL_ACTIVE_KEYBOARD,
                                        0,0,0,0,0);
                ivi_shell_set_active(usurf->shsurf,
                                     ICO_IVI_SHELL_ACTIVE_POINTER |
                                         ICO_IVI_SHELL_ACTIVE_KEYBOARD);
            }
            break;
        }
        uifw_trace("uifw_set_active: Leave(Change Active)");
    }
    else    {
        ivi_shell_set_active(NULL, target);
        uifw_trace("uifw_set_active: Leave(Reset active surface)");
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_set_layer_visible: layer visible control
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   layer       layer id
 * @param[in]   visible     visible(1=show/0=hide)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_layer_visible(struct wl_client *client, struct wl_resource *resource,
                       int32_t layer, int32_t visible)
{
    uifw_trace("uifw_set_layer_visible: Enter(layer=%d, visilbe=%d)", layer, visible);

    ivi_shell_set_layer_visible(layer, visible);

    uifw_trace("uifw_set_layer_visible: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_surface_change_mgr: surface chagen from manager(HomeScreen)
 *
 * @param[in]   surface     Weston surface
 * @param[in]   x           X coordinate on screen
 * @param[in]   y           Y coordinate on screen
 * @param[in]   width       surface width
 * @param[in]   height      surface height
 * @return      number of managers
 * @retval      > 0         number of managers
 * @retval      0           manager not exist
 */
/*--------------------------------------------------------------------------*/
static int
win_mgr_surface_change_mgr(struct weston_surface *surface,
                           const int x, const int y, const int width, const int height)
{
    int     num_mgr;
    struct uifw_win_surface *usurf;

    uifw_trace("win_mgr_surface_change_mgr: Enter(%08x,x/y=%d/%d,w/h=%d/%d)",
               (int)surface, x, y, width, height);

    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("win_mgr_surface_change_mgr: Leave(Not Exist)");
        return 0;
    }

    num_mgr = ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_CONFIGURE,
                                      usurf->id, usurf->uclient->appid, usurf->layer,
                                      x, y, width, height, 1);

    uifw_trace("win_mgr_surface_change_mgr: Leave(%d)", num_mgr);
    return num_mgr;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_surface_change: surface change
 *
 * @param[in]   surface     Weston surface
 * @param[in]   to          destination(0=Client&Manager,1=Client,-1=Manager)
 * @param[in]   manager     request from manager(0=Client,1=Manager)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_surface_change(struct weston_surface *surface, const int to, const int manager)
{
    struct uifw_win_surface *usurf;

    uifw_trace("win_mgr_surface_change: Enter(%08x,%d,%d)", (int)surface, to, manager);

    /* Find surface         */
    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("win_mgr_surface_change: Leave(Not Exist)");
        return;
    }

    /* send wayland event to client     */
    if ((to >= 0) && (usurf->shsurf != NULL) && (manager !=0))    {
        uifw_trace("win_mgr_surface_change: Send SHELL_SURFACE_CONFIGURE(%08x,w/h=%d/%d)",
                   usurf->id, usurf->width, usurf->height);
        ivi_shell_send_configure(usurf->shsurf, usurf->id,
                                 (WL_SHELL_SURFACE_RESIZE_RIGHT |
                                  WL_SHELL_SURFACE_RESIZE_BOTTOM),
                                 usurf->width, usurf->height);
    }

    /* send manager event to HomeScreen */
    if (to <= 0)    {
        ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_CONFIGURE,
                                usurf->id, usurf->uclient->appid, usurf->layer,
                                usurf->x, usurf->y, usurf->width, usurf->height,
                                (manager != 0) ? 0 : 1);
    }
    uifw_trace("win_mgr_surface_change: Leave(OK)");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_surface_select: select surface by Bottun/Touch
 *
 * @param[in]   surface     Weston surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_surface_select(struct weston_surface *surface)
{
    struct uifw_win_surface *usurf;

    uifw_trace("win_mgr_surface_select: Enter(%08x)", (int)surface);

    /* find surface         */
    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("win_mgr_surface_select: Leave(Not Exist)");
        return;
    }

    /* send active event to manager     */
    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                            usurf->id, NULL, ICO_IVI_SHELL_ACTIVE_SELECTED, 0,0,0,0,0);

    uifw_trace("win_mgr_surface_select: Leave(OK)");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_surface_destroy: surface destroy
 *
 * @param[in]   surface     Weston surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_surface_destroy(struct weston_surface *surface)
{
    struct uifw_win_surface *usurf;
    struct uifw_win_surface *phash;
    struct uifw_win_surface *bhash;
    uint32_t    hash;

    uifw_trace("win_mgr_surface_destroy: Enter(%08x)", (int)surface);

    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("win_mgr_surface_destroy: Leave(Not Exist)");
        return;
    }

    hash = MAKE_IDHASH(usurf->id);
    phash = _ico_win_mgr->idhash[hash];
    bhash = NULL;
    while ((phash) && (phash != usurf)) {
        bhash = phash;
        phash = phash->next_idhash;
    }
    if (bhash)  {
        bhash->next_idhash = usurf->next_idhash;
    }
    else    {
        _ico_win_mgr->idhash[hash] = usurf->next_idhash;
    }

    hash = MAKE_WSHASH(usurf->surface);
    phash = _ico_win_mgr->wshash[hash];
    bhash = NULL;
    while ((phash) && (phash != usurf)) {
        bhash = phash;
        phash = phash->next_wshash;
    }
    if (bhash)  {
        bhash->next_wshash = usurf->next_wshash;
    }
    else    {
        _ico_win_mgr->wshash[hash] = usurf->next_wshash;
    }

    wl_list_remove(&usurf->link);
    wl_list_init(&usurf->link);

    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_DESTROYED,
                           usurf->id, NULL, 0,0,0,0,0,0);

    hash = usurf->id & SURCAFE_ID_MASK;
    _ico_win_mgr->surfaceid_map[(hash - 1)/16] &= ~(1 << ((hash - 1) % 16));

    free(usurf);

    if (win_mgr_hook_destroy) {
        (void) (*win_mgr_hook_destroy) (surface);
    }
    uifw_trace("win_mgr_surface_destroy: Leave(OK)");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   bind_ico_win_mgr: bind Multi Window Manager from client
 *
 * @param[in]   client      client
 * @param[in]   data        user data(unused)
 * @param[in]   version     protocol version(unused)
 * @param[in]   id          client object id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
bind_ico_win_mgr(struct wl_client *client,
               void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *add_resource;
    struct uifw_manager *nm;

    uifw_trace("bind_ico_win_mgr: Enter(client=%08x, id=%x)", (int)client, (int)id);

    add_resource = wl_client_add_object(client, &ico_window_mgr_interface,
                                        &ico_window_mgr_implementation, id, _ico_win_mgr);
    add_resource->destroy = unbind_ico_win_mgr;

    /* Manager                                      */
    nm = (struct uifw_manager *)malloc(sizeof(struct uifw_manager));
    memset(nm, 0, sizeof(struct uifw_manager));
    nm->resource = add_resource;
    wl_list_insert(&_ico_win_mgr->manager_list, &nm->link);

    uifw_trace("bind_ico_win_mgr: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   unbind_ico_win_mgr: unbind Multi Window Manager from client
 *
 * @param[in]   resource    client resource
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
unbind_ico_win_mgr(struct wl_resource *resource)
{
    struct uifw_manager *mgr, *itmp;

    uifw_trace("unbind_ico_win_mgr: Enter");

    /* Remove manager from manager list */
    _ico_win_mgr->num_manager = 0;
    wl_list_for_each_safe (mgr, itmp, &_ico_win_mgr->manager_list, link)    {
        if (mgr->resource == resource) {
            wl_list_remove(&mgr->link);
            free(mgr);
        }
        else    {
            if (mgr->eventcb)   {
                _ico_win_mgr->num_manager++;
            }
        }
    }

    free(resource);
    uifw_trace("unbind_ico_win_mgr: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_winmgr_usurf_2_node: get surface nodeId
 *
 * @param[in]   surfaceid   UIFW surface id
 * @return      node id
 * @retval      >= 0        success(surface node id)
 * @retval      < 0         error(surface id dose not exist)
 */
/*--------------------------------------------------------------------------*/
static  int
ico_winmgr_usurf_2_node(const int surfaceid)
{
    /* currently support single ECU system  */
    return(ICO_IVI_SURFACEID_2_NODEID(surfaceid));
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_win_mgr_send_to_mgr: send event to manager(HomeScreen)
 *
 * @param[in]   event       event code(if -1, not send event)
 * @param[in]   surfaceid   UIFW surface id
 * @param[in]   appid       applicationId
 * @param[in]   param1      parameter 1
 * @param[in]      :             :
 * @param[in]   param6      parameter 6
 * @return      number of managers
 * @retval      > 0         number of managers
 * @retval      0           manager not exist
 */
/*--------------------------------------------------------------------------*/
static int
ico_win_mgr_send_to_mgr(const int event, const int surfaceid, const char *appid,
                        const int param1, const int param2, const int param3,
                        const int param4, const int param5, const int param6)
{
    int     num_mgr = 0;
    struct uifw_manager* mgr;

    wl_list_for_each (mgr, &_ico_win_mgr->manager_list, link)   {
        if (mgr->eventcb)   {
            num_mgr ++;
            switch(event)   {
            case ICO_WINDOW_MGR_WINDOW_CREATED:
                uifw_trace("ico_win_mgr_send_to_mgr: Send Manager(%08x) WINDOW_CREATED"
                           "(surf=%08x,pid=%d,appid=%s)",
                           (int)mgr->resource, surfaceid, param1, appid);
                ico_window_mgr_send_window_created(mgr->resource, surfaceid, param1, appid);
                break;

            case ICO_WINDOW_MGR_WINDOW_VISIBLE:
                uifw_trace("ico_win_mgr_send_to_mgr: Send Manager(%08x) VISIBLE"
                           "(surf=%08x,vis=%d,raise=%d,hint=%d)",
                           (int)mgr->resource, surfaceid, param1, param2, param3);
                ico_window_mgr_send_window_visible(mgr->resource,
                                                   surfaceid, param1, param2, param3);
                break;

            case ICO_WINDOW_MGR_WINDOW_CONFIGURE:
                uifw_trace("ico_win_mgr_send_to_mgr: Send Manager(%08x) CONFIGURE"
                           "(surf=%08x,app=%s,layer=%d,x/y=%d/%d,w/h=%d/%d,hint=%d)",
                           (int)mgr->resource, surfaceid, appid,
                           param1, param2, param3, param4, param5, param6);
                ico_window_mgr_send_window_configure(mgr->resource, surfaceid, appid,
                                                     param1, param2, param3, param4,
                                                     param5, param6);
                break;

            case ICO_WINDOW_MGR_WINDOW_DESTROYED:
                uifw_trace("ico_win_mgr_send_to_mgr: Send Manager(%08x) DESTROYED "
                           "surf=%08x", (int)mgr->resource, surfaceid);
                ico_window_mgr_send_window_destroyed(mgr->resource, surfaceid);
                break;

            case ICO_WINDOW_MGR_WINDOW_ACTIVE:
                uifw_trace("ico_win_mgr_send_to_mgr: Send Manager(%08x) ACTIVE surf=%08x "
                           "active=%d", (int)mgr->resource, surfaceid, param1);
                ico_window_mgr_send_window_active(mgr->resource, surfaceid,
                                                  (uint32_t)param1);
                break;

            default:
                break;
            }
        }
    }
    return num_mgr;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_win_mgr_hook_set_user: set hook function for set user
 *
 * @param[in]   hook_set_user   hook function
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ico_win_mgr_hook_set_user(void (*hook_set_user)(struct wl_client *client,
                                                const char *appid))
{
    uifw_trace("ico_win_mgr_hook_set_user: Hook %08x", (int)hook_set_user);
    win_mgr_hook_set_user = hook_set_user;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_win_mgr_hook_create: set hook function for create surface
 *
 * @param[in]   hook_create     hook function
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ico_win_mgr_hook_create(void (*hook_create)(struct wl_client *client,
                                            struct weston_surface *surface,
                                            int surfaceId, const char *appid))
{
    uifw_trace("ico_win_mgr_hook_create: Hook %08x", (int)hook_create);
    win_mgr_hook_create = hook_create;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_win_mgr_hook_destroy: set hook function for destroy surface
 *
 * @param[in]   hook_destroy        hook function
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ico_win_mgr_hook_destroy(void (*hook_destroy)(struct weston_surface *surface))
{
    uifw_trace("ico_win_mgr_hook_destroy: Hook %08x", (int)hook_destroy);
    win_mgr_hook_destroy = hook_destroy;
}


/*--------------------------------------------------------------------------*/
/**
 * @brief   module_init: initialize ico_window_mgr
 *                       this function called from ico_pluign_loader
 *
 * @param[in]   es          weston compositor
 * @return      result
 * @retval      0           sccess
 * @retval      -1          error
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT int
module_init(struct weston_compositor *ec)
{
    int     nodeId;

    uifw_info("ico_window_mgr: Enter(module_init)");

    _ico_win_mgr = (struct ico_win_mgr *)malloc(sizeof(struct ico_win_mgr));
    if (_ico_win_mgr == NULL)   {
        uifw_error("ico_window_mgr: malloc failed");
        return -1;
    }

    memset(_ico_win_mgr, 0, sizeof(struct ico_win_mgr));
    _ico_win_mgr->surfaceid_map = (uint16_t *) malloc(INIT_SURFACE_IDS/8);
    if (! _ico_win_mgr->surfaceid_map)  {
        uifw_error("ico_window_mgr: malloc failed");
        return -1;
    }
    uifw_trace("ico_window_mgr: sh=%08x", (int)_ico_win_mgr);
    memset(_ico_win_mgr->surfaceid_map, 0, INIT_SURFACE_IDS/8);

    _ico_win_mgr->compositor = ec;

    _ico_win_mgr->surfaceid_max = INIT_SURFACE_IDS;
    _ico_win_mgr->surfaceid_count = INIT_SURFACE_IDS;

    uifw_trace("ico_window_mgr: wl_display_add_global(bind_ico_win_mgr)");
    if (wl_display_add_global(ec->wl_display, &ico_window_mgr_interface,
                              _ico_win_mgr, bind_ico_win_mgr) == NULL)  {
        uifw_error("ico_window_mgr: Error(wl_display_add_global)");
        return -1;
    }

    wl_list_init(&_ico_win_mgr->surface_list);
    wl_list_init(&_ico_win_mgr->client_list);
    wl_list_init(&_ico_win_mgr->manager_list);

    nodeId = ico_ivi_get_mynode();
    _ico_win_mgr->surface_head = ICO_IVI_SURFACEID_BASE(nodeId);
    uifw_trace("ico_window_mgr: NoedId=%08x SurfaceIdBase=%08x",
                nodeId, _ico_win_mgr->surface_head);

    /* Regist usurf_2_node to ivi_common plugin     */
    ico_ivi_set_usurf_2_node(ico_winmgr_usurf_2_node);

    /* Regist send_to_mgr to ivi_common plugin      */
    ico_ivi_set_send_to_mgr(ico_win_mgr_send_to_mgr);
    ico_ivi_set_send_surface_change(win_mgr_surface_change_mgr);

    /* Hook to IVI-Shell                            */
    ivi_shell_hook_bind(bind_shell_client);
    ivi_shell_hook_unbind(unbind_shell_client);
    ivi_shell_hook_create(client_register_surface);
    ivi_shell_hook_destroy(win_mgr_surface_destroy);
    ivi_shell_hook_map(win_mgr_map_surface);
    ivi_shell_hook_change(win_mgr_surface_change);
    ivi_shell_hook_select(win_mgr_surface_select);

    uifw_info("ico_window_mgr: Leave(module_init)");

    return 0;
}

