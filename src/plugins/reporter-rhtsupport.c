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
#include "abrt_curl.h"
#include "abrt_rh_support.h"
#include "reporter-rhtsupport.h"

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
        dest = xstrdup(src); \
    } } while (0)

static report_result_t *get_reported_to(const char *dump_dir_name)
{
    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        xfunc_die();
    report_result_t *reported_to = find_in_reported_to(dd, "RHTSupport:");
    dd_close(dd);
    return reported_to;
}

static
int create_tarball(const char *tempfile, problem_data_t *problem_data)
{
    reportfile_t *file = NULL;
    int retval = 0; /* everything is ok so far .. */

    int pipe_from_parent_to_child[2];
    xpipe(pipe_from_parent_to_child);
    pid_t child = fork();
    if (child == 0)
    {
        /* child */
        close(pipe_from_parent_to_child[1]);
        xmove_fd(xopen3(tempfile, O_WRONLY | O_CREAT | O_EXCL, 0600), 1);
        xmove_fd(pipe_from_parent_to_child[0], 0);
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
                char *xml_name = concat_path_file("content", basename);
                reportfile_add_binding_from_namedfile(file,
                        /*on_disk_filename */ content,
                        /*binding_name     */ name,
                        /*recorded_filename*/ xml_name,
                        /*binary           */ 1);
                if (tar_append_file(tar, (char*)content, xml_name) != 0)
                {
                    free(xml_name);
                    goto ret_fail;
                }
                free(xml_name);
            }
        }
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
        char *block = (char*)memcpy(xzalloc(len512), signature, len);

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
         || full_write(tar_fd(tar), block, len512) != len512
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
    safe_waitpid(child, &status, 0);
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
        safe_waitpid(child, NULL, 0);
    }

ret_clean:
    /* now it's safe to free file */
    free_reportfile(file);
    return retval;
}

static
struct ureport_server_response *ureport_do_post_credentials(const char *json, struct ureport_server_config *config, const char *action)
{
    struct abrt_post_state *post_state = NULL;
    while (1)
    {
        post_state = ureport_do_post(json, config, action);

        if (post_state == NULL)
        {
            error_msg(_("Failed on submitting the problem"));
            return NULL;
        }

        if (post_state->http_resp_code != 401)
            break;

        free_abrt_post_state(post_state);

        char *login = NULL;
        char *password = NULL;
        ask_rh_credentials(&login, &password);
        ureport_server_config_set_basic_auth(config, login, password);
        free(password);
        free(login);
    }

    struct ureport_server_response *resp = ureport_server_response_from_reply(post_state, config);
    free(post_state);
    return resp;
}

static
char *submit_ureport(const char *dump_dir_name, struct ureport_server_config *conf)
{
    struct dump_dir *dd = dd_opendir(dump_dir_name, DD_OPEN_READONLY);
    if (dd == NULL)
        return NULL;

    report_result_t *rr_bthash = find_in_reported_to(dd, "uReport");
    dd_close(dd);

    if (rr_bthash != NULL)
    {
        VERB1 log("uReport has already been submitted.");
        char *ret = xstrdup(rr_bthash->bthash);
        free_report_result(rr_bthash);
        return ret;
    }

    char *json = ureport_from_dump_dir(dump_dir_name);
    if (json == NULL)
        return NULL;

    struct ureport_server_response *resp = ureport_do_post_credentials(json, conf, UREPORT_SUBMIT_ACTION);
    free(json);
    if (resp == NULL)
        return NULL;

    char *bthash = NULL;
    if (!resp->urr_is_error)
    {
        if (resp->urr_bthash != NULL)
            bthash = xstrdup(resp->urr_bthash);

        ureport_server_response_save_in_dump_dir(resp, dump_dir_name, conf);

        if (resp->urr_message)
            log(resp->urr_message);
    }
    else if (g_verbose > 2)
        error_msg(_("Server responded with an error: '%s'"), resp->urr_value);

    ureport_server_response_free(resp);
    return bthash;
}

static
void attach_to_ureport(struct ureport_server_config *conf,
        const char *bthash, const char *attach_id, const char *data)
{
    char *json = ureport_json_attachment_new(bthash, attach_id, data);
    struct ureport_server_response *resp = ureport_do_post_credentials(json, conf, UREPORT_ATTACH_ACTION);
    ureport_server_response_free(resp);
    free(json);
}

static
bool check_for_hints(const char *url, char **login, char **password, bool ssl_verify, const char *tempfile)
{
    rhts_result_t *result = NULL;

    INVALID_CREDENTIALS_LOOP((*login), (*password),
            result, get_rhts_hints(url, *login, *password, ssl_verify, tempfile)
    );

#if 0 /* testing */
    log("ERR:%d", result->error);
    log("MSG:'%s'", result->msg);
    log("BODY:'%s'", result->body);
    result->error = 0;
    result->body = xstrdup(
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<problems xmlns=\"http://www.redhat.com/gss/strata\">"
            "<link uri=\"http://access.redhat.com/\" rel=\"help\">The main Red Hat Support web site</link>"
            "<property name=\"content\">an ABRT report</property>"
            "<problem>"
            "<property name=\"source\">a backtrace in the ABRT report</property>"
            "<link uri=\"https://avalon-ci.gss.redhat.com/kb/docs/DOC-22029\" rel=\"suggestion\">[RHEL 5.3] EVO autocompletion lookup hang</link>"
            "</problem>"
            "</problems>"
            );
#endif
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
            hint = append_to_malloced_string(hint, " ");
            hint = append_to_malloced_string(hint,
                    _("Do you still want to create a RHTSupport ticket?")
                    );
            int create_ticket = ask_yes_no(hint);
            free(hint);
            if (!create_ticket)
                return true;
        }
    }
    free_rhts_result(result);
    return false;
}

static
char *ask_rh_login(const char *message)
{
    char *login = ask(message);
    if (login == NULL || login[0] == '\0')
        error_msg_and_die(_("Can't continue without login"));

    return login;
}

static
char *ask_rh_password(const char *message)
{
    char *password = ask_password(message);
    if (password == NULL || password[0] == '\0')
        error_msg_and_die(_("Can't continue without password"));

    return password;
}

static
void ask_rh_credentials(char **login, char **password)
{
    free(*login);
    free(*password);

    *login = ask_rh_login(_("Invalid password or login. Please enter your Red Hat login:"));

    char *question = xasprintf(_("Invalid password or login. Please enter the password for '%s':"), *login);
    *password = ask_rh_password(question);
    free(question);
}

static
char *get_param_string(const char *name, map_string_t *settings, const char *dflt)
{
    char *envname = xasprintf("RHTSupport_%s", name);
    const char *envvar = getenv(envname);
    free(envname);
    return xstrdup(envvar ? envvar : (get_map_string_item_or_NULL(settings, name) ? : dflt));
}

static
void prepare_ureport_configuration(const char *urcfile,
        map_string_t *settings, struct ureport_server_config *urconf,
        const char *portal_url, const char *login, const char *password, bool ssl_verify)
{
    load_conf_file(urcfile, settings, false);
    ureport_server_config_init(urconf);

    /* The following lines cause that we always use URL from ureport's
     * configuration becuase the GUI reporter always exports uReport_URL env
     * var.
     *
     *   char *url = NULL;
     *   UREPORT_OPTION_VALUE_FROM_CONF(settings, "URL", url, xstrdup);
     *   if (url != NULL)
     *       ureport_server_config_set_url(urconf, url);
     */

    ureport_server_config_set_url(urconf, concat_path_file(portal_url, "/telemetry/abrt"));
    urconf->ur_ssl_verify = ssl_verify;

    ureport_server_config_set_basic_auth(urconf, login, password);

    bool include_auth = true;
    UREPORT_OPTION_VALUE_FROM_CONF(settings, "IncludeAuthData", include_auth, string_to_bool);

    if (include_auth)
    {
        const char *auth_items = NULL;
        UREPORT_OPTION_VALUE_FROM_CONF(settings, "AuthDataItems", auth_items, (const char *));
        urconf->ur_prefs.urp_auth_items = parse_list(auth_items);
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
    const char *case_no = NULL;
    GList *conf_file = NULL;
    const char *urconf_file = UREPORT_CONF_FILE_PATH;

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "\n"
        "& [-v] [-c CONFFILE] -d DIR\n"
        "or:\n"
        "& [-v] [-c CONFFILE] [-d DIR] -t[ID] [-u -C UR_CONFFILE] FILE...\n"
        "\n"
        "Reports a problem to RHTSupport.\n"
        "\n"
        "If not specified, CONFFILE defaults to "CONF_DIR"/plugins/rhtsupport.conf\n"
        "Its lines should have 'PARAM = VALUE' format.\n"
        "Recognized string parameters: URL, Login, Password, BigFileURL.\n"
        "Recognized numeric parameter: BigSizeMB.\n"
        "Recognized boolean parameter (VALUE should be 1/0, yes/no): SSLVerify.\n"
        "Parameters can be overridden via $RHTSupport_PARAM environment variables.\n"
        "\n"
        "Option -t uploads FILEs to the already created case on RHTSupport site.\n"
        "The case ID is retrieved from directory specified by -d DIR.\n"
        "If problem data in DIR was never reported to RHTSupport, upload will fail.\n"
        "\n"
        "Option -tCASE uploads FILEs to the case CASE on RHTSupport site.\n"
        "-d DIR is ignored."
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
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_STRING(   'd', NULL, &dump_dir_name, "DIR" , _("Dump directory")),
        OPT_LIST(     'c', NULL, &conf_file    , "FILE", _("Configuration file (may be given many times)")),
        OPT_OPTSTRING('t', NULL, &case_no      , "ID"  , _("Upload FILEs [to case with this ID]")),
        OPT_BOOL(     'f', NULL, NULL          ,         _("Force reporting even if this problem is already reported")),
        OPT_BOOL(     'u', NULL, NULL          ,         _("Submit uReport before creating a new case")),
        OPT_STRING(   'C', NULL, &urconf_file  , "FILE", _("Configuration file for uReport")),
        OPT_END()
    };
    unsigned opts = parse_opts(argc, argv, program_options, program_usage_string);
    argv += optind;

    export_abrt_envvars(0);

    /* Parse config, extract necessary params */
    map_string_t *settings = new_map_string();
    if (!conf_file)
        conf_file = g_list_append(conf_file, (char*) CONF_DIR"/plugins/rhtsupport.conf");
    while (conf_file)
    {
        const char *fn = (char *)conf_file->data;
        VERB1 log("Loading settings from '%s'", fn);
        load_conf_file(fn, settings, /*skip key w/o values:*/ true);
        VERB3 log("Loaded '%s'", fn);
        conf_file = g_list_remove(conf_file, fn);
    }
    char *url      = get_param_string("URL"       , settings, "https://api.access.redhat.com/rs");
    char *login    = get_param_string("Login"     , settings, "");
    char *password = get_param_string("Password"  , settings, "");
    char *bigurl   = get_param_string("BigFileURL", settings, "ftp://dropbox.redhat.com/incoming/");
    if (!login[0] || !password[0])
        error_msg_and_die(_("Empty RHTS login or password"));
    char* envvar;
    envvar = getenv("RHTSupport_SSLVerify");
    bool ssl_verify = string_to_bool(
                envvar ? envvar : (get_map_string_item_or_NULL(settings, "SSLVerify") ? : "1")
    );
    envvar = getenv("RHTSupport_BigSizeMB");
    unsigned bigsize = xatoi_positive(
                /* RH has a 250m limit for web attachments (as of 2013) */
                envvar ? envvar : (get_map_string_item_or_NULL(settings, "BigSizeMB") ? : "200")
    );
    envvar = getenv("RHTSupport_SubmitUReport");
    bool submit_ur = string_to_bool(
                envvar ? envvar :
                    (get_map_string_item_or_NULL(settings, "SubmitUReport") ? :
                        ((opts & OPT_u) ? "1" : "0"))
    );
    free_map_string(settings);

    char *base_api_url = xstrdup(url);
    char *bthash = NULL;

    map_string_t *ursettings = new_map_string();
    struct ureport_server_config urconf;

    prepare_ureport_configuration(urconf_file, ursettings, &urconf,
            url, login, password, ssl_verify);

    if (opts & OPT_t)
    {
        if (!case_no)
        {
            /* -t: ask for it */
            report_result_t *reported_to = get_reported_to(dump_dir_name);
            if (!reported_to || !reported_to->url)
            {
                case_no = ask(_("Case number:"));
                if (!case_no)
                    perror_msg_and_die("ask");
                if (case_no[0])
                    goto got_case_no;
                /* User just pressed [enter] (or [cancel] in GIU). */
                error_msg_and_die(_("Reporting cancelled"));
            }
            /* We have a preexisting URL */
            log(_("Note: was already reported to '%s'"), reported_to->url);
#if 1
            case_no = ask(_("Case number:"));
#else
            case_no = ask(_("Case number (default:previously reported case):"));
#endif
            if (!case_no)
                perror_msg_and_die("ask");
            if (case_no[0])
                goto got_case_no;
#if 1
            error_msg_and_die(_("Reporting cancelled"));
#else
//GUI BUG: would like to use this, but in GUI, [cancel] in ask dialog
//also results in ""!
//Enabling this will make [cancel] in GUI to NOT cancel!
            /* User just pressed [enter]. Use previous URL */
            free(url);
            url = reported_to->url;
            reported_to->url = NULL;
            free_report_result(reported_to);
#endif
        }
        else
        {
 got_case_no:;
            /* -tCASE */
            char *url1 = concat_path_file(url, "cases");
            free(url);
            url = concat_path_file(url1, case_no);
            free(url1);
        }

        if (*argv)
        {
            /* -t[CASE] FILE: just attach files and exit */
            while (*argv)
            {
                log(_("Attaching '%s' to case '%s'"), *argv, url);
                rhts_result_t *result = attach_file_to_case(url,
                    login,
                    password,
                    ssl_verify,
                    *argv
                );
                if (result->error)
                    error_msg_and_die("%s", result->msg);
                log("Attachment URL:%s", result->url);
                log("File attached successfully");
                free_rhts_result(result);
                argv++;
            }
            return 0;
        }
    }
    else /* no -t: creating a new case */
    {
        if (*argv)
            show_usage_and_die(program_usage_string, program_options);

        report_result_t *reported_to = get_reported_to(dump_dir_name);
        if (reported_to && reported_to->url && !(opts & OPT_f))
        {
            char *msg = xasprintf("This problem was already reported to RHTS (see '%s')."
                            " Do you still want to create a RHTSupport ticket?",
                            reported_to->url);
            int yes = ask_yes_no(msg);
            free(msg);
            if (!yes)
                return 0;
        }
        free_report_result(reported_to);

        if (submit_ur)
        {
            log(_("Sending ABRT crash statistics data"));

            bthash = submit_ureport(dump_dir_name, &urconf);

            /* Ensure that we will use the updated credentials */
            STRCPY_IF_NOT_EQUAL(login, urconf.ur_username);
            STRCPY_IF_NOT_EQUAL(password, urconf.ur_password);
        }
    }

    /* Gzipping e.g. 0.5gig coredump takes a while. Let user know what we are doing */
    log(_("Compressing data"));

    problem_data_t *problem_data = create_problem_data_for_reporting(dump_dir_name);
    if (!problem_data)
        xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    const char *errmsg = NULL;
    char *tempfile = NULL;
    rhts_result_t *result = NULL;
    rhts_result_t *result_atch = NULL;
    char *dsc = NULL;
    char *summary = NULL;
    const char *function;
    const char *reason;
    const char *package;
    const char *release;

    release  = get_problem_item_content_or_NULL(problem_data, FILENAME_OS_RELEASE);
    if (!release) /* Old dump dir format compat. Remove in abrt-2.1 */
        release = get_problem_item_content_or_NULL(problem_data, "release");
    package  = get_problem_item_content_or_NULL(problem_data, FILENAME_PACKAGE);
    reason   = get_problem_item_content_or_NULL(problem_data, FILENAME_REASON);
    function = get_problem_item_content_or_NULL(problem_data, FILENAME_CRASH_FUNCTION);
    {
        struct strbuf *buf_summary = strbuf_new();
        strbuf_append_strf(buf_summary, "[abrt] %s", package);
        if (function && strlen(function) < 30)
            strbuf_append_strf(buf_summary, ": %s", function);
        if (reason)
            strbuf_append_strf(buf_summary, ": %s", reason);
        summary = strbuf_free_nobuf(buf_summary);
        dsc = make_description_bz(problem_data, CD_TEXT_ATT_SIZE_BZ);
    }

    char tmpdir_name[sizeof("/tmp/rhtsupport-"LIBREPORT_ISO_DATE_STRING_SAMPLE"-XXXXXX")];
    snprintf(tmpdir_name, sizeof(tmpdir_name), "/tmp/rhtsupport-%s-XXXXXX", iso_date_string(NULL));
    /* mkdtemp does mkdir(xxx, 0700), should be safe (is it?) */
    if (mkdtemp(tmpdir_name) == NULL)
    {
        error_msg_and_die(_("Can't create a temporary directory in /tmp"));
    }
    /* Starting from here, we must perform cleanup on errors
     * (delete temp dir)
     */
    tempfile = concat_path_basename(tmpdir_name, dump_dir_name);
    tempfile = append_to_malloced_string(tempfile, ".tar.gz");
    if (create_tarball(tempfile, problem_data) != 0)
    {
        errmsg = _("Can't create temporary file in /tmp");
        goto ret;
    }

    off_t tempfile_size = stat_st_size_or_die(tempfile);

    if (!(opts & OPT_t))
    {
        if (tempfile_size <= QUERY_HINTS_IF_SMALLER_THAN)
        {
            /* Check for hints and show them if we have something */
            log(_("Checking for hints"));
            if (check_for_hints(base_api_url, &login, &password, ssl_verify, tempfile))
            {
                ureport_server_config_destroy(&urconf);
                free_map_string(ursettings);
                free(bthash);
                goto ret;
            }
        }

        log(_("Creating a new case"));
        INVALID_CREDENTIALS_LOOP(login, password,
                result, create_new_case(url, login, password, ssl_verify,
                                        release, summary, dsc, package)
        );

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
        struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
        if (dd)
        {
            char *msg = xasprintf("RHTSupport: TIME=%s URL=%s%s%s",
                    iso_date_string(NULL),
                    result->url,
                    result->msg ? " MSG=" : "", result->msg ? result->msg : ""
            );
            add_reported_to(dd, msg);
            free(msg);
            dd_close(dd);
            if (result->msg)
                log("%s", result->msg);
            log("URL=%s", result->url);
        }
        /* else: error msg was already emitted by dd_opendir */

        if (bthash)
        {
            log(_("Linking ABRT crash statistics record with the case"));

            /* Make sure we use the current credentials */
            ureport_server_config_set_basic_auth(&urconf, login, password);

            /* Attach Customer Case ID*/
            attach_to_ureport(&urconf, bthash, "RHCID", result->url);

            /* Attach Contact e-mail if configured */
            const char *email = NULL;
            UREPORT_OPTION_VALUE_FROM_CONF(ursettings, "ContactEmail", email, (const char *));
            if (email != NULL)
            {
                log(_("Linking ABRT crash statistics record with contact email: '%s'"), email);
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
        /* log(_("Uploading problem data to '%s'"), bigurl); - upload_file does this */
        remote_filename = upload_file(bigurl, tempfile);
    }
    if (remote_filename)
    {
        log(_("Adding comment to case '%s'"), url);
        /*
         * Do not translate message below - it goes
         * to a server where *other people* will read it.
         */
        char *comment_text = xasprintf(
            "Problem data was uploaded to %s",
            remote_filename
        );
        free(remote_filename);
        INVALID_CREDENTIALS_LOOP(login, password,
                result_atch, add_comment_to_case(url, login, password, ssl_verify, comment_text)
        );
        free(comment_text);
    }
    else
    {
        /* Attach the tarball of -d DIR */
        log(_("Attaching problem data to case '%s'"), url);
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
            log("Case created but failed to attach problem data: %s", result_atch->msg);
        }
        else
        {
            log("Failed to attach problem data: %s", result_atch->msg);
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

    free(summary);
    free(dsc);

    free_rhts_result(result_atch);
    free_rhts_result(result);

    ureport_server_config_destroy(&urconf);
    free_map_string(ursettings);
    free(bthash);

    free(base_api_url);
    free(url);
    free(login);
    free(password);
    free_problem_data(problem_data);

    return 0;
}
