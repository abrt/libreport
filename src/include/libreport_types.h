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

GHashTable *libreport_clone_map_string(GHashTable *ms);
int libreport_try_get_map_string_item_as_bool(GHashTable *ms, const char *key, int *value);

#endif /* LIBREPORT_TYPES_H_ */
