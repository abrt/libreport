#!/usr/bin/python3

#    Copyright (C) 2022  ABRT team
#    Copyright (C) 2022  Red Hat Inc
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

import configparser
import getopt
import gettext
import locale
import logging
import os
import re
import stat
import sys
from getpass import getpass
from logging import getLogger
from typing import Any, Dict, List

import reportclient.internal.const as const
from reportclient.internal.bz_connection import BZConnection
from reportclient.internal.configuration_files import ConfFileLoader
from reportclient.internal.dump_dir import DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE, DD_FAIL_QUIETLY_ENOENT, DumpDir
from reportclient.internal.global_configuration import GlobalConfFileLoader
from reportclient.internal.problem_data import ProblemDataLoader
from reportclient.internal.problem_formatter import ProblemFormatter
from reportclient.internal.problem_utils import os_info_get_value, pd_get_item, pd_get_item_content
from reportclient.internal.report_result import report_result_new_with_label_from_env
from reportclient.internal.utils import string_to_bool

DEFAULT_BUGZILLA_PRODUCT = 'Fedora'
G_MAXUINT64 = 18446744073709551615

_ = gettext.gettext

logger = getLogger(__name__)


def logger_init(logger, level):
    handler = logging.StreamHandler(stream=sys.stderr)
    handler.setLevel(level)
    logger.addHandler(handler)
    logger.setLevel(level)


def init_gettext():
    try:
        locale.setlocale(locale.LC_ALL, '')
    except locale.Error:
        os.environ['LC_ALL'] = 'C'
        locale.setlocale(locale.LC_ALL, '')
    gettext.bindtextdomain(const.GETTEXT_PROGNAME, '/usr/share/locale')
    gettext.textdomain(const.GETTEXT_PROGNAME)


def set_default_settings(os_info: List[str], settings: Dict):
    # if BugzillaURL is defined in conf_file or env, it will replace this value
    settings['BugzillaURL'] = os_info_get_value('BUG_REPORT_URL', os_info)
    logger.debug("Loaded BUG_REPORT_URL '%s' from os-release", settings['BugzillaURL'])

    # if Product or ProductVersion is defined in conf_file or env, it will replace this value
    settings['Product'] = os_info_get_value('REDHAT_BUGZILLA_PRODUCT', os_info)
    settings['ProductVersion'] = os_info_get_value('REDHAT_BUGZILLA_PRODUCT_VERSION', os_info)
    logger.debug("Loaded Product '%s' from os-release", settings['Product'])
    logger.debug("Loaded ProductVersion '%s' from os-release", settings['ProductVersion'])


def set_settings(bz: Dict, settings: Dict):

    environ = os.environ.get('Bugzilla_APIKey')
    if not environ:
        environ = settings.get('APIKey')
    bz['b_api_key'] = environ or ''

    environ = os.environ.get("Bugzilla_BugzillaURL")
    if not environ:
        environ = settings.get('BugzillaURL')
    bz['b_bugzilla_url'] = environ or 'https://bugzilla.redhat.com'
    bz['b_bugzilla_url'] = bz['b_bugzilla_url'].rstrip('/')
    bz['b_bugzilla_xmlrpc'] = os.path.join(bz['b_bugzilla_url'], "xmlrpc.cgi")
    bz['b_bugzilla_rest'] = os.path.join(bz['b_bugzilla_url'], "rest.cgi")

    environ = os.environ.get("Bugzilla_Product")
    if environ:
        bz['b_product'] = environ
        environ = os.environ.get("Bugzilla_ProductVersion")
        if environ:
            bz['b_product_version'] = environ
    else:
        option = settings.get('Product')
        if option:
            bz['b_product'] = option
        option = settings.get('ProductVersion')
        if option:
            bz['b_product_version'] = option

    environ = os.environ.get("Bugzilla_SSLVerify")
    if not environ:
        environ = settings.get("SSLVerify")
    bz['b_ssl_verify'] = string_to_bool(environ)

    environ = os.environ.get('Bugzilla_DontMatchComponents')
    if not environ:
        environ = settings.get('DontMatchComponents')
    bz['b_DontMatchComponents'] = environ or ''

    environ = os.environ.get('ABRT_CREATE_PRIVATE_TICKET')
    if not environ:
        environ = os.environ.get("Bugzilla_CreatePrivate")
        if not environ:
            environ = settings.get('Bugzilla_CreatePrivate')
            if not environ:
                bz['b_create_private'] = False
            else:
                bz['b_create_private'] = string_to_bool(environ)

    if bz.get('b_create_private'):
        logger.info('create private bz ticket: YES')
    else:
        logger.info('create private bz ticket: NO')

    environ = os.environ.get('Bugzilla_PrivateGroups')
    if not environ:
        environ = settings.get('Bugzilla_PrivateGroups')
    if environ:
        groups = [i.strip() for i in environ.split(',')]
    else:
        groups = []
    if not bz.get('b_private_groups'):
        bz['b_private_groups'] = groups
        logger.info("groups: '%s'", bz['b_private_groups'])
    elif groups:
        logger.warning(_("Warning, private ticket groups already specified as cmdline argument, ignoring the env variable and configuration"))


def abrt_init():
    if not global_config.libreport_load_global_configuration():
        logger.error("Cannot continue without libreport global configuration.")
        sys.exit(1)

    env_verbose = os.environ.get('ABRT_VERBOSE')
    if env_verbose:
        verbose = int(env_verbose)
    else:
        verbose = 0

    progname = os.path.basename(sys.argv[0])

    pfx = os.environ.get('ABRT_PROG_PREFIX')
    if pfx:
        msg_prefix = g_progname

    return (progname, verbose)


def export_abrt_envvars(pfx=0):
    os.environ['ABRT_VERBOSE'] = str(g_verbose)
    if pfx:
        os.environ['ABRT_PROG_PREFIX'] = 1
        msg_prefix = g_progname


def find_in_reported_to(problem_dir: str, reportee: str):
    try:
        with open(f"{problem_dir}/reported_to", 'r', encoding='utf-8') as handle:
            lines = handle.read().split('\n')
    except (FileNotFoundError, PermissionError):
        pass
    for line in lines:
        key, value = line.split(': ', 1)
        if key == reportee:
            return value
    return None


def ask_yes_no(question):
    yes = _("y")
    no = _("N")
    is_slave_mode = bool(os.environ.get('REPORT_CLIENT_SLAVE') is not None)
    is_noninteractive_mode = bool(os.environ.get("REPORT_CLIENT_NONINTERACTIVE") is not None)

    env_response = os.environ.get('REPORT_CLIENT_RESPONSE')
    if env_response and env_response.lower() == yes:
        return True

    if is_slave_mode:
        print(f'ASK_YES_NO {question}')
    else:
        print(f'{question} [{yes}/{no}] ', end='')

    sys.stdout.flush()

    if not is_slave_mode and is_noninteractive_mode:
        print()
        sys.stdout.flush()
        return False

    try:
        answer = input()
    except (EOFError, KeyboardInterrupt):
        sys.exit(1)

    if not answer:
        return False

    return (is_slave_mode and answer[0] == 'y') or answer.lower() == yes


def ask(question):
    is_slave_mode = bool(os.environ.get('REPORT_CLIENT_SLAVE') is not None)
    is_noninteractive_mode = bool(os.environ.get("REPORT_CLIENT_NONINTERACTIVE") is not None)

    if is_slave_mode:
        print(f'ASK {question}')
    else:
        print(f'{question} ', end='')

    sys.stdout.flush()

    if not is_slave_mode and is_noninteractive_mode:
        print()
        sys.stdout.flush()
        return ''

    try:
        answer = input()
    except (EOFError, KeyboardInterrupt):
        sys.exit(1)

    return answer


def log_out(bug_info, rhbz, dump_dir_name):
    if bug_info['bi_resolution']:
        resolution = f" {bug_info['bi_resolution']}"
    else:
        resolution = ''
    logger.warning(_("Status: %s%s %s/show_bug.cgi?id=%u"),
                   bug_info['bi_status'],
                   resolution,
                   rhbz['b_bugzilla_url'],
                   bug_info['bi_id'])

    dump_dir_obj = DumpDir(logger)
    dd = dump_dir_obj.dd_opendir(dump_dir_name, 0)
    if dd:
        result = report_result_new_with_label_from_env("Bugzilla")

        result['url'] = f"{rhbz['b_bugzilla_url']}/show_bug.cgi?id={bug_info['bi_id']}"

        dump_dir_obj.libreport_add_reported_to_entry(dd, result)

        dump_dir_obj.dd_close(dd)

    sys.exit(0)


if __name__ == '__main__':

    global_config = GlobalConfFileLoader(logger)
    g_progname, g_verbose = abrt_init()

    # localization
    init_gettext()

    config = configparser.ConfigParser()

    program_usage_string = _(
        "Usage:"
        "\n{0} [-vbf] [-g GROUP-NAME]... [-c CONFFILE]... [-F FMTFILE] [-A FMTFILE2] -d DIR"
        "\nor:"
        "\n{0} [-v] [-c CONFFILE]... [-d DIR] -t[ID] FILE..."
        "\nor:"
        "\n{0} [-v] [-c CONFFILE]... [-d DIR] -t[ID] -w"
        "\nor:"
        "\n{0} [-v] [-c CONFFILE]... -h DUPHASH [-p[PRODUCT]]"
        "\n"
        "\nReports problem to Bugzilla."
        "\n"
        "\nThe tool reads DIR. Then it logs in to Bugzilla and tries to find a bug"
        "\nwith the same abrt_hash:HEXSTRING in 'Whiteboard'."
        "\n"
        "\nIf such bug is not found, then a new bug is created. Elements of DIR"
        "\nare stored in the bug as part of bug description or as attachments,"
        "\ndepending on their type and size."
        "\n"
        "\nOtherwise, if such bug is found and it is marked as CLOSED DUPLICATE,"
        "\nthe tool follows the chain of duplicates until it finds a non-DUPLICATE bug."
        "\nThe tool adds a new comment to found bug."
        "\n"
        "\nThe URL to new or modified bug is printed to stdout and recorded in"
        "\n'reported_to' element."
        "\n"
        "\nOption -t uploads FILEs to the already created bug on Bugzilla site."
        "\nThe bug ID is retrieved from directory specified by -d DIR."
        "\nIf problem data in DIR was never reported to Bugzilla, upload will fail."
        "\n"
        "\nOption -tID uploads FILEs to the bug with specified ID on Bugzilla site."
        "\n-d DIR is ignored."
        "\n"
        "\nOption -w adds bugzilla user to bug's CC list."
        "\n"
        "\nOption -r sets the last url from reporter_to element which is prefixed with"
        "\nTRACKER_NAME to URL field. This option is applied only when a new bug is to be"
        "\nfiled. The default value is 'ABRT Server'"
        "\n"
        "\nIf not specified, CONFFILE defaults to {1}/plugins/bugzilla.conf"
        "\nand user's local ~{2}/bugzilla.conf."
        "\nIts lines should have 'PARAM = VALUE' format."
        "\nRecognized string parameters: BugzillaURL, BugzillaAPIKey, OSRelease."
        "\nRecognized boolean parameter (VALUE should be 1/0, yes/no): SSLVerify."
        "\nUser's local configuration overrides the system wide configuration."
        "\nParameters can be overridden via $Bugzilla_PARAM environment variables."
        "\n"
        "\nFMTFILE and FMTFILE2 default to {1}/plugins/bugzilla_format.conf"
    ).format(g_progname, const.CONF_DIR, const.USER_HOME_CONFIG_PATH)

    program_options = _(
        "\n        --help                  Print this help and exit"
        "\n    -v, --verbose               Be verbose"
        "\n    -d DIR                      Problem directory"
        "\n    -c FILE                     Configuration file (may be given many times)"
        "\n    -F FILE                     Formatting file for initial comment"
        "\n    -A FILE                     Formatting file for duplicates"
        "\n    -t, --ticket[ID]            Attach FILEs [to bug with this ID]"
        "\n    -b                          When creating bug, attach binary files too"
        "\n    -f                          Force reporting even if this problem is already reported"
        "\n    -w                          Add bugzilla user to CC list [of bug with this ID]"
        "\n    -h, --duphash DUPHASH       Print BUG_ID which has given DUPHASH"
        "\n    -p, --product[PRODUCT]      Specify a Bugzilla product (ignored without -h)"
        "\n    -r, --tracker TRACKER_NAME  A name of bug tracker for an additional URL from 'reported_to'"
        "\n    -g, --group GROUP           Restrict access to this group only"
        "\n    -D, --debug[STR]            Debug\n"
    )

    dump_dir_name = os.getcwd()
    conf_files = []
    fmt_file = os.path.join(const.CONF_DIR, 'plugins/bugzilla_format.conf')
    fmt_file2 = fmt_file
    ticket_no = None
    abrt_hash = None
    product = None
    version = None
    tracker_str = 'ABRT Server'
    debug = False
    add_user_to_cc = False
    rhbz: Dict[str, Any] = {}
    attach_binaries = False

    try:
        opts, args = getopt.getopt(sys.argv[1:],
                                   'vd:c:F:A:t:bfwh:pr:g:D',
                                   ['verbose', 'help', 'ticket=', 'duphash=',
                                    'product', 'tracker=', 'group=', 'debug'])
    except getopt.GetoptError as exc:
        logger.error(exc)
        print(program_usage_string)
        print(program_options)
        sys.exit(1)

    for opt, arg in opts:
        if opt == '--help':
            print(program_usage_string)
            print(program_options)
            sys.exit(0)
        if opt in ('-v', '--verbose'):
            g_verbose += 1
        elif opt == '-d':
            dump_dir_name = arg
        elif opt == '-c':
            conf_files.append(arg)
        elif opt == '-F':
            fmt_file = arg
        elif opt == '-A':
            fmt_file2 = arg
        elif opt in ('-t', '--ticket'):
            ticket_no = arg
        elif opt == '-b':
            attach_binaries = True
        elif opt == '-w':
            add_user_to_cc = True
        elif opt in ('-h', '--duphash'):
            abrt_hash = arg
        elif opt in ('-p', '--product'):
            product = arg
            if product:
                continue
            product = os.environ.get('Bugzilla_Product')
            if product:
                continue
        elif opt in ('-r', '--tracker'):
            tracker_str = arg
        elif opt in ('-g', '--group'):
            rhbz['b_private_group'] = arg
        elif opt in ('-D', '--debug'):
            debug = True

    g_verbose = min(g_verbose, 2)
    logger_init(logger, ['WARNING', 'INFO', 'DEBUG'][g_verbose])

    export_abrt_envvars(0)

    s_reporter_settings: Dict[str, Any] = {}

    opt_switches = set(k for k, _ in opts)

    if '-d' in opt_switches:
        # pull in some defaults from os-release
        problem_data_loader = ProblemDataLoader(logger, global_config)
        problem_data = problem_data_loader.create_problem_data_for_reporting(dump_dir_name)
        if not problem_data:
            sys.exit(1)  # create_problem_data_for_reporting already emitted error msg
        os_info = pd_get_item_content(const.FILENAME_OS_INFO, problem_data).split('\n')
        set_default_settings(os_info, s_reporter_settings)

    # {

    conf_file_loader = ConfFileLoader(logger=logger)
    if not conf_files:
        conf_files.append(os.path.join(const.CONF_DIR, 'plugins/bugzilla.conf'))
        conf_files.append(os.path.join(os.environ['HOME'], const.USER_HOME_CONFIG_PATH, 'bugzilla.conf'))
    for file in conf_files:
        logger.info("Loading settings from '%s'", file)
        conf_file_loader.libreport_load_conf_file(file, settings=s_reporter_settings, skip_empty_keys=True)
        logger.debug("Loaded '%s'", file)

    set_settings(rhbz, s_reporter_settings)

    # }

    # either we got Bugzilla_CreatePrivate from settings or -g was specified on cmdline
    if opt_switches & {'-g', '--group'}:
        rhbz['b_create_private'] = True

    bz_url = s_reporter_settings.get('BugzillaURL') or 'https://bugzilla.redhat.com'

    ssl_verify = bool('SSLVerify' not in s_reporter_settings
                      or string_to_bool(str(s_reporter_settings['SSLVerify'])))

    bz_conn = BZConnection(logger, global_config, url=bz_url,
                           verify=ssl_verify)

    if abrt_hash:
        logger.warning(_("Looking for similar problems in bugzilla"))
        if not abrt_hash.startswith('abrt_hash:'):
            abrt_hash = f'abrt_hash:{abrt_hash}'

        if opt_switches & {'-p', '--product'}:
            # If only -p without following string is presented, using
            # 'REDHAT_BUGZILLA_PRODUCT' value from /etc/os-release or value
            # from environment variable 'Bugzilla_Product' is used.
            if not product and not os.environ.get('Bugzilla_Product'):
                try:
                    with open('/etc/os-release', 'r', encoding='utf-8') as os_release:
                        # Add a dummy section to allow parsing as ini file
                        os_release_content = '[os_release]\n' + os_release.read()
                        config.read_string(os_release_content)
                        product = config['os_release']['REDHAT_BUGZILLA_PRODUCT'].strip('"')
                        if not product:
                            logger.error(_("Failed to get 'REDHAT_BUGZILLA_PRODUCT' "
                                           "from '/etc/os-release'."))
                except (FileNotFoundError, PermissionError):
                    logger.error(_("Failed to read '/etc/os-release' to get Bugzilla product."))

        if not product:
            # Use DEFAULT_BUGZILLA_PRODUCT as default product due to backward compatibility
            product = DEFAULT_BUGZILLA_PRODUCT

            # If parameter -p was used and product == NULL, some error occured
            if opt_switches & {'-p', '--product'}:
                logger.error(_("Using default product '%s'"), product)

        logger.debug("Using Bugzilla product '%s' to find duplicate bug", product)
        bugs = bz_conn.bug_search(
            {'quicksearch': f'ALL whiteboard:"{abrt_hash}"'}
        )

        if bugs:
            print(bugs[0]['id'])

        sys.exit(0)

    if ('APIKey' not in s_reporter_settings or s_reporter_settings['APIKey'] == ''):
        try:
            s_reporter_settings['APIKey'] = getpass(_("API key is not provided by configuration. Please enter the API key for '{}': ").format(bz_url))
        except (EOFError, KeyboardInterrupt):
            s_reporter_settings['APIKey'] = None
        if not s_reporter_settings['APIKey']:
            logger.error(_("Can't continue without API key"))
            sys.exit(1)
    bz_conn.add_api_key(s_reporter_settings['APIKey'])

    if opt_switches & {'-t', '--ticket'}:
        if (
            (not args and '-w' not in opt_switches)
            or (args and '-w' in opt_switches)
        ):
            print(program_usage_string)
            print(program_options)
            sys.exit(1)

        if not ticket_no:
            dump_dir_obj = DumpDir(logger)
            dd = dump_dir_obj.dd_opendir(dump_dir_name, 0)
            reported_to = None
            url = ''

            if not dd:
                # libreport_xfunc_die()
                sys.exit(1)

            reported_to = dump_dir_obj.libreport_find_in_reported_to(dd, 'Bugzilla')

            dump_dir_obj.dd_close(dd)

            if not reported_to:
                logger.error(_("Can't get Bugzilla ID because this problem has not yet been reported to Bugzilla."))
                sys.exit(1)

            url = reported_to.get('url')

            if not url.startswith(rhbz['b_bugzilla_url']):
                logger.error(_("This problem has been reported to Bugzilla '%s' which differs from the configured Bugzilla '%s'."), url, rhbz['b_bugzilla_url'])

            if url.rfind('=') == -1:
                logger.error(_("Malformed url to Bugzilla '%s'."), url)

            ticket_no = url[url.rfind('=')+1:]
            logger.warning(_("Using Bugzilla ID '%s'"), ticket_no)

        if not rhbz['b_bugzilla_url'].endswith('redhat.com'):
            # Add API key as a REST param, but only for non-RH bugzilla instances
            bz_conn.add_api_key_param(rhbz['b_api_key'])

        if '-w' in opt_switches:
            ticket_intermediate = re.search(r'^\d+', ticket_no)
            if ticket_intermediate:
                ticket = ticket_intermediate.group(0)
            else:
                logger.error("expected a non-negative integer: '%s'", ticket_no)
                sys.exit(1)
            rhbz['b_login'] = ask(_(f"Please enter your {rhbz['b_bugzilla_url']} login:"))
            if not rhbz['b_login']:
                logger.error(_("Can't continue without login"))
                sys.exit(1)
            update_data = {'ids': [ticket], 'cc': {'add': [rhbz['b_login']]}, 'minor_update': True}
            bz_conn.bug_update(ticket, update_data)
        else:  # Attach files to existing BZ
            for filename in args:  # anything that isn't a cmdline option
                logger.info("Attaching file '%s' to bug %s", filename, ticket_no)

                try:
                    st = os.stat(filename)
                except OSError:
                    logger.error("Can't open '%s'", filename)
                    continue
                if not stat.S_ISREG(st.st_mode):
                    logger.error("'%s': not a regular file", filename)
                    continue

                response = bz_conn.attachment_create(int(ticket_no), filename, 0)
                # TODO: We can't print attachment id because
                # POST rest/bug/<id>/attachment returns empty response - bug?
                logger.info("Attached '%s' to bug #%s", filename, ticket_no)
        sys.exit(0)

    # Create new bug in Bugzilla

    if '-f' not in opt_switches:
        dump_dir_obj = DumpDir(logger)
        dd = dump_dir_obj.dd_opendir(dump_dir_name, 0)
        reported_to = None
        url = ''

        if not dd:
            # libreport_xfunc_die();
            sys.exit(1)

        reported_to = dump_dir_obj.libreport_find_in_reported_to(dd, "Bugzilla")

        dump_dir_obj.dd_close(dd)

        if reported_to:
            url = reported_to.get('url')
        if url:
            msg = (_(f"This problem was already reported to Bugzilla (see '{url}')."
                     f" Do you still want to create a new bug?"))

            if not ask_yes_no(msg):
                sys.exit(0)

    if '-d' not in opt_switches:
        problem_data_loader = ProblemDataLoader(logger, global_config)
        problem_data = problem_data_loader.create_problem_data_for_reporting(dump_dir_name)

    if not problem_data:
        sys.exit(1)  # create_problem_data_for_reporting already emitted error msg

    component = pd_get_item_content(const.FILENAME_COMPONENT, problem_data)
    if not component:
        sys.exit(1)
    duphash = pd_get_item_content(const.FILENAME_DUPHASH, problem_data)

    if not rhbz.get('b_product') or not rhbz.get('b_product_version'):  # if not overridden or empty...
        os_info = pd_get_item_content(const.FILENAME_OS_INFO, problem_data).split('\n')
        rhbz['b_product'] = os_info_get_value('REDHAT_BUGZILLA_PRODUCT', os_info)
        rhbz['b_product_version'] = os_info_get_value('REDHAT_BUGZILLA_PRODUCT_VERSION', os_info)

        if not rhbz['b_product'] or not rhbz['b_product_version']:
            logger.error(_("Can't determine Bugzilla Product from problem data."))

    if opt_switches & {'-D', '--debug'}:
        pf = ProblemFormatter(fmt_file, problem_data, logger)
        # errors are handled by ProblemFormatter

        report = pf.generate_report()
        if not report:  # won't happen. TODO: figure out when report generation should fail
            logger.error('Failed to format bug report from problem data')

        print(f"summary: {report['summary']}\n\n{report['text']}", end='')

        print('attachments:')
        for attachment in report['attach']:
            pd_item = pd_get_item(attachment, problem_data)
            print(f" {pd_item['name']}")

        sys.exit(0)

    if not rhbz['b_bugzilla_url'].endswith('redhat.com'):
        # Add API key as a REST param, but only for non-RH bugzilla instances
        bz_conn.add_api_key_param(rhbz['b_api_key'])

    bug_id = 0

    # If REMOTE_RESULT contains "DUPLICATE 12345", we consider it a dup of 12345
    # and won't search on bz server.
    remote_result = pd_get_item_content(const.FILENAME_REMOTE_RESULT, problem_data)
    if remote_result:
        matches = re.findall(r'DUPLICATE \d+', remote_result)
        if matches:
            bug_id = matches[0][matches[0].rfind(' ')+1:]

    bug_info = None
    if not bug_id:
        logger.warning(_('Checking for duplicates'))

        existing_id = -1
        crossver_id = -1

        # {

        # Figure out whether we want to match component
        # when doing dup search.
        dont_match_components_str = str(s_reporter_settings.get('DontMatchComponents'))
        if dont_match_components_str:
            dont_match_components = [i.strip() for i in dont_match_components_str.split(',')]
        else:
            dont_match_components = []

        if component in dont_match_components:
            component_substitute = None
        else:
            component_substitute = component

        # We don't do dup detection across versions (see below why),
        # but we do add a note if cross-version potential dup exists.
        # For that, we search for cross version dups first:

        query = {'quicksearch': f"ALL whiteboard:{duphash} product:{rhbz['b_product']} component:{component_substitute}"}

        crossver_bugs = bz_conn.bug_search(query)
        if crossver_bugs:
            # In dup detection we require match in product *and version*.
            # Otherwise we sometimes have bugs in e.g. Fedora 17
            # considered to be dups of Fedora 16 bugs.
            # Imagine that F16 is "end-of-lifed" - allowing cross-version
            # match will make all newly detected crashes DUPed
            # to a bug in a dead release.
            crossver_id = crossver_bugs[0]['id']
            logger.debug("Bugzilla has %i reports with duphash '%s' including cross-version ones",
                         len(crossver_bugs), duphash)

            query = {'quicksearch': f"ALL whiteboard:{duphash} product:{rhbz['b_product']} version:{rhbz['b_product_version']} component:{component_substitute}"}
            dup_bugs = bz_conn.bug_search(query)
            if dup_bugs:
                existing_id = dup_bugs[0]['id']
                logger.debug("Bugzilla has %i reports with duphash '%s'",
                             len(dup_bugs), duphash)

        # }

        if existing_id < 0 or rhbz['b_create_private']:
            pf = ProblemFormatter(fmt_file, problem_data, logger)
            report = pf.generate_report()
            # TODO: if not report (how would that happen?)
            if existing_id > 0:
                msg = _(
                    'You have requested to make your data accessible only to a '
                    'specific group and this bug is a duplicate of bug: '
                    '{}/{}'
                    ' '
                    'In case of bug duplicates a new comment is added to the '
                    'original bug report but access to the comments cannot be '
                    'restricted to a specific group.'
                    ' '
                    'Would you like to open a new bug report and close it as '
                    'DUPLICATE of the original one?'
                    ' '
                    'Otherwise, the bug reporting procedure will be terminated.'
                ).format(rhbz['b_bugzilla_url'], existing_id)
                if not ask_yes_no(msg):
                    sys.exit(const.EXIT_CANCEL_BY_USER)
                msg = _(
                    "\nThis is a private, duplicate bug report of bug {}. "
                    "The report has been created because Bugzilla cannot "
                    "grant access to a comment for a specific group.\n"
                ).format(existing_id)
                report['text'] += msg

            # Create new bug
            logger.warning(_('Creating a new bug'))

            if existing_id < 0 <= crossver_id:
                report['text'] += "\nPotential duplicate: bug {crossver_id}\n"

            new_id = bz_conn.bug_create(problem_data, rhbz['b_product'], rhbz['b_product_version'],
                                        report['summary'], report['text'], rhbz['b_create_private'],
                                        rhbz['b_private_groups'])
            if new_id == -1:
                # bug_create logged an error
                sys.exit(1)

            dump_dir_obj = DumpDir(logger)
            dd = dump_dir_obj.dd_opendir(dump_dir_name, 0)
            if dd:
                extra = dump_dir_obj.dd_load_text_ext(dd, 'extra-cc',
                                                      DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE | DD_FAIL_QUIETLY_ENOENT)
                if extra:
                    emails = extra.split('\n')
                    logger.warning(_("Adding extra ccs to bug report: %s"), emails)
                    update_data = {'ids': [new_id], 'cc': {'add': emails}, 'minor_update': False}
                    bz_conn.bug_update(ticket, update_data)

                reported_to = dump_dir_obj.libreport_find_in_reported_to(dd, tracker_str)
                url = ''
                if reported_to:
                    url = reported_to.get('url')
                if url:
                    logger.warning(_('Adding External URL to bug %i'), new_id)
                    update_data = {'ids': [new_id], 'url': url, 'minor_update': True}
                    bz_conn.bug_update(new_id, update_data)

                dump_dir_obj.dd_close(dd)

            logger.warning(_('Adding attachments to bug %i'), new_id)

            for attachment in report['attach']:
                response = bz_conn.attachment_create_from_problem_data(new_id, attachment, problem_data)
                logger.info("Attached '%s' to bug no. %s with id %s",
                            attachment, new_id, response.json()['ids'][0])

            bug_info = {'bi_id': new_id,
                        'bi_status': 'NEW',
                        'bi_dup_id': -1}

            if existing_id >= 0:
                logger.warning(_("Closing bug %i as duplicate of bug %i"), new_id, existing_id)
                update_data = {'ids': [new_id],
                               'status': 'CLOSED',
                               'resolution': 'DUPLICATE',
                               'dupe_of': existing_id,
                               'minor_update': True}
                bz_conn.bug_update(new_id, update_data)

            log_out(bug_info, rhbz, dump_dir_name)

        bug_id = existing_id

    bug_info = bz_conn.bug_info(bug_id)

    logger.warning(_("Bug is already reported: %i"), bug_info['bi_id'])

    # Follow duplicates
    if bug_info['bi_status'] == 'CLOSED' and bug_info['bi_resolution'] == 'DUPLICATE':
        origin = bz_conn.find_origin_bug_closed_duplicate(bug_info)
        if origin:
            bug_info = origin

    # Original author's note:
    # We used to skip adding the comment to CLOSED bugs:
    #
    # if (strcmp(bug_info['bi_status'], "CLOSED") != 0)
    # {
    #
    # But that condition has been added without a good explanation of the
    # reason for doing so:
    #
    # ABRT commit 1bf37ad93e87f065347fdb7224578d55cca8d384
    #
    # -    if (bug_id > 0)
    # +    if (strcmp(bz.bug_status, "CLOSED") != 0)
    #
    #
    # From my point of view, there is no good reason to not add the comment to
    # such a bug. The reporter spent several minutes waiting for the backtrace
    # and we don't want to make the reporters feel that they spent their time
    # in vain and I think that adding comments to already closed bugs doesn't
    # hurt the maintainers (at least not me).
    #
    # Plenty of new comments might convince the maintainer to reconsider the
    # bug's status.

    # Add user's login to CC if not there already, but only if the login is known
    if (
        rhbz.get('b_login')
        and bug_info['bi_reporter'] != rhbz['b_login']
        and rhbz['b_login'] not in bug_info['bi_cc_list']
    ):
        logger.warning(_('Adding %s to CC list'), rhbz['b_login'])
        update_data = {'ids': [bug_info['bi_id']], 'cc': {'add': [rhbz['b_login']]}, 'minor_update': True}
        bz_conn.bug_update(bug_info['bi_id'], update_data)

    # Add comment and bt
    comment = pd_get_item_content(const.FILENAME_COMMENT, problem_data)
    if comment:
        pf = ProblemFormatter(fmt_file2, problem_data, logger)

        report = pf.generate_report()
        if not report:  # won't happen. TODO: figure out when report generation should fail
            logger.error('Failed to format duplicate comment from problem data')

        bzcomment = report.get('text')

        dup_comment = False
        trimmed_comment = re.sub(r'[ \t\n]', r'', bzcomment)
        for c in bug_info['bi_comments']:
            trimmed_c = re.sub(r'[ \t\n]', r'', c)
            if trimmed_c == trimmed_comment:
                dup_comment = True
                break

        if not dup_comment:
            logger.warning(_("Adding new comment to bug %d"), bug_info['bi_id'])
            bz_conn.bug_add_comment(int(bug_info['bi_id']), bzcomment)

            bt = pd_get_item_content(const.FILENAME_BACKTRACE, problem_data)
            rating_str = pd_get_item_content(const.FILENAME_RATING, problem_data)
            rating = 0
            # python doesn't have rating file
            if rating_str:
                rtg = re.search(r'^\d+', rating_str)
                if rtg:
                    rating = rtg.group(0)
                if rating > G_MAXUINT64:
                    logger.error("expected number in range <0, %lu>: '%s'", G_MAXUINT64, rating_str)
            if bt and rating > bug_info['bi_best_bt_rating']:
                logger.warning(_("Attaching better backtrace"))
                bz_conn.attachment_create(int(bug_info['bi_id']), const.FILENAME_BACKTRACE, const.RHBZ_MINOR_UPDATE)
        else:
            logger.warning(_('Found the same comment in the bug history, not adding a new one'))
