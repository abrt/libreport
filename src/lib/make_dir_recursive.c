/*
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

bool make_dir_recursive(char *dir, mode_t dir_mode)
{
    bool created_parents = false;
 try_again:
    if (mkdir(dir, dir_mode) == -1 && errno != EEXIST)
    {
        int err = errno;
        if (!created_parents && errno == ENOENT)
        {
            char *p = dir + 1;
            while ((p = strchr(p, '/')) != NULL)
            {
                *p = '\0';
                int r = (mkdir(dir, 0755) == 0 || errno == EEXIST);
                *p++ = '/';
                if (!r)
                    goto report_err;
            }
            created_parents = true;
            goto try_again;
        }
 report_err:
        errno = err;
        return false;
    }

    return true;
}
