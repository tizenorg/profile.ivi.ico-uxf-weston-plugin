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
 * @brief   Weston(Wayland) Plugin Loader
 * @brief   Load the Weston plugins, because plugin loader of main body of Weston
 * @brief   cannot use other plugin functions by a other plugin.
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
#include <sys/time.h>
#include <time.h>

#include "compositor.h"
#include "ico_ivi_common.h"

/* This function is called from the main body of Weston and initializes this module.*/
int module_init(struct weston_compositor *ec);

/* Internal function to load one plugin.    */
static void load_module(struct weston_compositor *ec, const char *path, const char *entry);

/* Static valiables                         */
static char *moddir = NULL;                 /* Answer back from configuration       */
static char *modules = NULL;                /* Answer back from configuration       */
static int  debug_level = 3;                /* Debug Level                          */

/* Configuration key                        */
static const struct config_key plugin_config_keys[] = {
        { "moddir", CONFIG_KEY_STRING, &moddir },
        { "modules", CONFIG_KEY_STRING, &modules },
    };

static const struct config_section conf_plugin[] = {
        { "plugin", plugin_config_keys, ARRAY_LENGTH(plugin_config_keys) },
    };

static const struct config_key debug_config_keys[] = {
        { "ivi_debug", CONFIG_KEY_INTEGER, &debug_level },
    };

static const struct config_section conf_debug[] = {
        { "debug", debug_config_keys, ARRAY_LENGTH(debug_config_keys) },
    };


/*--------------------------------------------------------------------------*/
/**
 * @brief   ico_ivi_debuglevel: answer debug output level.
 *
 * @param       none
 * @return      debug output level
 * @retval      0       No debug output
 * @retval      1       Only error output
 * @retval      2       Error and information output
 * @retval      3       All output with debug write
 */
/*--------------------------------------------------------------------------*/
int
ico_ivi_debuglevel(void)
{
    return debug_level;
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   load_module: load one plugin module.
 *
 * @param[in]   ec          weston compositor. (from weston)
 * @param[in]   path        file path of plugin module.
 * @param[in]   entry       entry function name of plugin module.
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
load_module(struct weston_compositor *ec, const char *path, const char *entry)
{
    void    *module;                    /* module informations (dlopen)             */
    int (*init)(struct weston_compositor *ec);
                                        /* enter function of loaded plugin          */

    uifw_info("ico_plugin_loader: Load(path=%s entry=%s)", path, entry);

    /* get module informations                      */
    module = dlopen(path, RTLD_NOW | RTLD_NOLOAD);

    if (module) {
        /* plugin module already loaded             */
        dlclose(module);
        uifw_error("ico_plugin_loader: Load Error(%s already loaded)", path);
        return;
    }

    /* load plugin module                           */
    uifw_trace("ico_plugin_loader: %s loading", path);

    module = dlopen(path, RTLD_NOW | RTLD_GLOBAL);

    if (! module)   {
        /* plugin module dose not exist             */
        uifw_error("ico_plugin_loader: Load Error(%s error<%s>)", path, dlerror());
        return;
    }

    /* find initialize function                     */
    if (entry)  {
        init = dlsym(module, entry);
        if (! init) {
            uifw_error("ico_plugin_loader: Load Error(%s, function %s dose not exist(%s))",
                       path, entry, dlerror());
        }
        else    {
            /* call initialize function             */
            uifw_trace("ico_plugin_loader: Call %s:%s(%08x)", path, entry, (int)init);
            init(ec);
            uifw_info("ico_plugin_loader: %s Loaded", path);
        }
    }
}

/*--------------------------------------------------------------------------*/
/**
 * @brief   module_init: initialize function of ico_plugin_loader
 *                       called from weston compositor.
 *
 * @param[in]   ec          weston compositor(from weston)
 * @return      result
 * @retval      0           sccess
 * @retval      -1          error
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT int
module_init(struct weston_compositor *ec)
{
    char    *config_file;
    char    *p;
    char    *end;
    char    buffer[256];

    uifw_info("ico_plugin_loader: Enter(module_init)");

    /* get plugin module name from config file(weston_ivi_plugin.ini)   */
    config_file = config_file_path(ICO_IVI_PLUGIN_CONFIG);
    parse_config_file(config_file, conf_plugin, ARRAY_LENGTH(conf_plugin), NULL);
    parse_config_file(config_file, conf_debug, ARRAY_LENGTH(conf_debug), NULL);
    free(config_file);

    if (modules == NULL)    {
        uifw_error("ico_plugin_loader: Leave(No Plugin in config)");
        return -1;
    }
    moddir = getenv("WESTON_IVI_PLUGIN_DIR");

    p = modules;
    while (*p) {
        end = strchrnul(p, ',');
        if (*p == '/')  {
            snprintf(buffer, sizeof(buffer), "%.*s", (int) (end - p), p);
        }
        else if (moddir)    {
            snprintf(buffer, sizeof(buffer), "%s/%.*s", moddir, (int) (end - p), p);
        }
        else    {
            snprintf(buffer, sizeof(buffer), "%s/%.*s", MODULEDIR, (int) (end - p), p);
        }
        load_module(ec, buffer, "module_init");
        p = end;
        while (*p == ',')   {
            p++;
        }
    }

    uifw_info("ico_plugin_loader: Leave(module_init)");

    return 0;
}

