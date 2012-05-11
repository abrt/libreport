/*
    Copyright (C) 2012  ABRT Team
    Copyright (C) 2012  RedHat inc.

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
#include "abrt_curl.h"

int main(int argc, char **argv)
{
    abrt_init(argv);

    const char *dump_dir_path = ".", *url = "https://retrace.fedoraproject.org/faf/reports/new/";
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT__DUMP_DIR(&dump_dir_path),
        OPT_STRING('u', "url", &url, "URL", _("Specify url")),
        OPT_END(),
    };

    const char *program_usage_string = _(
        "& [-v] -d DIR\n"
        "\n"
        "Upload micro report"
    );

    parse_opts(argc, argv, program_options, program_usage_string);
    struct dump_dir *dd = dd_opendir(dump_dir_path, DD_OPEN_READONLY);
    if (!dd)
        xfunc_die();

    problem_data_t *pd = create_problem_data_from_dump_dir(dd);
    dd_close(dd);
    if (!pd)
        xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    abrt_post_state_t *post_state = NULL;
    post_state = post_ureport(pd, url);
    free_problem_data(pd);

    if (post_state->http_resp_code != 200)
    {
        char *errmsg = post_state->curl_error_msg;
        if (errmsg && *errmsg)
        {
            error_msg("%s '%s'", errmsg, url);
            free_abrt_post_state(post_state);
            return 1;
        }
    }

    char *line = strtok(post_state->body, "\n");
    int ret = 0;
    while (line)
    {
        if (!prefixcmp(line, "ERROR "))
        {
            ret = 1;
            break;
        }

        if (!prefixcmp(line, "NEEDMORE"))
        {
            log("%s", line);
            ret = 0;
            break;
        }

        line = strtok(NULL, "\n");
    }

    free_abrt_post_state(post_state);

    return ret;
}
