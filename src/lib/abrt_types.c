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

    gpointer key;
    gpointer value;
    map_string_iter_t iter;
    g_hash_table_iter_init(&iter, ms);
    while(g_hash_table_iter_next(&iter, &key, &value))
        g_hash_table_insert(clone, g_strdup((char *)key), g_strdup((char *)value));

    return clone;
}

#define GET_ITEM_OR_RETURN(val_name, conf, item_name)\
    const char *const val_name = g_hash_table_lookup(conf, item_name); \
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
    g_hash_table_replace(ms, g_strdup(key), g_strdup(raw_value));
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
    g_hash_table_replace(ms, g_strdup(key), g_strdup(raw_value));
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
