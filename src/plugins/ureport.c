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

#include <json/json.h>
#include "internal_libreport.h"
#include "ureport.h"
#include "libreport_curl.h"

/*
 * Loads uReport configuration from various sources.
 *
 * Replaces a value of an already configured option only if the
 * option was found in a configuration source.
 *
 * @param config a server configuration to be populated
 */
static void load_ureport_server_config(struct ureport_server_config *config)
{
    const char *environ;

    environ = getenv("uReport_URL");
    config->ur_url = environ ? environ : config->ur_url;

    environ = getenv("uReport_SSLVerify");
    config->ur_ssl_verify = environ ? string_to_bool(environ) : config->ur_ssl_verify;
}


enum response_type
{
    UREPORT_SERVER_RESP_UNKNOWN_TYPE,
    UREPORT_SERVER_RESP_KNOWN,
    UREPORT_SERVER_RESP_ERROR,
};

struct ureport_server_response {
    enum response_type type;
    const char *value;
};

/*
 * Reponse samples:
 * {"error":"field 'foo' is required"}
 * {"response":"true"}
 * {"response":"false"}
 */
static bool ureport_server_parse_json(json_object *json, struct ureport_server_response *out_response)
{
    json_object *obj = json_object_object_get(json, "error");

    if (obj)
    {
        out_response->type = UREPORT_SERVER_RESP_ERROR;
        out_response->value = json_object_to_json_string(obj);
        return true;
    }

    obj = json_object_object_get(json, "result");

    if (obj)
    {
        out_response->type = UREPORT_SERVER_RESP_KNOWN;
        out_response->value = json_object_to_json_string(obj);
        return true;
    }

    out_response->type = UREPORT_SERVER_RESP_UNKNOWN_TYPE;
    return false;
}

int main(int argc, char **argv)
{
    abrt_init(argv);

    struct ureport_server_config config = {
        .ur_url = "https://retrace.fedoraproject.org/faf/reports/new/",
        .ur_ssl_verify = true,
    };

    bool insecure = !config.ur_ssl_verify;
    const char *dump_dir_path = ".";
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT__DUMP_DIR(&dump_dir_path),
        OPT_STRING('u', "url", &config.ur_url, "URL", _("Specify url")),
        OPT_BOOL('k', "insecure", &insecure,
                          _("Allow insecure connection to ureport server")),
        OPT_END(),
    };

    const char *program_usage_string = _(
        "& [-v] [-u URL] [-k] -d DIR\n"
        "\n"
        "Upload micro report"
    );

    parse_opts(argc, argv, program_options, program_usage_string);
    struct dump_dir *dd = dd_opendir(dump_dir_path, DD_OPEN_READONLY);
    if (!dd)
        xfunc_die();

    config.ur_ssl_verify = !insecure;
    load_ureport_server_config(&config);

    problem_data_t *pd = create_problem_data_from_dump_dir(dd);
    dd_close(dd);
    if (!pd)
        xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    post_state_t *post_state = NULL;
    post_state = post_ureport(pd, &config);
    problem_data_free(pd);

    if (post_state->http_resp_code != 200)
    {
        char *errmsg = post_state->curl_error_msg;
        if (errmsg && *errmsg)
        {
            error_msg("%s '%s'", errmsg, config.ur_url);
            free_post_state(post_state);
            return 1;
        }
    }

    int ret = 1; /* return 1 by default */
    json_object *const json = json_tokener_parse(post_state->body);

    if (is_error(json))
    {
        error_msg("fatal: unable to parse response from ureport server");
        goto err;
    }

    struct ureport_server_response response = {
        .type=UREPORT_SERVER_RESP_UNKNOWN_TYPE,
        .value=NULL,
    };

    const bool is_valid_response = ureport_server_parse_json(json, &response);

    if (!is_valid_response)
    {
        error_msg("fatal: wrong format of response from ureport server");
        goto format_err;
    }

    switch (response.type)
    {
        case UREPORT_SERVER_RESP_KNOWN:
            VERB1 log("is known: %s", response.value);
            ret = 0;
            /* If a reported problem is not known then emit NEEDMORE */
            if (strcmp("true", response.value) == 0)
                log("THANKYOU");
            break;
        case UREPORT_SERVER_RESP_ERROR:
            VERB1 log("server side error: %s", response.value);
            ret = 1; /* just to be sure */
            break;
        case UREPORT_SERVER_RESP_UNKNOWN_TYPE:
            error_msg("invalid server response: %s", response.value);
            ret = 1; /* just to be sure */
            break;
        default:
            error_msg("reporter internal error: missing handler for response type");
            ret = 1; /* just to be sure */
            break;
    }

format_err:
    json_object_put(json);
err:
    free_post_state(post_state);

    return ret;
}
