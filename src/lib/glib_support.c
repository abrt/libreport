/*
    Copyright (C) 2011  ABRT team
    Copyright (C) 2011  RedHat Inc

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

#define LIST_DELIMITER ","

/*
 * Parser comma separated list of strings to Glist
 *
 * @param list comma separated list of strings
 * @returns GList or null if the list is empty
 */
GList *parse_list(const char* list)
{
    if (list == NULL)
        return NULL;

    GList *l = NULL;

    char *saved_ptr = NULL;
    char *tmp_list = xstrdup(list);
    char *item = strtok_r(tmp_list, LIST_DELIMITER, &saved_ptr);
    while (item)
    {
        l = g_list_append(l, strtrim(xstrdup(item)));
        item = strtok_r(NULL, LIST_DELIMITER, &saved_ptr);
    }

    free(tmp_list);
    return l;
}

void list_free_with_free(GList *list)
{
    GList *li;
    for (li = list; li; li = g_list_next(li))
        free(li->data);
    g_list_free(list);
}
