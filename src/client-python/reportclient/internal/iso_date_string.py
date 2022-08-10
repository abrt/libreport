#    Copyright (C) 2011  ABRT team
#    Copyright (C) 2011  Red Hat Inc
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License along
#    with this program; if not, write to the Free Software Foundation, Inc.,
#    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

import errno
import gettext
import time

_ = gettext.gettext

LIBREPORT_ISO_DATE_STRING_FORMAT = '%Y-%m-%d-%H:%M:%S'


class ISODateString:
    def __init__(self, logger):
        self.logger = logger

    def libreport_iso_date_string(self, pt: int):
        if pt:
            ptm = time.localtime(pt)
        else:
            ptm = time.localtime()

        # disallow insane years
        if ptm.tm_year + 1900 < 0 or ptm.tm_year + 1900 > 9999:
            self.logger.error("Year=%d?? Aborting", ptm.tm_year + 1900)

        return time.strftime(LIBREPORT_ISO_DATE_STRING_FORMAT, ptm)

    def libreport_iso_date_string_parse(self, date: str):
        pt = -1
        try:
            local = time.strptime(date, LIBREPORT_ISO_DATE_STRING_FORMAT)
        except ValueError:
            self.logger.warning(_("String doesn't seem to be a date: '%s'"), date)
            return pt, -errno.EINVAL

        if local.tm_year < 70:
            self.logger.warning(_("The date: '%s' is out of UNIX time stamp range"), date)
            return pt, -errno.EINVAL

        pt = int(time.mktime(local))
        return pt, 0
