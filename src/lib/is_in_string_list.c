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

bool is_in_string_list(const char *name, const char *const *v)
{
    while (*v)
    {
        if (strcmp(*v, name) == 0)
            return true;
        v++;
    }
    return false;
}

int index_of_string_in_list(const char *name, const char *const *v)
{
    for(int i = 0; v[i]; ++i)
    {
        if (strcmp(v[i], name) == 0)
            return i;
    }
    return -1;
}
