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
#include <glib-object.h>

#define LIST_DELIMITER ","

void glib_init(void)
{
#if (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 35)
    /* Became deprecated in glib-2.35.1 (branch 2.36 commit df02fa1e4cc61a2c7f3aafdf1a6534a831f1c0d6) */
    g_type_init();
#endif

    /* This is not necessary but is IMO is good to know that:
     *
     * If want to be defensive and ensure we're linked to GObject
     * call the following function [1]:
     *
     * g_type_ensure(G_TYPE_OBJECT);
     *
     *
     * See glib README -> Notes about GLib 2.36, 1st paragraph
     *
     * 1: https://bugzilla.gnome.org/show_bug.cgi?id=691077
     */

    /* Help with mysterious bug */
    if (g_verbose > 0)
    {
        const gchar *version_mismatch = glib_check_version(GLIB_MAJOR_VERSION,
                                                           GLIB_MINOR_VERSION,
                                                           GLIB_MICRO_VERSION);
        if (version_mismatch != NULL)
            log_warning("Running GLib incompatible version: %s", version_mismatch);
    }
}

/*
 * Parser a list of strings to Glist
 *
 * The function modifies the passed list.
 *
 * @param list a separated list of strings
 * @param delim a set of bytes that delimit the tokens in the parsed string
 * @returns GList or null if the list is empty
 */
GList *parse_delimited_list(char* list, const char *delim)
{
    if (list == NULL)
        return NULL;

    GList *l = NULL;

    char *saved_ptr = NULL;
    char *item = strtok_r(list, delim, &saved_ptr);
    while (item)
    {
        l = g_list_append(l, strtrim(xstrdup(item)));
        item = strtok_r(NULL, delim, &saved_ptr);
    }

    return l;
}

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

    char *tmp_list = xstrdup(list);

    GList *l = parse_delimited_list(tmp_list, LIST_DELIMITER);

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
