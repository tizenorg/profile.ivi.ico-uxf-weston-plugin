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
 * @brief   Public functions in ico_window_mgr Weston plugin
 *
 * @date    Feb-21-2014
 */

#ifndef _ICO_WINDOW_MGR_PRIVATE_H_
#define _ICO_WINDOW_MGR_PRIVATE_H_

/* Manager management table             */
struct uifw_manager {
    struct wl_resource *resource;           /* Manager resource                     */
    struct wl_list link;                    /* link to next manager                 */
};

/* Cleint management table              */
struct uifw_client  {
    struct wl_client *client;               /* Wayland client                       */
    int         pid;                        /* ProcessId (pid)                      */
    char        appid[ICO_IVI_APPID_LENGTH];/* ApplicationId(from AppCore AUL)      */
    struct uifw_manager *mgr;               /* Manager table (if manager)           */
    char        manager;                    /* Manager flag (Need send event)       */
    char        fixed_appid;                /* ApplicationId fix flag(and counter)  */
    char        noconfigure;                /* no need configure event              */
    char        privilege;                  /* privilege aplication                 */
    char        *shmbuf;                    /* shared memory for surface image      */
    int         bufsize;                    /* shared memory buffer size            */
    int         bufnum;                     /* number of shared memory buffer       */
    struct wl_listener destroy_listener;    /* client destroy listener              */
    struct wl_list  surface_link;           /* surface list of same client          */
    struct wl_list  link;                   /* client list                          */
};

/* Node information                     */
struct uifw_node_table {
    uint16_t    node;                       /* node Id                              */
    uint16_t    displayno;                  /* weston display number                */
    struct weston_output *output;           /* weston output                        */
    int         disp_x;                     /* display frame buffer X-coordinate    */
    int         disp_y;                     /* display frame buffer Y-coordinate    */
    int         disp_width;                 /* display width                        */
    int         disp_height;                /* display height                       */
};

/* Surface map table                    */
struct uifw_win_surface;
struct uifw_surface_map {
    struct uifw_win_surface *usurf;         /* UIFW surface                         */
    struct uifw_client      *uclient;       /* UIFW client                          */
    struct weston_buffer    *curbuf;        /* current buffer                       */
    char        filepath[ICO_IVI_FILEPATH_LENGTH];
                                            /* surface image file path              */
    uint32_t    format;                     /* format                               */
    uint16_t    type;                       /* buffer type(currently only EGL buffer)*/
    uint16_t    width;                      /* width                                */
    uint16_t    height;                     /* height                               */
    uint16_t    stride;                     /* stride                               */
    int16_t     framerate;                  /* update frame rate (frame/sec)        */
    int16_t     interval;                   /* interval time (ms)                   */
    uint32_t    lasttime;                   /* last event time (ms)                 */
    char        initflag;                   /* map event send flag(0=no/1=yes)      */
    char        eventque;                   /* send event queue flag                */
    char        res[2];                     /* (unused)                             */
    struct wl_list  map_link;               /* surface map list                     */
    struct wl_list  surf_link;              /* surface map list from UIFW surface   */
};

/* Animation information    */
struct uifw_win_surface_animation {         /* wndow animation                      */
    struct weston_animation animation;      /* weston animation control             */
    uint16_t    type;                       /* current animation type               */
    uint16_t    anima;                      /* curremt animation Id                 */
    uint16_t    next_anima;                 /* next animation Id                    */
    uint16_t    hide_anima;                 /* animation Id for hide                */
    uint16_t    hide_time;                  /* animation time(ms) for hide          */
    uint16_t    show_anima;                 /* animation Id for show                */
    uint16_t    show_time;                  /* animation time(ms) for show          */
    uint16_t    move_anima;                 /* animation Id for move                */
    uint16_t    move_time;                  /* animation time(ms) for move          */
    uint16_t    resize_anima;               /* animation Id for resize              */
    uint16_t    resize_time;                /* animation time(ms) for resize        */
    uint16_t    time;                       /* current animation time(ms)           */
    uint16_t    pos_x;                      /* start/end X-coordinate               */
    uint16_t    pos_y;                      /* start/end Y-coordinate               */
    uint16_t    pos_width;                  /* start/end width                      */
    uint16_t    pos_height;                 /* start/end height                     */
    float       alpha;                      /* original alpha                       */
    short       current;                    /* animation current percentage         */
    char        state;                      /* animation state                      */
    char        visible;                    /* need hide(1)/show(2) at end of animation*/
    char        restrain_configure;         /* restrain surface resize              */
    char        ahalf;                      /* after half                           */
    char        no_configure;               /* no send configure to client          */
    char        res;                        /* (unused)                             */
    uint32_t    starttime;                  /* start time(ms)                       */
    void        *animadata;                 /* animation data                       */
};
struct uifw_win_surface_anima_save  {       /* wndow animation save                 */
    uint16_t    saved;                      /* flag for original saved              */
    uint16_t    type;                       /* current animation type               */
    uint16_t    anima;                      /* curremt animation Id                 */
    uint16_t    next_anima;                 /* next animation Id                    */
    uint16_t    hide_anima;                 /* animation Id for hide                */
    uint16_t    hide_time;                  /* animation time(ms) for hide          */
    uint16_t    show_anima;                 /* animation Id for show                */
    uint16_t    show_time;                  /* animation time(ms) for show          */
    uint16_t    move_anima;                 /* animation Id for move                */
    uint16_t    move_time;                  /* animation time(ms) for move          */
    uint16_t    resize_anima;               /* animation Id for resize              */
    uint16_t    resize_time;                /* animation time(ms) for resize        */
};

/* UIFW surface                         */
struct shell_surface;
struct weston_layout_surface;
struct uifw_win_surface {
    uint32_t    surfaceid;                  /* UIFW SurfaceId                       */
    struct uifw_node_table *node_tbl;       /* Node manager of ico_window_mgr       */
    struct weston_surface *surface;         /* Weston surface                       */
    struct weston_layout_surface *ivisurf;  /* Weston layout surface                */
    struct uifw_client    *uclient;         /* Client                               */
    struct wl_resource    *shsurf_resource; /* wl_shell_surface resource            */
    int         x;                          /* X-coordinate                         */
    int         y;                          /* Y-coordinate                         */
    uint16_t    width;                      /* Width                                */
    uint16_t    height;                     /* Height                               */
    uint16_t    client_width;               /* Widht that a client(App) required    */
    uint16_t    client_height;              /* Height that a client(App) required   */
    uint16_t    configure_width;            /* Widht that a client(App) configured  */
    uint16_t    configure_height;           /* Height that a client(App) configured */
    char        winname[ICO_IVI_WINNAME_LENGTH];/* Window name                      */
    char        visible;                    /* visibility                           */
    char        restrain_configure;         /* restrant configure event             */
    char        res[1];                     /* (unused)                             */
    struct uifw_win_surface_animation
                animation;                  /* window animation information         */
    struct uifw_win_surface_anima_save  
                org_animation;              /* save original wndow animation        */
    struct wl_list  client_link;            /* surface list of same client          */
    struct wl_list  surf_map;               /* surface map list                     */
    struct wl_list  input_region;           /* surface input region list            */
    struct uifw_win_surface *next_idhash;   /* UIFW SurfaceId hash list             */
    struct uifw_win_surface *next_wshash;   /* Weston SurfaceId hash list           */
};

/* animation operation                  */
/* default animation                    */
#define ICO_WINDOW_MGR_ANIMATION_NONE           0   /* no animation                 */

/* return code of animation hook function*/
#define ICO_WINDOW_MGR_ANIMATION_RET_NOANIMA    -1  /* no animation                 */
#define ICO_WINDOW_MGR_ANIMATION_RET_ANIMA      0   /* animation                    */
#define ICO_WINDOW_MGR_ANIMATION_RET_ANIMANOCTL 1   /* animation but no control     */

/* animation state                      */
#define ICO_WINDOW_MGR_ANIMATION_STATE_NONE     0   /* not animation                */
#define ICO_WINDOW_MGR_ANIMATION_STATE_SHOW     1   /* show(in) animation           */
#define ICO_WINDOW_MGR_ANIMATION_STATE_HIDE     2   /* hide(out) animation          */
#define ICO_WINDOW_MGR_ANIMATION_STATE_MOVE     3   /* move animation               */
#define ICO_WINDOW_MGR_ANIMATION_STATE_RESIZE   4   /* resize animation             */

/* extended(plugin) animation operation */
#define ICO_WINDOW_MGR_ANIMATION_NAME       0       /* convert animation name to Id */
#define ICO_WINDOW_MGR_ANIMATION_DESTROY   99       /* surface destroy              */
#define ICO_WINDOW_MGR_ANIMATION_OPNONE     0       /* no animation                 */
#define ICO_WINDOW_MGR_ANIMATION_OPHIDE     1       /* change to hide               */
#define ICO_WINDOW_MGR_ANIMATION_OPSHOW     2       /* change to show               */
#define ICO_WINDOW_MGR_ANIMATION_OPMOVE     3       /* surface move                 */
#define ICO_WINDOW_MGR_ANIMATION_OPRESIZE   4       /* surface resize               */
#define ICO_WINDOW_MGR_ANIMATION_OPCANCEL   9       /* animation cancel             */
#define ICO_WINDOW_MGR_ANIMATION_OPHIDEPOS 11       /* change to hide with position */
#define ICO_WINDOW_MGR_ANIMATION_OPSHOWPOS 12       /* change to show with position */

/* Prototype for function               */
                                            /* find uifw_client table               */
struct uifw_client *ico_window_mgr_find_uclient(struct wl_client *client);
                                            /* get client applicationId             */
char *ico_window_mgr_get_appid(struct wl_client* client);
                                            /* get display coordinate               */
void ico_window_mgr_get_display_coordinate(int displayno, int *x, int *y);
                                            /* get buffer width                     */
int ico_ivi_surface_buffer_width(struct weston_surface *es);
                                            /* get buffer height                    */
int ico_ivi_surface_buffer_height(struct weston_surface *es);
                                            /* get buffer size                      */
void ico_ivi_surface_buffer_size(struct weston_surface *es, int *width, int *height);
                                            /* get surface primary view             */
struct weston_view *ico_ivi_get_primary_view(struct uifw_win_surface *usurf);
                                            /* change weston surface                */
void ico_window_mgr_set_weston_surface(struct uifw_win_surface *usurf, int x, int y,
                                       int width, int height);
                                            /* get UIFW client table                */
struct uifw_client *ico_window_mgr_get_uclient(const char *appid);
                                            /* get UIFW surface table               */
struct uifw_win_surface *ico_window_mgr_get_usurf(const uint32_t surfaceid);
                                            /* get UIFW surface table               */
struct uifw_win_surface *ico_window_mgr_get_usurf_client(const uint32_t surfaceid,
                                                         struct wl_client *client);
                                            /* get application surface              */
struct uifw_win_surface *ico_window_mgr_get_client_usurf(const char *target);
                                            /* set window animation hook            */
void ico_window_mgr_set_hook_animation(int (*hook_animation)(const int op, void *data));
                                            /* set surface attribute change hook    */
void ico_window_mgr_set_hook_change(void (*hook_change)(struct uifw_win_surface *usurf));
                                            /* set surface destory hook             */
void ico_window_mgr_set_hook_destory(void (*hook_destroy)(struct uifw_win_surface *usurf));
                                            /* set input region set/unset hook      */
void ico_window_mgr_set_hook_inputregion(void (*hook_inputregion)(
                        int set, struct uifw_win_surface *usurf, int32_t x, int32_t y,
                        int32_t width, int32_t height, int32_t hotspot_x, int32_t hotspot_y,
                        int32_t cursor_x, int32_t cursor_y, int32_t cursor_width,
                        int32_t cursor_height, uint32_t attr));

#endif  /*_ICO_WINDOW_MGR_PRIVATE_H_*/
