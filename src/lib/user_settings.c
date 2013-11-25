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

static char *get_conf_path(const char *name)
{
    char *s, *conf;

    s = xasprintf("abrt/settings/%s.conf", name);
    conf = concat_path_file(g_get_user_config_dir(), s);
    free(s);
    return conf;
}

bool save_app_conf_file(const char* application_name, map_string_t *settings)
{
    char *app_conf_path = get_conf_path(application_name);
    bool result = save_conf_file(app_conf_path, settings);
    free(app_conf_path);

    return result;
}

bool load_app_conf_file(const char *application_name, map_string_t *settings)
{
    char *app_conf_path = get_conf_path(application_name);
    bool result = load_conf_file(app_conf_path, settings, false);
    free(app_conf_path);

    return result;
}

void set_app_user_setting(map_string_t *settings, const char *name, const char *value)
{
    if (value)
        replace_map_string_item(settings, xstrdup(name), xstrdup(value));
    else
        remove_map_string_item(settings, name);
}

const char *get_app_user_setting(map_string_t *settings, const char *name)
{
    return get_map_string_item_or_NULL(settings, name);
}

bool save_user_settings()
{
    if (!conf_path || !user_settings)
        return true;

    return save_conf_file(conf_path, user_settings);
}

bool load_user_settings(const char *application_name)
{
    if (conf_path)
        free(conf_path);
    conf_path = get_conf_path(application_name);

    if (user_settings)
        free_map_string(user_settings);
    user_settings = new_map_string();

    return load_conf_file(conf_path, user_settings, false);
}

void set_user_setting(const char *name, const char *value)
{
    if (!user_settings)
        return;
    set_app_user_setting(user_settings, name, value);
}

const char *get_user_setting(const char *name)
{
    if (!user_settings)
        return NULL;

    return get_app_user_setting(user_settings, name);
}

GList *load_words_from_file(const char* filename)
{
    GList *words_list = NULL;
    GList *file_list = NULL;
    file_list = g_list_prepend(file_list, concat_path_file(CONF_DIR, filename));
    // get_conf_path adds .conf suffix, so we need to either change it or use it like this:
    file_list = g_list_prepend(file_list, get_conf_path("forbidden_words"));
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
            while ((line = xmalloc_fgetline(fp)) != NULL)
            {
                //FIXME: works only if the '#' is first char won't work for " #abcd#
                if (line[0] != '#') // if it's not comment
                    words_list = g_list_append(words_list, line);
                else
                    free(line);
            }
            fclose(fp);
        }
        else
        {
            log_warning("Can't open %s", cur_file);
        }

        file_list_cur = g_list_next(file_list_cur);
    }

    list_free_with_free(file_list);

    return words_list;
}

