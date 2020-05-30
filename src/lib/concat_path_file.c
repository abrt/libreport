/*
 * Utility routines.
 *
 * Copyright (C) 2001 Erik Andersen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "internal_libreport.h"

char *libreport_concat_path_basename(const char *path, const char *filename)
{
    g_autofree char *abspath = realpath(filename, NULL);
    char *base = strrchr((abspath ? abspath : filename), '/');

    /* If realpath failed and filename is malicious (say, "/foo/.."),
     * we may end up tricked into doing some bad things. Don't allow that.
     */
    char buf[sizeof("tmp-"LIBREPORT_ISO_DATE_STRING_SAMPLE"-%lu")];
    if (base && base[1] != '\0' && base[1] != '.')
    {
        /* We have a slash and it's not "foo/" or "foo/.<something>" */
        base++;
    }
    else
    {
        sprintf(buf, "tmp-%s-%lu", libreport_iso_date_string(NULL), (long)getpid());
        base = buf;
    }
    char *name = g_build_filename(path ? path : "", base, NULL);
    return name;
}

bool libreport_str_is_correct_filename(const char *str)
{
#define NOT_PRINTABLE(c) (c < ' ' || c == 0x7f)

    if (NOT_PRINTABLE(*str) || *str == '/' || *str == '\0')
        return false;
    ++str;

    if (*str == '\0')
        return *(str-1) != '.';
    if (NOT_PRINTABLE(*str) || *str == '/')
        return false;
    ++str;

    if (*str == '\0')
        return !(*(str-2) == '.' && *(str-1) == '.');
    if (NOT_PRINTABLE(*str) || *str == '/')
        return false;
    ++str;

    for (unsigned i = 0; *str != '\0' && i < 61; ++str, ++i)
        if (NOT_PRINTABLE(*str) || *str == '/')
            return false;

    return *str == '\0';

#undef NOT_PRINTABLE
}
