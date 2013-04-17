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

static void exec_and_feed_input(const char* text, char **args)
{
    int pipein[2];

    pid_t child = fork_execv_on_steroids(
                EXECFLG_INPUT | EXECFLG_QUIET,
                args,
                pipein,
                /*env_vec:*/ NULL,
                /*dir:*/ NULL,
                /*uid (ignored):*/ 0
    );

    full_write_str(pipein[1], text);
    close(pipein[1]);

    int status;
    safe_waitpid(child, &status, 0); /* wait for command completion */
    if (status != 0)
        error_msg_and_die("Error running '%s'", args[0]);
}

static char** append_str_to_vector(char **vec, unsigned *size_p, const char *str)
{
    //log("old vec: %p", vec);
    unsigned size = *size_p;
    vec = (char**) xrealloc(vec, (size+2) * sizeof(vec[0]));
    vec[size] = xstrdup(str);
    //log("new vec: %p, added [%d] %p", vec, size, vec[size]);
    size++;
    vec[size] = NULL;
    *size_p = size;
    return vec;
}

static void create_and_send_email(
                const char *dump_dir_name,
                map_string_t *settings,
                bool notify_only)
{
    problem_data_t *problem_data = create_problem_data_for_reporting(dump_dir_name);
    if (!problem_data)
        xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    char* env;
    env = getenv("Mailx_Subject");
    const char *subject = (env ? env : get_map_string_item_or_NULL(settings, "Subject") ? : "[abrt] full crash report");
    env = getenv("Mailx_EmailFrom");
    const char *email_from = (env ? env : get_map_string_item_or_NULL(settings, "EmailFrom") ? : "user@localhost");
    env = getenv("Mailx_EmailTo");
    const char *email_to = (env ? env : get_map_string_item_or_NULL(settings, "EmailTo") ? : "root@localhost");
    env = getenv("Mailx_SendBinaryData");
    bool send_binary_data = string_to_bool(env ? env : get_map_string_item_or_empty(settings, "SendBinaryData"));

    char **args = NULL;
    unsigned arg_size = 0;
    args = append_str_to_vector(args, &arg_size, "/bin/mailx");

    //char *dsc = make_description_mailx(problem_data);
    char *dsc = make_description_bz(problem_data, CD_TEXT_ATT_SIZE_LOGGER);

    if (send_binary_data)
    {
        GHashTableIter iter;
        char *name;
        struct problem_item *value;
        g_hash_table_iter_init(&iter, problem_data);
        while (g_hash_table_iter_next(&iter, (void**)&name, (void**)&value))
        {
            if (value->flags & CD_FLAG_BIN)
            {
                args = append_str_to_vector(args, &arg_size, "-a");
                args = append_str_to_vector(args, &arg_size, value->content);
            }
        }
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

    log(_("Sending an email..."));
    exec_and_feed_input(dsc, args);

    free(dsc);

    while (*args)
        free(*args++);
    args -= arg_size;
    free(args);

    problem_data_free(problem_data);

    if (!notify_only)
    {
        struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
        if (dd)
        {
            char *msg = xasprintf("email: %s", email_to);
            add_reported_to(dd, msg);
            free(msg);
            dd_close(dd);
        }
    }
    log(_("Email was sent to: %s"), email_to);
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

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "& [-v] -d DIR [-c CONFFILE]"
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
        OPT_n = 1 << 3,
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_STRING('d', NULL, &dump_dir_name, "DIR"     , _("Problem directory")),
        OPT_STRING('c', NULL, &conf_file    , "CONFFILE", _("Config file")),
        OPT_BOOL('n', "notify-only", NULL  , _("Notify only (Do not mark the report as sent)")),
        OPT_END()
    };
    unsigned opts = parse_opts(argc, argv, program_options, program_usage_string);

    export_abrt_envvars(0);

    map_string_t *settings = new_map_string();
    load_conf_file(conf_file, settings, /*skip key w/o values:*/ false);

    create_and_send_email(dump_dir_name, settings, /*notify_only*/(opts & OPT_n));

    free_map_string(settings);
    return 0;
}
