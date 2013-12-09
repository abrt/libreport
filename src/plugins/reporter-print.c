/*
    Write crash dump to stdout in text form.

    Copyright (C) 2009  Zdenek Prikryl (zprikryl@redhat.com)
    Copyright (C) 2009  RedHat inc.

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

static const char *dump_dir_name = ".";
static char *output_file = NULL;
static const char *append = "no";
static const char *open_mode = "w";

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
        "& [-v] -d DIR [-o FILE] [-a yes/no] [-r]\n"
        "\n"
        "Prints problem information to standard output or FILE"
    );
    enum {
        OPT_v = 1 << 0,
        OPT_d = 1 << 1,
        OPT_o = 1 << 2,
        OPT_a = 1 << 3,
        OPT_r = 1 << 4,
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_STRING('d', NULL, &dump_dir_name, "DIR"   , _("Problem directory")),
        OPT_STRING('o', NULL, &output_file  , "FILE"  , _("Output file")),
        OPT_STRING('a', NULL, &append       , "yes/no", _("Append to, or overwrite FILE")),
        OPT_BOOL(  'r', NULL, NULL          ,           _("Create reported_to in DIR")),
        OPT_END()
    };
    unsigned opts = parse_opts(argc, argv, program_options, program_usage_string);

    export_abrt_envvars(0);

    if (output_file)
    {
        char *HOME;
        if (output_file[0] == '~' && output_file[1] == '/'
         && (HOME = getenv("HOME")) != NULL
        ) {
            output_file = concat_path_file(HOME, output_file + 2);
        }
        else
            output_file = xstrdup(output_file);

        if (string_to_bool(append))
            open_mode = "a";

        /* We used freopen to change stdout,
         * but ask() writes to stdout. Can't use that trick anymore.
         */
        char *msg = NULL;
        while (1)
        {
            /* prompt for another file name if needed */
            if (msg)
            {
                free(output_file);
                char *response = ask(msg);
                if (!response)
                    perror_msg_and_die("ask");
                free(msg);

                if (response[0] == '\0' || response[0] == '\n')
                {
                    set_xfunc_error_retval(EXIT_CANCEL_BY_USER);
                    error_msg_and_die(_("Cancelled by user."));
                }

                output_file = strtrim(response);
            }

            FILE *outstream = fopen(output_file, open_mode);
            if (!outstream)
            {
                VERB1 pwarn_msg("fopen");
                msg = xasprintf(_("Can't open '%s' for writing. "
                                  "Please select another file:"), output_file);
                continue;
            }

            fclose(stdout);
            stdout = outstream;
            break;
        }
    }

    problem_data_t *problem_data = create_problem_data_for_reporting(dump_dir_name);
    if (!problem_data)
        xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    char *dsc = make_description_logger(problem_data, CD_TEXT_ATT_SIZE_LOGGER);
    fputs(dsc, stdout);
    if (open_mode[0] == 'a')
        fputs("\nEND:\n\n", stdout);
    free(dsc);
    problem_data_free(problem_data);

    if (output_file)
    {
        if (opts & OPT_r)
        {
            struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
            if (dd)
            {
                char *msg = xasprintf("file: URL=file://%s", output_file);
                add_reported_to(dd, msg);
                free(msg);
                dd_close(dd);
            }
        }
        const char *format = (open_mode[0] == 'a' ? _("The report was appended to %s") : _("The report was stored to %s"));
        log(format, output_file);
        free(output_file);
    }

    return 0;
}
