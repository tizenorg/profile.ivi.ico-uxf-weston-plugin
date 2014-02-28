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
 * @brief   Multi Window Manager (Weston(Wayland) PlugIn)
 *
 * @date    Feb-21-2014
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
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <pixman.h>
#include <wayland-server.h>
#include <dirent.h>
#include <aul/aul.h>
#include <bundle.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <weston/compositor.h>

/* detail debug log */
#define UIFW_DETAIL_OUT 0                   /* 1=detail debug log/0=no detail log   */

#include <weston/weston-layout.h>
#include "ico_ivi_common_private.h"
#include "ico_window_mgr_private.h"
#include "ico_window_mgr-server-protocol.h"

/* gl_surface_state (inport from weston-1.4.0/src/gl-renderer.c */
enum buffer_type {
    BUFFER_TYPE_NULL,
    BUFFER_TYPE_SHM,
    BUFFER_TYPE_EGL
};
struct uifw_gl_surface_state {      /* struct gl_surface_state  */
    GLfloat color[4];
    struct gl_shader *shader;

    GLuint textures[3];
    int num_textures;
    int needs_full_upload;
    pixman_region32_t texture_damage;

    void *images[3];            /* EGLImageKHR */
    GLenum target;
    int num_images;

    struct weston_buffer_reference buffer_ref;
    enum buffer_type buffer_type;
    int pitch; /* in pixels */
    int height; /* in pixels */
    int y_inverted;         /* add weston 1.3.x */
};

/* SurfaceID                            */
#define UIFW_HASH           64              /* Hash value (2's compliment)          */
#define SURCAFE_ID_MASK     0x0ffff         /* SurfaceId hash mask pattern          */

/* Internal fixed value                 */
#define ICO_WINDOW_MGR_APPID_FIXCOUNT   5   /* retry count of appid fix             */
                                            /* show/hide animation with position    */
#define ICO_WINDOW_MGR_ANIMATION_POS    0x10000000

/* Multi Windiw Manager                 */
struct ico_win_mgr {
    struct weston_compositor *compositor;   /* Weston compositor                    */
    void    *shell;                         /* shell table address                  */
    int32_t surface_head;                   /* (HostID << 24) | (DisplayNo << 16)   */

    struct wl_list  client_list;            /* Clients                              */
    struct wl_list  manager_list;           /* Manager(ex.HomeScreen) list          */

    struct wl_list  map_list;               /* surface map list                     */
    struct uifw_surface_map *free_maptable; /* free maped surface table list        */
    struct weston_animation map_animation[ICO_IVI_MAX_DISPLAY];
                                            /* animation for map check              */
    struct wl_event_source  *wait_mapevent; /* map event send wait timer            */

    struct uifw_win_surface *idhash[UIFW_HASH];  /* UIFW SerfaceID                  */
    struct uifw_win_surface *wshash[UIFW_HASH];  /* Weston Surface                  */

    char    shell_init;                     /* shell initialize flag                */
    char    res[3];                         /* (unused)                             */
};

/* Internal macros                      */
/* UIFW SurfaceID                       */
#define MAKE_IDHASH(v)  (((uint32_t)v) & (UIFW_HASH-1))
/* Weston Surface                       */
#define MAKE_WSHASH(v)  ((((uint32_t)v) >> 5) & (UIFW_HASH-1))

/* function prototype                   */
                                            /* get surface table from weston surface*/
static struct uifw_win_surface *find_uifw_win_surface_by_ws(
                    struct weston_surface *wsurf);
                                            /* bind shell client                    */
static void win_mgr_bind_client(struct wl_client *client, void *shell);
                                            /* destroy client                       */
static void win_mgr_destroy_client(struct wl_listener *listener, void *data);
#if 0       /* work around: Walk through child processes until app ID is found  */
                                            /* get parent pid                       */
static pid_t win_mgr_get_ppid(pid_t pid);
#endif      /* work around: Walk through child processes until app ID is found  */
                                            /* get appid from pid                   */
static void win_mgr_get_client_appid(struct uifw_client *uclient);
                                            /* create new surface                   */
static void win_mgr_register_surface(uint32_t id_surface, struct weston_surface *surface,
                                     struct wl_client *client,
                                     struct weston_layout_surface *ivisurf);
                                            /* surface destroy                      */
static void win_mgr_destroy_surface(struct weston_surface *surface);
                                            /* window manager surface configure     */
static void win_mgr_surface_configure(struct uifw_win_surface *usurf,
                                      int x, int y, int width, int height);
                                            /* set surface animation                */
static void uifw_set_animation(struct wl_client *client, struct wl_resource *resource,
                               uint32_t surfaceid, int32_t type,
                               const char *animation, int32_t time);
                                            /* check and change all mapped surface  */
static void win_mgr_check_mapsurface(struct weston_animation *animation,
                                     struct weston_output *output, uint32_t msecs);
                                            /* check timer of mapped surface        */
static int win_mgr_timer_mapsurface(void *data);
                                            /* check and change mapped surface      */
static void win_mgr_change_mapsurface(struct uifw_surface_map *sm, int event,
                                      uint32_t curtime);
                                            /* map surface to system application    */
static void uifw_map_surface(struct wl_client *client, struct wl_resource *resource,
                             uint32_t surfaceid, int32_t framerate, const char *filepath);
                                            /* unmap surface                        */
static void uifw_unmap_surface(struct wl_client *client, struct wl_resource *resource,
                               uint32_t surfaceid);
                                            /* bind manager                         */
static void bind_ico_win_mgr(struct wl_client *client,
                             void *data, uint32_t version, uint32_t id);
                                            /* unbind manager                       */
static void unbind_ico_win_mgr(struct wl_resource *resource);
                                            /* convert animation name to Id value   */
static int ico_get_animation_name(const char *animation);
                                            /* send event to controller             */
static void win_mgr_send_event(int event, uint32_t surfaceid, uint32_t arg1);
                                            /* touch/click select surface           */
static void win_mgr_select_surface(struct weston_seat *seat,
                                   struct weston_surface *focus, int target);
static void win_mgr_click_to_activate(struct weston_seat *seat, uint32_t time,
                                      uint32_t button, void *data);
static void win_mgr_touch_to_activate(struct weston_seat *seat, uint32_t time,
                                      void *data);
                                            /* hook for create surface of ivi-shell */
static void ico_ivi_surfaceCreateNotification(struct weston_layout_surface *ivisurf,
                                              void *userdata);
                                            /* hook for remove surface of ivi-shell */
static void ico_ivi_surfaceRemoveNotification(struct weston_layout_surface *ivisurf,
                                              void *userdata);
                                            /* hook for configure surface of ivi-shell*/
static void ico_ivi_surfaceConfigureNotification(struct weston_layout_surface *ivisurf,
                                                 void *userdata);
                                            /* hook for property change of ivi-shell*/
static void ico_ivi_surfacePropertyNotification(struct weston_layout_surface *ivisurf,
                                                struct weston_layout_SurfaceProperties *prop,
                                                enum weston_layout_notification_mask mask,
                                                void *userdata);
                                            /* hook for animation                   */
static int  (*win_mgr_hook_animation)(const int op, void *data) = NULL;
                                            /* hook for input region                */
static void (*win_mgr_hook_change)(struct uifw_win_surface *usurf) = NULL;
static void (*win_mgr_hook_destory)(struct uifw_win_surface *usurf) = NULL;
static void (*win_mgr_hook_inputregion)(int set, struct uifw_win_surface *usurf,
                                        int32_t x, int32_t y, int32_t width,
                                        int32_t height, int32_t hotspot_x, int32_t hotspot_y,
                                        int32_t cursor_x, int32_t cursor_y,
                                        int32_t cursor_width, int32_t cursor_height,
                                        uint32_t attr) = NULL;

/* static tables                        */
/* Multi Window Manager interface       */
static const struct ico_window_mgr_interface ico_window_mgr_implementation = {
    uifw_set_animation,
    uifw_map_surface,
    uifw_unmap_surface
};


/* plugin common value(without ico_plugin_loader)   */
static int  _ico_ivi_option_flag = 0;           /* option flags                     */
static int  _ico_ivi_debug_level = 3;           /* debug Level                      */
static char *_ico_ivi_animation_name = NULL;    /* default animation name           */
static int  _ico_ivi_animation_time = 500;      /* default animation time           */
static int  _ico_ivi_animation_fps = 30;        /* animation frame rate             */

/* static management table              */
static struct ico_win_mgr       *_ico_win_mgr = NULL;
static int                      _ico_num_nodes = 0;
static struct uifw_node_table   _ico_node_table[ICO_IVI_MAX_DISPLAY];
static struct weston_seat       *touch_check_seat = NULL;


/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_optionflag: get option flags
 *
 * @param       None
 * @return      option flags
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_optionflag(void)
{
    return _ico_ivi_option_flag;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_debuglevel: answer debug output level.
 *
 * @param       none
 * @return      debug output level
 * @retval      0       No debug output
 * @retval      1       Only error output
 * @retval      2       Error and Warning output
 * @retval      3       Error, Warning and information output
 * @retval      4       Error, Warning, information and Debug Trace output
 * @retval      5       All output with debug write
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_debuglevel(void)
{
    return (_ico_ivi_debug_level & 0x0ffff);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_debugflag: get debug flags
 *
 * @param       None
 * @return      debug flags
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_debugflag(void)
{
    return ((_ico_ivi_debug_level >> 16) & 0x0ffff);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_default_animation_name: get default animation name
 *
 * @param       None
 * @return      Default animation name
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   const char *
ico_ivi_default_animation_name(void)
{
    return _ico_ivi_animation_name;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_default_animation_time: get default animation time
 *
 * @param       None
 * @return      Default animation time(miri sec)
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_default_animation_time(void)
{
    return _ico_ivi_animation_time;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_default_animation_fps: get default animation frame rate
 *
 * @param       None
 * @return      Default animation frame rate(frames/sec)
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_default_animation_fps(void)
{
    return _ico_ivi_animation_fps;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_get_mynode: Get my NodeId
 *
 * @param       None
 * @return      NodeId of my node
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_get_mynode(void)
{
    /* Reference Platform 0.90 only support 1 ECU   */
    return 0;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_surface_buffer_width: get surface buffer width
 *
 * @param[in]   es          weston surface
 * @return      buffer width(if surface has no buffer, return 0)
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_surface_buffer_width(struct weston_surface *es)
{
    int v;

    if (! es->buffer_ref.buffer)    {
        return 0;
    }
    if (es->buffer_viewport.viewport_set)   {
        return es->buffer_viewport.dst_width;
    }
    switch (es->buffer_viewport.transform) {
    case WL_OUTPUT_TRANSFORM_90:
    case WL_OUTPUT_TRANSFORM_270:
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        v = es->buffer_ref.buffer->height;
        break;
    default:
        v = es->buffer_ref.buffer->width;
        break;
    }
    return (v / es->buffer_viewport.scale);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_surface_buffer_height: get surface buffer height
 *
 * @param[in]   es          weston surface
 * @return      buffer height(if surface has no buffer, return 0)
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_surface_buffer_height(struct weston_surface *es)
{
    int v;

    if (! es->buffer_ref.buffer)    {
        return 0;
    }
    if (es->buffer_viewport.viewport_set)   {
        return es->buffer_viewport.dst_height;
    }
    switch (es->buffer_viewport.transform) {
    case WL_OUTPUT_TRANSFORM_90:
    case WL_OUTPUT_TRANSFORM_270:
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        v = es->buffer_ref.buffer->width;
        break;
    default:
        v = es->buffer_ref.buffer->height;
        break;
    }
    return (v / es->buffer_viewport.scale);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_surface_buffer_size: get surface buffer size
 *
 * @param[in]   es          weston surface
 * @param[out]  width       buffer width
 * @param[out]  height      buffer height
 * @return      nothing
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_ivi_surface_buffer_size(struct weston_surface *es, int *width, int *height)
{
    if (! es->buffer_ref.buffer)    {
        *width = 0;
        *height = 0;
    }
    else if (es->buffer_viewport.viewport_set)  {
        *width = es->buffer_viewport.dst_width;
        *height = es->buffer_viewport.dst_height;
    }
    else    {
        switch (es->buffer_viewport.transform) {
        case WL_OUTPUT_TRANSFORM_90:
        case WL_OUTPUT_TRANSFORM_270:
        case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        case WL_OUTPUT_TRANSFORM_FLIPPED_270:
            *width = es->buffer_ref.buffer->height;
            *height = es->buffer_ref.buffer->width;
            break;
        default:
            *width = es->buffer_ref.buffer->width;
            *height = es->buffer_ref.buffer->height;
            break;
        }
        *width = *width / es->buffer_viewport.scale;
        *height = *height / es->buffer_viewport.scale;
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_surface_buffer_size: get surface buffer size
 *
 * @param[in]   es          weston surface
 * @param[out]  width       buffer width
 * @param[out]  height      buffer height
 * @return      nothing
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   struct weston_view *
ico_ivi_get_primary_view(struct uifw_win_surface *usurf)
{
    struct weston_view  *ev = NULL;
    struct shell_surface *shsurf;

    if (_ico_win_mgr->compositor->shell_interface.get_primary_view) {
        shsurf = usurf->surface->configure_private;
        if (shsurf) {
            ev = (*_ico_win_mgr->compositor->shell_interface.get_primary_view)(NULL, shsurf);
        }
    }
    if (! ev)   {
        ev = weston_layout_get_weston_view(usurf->ivisurf);
    }
    if (! ev)   {
        uifw_error("ico_ivi_get_primary_view: usurf=%08x(%x) surface=%08x has no view",
                   (int)usurf, usurf->surfaceid, (int)usurf->surface);
    }
    else    {
        uifw_debug("ico_ivi_get_primary_view: %08x view=%08x view->surf=%08x",
                   usurf->surfaceid, (int)ev, (int)ev->surface);
    }
    return ev;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_set_weston_surface: set weston surface
 *
 * @param[in]   usurf       UIFW surface
 * @param[in]   x           X coordinate on screen
 * @param[in]   y           Y coordinate on screen
 * @param[in]   width       width
 * @param[in]   height      height
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_window_mgr_set_weston_surface(struct uifw_win_surface *usurf,
                                  int x, int y, int width, int height)
{
    struct weston_surface *es = usurf->surface;
    struct weston_view  *ev;
    int     buf_width, buf_height;

    if ((es == NULL) || (usurf->ivisurf == NULL))    {
        uifw_trace("ico_window_mgr_set_weston_surface: usurf(%08x) has no surface",
                   (int)usurf);
        return;
    }

    if (es->buffer_ref.buffer != NULL)   {
        ico_ivi_surface_buffer_size(es, &buf_width, &buf_height);
        if ((width <= 0) || (height <= 0))    {
            width = buf_width;
            usurf->width = buf_width;
            height = buf_height;
            usurf->height = buf_height;
        }
        if (usurf->width > buf_width)   {
            width = buf_width;
            x += (usurf->width - buf_width)/2;
        }
        if (usurf->height > buf_height) {
            height = buf_height;
            y += (usurf->height - buf_height)/2;
        }
        if (usurf->visible) {
            x += usurf->node_tbl->disp_x;
            y += usurf->node_tbl->disp_y;
        }
        else    {
            x = ICO_IVI_MAX_COORDINATE+1;
            y = ICO_IVI_MAX_COORDINATE+1;
        }
        ev = ico_ivi_get_primary_view(usurf);
        if ((ev != NULL) &&
            (((int)ev->geometry.x != x) || ((int)ev->geometry.y != y) ||
             (es->width != width) || (es->height != height)))   {
            weston_view_damage_below(ev);
            win_mgr_surface_configure(usurf, x, y, width, height);
        }
        weston_surface_damage(es);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_get_usurf: find UIFW surface by surface id
 *
 * @param[in]   surfaceid   UIFW surface id
 * @return      UIFW surface table address
 * @retval      !=NULL      success(surface table address)
 * @retval      NULL        error(surface id dose not exist)
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   struct uifw_win_surface *
ico_window_mgr_get_usurf(const uint32_t surfaceid)
{
    struct uifw_win_surface *usurf;

    usurf = _ico_win_mgr->idhash[MAKE_IDHASH(surfaceid)];

    while (usurf)   {
        if (usurf->surfaceid == surfaceid) {
            return usurf;
        }
        usurf = usurf->next_idhash;
    }
    uifw_trace("ico_window_mgr_get_usurf: NULL");
    return NULL;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_get_usurf_client: find UIFW surface by surface id/or client
 *
 * @param[in]   surfaceid   UIFW surface id
 * @param[in]   client      Wayland client
 * @return      UIFW surface table address
 * @retval      !=NULL      success(surface table address)
 * @retval      NULL        error(surface id or client dose not exist)
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   struct uifw_win_surface *
ico_window_mgr_get_usurf_client(const uint32_t surfaceid, struct wl_client *client)
{
    struct uifw_win_surface *usurf;

    usurf = ico_window_mgr_get_usurf(surfaceid);
    return usurf;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   find_uifw_win_surface_by_ws: find UIFW surface by weston surface
 *
 * @param[in]   wsurf       Weston surface
 * @return      UIFW surface table address
 * @retval      !=NULL      success(surface table address)
 * @retval      NULL        error(surface dose not exist)
 */
/*--------------------------------------------------------------------------*/
static struct uifw_win_surface *
find_uifw_win_surface_by_ws(struct weston_surface *wsurf)
{
    struct uifw_win_surface *usurf;

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
 * @brief   ico_window_mgr_find_uclient: find UIFW client by wayland client
 *
 * @param[in]   client      Wayland client
 * @return      UIFW client table address
 * @retval      !=NULL      success(client table address)
 * @retval      NULL        error(client dose not exist)
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   struct uifw_client*
ico_window_mgr_find_uclient(struct wl_client *client)
{
    struct uifw_client  *uclient;

    wl_list_for_each (uclient, &_ico_win_mgr->client_list, link)    {
        if (uclient->client == client)  {
            return(uclient);
        }
    }
    uifw_trace("ico_window_mgr_find_uclient: client.%08x is NULL", (int)client);
    return NULL;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_get_appid: find application id by wayland client
 *
 * @param[in]   client      Wayland client
 * @return      application id
 * @retval      !=NULL      success(application id)
 * @retval      NULL        error(client dose not exist)
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   char *
ico_window_mgr_get_appid(struct wl_client* client)
{
    struct uifw_client  *uclient;

    uclient = ico_window_mgr_find_uclient(client);

    if (! uclient)  {
        return NULL;
    }
    return uclient->appid;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_get_display_coordinate: get display coordinate
 *
 * @param[in]   displayno   display number
 * @param[out]  x           relative X coordinate
 * @param[out]  y           relative Y coordinate
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_window_mgr_get_display_coordinate(int displayno, int *x, int *y)
{
    if ((displayno <= _ico_num_nodes) || (displayno < 0))   {
        displayno = 0;
    }
    *x = _ico_node_table[displayno].disp_x;
    *y = _ico_node_table[displayno].disp_y;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_bind_client: desktop_shell from client
 *
 * @param[in]   client          Wayland client
 * @param[in]   shell           shell table address
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_bind_client(struct wl_client *client, void *shell)
{
    struct uifw_client  *uclient;
    int     newclient;
    pid_t   pid;
    uid_t   uid;
    gid_t   gid;

    uifw_trace("win_mgr_bind_client: Enter(client=%08x, shell=%08x)",
               (int)client, (int)shell);

    /* save shell table address             */
    if (shell)  {
        _ico_win_mgr->shell = shell;
    }

    /* set client                           */
    uclient = ico_window_mgr_find_uclient(client);
    if (! uclient)  {
        /* client not exist, create client management table             */
        uifw_trace("win_mgr_bind_client: Create Client");
        uclient = (struct uifw_client *)malloc(sizeof(struct uifw_client));
        if (!uclient)   {
            uifw_error("win_mgr_bind_client: Error, No Memory");
            return;
        }
        memset(uclient, 0, sizeof(struct uifw_client));
        uclient->client = client;
        wl_list_init(&uclient->surface_link);
        newclient = 1;
        uclient->destroy_listener.notify = win_mgr_destroy_client;
        wl_client_add_destroy_listener(client, &uclient->destroy_listener);
    }
    else    {
        newclient = 0;
    }
    wl_client_get_credentials(client, &pid, &uid, &gid);
    uifw_trace("win_mgr_bind_client: client=%08x pid=%d uid=%d gid=%d",
               (int)client, (int)pid, (int)uid, (int)gid);
    if (pid > 0)    {
        uclient->pid = (int)pid;
        /* get applicationId from AppCore(AUL)  */
        win_mgr_get_client_appid(uclient);

        if (newclient > 0)  {
            wl_list_insert(&_ico_win_mgr->client_list, &uclient->link);
        }
    }
    else    {
        uifw_trace("win_mgr_bind_client: client=%08x pid dose not exist", (int)client);
    }
    uifw_trace("win_mgr_bind_client: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_destroy_client: destroy client
 *
 * @param[in]   listener    listener
 * @param[in]   data        listener
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_destroy_client(struct wl_listener *listener, void *data)
{
    struct uifw_client  *uclient;

    uclient = container_of(listener, struct uifw_client, destroy_listener);

    uifw_trace("win_mgr_destroy_client: Enter(uclient=%08x)", (int)uclient);

    if (uclient)    {
        /* Client exist, Destory client management table             */
        wl_list_remove(&uclient->link);
        free(uclient);
    }
    uifw_trace("win_mgr_destroy_client: Leave");
}

#if 0       /* work around: Walk through child processes until app ID is found  */
/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_get_ppid: Get parent process ID.
 *
 * Similar to getppid(), except that this implementation accepts an
 * arbitrary process ID.
 *
 * @param[in]   pid     Process ID of child process
 * @return      parent process ID on success, -1 on failure
 */
/*--------------------------------------------------------------------------*/
static pid_t
win_mgr_get_ppid(pid_t pid)
{
    pid_t ppid = -1;
    char procpath[PATH_MAX] = { 0 };

    snprintf(procpath, sizeof(procpath)-1, "/proc/%d/status", pid);

    /* We better have read permissions! */
    int const fd = open(procpath, O_RDONLY);

    if (fd < 0)
        return ppid;

    char buffer[1024] = { 0 };

    ssize_t const size = read(fd, buffer, sizeof(buffer));
    close(fd);

    if (size <= 0)
        return ppid;

    /* Find line containing the parent process ID. */
    char const * const ppid_line = strstr(buffer, "PPid");

    if (ppid_line != NULL)
        sscanf(ppid_line, "PPid:    %d", &ppid);

    return ppid;
}
#endif      /* work around: Walk through child processes until app ID is found  */

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_get_client_appid: get applicationId from pid
 *
 * @param[in]   uclient     UIFW client management table
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_get_client_appid(struct uifw_client *uclient)
{
    int status = AUL_R_ERROR;

    memset(uclient->appid, 0, ICO_IVI_APPID_LENGTH);

#if 0       /* work around: Walk through child processes until app ID is found  */
    /*
     * Walk the parent process chain until we find a parent process
     * with an app ID.
     */
    int pid;

    for (pid = uclient->pid;
         pid > 1 && status != AUL_R_OK;
         pid = win_mgr_get_ppid(pid)) {

        status = aul_app_get_appid_bypid(pid,
                                         uclient->appid,
                                         ICO_IVI_APPID_LENGTH);

        uifw_trace("win_mgr_get_client_appid: aul_app_get_appid_bypid ret=%d "
                   "pid=%d appid=<%s>", status, pid, uclient->appid);
    }
    /*
     * Walk the child process chain as well since app ID was not yet found
     */
    if (status != AUL_R_OK) {

        DIR *dr;
        struct dirent *de;
        struct stat ps;
        pid_t   tpid;
        uid_t   uid;
        gid_t   gid;

        dr = opendir("/proc/");

        /* get uid */
        wl_client_get_credentials(uclient->client, &tpid, &uid, &gid);

        while(((de = readdir(dr)) != NULL) && (status != AUL_R_OK)) {

            char fullpath[PATH_MAX] = { 0 };
            int is_child = 0;
            int tmppid;

            snprintf(fullpath, sizeof(fullpath)-1, "/proc/%s", de->d_name);

            if (stat(fullpath, &ps) == -1) {
                continue;
            }

            /* find pid dirs for this user (uid) only */
            if (ps.st_uid != uid)
                continue;

            pid = atoi(de->d_name);

            /* check if it's a valid child */
            if (pid < uclient->pid)
                continue;

            /* scan up to pid to find if a chain exists */
            for (tmppid = pid; tmppid > uclient->pid;) {
                tmppid = win_mgr_get_ppid(tmppid);
                if (tmppid == uclient->pid)
                    is_child = 1;
            }

            if (is_child) {
                status = aul_app_get_appid_bypid(pid, uclient->appid,
                                                      ICO_IVI_APPID_LENGTH);

                uifw_debug("win_mgr_get_client_appid: aul_app_get_appid_bypid "
                           "ret=%d pid=%d appid=<%s>", status, pid,
                           uclient->appid);
            }
        }
    }
#else       /* work around: Walk through child processes until app ID is found  */
    status = aul_app_get_appid_bypid(uclient->pid, uclient->appid, ICO_IVI_APPID_LENGTH);
    uifw_trace("win_mgr_get_client_appid: aul_app_get_appid_bypid ret=%d "
               "pid=%d appid=<%s>", status, uclient->pid, uclient->appid);
#endif      /* work around: Walk through child processes until app ID is found  */

    if (uclient->appid[0] != 0) {
        /* OK, end of get appid         */
        uclient->fixed_appid = ICO_WINDOW_MGR_APPID_FIXCOUNT;
    }
    else    {
        /* client does not exist in AppCore, search Linux process table */

        int     fd;
        int     size;
        int     i;
        int     j;
        char    procpath[128];

        uclient->fixed_appid ++;
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
        for (i = strlen(uclient->appid)-1; i >= 0; i--) {
            if (uclient->appid[i] != ' ')   break;
        }
        uclient->appid[i+1] = 0;
        if (uclient->appid[0])  {
            uifw_trace("win_mgr_get_client_appid: pid=%d appid=<%s> from "
                       "Process table(%d)",
                       uclient->pid, uclient->appid, uclient->fixed_appid );
        }
        else    {
            uifw_trace("win_mgr_get_client_appid: pid=%d dose not exist in Process table",
                       uclient->pid);
            sprintf(uclient->appid, "?%d?", uclient->pid);
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_get_animation_name: convert animation name to Id value
 *
 * @param[in]   animation       animation name
 * @return      animation Id value
 */
/*--------------------------------------------------------------------------*/
static int
ico_get_animation_name(const char *animation)
{
    int anima = ICO_WINDOW_MGR_ANIMATION_NONE;

    if (strcasecmp(animation, "none") == 0) {
        return ICO_WINDOW_MGR_ANIMATION_NONE;
    }

    if (win_mgr_hook_animation) {
        anima = (*win_mgr_hook_animation)(ICO_WINDOW_MGR_ANIMATION_NAME, (void *)animation);
    }
    if (anima <= 0) {
        anima = ICO_WINDOW_MGR_ANIMATION_NONE;
    }
    return anima;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_send_event: send event to controller
 *
 * @param[in]   event           event code
 * @param[in]   surfaceid       surface id
 * @param[in]   arg1            argument 1
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_send_event(int event, uint32_t surfaceid, uint32_t arg1)
{
    struct uifw_manager     *mgr;

    /* send event to manager     */
    wl_list_for_each (mgr, &_ico_win_mgr->manager_list, link)   {
        switch (event)  {
        case ICO_WINDOW_MGR_WINDOW_ACTIVE:      /* active event             */
            uifw_trace("win_mgr_send_event: Send ACTIVE(surf=%08x, select=%x)",
                       surfaceid, arg1);
            ico_window_mgr_send_window_active(mgr->resource, surfaceid, arg1);
            break;
        case ICO_WINDOW_MGR_DESTROY_SURFACE:    /* surface destroy event    */
            uifw_trace("win_mgr_send_event: Send DESTROY_SURFACE(surf=%08x)", surfaceid);
            ico_window_mgr_send_destroy_surface(mgr->resource, surfaceid);
            break;
        default:
            uifw_error("win_mgr_send_event: Unknown event(%d)", event);
            break;
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_select_surface: select surface by mouse click
 *
 * @param[in]   seat            weston seat
 * @param[in]   focus           selected surface
 * @param[in]   select          selected device
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_select_surface(struct weston_seat *seat, struct weston_surface *focus, int select)
{
    struct weston_surface   *surface;
    struct uifw_win_surface *usurf;
    struct uifw_manager     *mgr;

    surface = weston_surface_get_main_surface(focus);

    uifw_trace("win_mgr_select_surface: Enter(%08x,%d)", (int)surface, select);

    if (! surface)  {
        uifw_trace("win_mgr_select_surface: Leave(no surface)");
        return;
    }
    /* find surface         */
    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("win_mgr_select_surface: Leave(usurf not exist)");
        return;
    }
    /* surface active       */
    weston_surface_activate(surface, seat);

    /* send active event to manager     */
    wl_list_for_each (mgr, &_ico_win_mgr->manager_list, link)   {
        uifw_trace("win_mgr_select_surface: Send Manager ACTIVE(surf=%08x)",
                   usurf->surfaceid);
        ico_window_mgr_send_window_active(mgr->resource, usurf->surfaceid,
                                          select);
    }
    uifw_trace("win_mgr_select_surface: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_click_to_activate: select surface by mouse click
 *
 * @param[in]   seat            weston seat
 * @param[in]   time            click time(current time)
 * @param[in]   button          click button
 * @param[in]   data            user data(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_click_to_activate(struct weston_seat *seat, uint32_t time,
                          uint32_t button, void *data)
{
    if (seat->pointer->grab != &seat->pointer->default_grab)
        return;
    if (seat->pointer->focus == NULL)
        return;

    win_mgr_select_surface(seat, seat->pointer->focus->surface,
                           ICO_WINDOW_MGR_SELECT_POINTER);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_touch_to_activate: select surface by touch touch-panel
 *
 * @param[in]   seat            weston seat
 * @param[in]   time            click time(current time)
 * @param[in]   data            user data(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_touch_to_activate(struct weston_seat *seat, uint32_t time, void *data)
{
    if (seat->touch->grab != &seat->touch->default_grab)
        return;
    if (seat->touch->focus == NULL)
        return;

    win_mgr_select_surface(seat, seat->touch->focus->surface, ICO_WINDOW_MGR_SELECT_TOUCH);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_surfaceCreateNotification: create ivi-surface
 *
 * @param[in]   ivisurf         ivi surface
 * @param[in]   userdata        User Data(Unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_ivi_surfaceCreateNotification(struct weston_layout_surface *ivisurf, void *userdata)
{
    uint32_t                id_surface;
    struct weston_view      *ev;
    struct weston_surface   *es;
    struct wl_client        *client;

    id_surface = weston_layout_getIdOfSurface(ivisurf);
    uifw_trace("ico_ivi_surfaceCreateNotification: Create %x", id_surface);

    /* set property notification    */
    if (weston_layout_surfaceAddNotification(ivisurf, ico_ivi_surfacePropertyNotification, NULL) != 0)  {
        uifw_error("ico_ivi_surfaceCreateNotification: weston_layout_surfaceAddNotification Error");
    }
    ev = weston_layout_get_weston_view(ivisurf);
    if (! ev)   {
        uifw_error("ico_ivi_surfaceCreateNotification: weston_layout_get_weston_view Error");
    }
    else    {
        es = ev->surface;
        if (! es)   {
            uifw_error("ico_ivi_surfaceCreateNotification: no weston_surface");
        }
        else    {
            client = wl_resource_get_client(es->resource);
            if (! client)   {
                uifw_error("ico_ivi_surfaceCreateNotification: no wl_client");
            }
            else    {
                win_mgr_register_surface(id_surface, es, client, ivisurf);
            }
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_surfaceRemoveNotification: remove ivi-surface
 *
 * @param[in]   ivisurf         ivi surface
 * @param[in]   userdata        User Data(Unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_ivi_surfaceRemoveNotification(struct weston_layout_surface *ivisurf, void *userdata)
{
    uint32_t    id_surface;
    struct weston_view      *ev;
    struct weston_surface   *es;

    id_surface = weston_layout_getIdOfSurface(ivisurf);
    uifw_trace("ico_ivi_surfaceRemoveNotification: Remove %x", id_surface);

    ev = weston_layout_get_weston_view(ivisurf);
    if (! ev)   {
        uifw_error("ico_ivi_surfaceRemoveNotification: weston_layout_get_weston_view Error");
    }
    else    {
        es = ev->surface;
        if (! es)   {
            uifw_error("ico_ivi_surfaceRemoveNotification: no weston_surface");
        }
        else    {
            win_mgr_destroy_surface(es);
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_surfaceConfigureNotification: configure ivi-surface
 *
 * @param[in]   ivisurf         ivi surface
 * @param[in]   userdata        User Data(Unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_ivi_surfaceConfigureNotification(struct weston_layout_surface *ivisurf, void *userdata)
{
    struct weston_view *view;
    struct weston_surface *surface;
    uint32_t    id_surface;

    id_surface = weston_layout_getIdOfSurface(ivisurf);
    view = weston_layout_get_weston_view(ivisurf);
    if (! view) {
        uifw_trace("ico_ivi_surfaceConfigureNotification: %08x has no view",
                   id_surface);
    }
    else    {
        surface = view->surface;
        if (! surface)  {
            uifw_trace("ico_ivi_surfaceConfigureNotification: %08x has no surface",
                       id_surface);
        }
        else    {
            uifw_trace("ico_ivi_surfaceConfigureNotification: Configure %08x "
                       "x/y=%d/%d w/h=%d/%d",
                       id_surface, (int)view->geometry.x, (int)view->geometry.y,
                       surface->width, surface->height);
            weston_layout_surfaceSetSourceRectangle(ivisurf,
                        0, 0, surface->width, surface->height);
            weston_layout_surfaceSetDestinationRectangle(ivisurf,
                        (uint32_t)view->geometry.x, (uint32_t)view->geometry.y,
                        surface->width, surface->height);
            weston_layout_commitChanges();
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_surfacePropertyNotification: property change ivi-surface
 *
 * @param[in]   ivisurf         ivi surface
 * @param[in]   userdata        User Data(Unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ico_ivi_surfacePropertyNotification(struct weston_layout_surface *ivisurf,
                                    struct weston_layout_SurfaceProperties *prop,
                                    enum weston_layout_notification_mask mask,
                                    void *userdata)
{
    struct uifw_manager *mgr;
    uint32_t    id_surface;
    int         retanima;
    uint32_t    newmask;
    struct uifw_win_surface *usurf;

    newmask = ((uint32_t)mask) & (~(IVI_NOTIFICATION_OPACITY|IVI_NOTIFICATION_ORIENTATION|
                                    IVI_NOTIFICATION_PIXELFORMAT));
    id_surface = weston_layout_getIdOfSurface(ivisurf);
    usurf = ico_window_mgr_get_usurf(id_surface);

    if ((newmask != 0) && (usurf != NULL))  {
        uifw_trace("ico_ivi_surfacePropertyNotification: Property %x(%08x) usurf=%08x",
                   id_surface, newmask, (int)usurf);
        if (newmask & (IVI_NOTIFICATION_SOURCE_RECT|IVI_NOTIFICATION_DEST_RECT|
                       IVI_NOTIFICATION_POSITION|IVI_NOTIFICATION_DIMENSION))   {
            /* change position or size  */
            uifw_trace("ico_ivi_surfacePropertyNotification: %08x x/y=%d/%d->%d/%d "
                       "w/h=%d/%d->%d/%d(%d/%d)", id_surface, usurf->x, usurf->y,
                       prop->destX, prop->destY, usurf->width, usurf->height,
                       prop->destWidth, prop->destHeight,
                       prop->sourceWidth, prop->sourceHeight);
            if ((usurf->client_width == prop->sourceWidth) &&
                (usurf->client_height == prop->sourceHeight))   {
                newmask &= (~(IVI_NOTIFICATION_SOURCE_RECT|IVI_NOTIFICATION_DIMENSION));
            }
            else    {
                usurf->client_width = prop->sourceWidth;
                usurf->client_height = prop->sourceHeight;
            }
            if ((usurf->x == prop->destX) && (usurf->y == prop->destY) &&
                (usurf->width == prop->destWidth) && (usurf->height == prop->destHeight)) {
                newmask &= (~(IVI_NOTIFICATION_DEST_RECT|IVI_NOTIFICATION_POSITION));
            }
            else    {
                usurf->x = prop->destX;
                usurf->y = prop->destY;
                usurf->width = prop->destWidth;
                usurf->height = prop->destHeight;
            }
        }
        if (newmask & IVI_NOTIFICATION_VISIBILITY)  {
            if ((usurf->visible == 0) && (prop->visibility)) {
                uifw_trace("ico_ivi_surfacePropertyNotification: %08x Visible 0=>1",
                           id_surface);
                usurf->visible = 1;
                if ((usurf->animation.show_anima != ICO_WINDOW_MGR_ANIMATION_NONE) &&
                    (win_mgr_hook_animation != NULL))   {
                    /* show with animation      */
                    usurf->animation.pos_x = usurf->x;
                    usurf->animation.pos_y = usurf->y;
                    usurf->animation.pos_width = usurf->width;
                    usurf->animation.pos_height = usurf->height;
                    retanima =
                        (*win_mgr_hook_animation)(ICO_WINDOW_MGR_ANIMATION_OPSHOW,
                                                  (void *)usurf);
                    uifw_trace("ico_ivi_surfacePropertyNotification: ret call anima = %d",
                               retanima);
                }
            }
            else if ((usurf->visible != 0) && (! prop->visibility))  {
                uifw_trace("ico_ivi_surfacePropertyNotification: %08x Visible 1=>0",
                           id_surface);
                if ((usurf->animation.show_anima != ICO_WINDOW_MGR_ANIMATION_NONE) &&
                    (win_mgr_hook_animation != NULL))   {
                    /* hide with animation      */
                    usurf->animation.pos_x = usurf->x;
                    usurf->animation.pos_y = usurf->y;
                    usurf->animation.pos_width = usurf->width;
                    usurf->animation.pos_height = usurf->height;
                    retanima =
                        (*win_mgr_hook_animation)(ICO_WINDOW_MGR_ANIMATION_OPHIDE,
                                                  (void *)usurf);
                }
                else    {
                    retanima = ICO_WINDOW_MGR_ANIMATION_RET_ANIMA;
                }
                if (retanima != ICO_WINDOW_MGR_ANIMATION_RET_ANIMANOCTL)    {
                    usurf->visible = 0;
                    uifw_trace("ico_ivi_surfacePropertyNotification: Change to UnVisible");
                }
                else    {
                    usurf->visible = 1;
                    uifw_trace("ico_ivi_surfacePropertyNotification: Change to Visible");
                    weston_layout_surfaceSetVisibility(ivisurf, 1);
                    weston_layout_commitChanges();
                }
            }
            else    {
                uifw_trace("ico_ivi_surfacePropertyNotification: visible no change");
                newmask &= (~IVI_NOTIFICATION_VISIBILITY);
            }
        }

        if (newmask)    {
            /* surface changed, send event to controller    */
            wl_list_for_each (mgr, &_ico_win_mgr->manager_list, link)   {
                uifw_trace("win_mgr_send_event: Send UPDATE_SURFACE(surf=%08x)", id_surface);
                ico_window_mgr_send_update_surface(mgr->resource, id_surface,
                                usurf->visible, usurf->client_width,
                                usurf->client_height, usurf->x, usurf->y,
                                usurf->width, usurf->height);
            }
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_register_surface: create UIFW surface
 *
 * @param[in]   id_surface      surface id (by ivi-shell)
 * @param[in]   surface         Weston surface
 * @param[in]   client          Wayland client
 * @param[in]   ivisurf         weston layout surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_register_surface(uint32_t id_surface, struct weston_surface *surface,
                         struct wl_client *client, struct weston_layout_surface *ivisurf)
{
    struct uifw_win_surface *usurf;
    struct uifw_win_surface *phash;
    struct uifw_win_surface *bhash;
    uint32_t    hash;

    uifw_trace("win_mgr_register_surface: Enter(surf=%x[%08x],client=%08x,ivisurf=%08x)",
               id_surface, (int)surface, (int)client, (int)ivisurf);

    /* check new surface                    */
    if (find_uifw_win_surface_by_ws(surface))   {
        /* surface exist, NOP               */
        uifw_trace("win_mgr_register_surface: Leave(Already Exist)");
        return;
    }

    /* set default color and shader */
    weston_surface_set_color(surface, 0.0, 0.0, 0.0, 1.0);
    /* create UIFW surface management table */
    usurf = malloc(sizeof(struct uifw_win_surface));
    if (! usurf)    {
        uifw_error("win_mgr_register_surface: No Memory");
        return;
    }

    memset(usurf, 0, sizeof(struct uifw_win_surface));

    usurf->surfaceid = id_surface;
    usurf->surface = surface;
    usurf->ivisurf = ivisurf;
    usurf->node_tbl = &_ico_node_table[0];  /* set default node table (display no=0)    */
    wl_list_init(&usurf->client_link);
    wl_list_init(&usurf->animation.animation.link);
    wl_list_init(&usurf->surf_map);
    wl_list_init(&usurf->input_region);
    usurf->animation.hide_anima = ico_get_animation_name(ico_ivi_default_animation_name());
    usurf->animation.hide_time = ico_ivi_default_animation_time();
    usurf->animation.show_anima = usurf->animation.hide_anima;
    usurf->animation.show_time = usurf->animation.hide_time;
    usurf->animation.move_anima = usurf->animation.hide_anima;
    usurf->animation.move_time = usurf->animation.hide_time;
    usurf->animation.resize_anima = usurf->animation.hide_anima;
    usurf->animation.resize_time = usurf->animation.hide_time;
    usurf->visible = 0;

    /* set client                           */
    usurf->uclient = ico_window_mgr_find_uclient(client);
    if (! usurf->uclient)  {
        /* client not exist, create client management table */
        uifw_trace("win_mgr_register_surface: Create Client");
        win_mgr_bind_client(client, NULL);
        usurf->uclient = ico_window_mgr_find_uclient(client);
        if (! usurf->uclient)  {
            uifw_error("win_mgr_register_surface: No Memory");
            return;
        }
    }
    wl_list_insert(usurf->uclient->surface_link.prev, &usurf->client_link);

    /* make surface id hash table       */
    hash = MAKE_IDHASH(usurf->surfaceid);
    phash = _ico_win_mgr->idhash[hash];
    bhash = NULL;
    while (phash)   {
        bhash = phash;
        phash = phash->next_idhash;
    }
    if (bhash)  {
        bhash->next_idhash = usurf;
    }
    else    {
        _ico_win_mgr->idhash[hash] = usurf;
    }

    /* make weston surface hash table   */
    hash = MAKE_WSHASH(usurf->surface);
    phash = _ico_win_mgr->wshash[hash];
    bhash = NULL;
    while (phash)   {
        bhash = phash;
        phash = phash->next_wshash;
    }
    if (bhash)  {
        bhash->next_wshash = usurf;
    }
    else    {
        _ico_win_mgr->wshash[hash] = usurf;
    }
    uifw_trace("win_mgr_register_surface: Leave(surfaceId=%08x)", usurf->surfaceid);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_set_animation: set animation of surface visible/unvisible
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   surfaceid   UIFW surface id
 * @param[in]   type        how to change surface
 * @param[in]   anmation    animation name
 * @param[in]   time        animation time(ms), if 0, default time
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_animation(struct wl_client *client, struct wl_resource *resource,
                   uint32_t surfaceid, int32_t type, const char *animation, int32_t time)
{
    int     animaid;
    struct uifw_win_surface *usurf = ico_window_mgr_get_usurf_client(surfaceid, client);

    uifw_trace("uifw_set_transition: surf=%08x,type=%x,anim=%s,time=%d",
               surfaceid, type, animation, time);

    if (usurf) {
        if ((*animation != 0) && (*animation != ' '))   {
            animaid = ico_get_animation_name(animation);
            uifw_trace("uifw_set_animation: Leave(OK) type=%d", animaid);
            if (type & ICO_WINDOW_MGR_ANIMATION_TYPE_HIDE)  {
                if ((usurf->animation.type == ICO_WINDOW_MGR_ANIMATION_OPHIDE) ||
                    (usurf->animation.type == ICO_WINDOW_MGR_ANIMATION_OPHIDEPOS))  {
                    usurf->animation.next_anima = animaid;
                }
                else    {
                    usurf->animation.hide_anima = animaid;
                }
            }
            if (type & ICO_WINDOW_MGR_ANIMATION_TYPE_SHOW)  {
                if ((usurf->animation.type == ICO_WINDOW_MGR_ANIMATION_OPSHOW) ||
                    (usurf->animation.type == ICO_WINDOW_MGR_ANIMATION_OPSHOWPOS))  {
                    usurf->animation.next_anima = animaid;
                }
                else    {
                    usurf->animation.show_anima = animaid;
                }
            }
            if (type & ICO_WINDOW_MGR_ANIMATION_TYPE_MOVE)  {
                if (usurf->animation.type == ICO_WINDOW_MGR_ANIMATION_OPMOVE)    {
                    usurf->animation.next_anima = animaid;
                }
                else    {
                    usurf->animation.move_anima = animaid;
                }
            }
            if (type & ICO_WINDOW_MGR_ANIMATION_TYPE_RESIZE)    {
                if (usurf->animation.type == ICO_WINDOW_MGR_ANIMATION_OPRESIZE)  {
                    usurf->animation.next_anima = animaid;
                }
                else    {
                    usurf->animation.resize_anima = animaid;
                }
            }
        }
        if ((time > 0) && (time < 10000))   {
            if (type & ICO_WINDOW_MGR_ANIMATION_TYPE_HIDE)  {
                usurf->animation.hide_time = time;
            }
            if (type & ICO_WINDOW_MGR_ANIMATION_TYPE_SHOW)  {
                usurf->animation.show_time = time;
            }
            if (type & ICO_WINDOW_MGR_ANIMATION_TYPE_MOVE)  {
                usurf->animation.move_time = time;
            }
            if (type & ICO_WINDOW_MGR_ANIMATION_TYPE_RESIZE)    {
                usurf->animation.resize_time = time;
            }
        }
    }
    else    {
        uifw_trace("uifw_set_animation: Surface(%08x) Not exist", surfaceid);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_check_mapsurface: check and change all surface
 *
 * @param[in]   animation   weston animation table(unused)
 * @param[in]   outout      weston output table(unused)
 * @param[in]   mseces      current time(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_check_mapsurface(struct weston_animation *animation,
                         struct weston_output *output, uint32_t msecs)
{
    struct uifw_surface_map *sm, *sm_tmp;
    uint32_t    curtime;
    int         wait = 99999999;

    /* check touch down counter     */
    if ((touch_check_seat) &&
        (touch_check_seat->touch))  {
        if (touch_check_seat->touch->num_tp > 10)  {
            uifw_trace("win_mgr_check_mapsurface: illegal touch counter(num=%d), reset",
                       (int)touch_check_seat->touch->num_tp);
            touch_check_seat->touch->num_tp = 0;
        }
    }

    /* check all mapped surfaces    */
    curtime = weston_compositor_get_time();
    wl_list_for_each_safe (sm, sm_tmp, &_ico_win_mgr->map_list, map_link)   {
        uifw_detail("win_mgr_check_mapsurface: sm=%08x surf=%08x",
                    (int)sm, sm->usurf->surfaceid);
        win_mgr_change_mapsurface(sm, 0, curtime);
        if (sm->eventque)   {
            if (sm->interval < wait)    {
                wait = sm->interval;
            }
        }
    }

    /* check frame interval         */
    if (wait < 2000)    {
        wait = wait / 2;
    }
    else    {
        wait = 1000;
    }
    wl_event_source_timer_update(_ico_win_mgr->wait_mapevent, wait);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_timer_mapsurface: mapped surface check timer
 *
 * @param[in]   data        user data(unused)
 * @return      fixed 1
 */
/*--------------------------------------------------------------------------*/
static int
win_mgr_timer_mapsurface(void *data)
{
    win_mgr_check_mapsurface(NULL, NULL, 0);
    return 1;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_change_mapsurface: check and change mapped surface
 *
 * @param[in]   sm          map surface table
 * @param[in]   event       send event (if 0, send if changed)
 * @param[in]   curtime     current time(ms)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_change_mapsurface(struct uifw_surface_map *sm, int event, uint32_t curtime)
{
    struct weston_surface   *es;
    struct wl_shm_buffer    *shm_buffer;
    int         width;
    int         height;
    uint32_t    format;
    uint32_t    dtime;

    uifw_detail("win_mgr_change_mapsurface: surf=%08x event=%d", sm->usurf->surfaceid, event);
    if (event == 0) {
        event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_CONTENTS;
    }

    /* check if buffered        */
    es = sm->usurf->surface;
    if ((es == NULL) ||
        ((sm->type == ICO_WINDOW_MGR_MAP_TYPE_EGL) &&
         ((es->buffer_ref.buffer == NULL) ||
          (es->buffer_ref.buffer->width <= 0) || (es->buffer_ref.buffer->height <= 0)))) {
        /* surface has no buffer    */
        uifw_debug("win_mgr_change_mapsurface: surface(%08x) has no buffer %08x %08x",
                   sm->usurf->surfaceid, (int)es,
                   es ? (int)es->buffer_ref.buffer : 0);
        if (sm->initflag)   {
            event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP;
        }
        else    {
            event = 0;
        }
    }
    else if (sm->type == ICO_WINDOW_MGR_MAP_TYPE_EGL)   {
        if ((es->buffer_ref.buffer->legacy_buffer != NULL) && (es->renderer_state != NULL)) {
            if ((void *)wl_resource_get_user_data(
                            (struct wl_resource *)es->buffer_ref.buffer->legacy_buffer)
                == NULL)    {
                /* surface has no buffer    */
                uifw_debug("win_mgr_change_mapsurface: surface(%08x) has no buffer",
                           sm->usurf->surfaceid);
                if (sm->initflag)   {
                    event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP;
                }
                else    {
                    event = 0;
                }
            }
        }
    }
    else    {
        if (es->buffer_ref.buffer != NULL)  {
            shm_buffer = wl_shm_buffer_get(es->buffer_ref.buffer->resource);
            if (shm_buffer) {
                format = wl_shm_buffer_get_format(shm_buffer);
                if (format != WL_SHM_FORMAT_ARGB8888)   {
                    uifw_trace("win_mgr_change_mapsurface: %08x shm_buffer type %x",
                               sm->usurf->surfaceid, format);
                    event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP;
                }
            }
        }
    }

    if ((event != 0) && (event != ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP))  {

        if (sm->type == ICO_WINDOW_MGR_MAP_TYPE_EGL)    {
            format = EGL_TEXTURE_RGBA;          /* currently only support RGBA  */
            width = es->buffer_ref.buffer->width;
            height = es->buffer_ref.buffer->height;
            if ((sm->initflag == 0) && (width > 0) && (height > 0)) {
                sm->initflag = 1;
                event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_MAP;
            }
            else if ((width <= 0) || (height <= 0)) {
                event = 0;
            }
            else if (event == ICO_WINDOW_MGR_MAP_SURFACE_EVENT_CONTENTS)    {
                if ((sm->width != width) || (sm->height != height) ||
                    (format != sm->format)) {
                    event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_RESIZE;
                }
                else    {
                    if (es->buffer_ref.buffer->legacy_buffer != sm->curbuf) {
#if  PERFORMANCE_EVALUATIONS > 0
                        uifw_perf("SWAP_BUFFER appid=%s surface=%08x",
                                  sm->usurf->uclient->appid, sm->usurf->surfaceid);
#endif /*PERFORMANCE_EVALUATIONS*/
                        dtime = curtime - sm->lasttime;
                        if ((sm->interval > 0) && (dtime < sm->interval))   {
                            sm->eventque = 1;
                            event = 0;
                        }
                    }
                    else if (sm->eventque)  {
                        dtime = curtime - sm->lasttime;
                        if ((sm->interval > 0) && (dtime < sm->interval))   {
                            event = 0;
                        }
                    }
                    else    {
                        event =0;
                    }
                }
            }
            sm->width = width;
            sm->height = height;
            sm->stride = width * 4;
            sm->format = format;
            sm->curbuf = es->buffer_ref.buffer->legacy_buffer;
        }
        else    {
            if ((sm->eventque != 0) ||
                (es->buffer_ref.buffer == NULL) || (es->buffer_ref.buffer != sm->curbuf)) {
                sm->curbuf = es->buffer_ref.buffer;
                if (es->buffer_ref.buffer != NULL)  {
                    width = es->buffer_ref.buffer->width;
                    height = es->buffer_ref.buffer->height;
                }
                else    {
                    width = es->width;
                    height = es->height;
                }
                if ((sm->initflag == 0) && (width > 0) && (height > 0))    {
                    sm->initflag = 1;
                    event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_MAP;
                    uifw_detail("win_mgr_change_mapsurface: PIX MAP event %08x",
                                sm->usurf->surfaceid);
                }
                else    {
                    if ((width <= 0) || (height <= 0))  {
                        event = 0;
                        sm->curbuf = NULL;
                        uifw_detail("win_mgr_change_mapsurface: PIX %08x w/h=0/0",
                                    sm->usurf->surfaceid);
                    }
                    else if (event == ICO_WINDOW_MGR_MAP_SURFACE_EVENT_CONTENTS)    {
#if  PERFORMANCE_EVALUATIONS > 0
                        if (sm->type != ICO_WINDOW_MGR_MAP_TYPE_SHM)    {
                            uifw_perf("SWAP_BUFFER appid=%s surface=%08x",
                                      sm->usurf->uclient->appid, sm->usurf->surfaceid);
                        }
#endif /*PERFORMANCE_EVALUATIONS*/
                        if ((sm->width != width) || (sm->height != height)) {
                            event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_RESIZE;
                        }
                        else    {
                            dtime = curtime - sm->lasttime;
                            if ((sm->interval > 0) && (dtime < sm->interval))   {
                                sm->eventque = 1;
                                event = 0;
                                uifw_detail("win_mgr_change_mapsurface: PIX %08x new queue",
                                            sm->usurf->surfaceid);
                            }
                            else if (sm->eventque)  {
                                dtime = curtime - sm->lasttime;
                                if ((sm->interval > 0) && (dtime < sm->interval))   {
                                    event = 0;
                                    uifw_detail("win_mgr_change_mapsurface: PIX %08x queued",
                                                sm->usurf->surfaceid);
                                }
                            }
                        }
                    }
                }
                sm->width = width;
                sm->height = height;
                sm->stride = width * 4;
                sm->format = EGL_TEXTURE_RGBA;
            }
            else    {
                event = 0;
            }
        }
    }

    if (event != 0) {
        uifw_detail("win_mgr_change_mapsurface: send MAP event(ev=%d surf=%08x type=%d "
                    "w/h/s=%d/%d/%d format=%x",
                    event, sm->usurf->surfaceid, sm->type,
                    sm->width, sm->height, sm->stride, sm->format);
        sm->lasttime = curtime;
        sm->eventque = 0;
        if ((event != ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR) &&
            (sm->filepath != NULL)) {
            if (weston_layout_takeSurfaceScreenshot(sm->filepath,
                                                    sm->usurf->ivisurf) != 0)   {
                uifw_warn("win_mgr_change_mapsurface: surface.%08x image read(%s) Error",
                          sm->usurf->surfaceid, sm->filepath);
                event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR;
            }
        }
        ico_window_mgr_send_map_surface(sm->uclient->mgr->resource, event,
                                        sm->usurf->surfaceid, sm->type,
                                        sm->width, sm->height, sm->stride, sm->format);
        if (event == ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR)    {
            /* free map table if error  */
            wl_list_remove(&sm->surf_link);
            wl_list_remove(&sm->map_link);
            sm->usurf = (struct uifw_win_surface *)_ico_win_mgr->free_maptable;
            _ico_win_mgr->free_maptable = sm;
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_map_surface: mapped surface buffer to system application
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   surfaceid   surface id
 * @param[in]   framerate   frame rate of surface update(frame/sec)
 * @param[in]   filepath    surface image file path(if NULL, not create file)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_map_surface(struct wl_client *client, struct wl_resource *resource,
                 uint32_t surfaceid, int32_t framerate, const char *filepath)
{
    struct uifw_win_surface     *usurf;
    struct weston_surface       *es;
    struct uifw_surface_map     *sm;
    struct weston_buffer        *buffer;
    struct wl_shm_buffer        *shm_buffer;
    struct uifw_client          *uclient;
    struct uifw_gl_surface_state *gl_state;
    int     maptype;
    int     format;

    uifw_trace("uifw_map_surface: Enter(surface=%08x,fps=%d,file=%s)",
               surfaceid, framerate, filepath ? filepath : "(null)");

    uclient = ico_window_mgr_find_uclient(client);
    usurf = ico_window_mgr_get_usurf(surfaceid);
    if (! usurf)    {
        /* surface dose not exist, error        */
        ico_window_mgr_send_map_surface(resource, ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                        surfaceid, 1, 0, 0, 0, 0);
        uifw_trace("uifw_map_surface: Leave(surface=%08x dose not exist)", surfaceid);
        return;
    }

    /* check if buffered        */
    es = usurf->surface;
    if (es == NULL) {
        /* surface has no buffer, error         */
        ico_window_mgr_send_map_surface(resource, ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                        surfaceid, 2, 0, 0, 0, 0);
        uifw_trace("uifw_map_surface: Leave(surface(%08x) has no surface)", surfaceid);
        return;
    }
    buffer = es->buffer_ref.buffer;

    /* check buffer type        */
    gl_state = (struct uifw_gl_surface_state *)es->renderer_state;
    if (gl_state == NULL)   {
        ico_window_mgr_send_map_surface(resource,
                                        ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                        surfaceid, 3, 0, 0, 0, 0);
        uifw_trace("uifw_map_surface: Leave(surface(%08x) has no gl_state)", surfaceid);
        return;
    }
    else if (gl_state->buffer_type == BUFFER_TYPE_SHM)  {
        maptype = -1;
        format = 0xff;
        if (ico_ivi_optionflag() & ICO_IVI_OPTION_SUPPORT_SHM)  {
            if (buffer != NULL) {
                shm_buffer = wl_shm_buffer_get(buffer->resource);
                if (shm_buffer) {
                    format = wl_shm_buffer_get_format(shm_buffer);
                    uifw_detail("uifw_map_surface: %08x shm_buffer type %x",
                                surfaceid, format);
                    if (format == WL_SHM_FORMAT_ARGB8888)   {
                        maptype = ICO_WINDOW_MGR_MAP_TYPE_SHM;
                    }
                }
            }
            else    {
                maptype = ICO_WINDOW_MGR_MAP_TYPE_SHM;
            }
        }
        if (maptype < 0)    {
            ico_window_mgr_send_map_surface(resource,
                                            ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                            surfaceid, 4, 0, 0, 0, 0);
            uifw_trace("uifw_map_surface: Leave(surface(%08x) not support shm_buffer(%x))",
                       surfaceid, format);
            return;
        }
    }
    else    {
        maptype = ICO_WINDOW_MGR_MAP_TYPE_EGL;
    }

    /* maximum framerate        */
    if (maptype == ICO_WINDOW_MGR_MAP_TYPE_EGL) {
        if ((framerate <= 0) || (framerate > 15))
            framerate = 15;
    }
    else    {
        if ((framerate <= 0) || (framerate > 5))
            framerate = 5;
    }

    /* check same surface       */
    wl_list_for_each(sm, &usurf->surf_map, surf_link) {
        if ((sm->usurf == usurf) && (sm->uclient == uclient))   {
            break;
        }
    }

    if (&sm->surf_link == &usurf->surf_map) {
        /* create map table         */
        sm = _ico_win_mgr->free_maptable;
        if (sm) {
            _ico_win_mgr->free_maptable = (struct uifw_surface_map *)sm->usurf;
        }
        else    {
            sm = (struct uifw_surface_map *)malloc(sizeof(struct uifw_surface_map));
            if (! sm)   {
                ico_window_mgr_send_map_surface(resource,
                                                ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                                surfaceid, 5, 0, 0, 0, 0);
                uifw_trace("uifw_map_surface: Leave(malloc error)");
                return;
            }
        }
        memset(sm, 0, sizeof(struct uifw_surface_map));

        wl_list_init(&sm->map_link);
        wl_list_init(&sm->surf_link);
        sm->usurf = usurf;
        sm->uclient = uclient;
        sm->type = maptype;
        sm->framerate = framerate;
        sm->interval = (1000 / sm->framerate) - 1;
        wl_list_insert(_ico_win_mgr->map_list.next, &sm->map_link);
        wl_list_insert(usurf->surf_map.prev, &sm->surf_link);
    }
    else    {
        /* change frame rate    */
        uifw_trace("uifw_map_surface: Leave(chagne frame rate %d->%d",
                   sm->framerate, framerate);
        if (sm->framerate != framerate) {
            sm->framerate = framerate;
            sm->interval = (1000 / sm->framerate) - 1;
            win_mgr_change_mapsurface(sm, 0, weston_compositor_get_time());
        }
        return;
    }

    memset(sm->filepath, 0, ICO_IVI_FILEPATH_LENGTH);
    if ((filepath != NULL) && (*filepath != 0) && (*filepath != ' '))   {
        strncpy(sm->filepath, filepath, ICO_IVI_FILEPATH_LENGTH-1);
    }

    if (buffer != NULL) {
        sm->width = buffer->width;
        sm->height = buffer->height;
        if (maptype != ICO_WINDOW_MGR_MAP_TYPE_EGL) {
            sm->stride = sm->width * 4;
            sm->format = EGL_TEXTURE_RGBA;
            if ((sm->width > 0) && (sm->height > 0))    {
                sm->initflag = 1;
            }
            uifw_debug("uifw_map_surface: map type=%d,surface=%08x,fps=%d,w/h=%d/%d",
                       maptype, surfaceid, framerate, buffer->width, buffer->height);
        }
        else    {
            if (wl_resource_get_user_data((struct wl_resource *)buffer->legacy_buffer)
                != NULL)    {
                sm->format = EGL_TEXTURE_RGBA;
                if ((sm->width > 0) && (sm->height > 0) && (sm->stride > 0) &&
                    (gl_state != NULL))  {
                    sm->initflag = 1;
                }
                uifw_debug("uifw_map_surface: map EGL surface=%08x,fps=%d,w/h=%d/%d",
                           surfaceid, framerate, buffer->width, buffer->height);
            }
            else    {
                uifw_debug("uifw_map_surface: map EGL but no buffer surface=%08x,fps=%d",
                           surfaceid, framerate);
            }
        }
    }
    else if (maptype != ICO_WINDOW_MGR_MAP_TYPE_EGL)    {
        sm->width = es->width;
        sm->height = es->height;
        sm->stride = sm->width * 4;
        sm->format = EGL_TEXTURE_RGBA;
        if ((sm->width > 0) && (sm->height > 0))    {
            sm->initflag = 1;
        }
        uifw_debug("uifw_map_surface: map type=%d,surface=%08x,fps=%d,w/h=%d/%d",
                   maptype, surfaceid, framerate, sm->width, sm->height);
    }
    else    {
        uifw_debug("uifw_map_surface: map EGL but no buffer surface=%08x,fps=%d",
                   surfaceid, framerate);
    }

    /* send map event                       */
    if (sm->initflag)   {
        win_mgr_change_mapsurface(sm, ICO_WINDOW_MGR_MAP_SURFACE_EVENT_MAP,
                                  weston_compositor_get_time());
    }
    uifw_trace("uifw_map_surface: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_unmap_surface: unmap surface buffer
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   surfaceid   surface id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_unmap_surface(struct wl_client *client, struct wl_resource *resource,
                   uint32_t surfaceid)
{
    struct uifw_win_surface *usurf;
    struct uifw_surface_map *sm, *sm_tmp;
    struct uifw_client      *uclient;

    uifw_trace("uifw_unmap_surface: Enter(surface=%08x)", surfaceid);

    usurf = ico_window_mgr_get_usurf(surfaceid);
    if (! usurf)    {
        /* surface dose not exist, error        */
        uifw_trace("uifw_unmap_surface: Leave(surface=%08x dose not exist)", surfaceid);
        return;
    }
    if (client) {
        uclient = ico_window_mgr_find_uclient(client);
        if ((! uclient) || (! uclient->mgr))    {
            /* client dose not exist, error         */
            uifw_trace("uifw_unmap_surface: Leave(client=%08x dose not exist)", (int)client);
            return;
        }
    }
    else    {
        uclient = NULL;
        wl_list_for_each (sm, &usurf->surf_map, surf_link) {
            if (sm->uclient->mgr != NULL) {
                uifw_trace("uifw_unmap_surface: send UNMAP event(ev=%d surf=%08x "
                           "w/h/s=%d/%d/%d format=%x",
                           ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP, surfaceid,
                           sm->width, sm->height, sm->stride, sm->format);
                ico_window_mgr_send_map_surface(sm->uclient->mgr->resource,
                                                ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP,
                                                surfaceid, sm->type, sm->width,
                                                sm->height, sm->stride, sm->format);
            }
        }
    }
    wl_list_for_each_safe (sm, sm_tmp, &usurf->surf_map, surf_link) {
        if (((uclient != NULL) && (sm->uclient != uclient)))   continue;
        wl_list_remove(&sm->surf_link);
        wl_list_remove(&sm->map_link);
        if (sm->filepath[0])    {
            unlink(sm->filepath);
        }
        sm->usurf = (struct uifw_win_surface *)_ico_win_mgr->free_maptable;
        _ico_win_mgr->free_maptable = sm;
    }
    uifw_trace("uifw_unmap_surface: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_surface_configure: UIFW surface configure
 *
 * @param[in]   usurf       UIFW surface
 * @param[in]   x           X coordinate on screen
 * @param[in]   y           Y coordinate on screen
 * @param[in]   width       surface width
 * @param[in]   height      surface height
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_surface_configure(struct uifw_win_surface *usurf,
                          int x, int y, int width, int height)
{
    struct weston_surface   *es;
    struct weston_view      *ev;

    es = usurf->surface;
    if ((es != NULL) && (es->buffer_ref.buffer))  {
        if (usurf->client_width == 0)   {
            usurf->client_width = es->width;
            if (usurf->client_width == 0)
                usurf->client_width = ico_ivi_surface_buffer_width(es);
        }
        if (usurf->client_height == 0)  {
            usurf->client_height = es->height;
            if (usurf->client_height == 0)
                usurf->client_height = ico_ivi_surface_buffer_height(es);
        }

        /* not set geometry width/height    */
        ev = ico_ivi_get_primary_view(usurf);
        weston_view_set_position(ev, x, y);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_destroy_surface: surface destroy
 *
 * @param[in]   surface     Weston surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_destroy_surface(struct weston_surface *surface)
{
    struct uifw_win_surface *usurf;
    struct uifw_win_surface *phash;
    struct uifw_win_surface *bhash;
    uint32_t    hash;

    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("win_mgr_destroy_surface: UIFW surface Not Exist");
        return;
    }
    uifw_trace("win_mgr_destroy_surface: Enter(%08x) %08x", (int)surface, usurf->surfaceid);

    /* destory input region         */
    if (win_mgr_hook_destory)   {
        (*win_mgr_hook_destory)(usurf);
    }

    /* unmap surface                */
    if (&usurf->surf_map != usurf->surf_map.next)   {
        uifw_unmap_surface(NULL, NULL, usurf->surfaceid);
    }

    /* destroy animation extenson   */
    if (win_mgr_hook_animation) {
        (*win_mgr_hook_animation)(ICO_WINDOW_MGR_ANIMATION_DESTROY, (void *)usurf);
    }

    /* send destroy event to controller */
    win_mgr_send_event(ICO_WINDOW_MGR_DESTROY_SURFACE, usurf->surfaceid, 0);

    /* delete from cleint list      */
    wl_list_remove(&usurf->client_link);

    /* delete from hash table       */
    hash = MAKE_IDHASH(usurf->surfaceid);
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

    free(usurf);
    uifw_trace("win_mgr_destroy_surface: Leave(OK)");
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
    struct wl_resource  *add_resource;
    struct uifw_manager *mgr;
    struct uifw_client  *uclient;

    uifw_trace("bind_ico_win_mgr: Enter(client=%08x, id=%x)", (int)client, (int)id);

    add_resource = wl_resource_create(client, &ico_window_mgr_interface, 1, id);
    if (add_resource)   {
        wl_resource_set_implementation(add_resource, &ico_window_mgr_implementation,
                                       _ico_win_mgr, unbind_ico_win_mgr);
    }

    /* Create client management tabel       */
    uclient = ico_window_mgr_find_uclient(client);
    if (! uclient)  {
        win_mgr_bind_client(client, NULL);
        uclient = ico_window_mgr_find_uclient(client);
    }

    /* Manager                              */
    mgr = (struct uifw_manager *)malloc(sizeof(struct uifw_manager));
    if (! mgr)  {
        uifw_error("bind_ico_win_mgr: Error, No Memory");
        return;
    }
    memset(mgr, 0, sizeof(struct uifw_manager));
    mgr->resource = add_resource;
    if (uclient)    {
        uclient->mgr = mgr;
    }
    wl_list_insert(&_ico_win_mgr->manager_list, &mgr->link);

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
    wl_list_for_each_safe (mgr, itmp, &_ico_win_mgr->manager_list, link)    {
        if (mgr->resource == resource) {
            wl_list_remove(&mgr->link);
            free(mgr);
        }
    }
    uifw_trace("unbind_ico_win_mgr: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_get_uclient: get UIFW client table
 *
 * @param[in]   appid       application Id
 * @return      UIFW client table
 * @retval      !=NULL      success(UIFW client table address)
 * @retval      = NULL      error(appid not exist)
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   struct uifw_client *
ico_window_mgr_get_uclient(const char *appid)
{
    struct uifw_client *uclient;

    wl_list_for_each (uclient, &_ico_win_mgr->client_list, link)    {
        if (strcmp(uclient->appid, appid) == 0) {
            return uclient;
        }
    }
    return NULL;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_get_client_usurf: get client UIFW surface table
 *
 * @param[in]   target      surface window name and application Id(winname@appid)
 * @return      UIFW surface table
 * @retval      !=NULL      success(UIFW surface table address)
 * @retval      = NULL      error(appid or winname not exist)
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   struct uifw_win_surface *
ico_window_mgr_get_client_usurf(const char *target)
{
    struct uifw_client      *uclient;
    struct uifw_win_surface *usurf;
    int     i, j;
    char    winname[ICO_IVI_WINNAME_LENGTH];
    char    appid[ICO_IVI_APPID_LENGTH];

    /* get window name and application id   */
    j = 0;
    for (i = 0; target[i]; i++) {
        if (target[i] == '@')   {
            if (target[i+1] != '@') break;
            i ++;
        }
        if (j < (ICO_IVI_WINNAME_LENGTH-1)) {
            winname[j++] = target[i];
        }
    }
    winname[j] = 0;
    if (target[i] == '@')   {
        i ++;
    }
    else    {
        winname[0] = 0;
        i = 0;
    }
    j = 0;
    for ( ; target[i]; i++) {
        if ((target[i] == '@') && (target[i+1] == '@')) i ++;
        if (j < (ICO_IVI_APPID_LENGTH-1))  {
            appid[j++] = target[i];
        }
    }
    appid[j] = 0;
    uifw_debug("ico_window_mgr_get_client_usurf: target=<%s> appid=<%s> win=<%s>",
               target, appid, winname);

    wl_list_for_each (uclient, &_ico_win_mgr->client_list, link)    {
        if (strcmp(uclient->appid, appid) == 0) {
            wl_list_for_each (usurf, &uclient->surface_link, client_link)   {
                if ((winname[0] == 0) ||
                    (strcmp(winname, usurf->winname) == 0)) {
                    return usurf;
                }
            }
        }
    }
    return NULL;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_set_hook_animation: set animation hook routine
 *
 * @param[in]   hook_animation  hook routine
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_window_mgr_set_hook_animation(int (*hook_animation)(const int op, void *data))
{
    win_mgr_hook_animation = hook_animation;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_set_hook_change: set input region hook routine
 *
 * @param[in]   hook_change     hook routine
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_window_mgr_set_hook_change(void (*hook_change)(struct uifw_win_surface *usurf))
{
    win_mgr_hook_change = hook_change;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_set_hook_destory: set input region hook routine
 *
 * @param[in]   hook_destroy    hook routine
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_window_mgr_set_hook_destory(void (*hook_destroy)(struct uifw_win_surface *usurf))
{
    win_mgr_hook_destory = hook_destroy;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_set_hook_inputregion: set input region hook routine
 *
 * @param[in]   hook_inputregion    hook routine
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_window_mgr_set_hook_inputregion(
        void (*hook_inputregion)(int set, struct uifw_win_surface *usurf,
                                 int32_t x, int32_t y, int32_t width,
                                 int32_t height, int32_t hotspot_x, int32_t hotspot_y,
                                 int32_t cursor_x, int32_t cursor_y, int32_t cursor_width,
                                 int32_t cursor_height, uint32_t attr))
{
    win_mgr_hook_inputregion = hook_inputregion;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   module_init: initialize ico_window_mgr
 *                       this function called from ico_pluign_loader
 *
 * @param[in]   es          weston compositor
 * @param[in]   argc        number of arguments(unused)
 * @param[in]   argv        argument list(unused)
 * @return      result
 * @retval      0           sccess
 * @retval      -1          error
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
module_init(struct weston_compositor *ec, int *argc, char *argv[])
{
    int     nodeId;
    int     i;
    int     idx;
    struct weston_output *output;
    struct weston_config_section *section;
    char    *displayno = NULL;
    char    *p;
    struct wl_event_loop *loop;

    uifw_info("ico_window_mgr: Enter(module_init)");

    /* get ivi debug level                      */
    section = weston_config_get_section(ec->config, "ivi-option", NULL, NULL);
    if (section)    {
        weston_config_section_get_int(section, "flag", &_ico_ivi_option_flag, 0);
        weston_config_section_get_int(section, "log", &_ico_ivi_debug_level, 3);
    }

    /* get display number                       */
    section = weston_config_get_section(ec->config, "ivi-display", NULL, NULL);
    if (section)    {
        weston_config_section_get_string(section, "displayno", &displayno, NULL);
    }

    /* get animation default                    */
    section = weston_config_get_section(ec->config, "ivi-animation", NULL, NULL);
    if (section)    {
        weston_config_section_get_string(section, "default", &_ico_ivi_animation_name, NULL);
        weston_config_section_get_int(section, "time", &_ico_ivi_animation_time, 500);
        weston_config_section_get_int(section, "fps", &_ico_ivi_animation_fps, 30);
    }
    if (_ico_ivi_animation_name == NULL)
        _ico_ivi_animation_name = (char *)"fade";
    if (_ico_ivi_animation_time < 100)  _ico_ivi_animation_time = 500;
    if (_ico_ivi_animation_fps < 3)     _ico_ivi_animation_fps = 30;

    /* create ico_window_mgr management table   */
    _ico_win_mgr = (struct ico_win_mgr *)malloc(sizeof(struct ico_win_mgr));
    if (_ico_win_mgr == NULL)   {
        uifw_error("ico_window_mgr: malloc failed");
        return -1;
    }

    memset(_ico_win_mgr, 0, sizeof(struct ico_win_mgr));

    _ico_win_mgr->compositor = ec;

    uifw_trace("ico_window_mgr: wl_global_create(bind_ico_win_mgr)");
    if (wl_global_create(ec->wl_display, &ico_window_mgr_interface, 1,
                         _ico_win_mgr, bind_ico_win_mgr) == NULL)  {
        uifw_error("ico_window_mgr: Error(wl_global_create)");
        return -1;
    }

    wl_list_init(&_ico_win_mgr->client_list);
    wl_list_init(&_ico_win_mgr->manager_list);
    wl_list_init(&_ico_win_mgr->map_list);
    _ico_win_mgr->free_maptable = NULL;

    /* create display list                  */
    if (displayno != NULL)   {
        p = displayno;
    }
    else    {
        p = NULL;
    }
    _ico_num_nodes = 0;
    wl_list_for_each (output, &ec->output_list, link) {
        wl_list_init(&_ico_win_mgr->map_animation[_ico_num_nodes].link);
        _ico_win_mgr->map_animation[_ico_num_nodes].frame = win_mgr_check_mapsurface;
        wl_list_insert(output->animation_list.prev,
                       &_ico_win_mgr->map_animation[_ico_num_nodes].link);
        _ico_num_nodes++;
        if (_ico_num_nodes >= ICO_IVI_MAX_DISPLAY)   break;
    }
    memset(&_ico_node_table[0], 0, sizeof(_ico_node_table));
    i = 0;
    wl_list_for_each (output, &ec->output_list, link) {
        p = strtok(p, ",");
        if (p)  {
            idx = strtol(p, (char **)0, 0);
            uifw_trace("ico_window_mgr: config Display.%d is weston display.%d", i, idx);
            p = NULL;
            if ((idx < 0) || (idx >= _ico_num_nodes))   {
                idx = i;
            }
        }
        else    {
            idx = i;
        }
        if (_ico_node_table[idx].node)  {
            for (idx = 0; idx < _ico_num_nodes; idx++)  {
                if (_ico_node_table[idx].node == 0) break;
            }
            if (idx >= _ico_num_nodes)  {
                uifw_error("ico_window_mgr: number of display overflow");
                idx = 0;
            }
        }
        _ico_node_table[idx].node = idx + 0x100;
        _ico_node_table[idx].displayno = i;
        _ico_node_table[idx].output = output;
        _ico_node_table[idx].disp_x = output->x;
        _ico_node_table[idx].disp_y = output->y;
        _ico_node_table[idx].disp_width = output->width;
        _ico_node_table[idx].disp_height = output->height;
        i ++;
        if (i >= _ico_num_nodes) break;
    }
    idx = 0;
    for (i = 0; i < _ico_num_nodes; i++)    {
        _ico_node_table[i].node &= 0x0ff;
        uifw_info("ico_window_mgr: Display.%d no=%d x/y=%d/%d w/h=%d/%d",
                  i, _ico_node_table[i].displayno,
                  _ico_node_table[i].disp_x, _ico_node_table[i].disp_y,
                  _ico_node_table[i].disp_width, _ico_node_table[i].disp_height);
    }
    if (displayno)  free(displayno);

    /* my node Id ... this version fixed 0  */
    nodeId = ico_ivi_get_mynode();

    _ico_win_mgr->surface_head = ICO_IVI_SURFACEID_BASE(nodeId);
    uifw_trace("ico_window_mgr: NoedId=%04x SurfaceIdBase=%08x",
                nodeId, _ico_win_mgr->surface_head);

    /* get seat for touch down counter check    */
    touch_check_seat = container_of(ec->seat_list.next, struct weston_seat, link);
    loop = wl_display_get_event_loop(ec->wl_display);
    _ico_win_mgr->wait_mapevent =
            wl_event_loop_add_timer(loop, win_mgr_timer_mapsurface, NULL);
    wl_event_source_timer_update(_ico_win_mgr->wait_mapevent, 1000);

    uifw_info("ico_window_mgr: animation name=%s time=%d fps=%d",
              _ico_ivi_animation_name, _ico_ivi_animation_time, _ico_ivi_animation_fps);
    uifw_info("ico_window_mgr: option flag=0x%04x log level=%d debug flag=0x%04x",
              _ico_ivi_option_flag, _ico_ivi_debug_level & 0x0ffff,
              (_ico_ivi_debug_level >> 16) & 0x0ffff);

    /* touch/click binding for select surface           */
    weston_compositor_add_button_binding(ec, BTN_LEFT, 0, win_mgr_click_to_activate, NULL);
    weston_compositor_add_touch_binding(ec, 0, win_mgr_touch_to_activate, NULL);

    /* set Notification function for GENIVI ivi-shell   */
    if (weston_layout_setNotificationCreateSurface(ico_ivi_surfaceCreateNotification, NULL) != 0)   {
        uifw_error("ico_window_mgr: weston_layout_setNotificationCreateSurface Error");
    }
    if (weston_layout_setNotificationRemoveSurface(ico_ivi_surfaceRemoveNotification, NULL) != 0)   {
        uifw_error("ico_window_mgr: weston_layout_setNotificationRemoveSurface Error");
    }
    if (weston_layout_setNotificationConfigureSurface(ico_ivi_surfaceConfigureNotification, NULL) != 0) {
        uifw_error("ico_window_mgr: weston_layout_setNotificationConfigureSurface Error");
    }
    uifw_info("ico_window_mgr: Leave(module_init)");

    return 0;
}
