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
#include "abrt_curl.h"
#include "internal_libreport.h"
#include "client.h"

static char *ask_url(const char *message)
{
    char *url = ask(message);
    if (url == NULL || url[0] == '\0')
    {
        set_xfunc_error_retval(EXIT_CANCEL_BY_USER);
        error_msg_and_die(_("Can't continue without URL"));
    }

    return url;
}

static int create_and_upload_archive(
                const char *dump_dir_name,
                const char *url,
                map_string_t *settings,
                char **remote_name)
{
    int result = 1; /* error */

    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        xfunc_die(); /* error msg is already logged by dd_opendir */

    /* Create a child gzip which will compress the data */
    /* SELinux guys are not happy with /tmp, using /var/run/abrt */
    /* Reverted back to /tmp for ABRT2 */
    char* tempfile = NULL;
    tempfile = concat_path_basename("/tmp", dump_dir_name);
    tempfile = append_to_malloced_string(tempfile, ".tar.gz");

    string_vector_ptr_t exclude_from_report = get_global_always_excluded_elements();
    /* Gzipping e.g. 0.5gig coredump takes a while. Let client know what we are doing */
    log(_("Compressing data"));
    if (dd_create_archive(dd, tempfile, exclude_from_report, 0) != 0)
    {
        error_msg("Can't create temporary file in /tmp");
        goto ret;
    }

    /* Upload the tarball */
    /* Upload from /tmp to /tmp + deletion -> BAD, exclude this possibility */
    if (url && url[0] && strcmp(url, "file:///tmp/") != 0)
    {
        char *tmp = upload_file(url, tempfile, settings);
        if (remote_name)
            *remote_name = tmp;
        else
            free(tmp);

        result = (tmp == NULL);
    }
    else
    {
        result = 0; /* success */
        log(_("Archive is created: '%s'"), tempfile);
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
    if (!load_global_configuration())
        error_msg_and_die("Cannot continue without libreport global configuration.");

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
        "If URL is not specified, creates tarball in /tmp and exits.\n"
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
        "CONFFILE lines should have 'PARAM = VALUE' format.\n"
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
        OPT__VERBOSE(&g_verbose),
        OPT_STRING('d', NULL, &dump_dir_name, "DIR"     , _("Dump directory")),
        OPT_STRING('c', NULL, &conf_file    , "CONFFILE", _("Config file")),
        OPT_STRING('u', NULL, &url          , "URL"     , _("Base URL to upload to")),
        OPT_STRING('b', "pubkey",  &ssh_public_key , "FILE" , _("SSH public key file")),
        OPT_STRING('r', "key",     &ssh_private_key, "FILE" , _("SSH private key file")),
        OPT_END()
    };
    /*unsigned opts =*/ parse_opts(argc, argv, program_options, program_usage_string);

    export_abrt_envvars(0);

    map_string_t *settings = new_map_string();
    if (url)
        replace_map_string_item(settings, xstrdup("URL"), xstrdup(url));
    if (conf_file)
        load_conf_file(conf_file, settings, /*skip key w/o values:*/ true);

     /* set SSH keys */
    if (ssh_public_key)
        set_map_string_item_from_string(settings, "SSHPublicKey", ssh_public_key);
    else if (getenv("Upload_SSHPublicKey") != NULL)
        set_map_string_item_from_string(settings, "SSHPublicKey", getenv("Upload_SSHPublicKey"));

    if (ssh_private_key)
        set_map_string_item_from_string(settings, "SSHPrivateKey", ssh_private_key);
    else if (getenv("Upload_SSHPrivateKey") != NULL)
        set_map_string_item_from_string(settings, "SSHPrivateKey", getenv("Upload_SSHPrivateKey"));

    char *input_url = NULL;
    const char *conf_url = getenv("Upload_URL");
    if (!conf_url || conf_url[0] == '\0')
        conf_url = url;
    if (!conf_url || conf_url[0] == '\0')
        conf_url = get_map_string_item_or_empty(settings, "URL");
    if (!conf_url || conf_url[0] == '\0')
        conf_url = input_url = ask_url(_("Please enter a URL (scp, ftp, etc.) where the problem data is to be exported:"));

    char *remote_name = NULL;
    const int result = create_and_upload_archive(dump_dir_name, conf_url, settings, &remote_name);
    if (result != 0)
        goto finito;

    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (dd)
    {
        report_result_t rr = { .label = (char *)"upload" };
        rr.url = remote_name,
        add_reported_to_entry(dd, &rr);
        dd_close(dd);
    }
    free(remote_name);

finito:
    free(input_url);
    free_map_string(settings);
    return result;
}
