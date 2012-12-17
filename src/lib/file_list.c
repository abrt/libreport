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

GList *get_file_list(const char *path, const char *ext_filter)
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
        char *ext = strrchr(dent->d_name, '.');
        if (!ext)
            continue;
        if (ext_filter && strcmp(ext + 1, ext_filter) != 0)
            continue;

        char *fullname = concat_path_file(path, dent->d_name);
        *ext = '\0';

//TODO: get rid of special handling of symlinks?
        struct stat buf;
        if (0 != lstat(fullname, &buf))
            goto next;

        if (S_ISLNK(buf.st_mode))
        {
            GError *error = NULL;
            gchar *link = g_file_read_link(fullname, &error);
            if (error != NULL)
                error_msg_and_die("Error reading symlink '%s': %s", fullname, error->message);

            gchar *target = g_path_get_basename(link);
            char *ext = strrchr(target, '.');

//FIXME: why "xml"? Shouldn't it be ext_filter?
            if (!ext || 0 != strcmp(ext + 1, "xml"))
                error_msg_and_die("Invalid event symlink '%s': expected it to"
                                  " point to another xml file", fullname);
            *ext = '\0';
            //@@TODO symlink handling is broken!!
            //g_hash_table_replace(g_event_config_symlinks, xstrdup(dent->d_name), target);
            g_free(link);
            /* don't free target, it is owned by the hash table now */

            goto next;
        }

        file_obj_t *file = new_file_obj(fullname, dent->d_name);
        files = g_list_prepend(files, file);
 next:
        free(fullname);
    }

    closedir(dir);
    return files;
}

void free_file_list(GList *filelist)
{
    g_list_free_full(filelist, (GDestroyNotify)free_file_obj);
}
