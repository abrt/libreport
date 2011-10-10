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
#include "internal_libreport.h"
#include "abrt_curl.h"
#include "abrt_xmlrpc.h"
#include "abrt_rh_support.h"

static char *url;
static char *login;
static char *password;
static bool ssl_verify;

static void report_to_rhtsupport(const char *dump_dir_name)
{
    problem_data_t *problem_data = create_problem_data_for_reporting(dump_dir_name);
    if (!problem_data)
        xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    /* Gzipping e.g. 0.5gig coredump takes a while. Let client know what we are doing */
    log(_("Compressing data"));

    const char *errmsg = NULL;
    TAR *tar = NULL;
    pid_t child;
    char *tempfile = NULL;
    reportfile_t *file = NULL;
    rhts_result_t *result = NULL;
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
        dsc = make_description_bz(problem_data);
    }
    file = new_reportfile();
    const char *dt_string = iso_date_string(NULL);
    char tmpdir_name[sizeof("/tmp/rhtsupport-YYYY-MM-DD-hh:mm:ss-XXXXXX")];
    sprintf(tmpdir_name, "/tmp/rhtsupport-%s-XXXXXX", dt_string);
    /* mkdtemp does mkdir(xxx, 0700), should be safe (is it?) */
    if (mkdtemp(tmpdir_name) == NULL)
    {
        error_msg_and_die(_("Can't create a temporary directory in /tmp"));
    }

    /* Starting from here, we must perform cleanup on errors
     * (delete temp dir)
     */

    tempfile = xasprintf("%s/tmp-%s-%lu.tar.gz", tmpdir_name, iso_date_string(NULL), (long)getpid());

    int pipe_from_parent_to_child[2];
    xpipe(pipe_from_parent_to_child);
    child = fork();
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

    if (tar_fdopen(&tar, pipe_from_parent_to_child[1], tempfile,
                /*fileops:(standard)*/ NULL, O_WRONLY | O_CREAT, 0644, TAR_GNU) != 0)
    {
        errmsg = _("Can't create temporary file in /tmp");
        goto ret;
    }

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
                    errmsg = _("Can't create temporary file in /tmp");
                    free(xml_name);
                    goto ret;
                }
                free(xml_name);
            }
        }
    }

    /* Write out content.xml in the tarball's root */
    {
        const char *signature = reportfile_as_string(file);
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
            errmsg = _("Can't create temporary file in /tmp");
            goto ret;
        }
        tar = NULL;
        free(block);
    }

    /* We must be sure gzip finished, and finished successfully */
    int status;
    waitpid(child, &status, 0);
    child = -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        /* Hopefully, by this time child emitted more meaningful
         * error message. But just in case it didn't:
         */
        errmsg = _("Can't create temporary file in /tmp");
        goto ret;
    }

    /* Send tempfile */
    log(_("Creating a new case..."));
    result = send_report_to_new_case(url,
            login,
            password,
            ssl_verify,
            release,
            summary,
            dsc,
            package,
            tempfile
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

    /* No error */
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

 ret:
    /* We must close write fd first, or else child will wait forever */
    if (tar)
        tar_close(tar);
    //close(pipe_from_parent_to_child[1]); - tar_close() does it itself

    /* Now wait for child to exit */
    if (child > 0)
    {
        // Damn, selinux does not allow SIGKILLing our own child! wtf??
        //kill(child, SIGKILL); /* just in case */
        waitpid(child, NULL, 0);
    }

    unlink(tempfile);
    free(tempfile);
    free_reportfile(file);
    rmdir(tmpdir_name);

    /* Note: errmsg may be = result->msg, don't move this code block
     * below free_rhts_result(result)!
     */
    if (errmsg)
        error_msg_and_die("%s", errmsg);

    free(summary);
    free(dsc);

    free_rhts_result(result);

    free(url);
    free(login);
    free(password);
    free_problem_data(problem_data);
}

/* TODO: move to send_report_to_new_case (it has similar code) */
static void attach_to_rhtsupport(const char *file_name)
{
    log(_("Attaching '%s' to case '%s'"), file_name, url);

    static const char *headers[] = {
        "Accept: text/plain",
        NULL
    };

    int redirect_count = 0;
    char *atch_url = concat_path_file(url, "attachments");
    abrt_post_state_t *atch_state;

 redirect_attach:
    atch_state = new_abrt_post_state(0
            + ABRT_POST_WANT_HEADERS
            + ABRT_POST_WANT_BODY
            + ABRT_POST_WANT_ERROR_MSG
            + (ssl_verify ? ABRT_POST_WANT_SSL_VERIFY : 0)
    );
    atch_state->username = login;
    atch_state->password = password;
    abrt_post_file_as_form(atch_state,
        atch_url,
        "application/binary",
        headers,
        file_name
    );
    free(atch_url);

    char *atch_location = find_header_in_abrt_post_state(atch_state, "Location:");
    switch (atch_state->http_resp_code)
    {
    case 305: /* "305 Use Proxy" */
        if (++redirect_count < 10 && atch_location)
        {
            atch_url = xstrdup(atch_location);
            free_abrt_post_state(atch_state);
            goto redirect_attach;
        }
        /* fall through */

    default:
        /* Error */
        {
            const char *errmsg = atch_state->curl_error_msg;
            if (atch_state->body && atch_state->body[0])
            {
                if (errmsg && errmsg[0]
                 && strcmp(errmsg, atch_state->body) != 0
                ) /* both strata/curl error and body are present (and aren't the same) */
                    errmsg = xasprintf("%s. %s",
                            atch_state->body,
                            errmsg);
                else /* only body exists */
                    errmsg = atch_state->body;
            }
            error_msg_and_die("Can't attach. HTTP code %d%s%s",
                    atch_state->http_resp_code,
                    errmsg ? ". " : "",
                    errmsg ? errmsg : ""
            );
        }
        break;

    case 200:
    case 201:
        log("Attachment URL:%s", atch_location);
        log("File attached successfully");
    } /* switch */

    free_abrt_post_state(atch_state);
}

int main(int argc, char **argv)
{
    abrt_init(argv);

    const char *dump_dir_name = ".";
    const char *case_no = NULL;
    GList *conf_file = NULL;

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "\n"
        "\1 [-v] [-c CONFFILE] -d DIR\n"
        "or:\n"
        "\1 [-v] [-c CONFFILE] [-d DIR] -t[ID] FILE...\n"
        "\n"
        "Reports a problem to RHTSupport.\n"
        "\n"
        "If not specified, CONFFILE defaults to "CONF_DIR"/plugins/rhtsupport.conf\n"
        "Its lines should have 'PARAM = VALUE' format.\n"
        "Recognized string parameters: URL, Login, Password.\n"
        "Recognized boolean parameter (VALUE should be 1/0, yes/no): SSLVerify.\n"
        "Parameters can be overridden via $RHTSupport_PARAM environment variables.\n"
        "\n"
        "Option -t uploads FILEs to the already created case on RHTSupport site.\n"
        "The case ID is retrieved from directory specified by -d DIR.\n"
        "If problem data in DIR was never reported to RHTSupport, upload will fail.\n"
        "\n"
        "Option -tCASE uploads FILEs to the case CASE on RHTSupport site.\n"
        "-d DIR is ignored."
    );
    enum {
        OPT_v = 1 << 0,
        OPT_d = 1 << 1,
        OPT_c = 1 << 2,
        OPT_t = 1 << 3,
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_STRING(   'd', NULL, &dump_dir_name, "DIR" , _("Dump directory")),
        OPT_LIST(     'c', NULL, &conf_file    , "FILE", _("Configuration file (may be given many times)")),
        OPT_OPTSTRING('t', NULL, &case_no      , "ID"  , _("Upload FILEs [to case with this ID]")),
        OPT_END()
    };
    unsigned opts = parse_opts(argc, argv, program_options, program_usage_string);

    export_abrt_envvars(0);

    /* Parse config, extract necessary params */
    map_string_h *settings = new_map_string();
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
    char* envvar;
    envvar = getenv("RHTSupport_URL");
    url = xstrdup(envvar ? envvar : (get_map_string_item_or_NULL(settings, "URL") ? : "https://api.access.redhat.com/rs"));
    envvar = getenv("RHTSupport_Login");
    login = xstrdup(envvar ? envvar : get_map_string_item_or_empty(settings, "Login"));
    envvar = getenv("RHTSupport_Password");
    password = xstrdup(envvar ? envvar : get_map_string_item_or_empty(settings, "Password"));
    envvar = getenv("RHTSupport_SSLVerify");
    ssl_verify = string_to_bool(envvar ? envvar : get_map_string_item_or_empty(settings, "SSLVerify"));
    if (!login[0] || !password[0])
        error_msg_and_die(_("Empty RHTS login or password"));
    free_map_string(settings);

    VERB1 log("Initializing XML-RPC library");
    xmlrpc_env env;
    xmlrpc_env_init(&env);
    xmlrpc_client_setup_global_const(&env);
    if (env.fault_occurred)
        error_msg_and_die("XML-RPC Fault: %s(%d)", env.fault_string, env.fault_code);
    xmlrpc_env_clean(&env);

    argv += optind;
    if (opts & OPT_t)
    {
        if (!*argv)
            show_usage_and_die(program_usage_string, program_options);

        if (!case_no)
        {
            /* -t */
            struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
            if (!dd)
                xfunc_die();
            report_result_t *reported_to = find_in_reported_to(dd, "RHTSupport:");
            dd_close(dd);

            if (!reported_to || !reported_to->url)
                error_msg_and_die("Can't attach: problem data in '%s' "
                        "was not reported to RHTSupport and therefore has no URL",
                        dump_dir_name);

            //log("URL:'%s'", reported_to->url);
            //log("MSG:'%s'", reported_to->msg);
            free(url);
            url = reported_to->url;
            reported_to->url = NULL;
            free_report_result(reported_to);
        }
        else
        {
            /* -tCASE */
            char *url1 = concat_path_file(url, "cases");
            free(url);
            url = concat_path_file(url1, case_no);
            free(url1);
        }

        while (*argv)
            attach_to_rhtsupport(*argv++);

        return 0;
    }

    if (*argv)
        show_usage_and_die(program_usage_string, program_options);

    report_to_rhtsupport(dump_dir_name);
    return 0;
}
