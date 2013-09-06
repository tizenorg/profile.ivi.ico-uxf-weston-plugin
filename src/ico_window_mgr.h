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
 * @brief   Public functions in ico_window_mgr Weston plugin
 *
 * @date    Jul-26-2013
 */

#ifndef _ICO_WINDOW_MGR_H_
#define _ICO_WINDOW_MGR_H_

/* Manager management table             */
struct uifw_manager {
    struct wl_resource *resource;           /* Manager resource                     */
    int     manager;                        /* Manager(=event send flag)            */
    struct wl_list link;                    /* link to next manager                 */
};

/* Cleint management table              */
struct uifw_client  {
    struct wl_client *client;               /* Wayland client                       */
    int     pid;                            /* ProcessId (pid)                      */
    char    appid[ICO_IVI_APPID_LENGTH];    /* ApplicationId(from AppCore AUL)      */
    struct uifw_manager *mgr;               /* Manager table (if manager)           */
    char    manager;                        /* Manager flag (Need send event)       */
    char    fixed_appid;                    /* ApplicationId fix flag(and counter)  */
    char    noconfigure;                    /* no need configure event              */
    char    res;                            /* (unused)                             */
    struct wl_list  surface_link;           /* surface list of same client          */
    struct wl_list  link;
};

/* Node information                     */
struct uifw_node_table {
    uint16_t    node;                       /* node Id                              */
    uint16_t    displayno;                  /* weston display number                */
    int         disp_x;                     /* display frame buffer X-coordinate    */
    int         disp_y;                     /* display frame buffer Y-coordinate    */
    int         disp_width;                 /* display width                        */
    int         disp_height;                /* display height                       */
};

/* Layer management table               */
struct  uifw_win_layer  {
    uint32_t layer;                         /* Layer Id                             */
    char     visible;                       /* visibility                           */
    char     layer_type;                    /* layer type                           */
    char     res[2];                        /* (unused)                             */
    struct wl_list surface_list;            /* Surfacae list                        */
    struct wl_list link;                    /* Link pointer for layer list          */
};

/* Surface map table                    */
struct uifw_win_surface;
struct uifw_surface_map {
    struct uifw_win_surface *usurf;         /* UIFW surface                         */
    struct uifw_client  *uclient;           /* UIFW client                          */
    uint32_t    eglname;                    /* EGL buffer name                      */
    uint32_t    format;                     /* format                               */
    uint16_t    type;                       /* buffer type(currently only EGL buffer)*/
    uint16_t    width;                      /* width                                */
    uint16_t    height;                     /* height                               */
    uint16_t    stride;                     /* stride                               */
    uint16_t    framerate;                  /* update frame rate (frame/sec)        */
    char        initflag;                   /* map event send flag(0=no/1=yes)      */
    char        res;                        /* (unused)                             */
    struct wl_list  map_link;               /* map list                             */
    struct wl_list  surf_link;              /* map list from UIFW surface           */
};

/* UIFW surface                         */
struct shell_surface;
struct uifw_win_surface {
    uint32_t surfaceid;                     /* UIFW SurfaceId                       */
    struct uifw_node_table *node_tbl;       /* Node manager of ico_window_mgr       */
    struct uifw_win_layer *win_layer;       /* surface layer                        */
    struct weston_surface *surface;         /* Weston surface                       */
    struct shell_surface  *shsurf;          /* Shell(IVI-Shell) surface             */
    struct uifw_client    *uclient;         /* Client                               */
    struct weston_transform transform;      /* transform matrix                     */
    float   scalex;                         /* surface transform scale of X         */
    float   scaley;                         /* surface transform scale of Y         */
    int     x;                              /* X-coordinate                         */
    int     y;                              /* Y-coordinate                         */
    short   xadd;                           /* X-coordinate delta                   */
    short   yadd;                           /* Y-coordinate delta                   */
    uint16_t width;                         /* Width                                */
    uint16_t height;                        /* Height                               */
    uint16_t client_width;                  /* Widht that a client(App) required    */
    uint16_t client_height;                 /* Height that a client(App) required   */
    uint16_t conf_width;                    /* Width that notified to client        */
    uint16_t conf_height;                   /* Height that notified to client       */
    uint32_t attributes;                    /* surface attributes                   */
    char    winname[ICO_IVI_WINNAME_LENGTH];/* Window name                          */
    char    disable;                        /* can not display                      */
    char    visible;                        /* visibility                           */
    char    raise;                          /* raise(top of the layer)              */
    char    created;                        /* sended created event to manager      */
    char    mapped;                         /* end of map                           */
    char    restrain_configure;             /* restrant configure event             */
    char    set_transform;                  /* surface transform flag               */
    char    res;                            /* (unused)                             */
    struct  _uifw_win_surface_animation {   /* wndow animation                      */
        struct weston_animation animation;  /* weston animation control             */
        uint16_t type;                      /* current animation type               */
        uint16_t anima;                     /* curremt animation Id                 */
        uint16_t next_anima;                /* next animation Id                    */
        uint16_t hide_anima;                /* animation Id for hide                */
        uint16_t hide_time;                 /* animation time(ms) for hide          */
        uint16_t show_anima;                /* animation Id for show                */
        uint16_t show_time;                 /* animation time(ms) for show          */
        uint16_t move_anima;                /* animation Id for move                */
        uint16_t move_time;                 /* animation time(ms) for move          */
        uint16_t resize_anima;              /* animation Id for resize              */
        uint16_t resize_time;               /* animation time(ms) for resize        */
        uint16_t time;                      /* current animation time(ms)           */
        uint16_t pos_x;                     /* start/end X-coordinate               */
        uint16_t pos_y;                     /* start/end Y-coordinate               */
        uint16_t pos_width;                 /* start/end width                      */
        uint16_t pos_height;                /* start/end height                     */
        short   current;                    /* animation current percentage         */
        char    state;                      /* animation state                      */
        char    visible;                    /* need hide(1)/show(2) at end of animation*/
        char    restrain_configure;         /* restrain surface resize              */
        char    ahalf;                      /* after half                           */
        char    no_configure;               /* no send configure to client          */
        char    res;                        /* (unused)                             */
        uint32_t starttime;                 /* start time(ms)                       */
        void    *animadata;                 /* animation data                       */
    }       animation;
    struct wl_list          ivi_layer;      /* surface list of same layer           */
    struct wl_list          client_link;    /* surface list of same client          */
    struct wl_list          surf_map;       /* surface map list                     */
    struct uifw_win_surface *next_idhash;   /* UIFW SurfaceId hash list             */
    struct uifw_win_surface *next_wshash;   /* Weston SurfaceId hash list           */
};

/* layer type                           */
#define ICO_WINDOW_MGR_LAYER_TYPE_NORMAL     0      /* normal layer                 */
#define ICO_WINDOW_MGR_LAYER_TYPE_BACKGROUND 1      /* touch input layer            */
#define ICO_WINDOW_MGR_LAYER_TYPE_INPUT      7      /* touch input layer            */
#define ICO_WINDOW_MGR_LAYER_TYPE_CURSOR     8      /* cursor layer                 */

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
                                            /* surface visible control              */
void ico_window_mgr_set_visible(struct uifw_win_surface *usurf, const int visible);
                                            /* get client applicationId             */
char *ico_window_mgr_get_appid(struct wl_client* client);
                                            /* change weston surface                */
void ico_window_mgr_set_weston_surface(struct uifw_win_surface *usurf, int x, int y,
                                       int width, int height);
                                            /* surface change                       */
void ico_window_mgr_change_surface(struct uifw_win_surface *usurf,
                                   const int to, const int manager);
                                            /* get UIFW client table                */
struct uifw_client *ico_window_mgr_get_uclient(const char *appid);
                                            /* get UIFW surface table               */
struct uifw_win_surface *ico_window_mgr_get_usurf(const uint32_t surfaceid);
                                            /* get UIFW surface table               */
struct uifw_win_surface *ico_window_mgr_get_usurf_client(const uint32_t surfaceid,
                                                         struct wl_client *client);
                                            /* get application surface              */
struct uifw_win_surface *ico_window_mgr_get_client_usurf(const char *appid,
                                                         const char *winname);
                                            /* rebuild surface layer list           */
void ico_window_mgr_restack_layer(struct uifw_win_surface *usurf, const int omit_touch);
                                            /* set window animation hook            */
void ico_window_mgr_set_hook_animation(int (*hook_animation)(const int op, void *data));

#endif  /*_ICO_WINDOW_MGR_H_*/

