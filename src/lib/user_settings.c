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

static GHashTable *user_settings;
static char *conf_path;

static char *get_conf_path(const char *name)
{
    char *HOME = getenv("HOME"), *s, *conf;

    s = xasprintf("%s/%s.conf", ".abrt/settings", name);
    conf = concat_path_file(HOME, s);
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
        g_hash_table_destroy(user_settings);
    user_settings = g_hash_table_new_full(
            /*hash_func*/ g_str_hash,
            /*key_equal_func:*/ g_str_equal,
            /*key_destroy_func:*/ free,
            /*value_destroy_func:*/ free);

    return load_conf_file(conf_path, user_settings, false);
}

void set_user_setting(const char *name, const char *value)
{
    if (!user_settings)
        return;

    if (value)
        g_hash_table_replace(user_settings, xstrdup(name), xstrdup(value));
    else
        g_hash_table_remove(user_settings, name);
}

const char *get_user_setting(const char *name)
{
    if (!user_settings)
        return NULL;

    return g_hash_table_lookup(user_settings, name);
}

GList *load_forbidden_words(void)
{
    const char *conf_file = "forbidden_words.conf";
    GList *words_list = NULL;
    GList *file_list = NULL;
    file_list = g_list_prepend(file_list, concat_path_file(CONF_DIR, conf_file));
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
            VERB1 log("Can't open %s", cur_file);
        }

        file_list_cur = g_list_next(file_list_cur);
    }

    list_free_with_free(file_list);

    return words_list;
}

