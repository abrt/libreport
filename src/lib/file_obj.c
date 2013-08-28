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

file_obj_t *new_file_obj(const char* fullpath, const char* filename)
{
    file_obj_t *file = xmalloc(sizeof(file_obj_t));
    file->fullpath = xstrdup(fullpath);
    file->filename = xstrdup(filename);
    return file;
}

void free_file_obj(file_obj_t *f)
{
    if (f == NULL)
        return

    free(f->fullpath);
    free(f->filename);
    free(f);
}

const char *fo_get_fullpath(file_obj_t *fo)
{
    return fo->fullpath;
}

const char *fo_get_filename(file_obj_t *fo)
{
    return fo->filename;
}