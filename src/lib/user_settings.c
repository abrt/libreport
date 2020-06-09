/*
    Copyright (C) 2011  RedHat inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include "internal_libreport.h"
#include <augeas.h>

static map_string_t *user_settings;
static char *conf_path;

static char *get_user_config_file_path(const char *name, const char *suffix)
{
    g_autofree char *s = NULL;
    char *conf;

    if (suffix != NULL)
        s = g_strdup_printf("%s.%s", name, suffix);

    conf = g_build_filename(libreport_get_user_conf_base_dir(), s != NULL ? s : name, NULL);

    return conf;
}

static char *get_conf_path(const char *name)
{
    return get_user_config_file_path(name, "conf");
}

bool libreport_save_app_conf_file(const char* application_name, map_string_t *settings)
{
    char *app_conf_path = get_conf_path(application_name);
    bool result = libreport_save_conf_file(app_conf_path, settings);
    free(app_conf_path);

    return result;
}

bool libreport_load_app_conf_file(const char *application_name, map_string_t *settings)
{
    char *app_conf_path = get_conf_path(application_name);
    bool result = libreport_load_conf_file(app_conf_path, settings, false);
    free(app_conf_path);

    return result;
}

void libreport_set_app_user_setting(map_string_t *settings, const char *name, const char *value)
{
    if (value)
        libreport_replace_map_string_item(settings, g_strdup(name), g_strdup(value));
    else
        libreport_remove_map_string_item(settings, name);
}

const char *libreport_get_app_user_setting(map_string_t *settings, const char *name)
{
    return libreport_get_map_string_item_or_NULL(settings, name);
}

bool libreport_save_user_settings()
{
    if (!conf_path || !user_settings)
        return true;

    return libreport_save_conf_file(conf_path, user_settings);
}

bool libreport_load_user_settings(const char *application_name)
{
    if (conf_path)
        free(conf_path);
    conf_path = get_conf_path(application_name);

    if (user_settings)
        libreport_free_map_string(user_settings);
    user_settings = libreport_new_map_string();

    return libreport_load_conf_file(conf_path, user_settings, false);
}

void libreport_set_user_setting(const char *name, const char *value)
{
    if (!user_settings)
        return;
    libreport_set_app_user_setting(user_settings, name, value);
}

const char *libreport_get_user_setting(const char *name)
{
    if (!user_settings)
        return NULL;

    return libreport_get_app_user_setting(user_settings, name);
}

GList *libreport_load_words_from_file(const char* filename)
{
    GList *words_list = NULL;
    GList *file_list = NULL;
    file_list = g_list_prepend(file_list, g_build_filename(CONF_DIR ? CONF_DIR : "", filename, NULL));
    file_list = g_list_prepend(file_list, get_user_config_file_path(filename, /*don't append suffix*/NULL));
    GList *file_list_cur = file_list;

    while(file_list_cur)
    {
        char *cur_file = (char *)file_list_cur->data;
        FILE *fp = fopen(cur_file, "r");
        if (fp)
        {
            /* every line is one word
             */
            char *line;
            while ((line = libreport_xmalloc_fgetline(fp)) != NULL)
            {
                //FIXME: works only if the '#' is first char won't work for " #abcd#
                if (line[0] != '#') // if it's not comment
                    words_list = g_list_append(words_list, line);
                else
                    free(line);
            }
            fclose(fp);
        }
        /* Don't disturb users with useless warnings about missing files. */
        else if (errno != ENOENT || libreport_g_verbose >= 3)
        {
            perror_msg("Can't open %s", cur_file);
        }

        file_list_cur = g_list_next(file_list_cur);
    }

    libreport_list_free_with_free(file_list);

    return words_list;
}

