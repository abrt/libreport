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
