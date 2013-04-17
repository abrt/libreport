/*
    Copyright (C) 2013  ABRT team
    Copyright (C) 2013  RedHat Inc

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
#ifndef LIBREPORT_TYPES_H_
#define LIBREPORT_TYPES_H_

#include <glib.h>

typedef GHashTable map_string_t;
#define new_map_string libreport_new_map_string
map_string_t *new_map_string(void);
#define free_map_string libreport_free_map_string
void free_map_string(map_string_t *ms);
#define insert_map_string_item libreport_insert_map_string_item
static inline
void insert_map_string(map_string_t *ms, char *key, char *value)
{
    g_hash_table_insert(ms, key, value);
}
#define replace_map_string_item libreport_replace_map_string_item
static inline
void replace_map_string_item(map_string_t *ms, char *key, char *value)
{
    g_hash_table_replace(ms, key, value);
}
#define remove_map_string_item libreport_remove_map_string_item
static inline
void remove_map_string_item(map_string_t *ms, const char *key)
{
    g_hash_table_remove(ms, key);
}
#define get_map_string_item_or_empty libreport_get_map_string_item_or_empty
const char *get_map_string_item_or_empty(map_string_t *ms, const char *key);
#define get_map_string_item_or_NULL libreport_get_map_string_item_or_NULL
static inline
const char *get_map_string_item_or_NULL(map_string_t *ms, const char *key)
{
    return (const char*)g_hash_table_lookup(ms, key);
}

typedef GHashTableIter map_string_iter_t;
#define init_map_string_iter libreport_init_map_string_iter
static inline
void init_map_string_iter(map_string_iter_t *iter, map_string_t *ms)
{
    g_hash_table_iter_init(iter, ms);
}
#define next_map_string_iter libreport_next_map_string_iter
static inline
int next_map_string_iter(map_string_iter_t *iter, const char **key, const char **value)
{
    return g_hash_table_iter_next(iter, (gpointer *)key, (gpointer *)value);
}

#endif /* LIBREPORT_TYPES_H_ */
