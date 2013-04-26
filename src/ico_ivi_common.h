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
 * @brief   The common functions that each Plugin is available.
 *
 * @date    Feb-08-2013
 */

#ifndef _ICO_IVI_COMMON_H_
#define _ICO_IVI_COMMON_H_

/* Macros                               */
#define ICO_IVI_NODEID_2_HOSTID( nodeid )       (((unsigned int)nodeid) >> 16)
#define ICO_IVI_NODEID_2_DISPLAYNO( nodeid )    (((unsigned int)nodeid) & 0x0ffff)
#define ICO_IVI_NODEDISP_2_NODEID( nodeid, displayno )  \
                                                ((nodeid << 16) | displayno)
#define ICO_IVI_SURFACEID_2_HOSTID( surfid )    (((unsigned int)surfid) >> 24)
#define ICO_IVI_SURFACEID_2_DISPLAYNO( surfid ) ((((unsigned int)surfid) >> 16) & 0x0ff)
#define ICO_IVI_SURFACEID_2_NODEID( surfid )    \
            ICO_IVI_NODEDISP_2_NODEID( ICO_IVI_SURFACEID_2_HOSTID(surfid),  \
                                       ICO_IVI_SURFACEID_2_DISPLAYNO(surfid) )
#define ICO_IVI_SURFACEID_BASE( nodeid )    \
            (ICO_IVI_NODEID_2_HOSTID(nodeid) << 24) |   \
            (ICO_IVI_NODEID_2_DISPLAYNO(nodeid) << 16)

/* Return value                         */
#define ICO_IVI_EOK         0               /* OK                                   */
#define ICO_IVI_ENOENT      -2              /* No such object                       */
#define ICO_IVI_EIO         -5              /* Send error                           */
#define ICO_IVI_ENOMEM      -12             /* Out of memory                        */
#define ICO_IVI_EBUSY       -16             /* Not available now                    */
#define ICO_IVI_EINVAL      -22             /* Invalid argument                     */

/* Configuration file                   */
#define ICO_IVI_PLUGIN_CONFIG   "weston_ivi_plugin.ini"

/* System limit                         */
#define ICO_IVI_APPID_LENGTH    (128)       /* Maximum length of applicationId(AppCore) */
                                            /* (with terminate NULL)                */
#define ICO_IVI_MAX_COORDINATE  (16383)     /* Maximum X or Y coordinate            */
/* Fixed value                          */
#define ICO_IVI_DEFAULT_LAYER   (0)         /* Default layerId for surface creation */
#ifndef TRUE
#define TRUE    1
#endif
#ifndef FALSE
#define FALSE   0
#endif

/* Function prototype                   */
                                        /* Get my node numner                       */
int ico_ivi_get_mynode(void);
                                        /* Convert host name to ECU number          */
int ico_ivi_nodename_2_node(const char *nodename);
                                        /* Convert display name to nodeId           */
int ico_ivi_dispname_2_node(const char *dispname);
                                        /* Regist function of display to nodeId convert*/
void ico_ivi_set_usurf_2_node(int (*usurf_2_node)(const int surfaceid));
                                        /* Convery surfaceId to nodeId              */
int ico_ivi_usurf_2_node(const int surfaceid);
                                        /* Regist function of send event to manager */
void ico_ivi_set_send_to_mgr(int (*send_to_mgr)(const int event,
                            const int surfaceid, const char *appid,
                            const int param1, const int param2, const int param3,
                            const int param4, const int param5, const int param6));
                                        /* Send event to manager                    */
int ico_ivi_send_to_mgr(const int event, struct wl_resource *client_resource,
                            const int surfaceid, const char *appid,
                            const int param1, const int param2, const int param3,
                            const int param4, const int param5, const int param6);

void ico_ivi_set_send_surface_change(int (*send_surface_change)(
                            struct weston_surface *surface,
                            const int x, const int y, const int width, const int height));
int ico_ivi_send_surface_change(struct weston_surface *surface,
                            const int x, const int y, const int width, const int height);

int ico_option_flag(void);
int ico_ivi_debuglevel(void);

/* Debug Traces                         */
/* Define for debug write               */
#define UIFW_DEBUG_OUT  1   /* 1=Debug Print/0=No Debug Print           */

#if UIFW_DEBUG_OUT > 0
#define uifw_trace(fmt,...)  \
    { if (ico_ivi_debuglevel() >= 4) {weston_log("DBG>"fmt" (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__);} }
#else  /*UIFW_DEBUG_OUT*/
#define uifw_trace(fmt,...)
#endif /*UIFW_DEBUG_OUT*/

#define uifw_info(fmt,...)  \
    { if (ico_ivi_debuglevel() >= 3) {weston_log("INF>"fmt" (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__);} }
#define uifw_msg(fmt,...)  \
    { if (ico_ivi_debuglevel() >= 3) {weston_log("INF>"fmt"\n",##__VA_ARGS__);} }
#define uifw_warn(fmt,...)  \
    { if (ico_ivi_debuglevel() >= 2) {weston_log("WRN>"fmt" (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__);} }
#define uifw_error(fmt,...)  \
    { if (ico_ivi_debuglevel() >= 1) {weston_log("ERR>"fmt" (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__);} }

#endif  /*_ICO_IVI_COMMON_H_*/

