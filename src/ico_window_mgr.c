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
 * @date    Jul-26-2013
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

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <weston/compositor.h>
#include "ico_ivi_common.h"
#include "ico_ivi_shell.h"
#include "ico_window_mgr.h"
#include "desktop-shell-server-protocol.h"
#include "ico_window_mgr-server-protocol.h"

/* SurfaceID                            */
#define INIT_SURFACE_IDS    1024            /* SurfaceId table initiale size        */
#define ADD_SURFACE_IDS     512             /* SurfaceId table additional size      */
#define SURCAFE_ID_MASK     0x0ffff         /* SurfaceId bit mask pattern           */
#define UIFW_HASH           64              /* Hash value (2's compliment)          */

/* Internal fixed value                 */
#define ICO_WINDOW_MGR_APPID_FIXCOUNT   5   /* retry count of appid fix             */
                                            /* show/hide animation with position    */
#define ICO_WINDOW_MGR_ANIMATION_POS    0x10000000

/* wl_buffer (inport from wayland-1.2.0/src/wayland-server.h)                       */
struct uifw_wl_buffer   {       /* struct wl_buffer */
    struct wl_resource resource;
    int32_t width, height;
    uint32_t busy_count;
};

/* wl_drm_buffer (inport from mesa-9.1.3/src/egl/wayland/wayland-drm/wayland-drm.h) */
struct uifw_drm_buffer {
    struct uifw_wl_buffer buffer;
    void *drm;                  /* struct wl_drm    */
    uint32_t format;
    const void *driver_format;
    int32_t offset[3];
    int32_t stride[3];
    void *driver_buffer;
};

/* __DRIimage (inport from mesa-9.1.3/src/mesa/drivers/dri/intel/intel_regions.h)   */
struct uifw_intel_region  {     /* struct intel_region  */
   void *bo;                /**< buffer manager's buffer */
   uint32_t refcount;       /**< Reference count for region */
   uint32_t cpp;            /**< bytes per pixel */
   uint32_t width;          /**< in pixels */
   uint32_t height;         /**< in pixels */
   uint32_t pitch;          /**< in bytes */
   char *map;               /**< only non-NULL when region is actually mapped */
   uint32_t map_refcount;   /**< Reference count for mapping */
   uint32_t tiling;         /**< Which tiling mode the region is in */
   uint32_t name;           /**< Global name for the bo */
   void *screen;            /* screen   */
};
struct uifw_dri_image   {       /* struct __DRIimageRec */
   struct uifw_intel_region *region;
   int internal_format;
   uint32_t dri_format;
   uint32_t format;
   uint32_t offset;
   uint32_t strides[3];
   uint32_t offsets[3];
   void *planar_format; /* intel_image_format */
   void *data;
};

/* gl_surface_state (inport from weston-1.2.1/src/gl-renderer.c)    */
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
};

/* Multi Windiw Manager                 */
struct ico_win_mgr {
    struct weston_compositor *compositor;   /* Weston compositor                    */
    void    *shell;                         /* shell(ico_ivi_shell) table address   */
    int32_t surface_head;                   /* (HostID << 24) | (DisplayNo << 16)   */

    struct wl_list  client_list;            /* Clients                              */
    struct wl_list  manager_list;           /* Manager(ex.HomeScreen) list          */
    int             num_manager;            /* Number of managers                   */
    struct wl_list  ivi_layer_list;         /* Layer management table list          */
    struct wl_list  map_list;               /* surface map list                     */
    struct uifw_surface_map *free_maptable; /* free maped surface table list        */
    struct weston_animation map_animation[ICO_IVI_MAX_DISPLAY];
                                            /* animation for map check              */
    struct uifw_win_surface *active_pointer_usurf;  /* Active Pointer Surface       */
    struct uifw_win_surface *active_keyboard_usurf; /* Active Keyboard Surface      */

    struct uifw_win_surface *idhash[UIFW_HASH];  /* UIFW SerfaceID                  */
    struct uifw_win_surface *wshash[UIFW_HASH];  /* Weston Surface                  */

    uint32_t surfaceid_count;               /* Number of surface id                 */
    uint32_t surfaceid_max;                 /* Maximum number of surface id         */
    uint16_t *surfaceid_map;                /* SurfaceId assign bit map             */

    char    shell_init;                     /* shell initialize flag                */
    char    res;                            /* (unused)                             */
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
                                            /* get client table from weston client  */
static struct uifw_client* find_client_from_client(struct wl_client *client);
                                            /* assign new surface id                */
static uint32_t generate_id(void);
                                            /* bind shell client                    */
static void win_mgr_bind_client(struct wl_client *client, void *shell);
                                            /* unind shell client                   */
static void win_mgr_unbind_client(struct wl_client *client);
                                            /* get appid from pid                   */
static void win_mgr_get_client_appid(struct uifw_client *uclient);
                                            /* create new surface                   */
static void win_mgr_register_surface(
                    struct wl_client *client, struct wl_resource *resource,
                    struct weston_surface *surface, struct shell_surface *shsurf);
                                            /* surface destroy                      */
static void win_mgr_destroy_surface(struct weston_surface *surface);
                                            /* map new surface                      */
static void win_mgr_map_surface(struct weston_surface *surface, int32_t *width,
                                int32_t *height, int32_t *sx, int32_t *sy);
                                            /* send surface change event to manager */
static void win_mgr_change_surface(struct weston_surface *surface,
                                   const int to, const int manager);
                                            /* window manager surface configure     */
static void win_mgr_surface_configure(struct uifw_win_surface *usurf,
                                      int x, int y, int width, int height);
                                            /* shell surface configure              */
static void win_mgr_shell_configure(struct weston_surface *surface);
                                            /* surface select                       */
static void win_mgr_select_surface(struct weston_surface *surface);
                                            /* surface set title                    */
static void win_mgr_set_title(struct weston_surface *surface, const char *title);
                                            /* surface move request from shell      */
static void win_mgr_surface_move(struct weston_surface *surface, int *dx, int *dy);
                                            /* set raise                            */
static void win_mgr_set_raise(struct uifw_win_surface *usurf, const int raise);
                                            /* surface change from manager          */
static int win_mgr_surface_change_mgr(struct weston_surface *surface, const int x,
                                      const int y, const int width, const int height);
                                            /* create new layer                     */
static struct uifw_win_layer *win_mgr_create_layer(struct uifw_win_surface *usurf,
                                                   const uint32_t layer);
                                            /* set surface layer                    */
static void win_mgr_set_layer(struct uifw_win_surface *usurf, const uint32_t layer);
                                            /* set active surface                   */
static void win_mgr_set_active(struct uifw_win_surface *usurf, const int target);

                                            /* declare manager                      */
static void uifw_declare_manager(struct wl_client *client, struct wl_resource *resource,
                                 int manager);
                                            /* set window layer                     */
static void uifw_set_window_layer(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t surfaceid, uint32_t layer);
                                            /* set surface size and position        */
static void uifw_set_positionsize(struct wl_client *client, struct wl_resource *resource,
                                  uint32_t surfaceid, uint32_t node, int32_t x, int32_t y,
                                  int32_t width, int32_t height, int32_t flags);
                                            /* show/hide and raise/lower surface    */
static void uifw_set_visible(struct wl_client *client, struct wl_resource *resource,
                             uint32_t surfaceid, int32_t visible, int32_t raise,
                             int32_t flag);
                                            /* set surface animation                */
static void uifw_set_animation(struct wl_client *client, struct wl_resource *resource,
                               uint32_t surfaceid, int32_t type,
                               const char *animation, int32_t time);
                                            /* set surface attributes               */
static void uifw_set_attributes(struct wl_client *client, struct wl_resource *resource,
                                uint32_t surfaceid, uint32_t attributes);
                                            /* surface visibility control with animation*/
static void uifw_visible_animation(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t surfaceid, int32_t visible,
                                   int32_t x, int32_t y, int32_t width, int32_t height);
                                            /* set active surface (form HomeScreen) */
static void uifw_set_active(struct wl_client *client, struct wl_resource *resource,
                            uint32_t surfaceid, int32_t active);
                                            /* layer visibility control             */
static void uifw_set_layer_visible(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t layer, int32_t visible);
                                            /* get application surfaces             */
static void uifw_get_surfaces(struct wl_client *client, struct wl_resource *resource,
                              const char *appid);
                                            /* check and change all mapped surface  */
static void win_mgr_check_surfacemap(struct weston_animation *animation,
                                     struct weston_output *output, uint32_t msecs);
                                            /* check and change mapped surface      */
static void win_mgr_change_mapsurface(struct uifw_surface_map *sm, int event);
                                            /* map surface to system application    */
static void uifw_map_surface(struct wl_client *client, struct wl_resource *resource,
                             uint32_t surfaceid, int32_t framerate);
                                            /* unmap surface                        */
static void uifw_unmap_surface(struct wl_client *client, struct wl_resource *resource,
                               uint32_t surfaceid);
                                            /* bind manager                         */
static void bind_ico_win_mgr(struct wl_client *client,
                             void *data, uint32_t version, uint32_t id);
                                            /* unbind manager                       */
static void unbind_ico_win_mgr(struct wl_resource *resource);
                                            /* send event to manager                */
static int ico_win_mgr_send_to_mgr(const int event, struct uifw_win_surface *usurf,
                                   const int param1, const int param2, const int param3,
                                   const int param4, const int param5);
                                            /* set surface transform                */
static int win_mgr_set_scale(struct uifw_win_surface *usurf);
                                            /* convert animation name to Id value   */
static int ico_get_animation_name(const char *animation);
                                            /* hook for animation                   */
static int  (*win_mgr_hook_animation)(const int op, void *data) = NULL;

/* static tables                        */
/* Multi Window Manager interface       */
static const struct ico_window_mgr_interface ico_window_mgr_implementation = {
    uifw_declare_manager,
    uifw_set_window_layer,
    uifw_set_positionsize,
    uifw_set_visible,
    uifw_set_animation,
    uifw_set_attributes,
    uifw_visible_animation,
    uifw_set_active,
    uifw_set_layer_visible,
    uifw_get_surfaces,
    uifw_map_surface,
    uifw_unmap_surface
};

/* plugin common value(without ico_plugin_loader)   */
static int  _ico_ivi_debug_flag = 0;            /* Debug flags                      */
static int  _ico_ivi_debug_level = 3;           /* Debug Level                      */
static char *_ico_ivi_animation_name = NULL;    /* Default animation name           */
static int  _ico_ivi_animation_time = 500;      /* Default animation time           */
static int  _ico_ivi_animation_fps = 15;        /* Animation frame rate             */
static int  _ico_ivi_background_layer = 0;      /* Background layer                 */
static int  _ico_ivi_default_layer = 1;         /* Deafult layer id at surface create*/
static int  _ico_ivi_input_layer = 101;         /* Input layer id                   */
static int  _ico_ivi_cursor_layer = 102;        /* Cursor layer id                  */
static int  _ico_ivi_startup_layer = 109;       /* Deafult layer id at system startup*/

/* static management table              */
static struct ico_win_mgr       *_ico_win_mgr = NULL;
static int                      _ico_num_nodes = 0;
static struct uifw_node_table   _ico_node_table[ICO_IVI_MAX_DISPLAY];


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
    return _ico_ivi_debug_flag;
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
 * @retval      4       All output with debug write
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_debuglevel(void)
{
    return _ico_ivi_debug_level;
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
    int     buf_width, buf_height;

    if (es == NULL) {
        uifw_trace("ico_window_mgr_set_weston_surface: usurf(%08x) has no surface",
                   (int)usurf);
        return;
    }

    if (es->buffer_ref.buffer != NULL)   {
        buf_width = weston_surface_buffer_width(es);
        buf_height = weston_surface_buffer_height(es);
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
        if ((es->geometry.x != x) || (es->geometry.y != y) ||
            (es->geometry.width != width) || (es->geometry.height != height))    {
            weston_surface_damage_below(es);
            win_mgr_surface_configure(usurf, x, y, width, height);
        }
        weston_surface_damage(es);
        weston_compositor_schedule_repaint(_ico_win_mgr->compositor);
    }
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
ico_window_mgr_change_surface(struct uifw_win_surface *usurf,
                              const int to, const int manager)
{
    uifw_trace("ico_window_mgr_change_surface: Enter(%08x,%d,%d)",
               usurf->surfaceid, to, manager);
    win_mgr_change_surface(usurf->surface, to, manager);
    uifw_trace("ico_window_mgr_change_surface: Leave");
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
    struct uifw_client *uclient;

    if (surfaceid == ICO_WINDOW_MGR_V_MAINSURFACE) {
        uclient = find_client_from_client(client);
        if (uclient)    {
            if (&uclient->surface_link != uclient->surface_link.next)   {
                usurf = container_of(uclient->surface_link.next,
                                     struct uifw_win_surface, client_link);
            }
            else    {
                usurf = NULL;
            }
        }
        else    {
            usurf = NULL;
        }
    }
    else    {
        usurf = ico_window_mgr_get_usurf(surfaceid);
    }
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
 * @brief   find_client_from_client: find UIFW client by wayland client
 *
 * @param[in]   client      Wayland client
 * @return      UIFW client table address
 * @retval      !=NULL      success(client table address)
 * @retval      NULL        error(client dose not exist)
 */
/*--------------------------------------------------------------------------*/
static struct uifw_client*
find_client_from_client(struct wl_client *client)
{
    struct uifw_client  *uclient;

    wl_list_for_each (uclient, &_ico_win_mgr->client_list, link)    {
        if (uclient->client == client)  {
            return(uclient);
        }
    }
    uifw_trace("find_client_from_client: client.%08x is NULL", (int)client);
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

    uclient = find_client_from_client(client);

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
 * @brief   win_mgr_bind_client: desktop_shell from client
 *
 * @param[in]   client          Wayland client
 * @param[in]   shell           shell(ico_ivi_shell) table address
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
    uclient = find_client_from_client(client);
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

        /* weston internal client, not manage   */
        if (strcmp(uclient->appid, "weston") == 0)  {
            free(uclient);
            uifw_trace("win_mgr_bind_client: client=%08x is internal, delete", (int)client);
        }
        else if (newclient > 0)  {
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
 * @brief   win_mgr_unbind_client: unbind desktop_shell from client
 *
 * @param[in]   client          Wayland client
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_unbind_client(struct wl_client *client)
{
    struct uifw_client  *uclient;

    uifw_trace("win_mgr_unbind_client: Enter(client=%08x)", (int)client);

    uclient = find_client_from_client(client);
    if (uclient)    {
        /* Client exist, Destory client management table             */
        wl_list_remove(&uclient->link);
        free(uclient);
    }
    uifw_trace("win_mgr_unbind_client: Leave");
}

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
    int     fd;
    int     size;
    int     i;
    int     j;
    char    procpath[128];

    memset(uclient->appid, 0, ICO_IVI_APPID_LENGTH);
    i = aul_app_get_appid_bypid(uclient->pid, uclient->appid, ICO_IVI_APPID_LENGTH);
    uifw_trace("win_mgr_get_client_appid: aul_app_get_appid_bypid ret=%d "
               "pid=%d appid=<%s>", i, uclient->pid, uclient->appid);
    if (uclient->appid[0] != 0) {
        /* OK, end of get appid         */
        uclient->fixed_appid = ICO_WINDOW_MGR_APPID_FIXCOUNT;
    }
    else    {
        /* client dose not exist in AppCore, search Linux process table */
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
                       "Process table", uclient->pid, uclient->appid);
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
 * @brief   win_mgr_register_surface: create UIFW surface
 *
 * @param[in]   client          Wayland client
 * @param[in]   resource        client resource
 * @param[in]   surface         Weston surface
 * @param[in]   shsurf          shell surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_register_surface(struct wl_client *client, struct wl_resource *resource,
                         struct weston_surface *surface, struct shell_surface *shsurf)
{
    struct uifw_win_surface *usurf;
    struct uifw_win_surface *phash;
    struct uifw_win_surface *bhash;
    uint32_t    hash;

    uifw_trace("win_mgr_register_surface: Enter(surf=%08x,client=%08x,res=%08x)",
               (int)surface, (int)client, (int)resource);

    /* check new surface                    */
    if (find_uifw_win_surface_by_ws(surface))   {
        /* surface exist, NOP               */
        uifw_trace("win_mgr_register_surface: Leave(Already Exist)");
        return;
    }

    /* set default color and shader */
    weston_surface_set_color(surface, 0.0, 0.0, 0.0, 1);

    /* create UIFW surface management table */
    usurf = malloc(sizeof(struct uifw_win_surface));
    if (! usurf)    {
        uifw_error("win_mgr_register_surface: No Memory");
        return;
    }

    memset(usurf, 0, sizeof(struct uifw_win_surface));

    usurf->surfaceid = generate_id();
    usurf->surface = surface;
    usurf->shsurf = shsurf;
    usurf->node_tbl = &_ico_node_table[0];  /* set default node table (display no=0)    */
    wl_list_init(&usurf->ivi_layer);
    wl_list_init(&usurf->client_link);
    wl_list_init(&usurf->animation.animation.link);
    wl_list_init(&usurf->surf_map);
    usurf->animation.hide_anima = ico_get_animation_name(ico_ivi_default_animation_name());
    usurf->animation.hide_time = ico_ivi_default_animation_time();;
    usurf->animation.show_anima = usurf->animation.hide_anima;
    usurf->animation.show_time = usurf->animation.hide_time;
    usurf->animation.move_anima = usurf->animation.hide_anima;
    usurf->animation.move_time = usurf->animation.hide_time;
    usurf->animation.resize_anima = usurf->animation.hide_anima;
    usurf->animation.resize_time = usurf->animation.hide_time;

    if ((_ico_win_mgr->num_manager <= 0) ||
        (ico_ivi_debugflag() & ICO_IVI_DEBUG_SHOW_SURFACE)) {
        uifw_trace("win_mgr_register_surface: No Manager, Force visible");
        usurf->visible = 1;
    }
    else    {
        uifw_trace("win_mgr_register_surface: Manager exist, Not visible");
        usurf->visible = 0;
    }

    /* set client                           */
    usurf->uclient = find_client_from_client(client);
    if (! usurf->uclient)  {
        /* client not exist, create client management table */
        uifw_trace("win_mgr_register_surface: Create Client");
        win_mgr_bind_client(client, NULL);
        usurf->uclient = find_client_from_client(client);
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
    /* set default layer id             */
    win_mgr_set_layer(usurf, (_ico_win_mgr->num_manager > 0) ? _ico_ivi_default_layer :
                                                               _ico_ivi_startup_layer);
    uifw_trace("win_mgr_register_surface: Leave(surfaceId=%08x)", usurf->surfaceid);
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
                   usurf->surfaceid, usurf->width, usurf->height, usurf->visible);
        if ((usurf->width > 0) && (usurf->height > 0)) {
            uifw_trace("win_mgr_map_surface: HomeScreen registed, PositionSize"
                       "(surf=%08x x/y=%d/%d w/h=%d/%d vis=%d",
                       usurf->surfaceid, usurf->x, usurf->y, usurf->width, usurf->height,
                       usurf->visible);
            *width = usurf->width;
            *height = usurf->height;
            win_mgr_surface_configure(usurf, usurf->node_tbl->disp_x + usurf->x,
                                      usurf->node_tbl->disp_y + usurf->y,
                                      usurf->width, usurf->height);
        }
        else    {
            uifw_trace("win_mgr_map_surface: HomeScreen not regist Surface, "
                       "Change PositionSize(surf=%08x x/y=%d/%d w/h=%d/%d)",
                       usurf->surfaceid, *sx, *sy, *width, *height);
            usurf->width = *width;
            usurf->height = *height;
            usurf->x = *sx;
            usurf->y = *sy;
            if (usurf->x < 0)   usurf->x = 0;
            if (usurf->y < 0)   usurf->y = 0;

            if (_ico_win_mgr->num_manager > 0)  {
                /* HomeScreen exist, coodinate set by HomeScreen                */
                if (usurf->visible) {
                    win_mgr_surface_configure(usurf, usurf->node_tbl->disp_x + usurf->x,
                                              usurf->node_tbl->disp_y + usurf->y,
                                              usurf->width, usurf->height);
                }
                else    {
                    win_mgr_surface_configure(usurf, ICO_IVI_MAX_COORDINATE+1,
                                              ICO_IVI_MAX_COORDINATE+1,
                                              usurf->width, usurf->height);
                }
                uifw_trace("win_mgr_map_surface: Change size/position x/y=%d/%d w/h=%d/%d",
                           (int)surface->geometry.x, (int)surface->geometry.y,
                           surface->geometry.width, surface->geometry.height);
            }
            if ((_ico_win_mgr->num_manager <= 0) ||
                (ico_ivi_debugflag() & ICO_IVI_DEBUG_SHOW_SURFACE)) {
                uifw_trace("win_mgr_map_surface: Np HomeScreen, chaneg to Visible");
                ico_window_mgr_set_visible(usurf, 1);
            }
        }
        usurf->mapped = 1;
        if (usurf->visible) {
            ico_window_mgr_restack_layer(NULL, FALSE);
        }
        uifw_trace("win_mgr_map_surface: Leave");
    }
    else    {
        uifw_trace("win_mgr_map_surface: Leave(No UIFW Surface)");
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_restack_layer: restack surface list
 *
 * @param[in]   usurf           UIFW surface (if NULL, no surface)
 * @param[in]   omit_touch      omit touch layer flag (TRUE=omit/FALSE=not omit)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_window_mgr_restack_layer(struct uifw_win_surface *usurf, const int omit_touch)
{
    struct uifw_win_surface  *eu;
    struct uifw_win_layer *el;
    int32_t buf_width, buf_height;
    float   new_x, new_y;
    struct weston_layer *wlayer;
    struct weston_surface  *surface, *surfacetmp;
    int     num_visible = 0;
    int     layer_type;

    /* set layer type                   */
    ico_ivi_shell_set_layertype();

    /* make compositor surface list     */
    wlayer = ico_ivi_shell_weston_layer();

    uifw_trace("ico_window_mgr_restack_layer: Enter(surf=%08x,omit=%d) layer=%08x",
               (int)usurf, omit_touch, (int)wlayer);

    /* remove all surfaces in panel_layer   */
    wl_list_for_each_safe(surface, surfacetmp, &wlayer->surface_list, layer_link)   {
        wl_list_remove(&surface->layer_link);
        wl_list_init(&surface->layer_link);
    }
    wl_list_init(&wlayer->surface_list);

    wl_list_for_each (el, &_ico_win_mgr->ivi_layer_list, link)  {
        if (el->layer_type == ICO_WINDOW_MGR_LAYER_TYPE_CURSOR) continue;
        wl_list_for_each (eu, &el->surface_list, ivi_layer) {
            if (eu->surface == NULL)    continue;

            /* target only panel or unknown layer   */
            layer_type = ico_ivi_shell_layertype(eu->surface);
            if ((layer_type != LAYER_TYPE_PANEL) && (layer_type != LAYER_TYPE_UNKNOWN)) {
                continue;
            }
            wl_list_remove(&eu->surface->layer_link);
            wl_list_init(&eu->surface->layer_link);

            if (eu->mapped != 0)    {
                if ((el->visible == FALSE) || (eu->visible == FALSE) ||
                    ((omit_touch != FALSE) &&
                     (el->layer_type == ICO_WINDOW_MGR_LAYER_TYPE_INPUT)))  {
                    new_x = (float)(ICO_IVI_MAX_COORDINATE+1);
                    new_y = (float)(ICO_IVI_MAX_COORDINATE+1);
                }
                else if (eu->surface->buffer_ref.buffer)    {
                    buf_width = weston_surface_buffer_width(eu->surface);
                    buf_height = weston_surface_buffer_height(eu->surface);
                    if (eu->width > buf_width) {
                        new_x = (float)(eu->x +
                                (eu->width - eu->surface->geometry.width)/2);
                    }
                    else    {
                        new_x = (float)eu->x;
                    }
                    if (eu->height > buf_height)    {
                        new_y = (float) (eu->y +
                                (eu->height - eu->surface->geometry.height)/2);
                    }
                    else    {
                        new_y = (float)eu->y;
                    }
                    new_x += eu->node_tbl->disp_x + eu->xadd;
                    new_y += eu->node_tbl->disp_y + eu->yadd;
                    num_visible ++;
                }
                else    {
                    new_x = (float)(eu->x + eu->node_tbl->disp_x + eu->xadd);
                    new_y = (float)(eu->y + eu->node_tbl->disp_y + eu->yadd);
                }
                wl_list_insert(wlayer->surface_list.prev, &eu->surface->layer_link);
                if ((eu->restrain_configure == 0) &&
                    ((new_x != eu->surface->geometry.x) ||
                     (new_y != eu->surface->geometry.y)))   {
                    weston_surface_damage_below(eu->surface);
                    weston_surface_set_position(eu->surface, (float)new_x, (float)new_y);
                    weston_surface_damage(eu->surface);
                }
#if 0           /* too many debug log   */
                uifw_trace("ico_window_mgr_restack_layer:%3d(%d).%08x(%08x:%d) "
                           "x/y=%d/%d w/h=%d/%d",
                           el->layer, el->visible, eu->surfaceid, (int)eu->surface,
                           eu->visible, (int)eu->surface->geometry.x,
                           (int)eu->surface->geometry.y, eu->surface->geometry.width,
                           eu->surface->geometry.height);
#endif
            }
        }
    }

    /* damage(redraw) target surfacem if target exist   */
    if (usurf) {
        weston_surface_damage(usurf->surface);
    }

    /* composit and draw screen(plane)  */
    if (omit_touch == FALSE)    {
        weston_compositor_schedule_repaint(_ico_win_mgr->compositor);

        if ((_ico_win_mgr->shell_init == 0) && (num_visible > 0) &&
            (_ico_win_mgr->shell != NULL) && (_ico_win_mgr->num_manager > 0))   {
            /* start shell fade         */
            _ico_win_mgr->shell_init = 1;
            ico_ivi_shell_startup(_ico_win_mgr->shell);
        }
    }
    uifw_trace("ico_window_mgr_restack_layer: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_create_layer: create new layer
 *
 * @param[in]   usurf       UIFW surface, (if need)
 * @param[in]   layer       layer id
 * @return      new layer
 * @retval      != NULL     success(layer management table)
 * @retval      == NULL     error(No Memory)
 */
/*--------------------------------------------------------------------------*/
static struct uifw_win_layer *
win_mgr_create_layer(struct uifw_win_surface *usurf, const uint32_t layer)
{
    struct uifw_win_layer *el;
    struct uifw_win_layer *new_el;

    new_el = malloc(sizeof(struct uifw_win_layer));
    if (! new_el)   {
        uifw_trace("win_mgr_create_layer: Leave(No Memory)");
        return NULL;
    }

    memset(new_el, 0, sizeof(struct uifw_win_layer));
    new_el->layer = layer;
    if ((int)layer == _ico_ivi_background_layer )   {
        new_el->layer_type = ICO_WINDOW_MGR_LAYER_TYPE_BACKGROUND;
    }
    else if ((int)layer == _ico_ivi_input_layer )   {
        new_el->layer_type = ICO_WINDOW_MGR_LAYER_TYPE_INPUT;
    }
    else if ((int)layer == _ico_ivi_cursor_layer )  {
        new_el->layer_type = ICO_WINDOW_MGR_LAYER_TYPE_CURSOR;
    }
    else    {
        new_el->layer_type = ICO_WINDOW_MGR_LAYER_TYPE_NORMAL;
    }
    new_el->visible = TRUE;
    wl_list_init(&new_el->surface_list);
    wl_list_init(&new_el->link);

    wl_list_for_each (el, &_ico_win_mgr->ivi_layer_list, link) {
        if (layer >= el->layer) break;
    }
    if (&el->link == &_ico_win_mgr->ivi_layer_list)    {
        wl_list_insert(_ico_win_mgr->ivi_layer_list.prev, &new_el->link);
    }
    else    {
        wl_list_insert(el->link.prev, &new_el->link);
    }

    if (usurf)  {
        wl_list_remove(&usurf->ivi_layer);
        wl_list_insert(&new_el->surface_list, &usurf->ivi_layer);
        usurf->win_layer = new_el;
    }
    return new_el;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_set_layer: set(or change) surface layer
 *
 * @param[in]   usurf       UIFW surface
 * @param[in]   layer       layer id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_set_layer(struct uifw_win_surface *usurf, const uint32_t layer)
{
    struct uifw_win_layer *el;
    struct uifw_win_layer *new_el;

    uifw_trace("win_mgr_set_layer: Enter(%08x,%08x,%x)",
               usurf->surfaceid, (int)usurf->surface, layer);

    /* check if same layer                      */
    if ((usurf->win_layer != NULL) && (usurf->win_layer->layer == layer))   {
        uifw_trace("win_mgr_set_layer: Leave(Same Layer)");
        return;
    }

    /* search existing layer                    */
    wl_list_for_each (el, &_ico_win_mgr->ivi_layer_list, link) {
        if (el->layer == layer) break;
    }

    if (&el->link == &_ico_win_mgr->ivi_layer_list)    {
        /* layer not exist, create new layer    */
        uifw_trace("win_mgr_set_layer: New Layer %d", layer);
        new_el = win_mgr_create_layer(usurf, layer);
        if (! new_el)   {
            uifw_trace("win_mgr_set_layer: Leave(No Memory)");
            return;
        }
    }
    else    {
        uifw_trace("win_mgr_set_layer: Add surface to Layer %d", layer);
        wl_list_remove(&usurf->ivi_layer);
        wl_list_insert(&el->surface_list, &usurf->ivi_layer);
        usurf->win_layer = el;
    }

    /* rebild compositor surface list       */
    if (usurf->visible) {
        ico_window_mgr_restack_layer(usurf, 0);
    }
    uifw_trace("win_mgr_set_layer: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_set_active: set(or change) active surface
 *
 * @param[in]   usurf       UIFW surface
 * @param[in]   target      target device
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_set_active(struct uifw_win_surface *usurf, const int target)
{
    struct weston_seat *seat;
    struct weston_surface *surface;
    int object = target;
    wl_fixed_t sx, sy;

    uifw_trace("win_mgr_set_active: Enter(%08x,%x)", (int)usurf, target);

    if ((usurf) && (usurf->shsurf) && (usurf->surface)) {
        surface = usurf->surface;
        if ((target & (ICO_WINDOW_MGR_ACTIVE_POINTER|ICO_WINDOW_MGR_ACTIVE_KEYBOARD)) == 0) {
            surface = NULL;
            if (_ico_win_mgr->active_pointer_usurf == usurf) {
                object |= ICO_WINDOW_MGR_ACTIVE_POINTER;
            }
            if (_ico_win_mgr->active_keyboard_usurf == usurf)    {
                object |= ICO_WINDOW_MGR_ACTIVE_KEYBOARD;
            }
        }
        else    {
            if (object & ICO_WINDOW_MGR_ACTIVE_POINTER) {
                _ico_win_mgr->active_pointer_usurf = usurf;
            }
            if (object & ICO_WINDOW_MGR_ACTIVE_KEYBOARD)    {
                _ico_win_mgr->active_keyboard_usurf = usurf;
            }
        }
    }
    else    {
        surface = NULL;
        if (target == 0)    {
            object = ICO_WINDOW_MGR_ACTIVE_POINTER | ICO_WINDOW_MGR_ACTIVE_KEYBOARD;
        }
        if (object & ICO_WINDOW_MGR_ACTIVE_POINTER) {
            _ico_win_mgr->active_pointer_usurf = NULL;
        }
        if (object & ICO_WINDOW_MGR_ACTIVE_KEYBOARD)    {
            _ico_win_mgr->active_keyboard_usurf = NULL;
        }
    }

    wl_list_for_each(seat, &_ico_win_mgr->compositor->seat_list, link) {
        if ((object & ICO_WINDOW_MGR_ACTIVE_POINTER) && (seat->pointer))   {
            if (surface)    {
                if (seat->pointer->focus != surface) {
                    uifw_trace("win_mgr_set_active: pointer change surface(%08x=>%08x)",
                               (int)seat->pointer->focus, (int)surface);
                    weston_surface_from_global_fixed(surface,
                                                     seat->pointer->x,
                                                     seat->pointer->y,
                                                     &sx, &sy);
                    weston_pointer_set_focus(seat->pointer, surface, sx, sy);
                }
                else    {
                    uifw_trace("win_mgr_set_active: pointer nochange surface(%08x)",
                               (int)surface);
                }
            }
            else    {
                uifw_trace("win_mgr_set_active: pointer reset surface(%08x)",
                           (int)seat->pointer->focus);
                weston_pointer_set_focus(seat->pointer, NULL,
                                         wl_fixed_from_int(0), wl_fixed_from_int(0));
            }
        }
        if ((object & ICO_WINDOW_MGR_ACTIVE_KEYBOARD) && (seat->keyboard))  {
            if (surface)    {
                if (seat->keyboard->focus != surface)    {
                    weston_keyboard_set_focus(seat->keyboard, surface);
                    uifw_trace("win_mgr_set_active: keyboard change surface(%08x=>%08x)",
                               (int)seat->keyboard->focus, (int)surface);
                }
                else    {
                    uifw_trace("win_mgr_set_active: keyboard nochange surface(%08x)",
                               (int)surface);
                }
            }
            else    {
                uifw_trace("win_mgr_set_active: keyboard reset surface(%08x)",
                           (int)seat->keyboard);
                weston_keyboard_set_focus(seat->keyboard, NULL);
            }
        }
    }
    uifw_trace("win_mgr_set_active: Leave(%08x)", (int)usurf);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_ismykeyboard: check active keyboard
 *
 * @param[in]   usurf       UIFW surface
 * @return      check result
 * @retval      =1          usurf is active keyboard surface
 * @retval      =0          usurf is not active
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_window_mgr_ismykeyboard(struct uifw_win_surface *usurf)
{
    return (_ico_win_mgr->active_keyboard_usurf == usurf) ? 1 : 0;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_declare_manager: declare manager(ex.SystemController) client
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   manager     manager(1=manager, 0=not manager)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_declare_manager(struct wl_client *client, struct wl_resource *resource, int manager)
{
    struct uifw_manager* mgr;
    struct uifw_win_surface *usurf;
    struct uifw_client *uclient;
    struct uifw_win_layer *el;

    uifw_trace("uifw_declare_manager: Enter client=%08x manager=%d",
               (int)client, manager);

    uclient = find_client_from_client(client);
    if (! uclient)  {
        uifw_trace("uifw_declare_manager: Leave(unknown client=%08x)", (int)client);
        return;
    }

    uclient->manager = manager;

    /* client set to manager            */
    _ico_win_mgr->num_manager = 0;
    wl_list_for_each (mgr, &_ico_win_mgr->manager_list, link)   {
        if (mgr->resource == resource)  {
            if (mgr->manager != manager)    {
                uifw_trace("uifw_declare_manager: Event Client.%08x Callback %d=>%d",
                           (int)client, mgr->manager, manager);
                mgr->manager = manager;

                if (manager)    {
                    wl_list_for_each (el, &_ico_win_mgr->ivi_layer_list, link) {
                        wl_list_for_each (usurf, &el->surface_list, ivi_layer) {
                            /* send window create event to manager  */
                            if (usurf->created != 0)    {
                                uifw_trace("uifw_declare_manager: Send manager(%08x) "
                                           "WINDOW_CREATED(surf=%08x,pid=%d,appid=%s)",
                                           (int)resource, usurf->surfaceid,
                                           usurf->uclient->pid, usurf->uclient->appid);
                                ico_window_mgr_send_window_created(resource,
                                                                   usurf->surfaceid,
                                                                   usurf->winname,
                                                                   usurf->uclient->pid,
                                                                   usurf->uclient->appid);
                            }
                        }
                    }
                }
            }
        }
        if (mgr->manager)   {
            _ico_win_mgr->num_manager++;
        }
    }
    uifw_trace("uifw_declare_manager: Leave(managers=%d)", _ico_win_mgr->num_manager);
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
                      uint32_t surfaceid, uint32_t layer)
{
    if (layer == ICO_WINDOW_MGR_V_LAYER_INPUT)  {
        layer = _ico_ivi_input_layer;
    }
    else if (layer == ICO_WINDOW_MGR_V_LAYER_CURSOR)    {
        layer = _ico_ivi_cursor_layer;
    }

    uifw_trace("uifw_set_window_layer: Enter res=%08x surfaceid=%08x layer=%d",
               (int)resource, surfaceid, layer);

    struct uifw_win_surface *usurf = ico_window_mgr_get_usurf_client(surfaceid, client);

    if (! usurf)    {
        uifw_trace("uifw_set_window_layer: Leave(No Surface(id=%08x))", surfaceid);
        return;
    }

    if (usurf->win_layer->layer != layer) {
        win_mgr_set_layer(usurf, layer);
        win_mgr_change_surface(usurf->surface, -1, 1);
    }
    uifw_trace("uifw_set_window_layer: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_set_positionsize: set surface position and size
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   surfaceid   UIFW surface id
 * @param[in]   node        surface node id
 * @param[in]   x           X coordinate on screen(if bigger than 16383, no change)
 * @param[in]   y           Y coordinate on screen(if bigger than 16383, no change)
 * @param[in]   width       surface width(if bigger than 16383, no change)
 * @param[in]   height      surface height(if bigger than 16383, no change)
 * @param[in]   flags       with/without animation and client configure flag
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_positionsize(struct wl_client *client, struct wl_resource *resource,
                      uint32_t surfaceid, uint32_t node, int32_t x, int32_t y,
                      int32_t width, int32_t height, int32_t flags)
{
    struct uifw_client *uclient;
    struct uifw_win_surface *usurf;
    struct weston_surface *es;
    int     op;
    int     retanima;

    uifw_trace("uifw_set_positionsize: Enter surf=%08x node=%x x/y/w/h=%d/%d/%d/%d flag=%x",
               surfaceid, node, x, y, width, height, flags);

    usurf = ico_window_mgr_get_usurf_client(surfaceid, client);
    if (! usurf)    {
        uifw_trace("uifw_set_positionsize: Leave(surf=%08x NOT Found)", surfaceid);
        return;
    }

    uclient = find_client_from_client(client);

    usurf->disable = 0;
    if (((int)node) >= _ico_num_nodes)  {
        uifw_trace("uifw_set_positionsize: node=%d dose not exist(max=%d)",
                   node, _ico_num_nodes);
        if ((ico_ivi_debugflag() & ICO_IVI_DEBUG_SHOW_SURFACE) == 0)    {
            if (usurf->visible) {
                /* no display, change to hide   */
                uifw_set_visible(client, resource, surfaceid, ICO_WINDOW_MGR_VISIBLE_HIDE,
                                 ICO_WINDOW_MGR_V_NOCHANGE, 0);
            }
            usurf->disable = 1;
        }
        node = 0;
    }
    usurf->node_tbl = &_ico_node_table[node];

    es = usurf->surface;
    if (es)  {
        /* weston surface exist             */
        es = usurf->surface;
        retanima = ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA;

        /* if x,y,width,height bigger then ICO_IVI_MAX_COORDINATE, no change    */
        if (x > ICO_IVI_MAX_COORDINATE)         x = usurf->x;
        if (y > ICO_IVI_MAX_COORDINATE)         y = usurf->y;
        if (width > ICO_IVI_MAX_COORDINATE)     width = usurf->width;
        if (height > ICO_IVI_MAX_COORDINATE)    height = usurf->height;

        /* check animation                  */
        if ((usurf->animation.restrain_configure != 0) &&
            (x == usurf->x) && (y == usurf->y) &&
            (width == usurf->width) && (height == usurf->height))   {
            uifw_trace("uifw_set_positionsize: Leave(same position size at animation)");
            return;
        }

        if (uclient)    {
            if ((surfaceid != ICO_WINDOW_MGR_V_MAINSURFACE) &&
                (uclient->manager == 0)) uclient = NULL;
        }
        if (! uclient)  {
            if ((usurf->width > 0) && (usurf->height > 0))  {
                win_mgr_surface_change_mgr(es, x, y, width, height);
                uifw_trace("uifw_set_positionsize: Leave(Request from App)");
                return;
            }

            uifw_trace("uifw_set_positionsize: Initial Position/Size visible=%d",
                       usurf->visible);
            /* Initiale position is (0,0)   */
            weston_surface_set_position(es, (float)(usurf->node_tbl->disp_x),
                                        (float)(usurf->node_tbl->disp_y));
        }

        uifw_trace("uifw_set_positionsize: Old geometry x/y=%d/%d,w/h=%d/%d",
                   (int)es->geometry.x, (int)es->geometry.y,
                   (int)es->geometry.width, (int)es->geometry.height);

        usurf->animation.pos_x = usurf->x;
        usurf->animation.pos_y = usurf->y;
        usurf->animation.pos_width = usurf->width;
        usurf->animation.pos_height = usurf->height;
        usurf->animation.no_configure = (flags & ICO_WINDOW_MGR_FLAGS_NO_CONFIGURE) ? 1 : 0;
        usurf->x = x;
        usurf->y = y;
        usurf->width = width;
        usurf->height = height;
        if (_ico_win_mgr->num_manager <= 0) {
            /* no manager(HomeScreen), set geometory    */
            weston_surface_set_position(es, (float)(usurf->node_tbl->disp_x + x),
                                        (float)(usurf->node_tbl->disp_y + y));
        }
        if ((es->output) && (es->buffer_ref.buffer) &&
            (es->geometry.width > 0) && (es->geometry.height > 0)) {
            uifw_trace("uifw_set_positionsize: Fixed Geometry, Change(Vis=%d)",
                       usurf->visible);
            if (usurf->visible) {
                if ((flags & ICO_WINDOW_MGR_FLAGS_ANIMATION) &&
                    (win_mgr_hook_animation != NULL))   {
                    /* with animation   */
                    if ((x != (usurf->surface->geometry.x - usurf->node_tbl->disp_x)) ||
                        (y != (usurf->surface->geometry.y - usurf->node_tbl->disp_y)))  {
                        op = ICO_WINDOW_MGR_ANIMATION_OPMOVE;
                    }
                    else if ((width != usurf->surface->geometry.width) ||
                             (height != usurf->surface->geometry.height))   {
                        op = ICO_WINDOW_MGR_ANIMATION_OPRESIZE;
                    }
                    else    {
                        op = ICO_WINDOW_MGR_ANIMATION_OPNONE;
                    }
                    if (((op == ICO_WINDOW_MGR_ANIMATION_OPMOVE) &&
                         (usurf->animation.move_anima != ICO_WINDOW_MGR_ANIMATION_NONE)) ||
                        ((op == ICO_WINDOW_MGR_ANIMATION_OPRESIZE) &&
                         (usurf->animation.resize_anima != ICO_WINDOW_MGR_ANIMATION_NONE))) {
                        retanima = (*win_mgr_hook_animation)(op, (void *)usurf);
                        uifw_trace("uifw_set_positionsize: ret call anima = %d", retanima);
                    }
                }
                if ((retanima == ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA) ||
                    (retanima != ICO_WINDOW_MGR_ANIMATION_RET_ANIMANOCTL))  {
                    ico_window_mgr_set_weston_surface(usurf, usurf->x, usurf->y,
                                                      usurf->width, usurf->height);
                }
            }
        }
        if ((retanima == ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA) ||
            (retanima != ICO_WINDOW_MGR_ANIMATION_RET_ANIMANOCTL))  {
            win_mgr_change_surface(es,
                                   (flags & ICO_WINDOW_MGR_FLAGS_NO_CONFIGURE) ? -1 : 0, 1);
        }
        uifw_trace("uifw_set_positionsize: Leave(OK,output=%08x)", (int)es->output);
    }
    else    {
        usurf->x = x;
        usurf->y = y;
        usurf->width = width;
        usurf->height = height;
        uifw_trace("uifw_set_positionsize: Leave(OK,but no buffer)");
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
 * @param[in]   flags       with/without animation
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_visible(struct wl_client *client, struct wl_resource *resource,
                 uint32_t surfaceid, int32_t visible, int32_t raise, int32_t flags)
{
    struct uifw_win_surface *usurf;
    struct uifw_client *uclient;
    int     restack;
    int     retanima;

    uifw_trace("uifw_set_visible: Enter(surf=%08x,%d,%d,%x)",
               surfaceid, visible, raise, flags);

    usurf = ico_window_mgr_get_usurf_client(surfaceid, client);
    if ((! usurf) || (! usurf->surface))    {
        uifw_trace("uifw_set_visible: Leave(Surface Not Exist)");
        return;
    }

    uclient = find_client_from_client(client);
    if (uclient)    {
        if ((surfaceid != ICO_WINDOW_MGR_V_MAINSURFACE) &&
            (uclient->manager == 0))    {
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

    restack = 0;

    if ((usurf->disable == 0) && (visible == ICO_WINDOW_MGR_VISIBLE_SHOW))  {

        if (! usurf->visible)  {
            usurf->visible = 1;
            uifw_trace("uifw_set_visible: Change to Visible");

            ico_ivi_shell_set_toplevel(usurf->shsurf);

            /* Weston surface configure                     */
            uifw_trace("uifw_set_visible: Visible to Weston WSurf=%08x,%d.%d/%d/%d/%d",
                       (int)usurf->surface, usurf->node_tbl->node, usurf->x, usurf->y,
                       usurf->width, usurf->height);
            ico_ivi_shell_set_surface_type(usurf->shsurf);
            ico_window_mgr_set_weston_surface(usurf, usurf->x, usurf->y,
                                              usurf->width, usurf->height);

            restack = 1;                    /* need damage      */

            if ((flags & (ICO_WINDOW_MGR_ANIMATION_POS|ICO_WINDOW_MGR_FLAGS_ANIMATION)) &&
                (usurf->animation.show_anima != ICO_WINDOW_MGR_ANIMATION_NONE) &&
                (win_mgr_hook_animation != NULL))   {
                usurf->animation.pos_x = usurf->x;
                usurf->animation.pos_y = usurf->y;
                usurf->animation.pos_width = usurf->width;
                usurf->animation.pos_height = usurf->height;
                usurf->animation.no_configure = 0;
                retanima = (*win_mgr_hook_animation)(
                                (flags & ICO_WINDOW_MGR_ANIMATION_POS) ?
                                    ICO_WINDOW_MGR_ANIMATION_OPSHOWPOS :
                                    ICO_WINDOW_MGR_ANIMATION_OPSHOW,
                                (void *)usurf);
                uifw_trace("uifw_set_visible: ret call anima = %d", retanima);
            }
        }
        else if ((raise != ICO_WINDOW_MGR_RAISE_LOWER) &&
                 (raise != ICO_WINDOW_MGR_RAISE_RAISE))  {
            uifw_trace("uifw_set_visible: Leave(No Change)");
            return;
        }
    }
    else if (visible == ICO_WINDOW_MGR_VISIBLE_HIDE)    {

        if (usurf->visible)    {

            /* Weston surface configure                     */
            weston_surface_damage_below(usurf->surface);
            ico_window_mgr_set_weston_surface(usurf, usurf->x, usurf->y,
                                              usurf->width, usurf->height);

            retanima = ICO_WINDOW_MGR_ANIMATION_RET_ANIMA;
            if ((flags & (ICO_WINDOW_MGR_FLAGS_ANIMATION|ICO_WINDOW_MGR_ANIMATION_POS)) &&
                (usurf->animation.hide_anima != ICO_WINDOW_MGR_ANIMATION_NONE) &&
                (win_mgr_hook_animation != NULL))   {
                usurf->animation.pos_x = usurf->x;
                usurf->animation.pos_y = usurf->y;
                usurf->animation.pos_width = usurf->width;
                usurf->animation.pos_height = usurf->height;
                usurf->animation.no_configure = 0;
                retanima = (*win_mgr_hook_animation)(
                                (flags & ICO_WINDOW_MGR_ANIMATION_POS) ?
                                    ICO_WINDOW_MGR_ANIMATION_OPHIDEPOS :
                                    ICO_WINDOW_MGR_ANIMATION_OPHIDE,
                                (void *)usurf);
            }
            if (retanima != ICO_WINDOW_MGR_ANIMATION_RET_ANIMANOCTL)    {
                usurf->visible = 0;
                uifw_trace("uifw_set_visible: Change to UnVisible");
                /* change visible to unvisible, restack surface list    */
                restack = 1;
                /* Weston surface configure                     */
                ico_window_mgr_set_weston_surface(usurf, usurf->x, usurf->y,
                                                  usurf->width, usurf->height);
            }
            else    {
                uifw_trace("uifw_set_visible: UnVisible but animation");
            }
        }
        else if ((raise != ICO_WINDOW_MGR_RAISE_LOWER) &&
                 (raise != ICO_WINDOW_MGR_RAISE_RAISE))  {
            uifw_trace("uifw_set_visible: Leave(No Change)");
            return;
        }
    }
    else if ((raise != ICO_WINDOW_MGR_RAISE_LOWER) &&
             (raise != ICO_WINDOW_MGR_RAISE_RAISE))  {
        uifw_trace("uifw_set_visible: Leave(No Change)");
        return;
    }

    /* raise/lower                              */
    if ((raise == ICO_WINDOW_MGR_RAISE_LOWER) || (raise == ICO_WINDOW_MGR_RAISE_RAISE))  {
        win_mgr_set_raise(usurf, raise);
        if (usurf->visible == 0)    {
            restack |= 2;
        }
    }
    else    {
        raise = ICO_WINDOW_MGR_V_NOCHANGE;
    }

    if (restack)    {
        ico_window_mgr_restack_layer(usurf, 0);
    }

    /* send event(VISIBLE) to manager           */
    if (restack)    {
        ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_VISIBLE,
                                usurf,
                                (visible == ICO_WINDOW_MGR_VISIBLE_SHOW) ? 1 :
                                    ((visible == ICO_WINDOW_MGR_VISIBLE_HIDE) ? 0 :
                                        ICO_WINDOW_MGR_V_NOCHANGE),
                                raise, uclient ? 0 : 1, 0,0);
    }
    uifw_trace("uifw_set_visible: Leave(OK)");
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
    int animaid;
    struct uifw_win_surface *usurf = ico_window_mgr_get_usurf_client(surfaceid, client);

    uifw_trace("uifw_set_transition: Enter(surf=%08x,type=%x,anim=%s,time=%d)",
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
        if ((time > 0) && (time != ICO_WINDOW_MGR_V_NOCHANGE))  {
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
        uifw_trace("uifw_set_animation: Leave(Surface(%08x) Not exist)", surfaceid);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_set_attributes: set surface attributes
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   surfaceid   UIFW surface id
 * @param[in]   attributes  surface attributes
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_attributes(struct wl_client *client, struct wl_resource *resource,
                    uint32_t surfaceid, uint32_t attributes)
{
    struct uifw_win_surface *usurf = ico_window_mgr_get_usurf_client(surfaceid, client);

    uifw_trace("uifw_set_attributes: Enter(surf=%08x,attributes=%x)", surfaceid, attributes);

    if (usurf) {
        usurf->attributes = attributes;
        if ((attributes & (ICO_WINDOW_MGR_ATTR_ALIGN_LEFT|ICO_WINDOW_MGR_ATTR_ALIGN_RIGHT)) ==
            (ICO_WINDOW_MGR_ATTR_ALIGN_LEFT|ICO_WINDOW_MGR_ATTR_ALIGN_RIGHT))   {
            usurf->attributes &=
                ~(ICO_WINDOW_MGR_ATTR_ALIGN_LEFT|ICO_WINDOW_MGR_ATTR_ALIGN_RIGHT);
        }
        if ((attributes & (ICO_WINDOW_MGR_ATTR_ALIGN_TOP|ICO_WINDOW_MGR_ATTR_ALIGN_BOTTOM)) ==
            (ICO_WINDOW_MGR_ATTR_ALIGN_TOP|ICO_WINDOW_MGR_ATTR_ALIGN_BOTTOM))   {
            usurf->attributes &=
                ~(ICO_WINDOW_MGR_ATTR_ALIGN_TOP|ICO_WINDOW_MGR_ATTR_ALIGN_BOTTOM);
        }
    }
    uifw_trace("uifw_set_attributes: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_visible_animation: surface visibility control with animation
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   surfaceid   surface id
 * @param[in]   visible     visible(1=show/0=hide)
 * @param[in]   x           X coordinate on screen(if bigger than 16383, no change)
 * @param[in]   y           Y coordinate on screen(if bigger than 16383, no change)
 * @param[in]   width       surface width(if bigger than 16383, no change)
 * @param[in]   height      surface height(if bigger than 16383, no change)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_visible_animation(struct wl_client *client, struct wl_resource *resource,
                       uint32_t surfaceid, int32_t visible,
                       int32_t x, int32_t y, int32_t width, int32_t height)
{
    struct uifw_win_surface *usurf;

    uifw_trace("uifw_visible_animation: Enter(%08x,%d,x/y=%d/%d,w/h=%d/%d)",
               surfaceid, visible, x, y, width, height);

    usurf = ico_window_mgr_get_usurf_client(surfaceid, client);

    if ((! usurf) || (! usurf->surface))    {
        uifw_trace("uifw_visible_animation: Leave(Surface Not Exist)");
        return;
    }

    usurf->animation.pos_x = x;
    usurf->animation.pos_y = y;
    if (width > 0)  usurf->animation.pos_width = width;
    else            usurf->animation.pos_width = 1;
    if (height > 0) usurf->animation.pos_height = height;
    else            usurf->animation.pos_height = 1;
    usurf->animation.no_configure = 0;

    uifw_set_visible(client, resource, surfaceid, visible,
                     ICO_WINDOW_MGR_V_NOCHANGE, ICO_WINDOW_MGR_ANIMATION_POS);

    uifw_trace("uifw_visible_animation: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_set_active: set active surface
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   surfaceid   UIFW surface id
 * @param[in]   active      target device
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_active(struct wl_client *client, struct wl_resource *resource,
                uint32_t surfaceid, int32_t active)
{
    struct uifw_win_surface *usurf;

    uifw_trace("uifw_set_active: Enter(surf=%08x,active=%x)", surfaceid, active);

    if ((surfaceid > 0) &&
        ((active & (ICO_WINDOW_MGR_ACTIVE_POINTER|ICO_WINDOW_MGR_ACTIVE_KEYBOARD)) != 0)) {
        usurf = ico_window_mgr_get_usurf_client(surfaceid, client);
    }
    else    {
        usurf = NULL;
    }
    if (usurf) {
        switch (active & (ICO_WINDOW_MGR_ACTIVE_POINTER|ICO_WINDOW_MGR_ACTIVE_KEYBOARD)) {
        case ICO_WINDOW_MGR_ACTIVE_POINTER:
            if (usurf != _ico_win_mgr->active_pointer_usurf)  {
                if (_ico_win_mgr->active_pointer_usurf)   {
                    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                            _ico_win_mgr->active_pointer_usurf,
                                            (_ico_win_mgr->active_keyboard_usurf ==
                                             _ico_win_mgr->active_pointer_usurf) ?
                                                ICO_WINDOW_MGR_ACTIVE_KEYBOARD :
                                                ICO_WINDOW_MGR_ACTIVE_NONE,
                                            0,0,0,0);
                }
                _ico_win_mgr->active_pointer_usurf = usurf;
                ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                        usurf,
                                        ICO_WINDOW_MGR_ACTIVE_POINTER |
                                        (_ico_win_mgr->active_keyboard_usurf == usurf) ?
                                            ICO_WINDOW_MGR_ACTIVE_KEYBOARD : 0,
                                        0,0,0,0);
                win_mgr_set_active(usurf, ICO_WINDOW_MGR_ACTIVE_POINTER);
            }
            break;
        case ICO_WINDOW_MGR_ACTIVE_KEYBOARD:
            if (usurf != _ico_win_mgr->active_keyboard_usurf) {
                if (_ico_win_mgr->active_keyboard_usurf)   {
                    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                            _ico_win_mgr->active_keyboard_usurf,
                                            (_ico_win_mgr->active_keyboard_usurf ==
                                             _ico_win_mgr->active_pointer_usurf) ?
                                                ICO_WINDOW_MGR_ACTIVE_POINTER :
                                                ICO_WINDOW_MGR_ACTIVE_NONE,
                                            0,0,0,0);
                }
                _ico_win_mgr->active_keyboard_usurf = usurf;
                ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                        usurf,
                                        ICO_WINDOW_MGR_ACTIVE_KEYBOARD |
                                        (_ico_win_mgr->active_pointer_usurf == usurf) ?
                                            ICO_WINDOW_MGR_ACTIVE_POINTER : 0,
                                        0,0,0,0);
                win_mgr_set_active(usurf, ICO_WINDOW_MGR_ACTIVE_KEYBOARD);
            }
            break;
        default:
            if ((usurf != _ico_win_mgr->active_pointer_usurf) ||
                (usurf != _ico_win_mgr->active_keyboard_usurf))   {
                if (_ico_win_mgr->active_pointer_usurf)   {
                    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                            _ico_win_mgr->active_pointer_usurf,
                                            ICO_WINDOW_MGR_ACTIVE_NONE,
                                            0,0,0,0);
                    if (_ico_win_mgr->active_keyboard_usurf ==
                        _ico_win_mgr->active_pointer_usurf)   {
                        _ico_win_mgr->active_keyboard_usurf = NULL;
                    }
                }
                if (_ico_win_mgr->active_keyboard_usurf)   {
                    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                            _ico_win_mgr->active_keyboard_usurf,
                                            ICO_WINDOW_MGR_ACTIVE_NONE,
                                            0,0,0,0);
                }
                _ico_win_mgr->active_pointer_usurf = usurf;
                _ico_win_mgr->active_keyboard_usurf = usurf;
                ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                        usurf,
                                        ICO_WINDOW_MGR_ACTIVE_POINTER |
                                            ICO_WINDOW_MGR_ACTIVE_KEYBOARD,
                                        0,0,0,0);
                win_mgr_set_active(usurf, ICO_WINDOW_MGR_ACTIVE_POINTER |
                                              ICO_WINDOW_MGR_ACTIVE_KEYBOARD);
            }
            break;
        }
        uifw_trace("uifw_set_active: Leave(Change Active)");
    }
    else    {
        win_mgr_set_active(NULL, active);
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
                       uint32_t layer, int32_t visible)
{
    struct uifw_win_layer   *el;
    struct uifw_win_layer   *new_el;
    struct uifw_win_surface *usurf;

    if (layer == ICO_WINDOW_MGR_V_LAYER_INPUT)  {
        layer = _ico_ivi_input_layer;
    }
    else if (layer == ICO_WINDOW_MGR_V_LAYER_CURSOR)    {
        layer = _ico_ivi_cursor_layer;
    }

    uifw_trace("uifw_set_layer_visible: Enter(layer=%d, visilbe=%d)", layer, visible);

    /* Search Layer                             */
    wl_list_for_each (el, &_ico_win_mgr->ivi_layer_list, link) {
        if (el->layer == layer) break;
    }

    if (&el->link == &_ico_win_mgr->ivi_layer_list)    {
        /* layer not exist, create new layer    */
        uifw_trace("uifw_set_layer_visible: New Layer %d", layer);
        new_el = win_mgr_create_layer(NULL, layer);
        if (! new_el)   {
            uifw_trace("uifw_set_layer_visible: Leave(No Memory)");
            return;
        }
        new_el->visible = (visible != 0) ? TRUE : FALSE;
        ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_LAYER_VISIBLE, NULL,
                                layer, new_el->visible, 0,0,0);
        uifw_trace("uifw_set_layer_visible: Leave(new layer)");
        return;
    }

    /* control all surface in layer */
    if ((el->visible != FALSE) && (visible == 0))   {
        /* layer change to NOT visible  */
        uifw_trace("uifw_set_layer_visible: change to not visible");
        el->visible = FALSE;
    }
    else if ((el->visible == FALSE) && (visible != 0))  {
        /* layer change to visible      */
        uifw_trace("uifw_set_layer_visible: change to visible");
        el->visible = TRUE;
    }
    else    {
        /* no change    */
        uifw_trace("uifw_set_layer_visible: Leave(no Change %d=>%d)",
                   el->visible, visible);
        return;
    }

    /* set damege area          */
    wl_list_for_each (usurf, &el->surface_list, ivi_layer) {
        if ((usurf->visible != FALSE) && (usurf->surface != NULL) &&
            (usurf->surface->output != NULL))  {
            /* Damage(redraw) target surface    */
            weston_surface_damage_below(usurf->surface);
        }
    }

    /* rebild compositor surface list       */
    ico_window_mgr_restack_layer(NULL, 0);

    /* send layer visible event to manager  */
    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_LAYER_VISIBLE, NULL,
                            layer, el->visible, 0,0,0);

    uifw_trace("uifw_set_layer_visible: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   uifw_get_surfaces: get application surfaces
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   appid       application id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_get_surfaces(struct wl_client *client, struct wl_resource *resource, const char *appid)
{
    struct uifw_client  *uclient;
    struct uifw_win_layer *el;
    struct uifw_win_surface *usurf;
    struct wl_array     reply;
    uint32_t            *up;

    uifw_trace("uifw_get_surfaces: Enter(appid=%s)", appid);

    wl_array_init(&reply);

    wl_list_for_each (uclient, &_ico_win_mgr->client_list, link)    {
        if (strcmp(uclient->appid, appid) == 0) break;
    }
    if (&uclient->link == &_ico_win_mgr->client_list)    {
        uifw_trace("uifw_get_surfaces: appid=%s dose not exist", appid);
    }
    else    {
        wl_list_for_each (el, &_ico_win_mgr->ivi_layer_list, link) {
            wl_list_for_each (usurf, &el->surface_list, ivi_layer) {
                if (usurf->uclient == uclient)  {
                    uifw_trace("uifw_get_surfaces: %s surf=%08x", appid, usurf->surfaceid);
                    up = (uint32_t *)wl_array_add(&reply, sizeof(uint32_t));
                    if (up) {
                        *up = usurf->surfaceid;
                    }
                }
            }
        }
    }
    ico_window_mgr_send_app_surfaces(resource, appid, &reply);

    wl_array_release(&reply);
    uifw_trace("uifw_get_surfaces: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_check_surfacemap: check and change all surface
 *
 * @param[in]   animation   weston animation table(unused)
 * @param[in]   outout      weston output table
 * @param[in]   mseces      current time(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_check_surfacemap(struct weston_animation *animation,
                            struct weston_output *output, uint32_t msecs)
{
    struct uifw_surface_map *sm;

    wl_list_for_each(sm, &_ico_win_mgr->map_list, map_link) {
        if (sm->usurf->surface->output == output)   {
            win_mgr_change_mapsurface(sm, 0);
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_change_mapsurface: check and change mapped surface
 *
 * @param[in]   sm          map surface table
 * @param[in]   event       send event (if 0, send if changed)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_change_mapsurface(struct uifw_surface_map *sm, int event)
{
    struct uifw_drm_buffer  *drm_buffer;
    struct uifw_dri_image   *dri_image;
    struct uifw_intel_region  *dri_region;
    struct uifw_gl_surface_state *gl_state;
    struct weston_surface   *es;
    uint32_t    eglname = 0;
    int         width;
    int         height;
    int         stride;
    uint32_t    format;

    /* check if buffered        */
    es = sm->usurf->surface;
    if ((es == NULL) || (es->buffer_ref.buffer == NULL) ||
        (es->buffer_ref.buffer->width <= 0) || (es->buffer_ref.buffer->height <= 0) ||
        (es->buffer_ref.buffer->legacy_buffer == NULL) || (es->renderer_state == NULL) ||
        (((struct uifw_drm_buffer *)es->buffer_ref.buffer->legacy_buffer)->driver_buffer
         == NULL))  {
        /* surface has no buffer, error         */
        uifw_trace("win_mgr_change_mapsurface: surface(%08x) has no buffer",
                   sm->usurf->surfaceid);
        sm->width = 0;
        sm->height = 0;
        sm->stride = 0;
        sm->eglname = 0;
        sm->format = 0;
        if (sm->initflag)   {
            event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP;
        }
        else    {
            event = 0;
        }
    }
    else    {
        gl_state = (struct uifw_gl_surface_state *)es->renderer_state;
        if (gl_state->buffer_type == BUFFER_TYPE_SHM)   {
            event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR;
        }
        else if (gl_state->buffer_type != BUFFER_TYPE_EGL)   {
            event = 0;
        }
        else    {
            drm_buffer = (struct uifw_drm_buffer *)es->buffer_ref.buffer->legacy_buffer;
            dri_image = (struct uifw_dri_image *)drm_buffer->driver_buffer;
            dri_region = dri_image->region;
            width = es->buffer_ref.buffer->width;
            height = es->buffer_ref.buffer->height;
            stride = drm_buffer->stride[0];
            if (drm_buffer->format == 0x34325258)   {
                format = EGL_TEXTURE_RGB;
            }
            else if (drm_buffer->format == 0x34325241)  {
                format = EGL_TEXTURE_RGBA;
            }
            else    {
                /* unknown format, error    */
                format = EGL_NO_TEXTURE;
            }
            eglname = dri_region->name;

            if ((sm->initflag == 0) && (width > 0) && (height > 0) && (stride > 0)) {
                sm->initflag = 1;
                event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_MAP;
            }
            else    {
                if ((width <= 0) || (height <= 0) || (stride <= 0)) {
                    event = 0;
                }
                else if (event == 0)    {
                    if ((sm->width != width) || (sm->height != height) ||
                        (sm->stride != stride)) {
                        event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_RESIZE;
                    }
                    else if ((eglname != sm->eglname) || (format != sm->format))    {
                        event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_CONTENTS;
                    }
                }
            }
            sm->width = width;
            sm->height = height;
            sm->stride = stride;
            sm->eglname = eglname;
            sm->format = format;
            sm->eglname = eglname;
        }
    }

    if (event != 0) {
#if 0       /* too many log */
        uifw_trace("win_mgr_change_mapsurface: send MAP event(ev=%d surf=%08x name=%08x "
                   "w/h/s=%d/%d/%d format=%x",
                   event, sm->usurf->surfaceid, sm->eglname, sm->width, sm->height,
                   sm->stride, sm->format);
#endif
        ico_window_mgr_send_map_surface(sm->uclient->mgr->resource, event,
                                        sm->usurf->surfaceid, sm->type, sm->eglname,
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
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_map_surface(struct wl_client *client, struct wl_resource *resource,
                 uint32_t surfaceid, int32_t framerate)
{
    struct uifw_win_surface     *usurf;
    struct weston_surface       *es;
    struct uifw_surface_map     *sm;
    struct weston_buffer        *buffer;
    struct uifw_client          *uclient;
    struct uifw_drm_buffer      *drm_buffer;
    struct uifw_gl_surface_state *gl_state;

    uifw_trace("uifw_map_surface: Enter(surface=%08x,fps=%d)", surfaceid, framerate);

    usurf = ico_window_mgr_get_usurf(surfaceid);
    if (! usurf)    {
        /* surface dose not exist, error        */
        ico_window_mgr_send_map_surface(resource, ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                        surfaceid, 1, 0, 0, 0, 0, 0);
        uifw_trace("uifw_map_surface: Leave(surface=%08x dose not exist)", surfaceid);
        return;
    }
    uclient = find_client_from_client(client);
    if ((! uclient) || (! uclient->mgr))    {
        /* client dose not exist, error         */
        ico_window_mgr_send_map_surface(resource, ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                        surfaceid, 2, 0, 0, 0, 0, 0);
        uifw_trace("uifw_map_surface: Leave(client=%08x dose not exist)", (int)client);
        return;
    }

    es = usurf->surface;

    /* check if buffered        */
    if ((es == NULL) || (es->buffer_ref.buffer == NULL))    {
        /* surface has no buffer, error         */
        ico_window_mgr_send_map_surface(resource, ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                        surfaceid, 3, 0, 0, 0, 0, 0);
        uifw_trace("uifw_map_surface: Leave(surface(%08x) has no buffer)", surfaceid);
        return;
    }

    /* check buffer type        */
    gl_state = (struct uifw_gl_surface_state *)es->renderer_state;
    if ((gl_state == NULL) || (gl_state->buffer_type == BUFFER_TYPE_SHM))   {
        /* wl_shm_buffer not support    */
        ico_window_mgr_send_map_surface(resource, ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                        surfaceid, 4, 0, 0, 0, 0, 0);
        uifw_trace("uifw_map_surface: Leave(surface(%08x) is wl_shm_buffer, "
                   "not support)", surfaceid);
        return;
    }

    /* create map table         */
    sm = _ico_win_mgr->free_maptable;
    if (sm) {
        _ico_win_mgr->free_maptable = (struct uifw_surface_map *)sm->usurf;
    }
    else    {
        sm = (struct uifw_surface_map *)malloc(sizeof(struct uifw_surface_map));
        if (! sm)   {
            ico_window_mgr_send_map_surface(resource, ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                            surfaceid, 5, 0, 0, 0, 0, 0);
            uifw_trace("uifw_map_surface: Leave(malloc error)");
            return;
        }
    }
    memset(sm, 0, sizeof(struct uifw_surface_map));

    wl_list_init(&sm->map_link);
    wl_list_init(&sm->surf_link);
    sm->usurf = usurf;
    sm->uclient = uclient;
    sm->type = ICO_WINDOW_MGR_MAP_TYPE_EGL;
    sm->eglname = 0;
    sm->framerate = framerate;
    wl_list_insert(_ico_win_mgr->map_list.next, &sm->map_link);
    wl_list_insert(usurf->surf_map.prev, &sm->surf_link);

    buffer = es->buffer_ref.buffer;
    if (gl_state->buffer_type == BUFFER_TYPE_EGL) {
        sm->width = buffer->width;
        sm->height = buffer->height;
        drm_buffer = (struct uifw_drm_buffer *)buffer->legacy_buffer;
        if (drm_buffer != NULL) {
            sm->stride = drm_buffer->stride[0];
            if (drm_buffer->format == 0x34325258)   {
                sm->format = EGL_TEXTURE_RGB;
            }
            else if (drm_buffer->format == 0x34325241)  {
                sm->format = EGL_TEXTURE_RGBA;
            }
            else    {
                /* unknown format, error    */
                sm->format = EGL_NO_TEXTURE;
            }
            if ((sm->width > 0) && (sm->height > 0) && (sm->stride > 0) &&
                (gl_state != NULL))  {
                sm->initflag = 1;
            }
        }
    }

    /* send map event                       */
    if (sm->initflag)   {
        win_mgr_change_mapsurface(sm, ICO_WINDOW_MGR_MAP_SURFACE_EVENT_MAP);
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
        uclient = find_client_from_client(client);
        if ((! uclient) || (! uclient->mgr))    {
            /* client dose not exist, error         */
            uifw_trace("uifw_unmap_surface: Leave(client=%08x dose not exist)", (int)client);
            return;
        }
    }
    else    {
        uclient = NULL;
        wl_list_for_each(sm, &usurf->surf_map, surf_link) {
            if (sm->uclient->mgr != NULL) {
                uifw_trace("uifw_unmap_surface: send MAP event(ev=%d surf=%08x name=%08x "
                           "w/h/s=%d/%d/%d format=%x",
                           ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP, surfaceid,
                           sm->eglname, sm->width, sm->height, sm->stride, sm->format);
                ico_window_mgr_send_map_surface(sm->uclient->mgr->resource,
                                                ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP,
                                                surfaceid, sm->type, sm->eglname, sm->width,
                                                sm->height, sm->stride, sm->format);
            }
        }
    }

    wl_list_for_each_safe(sm, sm_tmp, &usurf->surf_map, surf_link) {
        if (((uclient != NULL) && (sm->uclient != uclient)))   continue;
        /* send unmap event                     */
        if ((uclient != NULL) && (uclient->mgr != NULL))    {
            uifw_trace("uifw_unmap_surface: send MAP event(ev=%d surf=%08x name=%08x "
                       "w/h/s=%d/%d/%d format=%x",
                       ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP, surfaceid,
                       sm->eglname, sm->width, sm->height, sm->stride, sm->format);
            ico_window_mgr_send_map_surface(uclient->mgr->resource,
                                            ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP,
                                            surfaceid, sm->type, sm->eglname, sm->width,
                                            sm->height, sm->stride, sm->format);
        }
        wl_list_remove(&sm->surf_link);
        wl_list_remove(&sm->map_link);
        sm->usurf = (struct uifw_win_surface *)_ico_win_mgr->free_maptable;
        _ico_win_mgr->free_maptable = sm;
    }
    uifw_trace("uifw_unmap_surface: Leave");
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
                                      usurf, x, y, width, height, 1);

    uifw_trace("win_mgr_surface_change_mgr: Leave(%d)", num_mgr);
    return num_mgr;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_change_surface: surface change
 *
 * @param[in]   surface     Weston surface
 * @param[in]   to          destination(0=Client&Manager,1=Client,-1=Manager)
 * @param[in]   manager     request from manager(0=Client,1=Manager)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_change_surface(struct weston_surface *surface, const int to, const int manager)
{
    struct uifw_win_surface *usurf;
    struct weston_surface   *es;
    int     x;
    int     y;
    int     repaint = 0;

    uifw_trace("win_mgr_change_surface: Enter(%08x,%d,%d)", (int)surface, to, manager);

    /* Find surface         */
    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("win_mgr_change_surface: Leave(Not Exist)");
        return;
    }
    es = usurf->surface;
    if (! es)   {
        uifw_trace("win_mgr_change_surface: Leave(No weston surface)");
        return;
    }

    /* if not configure surface, set surface size   */
    if ((usurf->width <= 0) || (usurf->height <= 0))  {
        uifw_trace("win_mgr_change_surface: set surface x/y=%d/%d=>%d/%d w/h=%d/%d=>%d/%d",
                   (int)es->geometry.x, (int)es->geometry.y, usurf->x, usurf->y,
                   usurf->width, usurf->height, es->geometry.width, es->geometry.height);
        usurf->width = es->geometry.width;
        usurf->height = es->geometry.height;
        win_mgr_set_scale(usurf);
        if (usurf->visible) {
            weston_surface_set_position(usurf->surface,
                                        (float)(usurf->node_tbl->disp_x +
                                                usurf->x + usurf->xadd),
                                        (float)(usurf->node_tbl->disp_y +
                                                usurf->y + usurf->yadd));
            ico_window_mgr_restack_layer(usurf, 0);
        }
        else    {
            weston_surface_set_position(usurf->surface, (float)(ICO_IVI_MAX_COORDINATE+1),
                                        (float)(ICO_IVI_MAX_COORDINATE+1));
        }
    }

    /* send wayland event to client     */
    if ((to >= 0) && (usurf->shsurf != NULL) && (manager !=0) &&
        (usurf->width > 0) && (usurf->height > 0))  {
        if ((usurf->width != usurf->conf_width) ||
            (usurf->height != usurf->conf_height))  {
            usurf->conf_width = usurf->width;
            usurf->conf_height = usurf->height;
            uifw_trace("win_mgr_change_surface: SURFACE_CONFIGURE %08x(%08x),w/h=%d/%d ",
                       usurf->surfaceid, (int)es, usurf->width, usurf->height);
            ico_ivi_shell_send_configure(es,
                                         WL_SHELL_SURFACE_RESIZE_BOTTOM_RIGHT,
                                         usurf->width, usurf->height);
        }
    }

    if (usurf->visible) {
        x = usurf->node_tbl->disp_x + usurf->x;
        y = usurf->node_tbl->disp_y + usurf->y;
    }
    else    {
        x = ICO_IVI_MAX_COORDINATE+1;
        y = ICO_IVI_MAX_COORDINATE+1;
    }
    /* change geometry if request from manager  */
    if (manager)    {
        if ((usurf->width != es->geometry.width) ||
            (usurf->height != es->geometry.height) ||
            (es->geometry.x != (float)x) || (es->geometry.y != (float)y))   {
            win_mgr_surface_configure(usurf, (float)x, (float)y, usurf->width, usurf->height);
            repaint ++;
        }
    }

    /* send manager event to HomeScreen */
    if (to <= 0)    {
        if (manager)    {
            ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_CONFIGURE,
                                    usurf, usurf->x, usurf->y,
                                    usurf->width, usurf->height, 0);
        }
        else    {
            ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_CONFIGURE,
                                    usurf, (int)es->geometry.x, (int)es->geometry.y,
                                    es->geometry.width, es->geometry.height, 1);
        }
    }

    /* change geometry if request from client   */
    if (! manager)  {
        if ((usurf->width != es->geometry.width) || (usurf->height != es->geometry.height) ||
            (es->geometry.x != (float)x) || (es->geometry.y != (float)y))   {
            win_mgr_surface_configure(usurf, x, y, usurf->width, usurf->height);
            repaint ++;
        }
    }

    if (repaint)    {
        uifw_trace("win_mgr_change_surface: repaint");
        weston_compositor_schedule_repaint(_ico_win_mgr->compositor);
    }
    uifw_trace("win_mgr_change_surface: Leave(OK)");
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

    es = usurf->surface;
    if ((es != NULL) && (es->buffer_ref.buffer))  {
        if (usurf->client_width == 0)   {
            usurf->client_width = es->geometry.width;
            if (usurf->client_width == 0)
                usurf->client_width = weston_surface_buffer_width(es);
        }
        if (usurf->client_height == 0)  {
            usurf->client_height = es->geometry.height;
            if (usurf->client_height == 0)
                usurf->client_height = weston_surface_buffer_height(es);
        }

        /* not set geometry width/height    */
        win_mgr_set_scale(usurf);
        weston_surface_set_position(es, x + usurf->xadd, y + usurf->yadd);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_shell_configure: shell surface configure
 *
 * @param[in]   surface     Weston surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_shell_configure(struct weston_surface *surface)
{
    struct uifw_win_surface *usurf;
    int     buf_width;
    int     buf_height;

    uifw_trace("win_mgr_shell_configure: Enter(%08x)", (int)surface);

    /* Find UIFW surface        */
    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("win_mgr_shell_configure: Leave(Not Exist)");
        return;
    }

    usurf->client_width = surface->geometry.width;
    usurf->client_height = surface->geometry.height;
    buf_width = weston_surface_buffer_width(surface);
    buf_height = weston_surface_buffer_height(surface);
    uifw_trace("win_mgr_shell_configure: %08x client w/h=%d/%d buf=%d/%d",
               usurf->surfaceid,
               usurf->client_width, usurf->client_height, buf_width, buf_height);
    if (usurf->client_width > buf_width)    usurf->client_width = buf_width;
    if (usurf->client_height > buf_height)  usurf->client_height = buf_height;

    /* send event to manager    */
    win_mgr_change_surface(surface, -1, 0);

    uifw_trace("win_mgr_shell_configure: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_select_surface: select surface by Bottun/Touch
 *
 * @param[in]   surface     Weston surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_select_surface(struct weston_surface *surface)
{
    struct uifw_win_surface *usurf;

    uifw_trace("win_mgr_select_surface: Enter(%08x)", (int)surface);

    /* find surface         */
    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("win_mgr_select_surface: Leave(Not Exist)");
        return;
    }
    if (usurf != _ico_win_mgr->active_pointer_usurf)  {

        /* send active event to manager     */
        if (ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                    usurf, ICO_WINDOW_MGR_ACTIVE_SELECTED, 0,0,0,0) <= 0) {
            uifw_trace("win_mgr_select_surface: not found manager, set active");
            win_mgr_set_active(usurf, ICO_WINDOW_MGR_ACTIVE_POINTER |
                                        ICO_WINDOW_MGR_ACTIVE_KEYBOARD);
        }
    }
    uifw_trace("win_mgr_select_surface: Leave(OK)");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_set_title: set tile name to surface
 *
 * @param[in]   surface     weston surface
 * @param[in]   title       title name
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_set_title(struct weston_surface *surface, const char *title)
{
    struct uifw_win_surface *usurf;

    uifw_trace("win_mgr_set_title: Enter(%08x) name=%s", (int)surface, title);

    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("win_mgr_set_title: Leave(Not Exist)");
        return;
    }
    if (((usurf->width > 0) && (usurf->height > 0)) &&
        ((usurf->created == 0) ||
         (strncmp(title, usurf->winname, ICO_IVI_WINNAME_LENGTH-1) != 0)))    {
        strncpy(usurf->winname, title, ICO_IVI_WINNAME_LENGTH-1);
        usurf->winname[ICO_IVI_WINNAME_LENGTH-1] = 0;
        if (usurf->created == 0)    {
            ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_CREATED, usurf, 0,0,0,0,0);
            usurf->created = 1;
        }
        else    {
            ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_NAME, usurf, 0,0,0,0,0);
        }
    }
    else    {
        strncpy(usurf->winname, title, ICO_IVI_WINNAME_LENGTH-1);
        usurf->winname[ICO_IVI_WINNAME_LENGTH-1] = 0;
    }
    uifw_trace("win_mgr_set_title: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_surface_move: set tile name to surface
 *
 * @param[in]   surface     weston surface
 * @param[in]   title       title name
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_surface_move(struct weston_surface *surface, int *dx, int *dy)
{
    struct uifw_win_surface *usurf;

    uifw_trace("win_mgr_surface_move: Enter(%08x) x/y=%d,%d", (int)surface, *dx, *dy);

    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("win_mgr_surface_move: Leave(Not Exist)");
        return;
    }
    if (usurf->visible) {
        *dx = usurf->node_tbl->disp_x + usurf->x;
        *dy = usurf->node_tbl->disp_y + usurf->y;
    }
    else    {
        *dx = ICO_IVI_MAX_COORDINATE+1;
        *dy = ICO_IVI_MAX_COORDINATE+1;
    }
    uifw_trace("win_mgr_surface_move: Leave(change to x/y=%d/%d)", *dx, *dy);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_set_visible: change surface visibility
 *
 * @param[in]   usurf       UIFW surface
 * @param[in]   visible     visible(=1)/unvisible(0)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_window_mgr_set_visible(struct uifw_win_surface *usurf, const int visible)
{
    if (visible)    {
        if (usurf->visible == 0)    {
            uifw_trace("ico_window_mgr_set_visible: Chagne to Visible(%08x)", (int)usurf);
            usurf->visible = 1;
            /* change unvisible to visible, restack surface list    */
            ico_window_mgr_restack_layer(usurf, 0);
        }
    }
    else    {
        if (usurf->visible != 0)    {
            uifw_trace("ico_window_mgr_set_visible: Chagne to Unvisible(%08x)", (int)usurf);
            usurf->visible = 0;
            /* change visible to unvisible, restack surface list    */
            ico_window_mgr_restack_layer(usurf, 0);
        }
    }
    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_VISIBLE,
                            usurf, usurf->visible, usurf->raise, 0, 0,0);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_set_raise: change surface raise/lower
 *
 * @param[in]   usurf       UIFW surface
 * @param[in]   raise       raise(=1)/lower(0)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_set_raise(struct uifw_win_surface *usurf, const int raise)
{
    uifw_trace("win_mgr_set_raise: Enter(%08x,%d) layer=%x",
               (int)usurf, raise, (int)usurf->win_layer->layer);

    wl_list_remove(&usurf->ivi_layer);
    if (raise)  {
        /* raise ... surface stack to top of layer          */
        wl_list_insert(&usurf->win_layer->surface_list, &usurf->ivi_layer);
        usurf->raise = 1;
        uifw_trace("win_mgr_set_raise: Raise Link to Top");
    }
    else    {
        /* Lower ... surface stack to bottom of layer       */
        wl_list_insert(usurf->win_layer->surface_list.prev, &usurf->ivi_layer);
        usurf->raise = 0;
        uifw_trace("win_mgr_set_raise: Lower Link to Bottom");
    }

    /* rebild compositor surface list               */
    if (usurf->visible) {
        ico_window_mgr_restack_layer(usurf, 0);
    }
    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_VISIBLE,
                            usurf, usurf->visible, usurf->raise, 0, 0,0);

    uifw_trace("win_mgr_set_raise: Leave");
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

    /* unmap surface                */
    if (&usurf->surf_map != usurf->surf_map.next)   {
        uifw_unmap_surface(NULL, NULL, usurf->surfaceid);
    }

    /* destroy active surface       */
    if (usurf == _ico_win_mgr->active_pointer_usurf)  {
        _ico_win_mgr->active_pointer_usurf = NULL;
    }
    if (usurf == _ico_win_mgr->active_keyboard_usurf) {
        _ico_win_mgr->active_keyboard_usurf = NULL;
    }

    /* destroy animation extenson   */
    if (win_mgr_hook_animation) {
        (*win_mgr_hook_animation)(ICO_WINDOW_MGR_ANIMATION_DESTROY, (void *)usurf);
    }

    /* delete from layer list       */
    wl_list_remove(&usurf->ivi_layer);
    ico_window_mgr_restack_layer(NULL, 0);

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

    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_DESTROYED,
                           usurf, 0,0,0,0,0);

    hash = usurf->surfaceid & SURCAFE_ID_MASK;
    _ico_win_mgr->surfaceid_map[(hash - 1)/16] &= ~(1 << ((hash - 1) % 16));

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
    uclient = find_client_from_client(client);
    if (! uclient)  {
        win_mgr_bind_client(client, NULL);
        uclient = find_client_from_client(client);
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
    _ico_win_mgr->num_manager = 0;
    wl_list_for_each_safe (mgr, itmp, &_ico_win_mgr->manager_list, link)    {
        if (mgr->resource == resource) {
            wl_list_remove(&mgr->link);
            free(mgr);
        }
        else    {
            if (mgr->manager)   {
                _ico_win_mgr->num_manager++;
            }
        }
    }
    uifw_trace("unbind_ico_win_mgr: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_win_mgr_send_to_mgr: send event to manager(HomeScreen)
 *
 * @param[in]   event       event code(if -1, not send event)
 * @param[in]   usurf       UIFW surface table
 * @param[in]   param1      parameter 1
 * @param[in]      :             :
 * @param[in]   param5      parameter 5
 * @return      number of managers
 * @retval      > 0         number of managers
 * @retval      0           manager not exist
 */
/*--------------------------------------------------------------------------*/
static int
ico_win_mgr_send_to_mgr(const int event, struct uifw_win_surface *usurf,
                        const int param1, const int param2, const int param3,
                        const int param4, const int param5)
{
    int     num_mgr = 0;
    struct uifw_manager* mgr;

    /* if appid not fix, check and fix appid    */
    if ((usurf != NULL) &&
        (usurf->uclient->fixed_appid < ICO_WINDOW_MGR_APPID_FIXCOUNT)) {
        win_mgr_get_client_appid(usurf->uclient);
    }

    /* send created if not send created event   */
    if ((usurf != NULL) && (usurf->created == 0) &&
        (((usurf->width > 0) && (usurf->height > 0)) ||
         ((event != ICO_WINDOW_MGR_WINDOW_CREATED) &&
          (event != ICO_WINDOW_MGR_WINDOW_NAME))))  {
        wl_list_for_each (mgr, &_ico_win_mgr->manager_list, link)   {
            if (mgr->manager)   {
                uifw_trace("ico_win_mgr_send_to_mgr: Send Manager(%08x) WINDOW_CREATED"
                           "(surf=%08x,name=%s,pid=%d,appid=%s)", (int)mgr->resource,
                           usurf->surfaceid, usurf->winname, usurf->uclient->pid,
                           usurf->uclient->appid);
                ico_window_mgr_send_window_created(mgr->resource, usurf->surfaceid,
                                                   usurf->winname, usurf->uclient->pid,
                                                   usurf->uclient->appid);
            }
        }
        usurf->created = 1;
    }

    wl_list_for_each (mgr, &_ico_win_mgr->manager_list, link)   {
        if (mgr->manager)   {
            num_mgr ++;

            switch(event)   {
            case ICO_WINDOW_MGR_WINDOW_CREATED:
                /* Do nothing anymore because sended window create event    */
                break;

            case ICO_WINDOW_MGR_WINDOW_NAME:
                uifw_trace("ico_win_mgr_send_to_mgr: Send Manager(%08x) WINDOW_NAME"
                           "(surf=%08x,name=%s)", (int)mgr->resource,
                           usurf->surfaceid, usurf->winname);
                ico_window_mgr_send_window_name(mgr->resource, usurf->surfaceid,
                                                usurf->winname);
                break;

            case ICO_WINDOW_MGR_WINDOW_DESTROYED:
                uifw_trace("ico_win_mgr_send_to_mgr: Send Manager(%08x) DESTROYED"
                           "(surf=%08x)", (int)mgr->resource, usurf->surfaceid);
                ico_window_mgr_send_window_destroyed(mgr->resource, usurf->surfaceid);
                break;

            case ICO_WINDOW_MGR_WINDOW_VISIBLE:
                uifw_trace("ico_win_mgr_send_to_mgr: Send Manager(%08x) VISIBLE"
                           "(surf=%08x,vis=%d,raise=%d,hint=%d)",
                           (int)mgr->resource, usurf->surfaceid, param1, param2, param3);
                ico_window_mgr_send_window_visible(mgr->resource,
                                                   usurf->surfaceid, param1, param2, param3);
                break;

            case ICO_WINDOW_MGR_WINDOW_CONFIGURE:
                uifw_trace("ico_win_mgr_send_to_mgr: Send Manager(%08x) CONFIGURE"
                           "(surf=%08x,app=%s,node=%x,layer=%x,x/y=%d/%d,w/h=%d/%d,hint=%d)",
                           (int)mgr->resource, usurf->surfaceid, usurf->uclient->appid,
                           usurf->node_tbl->node, usurf->win_layer->layer,
                           param1, param2, param3, param4, param5);
                ico_window_mgr_send_window_configure(mgr->resource, usurf->surfaceid,
                                                     usurf->node_tbl->node,
                                                     usurf->win_layer->layer,
                                                     param1, param2, param3, param4, param5);
                break;

            case ICO_WINDOW_MGR_WINDOW_ACTIVE:
                uifw_trace("ico_win_mgr_send_to_mgr: Send Manager(%08x) ACTIVE"
                           "(surf=%08x,active=%d)", (int)mgr->resource, usurf->surfaceid,
                           param1);
                ico_window_mgr_send_window_active(mgr->resource, usurf->surfaceid,
                                                  (uint32_t)param1);
                break;

            case ICO_WINDOW_MGR_LAYER_VISIBLE:
                uifw_trace("ico_win_mgr_send_to_mgr: Send Manager(%08x) LAYER_VISIBLE"
                           "(layer=%x,visivle=%d)", (int)mgr->resource, param1, param2);
                ico_window_mgr_send_layer_visible(mgr->resource, (uint32_t)param1, param2);
                break;

            default:
                uifw_error("ico_win_mgr_send_to_mgr: Illegal event(%08x)", event);
                break;
            }
        }
    }
    return num_mgr;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_set_scale: set surface transform scale
 *
 * @param[in]   usurf       UIFW surface
 * @return      chagne display
 * @retval      =1          change display
 * @retval      =0          no change
 */
/*--------------------------------------------------------------------------*/
static int
win_mgr_set_scale(struct uifw_win_surface *usurf)
{
    struct weston_surface   *es;
    float   scalex;
    float   scaley;
    int     ret = 0;

    es = usurf->surface;
    if ((es != NULL) && (es->buffer_ref.buffer))  {
        if (usurf->client_width == 0)   usurf->client_width = es->geometry.width;
        if (usurf->client_height == 0)  usurf->client_height = es->geometry.height;
        scalex = (float)usurf->width / (float)usurf->client_width;
        scaley = (float)usurf->height / (float)usurf->client_height;
        uifw_debug("win_mgr_set_scale: %08x X=%4.2f(%d/%d) Y=%4.2f(%d/%d)",
                   usurf->surfaceid, scalex, usurf->width, usurf->client_width,
                   scaley, usurf->height, usurf->client_height);
        usurf->xadd = 0;
        usurf->yadd = 0;
        if (usurf->attributes & ICO_WINDOW_MGR_ATTR_FIXED_ASPECT)   {
            if (scalex > scaley)    {
                scalex = scaley;
                if ((usurf->attributes & ICO_WINDOW_MGR_ATTR_ALIGN_LEFT) == 0)  {
                    usurf->xadd = (float)usurf->width - ((float)usurf->client_width * scalex);
                    if ((usurf->attributes & ICO_WINDOW_MGR_ATTR_ALIGN_RIGHT) == 0) {
                        usurf->xadd /= 2;
                    }
                }
            }
            else if (scalex < scaley)   {
                scaley = scalex;
                if ((usurf->attributes & ICO_WINDOW_MGR_ATTR_ALIGN_TOP) == 0)   {
                    usurf->yadd = (float)usurf->height - ((float)usurf->client_height * scaley);
                    if ((usurf->attributes & ICO_WINDOW_MGR_ATTR_ALIGN_BOTTOM) == 0)    {
                        usurf->yadd /= 2;
                    }
                }
            }
            uifw_trace("win_mgr_set_scale: %08x fixed aspect x/yadd=%d/%d",
                       usurf->surfaceid, usurf->xadd, usurf->yadd);
        }
        if ((scalex != usurf->scalex) || (scaley != usurf->scaley)) {
            usurf->scalex = scalex;
            usurf->scaley = scaley;
            if ((scalex != 1.0f) || (scaley != 1.0f))   {
                weston_matrix_init(&usurf->transform.matrix);
                weston_matrix_scale(&usurf->transform.matrix, scalex, scaley, 1.0f);
                uifw_trace("win_mgr_set_scale: change scale(%d)", usurf->set_transform);
                if (usurf->set_transform == 0)  {
                    usurf->set_transform = 1;
                    wl_list_init(&usurf->transform.link);
                    wl_list_insert(&es->geometry.transformation_list, &usurf->transform.link);
                }
            }
            else if (usurf->set_transform != 0) {
                uifw_trace("win_mgr_set_scale: reset transform");
                usurf->set_transform = 0;
                wl_list_remove(&usurf->transform.link);
            }
            weston_surface_geometry_dirty(es);
            weston_surface_update_transform(es);
            weston_surface_damage_below(es);
            weston_surface_damage(es);
            ret = 1;
        }
    }
    return ret;
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
#if 0           /* too many debug log   */
    uifw_debug("ico_window_mgr_get_client_usurf: target=<%s> appid=<%s> win=<%s>",
               target, appid, winname);
#endif

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
    char    *p;
    struct weston_output *output;
    struct weston_config_section *section;
    char    *displayno = NULL;

    uifw_info("ico_window_mgr: Enter(module_init)");

    /* get ivi debug level                      */
    section = weston_config_get_section(ec->config, "ivi-debug", NULL, NULL);
    if (section)    {
        weston_config_section_get_int(section, "flag", &_ico_ivi_debug_flag, 0);
        weston_config_section_get_int(section, "log", &_ico_ivi_debug_level, 3);
    }

    /* get display number list                  */
    section = weston_config_get_section(ec->config, "ivi-display", NULL, NULL);
    if (section)    {
        weston_config_section_get_string(section, "displayno", &displayno, NULL);
    }

    /* get layer id                             */
    section = weston_config_get_section(ec->config, "ivi-layer", NULL, NULL);
    if (section)    {
        weston_config_section_get_int(section, "default", &_ico_ivi_default_layer, 1);
        weston_config_section_get_int(section, "startup", &_ico_ivi_startup_layer, 109);
        weston_config_section_get_int(section, "input", &_ico_ivi_input_layer, 101);
        weston_config_section_get_int(section, "cursor", &_ico_ivi_cursor_layer, 102);
        weston_config_section_get_int(section, "background", &_ico_ivi_background_layer, 0);
    }

    /* get animation default                    */
    section = weston_config_get_section(ec->config, "ivi-animation", NULL, NULL);
    if (section)    {
        weston_config_section_get_string(section, "default", &_ico_ivi_animation_name, NULL);
        weston_config_section_get_int(section, "time", &_ico_ivi_animation_time, 600);
        weston_config_section_get_int(section, "fps", &_ico_ivi_animation_fps, 15);
    }
    if (_ico_ivi_animation_time < 100)  _ico_ivi_animation_time = 600;
    if (_ico_ivi_animation_fps < 2)     _ico_ivi_animation_fps = 15;

    /* create ico_window_mgr management table   */
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
    uifw_debug("ico_window_mgr: table=%08x", (int)_ico_win_mgr);
    memset(_ico_win_mgr->surfaceid_map, 0, INIT_SURFACE_IDS/8);

    _ico_win_mgr->compositor = ec;

    _ico_win_mgr->surfaceid_max = INIT_SURFACE_IDS;
    _ico_win_mgr->surfaceid_count = INIT_SURFACE_IDS;

    uifw_trace("ico_window_mgr: wl_global_create(bind_ico_win_mgr)");
    if (wl_global_create(ec->wl_display, &ico_window_mgr_interface, 1,
                         _ico_win_mgr, bind_ico_win_mgr) == NULL)  {
        uifw_error("ico_window_mgr: Error(wl_global_create)");
        return -1;
    }

    wl_list_init(&_ico_win_mgr->client_list);
    wl_list_init(&_ico_win_mgr->manager_list);
    wl_list_init(&_ico_win_mgr->ivi_layer_list);
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
    wl_list_for_each(output, &ec->output_list, link) {
        wl_list_init(&_ico_win_mgr->map_animation[_ico_num_nodes].link);
        _ico_win_mgr->map_animation[_ico_num_nodes].frame = win_mgr_check_surfacemap;
        wl_list_insert(output->animation_list.prev,
                       &_ico_win_mgr->map_animation[_ico_num_nodes].link);
        _ico_num_nodes++;
        if (_ico_num_nodes >= ICO_IVI_MAX_DISPLAY)   break;
    }
    memset(&_ico_node_table[0], 0, sizeof(_ico_node_table));
    i = 0;
    wl_list_for_each(output, &ec->output_list, link) {
        p = strtok(p, ",");
        if (p)  {
            idx = strtol(p, (char **)0, 0);
            uifw_trace("ico_window_mgr: config Display.%d is %d", i, idx);
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
        uifw_trace("ico_window_mgr: Display.%d no=%d x/y=%d/%d w/h=%d/%d",
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

    /* Hook to IVI-Shell                            */
    ico_ivi_shell_hook_bind(win_mgr_bind_client);
    ico_ivi_shell_hook_unbind(win_mgr_unbind_client);
    ico_ivi_shell_hook_create(win_mgr_register_surface);
    ico_ivi_shell_hook_destroy(win_mgr_destroy_surface);
    ico_ivi_shell_hook_map(win_mgr_map_surface);
    ico_ivi_shell_hook_configure(win_mgr_shell_configure);
    ico_ivi_shell_hook_select(win_mgr_select_surface);
    ico_ivi_shell_hook_title(win_mgr_set_title);
    ico_ivi_shell_hook_move(win_mgr_surface_move);

    uifw_info("ico_window_mgr: animation name=%s time=%d fps=%d",
              _ico_ivi_animation_name, _ico_ivi_animation_time, _ico_ivi_animation_fps);
    uifw_info("ico_window_mgr: layer default=%d startup=%d background=%d",
              _ico_ivi_default_layer, _ico_ivi_startup_layer, _ico_ivi_background_layer);
    uifw_info("ico_window_mgr: layer input=%d cursor=%d",
              _ico_ivi_input_layer, _ico_ivi_cursor_layer);
    uifw_info("ico_window_mgr: debug flag=%x log level=%d",
              _ico_ivi_debug_flag, _ico_ivi_debug_level);

    uifw_info("ico_window_mgr: Leave(module_init)");

    return 0;
}

