/*
    Copyright (C) 2009  Zdenek Prikryl (zprikryl@redhat.com)
    Copyright (C) 2009  RedHat inc.

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
#include "client.h"
#include "problem_report.h"

#define PR_DEFAULT_SUBJECT \
    "[abrt] %pkg_name%[[: %crash_function%()]][[: %reason%]][[: TAINTED %tainted_short%]]"

#define PR_MAILX_TEMPLATE \
    "%%summary:: %s\n" \
    "\n" \
    "::" \
    FILENAME_REASON","FILENAME_CRASH_FUNCTION"," \
    FILENAME_CMDLINE","FILENAME_EXECUTABLE"," \
    FILENAME_PACKAGE","FILENAME_COMPONENT","FILENAME_PID","FILENAME_PWD"," \
    FILENAME_HOSTNAME","FILENAME_COUNT", %%oneline\n" \
    "\n" \
    "::" \
    FILENAME_COMMENT","FILENAME_REPORTED_TO","FILENAME_BACKTRACE"," \
    FILENAME_CORE_BACKTRACE", %%multiline"

#define PR_ATTACH_BINARY "\n%attach:: %binary"

enum {
    RM_FLAG_NOTIFY = (1 << 0),
    RM_FLAG_DEBUG  = (1 << 1)
};

static void exec_and_feed_input(const char* text, char **args)
{
    int pipein[2];

    pid_t child = libreport_fork_execv_on_steroids(
                EXECFLG_INPUT | EXECFLG_QUIET,
                args,
                pipein,
                /*env_vec:*/ NULL,
                /*dir:*/ NULL,
                /*uid (ignored):*/ 0
    );

    libreport_full_write_str(pipein[1], text);
    close(pipein[1]);

    int status;
    libreport_safe_waitpid(child, &status, 0); /* wait for command completion */
    if (status != 0)
        error_msg_and_die("Error running '%s'", args[0]);
}

static char** append_str_to_vector(char **vec, unsigned *size_p, const char *str)
{
    //log_warning("old vec: %p", vec);
    unsigned size = *size_p;
    vec = (char**) g_realloc(vec, (size+2) * sizeof(vec[0]));
    vec[size] = g_strdup(str);
    //log_warning("new vec: %p, added [%d] %p", vec, size, vec[size]);
    size++;
    vec[size] = NULL;
    *size_p = size;
    return vec;
}

static char *ask_email_address(const char *type, const char *def_address)
{
    g_autofree char *ask_text = g_strdup_printf(_("Email address of %s was not specified. Would you like to do so now? If not, '%s' is to be used"), type, def_address);
    const int ret = libreport_ask_yes_no(ask_text);

    if (!ret)
        return g_strdup(def_address);

    ask_text = g_strdup_printf(_("Please, type email address of %s:"), type);
    char *address = libreport_ask(ask_text);

    if (address == NULL || address[0] == '\0')
    {
        libreport_set_xfunc_error_retval(EXIT_CANCEL_BY_USER);
        error_msg_and_die(_("Can't continue without email address of %s"), type);
    }

    return address;
}

static void create_and_send_email(
                const char *dump_dir_name,
                GHashTable *settings,
                const char *fmt_file,
                int flag)
{
    problem_data_t *problem_data = create_problem_data_for_reporting(dump_dir_name);
    if (!problem_data)
        libreport_xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    char* env;
    env = getenv("Mailx_EmailFrom");
    g_autofree char *email_from = (env ? g_strdup(env) : g_strdup(g_hash_table_lookup(settings, "EmailFrom")) ? : ask_email_address("sender", "ABRT Daemon <DoNotReply>"));
    env = getenv("Mailx_EmailTo");
    g_autofree char *email_to = (env ? g_strdup(env) : g_strdup(g_hash_table_lookup(settings, "EmailTo")) ? : ask_email_address("receiver", "root@localhost"));
    env = getenv("Mailx_SendBinaryData");
    if (!env)
        env = g_hash_table_lookup(settings, "SendBinaryData");
    bool send_binary_data = libreport_string_to_bool(env ? env : "");

    problem_formatter_t *pf = problem_formatter_new();
    /* formatting file is not set */
    if (fmt_file == NULL)
    {
        env = getenv("Mailx_Subject");
        const char *subject = (env ? env : g_hash_table_lookup(settings, "Subject") ? : PR_DEFAULT_SUBJECT);

        g_autofree char *format_string = g_strdup_printf(PR_MAILX_TEMPLATE, subject);

        /* attaching binary file to the email */
        if (send_binary_data)
            format_string = libreport_append_to_malloced_string(format_string, PR_ATTACH_BINARY);

        if (problem_formatter_load_string(pf, format_string))
            error_msg_and_die("BUG: Invalid default problem report format string");
    }
    else
    {
        if (problem_formatter_load_file(pf, fmt_file))
            error_msg_and_die("Invalid format file: %s", fmt_file);
    }

    problem_report_t *pr = NULL;
    if (problem_formatter_generate_report(pf, problem_data, &pr))
        error_msg_and_die("Failed to format bug report from problem data");

    const char *subject = problem_report_get_summary(pr);
    const char *dsc = problem_report_get_description(pr);

    if (flag & RM_FLAG_DEBUG)
    {
        printf("subject: %s\n"
                  "\n"
                  "%s"
                  "\n"
                  , subject
                  , dsc);

        puts("attachments:");
        for (GList *a = problem_report_get_attachments(pr); a != NULL; a = g_list_next(a))
            printf(" %s\n", (const char *)a->data);

        problem_report_free(pr);
        problem_formatter_free(pf);
        exit(0);
    }

    g_autofree char **args = NULL;
    unsigned arg_size = 0;
    args = append_str_to_vector(args, &arg_size, "/bin/mailx");

    /* attaching files to the email */
    for (GList *a = problem_report_get_attachments(pr); a != NULL; a = g_list_next(a))
    {
        args = append_str_to_vector(args, &arg_size, "-a");
        g_autofree char *resolved_path = realpath(dump_dir_name, NULL);
        g_autofree char *full_name = g_build_filename(resolved_path ? resolved_path : "", a->data, NULL);
        args = append_str_to_vector(args, &arg_size, full_name);
    }

    args = append_str_to_vector(args, &arg_size, "-s");
    args = append_str_to_vector(args, &arg_size, subject);
    args = append_str_to_vector(args, &arg_size, "-r");
    args = append_str_to_vector(args, &arg_size, email_from);
    args = append_str_to_vector(args, &arg_size, email_to);

    /* This makes (some versions of) mailx to wait for child process to finish,
     * and to report its exit code, not useless "always 0" exit code.
     * Sadly, usually this still doesn't help. See:
     * https://bugzilla.redhat.com/show_bug.cgi?id=740895
     */
    putenv((char*)"sendwait=1");

    /* Prevent mailx from creating dead.letter if sending fails. The file is
     * useless in our case and if the reporter is called from abrtd, SELinux
     * complains a lot about mailx touching ABRT data.
     */
    putenv((char*)"DEAD=/dev/null");

    if (flag & RM_FLAG_NOTIFY)
        log_warning(_("Sending a notification email to: %s"), email_to);
    else
        log_warning(_("Sending an email..."));

    exec_and_feed_input(dsc, args);

    problem_report_free(pr);
    problem_formatter_free(pf);

    problem_data_free(problem_data);

    if (!(flag & RM_FLAG_NOTIFY))
    {
        struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
        if (dd)
        {
            report_result_t *result;

            result = report_result_new_with_label_from_env("email");
            g_autofree char *url = g_strdup_printf("mailto:%s", email_to);

            report_result_set_url(result, url);

            libreport_add_reported_to_entry(dd, result);

            report_result_free(result);

            dd_close(dd);
        }
        log_warning(_("Email was sent to: %s"), email_to);
    }
}

int main(int argc, char **argv)
{
    abrt_init(argv);

    /* I18n */
    setlocale(LC_ALL, "");
#if ENABLE_NLS
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
#endif

    const char *dump_dir_name = ".";
    const char *conf_file = CONF_DIR"/plugins/mailx.conf";
    const char *fmt_file = NULL;

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "& [-v] -d DIR [-c CONFFILE] [-F FMTFILE]"
        "\n"
        "\n""Sends contents of a problem directory DIR via email"
        "\n"
        "\n""If not specified, CONFFILE defaults to "CONF_DIR"/plugins/mailx.conf"
        "\n""Its lines should have 'PARAM = VALUE' format."
        "\n""Recognized string parameters: Subject, EmailFrom, EmailTo."
        "\n""Recognized boolean parameter (VALUE should be 1/0, yes/no): SendBinaryData."
        "\n""Parameters can be overridden via $Mailx_PARAM environment variables."
    );

    enum {
        OPT_v = 1 << 0,
        OPT_d = 1 << 1,
        OPT_c = 1 << 2,
        OPT_F = 1 << 3,
        OPT_n = 1 << 4,
        OPT_D = 1 << 5,
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&libreport_g_verbose),
        OPT_STRING('d', NULL, &dump_dir_name, "DIR"     , _("Problem directory")),
        OPT_STRING('c', NULL, &conf_file    , "CONFFILE", _("Config file")),
        OPT_STRING('F', NULL, &fmt_file     , "FILE"    , _("Formatting file for an email")),
        OPT_BOOL('n', "notify-only", NULL  , _("Notify only (Do not mark the report as sent)")),
        OPT_BOOL(  'D', NULL, NULL          ,         _("Debug")),
        OPT_END()
    };
    unsigned opts = libreport_parse_opts(argc, argv, program_options, program_usage_string);

    libreport_export_abrt_envvars(0);

    GHashTable *settings = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
    libreport_load_conf_file(conf_file, settings, /*skip key w/o values:*/ false);

    int flag = 0;
    if (opts & OPT_n)
        flag |= RM_FLAG_NOTIFY;

    if (opts & OPT_D)
        flag |= RM_FLAG_DEBUG;

    create_and_send_email(dump_dir_name, settings, fmt_file, flag);

    if (settings)
        g_hash_table_destroy(settings);
    return 0;
}
