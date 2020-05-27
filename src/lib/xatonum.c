/*
 * Utility routines.
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
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

int libreport_try_atou(const char *numstr, unsigned *value)
{
    int ret = 0;
    unsigned long r;
    int old_errno;
    char *e;

    g_return_val_if_fail (NULL != numstr, -EINVAL);

    old_errno = errno;
    if (*numstr < '0' || *numstr > '9')
    {
        ret = -EINVAL;
        goto finito;
    }

    errno = 0;
    r = strtoul(numstr, &e, 10);
    if (errno || numstr == e || *e != '\0')
    {
        ret = errno != 0 ? -errno : -EINVAL; /* error / no digits / illegal trailing chars */
        goto finito;
    }

    /* check range */
    if (r > UINT_MAX)
    {
        /* In this case errno is probably 0, because UINT_MAX < ULONG_MAX, thus
         * strtoul should not return an error */
        ret = -ERANGE;
        goto finito;
    }

    *value = r;

finito:
    errno = old_errno; /* Ok.  So restore errno. */
    return ret;
}

unsigned libreport_xatou(const char *numstr)
{
    unsigned value = (unsigned)-1;

    if (libreport_try_atou(numstr, &value) != 0)
        error_msg_and_die("expected number in range <0, %d>: '%s'", UINT_MAX, numstr);

    return value;
}

int libreport_try_atoi_positive(const char *numstr, int *value)
{
    unsigned tmp;

    g_return_val_if_fail (NULL != numstr, -EINVAL);

    int r = libreport_try_atou(numstr, &tmp);
    if (r != 0)
        return r;

    if (tmp > (unsigned)INT_MAX)
        return -ERANGE;

    *value = (int)tmp;
    return 0;
}

int libreport_try_atoi(const char *numstr, int *value)
{
    g_return_val_if_fail (NULL != numstr, -EINVAL);

    if (*numstr != '-')
        return libreport_try_atoi_positive(numstr, value);

    unsigned tmp;
    int r = libreport_try_atou(numstr + 1, &tmp);
    if (r < 0)
        return r;

    if (tmp > (unsigned)INT_MAX + 1)
        return -ERANGE;

    *value = - (int)tmp;
    return 0;
}
