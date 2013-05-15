/*
 * Copyright © 2011 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#define _GNU_SOURCE   /* for stchrnul() */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#if 1   /* Build in old Weston(1.0.6)   */
#include "config-parser.h"
#else   /* Build on new Weston          */
#include <weston/config-parser.h>
#endif

static int
handle_key(const struct config_key *key, const char *value)
{
    char *end, *s;
    int i, len;
    unsigned int ui;
    
    switch (key->type) {
    case CONFIG_KEY_INTEGER:
        i = strtol(value, &end, 0);
        if (*end != '\n') {
            fprintf(stderr, "invalid integer: %s\n", value);
            return -1;
        }
        *(int *)key->data = i;
        return 0;

    case CONFIG_KEY_UNSIGNED_INTEGER:
        ui = strtoul(value, &end, 0);
        if (*end != '\n') {
            fprintf(stderr, "invalid integer: %s\n", value);
            return -1;
        }
        *(unsigned int *)key->data = ui;
        return 0;

    case CONFIG_KEY_STRING:
        len = strlen(value);
        while (len > 0 && isspace(value[len - 1]))
            len--;
        s = malloc(len + 1);
        if (s == NULL)
            return -1;
        memcpy(s, value, len);
        s[len] = '\0';
        *(char **)key->data = s;
        return 0;

    case CONFIG_KEY_BOOLEAN:
        if (strcmp(value, "false\n") == 0)
            *(int *)key->data = 0;
        else if (strcmp(value, "true\n") == 0)
            *(int *)key->data = 1;
        else {
            fprintf(stderr, "invalid bool: %s\n", value);
            return -1;
        }
        return 0;

    default:
        assert(0);
        break;
    }

    return -1;
}

int
parse_config_file(int fd,
          const struct config_section *sections, int num_sections,
          void *data)
{
    FILE *fp;
    char line[512], *p;
    const struct config_section *current = NULL;
    int i;

    if (fd == -1)
        return -1;
    fp = fdopen(dup(fd), "r");
    if (fp == NULL) {
        perror("couldn't open config flle");
        return -1;
    }

    rewind(fp);

    while (fgets(line, sizeof line, fp)) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        } if (line[0] == '[') {
            p = strchr(&line[1], ']');
            if (!p || p[1] != '\n') {
                fprintf(stderr, "malformed "
                    "section header: %s\n", line);
                fclose(fp);
                return -1;
            }
            if (current && current->done)
                current->done(data);
            p[0] = '\0';
            for (i = 0; i < num_sections; i++) {
                if (strcmp(sections[i].name, &line[1]) == 0) {
                    current = &sections[i];
                    break;
                }
            }
            if (i == num_sections)
                current = NULL;
        } else if (p = strchr(line, '='), p != NULL) {
            if (current == NULL)
                continue;
            p[0] = '\0';
            for (i = 0; i < current->num_keys; i++) {
                if (strcmp(current->keys[i].name, line) == 0) {
                    if (handle_key(&current->keys[i], &p[1]) < 0) {
                        fclose(fp);
                        return -1;
                    }
                    break;
                }
            }
        } else {
            fprintf(stderr, "malformed config line: %s\n", line);
            fclose(fp);
            return -1;
        }
    }

    if (current && current->done)
        current->done(data);

    fclose(fp);

    return 0;
}

#if 0
char *
config_file_path(const char *name)
{
    const char dotconf[] = "/.config/";
    const char *config_dir;
    const char *home_dir;
    char *path;
    size_t size;

    config_dir = getenv("XDG_CONFIG_HOME");
    if (!config_dir) {
        home_dir = getenv("HOME");
        if (!home_dir) {
            fprintf(stderr, "HOME is not set, using cwd.\n");
            return strdup(name);
        }

        size = strlen(home_dir) + sizeof dotconf + strlen(name);
        path = malloc(size);
        if (!path)
            return NULL;

        snprintf(path, size, "%s%s%s", home_dir, dotconf, name);
        return path;
    }

    size = strlen(config_dir) + 1 + strlen(name) + 1;
    path = malloc(size);
    if (!path)
        return NULL;

    snprintf(path, size, "%s/%s", config_dir, name);
    return path;
}
#endif

int
open_config_file(const char *name)
{
    const char *config_dir  = getenv("XDG_CONFIG_HOME");
    const char *home_dir    = getenv("HOME");
    const char *config_dirs = getenv("XDG_CONFIG_DIRS");
    char path[PATH_MAX];
    const char *p, *next;
    int fd;

    /* Precedence is given to config files in the home directory,
     * and then to directories listed in XDG_CONFIG_DIRS and
     * finally to the current working directory. */

    /* $XDG_CONFIG_HOME */
    if (config_dir) {
        snprintf(path, sizeof path, "%s/%s", config_dir, name);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0)
            return fd;
    }

    /* $HOME/.config */
    if (home_dir) {
        snprintf(path, sizeof path, "%s/.config/%s", home_dir, name);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0)
            return fd;
        if (fd >= 0)
            return fd;
    }

    /* For each $XDG_CONFIG_DIRS: weston/<config_file> */
    if (!config_dirs)
        config_dirs = "/etc/xdg";  /* See XDG base dir spec. */

    for (p = config_dirs; *p != '\0'; p = next) {
        next = strchrnul(p, ':');
        snprintf(path, sizeof path,
                 "%.*s/weston/%s", (int)(next - p), p, name);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0)
            return fd;

        if (*next == ':')
            next++;
    }

    /* Current working directory. */
    snprintf(path, sizeof path, "./%s", name);
    fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd >= 0)
        fprintf(stderr,
                "using config in current working directory: %s\n",
                 path);
    else
        fprintf(stderr, "config file \"%s\" not found.\n", name);

    return fd;
}

