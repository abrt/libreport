/*
    Copyright (C) 2010  ABRT team
    Copyright (C) 2010  RedHat Inc

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
#include "abrt_xmlrpc.h"
#include "rhbz.h"

#define XML_RPC_SUFFIX "/xmlrpc.cgi"

static void report_to_bugzilla(const char *dump_dir_name, const char *login,
                               const char *password, const char *bugzilla_xmlrpc,
                               const char *bugzilla_url, int ssl_verify)
{
    problem_data_t *problem_data = create_problem_data_for_reporting(dump_dir_name);
    if (!problem_data)
        xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    const char *component = get_problem_item_content_or_NULL(problem_data, FILENAME_COMPONENT);
    const char *duphash   = get_problem_item_content_or_NULL(problem_data, FILENAME_DUPHASH);
//COMPAT, remove after 2.1 release
    if (!duphash) duphash = get_problem_item_content_or_NULL(problem_data, "global_uuid");
    if (!duphash)
        error_msg_and_die(_("Essential file '%s' is missing, can't continue.."),
                          FILENAME_DUPHASH);

    if (!*duphash)
        error_msg_and_die(_("Essential file '%s' is empty, can't continue.."),
                          FILENAME_DUPHASH);

    const char *release   = get_problem_item_content_or_NULL(problem_data, FILENAME_OS_RELEASE);
    if (!release) /* Old dump dir format compat. Remove in abrt-2.1 */
        release = get_problem_item_content_or_NULL(problem_data, "release");

    struct abrt_xmlrpc *client = abrt_xmlrpc_new_client(bugzilla_xmlrpc, ssl_verify);

    log(_("Logging into Bugzilla at %s"), bugzilla_url);
    rhbz_login(client, login, password);

    log(_("Checking for duplicates"));
    char *product = NULL;
    char *version = NULL;
    parse_release_for_bz(release, &product, &version);
    free(version);

    xmlrpc_value *result;
    if (strcmp(product, "Fedora") == 0)
        result  = rhbz_search_duphash(client, component, product, duphash);
    else
        result  = rhbz_search_duphash(client, component, NULL, duphash);

    xmlrpc_value *all_bugs = rhbz_get_member("bugs", result);
    xmlrpc_DECREF(result);

    if (!all_bugs)
        error_msg_and_die(_("Missing mandatory member 'bugs'"));

    int all_bugs_size = rhbz_array_size(all_bugs);
    // When someone clones bug it has same duphash, so we can find more than 1.
    // Need to be checked if component is same.
    VERB3 log("Bugzilla has %i reports with same duphash '%s'",
              all_bugs_size, duphash);

    int bug_id = -1;
    struct bug_info *bz = NULL;
    if (all_bugs_size > 0)
    {
        bug_id = rhbz_bug_id(all_bugs);
        xmlrpc_DECREF(all_bugs);
        bz = rhbz_bug_info(client, bug_id);

        if (strcmp(bz->bi_product, product) != 0)
        {
            /* found something, but its a different product */
            free_bug_info(bz);

            xmlrpc_value *result = rhbz_search_duphash(client, component,
                                                       product, duphash);
            xmlrpc_value *all_bugs = rhbz_get_member("bugs", result);
            xmlrpc_DECREF(result);

            all_bugs_size = rhbz_array_size(all_bugs);
            if (all_bugs_size > 0)
            {
                bug_id = rhbz_bug_id(all_bugs);
                bz = rhbz_bug_info(client, bug_id);
            }
            xmlrpc_DECREF(all_bugs);
        }
    }
    free(product);

    if (all_bugs_size == 0) // Create new bug
    {
        log(_("Creating a new bug"));
        bug_id = rhbz_new_bug(client, problem_data, bug_id);

        log("Adding attachments to bug %i", bug_id);
        char bug_id_str[sizeof(int)*3 + 2];
        sprintf(bug_id_str, "%i", bug_id);

        rhbz_attachments(client, bug_id_str, problem_data, RHBZ_NOMAIL_NOTIFY);

        bz = new_bug_info();
        bz->bi_status = xstrdup("NEW");
        bz->bi_id = bug_id;
        goto log_out;
    }

    // decision based on state
    log(_("Bug is already reported: %i"), bz->bi_id);
    if ((strcmp(bz->bi_status, "CLOSED") == 0)
     && (strcmp(bz->bi_resolution, "DUPLICATE") == 0)
    ) {
        struct bug_info *origin;
        origin = rhbz_find_origin_bug_closed_duplicate(client, bz);
        if (origin)
        {
            free_bug_info(bz);
            bz = origin;
        }
    }

    if (strcmp(bz->bi_status, "CLOSED") != 0)
    {
        if ((strcmp(bz->bi_reporter, login) != 0)
            && (!g_list_find_custom(bz->bi_cc_list, login, (GCompareFunc)g_strcmp0)))
        {
            log(_("Add %s to CC list"), login);
            rhbz_mail_to_cc(client, bz->bi_id, login, RHBZ_NOMAIL_NOTIFY);
        }

        const char *comment = get_problem_item_content_or_NULL(problem_data, FILENAME_COMMENT);
        if (comment && comment[0])
        {
            const char *package = get_problem_item_content_or_NULL(problem_data, FILENAME_PACKAGE);
            const char *release = get_problem_item_content_or_NULL(problem_data, FILENAME_OS_RELEASE);
//COMPAT, remove in abrt-2.1 release
            if (!release)release= get_problem_item_content_or_NULL(problem_data, "release");
            const char *arch    = get_problem_item_content_or_NULL(problem_data, FILENAME_ARCHITECTURE);
            char *full_dsc = xasprintf("Package: %s\n"
                                       "Architecture: %s\n"
                                       "OS Release: %s\n"
                                       "\n"
                                       "Comment\n"
                                       "-----\n"
                                       "%s\n",
                                       package, arch, release, comment
            );
            log(_("Adding new comment to bug %d"), bz->bi_id);
            /* unused code, enable it when gui/cli will be ready
            int is_priv = is_private && string_to_bool(is_private);
            const char *is_private = get_problem_item_content_or_NULL(problem_data,
                                                                      "is_private");
            */
            rhbz_add_comment(client, bz->bi_id, full_dsc, 0);
            free(full_dsc);
        }
    }

 log_out:
    log(_("Logging out"));
    rhbz_logout(client);

    log("Status: %s%s%s %s/show_bug.cgi?id=%u",
                bz->bi_status,
                bz->bi_resolution ? " " : "",
                bz->bi_resolution ? bz->bi_resolution : "",
                bugzilla_url,
                bz->bi_id);

    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (dd)
    {
        char *msg = xasprintf("Bugzilla: URL=%s/show_bug.cgi?id=%u", bugzilla_url, bz->bi_id);
        add_reported_to(dd, msg);
        free(msg);
        dd_close(dd);
    }

    free_problem_data(problem_data);
    free_bug_info(bz);
    abrt_xmlrpc_free_client(client);
}

int main(int argc, char **argv)
{
    abrt_init(argv);

    map_string_h *settings = new_map_string();
    const char *dump_dir_name = ".";
    GList *conf_file = NULL;

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "\n"
        "\1 [-v] [-c CONFFILE] -d DIR\n"
        "or:\n"
        "\1 [-v] [-c CONFFILE] [-d DIR] -t[ID] FILE...\n"
        "\n"
        "Reports problem to Bugzilla.\n"
        "\n"
        "The tool reads DIR. Then it logs in to Bugzilla and tries to find a bug\n"
        "with the same abrt_hash:HEXSTRING in 'Whiteboard'.\n"
        "\n"
        "If such bug is not found, then a new bug is created. Elements of DIR\n"
        "are stored in the bug as part of bug description or as attachments,\n"
        "depending on their type and size.\n"
        "\n"
        "Otherwise, if such bug is found and it is marked as CLOSED DUPLICATE,\n"
        "the tool follows the chain of duplicates until it finds a non-DUPLICATE bug.\n"
        "The tool adds a new comment to found bug.\n"
        "\n"
        "The URL to new or modified bug is printed to stdout and recorded in\n"
        "'reported_to' element.\n"
        "\n"
        "If not specified, CONFFILE defaults to "CONF_DIR"/plugins/bugzilla.conf\n"
        "Its lines should have 'PARAM = VALUE' format.\n"
        "Recognized string parameters: BugzillaURL, Login, Password.\n"
        "Recognized boolean parameter (VALUE should be 1/0, yes/no): SSLVerify.\n"
        "Parameters can be overridden via $Bugzilla_PARAM environment variables.\n"
        "\n"
        "Option -t uploads FILEs to the already created bug on Bugzilla site.\n"
        "The bug ID is retrieved from directory specified by -d DIR.\n"
        "If problem data in DIR was never reported to Bugzilla, upload will fail.\n"
        "\n"
        "Option -tID uploads FILEs to the bug with specified ID on Bugzilla site.\n"
        "-d DIR is ignored."
    );
    enum {
        OPT_v = 1 << 0,
        OPT_d = 1 << 1,
        OPT_c = 1 << 2,
        OPT_t = 1 << 3,
    };

    char *ticket_no = NULL;
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_STRING(   'd', NULL, &dump_dir_name, "DIR" , _("Dump directory")),
        OPT_LIST(     'c', NULL, &conf_file    , "FILE", _("Configuration file (may be given many times)")),
        OPT_OPTSTRING('t', "ticket", &ticket_no, "ID"  , _("Attach FILEs [to bug with this ID]")),
        OPT_END()
    };
    unsigned opts = parse_opts(argc, argv, program_options, program_usage_string);

    if ((opts & OPT_t) && !ticket_no)
    {
        error_msg_and_die("Not implemented yet");
//TODO:
//        /* -t */
//        struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
//        if (!dd)
//            xfunc_die();
//        report_result_t *reported_to = find_in_reported_to(dd, "Bugzilla:");
//        dd_close(dd);
//
//        if (!reported_to || !reported_to->url)
//            error_msg_and_die("Can't attach: problem data in '%s' "
//                    "was not reported to Bugzilla and therefore has no URL",
//                    dump_dir_name);
//        url = reported_to->url;
//        reported_to->url = NULL;
//        free_report_result(reported_to);
//        ...
    }

    export_abrt_envvars(0);

    if (!conf_file)
        conf_file = g_list_append(conf_file, (char*) CONF_DIR"/plugins/bugzilla.conf");
    while (conf_file)
    {
        char *fn = (char *)conf_file->data;
        VERB1 log("Loading settings from '%s'", fn);
        load_conf_file(fn, settings, /*skip key w/o values:*/ true);
        VERB3 log("Loaded '%s'", fn);
        conf_file = g_list_remove(conf_file, fn);
    }

    VERB1 log("Initializing XML-RPC library");
    xmlrpc_env env;
    xmlrpc_env_init(&env);
    xmlrpc_client_setup_global_const(&env);
    if (env.fault_occurred)
        error_msg_and_die("XML-RPC Fault: %s(%d)", env.fault_string, env.fault_code);
    xmlrpc_env_clean(&env);

    const char *environ;
    const char *login;
    const char *password;
    const char *bugzilla_xmlrpc;
    const char *bugzilla_url;
    bool ssl_verify;

    environ = getenv("Bugzilla_Login");
    login = environ ? environ : get_map_string_item_or_empty(settings, "Login");
    environ = getenv("Bugzilla_Password");
    password = environ ? environ : get_map_string_item_or_empty(settings, "Password");
    if (!login[0] || !password[0])
        error_msg_and_die(_("Empty login or password, please check your configuration"));

    environ = getenv("Bugzilla_BugzillaURL");
    bugzilla_url = environ ? environ : get_map_string_item_or_empty(settings, "BugzillaURL");
    if (!bugzilla_url[0])
        bugzilla_url = "https://bugzilla.redhat.com";
    bugzilla_xmlrpc = xasprintf("%s"XML_RPC_SUFFIX, bugzilla_url);

    environ = getenv("Bugzilla_SSLVerify");
    ssl_verify = string_to_bool(environ ? environ : get_map_string_item_or_empty(settings, "SSLVerify"));

    if (opts & OPT_t)
    {
        if (!argv[optind])
            show_usage_and_die(program_usage_string, program_options);

        struct abrt_xmlrpc *client = abrt_xmlrpc_new_client(bugzilla_xmlrpc, ssl_verify);

        log(_("Logging into Bugzilla at %s"), bugzilla_url);
        rhbz_login(client, login, password);

        while (argv[optind])
        {
            const char *filename = argv[optind++];
            VERB1 log("Attaching file '%s' to bugticket %s", filename, ticket_no);

            int fd = open(filename, O_RDONLY);
            if (fd < 0)
            {
                perror_msg("Can't open '%s'", filename);
                continue;
            }

            struct stat st;
            if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
            {
                error_msg("'%s': not a regular file", filename);
                close(fd);
                continue;
            }

            off_t size = lseek(fd, 0, SEEK_END);
            if (size < 0)
            {
                perror_msg("Can't lseek '%s'", filename);
                close(fd);
                continue;
            }
            lseek(fd, 0, SEEK_SET);

    /* FIXME: what if the file is tens of gigabytes? */
            char *data = xmalloc(size + 1);
            ssize_t r = full_read(fd, data, size);
            if (r < 0)
            {
                free(data);
                perror_msg("Can't read '%s'", filename);
                close(fd);
                continue;
            }
            close(fd);

            rhbz_attachment(client, filename, ticket_no, data, r, /*flags*/ 0);
            free(data);
        }

        log(_("Logging out"));
        rhbz_logout(client);
        abrt_xmlrpc_free_client(client);
    }
    else
    {
        report_to_bugzilla(dump_dir_name, login, password, bugzilla_xmlrpc,
                           bugzilla_url, ssl_verify);
    }

    free_map_string(settings);
    return 0;
}
