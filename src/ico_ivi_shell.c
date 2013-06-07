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
 * @brief   Weston(Wayland) IVI Shell
 * @brief   Shell for IVI(In-Vehicle Infotainment).
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
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

#include <wayland-server.h>
#include <weston/compositor.h>
#include "ico_ivi_common.h"
#include "ico_ivi_shell.h"
#include "ico_ivi_shell-server-protocol.h"

/* Layer management                 */
struct  ivi_layer_list  {
    int     layer;                  /* Layer.ID                             */
    int     visible;
    struct wl_list surface_list;    /* Surfacae list                        */
    struct wl_list link;            /* Link pointer for layer list          */
};

/* Static table for Shell           */
struct shell_surface;
struct ivi_shell {
    struct weston_compositor *compositor;
    struct wl_listener destroy_listener;
    struct weston_layer surface;            /* Surface list                 */
    struct ivi_layer_list ivi_layer;        /* Layer list                   */
    char win_animation[ICO_WINDOW_ANIMATION_LEN];
                                            /* Default animation name       */
    int win_animation_time;                 /* animation time(ms)           */
    int win_animation_fps;                  /* animation frame rate(fps)    */
    int win_visible_on_create;              /* Visible on create surface    */
    struct shell_surface *active_pointer_shsurf;
                                            /* Pointer active shell surface */
    struct shell_surface *active_keyboard_shsurf;
                                            /* Keyboard active shell surface*/
};

/* Surface type                     */
enum shell_surface_type {
    SHELL_SURFACE_NONE,             /* Surface type undefine                */
    SHELL_SURFACE_TOPLEVEL,         /* Top level surface for application    */
    SHELL_SURFACE_TRANSIENT,        /* Child surface                        */
    SHELL_SURFACE_FULLSCREEN,       /* Full screen surface                  */
    SHELL_SURFACE_MAXIMIZED,        /* maximum screen                       */
    SHELL_SURFACE_POPUP             /* pop up screen                        */
};

/* Shell surface table              */
struct shell_surface {
    struct wl_resource  resource;

    struct weston_surface *surface;
    struct wl_listener surface_destroy_listener;
    struct weston_surface *parent;
    struct ivi_shell *shell;

    enum    shell_surface_type type;
    enum    shell_surface_type next_type;
    char    *title;
    char    *class;

    int     geometry_x;
    int     geometry_y;
    int     geometry_width;
    int     geometry_height;
    char    visible;
    char    mapped;
    char    noconfigure;
    char    restrain;
    struct ivi_layer_list *layer_list;
    struct wl_list        ivi_layer;

    struct {
        unsigned short  x;
        unsigned short  y;
        unsigned short  width;
        unsigned short  height;
    }       configure_app;

    struct {
        struct weston_transform transform;
        struct weston_matrix rotation;
    } rotation;

    struct {
        int32_t  x;
        int32_t  y;
        uint32_t flags;
    } transient;

    struct wl_list link;
    struct wl_client    *wclient;
    const struct weston_shell_client *client;
};

static struct ivi_shell *default_shell = NULL;


/* static function prototype    */
static void bind_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id);
static void unbind_shell(struct wl_resource *resource);
static struct shell_surface *get_shell_surface(struct weston_surface *surface);
static struct ivi_shell *shell_surface_get_shell(struct shell_surface *shsurf);
static void ivi_shell_restack_ivi_layer(struct ivi_shell *shell,
                                        struct shell_surface *shsurf);

static void (*shell_hook_bind)(struct wl_client *client) = NULL;
static void (*shell_hook_unbind)(struct wl_client *client) = NULL;
static void (*shell_hook_create)(struct wl_client *client, struct wl_resource *resource,
                                 struct weston_surface *surface,
                                 struct shell_surface *shsurf) = NULL;
static void (*shell_hook_destroy)(struct weston_surface *surface) = NULL;
static void (*shell_hook_map)(struct weston_surface *surface, int32_t *width,
                              int32_t *height, int32_t *sx, int32_t *sy) = NULL;
static void (*shell_hook_change)(struct weston_surface *surface, const int to,
                                 const int manager) = NULL;
static void (*shell_hook_select)(struct weston_surface *surface) = NULL;


/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_configuration: initiale configuration ico_ivi_shell
 *
 * @param[in]   shell   ico_ivi_shell static table area
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_configuration(struct ivi_shell *shell)
{
    int     config_fd;
    char    *win_animation = NULL;
    int     win_animation_time = 800;
    int     win_animation_fps = 15;

    struct config_key shell_keys[] = {
        { "animation",          CONFIG_KEY_STRING, &win_animation },
        { "animation_time",     CONFIG_KEY_INTEGER, &win_animation_time },
        { "animation_fps",      CONFIG_KEY_INTEGER, &win_animation_fps },
        { "visible_on_create",  CONFIG_KEY_INTEGER, &shell->win_visible_on_create },
    };

    struct config_section cs[] = {
        { "shell", shell_keys, ARRAY_LENGTH(shell_keys), NULL },
    };

    config_fd = open_config_file(ICO_IVI_PLUGIN_CONFIG);
    parse_config_file(config_fd, cs, ARRAY_LENGTH(cs), shell);
    close(config_fd);

    if (win_animation)  {
        strncpy(shell->win_animation, win_animation, sizeof(shell->win_animation)-1);
    }
    if (win_animation_time < 100)   win_animation_time = 100;
    shell->win_animation_time = win_animation_time;
    if (win_animation_fps > 30)     win_animation_fps = 30;
    if (win_animation_fps < 5)      win_animation_fps = 5;
    shell->win_animation_fps = win_animation_fps;
    uifw_info("shell_configuration: Anima=%s,%dms,%dfps Visible=%d Debug=%d",
              shell->win_animation, shell->win_animation_time, shell->win_animation_fps,
              shell->win_visible_on_create, ico_ivi_debuglevel());
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   send_configure: send configure(resize) event to client applicstion
 *
 * @param[in]   surface     weston surface
 * @param[in]   edges       surface resize position
 * @param[in]   width       surface width
 * @param[in]   height      surface height
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
send_configure(struct weston_surface *surface,
               uint32_t edges, int32_t width, int32_t height)
{
    struct shell_surface *shsurf = get_shell_surface(surface);

    uifw_trace("send_configure: %08x edges=%x w/h=%d/%d map=%d",
               (int)shsurf->surface, edges, width, height, shsurf->mapped);
    if (shsurf->mapped == 0)    return;

    shsurf->configure_app.width = width;
    shsurf->configure_app.height = height;
    wl_shell_surface_send_configure(&shsurf->resource,
                                    edges, width, height);
}

static const struct weston_shell_client shell_client = {
    send_configure
};

/*--------------------------------------------------------------------------*/
/**
 * @brief   reset_shell_surface_type: reset surface type
 *
 * @param[in]   shsurf      shell surface
 * @return      always 0
 */
/*--------------------------------------------------------------------------*/
static int
reset_shell_surface_type(struct shell_surface *shsurf)
{
    uifw_trace("reset_shell_surface_type: [%08x]", (int)shsurf);
    shsurf->type = SHELL_SURFACE_NONE;
    return 0;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   set_surface_type: set surface type
 *
 * @param[in]   shsurf      shell surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
set_surface_type(struct shell_surface *shsurf)
{
    struct weston_surface *surface = shsurf->surface;
    struct weston_surface *pes = shsurf->parent;
    struct shell_surface *psh;

    uifw_trace("set_surface_type: [%08x] (%08x) type=%x",
               (int)shsurf, (int)surface, (int)shsurf->next_type);

    reset_shell_surface_type(shsurf);

    shsurf->type = shsurf->next_type;
    shsurf->next_type = SHELL_SURFACE_NONE;

    switch (shsurf->type) {
    case SHELL_SURFACE_TOPLEVEL:
        break;
    case SHELL_SURFACE_TRANSIENT:
        psh = get_shell_surface(pes);
        if (psh)    {
            shsurf->geometry_x = psh->geometry_x + shsurf->transient.x;
            shsurf->geometry_y = psh->geometry_y + shsurf->transient.y;
        }
        else    {
            shsurf->geometry_x = pes->geometry.x + shsurf->transient.x;
            shsurf->geometry_y = pes->geometry.y + shsurf->transient.y;
        }
        break;
    default:
        break;
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_pong: recceive pong(ping reply) (NOP)
 *
 * @param[in]   client      wayland client
 * @param[in]   resource    pong resource
 * @param[in]   serial      event serial number
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_surface_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial)
{
    uifw_trace("shell_surface_pong: NOP[%08x]", (int)resource->data);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_move: recceive move request (NOP)
 *
 * @param[in]   client          wayland client
 * @param[in]   resource        move request resource
 * @param[in]   seat_resource   seat resource
 * @param[in]   serial          event serial number
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_surface_move(struct wl_client *client, struct wl_resource *resource,
                   struct wl_resource *seat_resource, uint32_t serial)
{
    uifw_trace("shell_surface_move: [%08x] NOP", (int)resource->data);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_resize: recceive resize request (NOP)
 *
 * @param[in]   client          wayland client
 * @param[in]   resource        resize request resource
 * @param[in]   seat_resource   seat resource
 * @param[in]   serial          event serial number
 * @param[in]   edges           resize position
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_surface_resize(struct wl_client *client, struct wl_resource *resource,
                     struct wl_resource *seat_resource, uint32_t serial,
                     uint32_t edges)
{
    uifw_trace("shell_surface_resize: [%08x] NOP", (int)resource->data);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   set_toplevel: set surface to TopLevel
 *
 * @param[in]   shsurf          shell surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
set_toplevel(struct shell_surface *shsurf)
{
    shsurf->next_type = SHELL_SURFACE_TOPLEVEL;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_set_toplevel: set surface to TopLevel(client interface)
 *
 * @param[in]   client          wayland client
 * @param[in]   resource        set toplevel request resource
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_surface_set_toplevel(struct wl_client *client, struct wl_resource *resource)
{
    struct shell_surface *shsurf = resource->data;

    uifw_trace("shell_surface_set_toplevel: Set TopLevel[%08x] surf=%08x",
               (int)shsurf, (int)shsurf->surface);

    set_toplevel(shsurf);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   set_transient: set surface to child
 *
 * @param[in]   shsurf          shell surface
 * @param[in]   parent          parent surface
 * @param[in]   x               relative X position from a parent
 * @param[in]   y               relative Y position from a parent
 * @param[in]   flags           flag(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
set_transient(struct shell_surface *shsurf,
              struct weston_surface *parent, int x, int y, uint32_t flags)
{
    /* assign to parents output */
    shsurf->parent = parent;
    shsurf->transient.x = x;
    shsurf->transient.y = y;
    shsurf->transient.flags = flags;
    shsurf->next_type = SHELL_SURFACE_TRANSIENT;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_set_transient: set surface to child(client interface)
 *
 * @param[in]   client          wayland client
 * @param[in]   resource        set transient request resource
 * @param[in]   parent_resource parent surface resource
 * @param[in]   x               relative X position from a parent
 * @param[in]   y               relative Y position from a parent
 * @param[in]   flags           flag(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_surface_set_transient(struct wl_client *client, struct wl_resource *resource,
                            struct wl_resource *parent_resource,
                            int x, int y, uint32_t flags)
{
    struct shell_surface *shsurf = resource->data;
    struct weston_surface *parent = parent_resource->data;

    uifw_trace("shell_surface_set_transient: Set Transient[%08x] surf=%08x",
               (int)shsurf, (int)shsurf->surface);
    set_transient(shsurf, parent, x, y, flags);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_set_fullscreen: set surface to full screen(same as toplevel)
 *
 * @param[in]   client          wayland client
 * @param[in]   resource        set fullscreen request resource
 * @param[in]   method          method(unused)
 * @param[in]   framerate       frame rate(unused)
 * @param[in]   output_resource output resource(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_surface_set_fullscreen(struct wl_client *client, struct wl_resource *resource,
                             uint32_t method, uint32_t framerate,
                             struct wl_resource *output_resource)
{
    struct shell_surface *shsurf = resource->data;
    uifw_trace("shell_surface_set_fullscreen: "
               "NOP(same as set_toplevel)[%08x]", (int)shsurf);
    shsurf->next_type = SHELL_SURFACE_FULLSCREEN;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_set_popup: set surface to popup(same as toplevel)
 *
 * @param[in]   client          wayland client
 * @param[in]   resource        set popup request resource
 * @param[in]   seat_resource   seat resource(unused)
 * @param[in]   serial          event serial number(unused)
 * @param[in]   parent_resource parent resource(unused)
 * @param[in]   x               relative X position from a parent(unused)
 * @param[in]   y               relative Y position from a parent(unused)
 * @param[in]   flags           flag(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_surface_set_popup(struct wl_client *client, struct wl_resource *resource,
                        struct wl_resource *seat_resource, uint32_t serial,
                        struct wl_resource *parent_resource,
                        int32_t x, int32_t y, uint32_t flags)
{
    struct shell_surface *shsurf = resource->data;
    uifw_trace("shell_surface_set_popup: NOP(same as set_toplevel)[%08x]", (int)shsurf);
    shsurf->next_type = SHELL_SURFACE_POPUP;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_set_maximized: set surface to maximized(same as toplevel)
 *
 * @param[in]   client          wayland client
 * @param[in]   resource        set maximized request resource
 * @param[in]   output_resource output resource(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_surface_set_maximized(struct wl_client *client, struct wl_resource *resource,
                            struct wl_resource *output_resource )
{
    struct shell_surface *shsurf = resource->data;
    uifw_trace("shell_surface_set_maximized: NOP(same as set_toplevel)[%08x]", (int)shsurf);
    shsurf->next_type = SHELL_SURFACE_MAXIMIZED;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_set_title: set surface title
 *
 * @param[in]   client      wayland client
 * @param[in]   resource    set title request resource
 * @param[in]   title       surface title
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_surface_set_title(struct wl_client *client,
                        struct wl_resource *resource, const char *title)
{
    struct shell_surface *shsurf = resource->data;

    uifw_trace("shell_surface_set_title: [%08x] %s", (int)shsurf, title);

    if (shsurf->title)  {
        free(shsurf->title);
    }
    shsurf->title = strdup(title);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_set_class: set surface class name
 *
 * @param[in]   client      wayland client
 * @param[in]   resource    set class request resource
 * @param[in]   class       surface class name
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_surface_set_class(struct wl_client *client,
                        struct wl_resource *resource, const char *class)
{
    struct shell_surface *shsurf = resource->data;

    uifw_trace("shell_surface_set_class: [%08x] %s", (int)shsurf, class);

    if (shsurf->class)  {
        free(shsurf->class);
    }
    shsurf->class = strdup(class);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_raise: raise surface
 *
 * @param[in]   client      wayland client
 * @param[in]   resource    set class request resource
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_surface_raise(struct wl_client *client, struct wl_resource *resource)
{
    struct shell_surface *shsurf = resource->data;

    uifw_trace("shell_surface_raise: [%08x]", (int)shsurf);

    ivi_shell_set_raise(shsurf, 1);
}

static const struct wl_shell_surface_interface shell_surface_implementation = {
    shell_surface_pong,
    shell_surface_move,
    shell_surface_resize,
    shell_surface_set_toplevel,
    shell_surface_set_transient,
    shell_surface_set_fullscreen,
    shell_surface_set_popup,
    shell_surface_set_maximized,
    shell_surface_set_title,
    shell_surface_set_class,
    shell_surface_raise
};

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_get_shell: get ico_ivi_shell static table
 *
 * @param[in]   shsurf      shell surface(if NULL, return default)
 * @return      ico_ivi_shell static table address
 */
/*--------------------------------------------------------------------------*/
static struct ivi_shell *
shell_surface_get_shell(struct shell_surface *shsurf)
{
    if (shsurf) {
        return shsurf->shell;
    }
    return default_shell;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   destroy_shell_surface: destroy surface
 *
 * @param[in]   shsurf      shell surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
destroy_shell_surface(struct shell_surface *shsurf)
{
    uifw_trace("destroy_shell_surface: Enter[%08x]", (int)shsurf);

    if (shsurf->visible != FALSE)   {
        shsurf->visible = FALSE;
        ivi_shell_restack_ivi_layer(shell_surface_get_shell(shsurf), shsurf);
    }

    wl_list_remove(&shsurf->ivi_layer);

    if (shell_hook_destroy) {
        /* call sufrace destory hook routine    */
        uifw_trace("destroy_shell_surface: call ivi_shell_hook_destroy(%08x)",
                   (int)shsurf->surface);
        (void) (*shell_hook_destroy) (shsurf->surface);
        uifw_trace("destroy_shell_surface: ret");
    }

    wl_list_remove(&shsurf->surface_destroy_listener.link);
    shsurf->surface->configure = NULL;

    wl_list_remove(&shsurf->link);
    free(shsurf);

    uifw_trace("destroy_shell_surface: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_destroy_shell_surface: destroy surface(client interface)
 *
 * @param[in]   resource    destroy request resource
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_destroy_shell_surface(struct wl_resource *resource)
{
    struct shell_surface *shsurf = resource->data;

    uifw_trace("shell_destroy_shell_surface: Enter [%08x]", (int)shsurf);

    destroy_shell_surface(shsurf);

    uifw_trace("shell_destroy_shell_surface: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_handle_surface_destroy: destroy surface(listener interface)
 *
 * @param[in]   listener    listener
 * @param[in]   data        user data(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct shell_surface *shsurf = container_of(listener, struct shell_surface,
                                                surface_destroy_listener);

    uifw_trace("shell_handle_surface_destroy: Enter [%08x] data=%08x",
               (int)shsurf, (int)data);

    if (shsurf->resource.client) {
        wl_resource_destroy(&shsurf->resource);
    } else {
        wl_signal_emit(&shsurf->resource.destroy_signal, &shsurf->resource);
        destroy_shell_surface(shsurf);
    }
    uifw_trace("shell_handle_surface_destroy: Leave");
}

static void
shell_surface_configure(struct weston_surface *, int32_t, int32_t);

/*--------------------------------------------------------------------------*/
/**
 * @brief   get_shell_surface: get shell surface
 *
 * @param[in]   surface     weston surface
 * @return      shell surface address
 */
/*--------------------------------------------------------------------------*/
static struct shell_surface *
get_shell_surface(struct weston_surface *surface)
{
    if (surface->configure == shell_surface_configure)
        return surface->private;
    else
        return NULL;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   create_shell_surface: create shell surface
 *
 * @param[in]   surface     weston surface
 * @param[in]   client      client
 * @return      created shell surface address
 */
/*--------------------------------------------------------------------------*/
static  struct shell_surface *
create_shell_surface(void *shell, struct weston_surface *surface,
                     const struct weston_shell_client *client)
{
    struct shell_surface *shsurf;

    if (surface->configure) {
        uifw_warn("create_shell_surface: surface->configure already set");
        return NULL;
    }

    shsurf = calloc(1, sizeof *shsurf);
    if (!shsurf) {
        uifw_error("create_shell_surface: no memory to allocate shell surface");
        return NULL;
    }

    uifw_trace("create_shell_surface: (%08x) [%08x] client=%08x (visible=%d)",
               (int)surface, (int)shsurf, (int)client,
               ((struct ivi_shell *)shell)->win_visible_on_create);

    surface->configure = shell_surface_configure;
    surface->private = shsurf;

    shsurf->shell = (struct ivi_shell *) shell;
    shsurf->surface = surface;
    shsurf->visible = shsurf->shell->win_visible_on_create;

    /* set default color and shader */
    weston_surface_set_color(surface, 0.0, 0.0, 0.0, 1);

    wl_signal_init(&shsurf->resource.destroy_signal);
    shsurf->surface_destroy_listener.notify = shell_handle_surface_destroy;
    wl_signal_add(&surface->surface.resource.destroy_signal,
                  &shsurf->surface_destroy_listener);

    /* init link so its safe to always remove it in destroy_shell_surface */
    wl_list_init(&shsurf->link);

    /* empty when not in use */
    wl_list_init(&shsurf->rotation.transform.link);
    weston_matrix_init(&shsurf->rotation.rotation);

    shsurf->type = SHELL_SURFACE_NONE;
    shsurf->next_type = SHELL_SURFACE_NONE;

    shsurf->client = client;

    wl_list_init(&shsurf->ivi_layer);

    return shsurf;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_get_shell_surface: create shell surface(client interface)
 *
 * @param[in]   client              client
 * @param[in]   resource            get shell surface request resource
 * @param[in]   id                  created shell surface object id in the client
 * @param[in]   surface_resource    weston surface resource
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_get_shell_surface(struct wl_client *client, struct wl_resource *resource,
                        uint32_t id, struct wl_resource *surface_resource)
{
    struct weston_surface *surface = surface_resource->data;
    struct ivi_shell *shell = resource->data;
    struct shell_surface *shsurf;

    if (get_shell_surface(surface)) {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "ivi_shell::get_shell_surface already requested");
        return;
    }

    uifw_trace("shell_get_shell_surface: Enter (%08x) client=%08x",
               (int)surface, (int)client);

    shsurf = create_shell_surface(shell, surface, &shell_client);
    if (!shsurf) {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "surface->configure already set");
        return;
    }

    shsurf->wclient = client;
    shsurf->resource.destroy = shell_destroy_shell_surface;
    shsurf->resource.object.id = id;
    shsurf->resource.object.interface = &wl_shell_surface_interface;
    shsurf->resource.object.implementation =
        (void (**)(void)) &shell_surface_implementation;
    shsurf->resource.data = shsurf;

    wl_client_add_resource(client, &shsurf->resource);

    wl_list_init(&shsurf->ivi_layer);
    uifw_trace("shell_get_shell_surface: Init shsurf(%08x) weston_surf=%08x",
               (int)shsurf, (int)surface);

    if (shell_hook_create)  {
        /* call surface create hook routine     */
        uifw_trace("shell_get_shell_surface: call ivi_shell_hook_create(%08x,,%08x,%08x)",
                   (int)client, (int)surface, (int)shsurf);
        (void) (*shell_hook_create)(client, resource, surface, shsurf);
        uifw_trace("shell_get_shell_surface: ret  ivi_shell_hook_create");
    }
    uifw_trace("shell_get_shell_surface: Leave");
}

static const struct wl_shell_interface shell_implementation = {
    shell_get_shell_surface
};

/*--------------------------------------------------------------------------*/
/**
 * @brief   weston_surface_set_initial_position: set surface initial position
 *
 * @param[in]   surface     weston surface
 * @param[in]   shell       ico_ivi_shell static table address
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
weston_surface_set_initial_position(struct weston_surface *surface, struct ivi_shell *shell)
{
    struct weston_output *output, *target_output;

    if (shell->win_visible_on_create)   {
        wl_list_for_each (output, &shell->compositor->output_list, link)    {
            target_output = output;
            break;
        }
        if (! target_output)    {
            weston_surface_set_position(surface, (float)(10 + random() % 400),
                                        (float)(10 + random() % 400));
        }
        else    {
            int range_x = target_output->width - surface->geometry.width;
            int range_y = target_output->height - surface->geometry.height;
            if (range_x < 0)    range_x = 400;
            if (range_y < 0)    range_y = 400;
            weston_surface_set_position(surface, (float)(random() % range_x),
                                        (float)(random() % range_y));
        }
    }
    else    {
        weston_surface_set_position(surface, (float)0.0, (float)0.0);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   map: surface initial mapping to screen
 *
 * @param[in]   shell       ico_ivi_shell static table address
 * @param[in]   surface     weston surface
 * @param[in]   width       surface width
 * @param[in]   height      surface height
 * @param[in]   sx          surface upper-left X position on screen
 * @param[in]   sy          surface upper-left Y position on screen
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
map(struct ivi_shell *shell, struct weston_surface *surface,
    int32_t width, int32_t height, int32_t sx, int32_t sy)
{
    struct shell_surface *shsurf = get_shell_surface(surface);
    enum shell_surface_type surface_type = shsurf->type;
    struct weston_surface *parent;

    uifw_trace("map: Enter(%08x) sx/sy=%d/%d, w/h=%d/%d",
               (int)surface, ((int)sx)/256, ((int)sy)/256, width, height);

    shsurf->mapped = 1;
    surface->geometry.width = width;
    surface->geometry.height = height;
    shsurf->geometry_x = sx;
    shsurf->geometry_y = sy;
    shsurf->geometry_width = width;
    shsurf->geometry_height = height;
    surface->geometry.dirty = 1;

    /* initial positioning, see also configure() */
    switch (surface_type) {
    case SHELL_SURFACE_TOPLEVEL:
        weston_surface_set_initial_position(surface, shell);
        uifw_trace("map: TopLevel x/y=%d/%d w/h=%d/%d",
                   (int)surface->geometry.x, (int)surface->geometry.y,
                   (int)surface->geometry.width, (int)surface->geometry.height);
        shsurf->geometry_x = surface->geometry.x;
        shsurf->geometry_x = surface->geometry.y;
        break;
    case SHELL_SURFACE_NONE:
        weston_surface_set_position(surface, (float)(surface->geometry.x + sx),
                                    (float)(surface->geometry.y + sy));
        break;
    default:
        ;
    }
    if ((ico_option_flag() & ICO_OPTION_FLAG_UNVISIBLE) && (shsurf->visible == FALSE))   {
        surface->geometry.x = (float)(ICO_IVI_MAX_COORDINATE+1);
        surface->geometry.y = (float)(ICO_IVI_MAX_COORDINATE+1);
        surface->geometry.dirty = 1;
    }

    switch (surface_type) {
    case SHELL_SURFACE_TRANSIENT:
        parent = shsurf->parent;
        wl_list_insert(parent->layer_link.prev, &surface->layer_link);
        break;
    case SHELL_SURFACE_NONE:
        break;
    default:
        if (! shsurf->layer_list)   {
            ivi_shell_set_layer(shsurf, ICO_IVI_DEFAULT_LAYER);
        }
        break;
    }

    if (surface_type != SHELL_SURFACE_NONE) {
        weston_surface_update_transform(surface);
        ivi_shell_restack_ivi_layer(shell, shsurf);
    }

    if (shell_hook_map) {
        /* Surface map hook routine         */
        uifw_trace("map: call ivi_shell_hook_map(%08x, x/y=%d/%d, w/h=%d/%d)",
                   (int)surface, sx, sy, width, height);
        (void) (*shell_hook_map) (surface, &width, &height, &sx, &sy);
        uifw_trace("map: ret  ivi_shell_hook_map(%08x, x/y=%d/%d, w/h=%d/%d)",
                   (int)surface, sx, sy, width, height);
    }

    if (shell_hook_change)  {
        /* Surface change hook routine      */
        uifw_trace("map: call ivi_shell_hook_change(%08x)", (int)surface);
        (void) (*shell_hook_change)(surface, -1, 1);    /* Send to Manager  */
        uifw_trace("map: ret  ivi_shell_hook_change")
    }
    uifw_trace("map: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   configure: surface change
 *
 * @param[in]   shell       ico_ivi_shell static table address
 * @param[in]   surface     weston surface
 * @param[in]   x           surface upper-left X position on screen
 * @param[in]   y           surface upper-left Y position on screen
 * @param[in]   width       surface width
 * @param[in]   height      surface height
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
configure(struct ivi_shell *shell, struct weston_surface *surface,
          GLfloat x, GLfloat y, int32_t width, int32_t height)
{
    enum shell_surface_type surface_type = SHELL_SURFACE_NONE;
    struct shell_surface *shsurf;

    shsurf = get_shell_surface(surface);

    uifw_trace("configure: Enter(%08x) [%08x] x/y=%d/%d, w/h=%d/%d",
               (int)surface, (int)shsurf, (int)x, (int)y, width, height);

    if (shsurf) {
        surface_type = shsurf->type;
        shsurf->geometry_x = (int)x;
        shsurf->geometry_y = (int)y;
        shsurf->geometry_width = width;
        shsurf->geometry_height = height;
        ivi_shell_surface_configure(shsurf, x, y, width, height);
    }
    else    {
        weston_surface_configure(surface, x, y, width, height);
    }

    if (surface->output) {
        weston_surface_update_transform(surface);
    }

    uifw_trace("configure: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_surface_configure: shell surface change
 *
 * @param[in]   es          weston surface
 * @param[in]   sx          surface upper-left X position on screen
 * @param[in]   sy          surface upper-left Y position on screen
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_surface_configure(struct weston_surface *es, int32_t sx, int32_t sy)
{
    struct shell_surface *shsurf = get_shell_surface(es);
    struct ivi_shell *shell = shsurf->shell;
    int     type_changed = 0;
    int     num_mgr;
    int     dx, dy, dw, dh;

    uifw_trace("shell_surface_configure: Enter(surf=%08x out=%08x buf=%08x)",
               (int)es, (int)es->output, (int)es->buffer);

    if (shsurf->restrain)   {
        uifw_trace("shell_surface_configure: Leave(restrain)");
        return;
    }

    if (shsurf->next_type != SHELL_SURFACE_NONE &&
        shsurf->type != shsurf->next_type) {
        set_surface_type(shsurf);
        type_changed = 1;
    }

    if (! weston_surface_is_mapped(es)) {
        if ((es->geometry.width > 0) && (es->geometry.height >0))   {
            uifw_trace("shell_surface_configure: map Surface size(sx/sy=%d/%d w/h=%d/%d)",
                       sx, sy, es->buffer->width, es->buffer->height);
            map(shell, es, es->geometry.width, es->geometry.height, sx, sy);
        }
        else    {
            uifw_trace("shell_surface_configure: map Buffer size(sx/sy=%d/%d w/h=%d/%d)",
                       sx, sy, es->buffer->width, es->buffer->height);
            map(shell, es, es->buffer->width, es->buffer->height, sx, sy);
        }
    }
    else    {
        if ((shsurf->mapped == 0) && (es->buffer != NULL))  {
            if ((es->geometry.width > 0) && (es->geometry.height >0))   {
                uifw_trace("shell_surface_configure: map Surface size(sx/sy=%d/%d w/h=%d/%d)",
                           sx, sy, es->buffer->width, es->buffer->height);
                map(shell, es, es->geometry.width, es->geometry.height, sx, sy);
            }
            else    {
                uifw_trace("shell_surface_configure: map Buffer size(sx/sy=%d/%d w/h=%d/%d)",
                           sx, sy, es->buffer->width, es->buffer->height);
                map(shell, es, es->buffer->width, es->buffer->height, sx, sy);
            }
        }

        GLfloat from_x, from_y;
        GLfloat to_x, to_y;

        weston_surface_to_global_float(es, 0, 0, &from_x, &from_y);
        weston_surface_to_global_float(es, sx, sy, &to_x, &to_y);

        if ((es->geometry.width <= 0) || (es->geometry.height <= 0))    {
            num_mgr = 0;
        }
        else    {
            /* Surface change request from App  */
            uifw_trace("shell_surface_configure: App request change(sx/sy=%d/%d w/h=%d/%d)",
                       sx, sy, es->buffer->width, es->buffer->height);
            dx = shsurf->geometry_x;
            dy = shsurf->geometry_y;
            dw = shsurf->geometry_width;
            dh = shsurf->geometry_height;
            if (dw > es->buffer->width) {
                dw = es->buffer->width;
                dx = shsurf->geometry_x + (shsurf->geometry_width - dw)/2;
            }
            if (dh > es->buffer->height)   {
                dh = es->buffer->height;
                dy = shsurf->geometry_y + (shsurf->geometry_height - dh)/2;
            }
            weston_surface_configure(es, dx, dy, dw, dh);
            ivi_shell_surface_configure(shsurf, es->geometry.x, es->geometry.y,
                                        es->geometry.width, es->geometry.height);
            uifw_trace("shell_surface_configure: w/h=%d/%d->%d/%d x/y=%d/%d->%d/%d",
                       shsurf->geometry_width, shsurf->geometry_height,
                       es->geometry.width, es->geometry.height,
                       shsurf->geometry_x, shsurf->geometry_y,
                       (int)es->geometry.x, (int)es->geometry.y);
            num_mgr = ico_ivi_send_surface_change(es,
                                                  shsurf->geometry_x + to_x -from_x,
                                                  shsurf->geometry_y + to_y - from_y,
                                                  es->buffer->width, es->buffer->height);
            uifw_trace("shell_surface_configure: ret ivi_shell_hook_change(%d)", num_mgr)
        }
        if (num_mgr <= 0)   {
            /* manager not exist, change surface        */
            uifw_trace("shell_surface_configure: configure to Buffer size(no Manager) "
                       "x=%d+%d-%d y=%d+%d-%d",
                       (int)es->geometry.x, (int)to_x, (int)from_x,
                       (int)es->geometry.y, (int)to_y, (int)from_y);
            if ((es->geometry.x > ICO_IVI_MAX_COORDINATE) &&
                (es->geometry.y > ICO_IVI_MAX_COORDINATE) &&
                (shsurf->visible))  {
                es->geometry.x = 0;
                es->geometry.y = 0;
                es->geometry.dirty = 1;
            }
            configure(shell, es,
                      es->geometry.x + to_x - from_x,
                      es->geometry.y + to_y - from_y,
                      es->buffer->width, es->buffer->height);
        }
    }
    uifw_trace("shell_surface_configure: Leave(surf=%08x out=%08x buf=%08x)",
               (int)es, (int)es->output, (int)es->buffer);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   bind_shell: client bind shell
 *
 * @param[in]   client      client(ex.HomeScreen)
 * @param[in]   data        user data(ico_ivi_shell static table address)
 * @param[in]   version     interface version number(unused)
 * @param[in]   id          client object id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
bind_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct ivi_shell *shell = data;
    struct wl_resource *resource;

    uifw_trace("bind_shell: client=%08x id=%d", (int)client, (int)id);

    resource = wl_client_add_object(client, &wl_shell_interface,
                                    &shell_implementation, id, shell);

    resource->destroy = unbind_shell;

    if (shell_hook_bind)    {
        (*shell_hook_bind)(client);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   unbind_shell: client unbind shell
 *
 * @param[in]   resource    unbind request resource
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
unbind_shell(struct wl_resource *resource)
{
    uifw_trace("unbind_shell");

    if (shell_hook_unbind)  {
        (*shell_hook_unbind)(resource->client);
    }
    free(resource);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   unbind_ivi_shell: client unbind ico_ivi_shell
 *
 * @param[in]   resource    unbind request resource
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
unbind_ivi_shell(struct wl_resource *resource)
{
    uifw_trace("unbind_ivi_shell");
    free(resource);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   bind_ivi_shell: client bind ico_ivi_shell
 *
 * @param[in]   client      client(ex.HomeScreen)
 * @param[in]   data        user data(ico_ivi_shell static table address)
 * @param[in]   version     interface version number(unused)
 * @param[in]   id          client object id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
bind_ivi_shell(struct wl_client *client,
               void *data, uint32_t version, uint32_t id)
{
    struct ivi_shell *shell = data;
    struct wl_resource *resource;

    resource = wl_client_add_object(client, &ico_ivi_shell_interface,
                                    NULL, id, shell);

    uifw_trace("bind_ivi_shell: client=%08x id=%d", (int)client, (int)id);

    resource->destroy = unbind_ivi_shell;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   shell_destroy: destroy ico_ivi_shell
 *
 * @param[in]   listener    shell destroy listener
 * @param[in]   data        user data(unused)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
shell_destroy(struct wl_listener *listener, void *data)
{
    struct ivi_shell *shell =
        container_of(listener, struct ivi_shell, destroy_listener);

    uifw_trace("shell_destroy");

    free(shell);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_restack_ivi_layer: rebuild compositor surface list
 *
 * @param[in]   shell       ico_ivi_shell static table address
 * @param[in]   shsurf      target shell surface(if NULL, no need change surface)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
ivi_shell_restack_ivi_layer(struct ivi_shell *shell, struct shell_surface *shsurf)
{
    struct shell_surface  *es;
    struct ivi_layer_list *el;
    float   new_x, new_y;

    uifw_trace("ivi_shell_restack_ivi_layer: Enter[%08x]", (int)shsurf);

    /* make compositor surface list     */
    wl_list_init(&shell->surface.surface_list);
    wl_list_for_each (el, &shell->ivi_layer.link, link) {
        if (ico_option_flag() & ICO_OPTION_FLAG_UNVISIBLE)  {
            wl_list_for_each (es, &el->surface_list, ivi_layer) {
                if (es->surface != NULL)    {
                    if ((el->visible == FALSE) || (es->visible == FALSE))   {
                        new_x = (float)(ICO_IVI_MAX_COORDINATE+1);
                        new_y = (float)(ICO_IVI_MAX_COORDINATE+1);
                    }
                    else if (es->surface->buffer)   {
                        if (es->geometry_width > es->surface->buffer->width) {
                            new_x = (float)(es->geometry_x +
                                    (es->geometry_width - es->surface->geometry.width)/2);
                        }
                        else    {
                            new_x = (float)es->geometry_x;
                        }
                        if (es->geometry_height > es->surface->buffer->height) {
                            new_y = (float) (es->geometry_y +
                                    (es->geometry_height - es->surface->geometry.height)/2);
                        }
                        else    {
                            new_y = (float)es->geometry_y;
                        }
                    }
                    else    {
                        new_x = (float)(ICO_IVI_MAX_COORDINATE+1);
                        new_y = (float)(ICO_IVI_MAX_COORDINATE+1);
                    }
                    wl_list_insert(shell->surface.surface_list.prev,
                                   &es->surface->layer_link);
                    if ((new_x != es->surface->geometry.x) ||
                        (new_y != es->surface->geometry.y)) {
                        weston_surface_damage_below(es->surface);
                        es->surface->geometry.x = new_x;
                        es->surface->geometry.y = new_y;
                        es->surface->geometry.dirty = 1;
                        weston_surface_damage_below(es->surface);
                    }
                }
            }
        }
        else    {
            if (el->visible != FALSE)   {
                wl_list_for_each (es, &el->surface_list, ivi_layer) {
                    if ((es->visible != FALSE) && (es->surface) &&
                        (es->surface->output != NULL) &&
                        (es->surface->shader != NULL))  {
                        wl_list_insert(shell->surface.surface_list.prev,
                                       &es->surface->layer_link);
                    }
                }
            }
        }
    }

    /* damage(redraw) target surfacem if target exist   */
    if (shsurf) {
        weston_surface_damage_below(shsurf->surface);
    }

    /* composit and draw screen(plane)  */
    weston_compositor_schedule_repaint(shell->compositor);

    uifw_trace("ivi_shell_restack_ivi_layer: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_set_active: surface active control
 *
 * @param[in]   shsurf      shell surface(if NULL, no active surface)
 * @param[in]   target      target device
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_set_active(struct shell_surface *shsurf, const int target)
{
    struct ivi_shell *shell;
    struct weston_seat *seat;
    struct weston_surface *surface;
    int object = target;
    wl_fixed_t sx, sy;

    uifw_trace("ivi_shell_set_active: Enter(%08x,%x)", (int)shsurf, target);

    if (shsurf) {
        shell = shell_surface_get_shell(shsurf);
        surface = shsurf->surface;
        if (object == 0)    {
            surface = NULL;
            if (shell->active_pointer_shsurf == shsurf) {
                object |= ICO_IVI_SHELL_ACTIVE_POINTER;
            }
            if (shell->active_keyboard_shsurf == shsurf)    {
                object |= ICO_IVI_SHELL_ACTIVE_KEYBOARD;
            }
        }
        else    {
            if (object & ICO_IVI_SHELL_ACTIVE_POINTER) {
                shell->active_pointer_shsurf = shsurf;
            }
            if (object & ICO_IVI_SHELL_ACTIVE_KEYBOARD)    {
                shell->active_keyboard_shsurf = shsurf;
            }
        }
    }
    else    {
        shell = default_shell;
        surface = NULL;
        if (target == 0)    {
            object = ICO_IVI_SHELL_ACTIVE_POINTER|ICO_IVI_SHELL_ACTIVE_KEYBOARD;
        }
        if (object & ICO_IVI_SHELL_ACTIVE_POINTER) {
            shell->active_pointer_shsurf = NULL;
        }
        if (object & ICO_IVI_SHELL_ACTIVE_KEYBOARD)    {
            shell->active_keyboard_shsurf = NULL;
        }
    }

    wl_list_for_each(seat, &shell->compositor->seat_list, link) {
        if ((object & ICO_IVI_SHELL_ACTIVE_POINTER) && (seat->seat.pointer))   {
            if (surface)    {
                uifw_trace("ivi_shell_set_active: pointer set surface(%08x=>%08x)",
                           (int)seat->seat.pointer->focus, (int)&surface->surface);
                if (seat->seat.pointer->focus != &surface->surface) {
                    weston_surface_from_global_fixed(surface,
                                                     seat->seat.pointer->x,
                                                     seat->seat.pointer->y,
                                                     &sx, &sy);
                    wl_pointer_set_focus(seat->seat.pointer, &surface->surface, sx, sy);
                }
            }
            else    {
                uifw_trace("ivi_shell_set_active: pointer reset surface(%08x)",
                           (int)seat->seat.pointer->focus);
                wl_pointer_set_focus(seat->seat.pointer, NULL,
                                     wl_fixed_from_int(0), wl_fixed_from_int(0));
            }
        }
        if ((object & ICO_IVI_SHELL_ACTIVE_KEYBOARD) && (seat->has_keyboard))  {
            if (surface)    {
                uifw_trace("ivi_shell_set_active: keyboard set surface(%08x=>%08x)",
                           (int)seat->seat.keyboard->focus, (int)&surface->surface);
                if (seat->seat.keyboard->focus != &surface->surface)    {
                    wl_keyboard_set_focus(seat->seat.keyboard, &surface->surface);
                }
            }
            else    {
                uifw_trace("ivi_shell_set_active: keyboard reset surface(%08x)",
                           (int)seat->seat.keyboard);
                wl_keyboard_set_focus(seat->seat.keyboard, NULL);
            }
        }
        else    {
            uifw_trace("ivi_shell_set_active: seat[%08x] has no keyboard", (int)seat);
        }
    }
    uifw_trace("ivi_shell_set_active: Leave(%08x)", (int)shsurf);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_set_client_attr : set client ttribute
 *
 * @param[in]   client      target client
 * @param[in]   attr        attribute
 * @param[in]   value       attribute value
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_set_client_attr(struct wl_client *client, const int attr, const int value)
{
    struct shell_surface  *es;
    struct ivi_layer_list *el;

    uifw_trace("ivi_shell_set_client_attr: Enter(%08x,%d,%d)", (int)client, attr, value);

    wl_list_for_each (el, &default_shell->ivi_layer.link, link) {
        wl_list_for_each (es, &el->surface_list, ivi_layer) {
            if (es->wclient == client)   {
                switch(attr)    {
                case ICO_CLEINT_ATTR_NOCONFIGURE:
                    es->noconfigure = value;
                    uifw_trace("ivi_shell_set_client_attr: set surface %08x", (int)es);
                    break;
                default:
                    break;
                }
            }
        }
    }
    uifw_trace("ivi_shell_set_client_attr: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_set_active: surface active control
 *
 * @param[in]   shsurf      shell surface(if NULL, no active surface)
 * @param[in]   restrain    restrain(1)/not restrain(0)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_restrain_configure(struct shell_surface *shsurf, const int restrain)
{
    uifw_trace("ivi_shell_restrain_configure: set %08x to %d",
               (int)shsurf, restrain);
    shsurf->restrain = restrain;

    if (restrain == 0)  {
        shell_surface_configure(shsurf->surface, shsurf->geometry_x, shsurf->geometry_y);
        ivi_shell_restack_ivi_layer(shell_surface_get_shell(shsurf), shsurf);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_set_active: surface active control
 *
 * @param[in]   shsurf      shell surface(if NULL, no active surface)
 * @param[in]   restrain    restrain(1)/not restrain(0)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT int
ivi_shell_is_restrain(struct shell_surface *shsurf)
{
    return shsurf->restrain;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_default_animation: window default animation
 *
 * @param[out]  msec    animation time(ms)
 * @param[out]  fps     animation frame rate(fps)
 * @return      default animation name
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT const char *
ivi_shell_default_animation(int *msec, int *fps)
{
    if (msec)   {
        *msec = default_shell->win_animation_time;
    }
    if (fps)   {
        *fps = default_shell->win_animation_fps;
    }
    return default_shell->win_animation;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   click_to_activate_binding: clieck and select surface
 *
 * @param[in]   seat        clicked target seat
 * @param[in]   button      click button(unused)
 * @param[in]   data        user data(ico_ivi_shell static table address)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
click_to_activate_binding(struct wl_seat *seat, uint32_t time, uint32_t button, void *data)
{
    struct ivi_shell *shell = data;
    struct shell_surface *shsurf;
    struct weston_surface *surface;

    if ((! seat) || (! seat->pointer) || (! seat->pointer->focus))  {
        uifw_trace("click_to_activate_binding: Surface dose not exist");
    }
    else    {
        surface = (struct weston_surface *) seat->pointer->focus;
        shsurf = get_shell_surface(surface);
        if (! shsurf)   {
            uifw_trace("click_to_activate_binding: Shell surface dose not exist");
        }
        else if ((shsurf->type == SHELL_SURFACE_NONE) ||
                 (shsurf->visible == 0))    {
            uifw_trace("click_to_activate_binding: Surface[%08x] is not visible",
                       (int)shsurf);
        }
        else if (shell->active_pointer_shsurf != shsurf)    {
            if (shell_hook_select) {
                /* surface select hook routine      */
                uifw_trace("click_to_activate_binding: call ivi_shell_hook_select[%08x]",
                           (int)shsurf);
                (void) (*shell_hook_select)(surface);
                uifw_trace("click_to_activate_binding: ret  ivi_shell_hook_select")
            }
            else    {
                ivi_shell_set_active(shsurf,
                                     ICO_IVI_SHELL_ACTIVE_POINTER |
                                         ICO_IVI_SHELL_ACTIVE_KEYBOARD);
                uifw_trace("click_to_activate_binding: no hook[%08x]", (int)shsurf);
            }
        }
        else    {
            uifw_trace("click_to_activate_binding: ShellSurface[%08x] already active",
                       (int)shsurf);
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_set_visible: surface visible control
 *
 * @param[in]   shsurf      shell surface
 * @param[in]   visible     visibility(1=visible/0=unvisible/-1=system default)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_set_visible(struct shell_surface *shsurf, const int visible)
{
    struct ivi_shell *shell = shell_surface_get_shell(shsurf);
    int next;

    uifw_trace("ivi_shell_set_visible: [%08x] visible=%d", (int)shsurf, (int)visible);

    if (visible < 0)    {
        next = shell->win_visible_on_create;
    }
    else    {
        next = visible;
    }

    if ((shsurf->visible != FALSE) && (next == 0))  {
        /* change show ==> hide         */
        shsurf->visible = FALSE;
        ivi_shell_restack_ivi_layer(shell, shsurf);
    }
    else if ((shsurf->visible == FALSE) && (next != 0)) {
        /* change hide ==> show         */
        shsurf->visible = TRUE;
        ivi_shell_restack_ivi_layer(shell, shsurf);
    }
    else    {
        /* other case, no change        */
        uifw_trace("ivi_shell_set_visible: No change");
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_is_visible: get surface visibility
 *
 * @param[in]   shsurf      shell surface
 * @return      visibility
 * @retval      true        visible
 * @retval      false       unvisible
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT bool
ivi_shell_is_visible(struct shell_surface *shsurf)
{
    return(shsurf->visible);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_set_layer: set(or change) surface layer
 *
 * @param[in]   shsurf      shell surface
 * @param[in]   layer       layer id
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_set_layer(struct shell_surface *shsurf, const int layer)
{
    struct ivi_shell *shell;
    struct ivi_layer_list *el;
    struct ivi_layer_list *new_el;

    uifw_trace("ivi_shell_set_layer: Enter([%08x],%08x,%d)",
               (int)shsurf, (int)shsurf->surface, layer);

    shell = shell_surface_get_shell(shsurf);

    /* check if same layer                      */
    if ((shsurf->layer_list != NULL) && (shsurf->layer_list->layer == layer))   {
        uifw_trace("ivi_shell_set_layer: Leave(Same Layer)");
        return;
    }

    /* search existing layer                    */
    wl_list_for_each (el, &shell->ivi_layer.link, link) {
        uifw_trace("ivi_shell_set_layer: el=%08x(%d)", (int)el, el->layer);
        if (el->layer == layer) break;
    }

    if (&el->link == &shell->ivi_layer.link)    {
        /* layer not exist, create new layer    */
        uifw_trace("ivi_shell_set_layer: New Layer %d", layer);
        new_el = malloc(sizeof(struct ivi_layer_list));
        if (! new_el)   {
            uifw_trace("ivi_shell_set_layer: Leave(No Memory)");
            return;
        }

        memset(new_el, 0, sizeof(struct ivi_layer_list));
        new_el->layer = layer;
        new_el->visible = TRUE;
        wl_list_init(&new_el->surface_list);
        wl_list_init(&new_el->link);

        wl_list_remove(&shsurf->ivi_layer);
        wl_list_insert(&new_el->surface_list, &shsurf->ivi_layer);
        shsurf->layer_list = new_el;

        wl_list_for_each (el, &shell->ivi_layer.link, link) {
            if (layer >= el->layer) break;
        }
        if (&el->link == &shell->ivi_layer.link)    {
            wl_list_insert(shell->ivi_layer.link.prev, &new_el->link);
        }
        else    {
            wl_list_insert(el->link.prev, &new_el->link);
        }
    }
    else    {
        uifw_trace("ivi_shell_set_layer: Add surface to Layer %d", layer);
        wl_list_remove(&shsurf->ivi_layer);
        wl_list_insert(&el->surface_list, &shsurf->ivi_layer);
        shsurf->layer_list = el;
    }

    /* rebild compositor surface list       */
    ivi_shell_restack_ivi_layer(shell, shsurf);

    uifw_trace("ivi_shell_set_layer: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_set_raise: surface stack control
 *
 * @param[in]   shsurf      shell surface
 * @param[in]   raise       raise/lower(1=raise/0=lower)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_set_raise(struct shell_surface *shsurf, const int raise)
{
    uifw_trace("ivi_shell_set_raise: Enter(%08x,%d) layer_list=%08x",
               (int)shsurf->surface, raise, (int)shsurf->layer_list);

    wl_list_remove(&shsurf->ivi_layer);
    if (raise)  {
        /* raise ... surface stack to top of layer          */
        wl_list_insert(&shsurf->layer_list->surface_list, &shsurf->ivi_layer);
        uifw_trace("ivi_shell_set_raise: Raise Link to Top");
    }
    else    {
        /* Lower ... surface stack to bottom of layer       */
        wl_list_insert(shsurf->layer_list->surface_list.prev, &shsurf->ivi_layer);
        uifw_trace("ivi_shell_set_raise: Lower Link to Bottom");
    }

    /* rebild compositor surface list               */
    ivi_shell_restack_ivi_layer(shell_surface_get_shell(shsurf), shsurf);

    uifw_trace("ivi_shell_set_raise: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_set_toplevel: set surface type toplevel
 *
 * @param[in]   shsurf      shell surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_set_toplevel(struct shell_surface *shsurf)
{
    uifw_trace("ivi_shell_set_toplevel: (%08x)", (int)shsurf->surface);
    set_toplevel(shsurf);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_set_surface_type: set surface type
 *
 * @param[in]   shsurf      shell surface
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_set_surface_type(struct shell_surface *shsurf)
{
    uifw_trace("ivi_shell_set_surfacetype: (%08x)", (int)shsurf->surface);
    set_surface_type(shsurf);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_send_configure: send surface resize event
 *
 * @param[in]   shsurf      shell surface
 * @param[in]   id          client object id(unused)
 * @param[in]   edges       surface resize position
 * @param[in]   width       surface width
 * @param[in]   height      surface height
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_send_configure(struct shell_surface *shsurf, const int id,
                         const int edges, const int width, const int height)
{
    /* send cgange event to manager     */
    uifw_trace("ivi_shell_send_configure: (%08x) edges=%x w/h=%d/%d map=%d",
               (int)shsurf->surface, edges, width, height, shsurf->mapped);

    shsurf->geometry_width = width;
    shsurf->geometry_height = height;

    if ((shsurf->mapped == 0) || (shsurf->noconfigure != 0) ||
        (width == 0) || (height == 0))  {
        return;
    }

    /* send cgange event to application */
    uifw_trace("ivi_shell_send_configure: Send (%08x) w/h=%d/%d(old=%d/%d)",
               (int)shsurf->surface, width, height,
               shsurf->configure_app.width, shsurf->configure_app.height);
    shsurf->configure_app.width = width;
    shsurf->configure_app.height = height;

    wl_shell_surface_send_configure(&shsurf->resource,
                                    WL_SHELL_SURFACE_RESIZE_BOTTOM_RIGHT,
                                    width, height);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_get_positionsize: get surface position and size
 *
 * @param[in]   shsurf      shell surface
 * @param[out]  x           surface upper-left X position on screen
 * @param[out]  y           surface upper-left Y position on screen
 * @param[out]  width       surface width
 * @param[out]  height      surface height
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_get_positionsize(struct shell_surface *shsurf,
                           int *x, int *y, int *width, int *height)
{
    *x = shsurf->geometry_x;
    *y = shsurf->geometry_y;
    *width = shsurf->geometry_width;
    *height = shsurf->geometry_height;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_set_positionsize: set surface position and size
 *
 * @param[in]   shsurf      shell surface
 * @param[in]   x           surface upper-left X position on screen
 * @param[in]   y           surface upper-left Y position on screen
 * @param[in]   width       surface width
 * @param[in]   height      surface height
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_set_positionsize(struct shell_surface *shsurf,
                           const int x, const int y, const int width, const int height)
{
    shsurf->geometry_x = x;
    shsurf->geometry_y = y;
    shsurf->geometry_width = width;
    shsurf->geometry_height = height;

    weston_compositor_schedule_repaint(shell_surface_get_shell(shsurf)->compositor);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_set_layer_visible: layer visible control
 *
 * @param[in]   layer       layer id
 * @param[in]   visible     visibility(1=visible/0=unvisible)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_set_layer_visible(const int layer, const int visible)
{
    struct ivi_shell *shell;
    struct ivi_layer_list *el;
    struct ivi_layer_list *new_el;
    struct shell_surface  *es;

    uifw_trace("ivi_shell_set_layer_visible: Enter(layer=%d, visible=%d)", layer, visible);

    shell = shell_surface_get_shell(NULL);

    /* Search Layer                             */
    wl_list_for_each (el, &shell->ivi_layer.link, link) {
        if (el->layer == layer) break;
    }

    if (&el->link == &shell->ivi_layer.link)    {
        /* layer not exist, create new layer    */
        uifw_trace("ivi_shell_set_layer_visible: New Layer %d", layer);
        new_el = malloc(sizeof(struct ivi_layer_list));
        if (! new_el)   {
            uifw_trace("ivi_shell_set_layer_visible: Leave(No Memory)");
            return;
        }

        memset(new_el, 0, sizeof(struct ivi_layer_list));
        new_el->layer = layer;
        new_el->visible = TRUE;
        wl_list_init(&new_el->surface_list);
        wl_list_init(&new_el->link);

        wl_list_for_each (el, &shell->ivi_layer.link, link) {
            if (layer >= el->layer) break;
        }
        if (&el->link == &shell->ivi_layer.link)    {
            wl_list_insert(shell->ivi_layer.link.prev, &new_el->link);
        }
        else    {
            wl_list_insert(el->link.prev, &new_el->link);
        }
        uifw_trace("ivi_shell_set_layer_visible: Leave(new layer)");
        return;
    }

    /* control all surface in layer */
    if ((el->visible != FALSE) && (visible == 0))   {
        /* layer change to NOT visible  */
        uifw_trace("ivi_shell_set_layer_visible: change to not visible");
        el->visible = FALSE;
    }
    else if ((el->visible == FALSE) && (visible != 0))  {
        /* layer change to visible      */
        uifw_trace("ivi_shell_set_layer_visible: change to visible");
        el->visible = TRUE;
    }
    else    {
        /* no change    */
        uifw_trace("ivi_shell_set_layer_visible: Leave(no Change %d=>%d)",
                   el->visible, visible);
        return;
    }

    /* set damege area          */
    wl_list_for_each (es, &el->surface_list, ivi_layer) {
        if ((es->visible != FALSE) &&
            (es->surface->output != NULL) &&
            (es->surface->shader != NULL))  {
            /* Damage(redraw) target surface    */
            weston_surface_damage_below(es->surface);
        }
    }

    /* rebild compositor surface list       */
    ivi_shell_restack_ivi_layer(shell, NULL);

    uifw_trace("ivi_shell_set_layer_visible: Leave");
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_surface_configure: surface change
 *
 * @param[in]   shsurf      shell surface
 * @param[in]   x           surface upper-left X position on screen
 * @param[in]   y           surface upper-left Y position on screen
 * @param[in]   width       surface width
 * @param[in]   height      surface height
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_surface_configure(struct shell_surface *shsurf, const int x,
                            const int y, const int width, const int height)
{
    int set_unvisible = 0;

    if (ico_option_flag() & ICO_OPTION_FLAG_UNVISIBLE)  {
        if ((shsurf->surface != NULL) && (shsurf->surface->buffer != NULL) &&
            (shsurf->surface->output != NULL) && (shsurf->surface->shader != NULL))  {
            if ((shsurf->visible == FALSE) ||
                ((shsurf->layer_list != NULL) &&
                 (shsurf->layer_list->visible == FALSE)))   {
                set_unvisible = 1;
            }
        }
    }
    if (set_unvisible)  {
        uifw_trace("ivi_shell_surface_configure: %08x %d,%d(%d/%d) unvisible",
                   (int)shsurf, x, y, width, height);
        weston_surface_configure(shsurf->surface,
                                 ICO_IVI_MAX_COORDINATE+1, ICO_IVI_MAX_COORDINATE+1,
                                 width, height);
    }
    else    {
        uifw_trace("ivi_shell_surface_configure: %08x %d,%d(%d/%d) visible",
                   (int)shsurf, x, y, width, height);
        weston_surface_configure(shsurf->surface, x, y, width, height);
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_hook_bind: regist hook function for shell bind
 *
 * @param[in]   hook_bind       hook function(if NULL, reset hook function)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_hook_bind(void (*hook_bind)(struct wl_client *client))
{
    uifw_trace("ivi_shell_hook_bind: Hook %08x", (int)hook_bind);
    shell_hook_bind = hook_bind;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_hook_unbind: regist hook function for shell unbind
 *
 * @param[in]   hook_unbind     hook function(if NULL, reset hook function)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_hook_unbind(void (*hook_unbind)(struct wl_client *client))
{
    uifw_trace("ivi_shell_hook_unbind: Hook %08x", (int)hook_unbind);
    shell_hook_unbind = hook_unbind;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_hook_create: regist hook function for create shell surface
 *
 * @param[in]   hook_create     hook function(if NULL, reset hook function)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_hook_create(void (*hook_create)(struct wl_client *client,
                      struct wl_resource *resource, struct weston_surface *surface,
                      struct shell_surface *shsurf))
{
    uifw_trace("ivi_shell_hook_create: Hook %08x", (int)hook_create);
    shell_hook_create = hook_create;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_hook_destroy: regist hook function for destroy shell surface
 *
 * @param[in]   hook_destroy    hook function(if NULL, reset hook function)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_hook_destroy(void (*hook_destroy)(struct weston_surface *surface))
{
    uifw_trace("ivi_shell_hook_destroy: Hook %08x", (int)hook_destroy);
    shell_hook_destroy = hook_destroy;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_hook_map: regist hook function for map shell surface
 *
 * @param[in]   hook_map        hook function(if NULL, reset hook function)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_hook_map(void (*hook_map)(struct weston_surface *surface,
                        int32_t *width, int32_t *height, int32_t *sx, int32_t *sy))
{
    uifw_trace("ivi_shell_hook_map: Hook %08x", (int)hook_map);
    shell_hook_map = hook_map;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_hook_change: regist hook function for change shell surface
 *
 * @param[in]   hook_change     hook function(if NULL, reset hook function)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_hook_change(void (*hook_change)(struct weston_surface *surface,
                                          const int to, const int manager))
{
    uifw_trace("ivi_shell_hook_change: Hook %08x", (int)hook_change);
    shell_hook_change = hook_change;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ivi_shell_hook_select: regist hook function for select(active) shell surface
 *
 * @param[in]   hook_select     hook function(if NULL, reset hook function)
 * @return      none
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT void
ivi_shell_hook_select(void (*hook_select)(struct weston_surface *surface))
{
    uifw_trace("ivi_shell_hook_select: Hook %08x", (int)hook_select);
    shell_hook_select = hook_select;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   module_init: initialize ico_ivi_shell
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
    struct ivi_shell *shell;

    uifw_info("ico_ivi_shell: Enter(module_init)");

    shell = malloc(sizeof *shell);
    if (shell == NULL)  return -1;
    default_shell = shell;

    memset(shell, 0, sizeof *shell);
    shell->compositor = ec;

    shell->destroy_listener.notify = shell_destroy;
    wl_signal_add(&ec->destroy_signal, &shell->destroy_listener);
    ec->shell_interface.shell = shell;
    ec->shell_interface.create_shell_surface = create_shell_surface;
    ec->shell_interface.set_toplevel = set_toplevel;
    ec->shell_interface.set_transient = set_transient;

    wl_list_init(&shell->ivi_layer.link);
    weston_layer_init(&shell->surface, &ec->cursor_layer.link);

    uifw_trace("ico_ivi_shell: shell(%08x) ivi_layer.link.%08x=%08x/%08x",
               (int)shell, (int)&shell->ivi_layer.link,
               (int)shell->ivi_layer.link.next, (int)shell->ivi_layer.link.prev);

    shell_configuration(shell);

    if (wl_display_add_global(ec->wl_display, &wl_shell_interface, shell, bind_shell)
            == NULL)    {
        return -1;
    }
    if (wl_display_add_global(ec->wl_display, &ico_ivi_shell_interface,
                              shell, bind_ivi_shell) == NULL)   {
        return -1;
    }

    weston_compositor_add_button_binding(ec, BTN_LEFT, 0,
                                         click_to_activate_binding, shell);

    uifw_info("ico_ivi_shell: Leave(module_init)");
    return 0;
}

