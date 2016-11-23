/*
    Copyright (C) 2015  ABRT team
    Copyright (C) 2015  RedHat Inc

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

#include "global_configuration.h"
#include "internal_libreport.h"

#define OPT_NAME_SCRUBBED_VARIABLES "ScrubbedENVVariables"
#define OPT_NAME_EXCLUDED_ELEMENTS "AlwaysExcludedElements"

static const char *const s_recognized_options[] = {
    OPT_NAME_SCRUBBED_VARIABLES,
    OPT_NAME_EXCLUDED_ELEMENTS,
    NULL,
};

static map_string_t *s_global_settings;

bool load_global_configuration(void)
{
    static const char *dirs[] = {
        CONF_DIR,
        NULL,
        NULL,
    };

    static int dir_flags[] = {
#if 0
        CONF_DIR_FLAG_NONE,
#else
        /* jfilak: RHEL-7 do not use global configuration file */
        CONF_DIR_FLAG_OPTIONAL,
#endif
        CONF_DIR_FLAG_OPTIONAL,
        -1,
    };

    if (dirs[1] == NULL)
        dirs[1] = get_user_conf_base_dir();

    return load_global_configuration_from_dirs(dirs, dir_flags);
}

bool load_global_configuration_from_dirs(const char *dirs[], int dir_flags[])
{
    if (s_global_settings == NULL)
    {
        s_global_settings = new_map_string();

        bool ret = load_conf_file_from_dirs_ext("libreport.conf", dirs, dir_flags, s_global_settings,
                                               /*don't skip without value*/ false);
        if (!ret)
        {
            error_msg("Failed to load libreport global configuration");
            free_global_configuration();
            return false;
        }

        map_string_iter_t iter;
        init_map_string_iter(&iter, s_global_settings);
        const char *key, *value;
        while(next_map_string_iter(&iter, &key, &value))
        {
            /* Die to avoid security leaks in case where someone made a typo in a option name */
            if (!is_in_string_list(key, s_recognized_options))
            {
                error_msg("libreport global configuration contains unrecognized option : '%s'", key);
                free_global_configuration();
                return false;
            }
        }
    }
    else
        log_notice("libreport global configuration already loaded");

    return true;
}

void free_global_configuration(void)
{
    if (s_global_settings != NULL)
    {
        free_map_string(s_global_settings);
        s_global_settings = NULL;
    }
}

static void assert_global_configuration_initialized(void)
{
    if (NULL == s_global_settings)
    {
        error_msg("libreport global configuration is not initialized");
        abort();
    }
}

#define get_helper(type, getter, name) \
    ({ \
    assert_global_configuration_initialized(); \
    type opt; \
    if (getter(s_global_settings, name, &opt)) \
        /* Die to avoid security leaks in case where someone made a error */ \
        error_msg_and_die("libreport global settings contains invalid data: '"name"'"); \
    opt;\
    })

string_vector_ptr_t get_global_always_excluded_elements(void)
{
    assert_global_configuration_initialized();

    char *env_exclude = getenv("EXCLUDE_FROM_REPORT");
    const char *gc_exclude = get_map_string_item_or_NULL(s_global_settings, OPT_NAME_EXCLUDED_ELEMENTS);

    if (env_exclude != NULL && gc_exclude == NULL)
        return string_vector_new_from_string(env_exclude);

    if (env_exclude == NULL && gc_exclude != NULL)
        return string_vector_new_from_string(gc_exclude);

    if (env_exclude == NULL && gc_exclude == NULL)
        return string_vector_new_from_string(NULL);

    char *joined_exclude = xasprintf("%s, %s", env_exclude, gc_exclude);
    string_vector_ptr_t ret = string_vector_new_from_string(joined_exclude);
    free(joined_exclude);

    return ret;
}

bool get_global_create_private_ticket(void)
{
    assert_global_configuration_initialized();

    char *env_create_private = getenv(CREATE_PRIVATE_TICKET);

    if (env_create_private == NULL)
        return false;

    return string_to_bool(env_create_private);
}

void set_global_create_private_ticket(bool enabled, int flags/*unused - persistent*/)
{
    assert_global_configuration_initialized();

    if (enabled)
        xsetenv(CREATE_PRIVATE_TICKET, "1");
    else
        safe_unsetenv(CREATE_PRIVATE_TICKET);
}

bool get_global_stop_on_not_reportable(void)
{
    assert_global_configuration_initialized();

    char *env_create_private = getenv(STOP_ON_NOT_REPORTABLE);

    if (env_create_private == NULL)
        return true;

    return string_to_bool(env_create_private);
}

void set_global_stop_on_not_reportable(bool enabled, int flags/*unused - persistent*/)
{
    assert_global_configuration_initialized();

    if (enabled)
        xsetenv(STOP_ON_NOT_REPORTABLE, "1");
    else
        xsetenv(STOP_ON_NOT_REPORTABLE, "0");
}
