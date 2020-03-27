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

#define LIBREPORT_ISO_DATE_STRING_FORMAT "%Y-%m-%d-%H:%M:%S"

char *libreport_iso_date_string(const time_t *pt)
{
    static char buf[sizeof(LIBREPORT_ISO_DATE_STRING_SAMPLE) + 4];

    time_t t;
    struct tm *ptm = localtime(pt ? pt : (time(&t), &t));

    /* Callers expect that %Y is four digits, and size buffers accordingly.
     * For paranoid reasons, disallow insane years which can overflow
     * string buffers.
     */
    if (ptm->tm_year+1900 < 0 || ptm->tm_year+1900 > 9999)
        error_msg_and_die("Year=%d?? Aborting", ptm->tm_year+1900);

    strftime(buf, sizeof(buf), LIBREPORT_ISO_DATE_STRING_FORMAT, ptm);

    return buf;
}

int libreport_iso_date_string_parse(const char *date, time_t *pt)
{
    struct tm local;
    const char *r = strptime(date, LIBREPORT_ISO_DATE_STRING_FORMAT, &local);

    if (r == NULL)
    {
        log_warning(_("String doesn't seem to be a date: '%s'"), date);
        return -EINVAL;
    }
    if (*r != '\0')
    {
        log_warning(_("The date: '%s' has unrecognized suffix: '%s'"), date, r);
        return -EINVAL;
    }
    if (local.tm_year < 70)
    {
        log_warning(_("The date: '%s' is out of UNIX time stamp range"), date);
        return -EINVAL;
    }

    // daylight saving time not in use
    local.tm_isdst = 0;

    *pt = mktime(&local);
    return 0;
}
