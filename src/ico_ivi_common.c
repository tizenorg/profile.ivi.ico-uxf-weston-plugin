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
 * @brief   Weston(Wayland) Plugin: IVI Common Functions.
 *
 * @date    Feb-08-2013
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>

#include <weston/compositor.h>
#include "ico_ivi_common.h"

/* IVI Plugin Common Table                  */
struct ico_ivi_common {
    int32_t myNodeId;                       /* (HostId << 16) | DisplayNo               */
    int (*usurf_2_node)(const int surfaceid);
                                            /* Function address of nodeId from surfaceId*/
    int (*send_to_mgr)(const int event, const int surfaceid, const char *appid,
                       const int param1, const int param2, const int param3,
                       const int param4, const int param5, const int param6);
                                            /* Function address of send to manager      */
    int (*send_surface_change)(struct weston_surface *surface,
                               const int x, const int y, const int width, const int height);
                                            /* Function address of send configure to manager*/
};

/* This function is called from the ico_plugin-loader and initializes this module.*/
int module_init(struct weston_compositor *ec);

/* Static area for control ico_ivi_common       */
static struct ico_ivi_common *_ico_ivi_common = NULL;

/* Special options                              */
static int  _ico_option_flag = 0;

/* Debug level                                  */
static int  _ico_ivi_debug = 3;

static const struct config_key debug_config_keys[] = {
        { "option_flag", CONFIG_KEY_INTEGER, &_ico_option_flag },
        { "ivi_debug", CONFIG_KEY_INTEGER, &_ico_ivi_debug },
    };

static const struct config_section conf_debug[] = {
        { "debug", debug_config_keys, ARRAY_LENGTH(debug_config_keys) },
    };

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_special_option: Answer special option flag
 *
 * @param       None
 * @return      Special option flag
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_option_flag(void)
{
    return _ico_option_flag;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_debuglevel: Answer debug output level.
 *
 * @param       None
 * @return      Debug output level
 * @retval      0       No debug output
 * @retval      1       Only error output
 * @retval      2       Error and information output
 * @retval      3       All output with debug write
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_debuglevel(void)
{
    return _ico_ivi_debug;
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
    /* Reference Platform 0.50 only support 1 ECU   */
    return 0;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_set_usurf_2_node: Regist function of convert surfaceId to NodeId
 *
 * @param[in]   usurf_2_node    Function address of usurf_2_node
 * @return      None
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_ivi_set_usurf_2_node(int (*usurf_2_node)(const int surfaceid))
{
    _ico_ivi_common->usurf_2_node = usurf_2_node;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_usurf_2_node: Convert surfaceId to NodeId
 *
 * @param[in]   surfaceid       ivi-shell surfaceId
 * @return      NodeId
 * @retval      >= 0            NodeId
 * @retval      < 0             Surface dose not exist
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_usurf_2_node(const int surfaceid)
{
    int     nodeId;

    if (_ico_ivi_common->usurf_2_node)  {
        /* If declared convert function, call function          */
        nodeId = (*_ico_ivi_common->usurf_2_node) (surfaceid);
        if (nodeId >= 0)    {
            return nodeId;
        }
    }

    /* If NOT declare convert function, convert from surfaceId  */
    return ICO_IVI_SURFACEID_2_NODEID(surfaceid);
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_set_send_to_mgr: Regist function of send to manager
 *
 * @param[in]   send_to_mgr     Function address of send to manager
 * @return      None
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_ivi_set_send_to_mgr(int (*send_to_mgr)(const int event,
                        const int surfaceid, const char *appid,
                        const int param1, const int param2, const int param3,
                        const int param4, const int param5, const int param6))
{
    _ico_ivi_common->send_to_mgr = send_to_mgr;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_send_to_mgr: Send event to manager
 *
 * @param[in]   event           Event code
 * @param[in]   client_resource Client resource
 * @param[in]   surfaceid       SurfaceId  (depend of event code)
 * @param[in]   appid           ApplicationId  (depend of event code)
 * @param[in]   param1          Parameter.1 (depend of event code)
 * @param[in]   param2          Parameter.2 (depend of event code)
 * @param[in]     :
 * @param[in]   param6          Parameter.6 (depend of event code)
 * @return      number of managers
 * @retval      > 0             number of managers
 * @retval      0               manager not exist
 * @retval      -1              Multi Input Manager not exist
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_send_to_mgr(const int event, struct wl_resource *client_resource,
                    const int surfaceid, const char *appid,
                    const int param1, const int param2, const int param3,
                    const int param4, const int param5, const int param6)
{
    if (_ico_ivi_common->send_to_mgr)   {
        return (*_ico_ivi_common->send_to_mgr)(event, surfaceid, appid,
                                               param1, param2, param3,
                                               param4, param5, param6);
    }
    return -1;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_set_send_surface_change: Regist function of surface change
 *
 * @param[in]   send_surface_change     Function address of surface change
 * @return      None
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   void
ico_ivi_set_send_surface_change(int (*send_surface_change)(struct weston_surface *surface,
                                                           const int x, const int y,
                                                           const int width,
                                                           const int height))
{
    _ico_ivi_common->send_surface_change = send_surface_change;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_send_surface_change: Send surface change event to manager
 *
 * @param[in]   surface         Weston surface
 * @return      number of managers
 * @retval      > 0             number of managers
 * @retval      0               manager not exist
 * @retval      -1              Multi Input Manager not exist
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT   int
ico_ivi_send_surface_change(struct weston_surface *surface,
                            const int x, const int y, const int width, const int height)
{
    if (_ico_ivi_common->send_surface_change)   {
        return (*_ico_ivi_common->send_surface_change)(surface, x, y, width, height);
    }
    return -1;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_nodename_2_node: Convert node name to hostId
 *
 * @param[in]   nodename    Node name(same as host name)
 * @return      HostId
 * @retval      >= 0        HostId
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT int
ico_ivi_nodename_2_node(const char *nodename)
{
    if (nodename == NULL)   {
        uifw_trace("ico_ivi_nodename_2_node: NULL => %d",
                   ICO_IVI_NODEID_2_HOSTID(_ico_ivi_common->myNodeId));
        return ICO_IVI_NODEID_2_HOSTID(_ico_ivi_common->myNodeId);
    }

    /* Reference Platform 0.50 only support 1 ECU   */
    uifw_trace("ico_ivi_nodename_2_node: %s => None", nodename );
    return 0;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_dispname_2_node: Convert display name to nodeId
 *
 * @param[in]   dispname    Node name(same as display name)
 * @return      NodeId
 * @retval      >= 0        NodeId
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT int
ico_ivi_dispname_2_node(const char *dispname)
{
    if (dispname == NULL)   {
        uifw_trace("ico_ivi_dispname_2_node: NULL => %x", _ico_ivi_common->myNodeId);
        return _ico_ivi_common->myNodeId;
    }

    /* Reference Platform 0.50 only support 1 ECU   */
    uifw_trace("ico_ivi_dispname_2_node: %s => None", dispname);
    return 0;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   IVI Common: Initialize function of ico_ivi_common.
 *
 * @param[in]   ec          Weston compositor. (from Weston)
 * @return      result
 * @retval      0           Normal end
 * @retval      -1          Error
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT int
module_init(struct weston_compositor *ec)
{
    int config_fd;

    uifw_info("ico_ivi_common: Enter(module_init)");

    /* Get debug level from config file         */
    config_fd = open_config_file(ICO_IVI_PLUGIN_CONFIG);
    parse_config_file(config_fd, conf_debug, ARRAY_LENGTH(conf_debug), NULL);
    close(config_fd);

    uifw_info("ico_ivi_common: option flag=0x%08x debug=%d",
              ico_option_flag(), ico_ivi_debuglevel());

    /* Allocate static area                     */
    _ico_ivi_common = (struct ico_ivi_common *) malloc(sizeof(struct ico_ivi_common));
    if (! _ico_ivi_common)  {
        uifw_error("ico_ivi_common: Leave(No Memory)");
        return -1;
    }
    memset(_ico_ivi_common, 0, sizeof(struct ico_ivi_common));

    uifw_info("ico_ivi_common: Leave(module_init)");

    return 0;
}

