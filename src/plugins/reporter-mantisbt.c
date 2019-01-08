/*
    Copyright (C) 2014  ABRT team
    Copyright (C) 2014  RedHat Inc

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc., 51
    Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "internal_libreport.h"
#include "client.h"
#include "mantisbt.h"
#include "problem_report.h"

static void
parse_osinfo_for_mantisbt(map_string_t *osinfo, char** project, char** version)
{
    const char *name = get_map_string_item_or_NULL(osinfo, "CENTOS_MANTISBT_PROJECT");
    if (!name)
        name = get_map_string_item_or_NULL(osinfo, OSINFO_NAME);

    const char *version_id = get_map_string_item_or_NULL(osinfo, "CENTOS_MANTISBT_PROJECT_VERSION");
    if (!version_id)
        version_id = get_map_string_item_or_NULL(osinfo, OSINFO_VERSION_ID);

    if (name && version_id)
    {
        *project = xstrdup(name);
        *version = xstrdup(version_id);
        return;
    }

    /* something bad happend */
    *project = NULL;
    *version = NULL;
}

static char *
ask_mantisbt_login(const char *message)
{
    char *login = ask(message);
    if (login == NULL || login[0] == '\0')
    {
        set_xfunc_error_retval(EXIT_CANCEL_BY_USER);
        error_msg_and_die(_("Can't continue without login"));
    }

    return login;
}

static char *
ask_mantisbt_password(const char *message)
{
    char *password = ask_password(message);
    if (password == NULL || password[0] == '\0')
    {
        set_xfunc_error_retval(EXIT_CANCEL_BY_USER);
        error_msg_and_die(_("Can't continue without password"));
    }

    return password;
}

static void
ask_mantisbt_credentials(mantisbt_settings_t *settings, const char *pre_message)
{
    free(settings->m_login);
    free(settings->m_password);

    char *question = xasprintf("%s %s", pre_message, _("Please enter your MantisBT login:"));
    settings->m_login = ask_mantisbt_login(question);
    free(question);

    question = xasprintf("%s %s '%s':", pre_message, _("Please enter the password for"), settings->m_login);
    settings->m_password = ask_mantisbt_password(question);
    free(question);

    return;
}

static void
verify_credentials(mantisbt_settings_t *settings)
{
    if (settings->m_login[0] == '\0' || settings->m_password[0] == '\0')
        ask_mantisbt_credentials(settings, _("Credentials are not provided by configuration."));

    while (true)
    {
        soap_request_t *req = soap_request_new_for_method("mc_login");
        soap_request_add_credentials_parameter(req, settings);

        mantisbt_result_t *result = mantisbt_soap_call(settings, req);
        soap_request_free(req);

        if (g_verbose > 2)
        {
            GList *ids = response_get_main_ids_list(result->mr_body);
            if (ids != NULL)
                log_warning("%s", (char *)ids->data);
            response_values_free(ids);
        }

        int result_val = result->mr_http_resp_code;
        mantisbt_result_free(result);

        if (result_val == 200)
            return;

        ask_mantisbt_credentials(settings, _("Invalid password or login."));
    }
}

static void
set_settings(mantisbt_settings_t *m, map_string_t *settings, struct dump_dir *dd)
{
    const char *environ;

    environ = getenv("Mantisbt_Login");
    m->m_login = xstrdup(environ ? environ : get_map_string_item_or_empty(settings, "Login"));

    environ = getenv("Mantisbt_Password");
    m->m_password = xstrdup(environ ? environ : get_map_string_item_or_empty(settings, "Password"));

    environ = getenv("Mantisbt_MantisbtURL");
    m->m_mantisbt_url = environ ? environ : get_map_string_item_or_empty(settings, "MantisbtURL");
    if (!m->m_mantisbt_url[0])
        m->m_mantisbt_url = "http://localhost/mantisbt";
    else
    {
        /* We don't want trailing '/': "https://host/dir/" -> "https://host/dir" */
        char *last_slash = strrchr(m->m_mantisbt_url, '/');
        if (last_slash && last_slash[1] == '\0')
            *last_slash = '\0';
    }
    m->m_mantisbt_soap_url = concat_path_file(m->m_mantisbt_url, "api/soap/mantisconnect.php");

    environ = getenv("Mantisbt_Project");
    if (environ)
    {
        m->m_project = xstrdup(environ);
        environ = getenv("Mantisbt_ProjectVersion");
        if (environ)
            m->m_project_version = xstrdup(environ);
    }
    else
    {
        const char *option = get_map_string_item_or_NULL(settings, "Project");
        if (option)
            m->m_project = xstrdup(option);
        option = get_map_string_item_or_NULL(settings, "ProjectVersion");
        if (option)
            m->m_project_version = xstrdup(option);
    }

    if (!m->m_project || !*m->m_project) /* if not overridden or empty... */
    {
        free(m->m_project);
        free(m->m_project_version);

        if (dd != NULL)
        {
            map_string_t *osinfo = new_map_string();

            char *os_info_data = dd_load_text(dd, FILENAME_OS_INFO);
            parse_osinfo(os_info_data, osinfo);
            free(os_info_data);

            parse_osinfo_for_mantisbt(osinfo, &m->m_project, &m->m_project_version);
            free_map_string(osinfo);
        }
    }

    environ = getenv("Mantisbt_SSLVerify");
    m->m_ssl_verify = string_to_bool(environ ? environ : get_map_string_item_or_empty(settings, "SSLVerify"));

    environ = getenv("Mantisbt_DontMatchComponents");
    m->m_DontMatchComponents = environ ? environ : get_map_string_item_or_empty(settings, "DontMatchComponents");

    m->m_create_private = get_global_create_private_ticket();

    if (!m->m_create_private)
    {
        environ = getenv("Mantisbt_CreatePrivate");
        m->m_create_private = string_to_bool(environ ? environ : get_map_string_item_or_empty(settings, "CreatePrivate"));
    }
    log_notice("create private MantisBT ticket: '%s'", m->m_create_private ? "YES": "NO");
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

    const char *program_usage_string = _(
        "\n& [-vf] [-c CONFFILE]... [-F FMTFILE] [-A FMTFILE2] -d DIR"
        "\nor:"
        "\n& [-v] [-c CONFFILE]... [-d DIR] -t[ID] FILE..."
        "\nor:"
        "\n& [-v] [-c CONFFILE]... [-d DIR] -t[ID] -w"
        "\nor:"
        "\n& [-v] [-c CONFFILE]... -h DUPHASH"
        "\n"
        "\nReports problem to MantisBT."
        "\n"
        "\nThe tool reads DIR. Then it tries to find an issue"
        "\nwith the same abrt_hash in custom field 'abrt_hash'."
        "\n"
        "\nIf such issue is not found, then a new issue is created. Elements of DIR"
        "\nare stored in the issue as part of issue description or as attachments,"
        "\ndepending on their type and size."
        "\n"
        "\nOtherwise, if such issue is found and it is marked as CLOSED DUPLICATE,"
        "\nthe tool follows the chain of duplicates until it finds a non-DUPLICATE issue."
        "\nThe tool adds a new comment to found issue."
        "\n"
        "\nThe URL to new or modified issue is printed to stdout and recorded in"
        "\n'reported_to' element."
        "\n"
        "\nOption -t uploads FILEs to the already created issue on MantisBT site."
        "\nThe issue ID is retrieved from directory specified by -d DIR."
        "\nIf problem data in DIR was never reported to MantisBT, upload will fail."
        "\n"
        "\nOption -tID uploads FILEs to the issue with specified ID on MantisBT site."
        "\n-d DIR is ignored."
        "\n"
        "\nOption -r sets the last url from reporter_to element which is prefixed with"
        "\nTRACKER_NAME to URL field. This option is applied only when a new issue is to be"
        "\nfiled. The default value is 'ABRT Server'"
        "\n"
        "\nIf not specified, CONFFILE defaults to "CONF_DIR"/plugins/mantisbt.conf"
        "\nand user's local ~"USER_HOME_CONFIG_PATH"/mantisbt.conf."
        "\nIts lines should have 'PARAM = VALUE' format."
        "\nRecognized string parameters: MantisbtURL, Login, Password, Project, ProjectVersion."
        "\nRecognized boolean parameter (VALUE should be 1/0, yes/no): SSLVerify, CreatePrivate."
        "\nUser's local configuration overrides the system wide configuration."
        "\nParameters can be overridden via $Mantisbt_PARAM environment variables."
        "\n"
        "\nFMTFILE default to "CONF_DIR"/plugins/mantisbt_format.conf."
        "\nFMTFILE2 default to "CONF_DIR"/plugins/mantisbt_formatdup.conf."
    );

    enum {
        OPT_v = 1 << 0,
        OPT_d = 1 << 1,
        OPT_c = 1 << 2,
        OPT_F = 1 << 3,
        OPT_A = 1 << 4,
        OPT_t = 1 << 5,
        OPT_f = 1 << 6,
        OPT_h = 1 << 7,
        OPT_r = 1 << 8,
        OPT_D = 1 << 9,
    };

    const char *dump_dir_name = ".";
    GList *conf_file = NULL;
    const char *fmt_file = CONF_DIR"/plugins/mantisbt_format.conf";
    const char *fmt_file2 = CONF_DIR"/plugins/mantisbt_formatdup.conf";
    char *abrt_hash = NULL;
    char *ticket_no = NULL;
    const char *tracker_str = "ABRT Server";
    char *debug_str = NULL;
    mantisbt_settings_t mbt_settings = { 0 };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_STRING(   'd', NULL, &dump_dir_name , "DIR"    , _("Problem directory")),
        OPT_LIST(     'c', NULL, &conf_file     , "FILE"   , _("Configuration file (may be given many times)")),
        OPT_STRING(   'F', NULL, &fmt_file      , "FILE"   , _("Formatting file for initial comment")),
        OPT_STRING(   'A', NULL, &fmt_file2     , "FILE"   , _("Formatting file for duplicates")),
        OPT_OPTSTRING('t', "ticket", &ticket_no , "ID"     , _("Attach FILEs [to issue with this ID]")),
        OPT_BOOL(     'f', NULL, NULL,                       _("Force reporting even if this problem is already reported")),
        OPT_STRING(   'h', "duphash", &abrt_hash, "DUPHASH", _("Print BUG_ID which has given DUPHASH")),
        OPT_STRING(   'r', "tracker", &tracker_str, "TRACKER_NAME", _("A name of bug tracker for an additional URL from 'reported_to'")),

        OPT_OPTSTRING('D', "debug", &debug_str  , "STR"    , _("Debug")),
        OPT_END()
    };

    unsigned opts = parse_opts(argc, argv, program_options, program_usage_string);
    argv += optind;

    export_abrt_envvars(0);

    map_string_t *settings = new_map_string();

    {
        char *local_conf = NULL;
        if (!conf_file)
        {
            conf_file = g_list_append(conf_file, (char*) CONF_DIR"/plugins/mantisbt.conf");
            local_conf = xasprintf("%s"USER_HOME_CONFIG_PATH"/mantisbt.conf", getenv("HOME"));
            conf_file = g_list_append(conf_file, local_conf);
        }
        while (conf_file)
        {
            char *fn = (char *)conf_file->data;
            log_notice("Loading settings from '%s'", fn);
            load_conf_file(fn, settings, /*skip key w/o values:*/ false);
            log_debug("Loaded '%s'", fn);
            conf_file = g_list_delete_link(conf_file, conf_file);
        }
        free(local_conf);

        struct dump_dir *dd = NULL;
        if (abrt_hash == NULL)
        {
            dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
            if (!dd)
                error_msg_and_die(_("Can't open problem dir '%s'."), dump_dir_name);
        }

        set_settings(&mbt_settings, settings, dd);
        dd_close(dd);
        /* WRONG! set_settings() does not copy the strings, it merely sets up pointers
         * to settings[] dictionary:
         */
        /*free_map_string(settings);*/
    }

    /* No connection is opened between client and server. Users authentication
     * is performed on every SOAP method call. In the first step we verify the
     * credentials by calling 'mc_login' method.  In the case the credentials are
     * correctly applies the reporter uses them in the next requests. It is not
     * necessary to call 'mc_login' method because the method provides only
     * verification of credentials.
     */
    verify_credentials(&mbt_settings);

    if (abrt_hash)
    {
        log_warning(_("Looking for similar problems in MantisBT"));
        GList *ids = mantisbt_search_by_abrt_hash(&mbt_settings, abrt_hash);
        mantisbt_settings_free(&mbt_settings);

        if (ids == NULL)
            return EXIT_FAILURE;

        puts(ids->data);
        response_values_free(ids);
        return EXIT_SUCCESS;
    }

    mantisbt_get_project_id_from_name(&mbt_settings);

    if (opts & OPT_t)
    {
        if (!argv[0])
            show_usage_and_die(program_usage_string, program_options);

        if (!ticket_no)
        {
            struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
            g_autoptr(report_result_t) reported_to = NULL;
            char *url;

            if (!dd)
                xfunc_die();

            reported_to = find_in_reported_to(dd, "MantisBT");

            dd_close(dd);

            if (NULL == reported_to)
                error_msg_and_die(_("Can't get MantisBT ID because this problem has not yet been reported to MantisBT."));

            url = report_result_get_url(reported_to);

            if (prefixcmp(url, mbt_settings.m_mantisbt_url) != 0)
                error_msg_and_die(_("This problem has been reported to MantisBT '%s' which differs from the configured MantisBT '%s'."), url, mbt_settings.m_mantisbt_url);

            ticket_no = strrchr(url, '=');
            if (!ticket_no)
                error_msg_and_die(_("Malformed url to MantisBT '%s'."), url);

            /* won't ever call free on it - it simplifies the code a lot */
            ticket_no = xstrdup(ticket_no + 1);
            log_warning(_("Using MantisBT ID '%s'"), ticket_no);
        }

        /* Attach files to existing MantisBT issues */
        while (*argv)
        {
            const char *path = *argv++;
            char *filename = basename(path);
            log_warning(_("Attaching file '%s' to issue %s"), filename, ticket_no);
            mantisbt_attach_file(&mbt_settings, ticket_no, filename, path);
        }

        return 0;
    }

    /* Create new issue in MantisBT */

    if (!(opts & OPT_f))
    {
        struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
        g_autoptr(report_result_t) reported_to = NULL;
        g_autofree char *url = NULL;

        if (!dd)
            xfunc_die();

        reported_to = find_in_reported_to(dd, "MantisBT");

        dd_close(dd);

        if (NULL != reported_to)
        {
            url = report_result_get_url(reported_to);
        }
        if (NULL != url)
        {
            g_autofree char *msg = NULL;

            msg = xasprintf(_("This problem was already reported to MantisBT (see '%s')."
                            " Do you still want to create a new issue?"),
                            url);

            if (!ask_yes_no(msg))
                return 0;
        }
    }

    problem_data_t *problem_data = create_problem_data_for_reporting(dump_dir_name);
    if (!problem_data)
        xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    const char *category = problem_data_get_content_or_die(problem_data, FILENAME_COMPONENT);
    const char *duphash   = problem_data_get_content_or_die(problem_data, FILENAME_DUPHASH);

    if (opts & OPT_D)
    {
        problem_formatter_t *pf = problem_formatter_new();
        problem_formatter_add_section(pf, PR_SEC_ADDITIONAL_INFO, /* optional section */ 0);

        if (problem_formatter_load_file(pf, fmt_file))
            error_msg_and_die("Invalid format file: %s", fmt_file);

        problem_report_t *pr = NULL;
        if (problem_formatter_generate_report(pf, problem_data, &pr))
            error_msg_and_die("Failed to format issue report from problem data");

        printf("summary: %s\n"
                "\n"
                "Description:\n%s\n"
                "Additional info:\n%s\n"
                , problem_report_get_summary(pr)
                , problem_report_get_description(pr)
                , problem_report_get_section(pr, PR_SEC_ADDITIONAL_INFO)
        );

        puts("attachments:");
        for (GList *a = problem_report_get_attachments(pr); a != NULL; a = g_list_next(a))
            printf(" %s\n", (const char *)a->data);

        problem_report_free(pr);
        problem_formatter_free(pf);
        exit(0);
    }

     unsigned long bug_id = 0;

    /* If REMOTE_RESULT contains "DUPLICATE 12345", we consider it a dup of 12345
     * and won't search on MantisBT server.
     */
    char *remote_result;
    remote_result = problem_data_get_content_or_NULL(problem_data, FILENAME_REMOTE_RESULT);
    if (remote_result)
    {
        char *cmd = strtok(remote_result, " \n");
        char *id = strtok(NULL, " \n");

        if (!prefixcmp(cmd, "DUPLICATE"))
        {
            errno = 0;
            char *e;
            bug_id = strtoul(id, &e, 10);
            if (errno || id == e || *e != '\0' || bug_id > INT_MAX)
            {
                /* error / no digits / illegal trailing chars / too big a number */
                bug_id = 0;
            }
        }
    }

    mantisbt_issue_info_t *ii;
    if (!bug_id)
    {
        log_warning(_("Checking for duplicates"));

        int existing_id = -1;
        int crossver_id = -1;
        {
            /* Figure out whether we want to match category
             * when doing dup search.
             */
            const char *category_substitute = is_in_comma_separated_list(category, mbt_settings.m_DontMatchComponents) ? NULL : category;

            /* We don't do dup detection across versions (see below why),
             * but we do add a note if cross-version potential dup exists.
             * For that, we search for cross version dups first:
             */
            // SOAP API searching method is not in the final version, it's possible the project will be string
            GList *crossver_bugs_ids = mantisbt_search_duplicate_issues(&mbt_settings, category_substitute, /*version*/ NULL, duphash);

            unsigned crossver_bugs_count = g_list_length(crossver_bugs_ids);
            log_debug("MantisBT has %i reports with duphash '%s' including cross-version ones",
                    crossver_bugs_count, duphash);
            if (crossver_bugs_count > 0)
                crossver_id = atoi(g_list_first(crossver_bugs_ids)->data);

            if (crossver_bugs_count > 0)
            {
                // SOAP API searching method is not in the final version, it's possible the project will be string
                GList *dup_bugs_ids = mantisbt_search_duplicate_issues(&mbt_settings, category_substitute, mbt_settings.m_project_version, duphash);

                unsigned dup_bugs_count =  g_list_length(dup_bugs_ids);
                log_debug("MantisBT has %i reports with duphash '%s'",
                        dup_bugs_count, duphash);
                if (dup_bugs_count > 0)
                    existing_id = atoi(g_list_first(dup_bugs_ids)->data);
            }
        }

        if (existing_id < 0)
        {
            /* Create new issue */
            log_warning(_("Creating a new issue"));
            problem_formatter_t *pf = problem_formatter_new();
            problem_formatter_add_section(pf, PR_SEC_ADDITIONAL_INFO, 0);

            if (problem_formatter_load_file(pf, fmt_file))
                error_msg_and_die(_("Invalid format file: %s"), fmt_file);

            problem_report_t *pr = NULL;
            if (problem_formatter_generate_report(pf, problem_data, &pr))
                error_msg_and_die(_("Failed to format problem data"));

            if (crossver_id >= 0)
                problem_report_buffer_printf(
                        problem_report_get_buffer(pr, PR_SEC_DESCRIPTION),
                        "\nPotential duplicate: issue %u\n", crossver_id);

            problem_formatter_free(pf);

            /* get tracker URL if exists */
            struct dump_dir *dd = dd_opendir(dump_dir_name, 0);
            char *tracker_url = NULL;
            if (dd)
            {
                g_autoptr(report_result_t) reported_to = NULL;
                g_autofree char *url = NULL;

                reported_to = find_in_reported_to(dd, tracker_str);

                dd_close(dd);

                if (NULL != reported_to)
                {
                    url = report_result_get_url(reported_to);
                }
                if (NULL != url)
                {
                    log_warning(_("Adding External URL to issue"));
                    tracker_url = g_steal_pointer(&url);
                }
            }

            int new_id = mantisbt_create_new_issue(&mbt_settings, problem_data, pr, tracker_url);

            free(tracker_url);

            if (new_id == -1)
                return EXIT_FAILURE;

            log_warning(_("Adding attachments to issue %i"), new_id);
            char *new_id_str = xasprintf("%u", new_id);

            for (GList *a = problem_report_get_attachments(pr); a != NULL; a = g_list_next(a))
            {
                const char *item_name = (const char *)a->data;
                struct problem_item *item = problem_data_get_item_or_NULL(problem_data, item_name);
                if (!item)
                    continue;
                else if (item->flags & CD_FLAG_TXT)
                    mantisbt_attach_data(&mbt_settings, new_id_str, item_name, item->content, strlen(item->content));
                else if (item->flags & CD_FLAG_BIN)
                    mantisbt_attach_file(&mbt_settings, new_id_str, item_name, item->content);
            }

            free(new_id_str);
            problem_report_free(pr);
            ii = mantisbt_issue_info_new();
            ii->mii_id = new_id;
            ii->mii_status = xstrdup("new");

            goto finish;
        }

        bug_id = existing_id;
    }

    ii = mantisbt_get_issue_info(&mbt_settings, bug_id);

    log_warning(_("Bug is already reported: %i"), ii->mii_id);

    /* Follow duplicates */
    if ((strcmp(ii->mii_status, "closed") == 0)
     && (strcmp(ii->mii_resolution, "duplicate") == 0)
    ) {
        mantisbt_issue_info_t *origin = mantisbt_find_origin_bug_closed_duplicate(&mbt_settings, ii);
        if (origin)
        {
            mantisbt_issue_info_free(ii);
            ii = origin;
        }
    }

    /* TODO CC list
     * Is no MantisBT SOAP API method which allows adding users to CC list
     * without updating issue.
     */

    /* Add comment and bt */
    const char *comment = problem_data_get_content_or_NULL(problem_data, FILENAME_COMMENT);
    if (comment && comment[0])
    {
        problem_formatter_t *pf = problem_formatter_new();

        if (problem_formatter_load_file(pf, fmt_file2))
            error_msg_and_die(_("Invalid duplicate format file: '%s"), fmt_file2);

        problem_report_t *pr;
        if (problem_formatter_generate_report(pf, problem_data, &pr))
            error_msg_and_die(_("Failed to format duplicate comment from problem data"));

        const char *mbtcomment = problem_report_get_description(pr);

        int dup_comment = is_comment_dup(ii->mii_notes, mbtcomment);
        if (!dup_comment)
        {
            log_warning(_("Adding new comment to issue %d"), ii->mii_id);
            mantisbt_add_issue_note(&mbt_settings, ii->mii_id, mbtcomment);

            const char *bt = problem_data_get_content_or_NULL(problem_data, FILENAME_BACKTRACE);
            unsigned rating = 0;
            const char *rating_str = problem_data_get_content_or_NULL(problem_data, FILENAME_RATING);
            /* python doesn't have rating file */
            if (rating_str)
                rating = xatou(rating_str);
            if (bt && rating > ii->mii_best_bt_rating)
            {
                char *bug_id_str = xasprintf("%i", ii->mii_id);

                log_warning(_("Attaching better backtrace"));

                // find unique filename of attachment
                char *name = NULL;
                for (int i = 0;; ++i)
                {
                    if (i == 0)
                        name = xasprintf("%s", FILENAME_BACKTRACE);
                    else
                        name = xasprintf("%s%d", FILENAME_BACKTRACE, i);

                    if (g_list_find_custom(ii->mii_attachments, name, (GCompareFunc) strcmp) == NULL)
                        break;

                    free(name);
                }
                mantisbt_attach_data(&mbt_settings, bug_id_str, name, bt, strlen(bt));

                free(name);
                free(bug_id_str);
            }
        }
        else
            log_warning(_("Found the same comment in the issue history, not adding a new one"));

        problem_report_free(pr);
        problem_formatter_free(pf);
    }

finish:
    log_warning(_("Status: %s%s%s %s/view.php?id=%u"),
                ii->mii_status,
                ii->mii_resolution ? " " : "",
                ii->mii_resolution ? ii->mii_resolution : "",
                mbt_settings.m_mantisbt_url,
                ii->mii_id);

    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (dd)
    {
        report_result_t *result;
        char *url;

        result = report_result_new_with_label_from_env("MantisBT");
        url = xasprintf("%s/view.php?id=%u", mbt_settings.m_mantisbt_url, ii->mii_id);

        report_result_set_url(result, url);

        add_reported_to_entry(dd, result);

        free(url);
        report_result_free(result);
        dd_close(dd);
    }

    mantisbt_settings_free(&mbt_settings);
    return 0;
}
