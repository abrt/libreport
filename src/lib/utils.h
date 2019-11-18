/* Copyright (C) 2019  Red Hat, Inc.
 *
 * libreport is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libreport is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libreport.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

static inline gpointer
report_string_list_copy_func(gconstpointer src,
                             gpointer      data)
{
    (void)data;

    return g_strdup(src);
}

static inline void
report_hash_table_copy_foreach_func(gpointer key,
                                    gpointer value,
                                    gpointer user_data)
{
    g_hash_table_insert(user_data, key, value);
}
