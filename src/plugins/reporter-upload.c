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
#include "libreport_curl.h"
#include "internal_libreport.h"
#include "client.h"

static char *ask_url(const char *message)
{
    char *url = libreport_ask(message);
    if (url == NULL || url[0] == '\0')
    {
        libreport_set_xfunc_error_retval(EXIT_CANCEL_BY_USER);
        error_msg_and_die(_("Can't continue without URL"));
    }

    return url;
}

static int interactive_upload_file(const char *url, const char *file_name,
                                   map_string_t *settings, char **remote_name)
{
    post_state_t *state = new_post_state(POST_WANT_ERROR_MSG);
    state->username = libreport_get_map_string_item_or_NULL(settings, "UploadUsername");
    char *password_inp = NULL;
    if (state->username != NULL && state->username[0] != '\0')
    {
        /* Load Password only if Username is configured, it doesn't make */
        /* much sense to load Password without Username. */
        state->password = libreport_get_map_string_item_or_NULL(settings, "UploadPassword");
        if (state->password == NULL)
        {
            /* Be permissive and nice, ask only once and don't check */
            /* the result. User can dismiss this prompt but the upload */
            /* may work somehow??? */
            g_autofree char *msg = g_strdup_printf(_("Please enter password for uploading:"));
            state->password = password_inp = libreport_ask_password(msg);
        }
    }

    /* set SSH keys */
    state->client_ssh_public_keyfile = libreport_get_map_string_item_or_NULL(settings, "SSHPublicKey");
    state->client_ssh_private_keyfile = libreport_get_map_string_item_or_NULL(settings, "SSHPrivateKey");

    if (state->client_ssh_public_keyfile != NULL)
        log_debug("Using SSH public key '%s'", state->client_ssh_public_keyfile);
    if (state->client_ssh_private_keyfile != NULL)
        log_debug("Using SSH private key '%s'", state->client_ssh_private_keyfile);

    char *tmp = libreport_upload_file_ext(state, url, file_name, UPLOAD_FILE_HANDLE_ACCESS_DENIALS);

    if (remote_name)
        *remote_name = tmp;
    else
        free(tmp);

    free(password_inp);
    free_post_state(state);

    /* return 0 on success */
    return tmp == NULL;
}

static int create_and_upload_archive(
                const char *dump_dir_name,
                const char *url,
                map_string_t *settings,
                char **remote_name)
{
    int result = 1; /* error */
    char* tempfile = NULL;

    /* Create a child gzip which will compress the data */
    /* SELinux guys are not happy with /tmp, using /var/run/abrt */
    /* Reverted back to /tmp for ABRT2 */
    /* Changed again to /var/tmp because of Fedora feature tmp-on-tmpfs */
    tempfile = libreport_concat_path_basename(LARGE_DATA_TMP_DIR, dump_dir_name);
    tempfile = libreport_append_to_malloced_string(tempfile, ".tar.gz");

    string_vector_ptr_t exclude_from_report = libreport_get_global_always_excluded_elements();

    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        libreport_xfunc_die(); /* error msg is already logged by dd_opendir */

    /* Compressing e.g. 0.5gig coredump takes a while. Let client know what we are doing */
    log_warning(_("Compressing data"));
    if (dd_create_archive(dd, tempfile, (const_string_vector_const_ptr_t)exclude_from_report, 0) != 0)
    {
        log_error("Can't create temporary file in %s", LARGE_DATA_TMP_DIR);
        goto ret;
    }

    dd_close(dd);
    dd = NULL;

    /* Upload the archive */
    /* Upload from /tmp to /tmp + deletion -> BAD, exclude this possibility */
    if (url && url[0] && strcmp(url, "file://"LARGE_DATA_TMP_DIR"/") != 0)
        result = interactive_upload_file(url, tempfile, settings, remote_name);
    else
    {
        result = 0; /* success */
        log_warning(_("Archive is created: '%s'"), tempfile);
        *remote_name = tempfile;
        tempfile = NULL;
    }

 ret:
    dd_close(dd);

    if (tempfile)
    {
        unlink(tempfile);
        free(tempfile);
    }

    return result;
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
    const char *conf_file = CONF_DIR"/plugins/upload.conf";
    const char *url = NULL;
    const char *ssh_public_key = NULL;
    const char *ssh_private_key = NULL;

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "& [-v] -d DIR [-c CONFFILE] [-u URL] [-b FILE] [-r FILE]\n"
        "\n"
        "Uploads compressed tarball of problem directory DIR to URL.\n"
        "If URL is not specified, creates tarball in "LARGE_DATA_TMP_DIR" and exits.\n"
        "\n"
        "URL should have form 'protocol://[user[:pass]@]host/dir/[file.tar.gz]'\n"
        "where protocol can be http(s), ftp, scp, or file.\n"
        "File protocol can't have user and host parts: 'file:///dir/[file.tar.gz].'\n"
        "If URL ends with a slash, the archive name will be generated and appended\n"
        "to URL; otherwise, URL will be used as full file name.\n"
        "\n"
        "Files with names listed in $EXCLUDE_FROM_REPORT are not included\n"
        "into the tarball.\n"
        "\n"
        "\n""If not specified, CONFFILE defaults to "CONF_DIR"/plugins/upload.conf"
        "\n""Its lines should have 'PARAM = VALUE' format."
        "Recognized string parameter: URL.\n"
        "Parameter can be overridden via $Upload_URL."
    );
    enum {
        OPT_v = 1 << 0,
        OPT_d = 1 << 1,
        OPT_c = 1 << 2,
        OPT_u = 1 << 3,
        OPT_b = 1 << 4,
        OPT_r = 1 << 5,
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&libreport_g_verbose),
        OPT_STRING('d', NULL, &dump_dir_name, "DIR"     , _("Problem directory")),
        OPT_STRING('c', NULL, &conf_file    , "CONFFILE", _("Config file")),
        OPT_STRING('u', NULL, &url          , "URL"     , _("Base URL to upload to")),
        OPT_STRING('b', "pubkey",  &ssh_public_key , "FILE" , _("SSH public key file")),
        OPT_STRING('r', "key",     &ssh_private_key, "FILE" , _("SSH private key file")),
        OPT_END()
    };
    /*unsigned opts =*/ libreport_parse_opts(argc, argv, program_options, program_usage_string);

    libreport_export_abrt_envvars(0);

    // 2015-10-16 (jfilak):
    //   It looks like there is no demand for encryption and other archive
    //   types. Configurable ExcludeFiles sounds reasonable to me, I am
    //   not sure about globbing though.
    //
    //Encrypt = yes
    //ArchiveType = .tar.bz2
    //
    //TODO:
    //ExcludeFiles = foo,bar*,b*z

    map_string_t *settings = libreport_new_map_string();
    if (conf_file)
        libreport_load_conf_file(conf_file, settings, /*skip key w/o values:*/ false);

    char *input_url = NULL;
    const char *conf_url = getenv("Upload_URL");
    if (!conf_url || conf_url[0] == '\0')
        conf_url = url;
    if (!conf_url || conf_url[0] == '\0')
        conf_url = libreport_get_map_string_item_or_empty(settings, "URL");
    if (!conf_url || conf_url[0] == '\0')
        conf_url = input_url = ask_url(_("Please enter a URL (scp, ftp, etc.) where the problem data is to be exported:"));

    libreport_set_map_string_item_from_string(settings, "UploadUsername", getenv("Upload_Username"));
    libreport_set_map_string_item_from_string(settings, "UploadPassword", getenv("Upload_Password"));

    /* set SSH keys */
    if (ssh_public_key)
        libreport_set_map_string_item_from_string(settings, "SSHPublicKey", ssh_public_key);
    else if (getenv("Upload_SSHPublicKey") != NULL)
        libreport_set_map_string_item_from_string(settings, "SSHPublicKey", getenv("Upload_SSHPublicKey"));

    if (ssh_private_key)
        libreport_set_map_string_item_from_string(settings, "SSHPrivateKey", ssh_private_key);
    else if (getenv("Upload_SSHPrivateKey") != NULL)
        libreport_set_map_string_item_from_string(settings, "SSHPrivateKey", getenv("Upload_SSHPrivateKey"));

    char *remote_name = NULL;
    const int result = create_and_upload_archive(dump_dir_name, conf_url, settings, &remote_name);
    if (result != 0)
        goto finito;

    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (dd)
    {
        report_result_t *result;

        result = report_result_new_with_label_from_env("upload");

        report_result_set_url(result, remote_name);

        libreport_add_reported_to_entry(dd, result);

        report_result_free(result);

        dd_close(dd);
    }
    free(remote_name);

finito:
    free(input_url);
    libreport_free_map_string(settings);
    return result;
}
