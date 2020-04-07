/*
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Copyright (C) 2010  ABRT team
 * Copyright (C) 2010  RedHat Inc
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

/* Like strcpy but can copy overlapping strings. */
void libreport_overlapping_strcpy(char *dst, const char *src)
{
    /* Cheap optimization for dst == src case -
     * better to have it here than in many callers.
     */
    if (dst != src)
    {
        while ((*dst = *src) != '\0')
        {
            dst++;
            src++;
        }
    }
}
