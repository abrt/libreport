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
#include "libreport_curl.h"
#include "internal_libreport.h"

static int create_and_upload_archive(
                const char *dump_dir_name,
                map_string_t *settings)
{
    int result = 1; /* error */

    pid_t child;
    TAR* tar = NULL;
    const char* errmsg = NULL;
    char* tempfile = NULL;

    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        xfunc_die(); /* error msg is already logged by dd_opendir */

    /* Gzipping e.g. 0.5gig coredump takes a while. Let client know what we are doing */
    log(_("Compressing data"));

//TODO:
//Encrypt = yes
//ArchiveType = .tar.bz2
//ExcludeFiles = foo,bar*,b*z
    char* env;
    env = getenv("Upload_URL");
    const char *url = (env ? env : get_map_string_item_or_empty(settings, "URL"));

    /* Create a child gzip which will compress the data */
    /* SELinux guys are not happy with /tmp, using /var/run/abrt */
    /* Reverted back to /tmp for ABRT2 */
    tempfile = concat_path_basename("/tmp", dump_dir_name);
    tempfile = append_to_malloced_string(tempfile, ".tar.gz");

    int pipe_from_parent_to_child[2];
    xpipe(pipe_from_parent_to_child);
    child = vfork();
    if (child == 0)
    {
        /* child */
        close(pipe_from_parent_to_child[1]);
        xmove_fd(pipe_from_parent_to_child[0], 0);
        xmove_fd(xopen3(tempfile, O_WRONLY | O_CREAT | O_EXCL, 0600), 1);
        execlp("gzip", "gzip", NULL);
        perror_msg_and_die("Can't execute '%s'", "gzip");
    }
    close(pipe_from_parent_to_child[0]);

    /* If child died (say, in xopen), then parent might get SIGPIPE.
     * We want to properly unlock dd, therefore we must not die on SIGPIPE:
     */
    signal(SIGPIPE, SIG_IGN);

    /* Create tar writer object */
    if (tar_fdopen(&tar, pipe_from_parent_to_child[1], tempfile,
                /*fileops:(standard)*/ NULL, O_WRONLY | O_CREAT, 0644, TAR_GNU) != 0)
    {
        errmsg = "Can't create temporary file in /tmp";
        goto ret;
    }

    /* Write data to the tarball */
    {
        char *exclude_from_report = getenv("EXCLUDE_FROM_REPORT");
        dd_init_next_file(dd);
        char *short_name, *full_name;
        while (dd_get_next_file(dd, &short_name, &full_name))
        {
            if (exclude_from_report && is_in_comma_separated_list(short_name, exclude_from_report))
                goto next;

            // dd_get_next_file guarantees that it's a REG:
            //struct stat stbuf;
            //if (stat(full_name, &stbuf) != 0)
            // || !S_ISREG(stbuf.st_mode)
            //) {
            //     goto next;
            //}
            if (tar_append_file(tar, full_name, short_name) != 0)
            {
                errmsg = "Can't create temporary file in /tmp";
                free(short_name);
                free(full_name);
                goto ret;
            }
 next:
            free(short_name);
            free(full_name);
        }
    }
    dd_close(dd);
    dd = NULL;

    /* Close tar writer... */
    if (tar_append_eof(tar) != 0 || tar_close(tar) != 0)
    {
        errmsg = "Can't create temporary file in /tmp";
        goto ret;
    }
    tar = NULL;
    /* ...and check that gzip child finished successfully */
    int status;
    safe_waitpid(child, &status, 0);
    child = -1;
    if (status != 0)
    {
        /* We assume the error was out-of-disk-space or out-of-quota */
        errmsg = "Can't create temporary file in /tmp";
        goto ret;
    }

    /* Upload the tarball */
    /* Upload from /tmp to /tmp + deletion -> BAD, exclude this possibility */
    if (url && url[0] && strcmp(url, "file:///tmp/") != 0)
    {
        char *remote_name = upload_file(url, tempfile);
        result = (remote_name == NULL); /* error if NULL */
        free(remote_name);
        /* cleanup code will delete tempfile */
    }
    else
    {
        result = 0; /* success */
        log(_("Archive is created: '%s'"), tempfile);
        free(tempfile);
        tempfile = NULL;
    }

 ret:
    dd_close(dd);
    if (tar)
        tar_close(tar);
    /* close(pipe_from_parent_to_child[1]); - tar_close() does it itself */
    if (child > 0)
        safe_waitpid(child, NULL, 0);
    if (tempfile)
    {
        unlink(tempfile);
        free(tempfile);
    }
    if (errmsg)
        error_msg_and_die("%s", errmsg);

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
    const char *conf_file = NULL;
    const char *url = NULL;

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "& [-v] -d DIR [-c CONFFILE] [-u URL]\n"
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
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_STRING('d', NULL, &dump_dir_name, "DIR"     , _("Problem directory")),
        OPT_STRING('c', NULL, &conf_file    , "CONFFILE", _("Config file")),
        OPT_STRING('u', NULL, &url          , "URL"     , _("Base URL to upload to")),
        OPT_END()
    };
    /*unsigned opts =*/ parse_opts(argc, argv, program_options, program_usage_string);

    export_abrt_envvars(0);

    map_string_t *settings = new_map_string();
    if (url)
        replace_map_string_item(settings, xstrdup("URL"), xstrdup(url));
    if (conf_file)
        load_conf_file(conf_file, settings, /*skip key w/o values:*/ false);

    int result = create_and_upload_archive(dump_dir_name, settings);

    free_map_string(settings);
    return result;
}
