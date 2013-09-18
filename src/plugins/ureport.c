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

#define SERVER_URL "https://retrace.fedoraproject.org/faf"
#define REPORT_URL_SFX "reports/new/"
#define ATTACH_URL_SFX "reports/attach/"
#define BTHASH_URL_SFX "reports/bthash/"

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

struct ureport_server_response {
    bool is_error;
    char *value;
    char *message;
    char *bthash;
    GList *reported_to_list;
    char *solution;
};

void free_ureport_server_response(struct ureport_server_response *resp)
{
    if (!resp)
        return;

    free(resp->solution);
    g_list_free_full(resp->reported_to_list, g_free);
    free(resp->bthash);
    free(resp->message);
    free(resp->value);
    free(resp);
}

static char *parse_solution_from_json_list(struct json_object *list, GList **reported_to)
{
    json_object *list_elem, *struct_elem;
    const char *cause, *note, *url;
    struct strbuf *solution_buf = strbuf_new();

    const unsigned length = json_object_array_length(list);

    const char *one_format = _("Your problem seems to be caused by %s\n\n%s\n");
    if (length > 1)
    {
        strbuf_append_str(solution_buf, _("Your problem seems to be caused by one of the following:\n"));
        one_format = "\n* %s\n\n%s\n";
    }

    bool empty = true;
    for (unsigned i = 0; i < length; ++i)
    {
        list_elem = json_object_array_get_idx(list, i);
        if (!list_elem)
            continue;

        struct_elem = json_object_object_get(list_elem, "cause");
        if (!struct_elem)
            continue;

        cause = json_object_get_string(struct_elem);
        if (!cause)
            continue;

        struct_elem = json_object_object_get(list_elem, "note");
        if (!struct_elem)
            continue;

        note = json_object_get_string(struct_elem);
        if (!note)
            continue;

        empty = false;
        strbuf_append_strf(solution_buf, one_format, cause, note);

        struct_elem = json_object_object_get(list_elem, "url");
        if (!struct_elem)
            continue;

        url = json_object_get_string(struct_elem);
        if (url)
        {
            char *reported_to_line = xasprintf("%s: URL=%s", cause, url);
            *reported_to = g_list_append(*reported_to, reported_to_line);
        }
    }

    if (empty)
    {
        strbuf_free(solution_buf);
        return NULL;
    }

    return strbuf_free_nobuf(solution_buf);
}

/* reported_to json element should be a list of structures
{ "reporter": "Bugzilla",
  "type": "url",
  "value": "https://bugzilla.redhat.com/show_bug.cgi?id=XYZ" } */
static GList *parse_reported_to_from_json_list(struct json_object *list)
{
    int i;
    json_object *list_elem, *struct_elem;
    const char *reporter, *value, *type;
    char *reported_to_line, *prefix;
    GList *result = NULL;

    for (i = 0; i < json_object_array_length(list); ++i)
    {
        prefix = NULL;
        list_elem = json_object_array_get_idx(list, i);
        if (!list_elem)
            continue;

        struct_elem = json_object_object_get(list_elem, "reporter");
        if (!struct_elem)
            continue;

        reporter = json_object_get_string(struct_elem);
        if (!reporter)
            continue;

        struct_elem = json_object_object_get(list_elem, "value");
        if (!struct_elem)
            continue;

        value = json_object_get_string(struct_elem);
        if (!value)
            continue;

        struct_elem = json_object_object_get(list_elem, "type");
        if (!struct_elem)
            continue;

        type = json_object_get_string(struct_elem);
        if (type)
        {
            if (strcasecmp("url", type) == 0)
                prefix = xstrdup("URL=");
            else if (strcasecmp("bthash", type) == 0)
                prefix = xstrdup("BTHASH=");
        }

        if (!prefix)
            prefix = xstrdup("");

        reported_to_line = xasprintf("%s: %s%s", reporter, prefix, value);
        free(prefix);

        result = g_list_append(result, reported_to_line);
    }

    return result;
}

/*
 * Reponse samples:
 * {"error":"field 'foo' is required"}
 * {"response":"true"}
 * {"response":"false"}
 */
static struct ureport_server_response *ureport_server_parse_json(json_object *json)
{
    json_object *obj = json_object_object_get(json, "error");

    if (obj)
    {
        struct ureport_server_response *out_response = xzalloc(sizeof(*out_response));
        out_response->is_error = true;
        /*
         * Used to use json_object_to_json_string(obj), but it returns
         * the string in quote marks (") - IOW, json-formatted string.
         */
        out_response->value = xstrdup(json_object_get_string(obj));
        return out_response;
    }

    obj = json_object_object_get(json, "result");

    if (obj)
    {
        struct ureport_server_response *out_response = xzalloc(sizeof(*out_response));
        out_response->value = xstrdup(json_object_get_string(obj));

        json_object *message = json_object_object_get(json, "message");
        if (message)
            out_response->message = xstrdup(json_object_get_string(message));

        json_object *bthash = json_object_object_get(json, "bthash");
        if (bthash)
            out_response->bthash = xstrdup(json_object_get_string(bthash));

        json_object *reported_to_list = json_object_object_get(json, "reported_to");
        if (reported_to_list)
            out_response->reported_to_list = parse_reported_to_from_json_list(reported_to_list);

        json_object *solutions = json_object_object_get(json, "solutions");
        if (solutions)
            out_response->solution = parse_solution_from_json_list(solutions, &(out_response->reported_to_list));

        return out_response;
    }

    return NULL;
}

static struct ureport_server_response *get_server_response(post_state_t *post_state, struct ureport_server_config *config)
{
    if (post_state->errmsg[0] != '\0')
    {
        error_msg(_("Failed to upload uReport to the server '%s' with curl: %s"), config->ur_url, post_state->errmsg);
        return NULL;
    }

    if (post_state->http_resp_code == 404)
    {
        error_msg(_("The URL '%s' does not exist (got error 404 from server)"), config->ur_url);
        return NULL;
    }

    if (post_state->http_resp_code == 500)
    {
        error_msg(_("The server at '%s' encountered an internal error (got error 500)"), config->ur_url);
        return NULL;
    }

    if (post_state->http_resp_code == 503)
    {
        error_msg(_("The server at '%s' is currently can't handle the request (got error 503)"), config->ur_url);
        return NULL;
    }

    if (post_state->http_resp_code != 202
            && post_state->http_resp_code != 400
            && post_state->http_resp_code != 413)
    {
        /* can't print better error message */
        error_msg(_("Unexpected HTTP response from '%s': %d\n%s"), config->ur_url, post_state->http_resp_code, post_state->body);
        return NULL;
    }

    json_object *const json = json_tokener_parse(post_state->body);

    if (is_error(json))
    {
        error_msg(_("Unable to parse response from ureport server at '%s':\n%s"), config->ur_url, post_state->body);
        json_object_put(json);
        return NULL;
    }

    struct ureport_server_response *response = ureport_server_parse_json(json);
    json_object_put(json);

    if (!response)
        error_msg(_("The response from '%s' has invalid format"), config->ur_url);
    else if ((post_state->http_resp_code == 202 && response->is_error)
                || (post_state->http_resp_code != 202 && !response->is_error))
    {
        /* HTTP CODE 202 means that call was successful but the response */
        /* has an error message */
        error_msg(_("Type mismatch has been detected in the response from '%s'"), config->ur_url);
    }

    return response;
}

static bool perform_attach(struct ureport_server_config *config, const char *ureport_hash, int rhbz_bug)
{
    char *dest_url = concat_path_file(config->ur_url, ATTACH_URL_SFX);
    const char *old_url = config->ur_url;
    config->ur_url = dest_url;
    post_state_t *post_state = ureport_attach_rhbz(ureport_hash, rhbz_bug, config);
    config->ur_url = old_url;
    free(dest_url);

    struct ureport_server_response *resp = get_server_response(post_state, config);
    free_post_state(post_state);
    /* don't use str_bo_bool() because we require "true" string */
    const int result = !resp || resp->is_error || strcmp(resp->value,"true") != 0;

    if (resp && resp->is_error)
    {
        error_msg(_("The server at '%s' responded with an error: '%s'"), config->ur_url, resp->value);
    }

    free_ureport_server_response(resp);

    return result;
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
#if ENABLE_NLS
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
#endif

    abrt_init(argv);

    struct ureport_server_config config = {
        .ur_url = SERVER_URL,
        .ur_ssl_verify = true,
    };

    bool insecure = !config.ur_ssl_verify;
    bool attach_reported_to = false;
    const char *dump_dir_path = ".";
    const char *ureport_hash = NULL;
    int rhbz_bug = -1;
    struct dump_dir *dd = NULL;
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT__DUMP_DIR(&dump_dir_path),
        OPT_STRING('u', "url", &config.ur_url, "URL", _("Specify server URL")),
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
        return perform_attach(&config, ureport_hash, rhbz_bug);
    if (ureport_hash && rhbz_bug <= 0)
        error_msg_and_die(_("You need to specify bug ID to attach."));
    if (!ureport_hash && rhbz_bug > 0)
        error_msg_and_die(_("You need to specify bthash of the uReport to attach."));

    /* -r */
    if (attach_reported_to)
    {
        dd = dd_opendir(dump_dir_path, DD_OPEN_READONLY);
        if (!dd)
            xfunc_die();

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
            error_msg_and_die(_("Unable to find bug ID in bugzilla URL '%s'"), bz_result->url);
        bugid_ptr += strlen("show_bug.cgi?id=");
        int bugid;
        /* we're just reading int, sscanf works fine */
        if (sscanf(bugid_ptr, "%d", &bugid) != 1)
            error_msg_and_die(_("Unable to parse bug ID from bugzilla URL '%s'"), bz_result->url);

        free_report_result(bz_result);

        const int result = perform_attach(&config, bthash, bugid);

        free(bthash);
        return result;
    }

    /* -b, -a nor -r were specified - upload uReport from dump_dir */
    int ret = 1; /* "failure" (for now) */
    const char *server_url = config.ur_url;
    char *dest_url = concat_path_file(config.ur_url, REPORT_URL_SFX);
    config.ur_url = dest_url;

    char *json_ureport = ureport_from_dump_dir(dump_dir_path);
    if (!json_ureport)
    {
        error_msg(_("Not uploading an empty uReport"));
        goto format_err;
    }

    post_state = post_ureport(json_ureport, &config);
    free(json_ureport);

    if (!post_state)
    {
        error_msg(_("Failed on submitting the problem"));
        goto format_err;
    }

    struct ureport_server_response *response = get_server_response(post_state, &config);

    if (!response)
        goto format_err;

    if (!response->is_error)
    {
        VERB1 log("is known: %s", response->value);
        ret = 0; /* "success" */

        dd = dd_opendir(dump_dir_path, /* flags */ 0);
        if (!dd)
            xfunc_die();

        if (response->bthash)
        {
            char *msg = xasprintf("uReport: BTHASH=%s", response->bthash);
            add_reported_to(dd, msg);
            free(msg);

            char *bthash_url = concat_path_file(server_url, BTHASH_URL_SFX);
            msg = xasprintf("ABRT Server: URL=%s%s", bthash_url, response->bthash);
            add_reported_to(dd, msg);
            free(msg);
            free(bthash_url);
        }

        if (response->reported_to_list)
        {
            for (GList *e = response->reported_to_list; e; e = g_list_next(e))
                add_reported_to(dd, e->data);
        }

        if (response->solution)
            dd_save_text(dd, FILENAME_NOT_REPORTABLE, response->solution);

        dd_close(dd);

        /* If a reported problem is not known then emit NEEDMORE */
        if (strcmp("true", response->value) == 0)
        {
            log(_("This problem has already been reported."));
            if (response->message)
                log(response->message);

            ret = EXIT_STOP_EVENT_RUN;
        }
    }
    else
    {
        error_msg(_("Server responded with an error: '%s'"), response->value);
    }

    free_ureport_server_response(response);

format_err:
    free_post_state(post_state);
    free(dest_url);

    return ret;
}
