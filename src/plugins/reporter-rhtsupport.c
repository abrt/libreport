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
#include <libtar.h>
#include "ureport.h"
#include "internal_libreport.h"
#include "client.h"
#include "libreport_curl.h"
#include "abrt_rh_support.h"
#include "reporter-rhtsupport.h"
#include "problem_report.h"

/* problem report format template */
#define PROBLEM_REPORT_TEMPLATE \
    "%summary:: [abrt] [[%pkg_name%]][[: %crash_function%()]][[: %reason%]][[: TAINTED %tainted_short%]]\n" \
    "\n" \
    "Description of problem:: %bare_comment\n" \
    "\n" \
    "Additional info::" \
    "    count,reason,package,pkg_vendor,cmdline,executable,%reporter\n" \
    "\n" \
    "How reproducible:: %bare_reproducible\n" \
    "\n" \
    "Steps to reproduce:: %bare_reproducer\n" \
    "\n" \
    "Truncated backtrace:: %bare_%short_backtrace\n" \
    "\n" \
    "Other report identifiers:: %bare_reported_to\n"

#define ABRT_ELEMENTS_KB_ARTICLE "https://access.redhat.com/articles/2134281"

#define RHTSUPPORT_CASE_URL_PATH "cases"

#define QUERY_HINTS_IF_SMALLER_THAN  (8*1024*1024)

static void ask_rh_credentials(char **login, char **password);

#define INVALID_CREDENTIALS_LOOP(l, p, r, fncall) \
    do {\
        r = fncall;\
        if (r->error == 0 || r->http_resp_code != 401 ) { break; }\
        ask_rh_credentials(&l, &p);\
        free_rhts_result(r);\
    } while (1)

#define STRCPY_IF_NOT_EQUAL(dest, src) \
    do { if (strcmp(dest, src) != 0 ) { \
        free(dest); \
        dest = g_strdup(src); \
    } } while (0)

static report_result_t *get_reported_to(const char *dump_dir_name)
{
    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        libreport_xfunc_die();
    report_result_t *reported_to = libreport_find_in_reported_to(dd, "RHTSupport");
    dd_close(dd);
    return reported_to;
}

static
int create_tarball(const char *tempfile, struct dump_dir *dd,
     problem_data_t *problem_data)
{
    reportfile_t *file = NULL;
    int retval = 0; /* everything is ok so far .. */

    int pipe_from_parent_to_child[2];
    libreport_xpipe(pipe_from_parent_to_child);
    pid_t child = fork();
    if (child == 0)
    {
        /* child */
        close(pipe_from_parent_to_child[1]);
        libreport_xmove_fd(libreport_xopen3(tempfile, O_WRONLY | O_CREAT | O_EXCL, 0600), 1);
        libreport_xmove_fd(pipe_from_parent_to_child[0], 0);
        execlp("gzip", "gzip", NULL);
        perror_msg_and_die("Can't execute '%s'", "gzip");
    }
    close(pipe_from_parent_to_child[0]);

    TAR *tar = NULL;
    if (tar_fdopen(&tar, pipe_from_parent_to_child[1], (char*)tempfile,
                /*fileops:(standard)*/ NULL, O_WRONLY | O_CREAT, 0644, TAR_GNU) != 0)
    {
        goto ret_fail;
    }

    file = new_reportfile();
    {
        GHashTableIter iter;
        char *name;
        struct problem_item *value;
        g_hash_table_iter_init(&iter, problem_data);
        while (g_hash_table_iter_next(&iter, (void**)&name, (void**)&value))
        {
            const char *content = value->content;
            if (value->flags & CD_FLAG_TXT)
            {
                reportfile_add_binding_from_string(file, name, content);
            }
            else if (value->flags & CD_FLAG_BIN)
            {
                const char *basename = strrchr(content, '/');
                if (basename)
                    basename++;
                else
                    basename = content;
                char *xml_name = g_build_filename("content", basename, NULL);
                reportfile_add_binding_from_namedfile(file,
                        /*on_disk_filename */ content,
                        /*binding_name     */ name,
                        /*recorded_filename*/ xml_name,
                        /*binary           */ !(value->flags & CD_FLAG_BIGTXT)
                );
                free(xml_name);
            }
        }
    }

    /* append all files from dump dir */
    dd_init_next_file(dd);
    char *short_name, *full_name;
    while (dd_get_next_file(dd, &short_name, &full_name))
    {
        char *uploaded_name = g_build_filename("content", short_name, NULL);
        free(short_name);

        if (tar_append_file(tar, full_name, uploaded_name) != 0)
        {
            free(full_name);
            goto ret_fail;
        }

        free(full_name);
    }

    const char *signature = reportfile_as_string(file);
    /*
     * Note: this pointer points to string which is owned by
     * "file" object, can't free "file" just yet.
     */

    /* Write out content.xml in the tarball's root */
    {
        unsigned len = strlen(signature);
        unsigned len512 = (len + 511) & ~511;
        char *block = (char*)memcpy(g_malloc0(len512), signature, len);

        th_set_type(tar, S_IFREG | 0644);
        th_set_mode(tar, S_IFREG | 0644);
      //th_set_link(tar, char *linkname);
      //th_set_device(tar, dev_t device);
      //th_set_user(tar, uid_t uid);
      //th_set_group(tar, gid_t gid);
        th_set_mtime(tar, time(NULL));
        th_set_path(tar, (char*)"content.xml");
        th_set_size(tar, len);
        th_finish(tar); /* caclulate and store th xsum etc */

        if (th_write(tar) != 0 /* writes header block */
            /* writes content.xml, padded to 512 bytes */
         || libreport_full_write(tar_fd(tar), block, len512) != len512
         || tar_append_eof(tar) != 0 /* writes EOF blocks */
         || tar_close(tar) != 0
        ) {
            free(block);
            goto ret_fail;
        }
        tar = NULL;
        free(block);
    }

    /* We must be sure gzip finished, and finished successfully */
    int status;
    libreport_safe_waitpid(child, &status, 0);
    child = -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        /* Hopefully, by this time child emitted more meaningful
         * error message. But just in case it didn't:
         */
        goto ret_fail;
    }
    goto ret_clean; /* success */

ret_fail:
    retval = 1; /* failure */
    /* We must close write fd first, or else child will wait forever */
    if (tar)
        tar_close(tar);
    //close(pipe_from_parent_to_child[1]); - tar_close() does it itself

    /* Now wait for child to exit */
    if (child > 0)
    {
        // Damn, selinux does not allow SIGKILLing our own child! wtf??
        //kill(child, SIGKILL); /* just in case */
        libreport_safe_waitpid(child, NULL, 0);
    }

ret_clean:
    dd_close(dd);
    /* now it's safe to free file */
    free_reportfile(file);
    return retval;
}

static
struct ureport_server_response *ureport_do_post_credentials(const char *json, struct ureport_server_config *config, const char *action)
{
    struct post_state *post_state = NULL;
    while (1)
    {
        post_state = libreport_ureport_do_post(json, config, action);

        if (post_state == NULL)
        {
            error_msg(_("Failed on submitting the problem"));
            return NULL;
        }

        if (post_state->http_resp_code != 401)
            break;

        free_post_state(post_state);

        char *login = NULL;
        char *password = NULL;
        ask_rh_credentials(&login, &password);
        libreport_ureport_server_config_set_basic_auth(config, login, password);
        free(password);
        free(login);
    }

    struct ureport_server_response *resp = libreport_ureport_server_response_from_reply(post_state, config);
    free(post_state);
    return resp;
}

static
char *submit_ureport(const char *dump_dir_name, struct ureport_server_config *conf)
{
    struct dump_dir *dd = dd_opendir(dump_dir_name, DD_OPEN_READONLY);
    g_autoptr(report_result_t) rr_bthash = NULL;

    if (dd == NULL)
        return NULL;

    rr_bthash = libreport_find_in_reported_to(dd, "uReport");

    dd_close(dd);

    if (rr_bthash != NULL)
    {
        log_notice("uReport has already been submitted.");

        return report_result_get_bthash(rr_bthash);
    }

    char *json = libreport_ureport_from_dump_dir(dump_dir_name);
    if (json == NULL)
    {
        log_notice(_("Failed to generate microreport from the problem data"));
        return NULL;
    }

    struct ureport_server_response *resp = ureport_do_post_credentials(json, conf, UREPORT_SUBMIT_ACTION);
    free(json);
    if (resp == NULL)
        return NULL;

    char *bthash = NULL;
    if (!resp->urr_is_error)
    {
        if (resp->urr_bthash != NULL)
            bthash = g_strdup(resp->urr_bthash);

        libreport_ureport_server_response_save_in_dump_dir(resp, dump_dir_name, conf);

        if (resp->urr_message)
            log_warning("%s", resp->urr_message);
    }
    else if (libreport_g_verbose > 2)
        error_msg(_("Server responded with an error: '%s'"), resp->urr_value);

    libreport_ureport_server_response_free(resp);
    return bthash;
}

static
void attach_to_ureport(struct ureport_server_config *conf,
        const char *bthash, const char *attach_id, const char *data)
{
    char *json = ureport_json_attachment_new(bthash, attach_id, data);
    struct ureport_server_response *resp = ureport_do_post_credentials(json, conf, UREPORT_ATTACH_ACTION);
    libreport_ureport_server_response_free(resp);
    free(json);
}

static
bool check_for_hints(const char *url, char **login, char **password, bool ssl_verify, const char *tempfile)
{
    bool retval = false;
    rhts_result_t *result = NULL;

    INVALID_CREDENTIALS_LOOP((*login), (*password),
            result, get_rhts_hints(url, *login, *password, ssl_verify, tempfile)
    );

    if (result->error)
    {
        /* We don't use result->msg here because it looks like this:
         *  Error in file upload at 'URL', HTTP code: 404,
         *  server says: '<?xml...?><error...><code>404</code><message>...</message></error>'
         * TODO: make server send bare textual msgs, not XML.
         */
        error_msg("Error in file upload at '%s', HTTP code: %d", url, result->http_resp_code);
    }
    else if (result->body)
    {
        /* The message might contain URLs to known solutions and such */
        char *hint = parse_response_from_RHTS_hint_xml2txt(result->body);
        if (hint)
        {
            hint = libreport_append_to_malloced_string(hint, " ");
            hint = libreport_append_to_malloced_string(hint,
                    _("Do you still want to create a RHTSupport ticket?")
                    );

            /*
             * 'Yes' to the create ticket question means no hints were found.
             */
            retval = !libreport_ask_yes_no(hint);

            free(hint);
        }
    }

    free_rhts_result(result);
    return retval;
}

static
char *ask_rh_login(const char *message)
{
    char *login = libreport_ask(message);
    if (login == NULL || login[0] == '\0')
    {
        libreport_set_xfunc_error_retval(EXIT_CANCEL_BY_USER);
        error_msg_and_die(_("Can't continue without login"));
    }

    return login;
}

static
char *ask_rh_password(const char *message)
{
    char *password = libreport_ask_password(message);
    if (password == NULL || password[0] == '\0')
    {
        libreport_set_xfunc_error_retval(EXIT_CANCEL_BY_USER);
        error_msg_and_die(_("Can't continue without password"));
    }

    return password;
}

static
void ask_rh_credentials(char **login, char **password)
{
    free(*login);
    free(*password);

    *login = ask_rh_login(_("Invalid password or login. Please enter your Red Hat login:"));

    g_autofree char *question = g_strdup_printf(_("Invalid password or login. Please enter the password for '%s':"), *login);
    *password = ask_rh_password(question);
}

static
char *get_param_string(const char *name, map_string_t *settings, const char *dflt)
{
    g_autofree char *envname = g_strdup_printf("RHTSupport_%s", name);
    const char *envvar = getenv(envname);
    return g_strdup(envvar ? envvar : (libreport_get_map_string_item_or_NULL(settings, name) ? : dflt));
}

static
void prepare_ureport_configuration(const char *urcfile,
        map_string_t *settings, struct ureport_server_config *urconf,
        const char *portal_url, const char *login, const char *password, bool ssl_verify)
{
    libreport_load_conf_file(urcfile, settings, false);
    libreport_ureport_server_config_init(urconf);

    /* The following lines cause that we always use URL from ureport's
     * configuration becuase the GUI reporter always exports uReport_URL env
     * var.
     *
     *   char *url = NULL;
     *   UREPORT_OPTION_VALUE_FROM_CONF(settings, "URL", url, g_strdup);
     *   if (url != NULL)
     *       libreport_ureport_server_config_set_url(urconf, url);
     */

    libreport_ureport_server_config_set_url(urconf, g_build_filename(portal_url ? portal_url : "", "/telemetry/abrt", NULL));
    urconf->ur_ssl_verify = ssl_verify;

    libreport_ureport_server_config_set_basic_auth(urconf, login, password);

    bool include_auth = true;
    UREPORT_OPTION_VALUE_FROM_CONF(settings, "IncludeAuthData", include_auth, libreport_string_to_bool);

    if (include_auth)
    {
        const char *auth_items = NULL;
        UREPORT_OPTION_VALUE_FROM_CONF(settings, "AuthDataItems", auth_items, (const char *));
        if (NULL != auth_items)
        {
            urconf->ur_prefs.urp_auth_items = libreport_parse_delimited_list(auth_items, ",");
        }
    }

    urconf->ur_prefs.urp_flags |= UREPORT_PREF_FLAG_RETURN_ON_FAILURE;
}

static char *create_case_url(char *url, const char *case_no)
{
    char *url1 = g_build_filename(url ? url : "", RHTSUPPORT_CASE_URL_PATH, NULL);
    free(url);
    url = g_build_filename(url1 ? url1 : "", case_no, NULL);
    free(url1);

    return url;
}

static char *ask_case_no_create_url(char *url)
{
    g_autofree char *msg = g_strdup_printf(_("Please enter customer case number to which you want to attach the data:"));
    char *case_no = libreport_ask(msg);
    if (case_no == NULL || case_no[0] == '\0')
    {
        libreport_set_xfunc_error_retval(EXIT_CANCEL_BY_USER);
        error_msg_and_die(_("Can't continue without Red Hat Support case number"));
    }
    char *new_url = create_case_url(url, (const char *)case_no);
    free(case_no);

    return new_url;
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
    const char *case_no = NULL;
    GList *conf_file = NULL;
    const char *urconf_file = UREPORT_CONF_FILE_PATH;
    const char *fmt_file = NULL;

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "\n"
        "& [-v] [-c CONFFILE] [-F FMTFILE] -d DIR\n"
        "or:\n"
        "& [-v] [-c CONFFILE] [-d DIR] -t[ID] [-u -C UR_CONFFILE] FILE...\n"
        "\n"
        "Reports a problem to RHTSupport.\n"
        "\n"
        "If not specified, CONFFILE defaults to "CONF_DIR"/plugins/rhtsupport.conf\n"
        "and user's local ~"USER_HOME_CONFIG_PATH"/rhtsupport.conf.\n"
        "Its lines should have 'PARAM = VALUE' format.\n"
        "Recognized string parameters: URL, Login, Password, BigFileURL.\n"
        "Recognized numeric parameter: BigSizeMB.\n"
        "Recognized boolean parameter (VALUE should be 1/0, yes/no): SSLVerify.\n"
        "User's local configuration overrides the system wide configuration.\n"
        "Parameters can be overridden via $RHTSupport_PARAM environment variables.\n"
        "\n"
        "Option -t uploads FILEs to the already created case on RHTSupport site.\n"
        "The case ID is retrieved from directory specified by -d DIR.\n"
        "If problem data in DIR was never reported to Red Hat Support, you will be asked\n"
        "to enter case ID to which you want to upload the FILEs.\n"
        "\n"
        "Option -tCASE uploads FILEs to the case CASE on RHTSupport site.\n"
        "-d DIR is ignored.\n"
        "\n"
        "Option -u sends ABRT crash statistics data (uReport) before creating a new case.\n"
        "uReport configuration is loaded from UR_CONFFILE which defaults to\n"
        UREPORT_CONF_FILE_PATH".\n"
    );
    enum {
        OPT_v = 1 << 0,
        OPT_d = 1 << 1,
        OPT_c = 1 << 2,
        OPT_t = 1 << 3,
        OPT_f = 1 << 4,
        OPT_u = 1 << 5,
        OPT_C = 1 << 6,
        OPT_F = 1 << 7,
        OPT_D = 1 << 8,
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&libreport_g_verbose),
        OPT_STRING(   'd', NULL, &dump_dir_name, "DIR" , _("Problem directory")),
        OPT_LIST(     'c', NULL, &conf_file    , "FILE", _("Configuration file (may be given many times)")),
        OPT_OPTSTRING('t', NULL, &case_no      , "ID"  , _("Upload FILEs [to case with this ID]")),
        OPT_BOOL(     'f', NULL, NULL          ,         _("Force reporting even if this problem is already reported")),
        OPT_BOOL(     'u', NULL, NULL          ,         _("Submit uReport before creating a new case")),
        OPT_STRING(   'C', NULL, &urconf_file  , "FILE", _("Configuration file for uReport")),
        OPT_STRING(   'F', NULL, &fmt_file     , "FILE", _("Formatting file for a new case")),
        OPT_BOOL(     'D', NULL, NULL          ,         _("Debug")),
        OPT_END()
    };
    unsigned opts = libreport_parse_opts(argc, argv, program_options, program_usage_string);
    argv += optind;

    libreport_export_abrt_envvars(0);

    /* Parse config, extract necessary params */
    map_string_t *settings = libreport_new_map_string();
    g_autofree char *local_conf = NULL;
    if (!conf_file)
    {
        conf_file = g_list_append(conf_file, (char*) CONF_DIR"/plugins/rhtsupport.conf");
        local_conf = g_strdup_printf("%s"USER_HOME_CONFIG_PATH"/rhtsupport.conf", getenv("HOME"));
        conf_file = g_list_append(conf_file, local_conf);

    }
    while (conf_file)
    {
        const char *fn = (char *)conf_file->data;
        log_notice("Loading settings from '%s'", fn);
        libreport_load_conf_file(fn, settings, /*skip key w/o values:*/ false);
        log_debug("Loaded '%s'", fn);
        conf_file = g_list_remove(conf_file, fn);
    }

    char *url      = get_param_string("URL"       , settings, "https://api.access.redhat.com/rs");
    char *login    = get_param_string("Login"     , settings, "");
    char *password = get_param_string("Password"  , settings, "");
    char *bigurl   = get_param_string("BigFileURL", settings, "ftp://dropbox.redhat.com/incoming/");

    if (login[0] == '\0')
    {
        free(login);
        login = ask_rh_login(_("Login is not provided by configuration. Please enter your RHTS login:"));
    }

    if (password[0] == '\0')
    {
        free(password);
        g_autofree char *question = g_strdup_printf(_("Password is not provided by configuration. Please enter the password for '%s':"), login);
        password = ask_rh_password(question);
    }

    char* envvar;
    envvar = getenv("RHTSupport_SSLVerify");
    bool ssl_verify = libreport_string_to_bool(
                envvar ? envvar : (libreport_get_map_string_item_or_NULL(settings, "SSLVerify") ? : "1")
    );
    envvar = getenv("RHTSupport_BigSizeMB");
    if (!envvar)
        envvar = g_hash_table_lookup(settings, "BigSizeMB");
    unsigned bigsize = 0;
    char *endptr;
    unsigned long bigsz = g_ascii_strtoull(
                /* RH has a 250m limit for web attachments (as of 2013) */
                envvar ? envvar : "200",
                &endptr,
                10
    );
    if (bigsz >= 0 && bigsz <= UINT_MAX && endptr != envvar)
        bigsize = (unsigned)bigsz;
    else
        error_msg_and_die("expected number in range <0, %d>: '%s'", UINT_MAX, envvar);
    envvar = getenv("RHTSupport_SubmitUReport");
    bool submit_ur = libreport_string_to_bool(
                envvar ? envvar :
                    (libreport_get_map_string_item_or_NULL(settings, "SubmitUReport") ? :
                        ((opts & OPT_u) ? "1" : "0"))
    );
    libreport_free_map_string(settings);

    g_autofree char *base_api_url = g_strdup(url);
    char *bthash = NULL;

    map_string_t *ursettings = libreport_new_map_string();
    struct ureport_server_config urconf;

    prepare_ureport_configuration(urconf_file, ursettings, &urconf,
            url, login, password, ssl_verify);

    if (opts & OPT_t)
    {
        if (!case_no)
        {
            /* -t: extract URL where we previously reported it */
            g_autoptr(report_result_t) reported_to = NULL;
            char *report_url = NULL;

            reported_to = get_reported_to(dump_dir_name);
            if (NULL != reported_to)
            {
                report_url = report_result_get_url(reported_to);
            }
            if (NULL != report_url)
            {
                free(url);
                url = report_url;
                g_autofree char *msg = g_strdup_printf(
                    _("We found a similar Red Hat support case %s. "
                      "Do you want to attach the data to the case? "
                      "Otherwise, you will have to enter the existing "
                      "Red Hat support case number."), url);
                int yes = libreport_ask_yes_no(msg);
                if (!yes)
                {
                    url = ask_case_no_create_url(url);
                }
            }
            else
            {
                log_warning("Problem was not reported to Red Hat Support.");

                url = ask_case_no_create_url(url);
            }
        }
        else
        {
            /* -tCASE */
            url = create_case_url(url, case_no);
        }

        if (*argv)
        {
            /* -t[CASE] FILE: just attach files and exit */
            while (*argv)
            {
                log_warning(_("Attaching '%s' to case '%s'"), *argv, url);
                rhts_result_t *result = attach_file_to_case(url,
                    login,
                    password,
                    ssl_verify,
                    *argv
                );
                if (result->error)
                    error_msg_and_die("%s", result->msg);
                log_warning("Attachment URL:%s", result->url);
                log_warning("File attached successfully");
                free_rhts_result(result);
                argv++;
            }
            return 0;
        }
    }
    else /* no -t: creating a new case */
    {
        g_autoptr(report_result_t) reported_to = NULL;
        char *report_url = NULL;

        if (*argv)
            libreport_show_usage_and_die(program_usage_string, program_options);

        reported_to = get_reported_to(dump_dir_name);
        if (NULL != reported_to)
        {
            report_url = report_result_get_url(reported_to);
        }
        if (NULL != report_url && !(opts & OPT_f))
        {
            g_autofree char *msg = g_strdup_printf("This problem was already reported to RHTS (see '%s')."
                            " Do you still want to create a RHTSupport ticket?",
                            report_url);
            int yes = libreport_ask_yes_no(msg);
            g_free(report_url);
            if (!yes)
                return 0;
        }

        if (submit_ur)
        {
            log_warning(_("Sending ABRT crash statistics data"));

            bthash = submit_ureport(dump_dir_name, &urconf);

            /* Ensure that we will use the updated credentials */
            STRCPY_IF_NOT_EQUAL(login, urconf.ur_username);
            STRCPY_IF_NOT_EQUAL(password, urconf.ur_password);
        }
    }

    problem_data_t *problem_data = create_problem_data_for_reporting(dump_dir_name);
    if (!problem_data)
        libreport_xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    const char *errmsg = NULL;

    char tmpdir_name[sizeof(LARGE_DATA_TMP_DIR"/rhtsupport-"LIBREPORT_ISO_DATE_STRING_SAMPLE"-XXXXXX")];
    snprintf(tmpdir_name, sizeof(tmpdir_name), LARGE_DATA_TMP_DIR"/rhtsupport-%s-XXXXXX", libreport_iso_date_string(NULL));
    /* mkdtemp does mkdir(xxx, 0700), should be safe (is it?) */
    if (mkdtemp(tmpdir_name) == NULL)
    {
        error_msg_and_die(_("Can't create a temporary directory in "LARGE_DATA_TMP_DIR));
    }
    /* Starting from here, we must perform cleanup on errors
     * (delete temp dir)
     */
    char *tempfile = NULL;
    tempfile = libreport_concat_path_basename(tmpdir_name, dump_dir_name);
    tempfile = libreport_append_to_malloced_string(tempfile, ".tar.gz");

    rhts_result_t *result = NULL;
    rhts_result_t *result_atch = NULL;
    const char *package;
    package  = problem_data_get_content_or_NULL(problem_data, FILENAME_PACKAGE);

    const char *dsc = NULL;
    const char *summary = NULL;

    const char *count = NULL;
    count = problem_data_get_content_or_NULL(problem_data, FILENAME_COUNT);
    if (count != NULL
        && strcmp(count, "1") == 0
        /* the 'count' file can lie */
        && get_problem_data_reproducible(problem_data) <= PROBLEM_REPRODUCIBLE_UNKNOWN)
    {
        int r = libreport_ask_yes_no(
            _("The problem has only occurred once and the ability to reproduce "
              "the problem is unknown. Please ensure you will be able to "
              "provide detailed information to our Support Team. "
              "Would you like to continue and open a new support case?"));
        if (!r)
            exit(EXIT_CANCEL_BY_USER);
    }

    const char *vendor = NULL;
    vendor = problem_data_get_content_or_NULL(problem_data, FILENAME_PKG_VENDOR);
    if (package && vendor && strcmp(vendor, "Red Hat, Inc.") != 0)
    {
        g_autofree char *message = g_strdup_printf(
            _("The crashed program was released by '%s'. "
              "Would you like to report the problem to Red Hat Support?"),
              vendor);
        int r = libreport_ask_yes_no(message);
        if (!r)
            exit(EXIT_CANCEL_BY_USER);
    }

    /* In the case there is no pkg_vendor file use "unknown vendor"  */
    if (!vendor)
        problem_data_add_text_noteditable(problem_data, FILENAME_PKG_VENDOR, "unknown vendor");

    const char *executable = NULL;
    executable  = problem_data_get_content_or_NULL(problem_data, FILENAME_EXECUTABLE);
    if (!package)
    {
        g_autofree char *message = g_strdup_printf(
            _("The program '%s' does not appear to be provided by Red Hat. "
              "Would you like to report the problem to Red Hat Support?"),
              executable);
        int r = libreport_ask_yes_no(message);
        if (!r)
            exit(EXIT_CANCEL_BY_USER);

        problem_data_add_text_noteditable(problem_data, FILENAME_PACKAGE,
                                         "not belong to any package");
    }

    problem_formatter_t *pf = problem_formatter_new();

    /* formatting conf file was set */
    if (fmt_file)
    {
        if (problem_formatter_load_file(pf, fmt_file))
            error_msg_and_die("Invalid format file: %s", fmt_file);
    }
    /* using formatting template */
    else
    {
        if (problem_formatter_load_string(pf, PROBLEM_REPORT_TEMPLATE))
            error_msg_and_die("Invalid problem report format string");
    }

    problem_report_t *pr = NULL;
    if (problem_formatter_generate_report(pf, problem_data, &pr))
        error_msg_and_die("Failed to format bug report from problem data");

    /* Add information about attachments into the description */
    problem_report_buffer *dsc_buffer = problem_report_get_buffer(pr, PR_SEC_DESCRIPTION);

    char *tarball_name = basename(tempfile);
    problem_report_buffer_printf(dsc_buffer,
            "\n"
            "sosreport and other files were attached as '%s' to the case.\n"
            "For more details about elements collected by ABRT see:\n"
            "%s\n"
            , tarball_name, ABRT_ELEMENTS_KB_ARTICLE);


    summary = problem_report_get_summary(pr);
    dsc = problem_report_get_description(pr);

    /* debug */
    if (opts & OPT_D)
    {
        printf("summary: %s\n"
                "\n"
                "%s"
                "\n"
                , summary
                , dsc
        );

        problem_report_free(pr);
        problem_formatter_free(pf);

        exit(0);
    }

    /* Gzipping e.g. 0.5gig coredump takes a while. Let user know what we are doing */
    log_warning(_("Compressing data"));

    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        libreport_xfunc_die(); /* error msg is already logged by dd_opendir */

    if (create_tarball(tempfile, dd, problem_data) != 0)
    {
        errmsg = _("Can't create temporary file in "LARGE_DATA_TMP_DIR);
        goto ret;
    }

    off_t tempfile_size = libreport_stat_st_size_or_die(tempfile);

    if (!(opts & OPT_t))
    {
        if (tempfile_size <= QUERY_HINTS_IF_SMALLER_THAN)
        {
            /* Check for hints and show them if we have something */
            log_warning(_("Checking for hints"));
            if (check_for_hints(base_api_url, &login, &password, ssl_verify, tempfile))
                goto ret;
        }

        log_warning(_("Creating a new case"));

        char *product = NULL;
        char *version = NULL;
        map_string_t *osinfo = libreport_new_map_string();
        problem_data_get_osinfo(problem_data, osinfo);
        libreport_parse_osinfo_for_rhts(osinfo, &product, &version);
        libreport_free_map_string(osinfo);

        if (!product)
        {   /* How can we help user sorting out this problem? */
            error_msg_and_die(_("Can't determine RH Support Product from problem data."));
        }

        INVALID_CREDENTIALS_LOOP(login, password,
                result, create_new_case(url, login, password, ssl_verify,
                                        product, version, summary, dsc, package)
        );

        free(version);
        free(product);
        problem_report_free(pr);
        problem_formatter_free(pf);

        if (result->error)
        {
            /*
             * Message can contain "...server says: 'multi-line <html> text'"
             * Replace all '\n' with spaces:
             * we want this message to be, logically, one log entry.
             * IOW: one line, not many lines.
             */
            char *src, *dst;
            errmsg = dst = src = result->msg;
            while (1)
            {
                unsigned char c = *src++;
                if (c == '\n')
                    c = ' ';
                *dst++ = c;
                if (c == '\0')
                    break;
            }
            /* Remove trailing spaces (usually produced by trailing '\n') */
            while (--dst >= errmsg && *dst == ' ')
                *dst = '\0';
            goto ret;
        }
        /* No error in case creation */
        /* Record "reported_to" element */
        dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
        if (dd)
        {
            report_result_t *report_result;

            report_result = report_result_new_with_label_from_env("RHTSupport");

            report_result_set_message(report_result, result->msg);
            report_result_set_timestamp(report_result, time(NULL));
            report_result_set_url(report_result, result->url);

            libreport_add_reported_to_entry(dd, report_result);
            dd_close(dd);
            if (result->msg)
                log_warning("%s", result->msg);
            log_warning("URL=%s", result->url);

            report_result_free(report_result);
        }
        /* else: error msg was already emitted by dd_opendir */

        if (bthash)
        {
            log_warning(_("Linking ABRT crash statistics record with the case"));

            /* Make sure we use the current credentials */
            libreport_ureport_server_config_set_basic_auth(&urconf, login, password);

            /* Attach Customer Case ID*/
            attach_to_ureport(&urconf, bthash, "RHCID", result->url);

            /* Attach Contact e-mail if configured */
            const char *email = NULL;
            UREPORT_OPTION_VALUE_FROM_CONF(ursettings, "ContactEmail", email, (const char *));
            if (email != NULL)
            {
                log_warning(_("Linking ABRT crash statistics record with contact email: '%s'"), email);
                attach_to_ureport(&urconf, bthash, "email", email);
            }

            /* Update the credentials */
            STRCPY_IF_NOT_EQUAL(login, urconf.ur_username);
            STRCPY_IF_NOT_EQUAL(password, urconf.ur_password);
        }

        url = result->url;
        result->url = NULL;
        free_rhts_result(result);
        result = NULL;
    }

    char *remote_filename = NULL;
    if (bigsize != 0 && tempfile_size / (1024*1024) >= bigsize)
    {
        /* Upload tarball of -d DIR to "big file" FTP */
        /* log_warning(_("Uploading problem data to '%s'"), bigurl); - libreport_upload_file does this */
        remote_filename = libreport_upload_file(bigurl, tempfile);
    }
    if (remote_filename)
    {
        log_warning(_("Adding comment to case '%s'"), url);
        /*
         * Do not translate message below - it goes
         * to a server where *other people* will read it.
         */
        g_autofree char *comment_text = g_strdup_printf(
            "Problem data was uploaded to %s",
            remote_filename
        );
        free(remote_filename);
        INVALID_CREDENTIALS_LOOP(login, password,
                result_atch, add_comment_to_case(url, login, password, ssl_verify, comment_text)
        );
    }
    else
    {
        /* Attach the tarball of -d DIR */
        log_warning(_("Attaching problem data to case '%s'"), url);
        INVALID_CREDENTIALS_LOOP(login, password,
                result_atch, attach_file_to_case(url, login, password, ssl_verify, tempfile)
        );
    }
    if (result_atch->error)
    {
        if (!(opts & OPT_t))
        {
            /* Prepend "Case created" text to whatever error message there is,
             * so that user knows that case _was_ created despite error in attaching.
             */
            log_warning("Case created but failed to attach problem data: %s", result_atch->msg);
        }
        else
        {
            log_warning("Failed to attach problem data: %s", result_atch->msg);
        }
    }

 ret:
    unlink(tempfile);
    free(tempfile);
    rmdir(tmpdir_name);

    /* Note: errmsg may be = result->msg, don't move this code block
     * below free_rhts_result(result)!
     */
    if (errmsg)
        error_msg_and_die("%s", errmsg);

    free_rhts_result(result_atch);
    free_rhts_result(result);

    libreport_ureport_server_config_destroy(&urconf);
    libreport_free_map_string(ursettings);
    free(bthash);

    free(url);
    free(login);
    free(password);
    problem_data_free(problem_data);

    return 0;
}
