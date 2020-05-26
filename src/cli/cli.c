/*
    Copyright (C) 2009, 2010  Red Hat, Inc.

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
#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif
#include <getopt.h>
#include <syslog.h>
#include "internal_libreport.h"
#include "cli-report.h"

static char *steal_directory_if_needed(char *dump_dir_name)
{
    struct dump_dir *dd = libreport_open_directory_for_writing(dump_dir_name,
                                                     /* ask callback */ NULL);

    if (dd)
    {
        dump_dir_name = g_strdup(dd->dd_dirname);
        dd_close(dd);
    }

    return dump_dir_name;
}

int main(int argc, char** argv)
{
    bool runaway_arguments;
    bool missing_positional_argument;

    abrt_init(argv);

    setlocale(LC_ALL, "");
    /* Hack:
     * Right-to-left scripts don't work properly in many terminals.
     * Hebrew speaking people say he_IL.utf8 looks so mangled
     * they prefer en_US.utf8 instead.
     */
    const char *msg_locale = setlocale(LC_MESSAGES, NULL);
    if (msg_locale && strcmp(msg_locale, "he_IL.utf8") == 0)
        setlocale(LC_MESSAGES, "en_US.utf8");
#if ENABLE_NLS
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
#endif

    GList *event_list = NULL;
    const char *pfx = "";

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
            "& [-vsp] -L[PREFIX] [PROBLEM_DIR]"
        "\n""   or: & [-vspy] -e EVENT PROBLEM_DIR"
        "\n""   or: & [-vspy] -d PROBLEM_DIR"
        "\n""   or: & [-vspy] -x PROBLEM_DIR"
    );
    enum {
        OPT_list_events  = 1 << 0,
        OPT_run_event    = 1 << 1,
        OPT_delete       = 1 << 2,
        OPT_expert       = 1 << 3,
        OPT_version      = 1 << 4,
        OPT_y            = 1 << 5,
        OPT_v            = 1 << 6,
        OPT_s            = 1 << 7,
        OPT_p            = 1 << 8,
        /* An virtual option used when no other operation is specified */
        OPT_workflow     = 1 << 9,
        OPTMASK_op       = OPT_list_events|OPT_run_event|OPT_delete|OPT_expert|OPT_version,
        OPTMASK_need_arg = OPT_run_event|OPT_delete|OPT_expert|OPT_workflow
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        /*      short_name long_name  value    parameter_name  help */
        OPT_OPTSTRING('L', NULL     , &pfx, "PREFIX",          _("List possible events [which start with PREFIX]")),
        OPT_LIST(     'e', "event"  , &event_list, "EVENT",    _("Run only these events")),
        OPT_BOOL(     'd', "delete" , NULL,                    _("Remove PROBLEM_DIR after reporting")),
        OPT_BOOL(     'x', "expert" , NULL,                    _("Expert mode")),
        OPT_BOOL(     'V', "version", NULL,                    _("Display version and exit")),
        OPT_BOOL(     'y', "always" , NULL,                    _("Noninteractive: don't ask questions, assume 'yes'")),
        OPT__VERBOSE(&libreport_g_verbose),
        OPT_BOOL(     's', NULL     , NULL,                    _("Log to syslog")),
        OPT_BOOL(     'p', NULL     , NULL,                    _("Add program names to log")),
        OPT_END()
    };
    unsigned opts = libreport_parse_opts(argc, argv, program_options, program_usage_string);
    unsigned op = (opts & OPTMASK_op);
    if (!op)
    {
        opts |= OPT_workflow;
        op = OPT_workflow;
    }
    else if ((op-1) & op)
        /* "You must specify exactly one operation" */
        libreport_show_usage_and_die(program_usage_string, program_options);

    runaway_arguments = (argc - optind) > 1;
    missing_positional_argument = (opts & OPTMASK_need_arg) && (argc == optind);

    /* Check for bad usage */
    if (runaway_arguments || (missing_positional_argument && op != OPT_list_events))
    {
        libreport_show_usage_and_die(program_usage_string, program_options);
    }

    argv += optind;
    argc -= optind;

    if (op == OPT_version)
    {
        printf("%s "VERSION"\n", libreport_g_progname);
        return 0;
    }

    libreport_export_abrt_envvars(opts & OPT_p);
    if (opts & OPT_s)
    {
        openlog(libreport_msg_prefix, 0, LOG_DAEMON);
        libreport_logmode = LOGMODE_SYSLOG;
    }

    char *dump_dir_name = argv[0];

    /* Get settings */
    load_event_config_data();

    /* At least, needed by ASK_YES_NO_YESFOREVER event command requests.
     * Removing of the following statement will get the yes forever stuff not
     * working. */
    libreport_load_user_settings("report-cli");

    /* Do the selected operation. */
    int exitcode = 0;
    switch (op)
    {
        case OPT_list_events: /* -L[PREFIX] */
        {
            /* Note that dump_dir_name may be NULL here, it means
             * "show all possible events regardless of dir"
             */
            char *events = list_possible_events(NULL, dump_dir_name, pfx);
            if (!events)
                return 1; /* error msg is already logged */
            fputs(events, stdout);
            free(events);
            break;
        }
        case OPT_run_event: /* -e EVENT: run event */
        {
            dump_dir_name = steal_directory_if_needed(dump_dir_name);
            exitcode = run_event_chain(dump_dir_name, event_list, !(opts & OPT_y));
            break;
        }
        case OPT_delete:
        {
            dump_dir_name = steal_directory_if_needed(dump_dir_name);
            exitcode = delete_dump_dir_possibly_using_abrtd(dump_dir_name);
            break;
        }
        case OPT_expert:
        {
            dump_dir_name = steal_directory_if_needed(dump_dir_name);
            exitcode = select_and_run_one_event(dump_dir_name, pfx, !(opts & OPT_y));
            break;
        }
        case OPT_workflow:
        {
            /* list the workflows which can be applied to the current problem */
            GList *possible_names = list_possible_events_glist(dump_dir_name, "workflow");
            GHashTable *possible_workflows = load_workflow_config_data_from_list(
                            possible_names, WORKFLOWS_DIR);
            g_list_free_full(possible_names, free);

            /* this may steal the directory even if no workflow is run */
            dump_dir_name = steal_directory_if_needed(dump_dir_name);
            exitcode = select_and_run_workflow(dump_dir_name, possible_workflows, !(opts & OPT_y));
            break;
        }
    }

    /* At least, needed by ASK_YES_NO_YESFOREVER event command requests. */
    libreport_save_user_settings();
    return exitcode;
}
