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

#define REPORT_URL "https://retrace.fedoraproject.org/faf/reports/new/"
#define ATTACH_URL "https://retrace.fedoraproject.org/faf/reports/attach/"

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
    const char *message;
    const char *bthash;
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
        out_response->value = json_object_get_string(obj);

        json_object *message = json_object_object_get(json, "message");
        if (message)
            out_response->message = json_object_get_string(message);

        json_object *bthash = json_object_object_get(json, "bthash");
        if (bthash)
            out_response->bthash = json_object_get_string(bthash);

        return true;
    }

    out_response->type = UREPORT_SERVER_RESP_UNKNOWN_TYPE;
    return false;
}

static bool check_response_statuscode(post_state_t *post_state, 
                                      const char *url)
{
    if (post_state->http_resp_code != 202)
    {
        char *errmsg = post_state->curl_error_msg;
        if (errmsg && *errmsg)
            error_msg("%s '%s'", errmsg, url);
        else
            error_msg("Unexpected HTTP status code: %d",
                      post_state->http_resp_code);

        return false;
    }

    return true;
}

int main(int argc, char **argv)
{
    abrt_init(argv);

    struct ureport_server_config config = {
        .ur_url = NULL,
        .ur_ssl_verify = true,
    };

    bool insecure = !config.ur_ssl_verify;
    bool attach_reported_to = false;
    const char *dump_dir_path = ".";
    const char *ureport_hash = NULL;
    int rhbz_bug = -1;
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT__DUMP_DIR(&dump_dir_path),
        OPT_STRING('u', "url", &config.ur_url, "URL", _("Specify url")),
        OPT_BOOL('k', "insecure", &insecure,
                          _("Allow insecure connection to ureport server")),
        OPT_STRING('a', "attach", &ureport_hash, "BTHASH",
                          _("bthash of uReport to attach")),
        OPT_INTEGER('b', "bug-id", &rhbz_bug,
                          _("Attach RHBZ bug (requires -a)")),
        OPT_BOOL('r', "attach-reported-to", &attach_reported_to,
                          _("Attach contents of reported_to")),
        OPT_END(),
    };

    const char *program_usage_string = _(
        "& [-v] [-u URL] [-k] [-a bthash -b bug-id] [-r] [-d DIR]\n"
        "\n"
        "Upload micro report or add an attachment to a micro report"
    );

    parse_opts(argc, argv, program_options, program_usage_string);

    config.ur_ssl_verify = !insecure;
    load_ureport_server_config(&config);
    post_state_t *post_state = NULL;

    /* we either need both -b & -a or none of them */
    if (ureport_hash && rhbz_bug > 0)
    {
        if (!config.ur_url)
            config.ur_url = ATTACH_URL;

        post_state = ureport_attach_rhbz(ureport_hash, rhbz_bug, &config);
        if (!check_response_statuscode(post_state, config.ur_url))
        {
            free_post_state(post_state);
            return 1;
        }

        free_post_state(post_state);
        return 0;
    }
    else if (ureport_hash && rhbz_bug <= 0)
        error_msg_and_die(_("You need to specify bug ID to attach."));
    else if (!ureport_hash && rhbz_bug > 0)
        error_msg_and_die(_("You need to specify bthash of the uReport to attach."));

    struct dump_dir *dd = dd_opendir(dump_dir_path, DD_OPEN_READONLY);
    if (!dd)
        xfunc_die();

    /* -r */
    if (attach_reported_to)
    {
        if (!config.ur_url)
            config.ur_url = ATTACH_URL;

        report_result_t *ureport_result = find_in_reported_to(dd, "uReport");
        report_result_t *bz_result = find_in_reported_to(dd, "Bugzilla");

        dd_close(dd);

        if (!ureport_result || !ureport_result->bthash)
            error_msg_and_die(_("This problem does not have an uReport assigned."));

        if (!bz_result || !bz_result->url)
            error_msg_and_die(_("This problem has not been reported to Bugzilla."));

        char *bthash = xstrdup(ureport_result->bthash);
        free_report_result(ureport_result);

        char *bugid_ptr = strstr(bz_result->url, "show_bug.cgi?id=");
        if (!bugid_ptr)
            error_msg_and_die(_("Unable to find bug ID in bugzilla URL."));

        bugid_ptr += strlen("show_bug.cgi?id=");
        int bugid;
        /* we're just reading int, sscanf works fine */
        if (sscanf(bugid_ptr, "%d", &bugid) != 1)
            error_msg_and_die(_("Unable to parse bug ID from bugzilla URL."));

        free_report_result(bz_result);

        post_state = ureport_attach_rhbz(bthash, bugid, &config);
        free(bthash);

        int ret = !check_response_statuscode(post_state, config.ur_url);
        free_post_state(post_state);
        return ret;
    }

    /* -b, -a nor -r were specified - upload uReport from dump_dir */
    if (!config.ur_url)
        config.ur_url = REPORT_URL;

    problem_data_t *pd = create_problem_data_from_dump_dir(dd);
    dd_close(dd);
    if (!pd)
        xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    post_state = post_ureport(pd, &config);
    problem_data_free(pd);

    if (!check_response_statuscode(post_state, config.ur_url))
    {
        free_post_state(post_state);
        return 1;
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
        .message=NULL,
        .bthash=NULL,
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

            if (response.bthash)
            {
                dd = dd_opendir(dump_dir_path, /* flags */ 0);
                if (!dd)
                    xfunc_die();

                char *msg = xasprintf("uReport: BTHASH=%s", response.bthash);
                add_reported_to(dd, msg);
                free(msg);
                dd_close(dd);
            }

            /* If a reported problem is not known then emit NEEDMORE */
            if (strcmp("true", response.value) == 0)
            {
                log("This problem has already been reported.");
                if (response.message)
                    log(response.message);
                log("THANKYOU");
            }
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
