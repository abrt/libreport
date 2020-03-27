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
#include <fnmatch.h>
#include "internal_libreport.h"

bool libreport_is_in_comma_separated_list(const char *value, const char *list)
{
    if (!list)
        return false;
    unsigned len = strlen(value);
    while (*list)
    {
        const char *comma = strchrnul(list, ',');
        if ((comma - list == len) && strncmp(value, list, len) == 0)
            return true;
        if (!*comma)
            break;
        list = comma + 1;
    }
    return false;
}

bool libreport_is_in_comma_separated_list_of_glob_patterns(const char *value, const char *list)
{
    if (!list)
        return false;
    while (*list)
    {
        const char *comma = strchrnul(list, ',');
        char *pattern = libreport_xstrndup(list, comma - list);
        int match = !fnmatch(pattern, value, /*flags:*/ 0);
        free(pattern);
        if (match)
            return true;
        if (!*comma)
            break;
        list = comma + 1;
    }
    return false;
}
