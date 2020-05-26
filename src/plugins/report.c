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

int main(int argc, char **argv)
{
    abrt_init(argv);

    /* I18n */
    setlocale(LC_ALL, "");
#if ENABLE_NLS
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
#endif

    const char *target = NULL;
    const char *ticket = NULL;

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "& [-v] --target TARGET --ticket ID FILE...\n"
        "\n"
        "Uploads FILEs to specified ticket on TARGET.\n"
        "\n"
        "This tool is provided to ease transition of users of report package\n"
        "to libreport. Recognized TARGETs are 'rhts' and 'bugzilla',\n"
        "first one invokes upload to RHTSupport and second - to Bugzilla.\n"
        "\n"
        "Configuration (such as login data) can be supplied via files\n"
        CONF_DIR"/plugins/bugzilla.conf and $HOME"USER_HOME_CONFIG_PATH"/bugzilla.conf and\n"
        CONF_DIR"/plugins/rhtsupport.conf and $HOME"USER_HOME_CONFIG_PATH"/rhtsupport.conf,\n"
        "or via environment variables - read documentation of\n"
        "reporter-bugzilla and reporter-rhtsupport tools."
    );
    enum {
        OPT_v = 1 << 0,
        OPT_T = 1 << 1,
        OPT_t = 1 << 2,
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&libreport_g_verbose),
        OPT_STRING('T', "target", &target, "TARGET", _("'rhts' or 'bugzilla'")),
        OPT_STRING('t', "ticket", &ticket, "ID"    , _("Ticket/case ID")),
        OPT_END()
    };
    /*unsigned opts =*/ libreport_parse_opts(argc, argv, program_options, program_usage_string);

    libreport_export_abrt_envvars(0);

    argv += optind;
    if (!*argv || !target || !ticket)
        libreport_show_usage_and_die(program_usage_string, program_options);

    const char *tool_name;
    if (strcmp(target, "rhts") == 0 || strcmp(target, "strata") == 0)
        tool_name = "reporter-rhtsupport";
    else
    if (strcmp(target, "bugzilla") == 0)
        tool_name = "reporter-bugzilla";
    else
        libreport_show_usage_and_die(program_usage_string, program_options);

    argv -= 2;
    argv[0] = (char*) tool_name;
    argv[1] = g_strdup_printf("-t%s", ticket);

    execvp(argv[0], argv);
    perror_msg_and_die("Can't execute '%s'", argv[0]);
}
