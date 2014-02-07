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
 * @brief   Weston(Wayland) Plugin Loader
 * @brief   Load the Weston plugins, because plugin loader of main body of Weston
 * @brief   cannot use other plugin functions by a other plugin.
 *
 * @date    Jul-26-2013
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

#include <weston/compositor.h>
#include <weston/config-parser.h>
#include "ico_ivi_common_private.h"
#include "ico_plugin_version.h"

/* Internal function to load one plugin.    */
static void load_module(struct weston_compositor *ec, const char *path, const char *entry,
                        int *argc, char *argv[]);

/* Static valiables                         */
static int  debug_level = 3;                /* Debug Level                          */


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
 * @param[in]   path        file path of locading plugin module.
 * @param[in]   entry       entry function name of locading plugin module.
 * @param[in]   argc        number of arguments.
 * @param[in]   argv        arguments list.
 * @return      none
 */
/*--------------------------------------------------------------------------*/
static void
load_module(struct weston_compositor *ec, const char *path, const char *entry,
            int *argc, char *argv[])
{
    void    *module;                    /* module informations (dlopen)             */
    int (*init)(struct weston_compositor *ec, int *argc, char *argv[]);
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
            init(ec, argc, argv);
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
 * @param[in]   argc        number of arguments(unused)
 * @param[in]   argv        argument list(unused)
 * @return      result
 * @retval      0           sccess
 * @retval      -1          error
 */
/*--------------------------------------------------------------------------*/
WL_EXPORT int
module_init(struct weston_compositor *ec, int *argc, char *argv[])
{
    struct weston_config_section *section;
    char    *moddir = NULL;                 /* Answer back from configuration       */
    char    *modules = NULL;                /* Answer back from configuration       */
    char    *p;
    char    *end;
    char    buffer[256];

    weston_log("INF>ico-uxf-weston-plugin " ICO_PLUIGN_VERSION "\n");

    uifw_info("ico_plugin_loader: Enter(module_init)");

    /* get ivi debug level          */
    section = weston_config_get_section(ec->config, "ivi-option", NULL, NULL);
    if (section)    {
        weston_config_section_get_int(section, "log", &debug_level, 3);
    }

    /* get plugin module name from config file(weston_ivi_plugin.ini)   */
    section = weston_config_get_section(ec->config, "ivi-plugin", NULL, NULL);
    if (section)    {
        weston_config_section_get_string(section, "moddir", &moddir, NULL);
        weston_config_section_get_string(section, "modules", &modules, NULL);
    }

    if (modules == NULL)    {
        uifw_error("ico_plugin_loader: Leave(No Plugin in config)");
        if (moddir) free(moddir);
        return -1;
    }
    p = getenv("WESTON_IVI_PLUGIN_DIR");
    if (p)  {
        if (moddir) free(moddir);
        moddir = strdup(p);
    }

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
        load_module(ec, buffer, "module_init", argc, argv);
        p = end;
        while (*p == ',')   {
            p++;
        }
    }
    if (moddir) free(moddir);
    free(modules);
    uifw_info("ico_plugin_loader: Leave(module_init)");

    return 0;
}

