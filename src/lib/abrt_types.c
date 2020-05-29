/*
    Copyright (C) 2010  ABRT Team
    Copyright (C) 2010  RedHat inc.

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

map_string_t *libreport_clone_map_string(map_string_t *ms)
{
    if (ms == NULL)
        return NULL;

    map_string_t *clone = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);

    const char *key;
    const char *value;
    map_string_iter_t iter;
    libreport_init_map_string_iter(&iter, ms);
    while(libreport_next_map_string_iter(&iter, &key, &value))
        insert_map_string(clone, g_strdup(key), g_strdup(value));

    return clone;
}

const char *libreport_get_map_string_item_or_empty(map_string_t *ms, const char *key)
{
    const char *v = (const char*)g_hash_table_lookup(ms, key);
    if (!v) v = "";
    return v;
}

string_vector_ptr_t libreport_string_vector_new_from_string(const char *value)
{
    return g_strsplit(value == NULL ? "" : value, ", ", /*all tokens*/0);
}

void libreport_string_vector_free(string_vector_ptr_t vector)
{
    g_strfreev(vector);
}

void libreport_set_map_string_item_from_bool(map_string_t *ms, const char *key, int value)
{
    const char *const raw_value = value ? "yes" : "no";
    libreport_set_map_string_item_from_string(ms, key, raw_value);
}

#define GET_ITEM_OR_RETURN(val_name, conf, item_name)\
    const char *const val_name = libreport_get_map_string_item_or_NULL(conf, item_name); \
    if (val_name == NULL) \
    { \
        log_debug("Configuration option '%s' not found in loaded settings", item_name); \
        return 0; \
    }

int libreport_try_get_map_string_item_as_bool(map_string_t *ms, const char *key, int *value)
{
    GET_ITEM_OR_RETURN(option, ms, key);

    *value = libreport_string_to_bool(option);
    return true;
}

void libreport_set_map_string_item_from_int(map_string_t *ms, const char *key, int value)
{
    char raw_value[sizeof(int)*3 + 1];
    snprintf(raw_value, sizeof(raw_value), "%d", value);
    libreport_set_map_string_item_from_string(ms, key, raw_value);
}

int libreport_try_get_map_string_item_as_int(map_string_t *ms, const char *key, int *value)
{
    GET_ITEM_OR_RETURN(option, ms, key);

    char *endptr = NULL;
    errno = 0;
    long raw_value = strtol(option, &endptr, 10);

    /* Check for various possible errors */
    if (raw_value > INT_MAX || raw_value < INT_MIN || errno == ERANGE)
    {
        log_warning("Value of option '%s' is out of integer range", key);
        return 0;
    }

    if ((errno != 0 && raw_value == 0)
        || (endptr == option) /* empty */
        || (endptr[0] != '\0') /* trailing non-digits */)
    {
        log_warning("Value of option '%s' is not an integer", key);
        return 0;
    }

    *value = (int)raw_value;
    return 1;
}

void libreport_set_map_string_item_from_uint(map_string_t *ms, const char *key, unsigned int value)
{
    char raw_value[sizeof(int)*3 + 1];
    snprintf(raw_value, sizeof(raw_value), "%u", value);
    libreport_set_map_string_item_from_string(ms, key, raw_value);
}

int libreport_try_get_map_string_item_as_uint(map_string_t *ms, const char *key, unsigned int *value)
{
    GET_ITEM_OR_RETURN(option, ms, key);

    char *endptr = NULL;

    /* Check just negative number, positive ranges will be tested later */
    errno = 0;
    long raw_signed_value = strtol(option, &endptr, 10);
    if (raw_signed_value < 0)
    {
        log_warning("Value of option '%s' is out of unsigned integer range (bellow zero)", key);
        return 0;
    }

    unsigned long raw_value;
    if (raw_signed_value < INT_MAX)
    {   // no need to convert it again, this is already between 0 and maximal value of strtol()
        raw_value = raw_signed_value;
    }
    else
    {
        errno = 0;
        raw_value = strtoul(option, &endptr, 10);

        /* Check range */
        if (raw_value > UINT_MAX || errno == ERANGE)
        {
            log_warning("Value of option '%s' is out of unsigned integer range", key);
            return 0;
        }
    }

    /* Check for other possible errors */
    if ((errno != 0 && raw_value == 0)
        || (endptr == option) /* empty */
        || (endptr[0] != '\0') /* trailing non-digits */)
    {
        log_warning("Value of option '%s' is not an unsigned integer", key);
        return 0;
    }

    *value = (unsigned int)raw_value;
    return 1;
}

void libreport_set_map_string_item_from_string(map_string_t *ms, const char *key, const char *value)
{
    libreport_replace_map_string_item(ms, g_strdup(key), g_strdup(value));
}

int libreport_try_get_map_string_item_as_string(map_string_t *ms, const char *key, char **value)
{
    GET_ITEM_OR_RETURN(option, ms, key);

    char *dup = strdup(option);
    if (dup == NULL)
    {
        log_warning("Insufficient memory for value of option '%s'", key);
        return 0;
    }

    *value = dup;
    return 1;
}

void libreport_set_map_string_item_from_string_vector(map_string_t *ms, const char *key, string_vector_ptr_t value)
{
    if (value == NULL)
    {
        libreport_set_map_string_item_from_string(ms, key, "");
        return;
    }

    gchar *opt_val = g_strjoinv(", ", (gchar **)value);
    libreport_set_map_string_item_from_string(ms, key, opt_val);
    g_free(opt_val);
}

int libreport_try_get_map_string_item_as_string_vector(map_string_t *ms, const char *key, string_vector_ptr_t *value)
{
    GET_ITEM_OR_RETURN(option, ms, key);

    *value = libreport_string_vector_new_from_string(option);
    return 1;
}
