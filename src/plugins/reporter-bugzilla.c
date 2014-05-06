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
#include "client.h"
#include "abrt_xmlrpc.h"
#include "rhbz.h"

#define XML_RPC_SUFFIX "/xmlrpc.cgi"

static void set_settings(struct bugzilla_struct *b, map_string_t *settings)
{
    const char *environ;

    environ = getenv("Bugzilla_Login");
    b->b_login = environ ? environ : get_map_string_item_or_empty(settings, "Login");
    environ = getenv("Bugzilla_Password");
    b->b_password = environ ? environ : get_map_string_item_or_empty(settings, "Password");
    environ = getenv("Bugzilla_BugzillaURL");
    b->b_bugzilla_url = environ ? environ : get_map_string_item_or_empty(settings, "BugzillaURL");
    if (!b->b_bugzilla_url[0])
        b->b_bugzilla_url = "https://bugzilla.redhat.com";
    b->b_bugzilla_xmlrpc = xasprintf("%s"XML_RPC_SUFFIX, b->b_bugzilla_url);

    environ = getenv("Bugzilla_SSLVerify");
    b->b_ssl_verify = string_to_bool(environ ? environ : get_map_string_item_or_empty(settings, "SSLVerify"));

    environ = getenv("Bugzilla_OSRelease");
    b->b_release = environ ? environ : get_map_string_item_or_NULL(settings, "OSRelease");
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
    GList *conf_file = NULL;

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "\n"
        "& [-vbf] [-c CONFFILE] -d DIR\n"
        "or:\n"
        "& [-v] [-c CONFFILE] [-d DIR] -t[ID] FILE...\n"
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
        "Recognized string parameters: BugzillaURL, Login, Password, OSRelease.\n"
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
        OPT_b = 1 << 4,
        OPT_f = 1 << 5,
    };

    char *ticket_no = NULL, *abrt_hash = NULL;
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_STRING(   'd', NULL, &dump_dir_name, "DIR" , _("Dump directory")),
        OPT_LIST(     'c', NULL, &conf_file    , "FILE", _("Configuration file (may be given many times)")),
        OPT_OPTSTRING('t', "ticket", &ticket_no, "ID"  , _("Attach FILEs [to bug with this ID]")),
        OPT_BOOL(     'b', NULL, NULL,                   _("When creating bug, attach binary files too")),
        OPT_BOOL(     'f', NULL, NULL,                   _("Force reporting even if this problem is already reported")),
        OPT_STRING(   'h', "duphash", &abrt_hash, "DUPHASH", _("Find BUG-ID according to DUPHASH")),
        OPT_END()
    };
    unsigned opts = parse_opts(argc, argv, program_options, program_usage_string);
    argv += optind;

    export_abrt_envvars(0);

    map_string_t *settings = new_map_string();
    if (!conf_file)
        conf_file = g_list_append(conf_file, (char*) CONF_DIR"/plugins/bugzilla.conf");
    while (conf_file)
    {
        char *fn = (char *)conf_file->data;
        VERB1 log("Loading settings from '%s'", fn);
        load_conf_file(fn, settings, /*skip key w/o values:*/ true);
        VERB3 log("Loaded '%s'", fn);
        conf_file = g_list_delete_link(conf_file, conf_file);
    }

    VERB1 log("Initializing XML-RPC library");
    xmlrpc_env env;
    xmlrpc_env_init(&env);
    xmlrpc_client_setup_global_const(&env);
    if (env.fault_occurred)
        abrt_xmlrpc_die(&env);
    xmlrpc_env_clean(&env);

    INIT_BUGZILLA(rhbz);
    set_settings(&rhbz, settings);

    struct abrt_xmlrpc *client;
    client = abrt_xmlrpc_new_client(rhbz.b_bugzilla_xmlrpc, rhbz.b_ssl_verify);

    if (abrt_hash)
    {
        char *hash;
        if (prefixcmp(abrt_hash, "abrt_hash:"))
            hash = xasprintf("abrt_hash:%s", abrt_hash);
        else
            hash = xstrdup(abrt_hash);

        /* it's fedora specific */
        char *query = xasprintf("ALL whiteboard:\"%s\" product:\"Fedora\"", hash);
        free(hash);

        xmlrpc_value *result = abrt_xmlrpc_call(client, "Bug.search", "({s:s})",
                                             "quicksearch", query);
        free(query);

        xmlrpc_value *all_bugs = rhbz_get_member("bugs", result);
        xmlrpc_DECREF(result);

        unsigned all_bugs_size = rhbz_array_size(all_bugs);
        if (all_bugs_size > 0)
        {
            int bug_id = rhbz_bug_id(all_bugs);
            printf("%i", bug_id);
        }

        exit(EXIT_SUCCESS);
    }

    if (!rhbz.b_login[0] || !rhbz.b_password[0])
        error_msg_and_die(_("Empty login or password, please check your configuration"));

    if (opts & OPT_t)
    {
        if (!ticket_no)
        {
            error_msg_and_die("Not implemented yet");
//TODO:
//            /* -t */
//            if (!reported_to || !reported_to->url)
//                error_msg_and_die("Can't attach: problem data in '%s' "
//                        "was not reported to Bugzilla and therefore has no URL",
//                        dump_dir_name);
//            url = reported_to->url;
//            reported_to->url = NULL;
//            free_report_result(reported_to);
//            ...
        }

        /* Attach files to existing BZ */
        if (!argv[0])
            show_usage_and_die(program_usage_string, program_options);

        log(_("Logging into Bugzilla at %s"), rhbz.b_bugzilla_url);
        rhbz_login(client, &rhbz);

        while (*argv)
        {
            const char *filename = *argv++;
            VERB1 log("Attaching file '%s' to bug %s", filename, ticket_no);

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

            rhbz_attach_fd(client, filename, ticket_no, fd, /*flags*/ 0);
            close(fd);
        }

        log(_("Logging out"));
        rhbz_logout(client);

#if 0  /* enable if you search for leaks (valgrind etc) */
        abrt_xmlrpc_free_client(client);
        free_map_string(settings);
#endif
        return 0;
    }

    /* Create new bug in Bugzilla */

    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        xfunc_die();
    report_result_t *reported_to = find_in_reported_to(dd, "Bugzilla:");
    dd_close(dd);

    if (reported_to && reported_to->url && !(opts & OPT_f))
    {
        char *msg = xasprintf("This problem was already reported to Bugzilla (see '%s')."
                        " Do you still want to create a new bug?",
                        reported_to->url);
        int yes = ask_yes_no(msg);
        free(msg);
        if (!yes)
            return 0;
    }
    free_report_result(reported_to);

    problem_data_t *problem_data = create_problem_data_for_reporting(dump_dir_name);
    if (!problem_data)
        xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    const char *component = get_problem_item_content_or_die(problem_data, FILENAME_COMPONENT);
    const char *duphash   = get_problem_item_content_or_NULL(problem_data, FILENAME_DUPHASH);
//COMPAT, remove after 2.1 release
    if (!duphash) duphash = get_problem_item_content_or_die(problem_data, "global_uuid");
    if (!rhbz.b_release || !*rhbz.b_release) /* if not overridden or empty... */
    {
        rhbz.b_release = get_problem_item_content_or_NULL(problem_data, FILENAME_OS_RELEASE);
//COMPAT, remove in abrt-2.1
        if (!rhbz.b_release)
		rhbz.b_release = get_problem_item_content_or_die(problem_data, "release");
    }

    log(_("Logging into Bugzilla at %s"), rhbz.b_bugzilla_url);
    rhbz_login(client, &rhbz);

    char *version = NULL;
    parse_release_for_bz(rhbz.b_release, &rhbz.b_product, &version);
    free(version);

    log(_("Checking for duplicates"));

    /*
      selinux guy's almost always move filled bug from component selinux-policy
      to right component.

      bugzilla client is looking for duplicate bug by sending xmlrpc query

      "ALL whiteboard:<hash>  component:<name>  [product:<product>]"

      so if bug is moved from component selinux-policy to other, then query
      returns NULL and creates a new bug.
    */
    const char *product_substitute = (!strcmp(rhbz.b_product, "Fedora")) ? rhbz.b_product : NULL;
    const char *component_substitute = (!strcmp(component, "selinux-policy")) ? NULL : component;
    xmlrpc_value *result = rhbz_search_duphash(client, component_substitute,
                                               product_substitute, duphash);

    xmlrpc_value *all_bugs = rhbz_get_member("bugs", result);
    xmlrpc_DECREF(result);
    if (!all_bugs)
        error_msg_and_die(_("Missing mandatory member 'bugs'"));

    unsigned all_bugs_size = rhbz_array_size(all_bugs);
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

        if (strcmp(bz->bi_product, rhbz.b_product) != 0)
        {
            /* found something, but its a different product */
            free_bug_info(bz);

            xmlrpc_value *result = rhbz_search_duphash(client, component_substitute,
                                                       product_substitute, duphash);
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

    if (all_bugs_size == 0)
    {
        /* Create new bug */
        log(_("Creating a new bug"));
        bug_id = rhbz_new_bug(client, problem_data, rhbz.b_release, bug_id);

        log(_("Adding attachments to bug %i"), bug_id);
        char bug_id_str[sizeof(int)*3 + 2];
        sprintf(bug_id_str, "%i", bug_id);

        int flags = RHBZ_NOMAIL_NOTIFY;
        if (opts & OPT_b)
            flags |= RHBZ_ATTACH_BINARY_FILES;
        rhbz_attach_files(client, bug_id_str, problem_data, flags);

        bz = new_bug_info();
        bz->bi_status = xstrdup("NEW");
        bz->bi_id = bug_id;
        goto log_out;
    }

    log(_("Bug is already reported: %i"), bz->bi_id);

    /* Follow duplicates */
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
        /* Add user's login to CC if not there already */
        if (strcmp(bz->bi_reporter, rhbz.b_login) != 0
         && !g_list_find_custom(bz->bi_cc_list, rhbz.b_login, (GCompareFunc)g_strcmp0)
        ) {
            log(_("Add %s to CC list"), rhbz.b_login);
            rhbz_mail_to_cc(client, bz->bi_id, rhbz.b_login, RHBZ_NOMAIL_NOTIFY);
        }

        /* Add comment */
        const char *comment = get_problem_item_content_or_NULL(problem_data, FILENAME_COMMENT);
        if (comment && comment[0])
        {
            const char *package = get_problem_item_content_or_NULL(problem_data, FILENAME_PACKAGE);
            const char *arch    = get_problem_item_content_or_NULL(problem_data, FILENAME_ARCHITECTURE);
            const char *rating_str = get_problem_item_content_or_NULL(problem_data, FILENAME_RATING);

            struct strbuf *full_desc = strbuf_new();
            strbuf_append_strf(full_desc, "%s\n\n", comment);

            /* python doesn't have rating file */
            if (rating_str)
                strbuf_append_strf(full_desc, "%s: %s\n", FILENAME_RATING, rating_str);
            strbuf_append_strf(full_desc, "Package: %s\n", package);
            /* attach the architecture only if it's different from the initial report */
            if ((strcmp(bz->bi_platform, "All") != 0) &&
                (strcmp(bz->bi_platform, "Unspecified") != 0) &&
                (strcmp(bz->bi_platform, arch) !=0))
                strbuf_append_strf(full_desc, "Architecture: %s\n", arch);
            else
            {
                VERB3 log("not adding the arch: %s because rep_plat is %s", arch, bz->bi_platform);
            }
            strbuf_append_strf(full_desc, "OS Release: %s\n", rhbz.b_release);

            /* unused code, enable it when gui/cli will be ready
            int is_priv = is_private && string_to_bool(is_private);
            const char *is_private = get_problem_item_content_or_NULL(problem_data,
                                                                      "is_private");
            */

            int dup_comment = is_comment_dup(bz->bi_comments, full_desc->buf);
            if (!dup_comment)
            {
                log(_("Adding new comment to bug %d"), bz->bi_id);
                rhbz_add_comment(client, bz->bi_id, full_desc->buf, 0);
            }
            else
            {
                log(_("Found the same comment in the bug history, not adding a new one"));
            }
            strbuf_free(full_desc);

            unsigned rating = 0;
            /* python doesn't have rating file */
            if (rating_str)
                rating = xatou(rating_str);
            if (!dup_comment && (rating > bz->bi_best_bt_rating))
            {
                char bug_id_str[sizeof(int)*3 + 2];
                sprintf(bug_id_str, "%i", bz->bi_id);

                const char *bt =  get_problem_item_content_or_NULL(problem_data,
                                                                   FILENAME_BACKTRACE);
                log(_("Attaching better backtrace"));
                rhbz_attach_blob(client, FILENAME_BACKTRACE, bug_id_str, bt, strlen(bt),
                                 RHBZ_NOMAIL_NOTIFY);
            }
        }
    }

 log_out:
    log(_("Logging out"));
    rhbz_logout(client);

    log(_("Status: %s%s%s %s/show_bug.cgi?id=%u"),
                bz->bi_status,
                bz->bi_resolution ? " " : "",
                bz->bi_resolution ? bz->bi_resolution : "",
                rhbz.b_bugzilla_url,
                bz->bi_id);

    dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (dd)
    {
        char *msg = xasprintf("Bugzilla: URL=%s/show_bug.cgi?id=%u", rhbz.b_bugzilla_url, bz->bi_id);
        add_reported_to(dd, msg);
        free(msg);
        dd_close(dd);
    }

#if 0  /* enable if you search for leaks (valgrind etc) */
    free(product);
    free_problem_data(problem_data);
    free_bug_info(bz);
    abrt_xmlrpc_free_client(client);
    free_map_string(settings);
#endif
    return 0;
}
