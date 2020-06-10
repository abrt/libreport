/*
    Copyright (C) 2011  ABRT Team
    Copyright (C) 2011  RedHat inc.

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

GList *libreport_get_file_list(const char *path, const char *ext_filter)
{
    /* Load .$ext files */
    DIR *dir;
    dir = opendir(path);
    if (!dir)
        return NULL;

    GList *files = NULL;
    struct dirent *dent;
    while ((dent = readdir(dir)) != NULL)
    {
        /* skip . and .. */
        if (strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0)
            continue;
        g_autofree char *fullname = g_build_filename(path, dent->d_name, NULL);
        char *ext = NULL;

        if (ext_filter)
        {
            ext = strrchr(dent->d_name, '.');
            if (!ext)
                continue;
            if (ext_filter && strcmp(ext + 1, ext_filter) != 0)
                continue;
            *ext = '\0';
        }

//TODO: get rid of special handling of symlinks?
        struct stat buf;
        if (0 != lstat(fullname, &buf))
            continue;

        if (S_ISLNK(buf.st_mode))
        {
            GError *error = NULL;
            g_autofree gchar *link = g_file_read_link(fullname, &error);
            if (error != NULL)
            {
                error_msg("Error reading symlink '%s': %s", fullname, error->message);
                continue;
            }

            g_autofree gchar *target = g_path_get_basename(link);
            log_debug("Symlink '%s' is pointing to '%s'", link, target);
            if (ext_filter)
            {
                char *ext = strrchr(target, '.');

                if (!ext || 0 != strcmp(ext + 1, ext_filter))
                {
                    error_msg("Invalid event symlink '%s': expected it to"
                              " point to another '%s' file", fullname, ext_filter);
                    continue;
                }
                *ext = '\0';
            }
            fullname = g_build_filename(path, target, NULL);
            files = g_list_prepend(files, libreport_new_file_obj(fullname, target));

            continue;
        }

        file_obj_t *file = libreport_new_file_obj(fullname, dent->d_name);
        files = g_list_prepend(files, file);
    }

    closedir(dir);
    return files;
}

void libreport_free_file_list(GList *filelist)
{
    g_list_free_full(filelist, (GDestroyNotify)libreport_free_file_obj);
}
