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

#include <gio/gio.h>

static int report_copy_gfile_recursive(GFile *source, GFile *destination)
{
    const char *blacklist[] =
    {
        ".libreport",
        ".lock",
    };
    g_autofree char *name = NULL;
    g_autoptr(GError) error = NULL;
    bool file_copied;
    bool recurse;

    name = g_file_get_basename(source);
    for (size_t i = 0; i < G_N_ELEMENTS(blacklist); i++)
    {
        if (g_strcmp0(name, blacklist[i]) == 0)
        {
            log_debug("Skipping “%s”", name);

            return 0;
        }
    }
    file_copied = g_file_copy(source, destination,
                              (G_FILE_COPY_OVERWRITE |
                               G_FILE_COPY_NOFOLLOW_SYMLINKS |
                               G_FILE_COPY_ALL_METADATA),
                              NULL, NULL, NULL, &error);
    recurse = !file_copied && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_MERGE);
    if (recurse)
    {
        g_autoptr(GFileEnumerator) enumerator = NULL;
        GFileInfo *child_info;
        GFile *child;

        g_clear_error(&error);

        enumerator = g_file_enumerate_children(source,
                                               G_FILE_ATTRIBUTE_STANDARD_NAME,
                                               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                               NULL, &error);
        if (NULL != error)
        {
            log_error("Error occurred while enumerating files: %s", error->message);

            return -1;
        }

        while (g_file_enumerator_iterate(enumerator, &child_info, &child, NULL, &error))
        {
            const char *child_name;
            g_autoptr(GFile) child_destination = NULL;

            if (NULL == child)
            {
                break;
            }

            child_name = g_file_info_get_name(child_info);
            child_destination = g_file_get_child(destination, child_name);

            report_copy_gfile_recursive(child, child_destination);
        }

        if (NULL != error)
        {
            log_error("Error occurred while iterating files: %s", error->message);

            return -1;
        }
    }
    else if (NULL != error)
    {
        log_error("Error occurred while copying file: %s", error->message);

        return -1;
    }

    return 0;
}

int copy_file_recursive(const char *source, const char *dest)
{
    g_autoptr(GFile) source_file = NULL;
    g_autoptr(GFile) destination_file = NULL;

    g_return_val_if_fail(NULL != source, -1);
    g_return_val_if_fail(NULL != dest, -1);

    source_file = g_file_new_for_path(source);
    destination_file = g_file_new_for_path(dest);

    return report_copy_gfile_recursive(source_file, destination_file);
}
