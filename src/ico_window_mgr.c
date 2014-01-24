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
#include <GLES2/gl2ext.h>
#include <GL/internal/dri_interface.h>

#include <weston/compositor.h>
#include <libdrm/intel_bufmgr.h>

/* detail debug log */
#define UIFW_DETAIL_OUT 0                   /* 1=detail debug log/0=no detail log   */

#include "ico_ivi_common_private.h"
#include "ico_ivi_shell_private.h"
#include "ico_window_mgr_private.h"
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

/* GPU type code                        */
#define ICO_GPUTYPE_NOACCELERATION      0   /* Do not use GPU-dependent acceleration*/
#define ICO_GPUTYPE_INTEL_SANDYBRIDGE   1   /* Intel Sandybridge Mobile             */

/* Code for Intel Sandybridge Mobile GPU dependent acceleration */

/* wl_drm_buffer (inport from mesa-9.1.3 & mesa-9.2.1/          */
/*                src/egl/wayland/wayland-drm/wayland-drm.h)    */
struct uifw_drm_buffer {
    struct wl_resource *resource;
    void *drm;                  /* struct wl_drm    */
    int32_t width, height;
    uint32_t format;
    const void *driver_format;
    int32_t offset[3];
    int32_t stride[3];
    void *driver_buffer;
};

/* __DRIimage (inport from mesa-9.1.3/src/mesa/drivers/dri/intel/intel_regions.h    */
/*                         mesa-9.2.1/src/mesa/drivers/dri/i915/intel_regions.h     */
/*                         mesa-9.2.1/src/mesa/drivers/dri/i965/intel_regions.h)    */
struct uifw_intel_region    {   /* struct intel_region for mesa 9.2.1   */
   void *bo;                /**< buffer manager's buffer */
   uint32_t refcount;       /**< Reference count for region */
   uint32_t cpp;            /**< bytes per pixel */
   uint32_t width;          /**< in pixels */
   uint32_t height;         /**< in pixels */
   uint32_t pitch;          /**< in bytes */
   uint32_t tiling;         /**< Which tiling mode the region is in */
   uint32_t name;           /**< Global name for the bo */
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
   uint32_t width;
   uint32_t height;
   uint32_t tile_x;
   uint32_t tile_y;
   bool has_depthstencil;           /* i965 only    */
   void *data;
};

/* gl_surface_state (inport from weston-1.2.1/src/gl-renderer.c,    */
/*                               weston-1.3.1/src/gl-renderer.c     */
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

/* API access control configuration     */
struct ico_api_permit    {
    struct ico_api_permit       *next;      /* access control configuration list    */
    struct ico_uifw_api_permit  permit;     /* permission                           */
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
    struct uifw_win_layer *touch_layer;     /* layer table for touch panel layer    */

    struct wl_list  map_list;               /* surface map list                     */
    struct uifw_surface_map *free_maptable; /* free maped surface table list        */
    struct weston_animation map_animation[ICO_IVI_MAX_DISPLAY];
                                            /* animation for map check              */
    struct wl_event_source  *wait_mapevent; /* map event send wait timer            */

    struct uifw_win_surface *active_pointer_usurf;  /* Active Pointer Surface       */
    struct uifw_win_surface *active_keyboard_usurf; /* Active Keyboard Surface      */

    struct uifw_win_surface *idhash[UIFW_HASH];  /* UIFW SerfaceID                  */
    struct uifw_win_surface *wshash[UIFW_HASH];  /* Weston Surface                  */

    uint32_t surfaceid_count;               /* Number of surface id                 */
    uint32_t surfaceid_max;                 /* Maximum number of surface id         */
    uint16_t *surfaceid_map;                /* SurfaceId assign bit map             */

    struct ico_api_permit   *access_config; /* access control configuration list    */
    uint32_t default_access;                /* default permit APIs                  */

    char    shell_init;                     /* shell initialize flag                */
    char    res[3];                         /* (unused)                             */
};

/* access control api table             */
static const struct {
    char        name[24];                   /* API name                             */
    uint32_t    api;                        /* API flag                             */
} access_api_table[] = {
    { "declare_manager", ICO_UIFW_WINDOW_MGR_DECLARE_MANAGER },
    { "set_window_layer", ICO_UIFW_WINDOW_MGR_SET_WINDOW_LAYER },
    { "set_positionsize", ICO_UIFW_WINDOW_MGR_SET_POSITIONSIZE },
    { "set_visible", ICO_UIFW_WINDOW_MGR_SET_VISIBLE },
    { "set_animation", ICO_UIFW_WINDOW_MGR_SET_ANIMATION },
    { "set_attributes", ICO_UIFW_WINDOW_MGR_SET_ATTRIBUTES },
    { "visible_animation", ICO_UIFW_WINDOW_MGR_VISIBLE_ANIMATION },
    { "set_active", ICO_UIFW_WINDOW_MGR_SET_ACTIVE },
    { "set_layer_visible", ICO_UIFW_WINDOW_MGR_SET_LAYER_VISIBLE },
    { "get_surfaces", ICO_UIFW_WINDOW_MGR_GET_SURFACES },
    { "set_map_buffer", ICO_UIFW_WINDOW_MGR_SET_MAP_BUFFER },
    { "map_surface", ICO_UIFW_WINDOW_MGR_MAP_SURFACE },
    { "unmap_surface", ICO_UIFW_WINDOW_MGR_UNMAP_SURFACE },

    { "add_input_app", ICO_UIFW_INPUT_MGR_CONTROL_ADD_INPUT_APP },
    { "del_input_app", ICO_UIFW_INPUT_MGR_CONTROL_DEL_INPUT_APP },
    { "send_input_event", ICO_UIFW_INPUT_MGR_CONTROL_SEND_INPUT_EVENT },
    { "set_input_region", ICO_UIFW_EXINPUT_SET_INPUT_REGION },
    { "unset_input_region", ICO_UIFW_EXINPUT_UNSET_INPUT_REGION },
    { "confiure_input", ICO_UIFW_INPUT_MGR_DEVICE_CONFIGURE_INPUT },
    { "confiure_code", ICO_UIFW_INPUT_MGR_DEVICE_CONFIGURE_CODE },
    { "input_event", ICO_UIFW_INPUT_MGR_DEVICE_INPUT_EVENT },

    { "\0", 0 }
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
                                            /* assign new surface id                */
static uint32_t generate_id(void);
                                            /* bind shell client                    */
static void win_mgr_bind_client(struct wl_client *client, void *shell);
                                            /* unind shell client                   */
static void win_mgr_unbind_client(struct wl_client *client);
#if 0       /* work around: Walk through child processes until app ID is found  */
                                            /* get parent pid                       */
static pid_t win_mgr_get_ppid(pid_t pid);
#endif      /* work around: Walk through child processes until app ID is found  */
                                            /* get appid from pid                   */
static void win_mgr_get_client_appid(struct uifw_client *uclient);
                                            /* create new surface                   */
static void win_mgr_register_surface(
                    int layertype, struct wl_client *client, struct wl_resource *resource,
                    struct weston_surface *surface, struct shell_surface *shsurf);
                                            /* surface destroy                      */
static void win_mgr_destroy_surface(struct weston_surface *surface);
                                            /* map new surface                      */
static void win_mgr_surface_map(struct weston_surface *surface, int32_t *width,
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
static char *win_mgr_set_title(struct weston_surface *surface, const char *title);
                                            /* surface move request from shell      */
static void win_mgr_surface_move(struct weston_surface *surface, int *dx, int *dy);
                                            /* shell layer visible control          */
static void win_mgr_show_layer(int layertype, int show, void *data);
                                            /* shell full screen surface control    */
static int win_mgr_fullscreen(int event, struct weston_surface *surface);
                                            /* set raise                            */
static void win_mgr_set_raise(struct uifw_win_surface *usurf, const int raise);
                                            /* surface change from manager          */
static int win_mgr_surface_change_mgr(struct weston_surface *surface, const int x,
                                      const int y, const int width, const int height);
                                            /* reset surface focus                  */
static void win_mgr_reset_focus(struct uifw_win_surface *usurf);
                                            /* create new layer                     */
static struct uifw_win_layer *win_mgr_create_layer(struct uifw_win_surface *usurf,
                                                   const uint32_t layer, const int layertype);
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
                              const char *appid, int32_t pid);
                                            /* check and change all mapped surface  */
static void win_mgr_check_mapsurface(struct weston_animation *animation,
                                     struct weston_output *output, uint32_t msecs);
                                            /* check timer of mapped surface        */
static int win_mgr_timer_mapsurface(void *data);
                                            /* check and change mapped surface      */
static void win_mgr_change_mapsurface(struct uifw_surface_map *sm, int event,
                                      uint32_t curtime);
                                            /* set map buffer                       */
static void uifw_set_map_buffer(struct wl_client *client, struct wl_resource *resource,
                                const char *shmname, uint32_t bufsize, uint32_t bufnum);
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
    uifw_set_map_buffer,
    uifw_map_surface,
    uifw_unmap_surface
};


/* GPU driver name          */
static const struct _ico_gputype_table  {
    int     gpu_type;                           /* GPU type code                    */
    char    *gpu_name;                          /* GPU driver name                  */
}   ico_window_mgr_gpu_type[] = {
    { ICO_GPUTYPE_INTEL_SANDYBRIDGE, "Mesa DRI Intel(R) Sandybridge Mobile" },
    { 0, "\0" }
};

/* plugin common value(without ico_plugin_loader)   */
static int  _ico_ivi_option_flag = 0;           /* option flags                     */
static int  _ico_ivi_debug_level = 3;           /* debug Level                      */
static char *_ico_ivi_animation_name = NULL;    /* default animation name           */
static int  _ico_ivi_animation_time = 500;      /* default animation time           */
static int  _ico_ivi_animation_fps = 30;        /* animation frame rate             */
static char *_ico_ivi_inputpanel_animation = NULL; /* default animation name for input panel*/
static int  _ico_ivi_inputpanel_anima_time = 0; /* default animation time for input panel*/

static int  _ico_ivi_inputpanel_display = 0;    /* input panel display number       */
static int  _ico_ivi_inputdeco_mag = 100;       /* input panel magnification rate(%)*/
static int  _ico_ivi_inputdeco_diff = 0;        /* input panel difference from the bottom*/

static int  _ico_ivi_background_layer = 0;      /* background layer                 */
static int  _ico_ivi_default_layer = 1;         /* deafult layer id at surface create*/
static int  _ico_ivi_touch_layer = 101;         /* touch panel layer id             */
static int  _ico_ivi_cursor_layer = 102;        /* cursor layer id                  */
static int  _ico_ivi_startup_layer = 109;       /* deafult layer id at system startup*/

static int  _ico_ivi_gpu_type = 0;              /* GPU type for get EGL buffer      */
static int  _ico_ivi_map_framerate_gpu = 30;    /* framerate of map surface for GPU*/
static int  _ico_ivi_map_framerate_shm = 2;     /* framerate of map surface for shm buffer*/
static int  _ico_ivi_map_framerate_pixel = 5;   /* framerate of map surface for ReadPixels*/

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
        if ((usurf->width > buf_width) && (usurf->scalex <= 1.0f))  {
            width = buf_width;
            x += (usurf->width - buf_width)/2;
        }
        if ((usurf->height > buf_height) && (usurf->scaley <= 1.0f))    {
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
        uclient = ico_window_mgr_find_uclient(client);
        if (uclient)    {
            if (&uclient->surface_link != uclient->surface_link.next)   {
                usurf = container_of (uclient->surface_link.next,
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
    struct ico_api_permit   *accp;
    char    subject[ICO_UIFW_MAX_SUBJECT_NAME];
    int     fd;
    int     i;

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
        uclient->api_access_control = _ico_win_mgr->default_access;
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

        /* get SMACK subject label  */
        if (! _ico_win_mgr->access_config)  {
            /* no access control, permit all APIs   */
            uclient->api_access_control = 0xffffffff;
        }
        else    {
            sprintf(subject, "/proc/%d/attr/current", (int)pid);
            fd = open(subject, O_RDONLY);
            if (fd < 0) {
                uifw_warn("win_mgr_bind_client: pid=%d(%s) not exist current",
                          pid, uclient->appid);
            }
            else    {
                i = read(fd, subject, sizeof(subject)-1);
                if (i > 0)  {
                    subject[i] = 0;
                    accp = _ico_win_mgr->access_config;
                    while (accp)    {
                        if (strcmp(subject, accp->permit.subject) == 0) {
                            uclient->api_access_control |= accp->permit.api_permit;
                            break;
                        }
                        accp = accp->next;
                    }
                }
                else    {
                    i = errno;
                    uifw_trace("win_mgr_bind_client: pid=%d(%s) current read error<%d>",
                               pid, uclient->appid, i);
                    if (i == EINVAL)    {
                        /* SMACK not active     */
                        uclient->api_access_control = 0xffffffff;
                    }
                }
                close(fd);
            }
        }
        uifw_trace("win_mgr_bind_client: pid=%d(%s) access_control=%08x",
                   pid, uclient->appid, uclient->api_access_control);

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

    uclient = ico_window_mgr_find_uclient(client);
    if (uclient)    {
        /* Client exist, Destory client management table             */
        wl_list_remove(&uclient->link);
        free(uclient);
    }
    uifw_trace("win_mgr_unbind_client: Leave");
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
 * @brief   win_mgr_register_surface: create UIFW surface
 *
 * @param[in]   layertype       surface layer type
 * @param[in]   client          Wayland client
 * @param[in]   resource        client resource
 * @param[in]   surface         Weston surface
 * @param[in]   shsurf          shell surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_register_surface(int layertype, struct wl_client *client,
                         struct wl_resource *resource, struct weston_surface *surface,
                         struct shell_surface *shsurf)
{
    struct uifw_win_surface *usurf;
    struct uifw_win_surface *phash;
    struct uifw_win_surface *bhash;
    int         layer;
    uint32_t    hash;

    uifw_trace("win_mgr_register_surface: Enter(surf=%08x,client=%08x,res=%08x,layertype=%x)",
               (int)surface, (int)client, (int)resource, layertype);

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
    usurf->layertype = layertype;
    usurf->node_tbl = &_ico_node_table[0];  /* set default node table (display no=0)    */
    wl_list_init(&usurf->ivi_layer);
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
    if (layertype == LAYER_TYPE_INPUTPANEL) {
        usurf->attributes = ICO_WINDOW_MGR_ATTR_FIXED_ASPECT;
        usurf->animation.hide_anima = ico_get_animation_name(_ico_ivi_inputpanel_animation);
        usurf->animation.hide_time = _ico_ivi_inputpanel_anima_time;
        usurf->animation.show_anima = usurf->animation.hide_anima;
        usurf->animation.show_time = usurf->animation.hide_time;
    }
    if ((layertype != LAYER_TYPE_INPUTPANEL) &&
        ((_ico_win_mgr->num_manager <= 0) ||
         (ico_ivi_optionflag() & ICO_IVI_OPTION_SHOW_SURFACE))) {
        uifw_trace("win_mgr_register_surface: No Manager, Force visible");
        usurf->visible = 1;
    }
    else    {
        uifw_trace("win_mgr_register_surface: Manager exist, Not visible");
        usurf->visible = 0;
    }

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

    /* set default layer id             */
    switch (layertype)  {
    case LAYER_TYPE_BACKGROUND:
        layer = _ico_ivi_background_layer;
        break;
    case LAYER_TYPE_TOUCH:
        layer = _ico_ivi_touch_layer;
        break;
    case LAYER_TYPE_CURSOR:
        layer = _ico_ivi_cursor_layer;
        break;
    default:
        if ((_ico_win_mgr->num_manager > 0) ||
            (layertype == LAYER_TYPE_INPUTPANEL))   {
            layer = _ico_ivi_default_layer;
        }
        else    {
            layer = _ico_ivi_startup_layer;
        }
        break;
    }
    win_mgr_set_layer(usurf, layer);

    uifw_trace("win_mgr_register_surface: Leave(surfaceId=%08x)", usurf->surfaceid);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_surface_map: map surface to display
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
win_mgr_surface_map(struct weston_surface *surface, int32_t *width, int32_t *height,
                    int32_t *sx, int32_t *sy)
{
    struct uifw_win_surface *usurf;

    uifw_trace("win_mgr_surface_map: Enter(%08x, x/y=%d/%d w/h=%d/%d)",
               (int)surface, *sx, *sy, *width, *height);

    usurf = find_uifw_win_surface_by_ws(surface);

    if (usurf) {
        uifw_trace("win_mgr_surface_map: surf=%08x w/h=%d/%d vis=%d",
                   usurf->surfaceid, usurf->width, usurf->height, usurf->visible);
        if ((usurf->width > 0) && (usurf->height > 0)) {
            uifw_trace("win_mgr_surface_map: HomeScreen registed, PositionSize"
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
            uifw_trace("win_mgr_surface_map: HomeScreen not regist Surface, "
                       "Change PositionSize(surf=%08x x/y=%d/%d w/h=%d/%d)",
                       usurf->surfaceid, *sx, *sy, *width, *height);
            usurf->width = *width;
            usurf->height = *height;
            usurf->x = *sx;
            usurf->y = *sy;
            if (usurf->x < 0)   usurf->x = 0;
            if (usurf->y < 0)   usurf->y = 0;
            if (usurf->layertype == LAYER_TYPE_INPUTPANEL)  {
                /* set position */
                usurf->node_tbl = &_ico_node_table[_ico_ivi_inputpanel_display];

                usurf->width = (float)usurf->surface->geometry.width
                               * (float)_ico_ivi_inputdeco_mag / 100.0f;
                usurf->height = (float)usurf->surface->geometry.height
                                * (float)_ico_ivi_inputdeco_mag / 100.0f;

                if ((usurf->width > (usurf->node_tbl->disp_width - 16)) ||
                    (usurf->height > (usurf->node_tbl->disp_height - 16)))  {
                    usurf->x = (usurf->node_tbl->disp_width
                               - usurf->surface->geometry.width) / 2;
                    usurf->y = usurf->node_tbl->disp_height
                               - usurf->surface->geometry.height - 16
                               - _ico_ivi_inputdeco_diff;
                    if (usurf->x < 0)   usurf->x = 0;
                    if (usurf->y < 0)   usurf->y = 0;
                }
                else    {
                    win_mgr_set_scale(usurf);

                    usurf->x = (usurf->node_tbl->disp_width
                                - usurf->width) / 2;
                    usurf->y = usurf->node_tbl->disp_height
                               - usurf->height - 16 - _ico_ivi_inputdeco_diff;
                    if (usurf->x < 0)   usurf->x = 0;
                    if (usurf->y < 0)   usurf->y = 0;
                }
                uifw_trace("win_mgr_surface_map: set position %08x %d.%d/%d",
                           usurf->surfaceid, usurf->node_tbl->node, usurf->x, usurf->y);
            }
            if (((ico_ivi_optionflag() & ICO_IVI_OPTION_SHOW_SURFACE) == 0) &&
                (_ico_win_mgr->num_manager > 0))    {
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
                uifw_trace("win_mgr_surface_map: Change size/position x/y=%d/%d w/h=%d/%d",
                           (int)surface->geometry.x, (int)surface->geometry.y,
                           surface->geometry.width, surface->geometry.height);
            }
            else if (usurf->layertype != LAYER_TYPE_INPUTPANEL) {
                uifw_trace("win_mgr_surface_map: No HomeScreen, chaneg to Visible");
                ico_window_mgr_set_visible(usurf, 1);
            }
            else    {
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
            }
        }
        usurf->mapped = 1;
        if (usurf->visible) {
            ico_window_mgr_restack_layer(NULL);
        }
        uifw_trace("win_mgr_surface_map: Leave");
    }
    else    {
        uifw_trace("win_mgr_surface_map: Leave(No UIFW Surface)");
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_restack_layer: restack surface list
 *
 * @param[in]   usurf           UIFW surface (if NULL, no surface)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_window_mgr_restack_layer(struct uifw_win_surface *usurf)
{
    struct uifw_win_surface  *eu;
    struct uifw_win_layer *el;
    int32_t buf_width, buf_height;
    float   new_x, new_y;
    struct weston_layer *wlayer;
    struct weston_surface  *surface, *surfacetmp;
    int     num_visible = 0;
    int     layertype;

    /* make compositor surface list     */
    wlayer = ico_ivi_shell_weston_layer();

    uifw_trace("ico_window_mgr_restack_layer: Enter(surf=%08x) layer=%08x",
               (int)usurf, (int)wlayer);

    /* remove all surfaces in panel_layer   */
    wl_list_for_each_safe (surface, surfacetmp, &wlayer->surface_list, layer_link)   {
        wl_list_remove(&surface->layer_link);
        wl_list_init(&surface->layer_link);
    }
    wl_list_init(&wlayer->surface_list);

    wl_list_for_each (el, &_ico_win_mgr->ivi_layer_list, link)  {
        wl_list_for_each (eu, &el->surface_list, ivi_layer) {
            if (eu->surface == NULL)    continue;

            /* target only panel or unknown layer   */
            layertype = ico_ivi_shell_layertype(eu->surface);
            if ((layertype != LAYER_TYPE_PANEL) && (layertype != LAYER_TYPE_INPUTPANEL) &&
                (layertype != LAYER_TYPE_FULLSCREEN) && (layertype != LAYER_TYPE_CURSOR) &&
                (layertype != LAYER_TYPE_UNKNOWN))  {
                continue;
            }
            wl_list_remove(&eu->surface->layer_link);
            wl_list_init(&eu->surface->layer_link);

            if (eu->mapped != 0)    {
                if ((el->visible == FALSE) || (eu->visible == FALSE))   {
                    new_x = (float)(ICO_IVI_MAX_COORDINATE+1);
                    new_y = (float)(ICO_IVI_MAX_COORDINATE+1);
                }
                else if (eu->surface->buffer_ref.buffer)    {
                    buf_width = weston_surface_buffer_width(eu->surface);
                    buf_height = weston_surface_buffer_height(eu->surface);
                    if ((eu->width > buf_width) && (eu->scalex <= 1.0f))    {
                        new_x = (float)(eu->x +
                                (eu->width - eu->surface->geometry.width)/2);
                    }
                    else    {
                        new_x = (float)eu->x;
                    }
                    if ((eu->height > buf_height) && (eu->scaley <= 1.0f))  {
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
                uifw_debug("ico_window_mgr_restack_layer:%3d(%d).%08x(%08x:%d) "
                           "x/y=%d/%d w/h=%d/%d %x",
                           el->layer, el->visible, eu->surfaceid, (int)eu->surface,
                           eu->visible, (int)eu->surface->geometry.x,
                           (int)eu->surface->geometry.y, eu->surface->geometry.width,
                           eu->surface->geometry.height, eu->layertype);
            }
        }
    }

    /* damage(redraw) target surfacem if target exist   */
    if (usurf) {
        weston_surface_damage(usurf->surface);
    }

    /* composit and draw screen(plane)  */
    weston_compositor_schedule_repaint(_ico_win_mgr->compositor);

    if ((_ico_win_mgr->shell_init == 0) && (num_visible > 0) &&
        (_ico_win_mgr->shell != NULL) && (_ico_win_mgr->num_manager > 0))   {
        /* start shell fade         */
        _ico_win_mgr->shell_init = 1;
        ico_ivi_shell_startup(_ico_win_mgr->shell);
    }
    uifw_trace("ico_window_mgr_restack_layer: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_touch_layer: touch panel layer control
 *
 * @param[in]   omit        omit touch layer flag (TRUE=omit/FALSE=not omit)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_window_mgr_touch_layer(int omit)
{
    struct uifw_win_surface  *eu;

    /* check current touch layer mode   */
    if ((_ico_win_mgr->touch_layer == NULL) ||
        ((omit != FALSE) && (_ico_win_mgr->touch_layer->visible == FALSE))) {
        uifw_trace("ico_window_mgr_touch_layer: touch layer not exist or hide");
        return;
    }

    wl_list_for_each (eu, &_ico_win_mgr->touch_layer->surface_list, ivi_layer) {
        if ((eu->surface == NULL) || (eu->mapped == 0)) continue;
        if (omit != FALSE)  {
            eu->animation.pos_x = (int)eu->surface->geometry.x;
            eu->animation.pos_y = (int)eu->surface->geometry.y;
            eu->surface->geometry.x = (float)(ICO_IVI_MAX_COORDINATE+1);
            eu->surface->geometry.y = (float)(ICO_IVI_MAX_COORDINATE+1);
        }
        else    {
            eu->surface->geometry.x = (float)eu->animation.pos_x;
            eu->surface->geometry.y = (float)eu->animation.pos_y;
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_create_layer: create new layer
 *
 * @param[in]   usurf       UIFW surface, (if need)
 * @param[in]   layer       layer id
 * @param[in]   layertype   layer type if need
 * @return      new layer
 * @retval      != NULL     success(layer management table)
 * @retval      == NULL     error(No Memory)
 */
/*--------------------------------------------------------------------------*/
static struct uifw_win_layer *
win_mgr_create_layer(struct uifw_win_surface *usurf, const uint32_t layer,
                     const int layertype)
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
        new_el->layertype = LAYER_TYPE_BACKGROUND;
    }
    else if ((int)layer == _ico_ivi_touch_layer )   {
        new_el->layertype = LAYER_TYPE_TOUCH;
        _ico_win_mgr->touch_layer = new_el;
    }
    else if ((int)layer == _ico_ivi_cursor_layer )  {
        new_el->layertype = LAYER_TYPE_CURSOR;
    }
    else    {
        new_el->layertype = LAYER_TYPE_PANEL;
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
        if (new_el->layertype == LAYER_TYPE_CURSOR) {
            usurf->layertype = LAYER_TYPE_CURSOR;
        }
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
    uint32_t    oldlayer;

    uifw_trace("win_mgr_set_layer: Enter(%08x,%08x,%x)",
               usurf->surfaceid, (int)usurf->surface, layer);

    /* check if same layer                      */
    if (usurf->win_layer != NULL)   {
        oldlayer = usurf->win_layer->layer ;
        if (oldlayer == layer)  {
            uifw_trace("win_mgr_set_layer: Leave(Same Layer)");
            return;
        }
    }
    else    {
        oldlayer = 0;
    }

    /* search existing layer                    */
    wl_list_for_each (el, &_ico_win_mgr->ivi_layer_list, link) {
        if (el->layer == layer) break;
    }

    if (&el->link == &_ico_win_mgr->ivi_layer_list)    {
        /* layer not exist, create new layer    */
        uifw_trace("win_mgr_set_layer: New Layer %d(%d)", layer, usurf->layertype);
        new_el = win_mgr_create_layer(usurf, layer, usurf->layertype);
        if (! new_el)   {
            uifw_trace("win_mgr_set_layer: Leave(No Memory)");
            return;
        }
    }
    else    {
        uifw_trace("win_mgr_set_layer: Add surface to Layer %d", layer);
        usurf->win_layer = el;
        win_mgr_set_raise(usurf, 3);
    }

    /* set input region                     */
    if (layer == (uint32_t)_ico_ivi_cursor_layer)   {
        uifw_trace("win_mgr_set_layer: %08x cursor Change to cursor layer (%d,%d)-(%d,%d)",
                   usurf->surfaceid,
                   usurf->surface->input.extents.x1, usurf->surface->input.extents.y1,
                   usurf->surface->input.extents.x2, usurf->surface->input.extents.y2);
        pixman_region32_fini(&usurf->surface->pending.input);
        pixman_region32_init_rect(&usurf->surface->pending.input,
                                  ICO_IVI_MAX_COORDINATE + 1, ICO_IVI_MAX_COORDINATE + 1,
                                  1, 1);
    }
    else if (oldlayer == (uint32_t)_ico_ivi_cursor_layer)   {
        uifw_trace("win_mgr_set_layer: %08x cursor Change to normal layer (%d,%d)-(%d,%d)",
                   usurf->surfaceid,
                   usurf->surface->input.extents.x1, usurf->surface->input.extents.y1,
                   usurf->surface->input.extents.x2, usurf->surface->input.extents.y2);
        pixman_region32_fini(&usurf->surface->pending.input);
        pixman_region32_init_rect(&usurf->surface->pending.input,
                                  INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX);
    }

    /* rebild compositor surface list       */
    if (usurf->visible) {
        ico_window_mgr_restack_layer(usurf);
    }
    uifw_trace("win_mgr_set_layer: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_set_active: set(or change) active surface
 *
 * @param[in]   usurf       UIFW surface (if NULL, change to inactive)
 * @param[in]   target      target device
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_set_active(struct uifw_win_surface *usurf, const int target)
{
    struct weston_seat *seat;
    struct weston_surface *surface;
    int     object = target;
#if 0               /* pointer grab can not release */
    int     savetp, i;
#endif              /* pointer grab can not release */

    uifw_trace("win_mgr_set_active: Enter(%08x,%x)", usurf ? usurf->surfaceid : 0, target);

    if ((usurf) && (usurf->shsurf) && (usurf->surface)) {
        surface = usurf->surface;
        if ((object & (ICO_WINDOW_MGR_ACTIVE_POINTER|ICO_WINDOW_MGR_ACTIVE_KEYBOARD)) == 0) {
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
        if (object == 0)    {
            object = ICO_WINDOW_MGR_ACTIVE_POINTER | ICO_WINDOW_MGR_ACTIVE_KEYBOARD;
        }
        if (object & ICO_WINDOW_MGR_ACTIVE_POINTER) {
            _ico_win_mgr->active_pointer_usurf = NULL;
        }
        if (object & ICO_WINDOW_MGR_ACTIVE_KEYBOARD)    {
            _ico_win_mgr->active_keyboard_usurf = NULL;
        }
    }

    wl_list_for_each (seat, &_ico_win_mgr->compositor->seat_list, link) {
#if 0               /* pointer grab can not release */
        if (object & ICO_WINDOW_MGR_ACTIVE_POINTER) {
            if (surface)    {
                if ((seat->pointer != NULL) && (seat->pointer->focus != surface))   {
                    uifw_trace("win_mgr_set_active: pointer reset focus(%08x)",
                               (int)seat->pointer->focus);
                    if (seat->pointer->button_count > 0)    {
                        /* emulate button release   */
                        notify_button(seat, weston_compositor_get_time(),
                                      seat->pointer->grab_button,
                                      WL_POINTER_BUTTON_STATE_RELEASED);
                        /* count up button, because real mouse botan release    */
                        seat->pointer->button_count ++;
                    }
                    weston_pointer_set_focus(seat->pointer, NULL,
                                             wl_fixed_from_int(0), wl_fixed_from_int(0));
                }
                else    {
                    uifw_trace("win_mgr_set_active: pointer nochange surface(%08x)",
                               (int)surface);
                }
                if ((seat->touch != NULL) && (seat->touch->focus != surface))   {
                    uifw_trace("win_mgr_set_active: touch reset surface(%08x)",
                               (int)seat->touch->focus);
                    if (seat->num_tp > 10)  {
                        seat->num_tp = 0;       /* safty gard   */
                    }
                    else if (seat->num_tp > 0)   {
                        /* emulate touch up         */
                        savetp = seat->num_tp;
                        for (i = 0; i < savetp; i++)    {
                            notify_touch(seat, weston_compositor_get_time(), i+1,
                                         seat->touch->grab_x, seat->touch->grab_y,
                                         WL_TOUCH_UP);
                        }
                        /* touch count up, becase real touch release    */
                        seat->num_tp = savetp;
                    }
                    weston_touch_set_focus(seat, NULL);
                }
                else    {
                    uifw_trace("win_mgr_set_active: touch nochange surface(%08x)",
                               (int)surface);
                }
            }
            else    {
                uifw_trace("win_mgr_set_active: pointer reset surface(%08x)",
                           (int)seat->pointer->focus);
                if ((seat->pointer != NULL) && (seat->pointer->focus != NULL))  {
                    if (seat->pointer->button_count > 0)    {
                        /* emulate button release   */
                        notify_button(seat, weston_compositor_get_time(),
                                      seat->pointer->grab_button,
                                      WL_POINTER_BUTTON_STATE_RELEASED);
                        seat->pointer->button_count ++;
                    }
                    weston_pointer_set_focus(seat->pointer, NULL,
                                             wl_fixed_from_int(0), wl_fixed_from_int(0));
                }
                if ((seat->touch != NULL) && (seat->touch->focus != NULL))  {
                    if (seat->num_tp > 10)  {
                        seat->num_tp = 0;       /* safty gard   */
                    }
                    else if (seat->num_tp > 0)   {
                        /* emulate touch up         */
                        savetp = seat->num_tp;
                        for (i = 0; i < savetp; i++)    {
                            notify_touch(seat, weston_compositor_get_time(), i+1,
                                         seat->touch->grab_x, seat->touch->grab_y,
                                         WL_TOUCH_UP);
                        }
                        /* touch count up, becase real touch release    */
                        seat->num_tp = savetp;
                    }
                    weston_touch_set_focus(seat, NULL);
                }
            }
        }
#endif              /* pointer grab can not release */
        if ((object & ICO_WINDOW_MGR_ACTIVE_KEYBOARD) && (seat->keyboard))  {
            if (surface)    {
#if 0               /* pointer grab can not release */
                if (seat->keyboard->focus != surface)    {
#endif              /* pointer grab can not release */
                    weston_keyboard_set_focus(seat->keyboard, surface);
                    uifw_trace("win_mgr_set_active: keyboard change surface(%08x=>%08x)",
                               (int)seat->keyboard->focus, (int)surface);
#if 0               /* pointer grab can not release */
                }
                else    {
                    uifw_trace("win_mgr_set_active: keyboard nochange surface(%08x)",
                               (int)surface);
                }
#endif              /* pointer grab can not release */
            }
            else    {
                uifw_trace("win_mgr_set_active: keyboard reset surface(%08x)",
                           (int)seat->keyboard);
                weston_keyboard_set_focus(seat->keyboard, NULL);
            }
        }
    }
    uifw_trace("win_mgr_set_active: Leave");
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

    /* check for access control         */
    uclient = ico_window_mgr_find_uclient(client);
    if (! uclient)  {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "ico_window_mgr_declare_manager: unknown client");
        uifw_trace("uifw_declare_manager: Leave(unknown client=%08x)", (int)client);
        return;
    }
    if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_DECLARE_MANAGER) == 0)   {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "ico_window_mgr_declare_manager: not permitted");
        uifw_trace("uifw_declare_manager: Leave(%s not permitted)", uclient->appid);
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
                                                                   usurf->uclient->appid,
                                                                   usurf->layertype << 12);
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
    struct uifw_client      *uclient;
    struct uifw_win_surface *usurf;

    if (layer == ICO_WINDOW_MGR_LAYERTYPE_BACKGROUND)  {
        layer = _ico_ivi_background_layer;
    }
    else if (layer == ICO_WINDOW_MGR_LAYERTYPE_TOUCH)  {
        layer = _ico_ivi_touch_layer;
    }
    else if (layer == ICO_WINDOW_MGR_LAYERTYPE_CURSOR)    {
        layer = _ico_ivi_cursor_layer;
    }
    else if (layer == ICO_WINDOW_MGR_LAYERTYPE_STARTUP)    {
        layer = _ico_ivi_startup_layer;
    }

    uifw_trace("uifw_set_window_layer: Enter res=%08x surfaceid=%08x layer=%d",
               (int)resource, surfaceid, layer);

    /* check for access control         */
    if (resource != NULL)   {
        /* resource is NULL, internal use   */
        uclient = ico_window_mgr_find_uclient(client);
        if (! uclient)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_window_layer: unknown client");
            uifw_trace("uifw_set_window_layer: Leave(unknown client=%08x)", (int)client);
            return;
        }
        if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_SET_WINDOW_LAYER) == 0)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_window_layer: not permitted");
            uifw_trace("uifw_set_window_layer: Leave(%s not permitted)", uclient->appid);
            return;
        }
    }
    usurf = ico_window_mgr_get_usurf_client(surfaceid, client);

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
    int     oldx, oldy;
    struct uifw_node_table  *oldnode;

    uifw_trace("uifw_set_positionsize: Enter surf=%08x node=%x x/y/w/h=%d/%d/%d/%d flag=%x",
               surfaceid, node, x, y, width, height, flags);

    /* check for access control         */
    uclient = ico_window_mgr_find_uclient(client);
    if (resource != NULL)   {
        /* resource is NULL, internal use   */
        if (! uclient)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_positionsize: unknown client");
            uifw_trace("uifw_set_positionsize: Leave(unknown client=%08x)", (int)client);
            return;
        }
        if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_SET_POSITIONSIZE) == 0)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_positionsize: not permitted");
            uifw_trace("uifw_set_positionsize: Leave(%s not permitted)", uclient->appid);
            return;
        }
    }
    usurf = ico_window_mgr_get_usurf_client(surfaceid, client);
    if (! usurf)    {
        uifw_trace("uifw_set_positionsize: Leave(surf=%08x NOT Found)", surfaceid);
        return;
    }
    oldx = usurf->x;
    oldy = usurf->y;
    oldnode = usurf->node_tbl;

    usurf->disable = 0;
    if (((int)node) >= _ico_num_nodes)  {
        uifw_trace("uifw_set_positionsize: node=%d dose not exist(max=%d)",
                   node, _ico_num_nodes);
        if ((ico_ivi_optionflag() & ICO_IVI_OPTION_SHOW_NODISP) == 0)   {
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
                (uclient->manager == 0) && (uclient->privilege == 0))   uclient = NULL;
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
        uifw_trace("uifw_set_positionsize: Leave(OK, but no buffer)");
    }

    /* if position change, call hook for input region   */
    if (win_mgr_hook_change != NULL)    {
        if ((oldx != usurf->x) || (oldy != usurf->y) || (oldnode != usurf->node_tbl))   {
            (*win_mgr_hook_change)(usurf);
        }
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
    int     oldvisible;

    uifw_trace("uifw_set_visible: Enter(surf=%08x,%d,%d,%x)",
               surfaceid, visible, raise, flags);

    /* check for access control         */
    uclient = ico_window_mgr_find_uclient(client);
    if (resource != NULL)   {
        /* resource is NULL, internal use   */
        if (! uclient)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_visible: unknown client");
            uifw_trace("uifw_set_visible: Leave(unknown client=%08x)", (int)client);
            return;
        }
        if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_SET_POSITIONSIZE) == 0)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_visible: not permitted");
            uifw_trace("uifw_set_visible: Leave(%s not permitted)", uclient->appid);
            return;
        }
    }
    usurf = ico_window_mgr_get_usurf_client(surfaceid, client);
    if ((! usurf) || (! usurf->surface))    {
        uifw_trace("uifw_set_visible: Leave(Surface Not Exist)");
        return;
    }
    oldvisible = ico_window_mgr_is_visible(usurf);

    if ((surfaceid != ICO_WINDOW_MGR_V_MAINSURFACE) &&
        (uclient->manager == 0) && (uclient->privilege == 0))   {
        uifw_trace("uifw_set_visible: Request from App(%s), not Manager",
                   uclient ? uclient->appid : "");
        uclient = NULL;
    }
    else    {
        uifw_trace("uifw_set_visible: Request from Manager(%s)", uclient->appid);
    }

    restack = 0;

    if ((usurf->disable == 0) && (visible == ICO_WINDOW_MGR_VISIBLE_SHOW))  {

#if  PERFORMANCE_EVALUATIONS > 0
        if (! usurf->visible)   {
            uifw_perf("SWAP_BUFFER Show appid=%s surface=%08x",
                      usurf->uclient->appid, usurf->surfaceid);
        }
#endif /*PERFORMANCE_EVALUATIONS*/
        if ((! usurf->visible) ||
            (usurf->animation.state != ICO_WINDOW_MGR_ANIMATION_STATE_NONE))    {
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

#if  PERFORMANCE_EVALUATIONS > 0
        if (usurf->visible) {
            uifw_perf("SWAP_BUFFER Hide appid=%s surface=%08x",
                      usurf->uclient->appid, usurf->surfaceid);
        }
#endif /*PERFORMANCE_EVALUATIONS*/
        if ((usurf->visible) ||
            (usurf->animation.state != ICO_WINDOW_MGR_ANIMATION_STATE_NONE))    {

            /* Reset focus                                  */
            win_mgr_reset_focus(usurf);

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
        ico_window_mgr_restack_layer(usurf);
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

    /* if visible change, call hook for input region    */
    if (win_mgr_hook_change != NULL)    {
        if (oldvisible != ico_window_mgr_is_visible(usurf))    {
            (*win_mgr_hook_change)(usurf);
        }
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
    struct uifw_client *uclient;
    int     animaid;
    struct uifw_win_surface *usurf = ico_window_mgr_get_usurf_client(surfaceid, client);

    uifw_trace("uifw_set_transition: surf=%08x,type=%x,anim=%s,time=%d",
               surfaceid, type, animation, time);

    /* check for access control         */
    if (resource != NULL)   {
        /* resource is NULL, internal use   */
        uclient = ico_window_mgr_find_uclient(client);
        if (! uclient)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_animation: unknown client");
            uifw_trace("uifw_set_animation: Leave(unknown client=%08x)", (int)client);
            return;
        }
        if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_SET_ANIMATION) == 0) {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_animation: not permitted");
            uifw_trace("uifw_set_animation: Leave(%s not permitted)", uclient->appid);
            return;
        }
    }
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
        uifw_trace("uifw_set_animation: Surface(%08x) Not exist", surfaceid);
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
    struct uifw_client *uclient;
    struct uifw_win_surface *usurf = ico_window_mgr_get_usurf_client(surfaceid, client);

    uifw_trace("uifw_set_attributes: Enter(surf=%08x,attributes=%x)", surfaceid, attributes);

    /* check for access control         */
    if (resource != NULL)   {
        /* resource is NULL, internal use   */
        uclient = ico_window_mgr_find_uclient(client);
        if (! uclient)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_attributes: unknown client");
            uifw_trace("uifw_set_attributes: Leave(unknown client=%08x)", (int)client);
            return;
        }
        if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_SET_ATTRIBUTES) == 0)    {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_attributes: not permitted");
            uifw_trace("uifw_set_attributes: Leave(%s not permitted)", uclient->appid);
            return;
        }
    }
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
    struct uifw_client *uclient;
    struct uifw_win_surface *usurf;

    uifw_trace("uifw_visible_animation: Enter(%08x,%d,x/y=%d/%d,w/h=%d/%d)",
               surfaceid, visible, x, y, width, height);

    /* check for access control         */
    if (resource != NULL)   {
        /* resource is NULL, internal use   */
        uclient = ico_window_mgr_find_uclient(client);
        if (! uclient)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_visible_animation: unknown client");
            uifw_trace("uifw_visible_animation: Leave(unknown client=%08x)", (int)client);
            return;
        }
        if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_VISIBLE_ANIMATION) == 0) {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_visible_animation: not permitted");
            uifw_trace("uifw_visible_animation: Leave(%s not permitted)", uclient->appid);
            return;
        }
    }
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
    struct uifw_client *uclient;
    struct uifw_win_surface *usurf;

    uifw_trace("uifw_set_active: Enter(surf=%08x,active=%x)", surfaceid, active);

    /* check for access control         */
    if (resource != NULL)   {
        /* resource is NULL, internal use   */
        uclient = ico_window_mgr_find_uclient(client);
        if (! uclient)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_active: unknown client");
            uifw_trace("uifw_set_active: Leave(unknown client=%08x)", (int)client);
            return;
        }
        if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_SET_ACTIVE) == 0)    {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_active: not permitted");
            uifw_trace("uifw_set_active: Leave(%s not permitted)", uclient->appid);
            return;
        }
    }
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
                uifw_trace("uifw_set_active: pointer active change %08x->%08x",
                           _ico_win_mgr->active_pointer_usurf ?
                               _ico_win_mgr->active_pointer_usurf->surfaceid : 0,
                           usurf ? usurf->surfaceid : 0);
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
                uifw_trace("uifw_set_active: keyboard active change %08x->%08x",
                           _ico_win_mgr->active_keyboard_usurf ?
                               _ico_win_mgr->active_keyboard_usurf->surfaceid : 0,
                           usurf ? usurf->surfaceid : 0);
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
                uifw_trace("uifw_set_active: active change %08x/%08x->%08x",
                           _ico_win_mgr->active_pointer_usurf ?
                               _ico_win_mgr->active_pointer_usurf->surfaceid : 0,
                           _ico_win_mgr->active_keyboard_usurf ?
                               _ico_win_mgr->active_keyboard_usurf->surfaceid : 0,
                           usurf ? usurf->surfaceid : 0);
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
    struct uifw_client      *uclient;
    struct uifw_win_layer   *el;
    struct uifw_win_layer   *new_el;
    struct uifw_win_surface *usurf;
    int     layertype = 0;

    if ((layer == ICO_WINDOW_MGR_LAYERTYPE_BACKGROUND) ||
        (layer == (uint32_t)_ico_ivi_background_layer))   {
        layer = _ico_ivi_background_layer;
        layertype = LAYER_TYPE_BACKGROUND;
    }
    else if ((layer == ICO_WINDOW_MGR_LAYERTYPE_TOUCH) ||
             (layer == (uint32_t)_ico_ivi_touch_layer))   {
        layer = _ico_ivi_touch_layer;
        layertype = LAYER_TYPE_TOUCH;
    }
    else if ((layer == ICO_WINDOW_MGR_LAYERTYPE_CURSOR) ||
             (layer == (uint32_t)_ico_ivi_cursor_layer))  {
        layer = _ico_ivi_cursor_layer;
        layertype = LAYER_TYPE_CURSOR;
    }
    else if (layer == ICO_WINDOW_MGR_LAYERTYPE_STARTUP)    {
        layer = _ico_ivi_startup_layer;
    }
    uifw_trace("uifw_set_layer_visible: Enter(layer=%d, visilbe=%d)", layer, visible);

    /* check for access control         */
    if (resource != NULL)   {
        /* resource is NULL, internal use   */
        uclient = ico_window_mgr_find_uclient(client);
        if (! uclient)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_layer_visible: unknown client");
            uifw_trace("uifw_set_layer_visible: Leave(unknown client=%08x)", (int)client);
            return;
        }
        if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_SET_LAYER_VISIBLE) == 0) {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_set_layer_visible: not permitted");
            uifw_trace("uifw_set_layer_visible: Leave(%s not permitted)", uclient->appid);
            return;
        }
    }
    /* Search Layer                             */
    wl_list_for_each (el, &_ico_win_mgr->ivi_layer_list, link) {
        if (el->layer == layer) break;
    }

    if (&el->link == &_ico_win_mgr->ivi_layer_list)    {
        /* layer not exist, create new layer    */
        uifw_trace("uifw_set_layer_visible: New Layer %d", layer);
        new_el = win_mgr_create_layer(NULL, layer, layertype);
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
            /* Reset focus if hide              */
            if (visible == 0)   {
                win_mgr_reset_focus(usurf);
            }
            /* Damage(redraw) target surface    */
            weston_surface_damage_below(usurf->surface);
        }
    }

    /* rebild compositor surface list       */
    ico_window_mgr_restack_layer(NULL);

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
 * @param[in]   pid         process id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_get_surfaces(struct wl_client *client, struct wl_resource *resource,
                  const char *appid, int32_t pid)
{
    struct uifw_client  *uclient;
    struct uifw_win_layer *el;
    struct uifw_win_surface *usurf;
    struct wl_array     reply;
    uint32_t            *up;

    uifw_trace("uifw_get_surfaces: Enter(appid=%s, pid=%d)", appid ? appid : " ", pid);

    /* check for access control         */
    uclient = ico_window_mgr_find_uclient(client);
    if (! uclient)  {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "ico_window_mgr_get_surfaces: unknown client");
        uifw_trace("uifw_get_surfaces: Leave(unknown client=%08x)", (int)client);
        return;
    }
    if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_GET_SURFACES) == 0)  {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "ico_window_mgr_get_surfaces: not permitted");
        uifw_trace("uifw_get_surfaces: Leave(%s not permitted)", uclient->appid);
        return;
    }

    wl_array_init(&reply);

    wl_list_for_each (uclient, &_ico_win_mgr->client_list, link)    {
        if ((appid != NULL) && (*appid != ' ')) {
            if (strcmp(uclient->appid, appid) == 0) break;
        }
        if (pid != 0)   {
            if (uclient->pid == pid)    break;
        }
    }
    if (&uclient->link == &_ico_win_mgr->client_list)    {
        uifw_trace("uifw_get_surfaces: appid=%s pid=%d dose not exist",
                   appid ? appid : " ", pid);
    }
    else    {
        wl_list_for_each (el, &_ico_win_mgr->ivi_layer_list, link) {
            wl_list_for_each (usurf, &el->surface_list, ivi_layer) {
                if (usurf->uclient == uclient)  {
                    uifw_trace("uifw_get_surfaces: %s(%d) surf=%08x",
                               uclient->appid, uclient->pid, usurf->surfaceid);
                    up = (uint32_t *)wl_array_add(&reply, sizeof(uint32_t));
                    if (up) {
                        *up = usurf->surfaceid;
                    }
                }
            }
        }
    }
    ico_window_mgr_send_app_surfaces(resource, uclient->appid, uclient->pid, &reply);

    wl_array_release(&reply);
    uifw_trace("uifw_get_surfaces: Leave");
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
    struct uifw_surface_map *sm;
    uint32_t    curtime;
    int         wait = 99999999;

    /* check touch down counter     */
    if (touch_check_seat)   {
        if (touch_check_seat->num_tp > 10)  {
            uifw_trace("win_mgr_check_mapsurface: illegal touch counter(num=%d), reset",
                       (int)touch_check_seat->num_tp);
            touch_check_seat->num_tp = 0;
        }
    }

    /* check all mapped surfaces    */
    curtime = weston_compositor_get_time();
    wl_list_for_each (sm, &_ico_win_mgr->map_list, map_link) {
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
    struct uifw_drm_buffer  *drm_buffer;
    struct uifw_dri_image   *dri_image;
    struct uifw_intel_region  *dri_region;
    struct uifw_gl_surface_state *gl_state;
    struct weston_surface   *es;
    struct wl_shm_buffer    *shm_buffer;
    uint32_t    eglname;
    int         width;
    int         height;
    int         stride;
    uint32_t    format;
    uint32_t    dtime;
    int         idx, idx2;
    int         delta, settime2;
    struct ico_uifw_image_buffer    *p;

    uifw_detail("win_mgr_change_mapsurface: surf=%08x event=%d", sm->usurf->surfaceid, event);
    if (event == 0) {
        event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_CONTENTS;
    }

    /* check if buffered        */
    drm_buffer = NULL;
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
            drm_buffer = (struct uifw_drm_buffer *)wl_resource_get_user_data(
                                (struct wl_resource *)es->buffer_ref.buffer->legacy_buffer);
            if ((drm_buffer != NULL) && (drm_buffer->driver_buffer == NULL))    {
                drm_buffer = NULL;
            }
        }
        if (drm_buffer == NULL) {
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
    else if (sm->uclient->shmbuf == NULL)   {
        /* no GPU acceleration but no buffer    */
        uifw_debug("win_mgr_change_mapsurface: client has no shared memory buffer");
        if (sm->initflag)   {
            event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP;
        }
        else    {
            event = 0;
        }
    }
    else if (sm->type == ICO_WINDOW_MGR_MAP_TYPE_SHM)   {
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
            gl_state = (struct uifw_gl_surface_state *)es->renderer_state;
            if (gl_state->buffer_type == BUFFER_TYPE_SHM)   {
                event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR;
            }
            else if (gl_state->buffer_type != BUFFER_TYPE_EGL)   {
                event = 0;
            }
            else    {
                dri_image = (struct uifw_dri_image *)drm_buffer->driver_buffer;
                dri_region = dri_image->region;
                width = es->buffer_ref.buffer->width;
                height = es->buffer_ref.buffer->height;
                stride = drm_buffer->stride[0];
                if (drm_buffer->format == __DRI_IMAGE_FOURCC_XRGB8888)  {
                    format = EGL_TEXTURE_RGB;
                }
                else if (drm_buffer->format == __DRI_IMAGE_FOURCC_ARGB8888) {
                    format = EGL_TEXTURE_RGBA;
                }
                else    {
                    /* unknown format, error    */
                    format = EGL_NO_TEXTURE;
                }
                eglname = dri_region->name;
                if (eglname == 0)   {
                    if (drm_intel_bo_flink((drm_intel_bo *)dri_region->bo, &eglname))   {
                        uifw_warn("win_mgr_change_mapsurface: drm_intel_bo_flink() Error");
                        eglname = 0;
                    }
                }
                if ((sm->initflag == 0) && (eglname != 0) &&
                    (width > 0) && (height > 0) && (stride > 0))    {
                    sm->initflag = 1;
                    event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_MAP;
                }
                else    {
                    if ((eglname == 0) || (width <= 0) || (height <= 0) || (stride <= 0))   {
                        event = 0;
                    }
                    else if (event == ICO_WINDOW_MGR_MAP_SURFACE_EVENT_CONTENTS)    {
                        if ((sm->width != width) || (sm->height != height) ||
                            (sm->stride != stride) || (format != sm->format))   {
                            event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_RESIZE;
                        }
                        else if (eglname != sm->eglname)    {
#if  PERFORMANCE_EVALUATIONS > 0
                            uifw_perf("SWAP_BUFFER appid=%s surface=%08x name=%d",
                                      sm->usurf->uclient->appid, sm->usurf->surfaceid,
                                      sm->eglname);
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
                sm->stride = stride;
                sm->eglname = eglname;
                sm->format = format;
            }
        }
        else    {
            if ((sm->type != ICO_WINDOW_MGR_MAP_TYPE_PIXEL) || (sm->eventque != 0) ||
                (es->buffer_ref.buffer == NULL) || (es->buffer_ref.buffer != sm->curbuf)) {
                sm->curbuf = es->buffer_ref.buffer;
                if (es->buffer_ref.buffer != NULL)  {
                    width = es->buffer_ref.buffer->width;
                    height = es->buffer_ref.buffer->height;
                }
                else    {
                    width = sm->usurf->client_width;
                    height = sm->usurf->client_height;
                }
                /* get shared memory buffer area    */
                idx2 = sm->uclient->bufnum;
                settime2 = 0x7fffffff;
                for (idx = 0; idx < sm->uclient->bufnum; idx++) {
                    p = (struct ico_uifw_image_buffer *)
                        (((char *)sm->uclient->shmbuf) + idx * sm->uclient->bufsize);
                    if (p->settime == p->reftime)   break;
                    delta = curtime - p->settime;
                    if (delta < 0)  {
                        delta = delta + 0x80000000;
                    }
                    if ((delta > 3000) && (delta < settime2))   {
                        idx2 = idx;
                        settime2 = p->settime;
                    }
                }
                uifw_detail("win_mgr_change_mapsurface: PIX %08x idx=%d idx2=%d w/h=%d/%d",
                            sm->usurf->surfaceid, idx, idx2, width, height);
                if (idx >= sm->uclient->bufnum) {
                    idx = idx2;
                }
                if (idx >= sm->uclient->bufnum) {
                    uifw_debug("win_mgr_change_mapsurface: shared buffer full");
                    event = 0;
                    sm->curbuf = NULL;
                    sm->eventque = 1;
                }
                else if ((sm->initflag == 0) && (width > 0) && (height > 0))    {
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
                if (event != 0) {
                    /* read pixel           */
                    p = (struct ico_uifw_image_buffer *)
                        (((char *)sm->uclient->shmbuf) + idx * sm->uclient->bufsize);
                    height = (sm->uclient->bufsize - sizeof(struct ico_uifw_image_buffer))
                             / (width * 4);
                    uifw_detail("win_mgr_change_mapsurface: PIX read buf=%08x height=%d(%d)",
                                (int)p, height, sm->height);
                    if ((height < sm->height) &&
                        (sm->type == ICO_WINDOW_MGR_MAP_TYPE_SHM))  {
                        uifw_warn("win_mgr_change_mapsurface: Buffer SHM, "
                                  "but buffer overflow(%d>%d)", sm->height, height);
                        event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR;
                        sm->eventque = 0;
                    }
                    else    {
                        if (height > sm->height)    {
                            height = sm->height;
                        }
                        if ((*(_ico_win_mgr->compositor->renderer->read_surface_pixels))
                                    (es, PIXMAN_a8r8g8b8, p->image,
                                     0, 0, sm->width, height) != 0) {
                            uifw_debug("win_mgr_change_mapsurface: Error read pixel %s.%08x",
                                       sm->usurf->uclient->appid, sm->usurf->surfaceid);
                            event = ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR;
                            sm->eventque = 0;
                        }
                        else    {
                            uifw_detail("win_mgr_change_mapsurface: PIX read pixels(%d)",
                                        idx+1);
                            p->surfaceid = sm->usurf->surfaceid;
                            p->settime = curtime;
                            p->width = sm->width;
                            p->height = height;
                            sm->eglname = idx + 1;
                        }
                    }
                }
            }
            else    {
                event = 0;
            }
        }
    }

    if (event != 0) {
        uifw_detail("win_mgr_change_mapsurface: send MAP event(ev=%d surf=%08x type=%d "
                    "name=%d w/h/s=%d/%d/%d format=%x",
                    event, sm->usurf->surfaceid, sm->type, sm->eglname,
                    sm->width, sm->height, sm->stride, sm->format);
        sm->lasttime = curtime;
        sm->eventque = 0;
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
 * @brief   uifw_set_map_buffer: set map surface image buffer
 *
 * @param[in]   client      Weyland client
 * @param[in]   resource    resource of request
 * @param[in]   shmname     shared memory name(POSIX I/F)
 * @param[in]   bufsize     buffer size in byte
 * @param[in]   bufnum      number of buffers
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
uifw_set_map_buffer(struct wl_client *client, struct wl_resource *resource,
                    const char *shmname, uint32_t bufsize, uint32_t bufnum)
{
    struct uifw_client              *uclient;
    struct ico_uifw_image_buffer    *p;
    char    *shmbuf;
    int     shmfd;
    int     i;

    uifw_trace("uifw_set_map_buffer: Enter(%s,%d,%d)",
               shmname ? shmname : "(null)", bufsize, bufnum);

    /* check for access control         */
    uclient = ico_window_mgr_find_uclient(client);
    if (! uclient)  {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "ico_window_mgr_set_map_buffer: unknown client");
        uifw_trace("uifw_set_map_buffer: Leave(unknown client=%08x)", (int)client);
        return;
    }
    if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_SET_MAP_BUFFER) == 0)    {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "ico_window_mgr_set_map_buffer: not permitted");
        uifw_trace("uifw_set_map_buffer: Leave(%s not permitted)", uclient->appid);
        return;
    }

    if ((shmname == NULL) || (*shmname == 0) || (*shmname == ' ') ||
        (bufsize == 0) || (bufnum == 0))    {
        /* delete shared memory buffer          */
        if (uclient->shmbuf)    {
            munmap(uclient->shmbuf, uclient->bufsize * uclient->bufnum);
            uclient->shmbuf = NULL;
        }
        uifw_trace("uifw_set_map_buffer: Leave(delete shared memory buffer)");
        return;
    }

    shmfd = shm_open(shmname, O_RDWR, 0600);
    if (shmfd == -1)    {
        /* shared memory dose not exist         */
        uifw_trace("uifw_set_map_buffer: Leave(shared memory(%s) dose not exist)", shmname);
        return;
    }

    shmbuf = (char *)mmap(NULL, bufsize * bufnum, PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0);
    close(shmfd);

    if (shmbuf == NULL) {
        /* can not map shared memory            */
        uifw_trace("uifw_set_map_buffer: Leave(can not map shared memory(%s))", shmname);
        return;
    }
    if (uclient->shmbuf)    {
        munmap(uclient->shmbuf, uclient->bufsize * uclient->bufnum);
    }

    uclient->shmbuf = shmbuf;
    uclient->bufsize = bufsize;
    uclient->bufnum = bufnum;
    for (i = 0; i < (int)bufnum; i++)    {
        p = (struct ico_uifw_image_buffer *)(((char *)shmbuf) + bufsize * i);
        memset((char *)p, 0, sizeof(struct ico_uifw_image_buffer));
        memcpy((char *)&p->magich, ICO_UIFW_IMAGE_BUFFER_MAGICH, 4);
        memcpy((char *)&p->magict, ICO_UIFW_IMAGE_BUFFER_MAGICT, 4);
    }
    uifw_trace("uifw_set_map_buffer: Leave(shm addr=%08x)", (int)uclient->shmbuf);
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
    struct wl_shm_buffer        *shm_buffer;
    struct uifw_client          *uclient;
    struct uifw_drm_buffer      *drm_buffer;
    struct uifw_gl_surface_state *gl_state;
    int     maptype;
    int     format;

    uifw_trace("uifw_map_surface: Enter(surface=%08x,fps=%d)", surfaceid, framerate);

    /* check for access control         */
    uclient = ico_window_mgr_find_uclient(client);
    if (resource != NULL)   {
        /* resource is NULL, internal use   */
        if (! uclient)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_map_surface: unknown client");
            uifw_trace("uifw_map_surface: Leave(unknown client=%08x)", (int)client);
            return;
        }
        if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_MAP_SURFACE) == 0)   {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_map_surface: not permitted");
            uifw_trace("uifw_map_surface: Leave(%s not permitted)", uclient->appid);
            return;
        }
    }
    if (! uclient)  {
        uifw_trace("uifw_map_surface: Leave(unknown client=%08x)", (int)client);
        return;
    }

    usurf = ico_window_mgr_get_usurf(surfaceid);
    if (! usurf)    {
        /* surface dose not exist, error        */
        ico_window_mgr_send_map_surface(resource, ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                        surfaceid, 1, 0, 0, 0, 0, 0);
        uifw_trace("uifw_map_surface: Leave(surface=%08x dose not exist)", surfaceid);
        return;
    }

    /* check if buffered        */
    es = usurf->surface;
    if (es == NULL) {
        /* surface has no buffer, error         */
        ico_window_mgr_send_map_surface(resource, ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                        surfaceid, 3, 0, 0, 0, 0, 0);
        uifw_trace("uifw_map_surface: Leave(surface(%08x) has no surface)", surfaceid);
        return;
    }
    buffer = es->buffer_ref.buffer;

    /* check buffer type        */
    gl_state = (struct uifw_gl_surface_state *)es->renderer_state;
    if ((_ico_ivi_gpu_type == ICO_GPUTYPE_NOACCELERATION) ||
        (gl_state == NULL) || (gl_state->buffer_type == BUFFER_TYPE_SHM))   {
        /* No Acceleration or wl_shm_buffer support ReadPixels  */
        if ((_ico_win_mgr->compositor->renderer == NULL) ||
            (_ico_win_mgr->compositor->renderer->read_surface_pixels == NULL) ||
            (uclient->shmbuf == NULL))  {
            ico_window_mgr_send_map_surface(resource, ICO_WINDOW_MGR_MAP_SURFACE_EVENT_ERROR,
                                            surfaceid, 4, 0, 0, 0, 0, 0);
            uifw_trace("uifw_map_surface: Leave(surface(%08x) not support ReadPixels)",
                       surfaceid);
            return;
        }
        if ((gl_state != NULL) && (gl_state->buffer_type == BUFFER_TYPE_SHM))   {
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
                                                surfaceid, 5, 0, 0, 0, 0, 0);
                uifw_trace("uifw_map_surface: Leave(surface(%08x) not support shm_buffer(%x))",
                           surfaceid, format);
                return;
            }
        }
        else    {
            maptype = ICO_WINDOW_MGR_MAP_TYPE_PIXEL;
        }
    }
    else    {
        /* H/W(GPU) driver EGL buffer (Intel only)  */
        maptype = ICO_WINDOW_MGR_MAP_TYPE_EGL;
    }

    /* maximum framerate        */
    if (maptype == ICO_WINDOW_MGR_MAP_TYPE_EGL) {
        if ((framerate <= 0) || (framerate > _ico_ivi_map_framerate_gpu))
            framerate = _ico_ivi_map_framerate_gpu;
    }
    else if (maptype == ICO_WINDOW_MGR_MAP_TYPE_SHM) {
        if ((framerate <= 0) || (framerate > _ico_ivi_map_framerate_shm))
            framerate = _ico_ivi_map_framerate_shm;
    }
    else    {
        if ((framerate <= 0) || (framerate > _ico_ivi_map_framerate_pixel))
            framerate = _ico_ivi_map_framerate_pixel;
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
                                                surfaceid, 6, 0, 0, 0, 0, 0);
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
            drm_buffer = (struct uifw_drm_buffer *)wl_resource_get_user_data(
                                            (struct wl_resource *)buffer->legacy_buffer);
            if (drm_buffer != NULL) {
                sm->stride = drm_buffer->stride[0];
                if (drm_buffer->format == __DRI_IMAGE_FOURCC_XRGB8888)  {
                    sm->format = EGL_TEXTURE_RGB;
                }
                else if (drm_buffer->format == __DRI_IMAGE_FOURCC_ARGB8888) {
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
        sm->width = usurf->client_width;
        sm->height = usurf->client_height;
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
    struct ico_uifw_image_buffer    *p;
    int     idx;

    uifw_trace("uifw_unmap_surface: Enter(surface=%08x)", surfaceid);

    /* check for access control         */
    if (resource != NULL)   {
        /* resource is NULL, internal use   */
        uclient = ico_window_mgr_find_uclient(client);
        if (! uclient)  {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_unmap_surface: unknown client");
            uifw_trace("uifw_unmap_surface: Leave(unknown client=%08x)", (int)client);
            return;
        }
        if ((uclient->api_access_control & ICO_UIFW_WINDOW_MGR_UNMAP_SURFACE) == 0) {
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                                   "ico_window_mgr_unmap_surface: not permitted");
            uifw_trace("uifw_unmap_surface: Leave(%s not permitted)", uclient->appid);
            return;
        }
    }
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
                uifw_trace("uifw_unmap_surface: send UNMAP event(ev=%d surf=%08x name=%08x "
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

    wl_list_for_each_safe (sm, sm_tmp, &usurf->surf_map, surf_link) {
        if (((uclient != NULL) && (sm->uclient != uclient)))   continue;
        /* send unmap event                     */
        if ((uclient != NULL) && (uclient->mgr != NULL))    {
            uifw_trace("uifw_unmap_surface: send UNMAP event(ev=%d surf=%08x name=%08x "
                       "w/h/s=%d/%d/%d format=%x",
                       ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP, surfaceid,
                       sm->eglname, sm->width, sm->height, sm->stride, sm->format);
            ico_window_mgr_send_map_surface(uclient->mgr->resource,
                                            ICO_WINDOW_MGR_MAP_SURFACE_EVENT_UNMAP,
                                            surfaceid, sm->type, sm->eglname, sm->width,
                                            sm->height, sm->stride, sm->format);
        }
        if ((sm->type != ICO_WINDOW_MGR_MAP_TYPE_EGL) &&
            (sm->uclient->shmbuf != NULL))  {
            /* reset shared memory buffer   */
            for (idx = 0; idx < sm->uclient->bufnum; idx++) {
                p = (struct ico_uifw_image_buffer *)
                    (((char *)sm->uclient->shmbuf) + idx * sm->uclient->bufsize);
                if (p->surfaceid == surfaceid)  {
                    p->surfaceid = 0;
                    p->settime = 0;
                    p->reftime = 0;
                }
            }
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

    /* set surface size     */
    uifw_debug("win_mgr_change_surface: set surface x/y=%d/%d=>%d/%d w/h=%d/%d=>%d/%d",
               (int)es->geometry.x, (int)es->geometry.y, usurf->x, usurf->y,
               usurf->width, usurf->height, es->geometry.width, es->geometry.height);
    if ((usurf->width <= 0) || (usurf->height <= 0))    {
        usurf->width = es->geometry.width;
        usurf->height = es->geometry.height;
    }
    win_mgr_set_scale(usurf);
    if (usurf->visible) {
        weston_surface_set_position(usurf->surface,
                                    (float)(usurf->node_tbl->disp_x +
                                            usurf->x + usurf->xadd),
                                    (float)(usurf->node_tbl->disp_y +
                                            usurf->y + usurf->yadd));
        ico_window_mgr_restack_layer(usurf);
    }
    else    {
        weston_surface_set_position(usurf->surface, (float)(ICO_IVI_MAX_COORDINATE+1),
                                    (float)(ICO_IVI_MAX_COORDINATE+1));
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
 * @return      title string
 * @retval      != NULL     title string
 * @retval      NULL        no title string
 */
/*--------------------------------------------------------------------------*/
static char *
win_mgr_set_title(struct weston_surface *surface, const char *title)
{
    struct uifw_win_surface *usurf;
    char    *optpoint, *ppoint, *bpoint, *cpoint;
    int     layer, visible, raise, node, x, y, width, height, attribute;
    int     rx, ry, rw, rh;
    char    wktitle[ICO_IVI_WINNAME_LENGTH + 256];

    uifw_trace("win_mgr_set_title: Enter(%08x) name=%s", (int)surface, title);

    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("win_mgr_set_title: Leave(Not Exist)");
        return (char *)title;
    }

    layer = -1;
    visible = -1;
    raise = -1;
    node = -1;
    x = -1;
    y = -1;
    width = -1;
    height = -1;
    attribute = -1;

    strncpy(wktitle, title, ICO_IVI_WINNAME_LENGTH+256-1);
    wktitle[ICO_IVI_WINNAME_LENGTH+256-1] = 0;

    /* get option parameters    */
    optpoint = strstr(wktitle, "@@");

    if (optpoint != NULL)   {
        *optpoint = 0;

        usurf->uclient->privilege = 1;  /* privilege application    */
        ppoint = optpoint + 2;
        while (1)   {
            bpoint = strtok(ppoint, "=;");
            ppoint = NULL;
            if (! bpoint)   break;
            if (strcasecmp(bpoint, "layer") == 0)   {
                /* layer name or number */
                bpoint = strtok(ppoint, "=;");
                if (bpoint) {
                    if (strcasecmp(bpoint, "background") == 0)  {
                        layer = _ico_ivi_background_layer;
                    }
                    else if (strcasecmp(bpoint, "default") == 0)    {
                        layer = _ico_ivi_default_layer;
                    }
                    else if (strcasecmp(bpoint, "touch") == 0)  {
                        layer = _ico_ivi_touch_layer;
                    }
                    else if (strcasecmp(bpoint, "cursor") == 0) {
                        layer = _ico_ivi_cursor_layer;
                    }
                    else if (strcasecmp(bpoint, "startup") == 0)    {
                        layer = _ico_ivi_startup_layer;
                    }
                    else    {
                        layer = strtol(bpoint, (char **)0, 0);
                    }
                }
            }
            else if (strcasecmp(bpoint, "show") == 0)   {
                /* show     */
                visible = 1;
            }
            else if (strcasecmp(bpoint, "hide") == 0)   {
                /* hide     */
                visible = 0;
            }
            else if (strcasecmp(bpoint, "raise") == 0)  {
                /* raiee    */
                raise = 1;
            }
            else if (strcasecmp(bpoint, "lower") == 0)  {
                /* lower    */
                raise = 0;
            }
            else if (strcasecmp(bpoint, "node") == 0)   {
                /* node     */
                bpoint = strtok(ppoint, ",");
                if (bpoint) {
                    node = strtol(bpoint, (char **)0, 0);
                }
            }
            else if (strcasecmp(bpoint, "position") == 0)   {
                /* position */
                bpoint = strtok(ppoint, ",");
                if (bpoint) {
                    cpoint = strtok(ppoint, ";");
                    if (cpoint) {
                        x = strtol(bpoint, (char **)0, 0);
                        y = strtol(cpoint, (char **)0, 0);
                    }
                }
            }
            else if (strcasecmp(bpoint, "size") == 0)   {
                /* size     */
                bpoint = strtok(ppoint, ",");
                if (bpoint) {
                    cpoint = strtok(ppoint, ";");
                    if (cpoint) {
                        width = strtol(bpoint, (char **)0, 0);
                        height = strtol(cpoint, (char **)0, 0);
                    }
                }
            }
            else if (strcasecmp(bpoint, "attribute") == 0)  {
                /* attribute flags  */
                bpoint = strtok(ppoint, ",");
                if (bpoint) {
                    attribute = strtol(bpoint, (char **)0, 0);
                }
            }
            else if (strcasecmp(bpoint, "region") == 0) {
                /* set input region     */
                bpoint = strtok(ppoint, ",");
                if (bpoint) {
                    ry = 0;
                    rw = 100;
                    rh = 50;
                    rx = strtol(bpoint, (char **)0, 0);
                    bpoint = strtok(ppoint, ",");
                    if (bpoint) {
                        ry = strtol(bpoint, (char **)0, 0);
                    }
                    bpoint = strtok(ppoint, ",");
                    if (bpoint) {
                        rw = strtol(bpoint, (char **)0, 0);
                    }
                    bpoint = strtok(ppoint, ";");
                    if (bpoint) {
                        rh = strtol(bpoint, (char **)0, 0);
                    }
                    if (win_mgr_hook_inputregion)   {
                        (*win_mgr_hook_inputregion)(1, usurf, rx, ry, rw, rh, 0, 0,
                                                    0, 0, 0, 0, 0);
                    }
                }
            }
            else if (strcasecmp(bpoint, "unregion") == 0)   {
                /* unset input region   */
                bpoint = strtok(ppoint, ",");
                if (bpoint) {
                    ry = 0;
                    rw = 100;
                    rh = 50;
                    rx = strtol(bpoint, (char **)0, 0);
                    bpoint = strtok(ppoint, ",");
                    if (bpoint) {
                        ry = strtol(bpoint, (char **)0, 0);
                    }
                    bpoint = strtok(ppoint, ",");
                    if (bpoint) {
                        rw = strtol(bpoint, (char **)0, 0);
                    }
                    bpoint = strtok(ppoint, ";");
                    if (bpoint) {
                        rh = strtol(bpoint, (char **)0, 0);
                    }
                    if (win_mgr_hook_inputregion)   {
                        (*win_mgr_hook_inputregion)(0, usurf, rx, ry, rw, rh, 0, 0,
                                                    0, 0, 0, 0, 0);
                    }
                }
            }
        }
    }

    if ((optpoint == NULL) || (wktitle[0] != 0))  {
        if (((usurf->width > 0) && (usurf->height > 0)) &&
            ((usurf->created == 0) ||
             (strncmp(wktitle, usurf->winname, ICO_IVI_WINNAME_LENGTH-1) != 0)))    {
            strncpy(usurf->winname, wktitle, ICO_IVI_WINNAME_LENGTH-1);
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
            strncpy(usurf->winname, wktitle, ICO_IVI_WINNAME_LENGTH-1);
            usurf->winname[ICO_IVI_WINNAME_LENGTH-1] = 0;
        }
    }

    if (optpoint)   {
        if (layer >= 0) {
            uifw_set_window_layer(usurf->uclient->client, NULL, usurf->surfaceid, layer);
        }
        if (attribute >= 0) {
            uifw_set_attributes(usurf->uclient->client, NULL, usurf->surfaceid, attribute);
        }
        if ((node >= 0) || (x >=0) || (y >= 0) || (width >=0) || (height >= 0)) {
            if (node < 0)   node = usurf->node_tbl->node;
            if (x < 0)      x = ICO_IVI_MAX_COORDINATE + 1;
            if (y < 0)      y = ICO_IVI_MAX_COORDINATE + 1;
            if (width < 0)  width = ICO_IVI_MAX_COORDINATE + 1;
            if (height < 0) height = ICO_IVI_MAX_COORDINATE + 1;
            uifw_set_positionsize(usurf->uclient->client, NULL, usurf->surfaceid,
                                  node, x, y, width, height, 0);
        }
        if ((visible >= 0)  || (raise >= 0))    {
            if (visible < 0)    visible = ICO_WINDOW_MGR_V_NOCHANGE;
            if (raise < 0)      raise = ICO_WINDOW_MGR_V_NOCHANGE;
            uifw_set_visible(usurf->uclient->client, NULL, usurf->surfaceid,
                             visible, raise, 0);
        }
    }
    uifw_trace("win_mgr_set_title: Leave");

    return usurf->winname;
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
 * @brief   win_mgr_show_layer: shell layer visible control
 *
 * @param[in]   layertype   shell layer type
 * @param[in]   show        show(1)/hide(0)
 * @param[in]   data        requested weston surface in show
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_show_layer(int layertype, int show, void *data)
{
    struct uifw_win_layer   *el;
    struct uifw_win_surface *usurf;
    struct uifw_win_surface *inusurf = NULL;

    uifw_trace("win_mgr_show_layer: Enter(type=%d, show=%d, data=%08x)",
               layertype, show, (int)data);

    if (layertype != LAYER_TYPE_INPUTPANEL) {
        uifw_trace("win_mgr_show_layer: Leave(layertype npt InputPanel)");
        return;
    }
    if (show)   {
        if (data == NULL)   {
            uifw_trace("win_mgr_show_layer: Leave(show but input surface not exist)");
            return;
        }
        inusurf = find_uifw_win_surface_by_ws((struct weston_surface *)data);
        if (! inusurf)  {
            uifw_trace("win_mgr_show_layer: Leave(show but unknown input surface)");
            return;
        }
    }

    /*  all input panel surface show/hide   */
    wl_list_for_each (el, &_ico_win_mgr->ivi_layer_list, link)  {
        if ((el->layertype == LAYER_TYPE_CURSOR) ||
            (el->layertype == LAYER_TYPE_TOUCH))    continue;
        wl_list_for_each (usurf, &el->surface_list, ivi_layer) {
            if ((usurf->layertype == LAYER_TYPE_INPUTPANEL) &&
                (usurf->surface != NULL) && (usurf->mapped != 0) &&
                (usurf->surface->buffer_ref.buffer != NULL))    {

                if ((inusurf != NULL) && (usurf->win_layer != inusurf->win_layer))  {
                    win_mgr_set_layer(usurf, usurf->win_layer->layer);
                    usurf->raise = 1;
                    win_mgr_change_surface(usurf->surface, -1, 1);
                }
                if ((show == 0) || (ico_ivi_optionflag() & ICO_IVI_OPTION_SHOW_INPUTLAYER)) {
                    /* show input panel automatically   */
                    ico_window_mgr_set_visible(usurf, show | 2);
                }
                else    {
                    /* send hint event to HomeScreen    */
                    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_VISIBLE,
                                            usurf, show, ICO_WINDOW_MGR_RAISE_RAISE, 1, 0,0);
                }
            }
        }
    }
    uifw_trace("win_mgr_show_layer: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_fullscreen: shell full screen surface control
 *
 * @param[in]   event       control event
 * @param[in]   surface     target weston surface
 * @return      result
 */
/*--------------------------------------------------------------------------*/
static int
win_mgr_fullscreen(int event, struct weston_surface *surface)
{
    struct uifw_win_surface *usurf;
    struct uifw_win_surface *tmpusurf;
    struct uifw_win_layer   *ulayer;
    int     width, height;
    int     sx, sy;

    uifw_trace("win_mgr_fullscreen: Enter(event=%d, surface=%08x)", event, (int)surface);

    if (event == SHELL_FULLSCREEN_HIDEALL)  {
        /* hide all fullscreen srface       */
        uifw_trace("win_mgr_fullscreen: SHELL_FULLSCREEN_HIDEALL");

        wl_list_for_each (ulayer, &_ico_win_mgr->ivi_layer_list, link)  {
            if (ulayer->layertype >= LAYER_TYPE_TOUCH)  continue;
            wl_list_for_each_safe (usurf, tmpusurf, &ulayer->surface_list, ivi_layer)   {
                if (usurf->layertype == LAYER_TYPE_FULLSCREEN)  {
                    ico_window_mgr_set_visible(usurf, 2);
                    usurf->layertype = usurf->old_layertype;
                    win_mgr_set_layer(usurf, usurf->old_layer->layer);
                    win_mgr_change_surface(usurf->surface, -1, 1);
                    /* send event to HomeScreen         */
                    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_CONFIGURE,
                                            usurf, usurf->x, usurf->y,
                                            usurf->width, usurf->height, 0);
                }
            }
        }
        uifw_trace("win_mgr_fullscreen: Leave");
        return 0;
    }

    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf)    {
        uifw_trace("win_mgr_fullscreen: Leave(surface dose not exist)");
        return -1;
    }

    switch(event)   {
    case SHELL_FULLSCREEN_ISTOP:        /* check if top surface             */
        if (usurf->layertype == LAYER_TYPE_FULLSCREEN)  {
            wl_list_for_each (ulayer, &_ico_win_mgr->ivi_layer_list, link)  {
                if (ulayer->layertype >= LAYER_TYPE_TOUCH)  continue;
                wl_list_for_each(tmpusurf, &ulayer->surface_list, ivi_layer)    {
                    if (usurf == tmpusurf)  {
                        uifw_trace("win_mgr_fullscreen: %08x SHELL_FULLSCREEN_ISTOP"
                                   "(fullscreen surface)", usurf->surfaceid);
                        return 1;
                    }
                    if (tmpusurf->layertype == LAYER_TYPE_FULLSCREEN)   {
                        uifw_trace("win_mgr_fullscreen: %08x SHELL_FULLSCREEN_ISTOP"
                                   "(fullscreen surface but not top)", usurf->surfaceid);
                        return 0;
                    }
                }
            }
        }
        uifw_trace("win_mgr_fullscreen: %08x SHELL_FULLSCREEN_ISTOP"
                   "(not fullscreen surface)", usurf->surfaceid);
        return 0;
    case SHELL_FULLSCREEN_SET:          /* change surface to full screen    */
        uifw_trace("win_mgr_fullscreen: %08x SHELL_FULLSCREEN_SET", usurf->surfaceid);
        if (usurf->layertype != LAYER_TYPE_FULLSCREEN)  {
            usurf->old_layertype = usurf->layertype;
            usurf->layertype = LAYER_TYPE_FULLSCREEN;
            usurf->old_layer = usurf->win_layer;
            /* send hint event to HomeScreen    */
            ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_CONFIGURE,
                                    usurf, usurf->x, usurf->y,
                                    usurf->width, usurf->height, 1);
        }
        break;
    case SHELL_FULLSCREEN_STACK:        /* change surface to top of layer   */
        uifw_trace("win_mgr_fullscreen: %08x SHELL_FULLSCREEN_STACK", usurf->surfaceid);
        if (usurf->mapped == 0) {
            uifw_trace("win_mgr_fullscreen: not map, change to map");
            width = usurf->node_tbl->disp_width;
            height = usurf->node_tbl->disp_height;
            sx = 0;
            sy = 0;
            win_mgr_surface_map(usurf->surface, &width, &height, &sx, &sy);
        }
        if ((usurf->surface != NULL) && (usurf->mapped != 0) &&
            (usurf->surface->buffer_ref.buffer != NULL))    {
            /* fullscreen surface raise         */
            win_mgr_set_raise(usurf, 1);
        }
        break;
    case SHELL_FULLSCREEN_CONF:         /* configure full screen surface    */
        uifw_trace("win_mgr_fullscreen: %08x SHELL_FULLSCREEN_CONF", usurf->surfaceid);
        if (usurf->mapped == 0) {
            width = usurf->node_tbl->disp_width;
            height = usurf->node_tbl->disp_height;
            sx = 0;
            sy = 0;
            win_mgr_surface_map(usurf->surface, &width, &height, &sx, &sy);
        }
        break;
    default:
        uifw_trace("win_mgr_fullscreen: Leave(unknown event %d)", event);
        return -1;
    }
    uifw_trace("win_mgr_fullscreen: Leave");
    return 0;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   win_mgr_reset_focus: reset surface focus
 *
 * @param[in]   usurf       UIFW surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
win_mgr_reset_focus(struct uifw_win_surface *usurf)
{
    struct weston_seat      *seat;
    struct weston_surface   *surface;

    uifw_trace("win_mgr_reset_focus: Enter(%08x)", usurf->surfaceid);

    seat = container_of (_ico_win_mgr->compositor->seat_list.next, struct weston_seat, link);
    surface = usurf->surface;
    if ((seat != NULL) && (surface != NULL))    {
#if 0               /* pointer grab can not release */
        /* reset pointer focus          */
        if ((seat->pointer != NULL) && (seat->pointer->focus == surface))   {
            weston_pointer_set_focus(seat->pointer, NULL,
                                     wl_fixed_from_int(0), wl_fixed_from_int(0));
        }
        /* reset touch focus            */
        if ((seat->touch != NULL) && (seat->touch->focus == surface))   {
            weston_touch_set_focus(seat, NULL);
        }
#endif              /* pointer grab can not release */
        /* reset keyboard focus         */
        if ((seat->keyboard != NULL) && (seat->keyboard->focus == surface)) {
            weston_keyboard_set_focus(seat->keyboard, NULL);
        }
    }
    uifw_trace("win_mgr_reset_focus: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_set_visible: change surface visibility
 *
 * @param[in]   usurf       UIFW surface
 * @param[in]   visible     bit 0: visible(=1)/unvisible(=0)
 *                          bit 1: widht anima(=1)/without anima(=0)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_window_mgr_set_visible(struct uifw_win_surface *usurf, const int visible)
{
    int     retanima;

    if (visible & 1)    {
        if ((usurf->visible == 0) ||
            (usurf->animation.state != ICO_WINDOW_MGR_ANIMATION_STATE_NONE))    {
            uifw_trace("ico_window_mgr_set_visible: Chagne to Visible(%08x) x/y=%d/%d",
                       usurf->surfaceid, usurf->x, usurf->y);
            usurf->visible = 1;
            ico_window_mgr_set_weston_surface(usurf, usurf->x, usurf->y,
                                              usurf->width, usurf->height);
            if ((visible & 2) && (win_mgr_hook_animation != NULL))  {
                usurf->animation.pos_x = usurf->x;
                usurf->animation.pos_y = usurf->y;
                usurf->animation.pos_width = usurf->width;
                usurf->animation.pos_height = usurf->height;
                usurf->animation.no_configure = 0;
                retanima = (*win_mgr_hook_animation)(ICO_WINDOW_MGR_ANIMATION_OPSHOW,
                                                 (void *)usurf);
                uifw_trace("ico_window_mgr_set_visible: show animation = %d", retanima);
            }
            /* change unvisible to visible, restack surface list    */
            ico_window_mgr_restack_layer(usurf);
        }
    }
    else    {
        if ((usurf->visible != 0) ||
            (usurf->animation.state != ICO_WINDOW_MGR_ANIMATION_STATE_NONE))    {

            uifw_trace("ico_window_mgr_set_visible: Chagne to Unvisible(%08x)",
                       usurf->surfaceid);

            /* Reset focus              */
            win_mgr_reset_focus(usurf);

            retanima = ICO_WINDOW_MGR_ANIMATION_RET_ANIMA;
            if ((visible & 2) && (win_mgr_hook_animation != NULL))  {
                usurf->animation.pos_x = usurf->x;
                usurf->animation.pos_y = usurf->y;
                usurf->animation.pos_width = usurf->width;
                usurf->animation.pos_height = usurf->height;
                usurf->animation.no_configure = 0;
                retanima = (*win_mgr_hook_animation)(ICO_WINDOW_MGR_ANIMATION_OPHIDE,
                                                    (void *)usurf);
                uifw_trace("ico_window_mgr_set_visible: hide animation = %d", retanima);
                if (retanima != ICO_WINDOW_MGR_ANIMATION_RET_ANIMANOCTL)    {
                    usurf->visible = 0;
                    /* Weston surface configure                     */
                    ico_window_mgr_set_weston_surface(usurf, usurf->x, usurf->y,
                                                      usurf->width, usurf->height);
                }
            }
            else    {
                usurf->visible = 0;
                /* Weston surface configure                     */
                ico_window_mgr_set_weston_surface(usurf, usurf->x, usurf->y,
                                                  usurf->width, usurf->height);
            }
            ico_window_mgr_restack_layer(usurf);
        }
    }
    ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_VISIBLE,
                            usurf, usurf->visible, usurf->raise, 0, 0, 0);
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
    struct uifw_win_surface *eu;

    uifw_trace("win_mgr_set_raise: Enter(%08x,%d) layer=%d type=%x",
               (int)usurf, raise, (int)usurf->win_layer->layer, usurf->layertype);

    wl_list_remove(&usurf->ivi_layer);
    if (raise & 1)  {
        /* raise ... surface stack to top of layer          */
        if (usurf->layertype == LAYER_TYPE_INPUTPANEL)  {
            /* if input panel, top of surface list          */
            uifw_trace("win_mgr_set_raise: Raise Link to Top(InputPanel)");
            wl_list_insert(&usurf->win_layer->surface_list, &usurf->ivi_layer);
        }
        else    {
            /* if not input panel, search not input panel   */
            wl_list_for_each (eu, &usurf->win_layer->surface_list, ivi_layer)   {
                if (eu->layertype != LAYER_TYPE_INPUTPANEL) break;
            }
            uifw_trace("win_mgr_set_raise: Raise Link to Top(Normal)");
            wl_list_insert(eu->ivi_layer.prev, &usurf->ivi_layer);
        }
        usurf->raise = 1;
    }
    else    {
        /* Lower ... surface stack to bottom of layer       */
        if (usurf->layertype == LAYER_TYPE_INPUTPANEL)  {
            /* if input panel, search not input panel       */
            uifw_trace("win_mgr_set_raise: Lower Link to Bottom(InputPanel)");
            wl_list_for_each (eu, &usurf->win_layer->surface_list, ivi_layer)   {
                if (eu->layertype != LAYER_TYPE_INPUTPANEL) break;
            }
            wl_list_insert(eu->ivi_layer.prev, &usurf->ivi_layer);
        }
        else    {
            /* if not input panel, bottom of surface list   */
            uifw_trace("win_mgr_set_raise: Lower Link to Bottom(Normal)");
            wl_list_insert(usurf->win_layer->surface_list.prev, &usurf->ivi_layer);
        }
        usurf->raise = 0;
    }

    /* rebild compositor surface list               */
    if ((raise & 2) == 0)   {
        if (usurf->visible) {
            ico_window_mgr_restack_layer(usurf);
        }
        ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_VISIBLE,
                                usurf, usurf->visible, usurf->raise, 0, 0,0);
    }
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

    /* Reset focus                  */
    win_mgr_reset_focus(usurf);

    /* destory input region         */
    if (win_mgr_hook_destory)   {
        (*win_mgr_hook_destory)(usurf);
    }

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
    ico_window_mgr_restack_layer(NULL);

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

    /* if DESTROY and no send CREATE, Nop       */
    if ((event == ICO_WINDOW_MGR_WINDOW_DESTROYED) &&
        (usurf != NULL) && (usurf->created == 0))   {
        return 0;
    }

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
                           "(surf=%08x,name=%s,pid=%d,appid=%s,type=%x)", (int)mgr->resource,
                           usurf->surfaceid, usurf->winname, usurf->uclient->pid,
                           usurf->uclient->appid, usurf->layertype);
                ico_window_mgr_send_window_created(mgr->resource, usurf->surfaceid,
                                                   usurf->winname, usurf->uclient->pid,
                                                   usurf->uclient->appid,
                                                   usurf->layertype << 12);
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
                           "(surf=%08x,app=%s,node=%x,type=%x,layer=%d,"
                           "x/y=%d/%d,w/h=%d/%d,hint=%d)",
                           (int)mgr->resource, usurf->surfaceid, usurf->uclient->appid,
                           usurf->node_tbl->node, usurf->layertype,
                           usurf->win_layer->layer, param1, param2, param3, param4, param5);
                ico_window_mgr_send_window_configure(mgr->resource, usurf->surfaceid,
                                                     usurf->node_tbl->node,
                                                     usurf->layertype << 12,
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
                           "(layer=%d,visivle=%d)", (int)mgr->resource, param1, param2);
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
        if ((usurf->client_width > 0) && (usurf->client_height > 0))    {
            scalex = (float)usurf->width / (float)usurf->client_width;
            scaley = (float)usurf->height / (float)usurf->client_height;
        }
        else    {
            scalex = 1.0f;
            scaley = 1.0f;
        }
        uifw_debug("win_mgr_set_scale: %08x X=%4.2f(%d/%d) Y=%4.2f(%d/%d)",
                   usurf->surfaceid, scalex, usurf->width, usurf->client_width,
                   scaley, usurf->height, usurf->client_height);
        usurf->xadd = 0;
        usurf->yadd = 0;
        if ((ico_ivi_optionflag() & ICO_IVI_OPTION_FIXED_ASPECT) ||
            (usurf->attributes & ICO_WINDOW_MGR_ATTR_FIXED_ASPECT)) {
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
 * @brief   ico_window_mgr_is_visible: check surface visible
 *
 * @param[in]   usurf       UIFW surface
 * @return      visibility
 * @retval      =1          visible
 * @retval      =0          not visible
 * @retval      =-1         surface visible but layer not vlsible
 * @retval      =-2         surface visible but lower(currently not support)
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_window_mgr_is_visible(struct uifw_win_surface *usurf)
{
    if ((usurf->visible == 0) || (usurf->surface == NULL) || (usurf->mapped == 0))  {
        return 0;
    }
    if (usurf->win_layer->visible == 0) {
        return -1;
    }
    return 1;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_window_mgr_active_surface: set active surface
 *
 * @param[in]   surface     Weston surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_window_mgr_active_surface(struct weston_surface *surface)
{
    struct uifw_win_surface *usurf;

    /* find surface         */
    usurf = find_uifw_win_surface_by_ws(surface);
    if (! usurf) {
        uifw_trace("ico_window_mgr_active_surface: Enter(%08x)", (int)surface);
        uifw_trace("ico_window_mgr_active_surface: Leave(Not Exist)");
        return;
    }
    uifw_trace("ico_window_mgr_active_surface: Enter(%08x)", usurf->surfaceid);

    if ((usurf != _ico_win_mgr->active_pointer_usurf) ||
        (usurf != _ico_win_mgr->active_keyboard_usurf)) {

        /* set weston active surface    */
        win_mgr_set_active(usurf, ICO_WINDOW_MGR_ACTIVE_POINTER |
                           ICO_WINDOW_MGR_ACTIVE_KEYBOARD);
        /* send active event to manager     */
        (void) ico_win_mgr_send_to_mgr(ICO_WINDOW_MGR_WINDOW_ACTIVE,
                                    usurf, ICO_WINDOW_MGR_ACTIVE_SELECTED, 0,0,0,0);
    }
    uifw_trace("ico_window_mgr_active_surface: Leave(OK)");
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
    char    *p, *pp;
    char    *wrkstrp;
    char    wkbuf[ICO_UIFW_MAX_SUBJECT_NAME];
    struct ico_api_permit   *accp;
    struct ico_api_permit   *accp_next;
    int     same_accp;
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
        weston_config_section_get_int(section, "inputpanel",
                                      &_ico_ivi_inputpanel_display, 0);
    }

    /* get layer id                             */
    section = weston_config_get_section(ec->config, "ivi-layer", NULL, NULL);
    if (section)    {
        weston_config_section_get_int(section, "default", &_ico_ivi_default_layer, 1);
        weston_config_section_get_int(section, "background", &_ico_ivi_background_layer, 0);
        weston_config_section_get_int(section, "touch", &_ico_ivi_touch_layer, 101);
        weston_config_section_get_int(section, "cursor", &_ico_ivi_cursor_layer, 102);
        weston_config_section_get_int(section, "startup", &_ico_ivi_startup_layer, 103);
        weston_config_section_get_string(section, "inputpaneldeco", &wrkstrp, NULL);
        if (wrkstrp)  {
            p = strtok(wrkstrp, ";,");
            if (p)  {
                _ico_ivi_inputdeco_mag = strtol(p, (char **)0, 0);
                p = strtok(NULL, ";,");
                if (p)  {
                    _ico_ivi_inputdeco_diff = strtol(p, (char **)0, 0);
                }
            }
            free(wrkstrp);
        }
    }
    if (_ico_ivi_inputdeco_mag < 20)    _ico_ivi_inputdeco_mag = 100;

    /* get animation default                    */
    section = weston_config_get_section(ec->config, "ivi-animation", NULL, NULL);
    if (section)    {
        weston_config_section_get_string(section, "default", &_ico_ivi_animation_name, NULL);
        weston_config_section_get_string(section, "inputpanel",
                                         &_ico_ivi_inputpanel_animation, NULL);
        weston_config_section_get_int(section, "time", &_ico_ivi_animation_time, 500);
        weston_config_section_get_int(section, "fps", &_ico_ivi_animation_fps, 30);
        if (_ico_ivi_animation_name)    {
            p = strtok(_ico_ivi_animation_name, ";,");
            if (p)  {
                p = strtok(NULL, ";.");
                if (p)  {
                    _ico_ivi_animation_time = strtol(p, (char **)0, 0);
                    p = strtok(NULL, ";.");
                    if (p)  {
                        _ico_ivi_animation_fps = strtol(p, (char **)0, 0);
                    }
                }
            }
        }
        if (_ico_ivi_inputpanel_animation)  {
            p = strtok(_ico_ivi_inputpanel_animation, ";,");
            if (p)  {
                p = strtok(NULL, ",;");
                if (p)  {
                    _ico_ivi_inputpanel_anima_time = strtol(p, (char **)0, 0);
                }
            }
        }
    }
    if (_ico_ivi_animation_name == NULL)
        _ico_ivi_animation_name = (char *)"fade";
    if (_ico_ivi_inputpanel_animation == NULL)
        _ico_ivi_inputpanel_animation = (char *)"fade";
    if (_ico_ivi_animation_time < 100)  _ico_ivi_animation_time = 500;
    if (_ico_ivi_animation_fps < 3)     _ico_ivi_animation_fps = 30;
    if (_ico_ivi_inputpanel_anima_time < 100)
        _ico_ivi_inputpanel_anima_time = _ico_ivi_animation_time;

    /* get thumbnail maximum frame rate     */
    section = weston_config_get_section(ec->config, "ivi-thumbnail", NULL, NULL);
    if (section)    {
        weston_config_section_get_int(section, "gpu_accel_fps",
                                      &_ico_ivi_map_framerate_gpu, 30);
        if ((_ico_ivi_map_framerate_gpu <= 0) || (_ico_ivi_map_framerate_gpu > 60)) {
            _ico_ivi_map_framerate_gpu = 30;
        }
        weston_config_section_get_int(section, "shm_buffer_fps",
                                      &_ico_ivi_map_framerate_shm, 5);
        if ((_ico_ivi_map_framerate_shm <= 0) || (_ico_ivi_map_framerate_shm > 10)) {
            _ico_ivi_map_framerate_shm = 5;
        }
        weston_config_section_get_int(section, "no_accel_fps",
                                      &_ico_ivi_map_framerate_pixel, 10);
        if ((_ico_ivi_map_framerate_pixel <= 0) || (_ico_ivi_map_framerate_pixel > 20)) {
            _ico_ivi_map_framerate_pixel = 10;
        }
    }

    /* create ico_window_mgr management table   */
    _ico_win_mgr = (struct ico_win_mgr *)malloc(sizeof(struct ico_win_mgr));
    if (_ico_win_mgr == NULL)   {
        uifw_error("ico_window_mgr: malloc failed");
        return -1;
    }

    memset(_ico_win_mgr, 0, sizeof(struct ico_win_mgr));

    /* get access control configuration         */
    section = weston_config_get_section(ec->config, "ivi-access-control", NULL, NULL);
    accp_next = NULL;
    if (section)    {
        for (idx = 0; ; idx++)  {
            sprintf(wkbuf, "access_api.%d", idx);
            weston_config_section_get_string(section, wkbuf, &wrkstrp, NULL);
            if (wrkstrp == NULL)    {
                if (idx <= 0)   continue;
                break;
            }
            p = wrkstrp;
            for (i = 0; i < (ICO_UIFW_MAX_SUBJECT_NAME-1); i++, p++) {
                if ((*p == 0) || (*p == ';'))   break;
                wkbuf[i] = *p;
            }
            if ((i <= 0) || (*p == 0))  {
                uifw_error("ico_window_mgr: illegal config format[%s]", wrkstrp);
                free(wrkstrp);
                continue;
            }
            wkbuf[i] = 0;
            accp = _ico_win_mgr->access_config;
            while (accp)   {
                if (strcmp(wkbuf, accp->permit.subject) == 0)  break;
                accp = accp->next;
            }
            if (! accp) {
                accp = (struct ico_api_permit *) malloc(sizeof(struct ico_api_permit));
                if (! accp) {
                    uifw_error("ico_window_mgr: malloc failed");
                    continue;
                }
                same_accp = 0;
                memset(accp, 0, sizeof(struct ico_api_permit));
                strcpy(accp->permit.subject, wkbuf);
            }
            else    {
                same_accp = 1;
            }

            while (1)   {
                pp = p + 1;
                for (p++; (*p != 0) && (*p != ','); p++)    ;
                if ((strcasecmp(pp, "all") == 0) || (strcmp(pp, "*") == 0)) {
                    accp->permit.api_permit = 0xffffffff;
                    break;
                }
                if (strncasecmp(pp, "ico_", 4) == 0)            pp += 4;
                if (strncasecmp(pp, "window_mgr_", 11) == 0)    pp += 11;
                if (strncasecmp(pp, "input_mgr_", 10) == 0)     pp += 10;
                if (strncasecmp(pp, "exinput_", 8) == 0)        pp += 8;
                for (i = 0; access_api_table[i].name[0]; i++)   {
                    if (strncasecmp(pp, access_api_table[i].name, p - pp) == 0) {
                        accp->permit.api_permit |= access_api_table[i].api;
                        break;
                    }
                }
                if (*p == 0)    break;
            }
            free(wrkstrp);
            if (same_accp == 0) {
                if (accp_next)  {
                    accp_next->next = accp;
                }
                else    {
                    _ico_win_mgr->access_config = accp;
                }
                accp_next = accp;
            }
            if (strcmp(accp->permit.subject, "*") == 0) {
                _ico_win_mgr->default_access |= accp->permit.api_permit;
            }
        }
        accp = _ico_win_mgr->access_config;
        while (accp)   {
            accp->permit.api_permit |= _ico_win_mgr->default_access;
            accp = accp->next;
        }
    }
    else    {
        uifw_trace("ico_window_mgr: no access control in weston.ini");
    }

    _ico_win_mgr->surfaceid_map = (uint16_t *) malloc(INIT_SURFACE_IDS/8);
    if (! _ico_win_mgr->surfaceid_map)  {
        uifw_error("ico_window_mgr: malloc failed");
        return -1;
    }
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

    if (_ico_ivi_inputpanel_display >= _ico_num_nodes)  {
        _ico_ivi_inputpanel_display = 0;
    }
    uifw_info("ico_window_mgr: inputpanel_display=%d", _ico_ivi_inputpanel_display);

    /* set default display to ico_ivi_shell */
    ivi_shell_set_default_display(_ico_node_table[_ico_ivi_inputpanel_display].output);

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

    /* Hook to IVI-Shell                            */
    ico_ivi_shell_hook_bind(win_mgr_bind_client);
    ico_ivi_shell_hook_unbind(win_mgr_unbind_client);
    ico_ivi_shell_hook_create(win_mgr_register_surface);
    ico_ivi_shell_hook_destroy(win_mgr_destroy_surface);
    ico_ivi_shell_hook_map(win_mgr_surface_map);
    ico_ivi_shell_hook_configure(win_mgr_shell_configure);
    ico_ivi_shell_hook_select(win_mgr_select_surface);
    ico_ivi_shell_hook_title(win_mgr_set_title);
    ico_ivi_shell_hook_move(win_mgr_surface_move);
    ico_ivi_shell_hook_show_layer(win_mgr_show_layer);
    ico_ivi_shell_hook_fullscreen(win_mgr_fullscreen);

    uifw_info("ico_window_mgr: animation name=%s/%s time=%d/%d fps=%d",
              _ico_ivi_animation_name, _ico_ivi_inputpanel_animation,
              _ico_ivi_animation_time, _ico_ivi_inputpanel_anima_time,
              _ico_ivi_animation_fps);
    uifw_info("ico_window_mgr: input panel mag=%d%% diff=%d",
              _ico_ivi_inputdeco_mag,_ico_ivi_inputdeco_diff);
    uifw_info("ico_window_mgr: layer default=%d background=%d",
              _ico_ivi_default_layer, _ico_ivi_background_layer);
    uifw_info("ico_window_mgr: layer touch=%d cursor=%d startup=%d",
              _ico_ivi_touch_layer, _ico_ivi_cursor_layer, _ico_ivi_startup_layer);
    uifw_info("ico_window_mgr: thumbnail framerate gpu_accel=%d shm_buff=%d no_accel=%d",
              _ico_ivi_map_framerate_gpu, _ico_ivi_map_framerate_shm,
              _ico_ivi_map_framerate_pixel);
    uifw_info("ico_window_mgr: option flag=0x%04x log level=%d debug flag=0x%04x",
              _ico_ivi_option_flag, _ico_ivi_debug_level & 0x0ffff,
              (_ico_ivi_debug_level >> 16) & 0x0ffff);
    accp = _ico_win_mgr->access_config;
    while (accp)    {
        uifw_info("ico_window_mgr: access_control %s=%08x",
                  accp->permit.subject, accp->permit.api_permit);
        accp = accp->next;
    }

    /* get GPU type for H/W support of the thumbnail acquisition    */
    if ((ico_ivi_optionflag() & ICO_IVI_OPTION_GPU_DEPEND) == 0)    {
        /* can not use GPU H/W dependent acceleration   */
        _ico_ivi_gpu_type = ICO_GPUTYPE_NOACCELERATION;
        uifw_info("ico_window_mgr: GPU type=No Acceleration by option flag");
    }
    else    {
        _ico_ivi_gpu_type = ICO_GPUTYPE_NOACCELERATION;

        p = (char *)glGetString(GL_RENDERER);
        if (p)  {
            uifw_info("ico_window_mgr: Renderer=<%s>", p);
            for (i = 0; ico_window_mgr_gpu_type[i].gpu_type; i++)   {
                if (strncmp(p, ico_window_mgr_gpu_type[i].gpu_name,
                            strlen(ico_window_mgr_gpu_type[i].gpu_name)) == 0)  {
                    _ico_ivi_gpu_type = ico_window_mgr_gpu_type[i].gpu_type;
                    uifw_info("ico_window_mgr: GPU type=Acceleration %d", _ico_ivi_gpu_type);
                    break;
                }
            }
        }
        if (_ico_ivi_gpu_type == ICO_GPUTYPE_NOACCELERATION)    {
            uifw_info("ico_window_mgr: GPU type=No Acceleration(can not find GPU)");
        }
    }
    uifw_info("ico_window_mgr: Leave(module_init)");

    return 0;
}
