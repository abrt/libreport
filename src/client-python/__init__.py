# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

from _reportclient import *

import shutil
import sys
import os

tmpdir = None

# everything was ok
RETURN_OK = 0
# serious problem, should be logged somewhere
RETURN_FAILURE = 2
# user canceled processing
from report import EXIT_CANCEL_BY_USER as RETURN_CANCEL_BY_USER
# event canceled processing
from report import EXIT_STOP_EVENT_RUN as RETURN_STOP_EVENT_RUN


GETTEXT_PROGNAME = "abrt"
import locale
import gettext

_ = lambda x: gettext.lgettext(x)

def init_gettext():
    try:
        locale.setlocale(locale.LC_ALL, "")
    except locale.Error:
        os.environ['LC_ALL'] = 'C'
        locale.setlocale(locale.LC_ALL, "")
    # Defeat "AttributeError: 'module' object has no attribute 'nl_langinfo'"
    try:
        gettext.bind_textdomain_codeset(GETTEXT_PROGNAME, locale.nl_langinfo(locale.CODESET))
    except AttributeError:
        pass
    gettext.bindtextdomain(GETTEXT_PROGNAME, '/usr/share/locale')
    gettext.textdomain(GETTEXT_PROGNAME)

init_gettext()

verbose = 0

def set_verbosity(verbosity):
    global verbose
    verbose = verbosity

def log(fmt, *args):
    print verbose
    sys.stderr.write("%s\n" % (fmt % args))

def log1(fmt, *args):
    """ prints log message if verbosity >= 1 """
    if verbose >= 1:
        sys.stderr.write("%s\n" % (fmt % args))

def log2(fmt, *args):
    """ prints log message if verbosity >= 2 """
    if verbose >= 2:
        sys.stderr.write("%s\n" % (fmt % args))

def error_msg(fmt, *args):
    sys.stderr.write("%s\n" % (fmt % args))

def error_msg_and_die(fmt, *args):
    sys.stderr.write("%s\n" % (fmt % args))
    sys.exit(1)

