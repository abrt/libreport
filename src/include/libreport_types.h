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

#include <stdbool.h>
#include <glib.h>

typedef gchar **string_vector_ptr_t;
typedef const gchar *const *const_string_vector_const_ptr_t;

string_vector_ptr_t libreport_string_vector_new_from_string(const char *vector);
void libreport_string_vector_free(string_vector_ptr_t vector);

typedef GHashTable map_string_t;
map_string_t *libreport_clone_map_string(map_string_t *ms);
static inline
unsigned libreport_size_map_string(map_string_t *ms)
{
    if (ms == NULL)
        return 0;

    return g_hash_table_size(ms);
}
static inline
void insert_map_string(map_string_t *ms, char *key, char *value)
{
    g_hash_table_insert(ms, key, value);
}
static inline
void libreport_replace_map_string_item(map_string_t *ms, char *key, char *value)
{
    g_hash_table_replace(ms, key, value);
}

void libreport_set_map_string_item_from_bool(map_string_t *ms, const char *key, int value);
int libreport_try_get_map_string_item_as_bool(map_string_t *ms, const char *key, int *value);

void libreport_set_map_string_item_from_int(map_string_t *ms, const char *key, int value);
int libreport_try_get_map_string_item_as_int(map_string_t *ms, const char *key, int *value);

void libreport_set_map_string_item_from_uint(map_string_t *ms, const char *key, unsigned int value);
int libreport_try_get_map_string_item_as_uint(map_string_t *ms, const char *key, unsigned int *value);

void libreport_set_map_string_item_from_string(map_string_t *ms, const char *key, const char *value);
int libreport_try_get_map_string_item_as_string(map_string_t *ms, const char *key, char **value);

void libreport_set_map_string_item_from_string_vector(map_string_t *ms, const char *key, string_vector_ptr_t value);
int libreport_try_get_map_string_item_as_string_vector(map_string_t *ms, const char *key, string_vector_ptr_t *value);


typedef GHashTableIter map_string_iter_t;
static inline
int libreport_next_map_string_iter(map_string_iter_t *iter, const char **key, const char **value)
{
    return g_hash_table_iter_next(iter, (gpointer *)key, (gpointer *)value);
}

#endif /* LIBREPORT_TYPES_H_ */
