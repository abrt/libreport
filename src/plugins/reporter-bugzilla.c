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
#include "problem_report.h"
#include "client.h"
#include "abrt_xmlrpc.h"
#include "rhbz.h"

/* BZ attachments */

#define DEFAULT_BUGZILLA_PRODUCT "Fedora"

static
int attach_text_item(struct abrt_xmlrpc *ax, const char *bug_id,
                const char *item_name, struct problem_item *item)
{
    if (!(item->flags & CD_FLAG_TXT))
        return 0;
    log_debug("attaching '%s' as text", item_name);
    int r = rhbz_attach_blob(ax, bug_id,
                item_name, item->content, strlen(item->content),
                RHBZ_NOMAIL_NOTIFY
    );
    return (r == 0);
}

static
int attach_file_item(struct abrt_xmlrpc *ax, const char *bug_id,
                const char *item_name, struct problem_item *item)
{
    if (!(item->flags & CD_FLAG_BIN))
        return 0;

    char *filename = item->content;
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        perror_msg("Can't open '%s'", filename);
        return 0;
    }
    errno = 0;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
    {
        perror_msg("'%s': not a regular file", filename);
        close(fd);
        return 0;
    }
    log_debug("attaching '%s' as file", item_name);
    int flag = RHBZ_NOMAIL_NOTIFY;
    if (!(item->flags & CD_FLAG_BIGTXT))
        flag |= RHBZ_BINARY_ATTACHMENT;
    int r = rhbz_attach_fd(ax, bug_id, item_name, fd, flag);
    close(fd);
    return (r == 0);
}

/* Main */

struct bugzilla_struct {
    char *b_login;
    char *b_password;
    const char *b_bugzilla_url;
    const char *b_bugzilla_xmlrpc;
    char *b_product;
    char *b_product_version;
    const char *b_DontMatchComponents;
    int         b_ssl_verify;
    int         b_create_private;
    GList       *b_private_groups;
};

static void set_default_settings(map_string_t *osinfo, map_string_t *settings)
{
    char *default_BugzillaURL;
    parse_osinfo_for_bug_url(osinfo, &default_BugzillaURL);
    /* if BugzillaURL is defined in conf_file or env , it will replace this value */
    set_map_string_item_from_string(settings, "BugzillaURL", default_BugzillaURL);
    log_debug("Loaded BUG_REPORT_URL '%s' from os-release", default_BugzillaURL);
    free(default_BugzillaURL);

    char *default_Product;
    char *default_ProductVersion;
    parse_osinfo_for_bz(osinfo, &default_Product, &default_ProductVersion);
    /* if Product or ProductVersion is defined in conf_file or env , it will replace this value */
    set_map_string_item_from_string(settings, "Product", default_Product);
    set_map_string_item_from_string(settings, "ProductVersion", default_ProductVersion);
    log_debug("Loaded Product '%s' from os-release", default_Product);
    log_debug("Loaded ProductVersion '%s' from os-release", default_ProductVersion);
    free(default_Product);
    free(default_ProductVersion);
}

static void set_settings(struct bugzilla_struct *b, map_string_t *settings)
{
    const char *environ;

    environ = getenv("Bugzilla_Login");
    b->b_login = xstrdup(environ ? environ : get_map_string_item_or_empty(settings, "Login"));

    environ = getenv("Bugzilla_Password");
    b->b_password = xstrdup(environ ? environ : get_map_string_item_or_empty(settings, "Password"));

    environ = getenv("Bugzilla_BugzillaURL");
    b->b_bugzilla_url = xstrdup(environ ? environ : get_map_string_item_or_empty(settings, "BugzillaURL"));
    if (!b->b_bugzilla_url[0])
        b->b_bugzilla_url = "https://bugzilla.redhat.com";
    else
    {
        /* We don't want trailing '/': "https://host/dir/" -> "https://host/dir" */
        char *last_slash = strrchr(b->b_bugzilla_url, '/');
        if (last_slash && last_slash[1] == '\0')
            *last_slash = '\0';
    }
    b->b_bugzilla_xmlrpc = concat_path_file(b->b_bugzilla_url, "xmlrpc.cgi");

    environ = getenv("Bugzilla_Product");
    if (environ)
    {
        b->b_product = xstrdup(environ);
        environ = getenv("Bugzilla_ProductVersion");
        if (environ)
            b->b_product_version = xstrdup(environ);
    }
    else
    {
        const char *option = get_map_string_item_or_NULL(settings, "Product");
        if (option)
            b->b_product = xstrdup(option);
        option = get_map_string_item_or_NULL(settings, "ProductVersion");
        if (option)
            b->b_product_version = xstrdup(option);
    }

    if (!b->b_product)
    {   /* Compat, remove it later (2014?). */
        environ = getenv("Bugzilla_OSRelease");
        if (environ)
            parse_release_for_bz(environ, &b->b_product, &b->b_product_version);
    }

    environ = getenv("Bugzilla_SSLVerify");
    b->b_ssl_verify = string_to_bool(environ ? environ : get_map_string_item_or_empty(settings, "SSLVerify"));

    environ = getenv("Bugzilla_DontMatchComponents");
    b->b_DontMatchComponents = environ ? environ : get_map_string_item_or_empty(settings, "DontMatchComponents");

    b->b_create_private = get_global_create_private_ticket();

    if (!b->b_create_private)
    {
        environ = getenv("Bugzilla_CreatePrivate");
        b->b_create_private = string_to_bool(environ ? environ : get_map_string_item_or_empty(settings, "Bugzilla_CreatePrivate"));
    }
    log_notice("create private bz ticket: '%s'", b->b_create_private ? "YES": "NO");

    environ = getenv("Bugzilla_PrivateGroups");
    GList *groups = parse_list(environ ? environ : get_map_string_item_or_empty(settings, "Bugzilla_PrivateGroups"));
    if (b->b_private_groups == NULL)
    {
        b->b_private_groups = groups;
        log_notice("groups: '%p'", b->b_private_groups);
    }
    else if (groups)
    {
        g_list_free_full(groups, free);
        error_msg(_("Warning, private ticket groups already specified as cmdline argument, ignoring the env variable and configuration"));
    }
}

static
char *ask_bz_login(const char *message)
{
    char *login = ask(message);
    if (login == NULL || login[0] == '\0')
    {
        set_xfunc_error_retval(EXIT_CANCEL_BY_USER);
        error_msg_and_die(_("Can't continue without login"));
    }

    return login;
}

static
char *ask_bz_password(const char *message)
{
    char *password = ask_password(message);
    if (password == NULL || password[0] == '\0')
    {
        set_xfunc_error_retval(EXIT_CANCEL_BY_USER);
        error_msg_and_die(_("Can't continue without password"));
    }

    return password;
}

static
void login(struct abrt_xmlrpc *client, struct bugzilla_struct *rhbz)
{
    log(_("Logging into Bugzilla at %s"), rhbz->b_bugzilla_url);
    while (!rhbz_login(client, rhbz->b_login, rhbz->b_password))
    {
        free(rhbz->b_login);
        rhbz->b_login = ask_bz_login(_("Invalid password or login. Please enter your Bugzilla login:"));

        free(rhbz->b_password);
        char *question = xasprintf(_("Invalid password or login. Please enter the password for '%s':"), rhbz->b_login);
        rhbz->b_password = ask_bz_password(question);
        free(question);
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

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "\n& [-vbf] [-g GROUP-NAME]... [-c CONFFILE]... [-F FMTFILE] [-A FMTFILE2] -d DIR"
        "\nor:"
        "\n& [-v] [-c CONFFILE]... [-d DIR] -t[ID] FILE..."
        "\nor:"
        "\n& [-v] [-c CONFFILE]... [-d DIR] -t[ID] -w"
        "\nor:"
        "\n& [-v] [-c CONFFILE]... -h DUPHASH [-p[PRODUCT]]"
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
        "\nIf not specified, CONFFILE defaults to "CONF_DIR"/plugins/bugzilla.conf"
        "\nIts lines should have 'PARAM = VALUE' format."
        "\nRecognized string parameters: BugzillaURL, Login, Password, OSRelease."
        "\nRecognized boolean parameter (VALUE should be 1/0, yes/no): SSLVerify."
        "\nParameters can be overridden via $Bugzilla_PARAM environment variables."
        "\n"
        "\nFMTFILE and FMTFILE2 default to "CONF_DIR"/plugins/bugzilla_format.conf"
    );
    enum {
        OPT_v = 1 << 0,
        OPT_d = 1 << 1,
        OPT_c = 1 << 2,
        OPT_F = 1 << 3,
        OPT_A = 1 << 4,
        OPT_t = 1 << 5,
        OPT_b = 1 << 6,
        OPT_f = 1 << 7,
        OPT_w = 1 << 8,
        OPT_h = 1 << 9,
        OPT_p = 1 << 10,
        OPT_r = 1 << 11,
        OPT_g = 1 << 12,
        OPT_D = 1 << 13,
    };
    const char *dump_dir_name = ".";
    GList *conf_file = NULL;
    const char *fmt_file = CONF_DIR"/plugins/bugzilla_format.conf";
    const char *fmt_file2 = fmt_file;
    char *abrt_hash = NULL;
    char *product = NULL;
    char *ticket_no = NULL;
    const char *tracker_str = "ABRT Server";
    char *debug_str = NULL;
    struct bugzilla_struct rhbz = { 0 };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_STRING(   'd', NULL, &dump_dir_name , "DIR"    , _("Problem directory")),
        OPT_LIST(     'c', NULL, &conf_file     , "FILE"   , _("Configuration file (may be given many times)")),
        OPT_STRING(   'F', NULL, &fmt_file      , "FILE"   , _("Formatting file for initial comment")),
        OPT_STRING(   'A', NULL, &fmt_file2     , "FILE"   , _("Formatting file for duplicates")),
        OPT_OPTSTRING('t', "ticket", &ticket_no , "ID"     , _("Attach FILEs [to bug with this ID]")),
        OPT_BOOL(     'b', NULL, NULL,                       _("When creating bug, attach binary files too")),
        OPT_BOOL(     'f', NULL, NULL,                       _("Force reporting even if this problem is already reported")),
        OPT_BOOL(     'w', NULL, NULL,                       _("Add bugzilla user to CC list [of bug with this ID]")),
        OPT_STRING(   'h', "duphash", &abrt_hash, "DUPHASH", _("Print BUG_ID which has given DUPHASH")),
        OPT_OPTSTRING('p', "product", &product  , "PRODUCT", _("Specify a Bugzilla product (ignored without -h)")),
        OPT_STRING(   'r', "tracker", &tracker_str, "TRACKER_NAME", _("A name of bug tracker for an additional URL from 'reported_to'")),
        OPT_LIST(     'g', "group", &rhbz.b_private_groups , "GROUP"  , _("Restrict access to this group only")),
        OPT_OPTSTRING('D', "debug", &debug_str  , "STR"    , _("Debug")),
        OPT_END()
    };
    unsigned opts = parse_opts(argc, argv, program_options, program_usage_string);
    argv += optind;

    export_abrt_envvars(0);

    map_string_t *settings = new_map_string();
    problem_data_t *problem_data;

    /* pull in some defaults from os-release */
    problem_data = create_problem_data_for_reporting(dump_dir_name);
    if (!problem_data)
        xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */
    else
    {
        map_string_t *osinfo = new_map_string();
        problem_data_get_osinfo(problem_data, osinfo);
        set_default_settings(osinfo, settings);
        free_map_string(osinfo);
    }

    {
        if (!conf_file)
            conf_file = g_list_append(conf_file, (char*) CONF_DIR"/plugins/bugzilla.conf");
        while (conf_file)
        {
            char *fn = (char *)conf_file->data;
            log_notice("Loading settings from '%s'", fn);
            load_conf_file(fn, settings, /*skip key w/o values:*/ false);
            log_debug("Loaded '%s'", fn);
            conf_file = g_list_delete_link(conf_file, conf_file);
        }
        set_settings(&rhbz, settings);
        /* WRONG! set_settings() does not copy the strings, it merely sets up pointers
         * to settings[] dictionary:
         */
        /*free_map_string(settings);*/
    }
    /* either we got Bugzilla_CreatePrivate from settings or -g was specified on cmdline */
    rhbz.b_create_private |= (opts & OPT_g);

    log_notice("Initializing XML-RPC library");
    xmlrpc_env env;
    xmlrpc_env_init(&env);
    xmlrpc_client_setup_global_const(&env);
    if (env.fault_occurred)
        abrt_xmlrpc_die(&env);
    xmlrpc_env_clean(&env);

    struct abrt_xmlrpc *client;
    client = abrt_xmlrpc_new_client(rhbz.b_bugzilla_xmlrpc, rhbz.b_ssl_verify);
    unsigned rhbz_ver = rhbz_version(client);

    if (abrt_hash)
    {
        log(_("Looking for similar problems in bugzilla"));
        char *hash;
        if (prefixcmp(abrt_hash, "abrt_hash:"))
            hash = xasprintf("abrt_hash:%s", abrt_hash);
        else
            hash = xstrdup(abrt_hash);

        if (opts & OPT_p)
        {
            /* If only -p without following string is presented, using
             * 'REDHAT_BUGZILLA_PRODUCT' value from /etc/os-release or value
             * from environment variable 'Bugzilla_Product' is used.
             */
            if (product == NULL && (product = getenv("Bugzilla_Product")) == NULL)
            {
                char *os_release = load_text_file("/etc/os-release",
                                DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE | DD_OPEN_FOLLOW);

                if (os_release != NULL)
                {
                    map_string_t *os_release_map = new_map_string();
                    parse_osinfo(os_release, os_release_map);

                    product = xstrdup(get_map_string_item_or_NULL(os_release_map, "REDHAT_BUGZILLA_PRODUCT"));

                    free_map_string(os_release_map);
                    free(os_release);

                    if (product == NULL)
                        error_msg(_("Failed to get 'REDHAT_BUGZILLA_PRODUCT' from '/etc/os-release'."));
                }
                else
                    error_msg(_("Failed to read '/etc/os-release' to get Bugzilla product."));
            }
        }

        if (product == NULL)
        {
            /* Use DEFAULT_BUGZILLA_PRODUCT as default product due to backward compatibility */
            product = xstrdup(DEFAULT_BUGZILLA_PRODUCT);

            /* If parameter -p was used and product == NULL, some error occured */
            if (opts & OPT_p)
                error_msg(_("Using default product '%s'"), product);
        }

        log_debug("Using Bugzilla product '%s' to find duplicate bug", product);
        xmlrpc_value *all_bugs = rhbz_search_duphash(client,
                                /*product:*/ product,
                                /*version:*/ NULL,
                                /*component:*/ NULL,
                                hash);
        free(hash);
        unsigned all_bugs_size = rhbz_array_size(all_bugs);
        if (all_bugs_size > 0)
        {
            int bug_id = rhbz_get_bug_id_from_array0(all_bugs, rhbz_ver);
            printf("%i\n", bug_id);
        }

        return EXIT_SUCCESS;
    }

    if (rhbz.b_login[0] == '\0')
    {
        free(rhbz.b_login);
        rhbz.b_login = ask_bz_login(_("Login is not provided by configuration. Please enter your Bugzilla login:"));
    }

    if (rhbz.b_password[0] == '\0')
    {
        free(rhbz.b_password);
        char *question = xasprintf(_("Password is not provided by configuration. Please enter the password for '%s':"), rhbz.b_login);
        rhbz.b_password = ask_bz_password(question);
        free(question);
    }

    if (opts & OPT_t)
    {
        if ((!argv[0] && !(opts & OPT_w)) || (argv[0] && (opts & OPT_w)))
            show_usage_and_die(program_usage_string, program_options);

        if (!ticket_no)
        {
            struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
            if (!dd)
                xfunc_die();
            report_result_t *reported_to = find_in_reported_to(dd, "Bugzilla");
            dd_close(dd);

            if (!reported_to || !reported_to->url)
                error_msg_and_die(_("Can't get Bugzilla ID because this problem has not yet been reported to Bugzilla."));

            char *url = reported_to->url;
            reported_to->url = NULL;
            free_report_result(reported_to);

            if (prefixcmp(url, rhbz.b_bugzilla_url) != 0)
                error_msg_and_die(_("This problem has been reported to Bugzilla '%s' which differs from the configured Bugzilla '%s'."), url, rhbz.b_bugzilla_url);

            ticket_no = strrchr(url, '=');
            if (!ticket_no)
                error_msg_and_die(_("Malformed url to Bugzilla '%s'."), url);

            /* won't ever call free on it - it simplifies the code a lot */
            ticket_no = xstrdup(ticket_no + 1);
            log(_("Using Bugzilla ID '%s'"), ticket_no);
        }

        login(client, &rhbz);

        if (opts & OPT_w)
            rhbz_mail_to_cc(client, xatoi_positive(ticket_no), rhbz.b_login, /* require mail notify */ 0);
        else
        {   /* Attach files to existing BZ */
            while (*argv)
            {
                const char *filename = *argv++;
                log_notice("Attaching file '%s' to bug %s", filename, ticket_no);

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

                rhbz_attach_fd(client, ticket_no, filename, fd, /*flags*/ 0);
                close(fd);
            }
        }

        log(_("Logging out"));
        rhbz_logout(client);

#if 0  /* enable if you search for leaks (valgrind etc) */
        abrt_xmlrpc_free_client(client);
#endif
        return 0;
    }

    /* Create new bug in Bugzilla */

    if (!(opts & OPT_f))
    {
        struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
        if (!dd)
            xfunc_die();
        report_result_t *reported_to = find_in_reported_to(dd, "Bugzilla");
        dd_close(dd);

        if (reported_to && reported_to->url)
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
    }

    const char *component = problem_data_get_content_or_die(problem_data, FILENAME_COMPONENT);
    const char *duphash   = problem_data_get_content_or_NULL(problem_data, FILENAME_DUPHASH);
//COMPAT, remove after 2.1 release
    if (!duphash) duphash = problem_data_get_content_or_die(problem_data, "global_uuid");

    if (!rhbz.b_product || !*rhbz.b_product) /* if not overridden or empty... */
    {
        free(rhbz.b_product);
        free(rhbz.b_product_version);
        map_string_t *osinfo = new_map_string();
        problem_data_get_osinfo(problem_data, osinfo);
        parse_osinfo_for_bz(osinfo, &rhbz.b_product, &rhbz.b_product_version);
        free_map_string(osinfo);

        if (!rhbz.b_product)
            error_msg_and_die(_("Can't determine Bugzilla Product from problem data."));
    }

    if (opts & OPT_D)
    {
        problem_formatter_t *pf = problem_formatter_new();

        if (problem_formatter_load_file(pf, fmt_file))
            error_msg_and_die("Invalid format file: %s", fmt_file);

        problem_report_t *pr = NULL;
        if (problem_formatter_generate_report(pf, problem_data, &pr))
            error_msg_and_die("Failed to format bug report from problem data");

        printf("summary: %s\n"
                "\n"
                "%s"
                "\n"
                , problem_report_get_summary(pr)
                , problem_report_get_description(pr)
        );

        puts("attachments:");
        for (GList *a = problem_report_get_attachments(pr); a != NULL; a = g_list_next(a))
            printf(" %s\n", (const char *)a->data);

        problem_report_free(pr);
        problem_formatter_free(pf);
        exit(0);
    }

    login(client, &rhbz);


    int bug_id = 0;

    /* If REMOTE_RESULT contains "DUPLICATE 12345", we consider it a dup of 12345
     * and won't search on bz server.
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

    struct bug_info *bz = NULL;
    if (!bug_id)
    {
        log(_("Checking for duplicates"));

        int existing_id = -1;
        int crossver_id = -1;
        {
            /* Figure out whether we want to match component
             * when doing dup search.
             */
            const char *component_substitute = is_in_comma_separated_list(component, rhbz.b_DontMatchComponents) ? NULL : component;

            /* We don't do dup detection across versions (see below why),
             * but we do add a note if cross-version potential dup exists.
             * For that, we search for cross version dups first:
             */
            xmlrpc_value *crossver_bugs = rhbz_search_duphash(client, rhbz.b_product, /*version:*/ NULL,
                            component_substitute, duphash);
            unsigned crossver_bugs_count = rhbz_array_size(crossver_bugs);
            log_debug("Bugzilla has %i reports with duphash '%s' including cross-version ones",
                    crossver_bugs_count, duphash);
            if (crossver_bugs_count > 0)
                crossver_id = rhbz_get_bug_id_from_array0(crossver_bugs, rhbz_ver);
            xmlrpc_DECREF(crossver_bugs);

            if (crossver_bugs_count > 0)
            {
                /* In dup detection we require match in product *and version*.
                 * Otherwise we sometimes have bugs in e.g. Fedora 17
                 * considered to be dups of Fedora 16 bugs.
                 * Imagine that F16 is "end-of-lifed" - allowing cross-version
                 * match will make all newly detected crashes DUPed
                 * to a bug in a dead release.
                 */
                xmlrpc_value *dup_bugs = rhbz_search_duphash(client, rhbz.b_product,
                                rhbz.b_product_version, component_substitute, duphash);
                unsigned dup_bugs_count = rhbz_array_size(dup_bugs);
                log_debug("Bugzilla has %i reports with duphash '%s'",
                        dup_bugs_count, duphash);
                if (dup_bugs_count > 0)
                    existing_id = rhbz_get_bug_id_from_array0(dup_bugs, rhbz_ver);
                xmlrpc_DECREF(dup_bugs);
            }
        }

        if (existing_id < 0 || rhbz.b_create_private)
        {
            problem_formatter_t *pf = problem_formatter_new();

            if (problem_formatter_load_file(pf, fmt_file))
                error_msg_and_die("Invalid format file: %s", fmt_file);

            problem_report_t *pr = NULL;
            if (problem_formatter_generate_report(pf, problem_data, &pr))
                error_msg_and_die("Failed to format problem data");

            if (existing_id >= 0)
            {
                char *msg = xasprintf(_(
                "You have requested to make your data accessible only to a "
                "specific group and this bug is a duplicate of bug: "
                "%s/%u"
                " "
                "In case of bug duplicates a new comment is added to the "
                "original bug report but access to the comments cannot be "
                "restricted to a specific group."
                " "
                "Would you like to open a new bug report and close it as "
                "DUPLICATE of the original one?"
                " "
                "Otherwise, the bug reporting procedure will be terminated."),
                rhbz.b_bugzilla_url, existing_id);

                int r = ask_yes_no(msg);
                free(msg);

                if (r == 0)
                {
                    log(_("Logging out"));
                    rhbz_logout(client);

                    problem_formatter_free(pf);
                    exit(EXIT_CANCEL_BY_USER);
                }

                problem_report_buffer_printf(
                        problem_report_get_buffer(pr, PR_SEC_DESCRIPTION),
                        "\nThis is a private, duplicate bug report of bug %u. "
                        "The report has been created because Bugzilla cannot "
                        "grant access to a comment for a specific group.\n",
                        existing_id);
            }

            /* Create new bug */
            log(_("Creating a new bug"));

            if (existing_id < 0 && crossver_id >= 0)
                problem_report_buffer_printf(
                        problem_report_get_buffer(pr, PR_SEC_DESCRIPTION),
                        "\nPotential duplicate: bug %u\n", crossver_id);

            problem_formatter_free(pf);

            int new_id = rhbz_new_bug(client,
                    problem_data, rhbz.b_product, rhbz.b_product_version,
                    problem_report_get_summary(pr),
                    problem_report_get_description(pr),
                    rhbz.b_create_private,
                    rhbz.b_private_groups
                    );

            if (new_id == -1)
            {
                error_msg_and_die(_("Failed to create a new bug."));
            }

            struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
            if (dd)
            {
                report_result_t *reported_to = find_in_reported_to(dd, tracker_str);
                char *extra = dd_load_text_ext(dd, "extra-cc",
				DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE |
				DD_FAIL_QUIETLY_ENOENT);

                dd_close(dd);

                if (extra != NULL) {
                    char *email = strtok(extra, "\n");
                    while (email != NULL) {
                        log(_("Adding extra cc %s to bug report"), email);
                        rhbz_mail_to_cc(client, new_id, email, /* require mail notify */ 0);
                        email = strtok(NULL, "\n");
                    }
                    free(extra);
                }

                if (reported_to && reported_to->url)
                {
                    log(_("Adding External URL to bug %i"), new_id);
                    rhbz_set_url(client, new_id, reported_to->url, RHBZ_NOMAIL_NOTIFY);
                    free_report_result(reported_to);
                }
            }

            log(_("Adding attachments to bug %i"), new_id);
            char new_id_str[sizeof(int)*3 + 2];
            sprintf(new_id_str, "%i", new_id);

            for (GList *a = problem_report_get_attachments(pr); a != NULL; a = g_list_next(a))
            {
                const char *item_name = (const char *)a->data;
                struct problem_item *item = problem_data_get_item_or_NULL(problem_data, item_name);
                if (!item)
                    continue;
                else if (item->flags & CD_FLAG_TXT)
                    attach_text_item(client, new_id_str, item_name, item);
                else if (item->flags & CD_FLAG_BIN)
                    attach_file_item(client, new_id_str, item_name, item);
            }

            bz = new_bug_info();
            bz->bi_status = xstrdup("NEW");
            bz->bi_id = new_id;

            if (existing_id >= 0)
            {
                log(_("Closing bug %i as duplicate of bug %i"), new_id, existing_id);
                rhbz_close_as_duplicate(client, new_id, existing_id, RHBZ_NOMAIL_NOTIFY);
            }

            goto log_out;
        }

        bug_id = existing_id;
    }

    bz = rhbz_bug_info(client, bug_id);

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

    /* We used to skip adding the comment to CLOSED bugs:
     *
     * if (strcmp(bz->bi_status, "CLOSED") != 0)
     * {
     *
     * But that condition has been added without a good explanation of the
     * reason for doing so:
     *
     * ABRT commit 1bf37ad93e87f065347fdb7224578d55cca8d384
     *
     * -    if (bug_id > 0)
     * +    if (strcmp(bz.bug_status, "CLOSED") != 0)
     *
     *
     * From my point of view, there is no good reason to not add the comment to
     * such a bug. The reporter spent several minutes waiting for the backtrace
     * and we don't want to make the reporters feel that they spent their time
     * in vain and I think that adding comments to already closed bugs doesn't
     * hurt the maintainers (at least not me).
     *
     * Plenty of new comments might convince the maintainer to reconsider the
     * bug's status.
     */

    /* Add user's login to CC if not there already */
    if (strcmp(bz->bi_reporter, rhbz.b_login) != 0
     && !g_list_find_custom(bz->bi_cc_list, rhbz.b_login, (GCompareFunc)g_strcmp0)
    ) {
        log(_("Adding %s to CC list"), rhbz.b_login);
        rhbz_mail_to_cc(client, bz->bi_id, rhbz.b_login, RHBZ_NOMAIL_NOTIFY);
    }

    /* Add comment and bt */
    const char *comment = problem_data_get_content_or_NULL(problem_data, FILENAME_COMMENT);
    if (comment && comment[0])
    {
        problem_formatter_t *pf = problem_formatter_new();
        if (problem_formatter_load_file(pf, fmt_file2))
            error_msg_and_die("Invalid duplicate format file: '%s", fmt_file2);

        problem_report_t *pr;
        if (problem_formatter_generate_report(pf, problem_data, &pr))
            error_msg_and_die("Failed to format duplicate comment from problem data");

        const char *bzcomment = problem_report_get_description(pr);

        int dup_comment = is_comment_dup(bz->bi_comments, bzcomment);
        if (!dup_comment)
        {
            log(_("Adding new comment to bug %d"), bz->bi_id);
            rhbz_add_comment(client, bz->bi_id, bzcomment, 0);

            const char *bt = problem_data_get_content_or_NULL(problem_data, FILENAME_BACKTRACE);
            unsigned rating = 0;
            const char *rating_str = problem_data_get_content_or_NULL(problem_data, FILENAME_RATING);
            /* python doesn't have rating file */
            if (rating_str)
                rating = xatou(rating_str);
            if (bt && rating > bz->bi_best_bt_rating)
            {
                char bug_id_str[sizeof(int)*3 + 2];
                sprintf(bug_id_str, "%i", bz->bi_id);
                log(_("Attaching better backtrace"));
                rhbz_attach_blob(client, bug_id_str, FILENAME_BACKTRACE, bt, strlen(bt),
                                 RHBZ_NOMAIL_NOTIFY);
            }
        }
        else
            log(_("Found the same comment in the bug history, not adding a new one"));

        problem_report_free(pr);
        problem_formatter_free(pf);
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

    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (dd)
    {
        report_result_t rr = { .label = (char *)"Bugzilla" };
        rr.url = xasprintf("%s/show_bug.cgi?id=%u", rhbz.b_bugzilla_url, bz->bi_id);
        add_reported_to_entry(dd, &rr);
        free(rr.url);
        dd_close(dd);
    }

#if 0  /* enable if you search for leaks (valgrind etc) */
    free(rhbz.b_product);
    free(rhbz.b_product_version);
    problem_data_free(problem_data);
    free_bug_info(bz);
    abrt_xmlrpc_free_client(client);
#endif
    return 0;
}
