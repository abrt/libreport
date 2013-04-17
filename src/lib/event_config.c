/*
    Copyright (C) 2011  ABRT Team
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

GHashTable *g_event_config_list;
static GHashTable *g_event_config_symlinks;

event_option_t *new_event_option(void)
{
    return xzalloc(sizeof(event_option_t));
}

event_config_t *new_event_config(const char *name)
{
    event_config_t *e = xzalloc(sizeof(event_config_t));
    e->info = new_config_info(name);
    return e;
}

config_item_info_t *ec_get_config_info(event_config_t * ec)
{
    return ec->info;
}

void ec_set_screen_name(event_config_t *ec, const char *screen_name)
{
    ci_set_screen_name(ec->info, screen_name);
}

const char *ec_get_screen_name(event_config_t *ec)
{
    return ci_get_screen_name(ec->info);
}

const char *ec_get_description(event_config_t *ec)
{
    return ci_get_description(ec->info);
}

void ec_set_description(event_config_t *ec, const char *description)
{
    ci_set_description(ec->info, description);
}

const char *ec_get_name(event_config_t *ec)
{
    return ci_get_name(ec->info);
}

const char *ec_get_long_desc(event_config_t *ec)
{
    return ci_get_long_desc(ec->info);
}

void ec_set_long_desc(event_config_t *ec, const char *long_descr)
{
    ci_set_long_desc(ec->info, long_descr);
}

bool ec_is_configurable(event_config_t* ec)
{
    return g_list_length(ec->options) > 0;
}

void ec_print(event_config_t *ec)
{
    printf("%s\n\t%s\n\t%s\n",
        ec_get_name(ec),
        ec_get_screen_name(ec),
        ec_get_description(ec)
        );
}

void free_event_option(event_option_t *p)
{
    if (!p)
        return;
    free(p->eo_name);
    free(p->eo_value);
    free(p->eo_label);
    free(p->eo_note_html);
    //free(p->eo_description);
    //free(p->eo_allowed_value);
    free(p);
}

void free_event_config(event_config_t *p)
{
    if (!p)
        return;

    free_config_info(p->info);
    free(p->ec_creates_items);
    free(p->ec_requires_items);
    free(p->ec_exclude_items_by_default);
    free(p->ec_include_items_by_default);
    free(p->ec_exclude_items_always);
    GList *opt;
    for (opt = p->options; opt; opt = opt->next)
        free_event_option(opt->data);
    g_list_free(p->options);

    free(p);
}


static int cmp_event_option_name_with_string(gconstpointer a, gconstpointer b)
{
    const event_option_t *evopt = a;
    return !evopt->eo_name || strcmp(evopt->eo_name, (char *)b) != 0;
}

event_option_t *get_event_option_from_list(const char *name, GList *options)
{
    GList *elem = g_list_find_custom(options, name, &cmp_event_option_name_with_string);
    if (elem)
        return (event_option_t *)elem->data;
    return NULL;
}

static void load_config_files(const char *dir_path)
{
    GList *conf_files = get_file_list(dir_path, "conf");
    while (conf_files != NULL)
    {
        file_obj_t *file = (file_obj_t *)conf_files->data;
        char *fullpath = file->fullpath;
        char *filename = file->filename;

        event_config_t *event_config = get_event_config(filename);
        bool new_config = (!event_config);
        if (new_config)
            event_config = new_event_config(filename);

        map_string_t *keys_and_values = new_map_string();

        load_conf_file(fullpath, keys_and_values, /*skipKeysWithoutValue:*/ false);

        /* Insert or replace every key/value from keys_and_values to event_config->option */
        map_string_iter_t iter;
        const char *name;
        const char *value;
        init_map_string_iter(&iter, keys_and_values);
        while (next_map_string_iter(&iter, &name, &value))
        {
            event_option_t *opt;
            GList *elem = g_list_find_custom(event_config->options, name,
                                            cmp_event_option_name_with_string);
            if (elem)
            {
                opt = elem->data;
                // log("conf: replacing '%s' value:'%s'->'%s'", name, opt->value, value);
                free(opt->eo_value);
            }
            else
            {
                // log("conf: new value %s='%s'", name, value);
                opt = new_event_option();
                opt->eo_name = xstrdup(name);
            }
            opt->eo_value = xstrdup(value);
            if (!elem)
                event_config->options = g_list_append(event_config->options, opt);
        }

        free_map_string(keys_and_values);

        if (new_config)
            g_hash_table_replace(g_event_config_list, xstrdup(ec_get_name(event_config)), event_config);

        free_file_obj(file);
        conf_files = g_list_delete_link(conf_files, conf_files);
    }
}

/* (Re)loads data from /etc/abrt/events/foo.{xml,conf} and $XDG_CACHE_HOME/abrt/events/foo.conf */
GHashTable *load_event_config_data(void)
{
    free_event_config_data();

    if (!g_event_config_list)
        g_event_config_list = g_hash_table_new_full(
                /*hash_func*/ g_str_hash,
                /*key_equal_func:*/ g_str_equal,
                /*key_destroy_func:*/ free,
                /*value_destroy_func:*/ (GDestroyNotify) free_event_config
        );
    if (!g_event_config_symlinks)
        g_event_config_symlinks = g_hash_table_new_full(
                /*hash_func*/ g_str_hash,
                /*key_equal_func:*/ g_str_equal,
                /*key_destroy_func:*/ free,
                /*value_destroy_func:*/ free
        );

    GList *event_files = get_file_list(EVENTS_DIR, "xml");
    while (event_files)
    {
        file_obj_t *file = (file_obj_t *)event_files->data;

        event_config_t *event_config = get_event_config(file->filename);
        bool new_config = (!event_config);
        if (new_config)
           event_config = new_event_config(file->filename);

        load_event_description_from_file(event_config, file->fullpath);

        if (new_config)
            g_hash_table_replace(g_event_config_list, xstrdup(ec_get_name(event_config)), event_config);

        free_file_obj(file);
        event_files = g_list_delete_link(event_files, event_files);
    }

    load_config_files(EVENTS_DIR);

    char *cachedir;
    cachedir = concat_path_file(g_get_user_cache_dir(), "abrt/events");
    load_config_files(cachedir);
    free(cachedir);

    return g_event_config_list;
}

/* Frees all loaded data */
void free_event_config_data(void)
{
    if (g_event_config_list)
    {
        g_hash_table_destroy(g_event_config_list);
        g_event_config_list = NULL;
    }
    if (g_event_config_symlinks)
    {
        g_hash_table_destroy(g_event_config_symlinks);
        g_event_config_symlinks = NULL;
    }
}

event_config_t *get_event_config(const char *name)
{
    if (!g_event_config_list)
        return NULL;
    if (g_event_config_symlinks)
    {
        char *link = g_hash_table_lookup(g_event_config_symlinks, name);
        if (link)
            name = link;
    }
    return g_hash_table_lookup(g_event_config_list, name);
}

GList *export_event_config(const char *event_name)
{
    GList *env_list = NULL;

    event_config_t *config = get_event_config(event_name);
    if (config)
    {
        GList *lopt;
        for (lopt = config->options; lopt; lopt = lopt->next)
        {
            event_option_t *opt = lopt->data;
            if (!opt->eo_value)
                continue;
            char *var_val = xasprintf("%s=%s", opt->eo_name, opt->eo_value);
            VERB3 log("Exporting '%s'", var_val);
            env_list = g_list_prepend(env_list, var_val);
            putenv(var_val);
        }
    }

    return env_list;
}

void unexport_event_config(GList *env_list)
{
    while (env_list)
    {
        char *var_val = env_list->data;
        VERB3 log("Unexporting '%s'", var_val);
        safe_unsetenv(var_val);
        env_list = g_list_remove(env_list, var_val);
        free(var_val);
    }
}

/* return NULL if successful otherwise appropriate error message */
static char *validate_event_option(event_option_t *opt)
{
    if (!opt->eo_allow_empty && (!opt->eo_value || !opt->eo_value[0]))
        return xstrdup(_("Missing mandatory value"));

    /* if value is NULL and allow-empty yes than it doesn't make sence to check it */
    if (!opt->eo_value)
        return NULL;

    const gchar *s = NULL;
    if (!g_utf8_validate(opt->eo_value, -1, &s))
            return xasprintf(_("Invalid utf8 character '%c'"), *s);

    switch (opt->eo_type) {
    case OPTION_TYPE_TEXT:
    case OPTION_TYPE_PASSWORD:
        break;
    case OPTION_TYPE_NUMBER:
    {
        char *endptr;
        errno = 0;
        long r = strtol(opt->eo_value, &endptr, 10);
        (void) r;
        if (errno != 0 || endptr == opt->eo_value || *endptr != '\0')
            return xasprintf(_("Invalid number '%s'"), opt->eo_value);

        break;
    }
    case OPTION_TYPE_BOOL:
        /* note: should match strings which string_to_bool accepts */
        if (strcasecmp(opt->eo_value, "yes") != 0
         && strcasecmp(opt->eo_value, "no") != 0
         && strcasecmp(opt->eo_value, "on") != 0
         && strcasecmp(opt->eo_value, "off") != 0
         && strcasecmp(opt->eo_value, "true") != 0
         && strcasecmp(opt->eo_value, "false") != 0
         && strcmp(opt->eo_value, "1") != 0
         && strcmp(opt->eo_value, "0") != 0
        ) {
            return xasprintf(_("Invalid boolean value '%s'"), opt->eo_value);
        }
        break;
    case OPTION_TYPE_HINT_HTML:
        return NULL;
    default:
        return xstrdup(_("Unsupported option type"));
    };

    return NULL;
}

GHashTable *validate_event(const char *event_name)
{
    event_config_t *config = get_event_config(event_name);
    if (!config)
        return NULL;

    GHashTable *errors = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
    GList *li;

    for (li = config->options; li; li = li->next)
    {
        event_option_t *opt = (event_option_t *)li->data;
        char *err = validate_event_option(opt);
        if (err)
            g_hash_table_insert(errors, xstrdup(opt->eo_name), err);
    }

    if (g_hash_table_size(errors))
        return errors;

    g_hash_table_destroy(errors);

    return NULL;
}

/*
 * This function checks if a value of problem's backtrace rating is acceptable
 * for event according to event's required rating value.
 *
 * The solved problem seems to be pretty straightforward but there are some
 * pitfalls.
 *
 * 1. Is rating acceptable if event doesn't have configuration?
 * 2. Is rating acceptable if problem's data doesn't contains rating value?
 * 3. Is rating acceptable if rating value is not a number?
 * 4. What message show to user if there is a concern about usability?
 */
bool check_problem_rating_usability(const event_config_t *cfg,
                                    problem_data_t *pd,
                                    char **description,
                                    char **detail)
{
    char *tmp_desc = NULL;
    char *tmp_detail = NULL;
    bool result = true;

    if (!cfg)
        goto finish;

    const char *rating_str = problem_data_get_content_or_NULL(pd, FILENAME_RATING);

    if (!rating_str)
        goto finish;

    /* just to be sure */
    result = false;

    const long minimal_rating = cfg->ec_minimal_rating;
    char *endptr;
    errno = 0;
    const long rating = strtol(rating_str, &endptr, 10);
    if (errno != 0 || endptr == rating_str || *endptr != '\0')
    {
        tmp_desc = xasprintf("%s '%s'.", _("Reporting disabled because the rating does not contain a number."), rating_str);
        tmp_detail = xstrdup(_("Please report this problem to ABRT project developers."));

        result = false;
    }
    else if (rating == minimal_rating) /* bt is usable, but not complete, so show a warning */
    {
        tmp_desc = xstrdup(_("The backtrace is incomplete, please make sure you provide the steps to reproduce."));
        tmp_detail = xstrdup(_("The backtrace probably can't help developer to diagnose the bug."));

        result = true;
    }
    else if (rating < minimal_rating)
    {
        tmp_desc = xstrdup(_("Reporting disabled because the backtrace is unusable."));

        const char *package = problem_data_get_content_or_NULL(pd, FILENAME_PACKAGE);
        if (package && package[0])
            tmp_detail = xasprintf(_("Please try to install debuginfo manually using the command: \"debuginfo-install %s\" and try again."), package);
        else
            tmp_detail = xstrdup(_("A proper debuginfo is probably missing or the coredump is corrupted."));

        result = false;
    }
    else /* rating > minimal_rating */
        result = true;

finish:
    if (description)
        *description = tmp_desc;
    else
        free(tmp_desc);

    if (detail)
        *detail = tmp_detail;
    else
        free(tmp_detail);

    return result;
}
