/* Copyright (C) 2019  Red Hat, Inc.
 *
 * libreport is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libreport is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libreport.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <glib-object.h>
#include <json.h>
#include <libreport_curl.h>
#include <ureport.h>
#include <utils.h>

#define BTHASH_URL_SUFFIX "reports/bthash/"

/*
 * uReport server response
 */
struct _UReportServerResponse
{
    bool is_error;  ///< True if server replied with error response
    char *value;    ///< Value of the response
    char *message;  ///< Additional message
    char *bthash;   ///< uReport's server side identifier
    GList *reported_to_list; ///< Known external reports for uReport
                                 ///< in *reported_to* format
    char *solution; ///< URL pointing to solution for uReport
};

G_DEFINE_BOXED_TYPE(UReportServerResponse, ureport_server_response,
                    ureport_server_response_dup, ureport_server_response_destroy)

char *
ureport_server_response_get_bthash(UReportServerResponse *self)
{
    g_return_val_if_fail(NULL != self, NULL);

    return g_strdup(self->bthash);
}

bool
ureport_server_response_get_is_error(UReportServerResponse *self)
{
    g_return_val_if_fail(NULL != self, false);

    return self->is_error;
}

char *
ureport_server_response_get_message(UReportServerResponse *self)
{
    g_return_val_if_fail(NULL != self, NULL);

    return g_strdup(self->message);
}

char *
ureport_server_response_get_report_url(UReportServerResponse *response,
                                       UReportServerConfig *config)
{
    g_autofree char *url = NULL;
    g_autofree char *bthash_url = NULL;

    url = ureport_server_config_get_url(config);
    bthash_url = g_build_path(G_DIR_SEPARATOR_S, url, BTHASH_URL_SUFFIX, NULL);

    return g_build_path(G_DIR_SEPARATOR_S, bthash_url, response->bthash, NULL);
}

char *
ureport_server_response_get_value(UReportServerResponse *self)
{
    g_return_val_if_fail(NULL != self, NULL);

    return g_strdup(self->value);
}

GList *
ureport_server_response_get_reported_to_list(UReportServerResponse *self)
{
    g_return_val_if_fail(NULL != self, NULL);

    if (NULL == self->reported_to_list)
    {
        return NULL;
    }

    return g_list_copy(self->reported_to_list);
}

/* reported_to json element should be a list of structures
   {
     "reporter": "Bugzilla",
     "type": "url",
     "value": "https://bugzilla.redhat.com/show_bug.cgi?id=XYZ"
   }
 */
static GList *
parse_reported_to_from_json_list(struct json_object *list)
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

        if (!json_object_object_get_ex(list_elem, "reporter", &struct_elem))
            continue;

        reporter = json_object_get_string(struct_elem);
        if (!reporter)
            continue;

        if (!json_object_object_get_ex(list_elem, "value", &struct_elem))
            continue;

        value = json_object_get_string(struct_elem);
        if (!value)
            continue;

        if (!json_object_object_get_ex(list_elem, "type", &struct_elem))
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

        reported_to_line = g_strdup_printf("%s: %s%s", reporter, prefix, value);
        free(prefix);

        result = g_list_append(result, reported_to_line);
    }

    return result;
}

static char *
parse_solution_from_json_list(struct json_object  *list,
                              GList              **reported_to)
{
    unsigned length;
    json_object *list_elem;
    json_object *struct_elem;
    struct strbuf *solution_buf;

    length = json_object_array_length(list);
    solution_buf = strbuf_new();

    const char *one_format = _("Your problem seems to be caused by %s\n\n%s\n");
    if (length > 1)
    {
        strbuf_append_str(solution_buf, _("Your problem seems to be caused by one of the following:\n"));
        one_format = "\n* %s\n\n%s\n";
    }

    bool empty = true;
    for (unsigned i = 0; i < length; ++i)
    {
        const char *cause;
        const char *note;
        const char *url;

        list_elem = json_object_array_get_idx(list, i);
        if (!list_elem)
            continue;

        if (!json_object_object_get_ex(list_elem, "cause", &struct_elem))
            continue;

        cause = json_object_get_string(struct_elem);
        if (!cause)
            continue;

        if (!json_object_object_get_ex(list_elem, "note", &struct_elem))
            continue;

        note = json_object_get_string(struct_elem);
        if (!note)
            continue;

        empty = false;
        strbuf_append_strf(solution_buf, one_format, cause, note);

        if (!json_object_object_get_ex(list_elem, "url", &struct_elem))
            continue;

        url = json_object_get_string(struct_elem);
        if (url)
        {
            char *reported_to_line = g_strdup_printf("%s: URL=%s", cause, url);
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

/*
 * Reponse samples:
 * {"error":"field 'foo' is required"}
 * {"response":"true"}
 * {"response":"false"}
 */
static UReportServerResponse *
ureport_server_parse_json(json_object *json)
{
    json_object *obj = NULL;
    if (json_object_object_get_ex(json, "error", &obj))
    {
        UReportServerResponse *response;

        response = ureport_server_response_new();

        response->is_error = true;
        /*
         * Used to use json_object_to_json_string(obj), but it returns
         * the string in quote marks (") - IOW, json-formatted string.
         */
        response->value = g_strdup(json_object_get_string(obj));
        return response;
    }

    if (json_object_object_get_ex(json, "result", &obj))
    {
        UReportServerResponse *response;

        response = ureport_server_response_new();

        response->value = g_strdup(json_object_get_string(obj));

        json_object *message = NULL;
        if (json_object_object_get_ex(json, "message", &message))
            response->message = g_strdup(json_object_get_string(message));

        json_object *bthash = NULL;
        if (json_object_object_get_ex(json, "bthash", &bthash))
            response->bthash = g_strdup(json_object_get_string(bthash));

        json_object *reported_to_list = NULL;
        if (json_object_object_get_ex(json, "reported_to", &reported_to_list))
            response->reported_to_list =
                parse_reported_to_from_json_list(reported_to_list);

        json_object *solutions = NULL;
        if (json_object_object_get_ex(json, "solutions", &solutions))
            response->solution =
                parse_solution_from_json_list(solutions, &(response->reported_to_list));

        return response;
    }

    return NULL;
}

UReportServerResponse *
ureport_server_response_new_from_reply(post_state_t        *post_state,
                                       UReportServerConfig *config)
{
    g_autofree char *url = NULL;
    json_object *json;
    UReportServerResponse *response;

    g_return_val_if_fail(NULL != post_state, NULL);

    url = ureport_server_config_get_url(config);

    /* Previously, the condition here was (post_state->errmsg[0] != '\0')
     * however when the server asks for optional client authentication and we do not have the certificates,
     * then post_state->errmsg contains "NSS: client certificate not found (nickname not specified)" even though
     * the request succeeded.
     */
    if (post_state->curl_result != CURLE_OK)
    {
        if (strcmp(post_state->errmsg, "") != 0)
            error_msg(_("Failed to upload uReport to the server '%s' with curl: %s"),
                                                                    url,
                                                                    post_state->errmsg);
        else
            error_msg(_("Failed to upload uReport to the server '%s'"), url);

        if (post_state->curl_error_msg != NULL && strcmp(post_state->curl_error_msg, "") != 0)
            error_msg(_("Error: %s"), post_state->curl_error_msg);

        return NULL;
    }

    if (post_state->http_resp_code == 404)
    {
        error_msg(_("The URL '%s' does not exist (got error 404 from server)"), url);
        return NULL;
    }

    if (post_state->http_resp_code == 500)
    {
        error_msg(_("The server at '%s' encountered an internal error (got error 500)"), url);
        return NULL;
    }

    if (post_state->http_resp_code == 503)
    {
        error_msg(_("The server at '%s' currently can't handle the request (got error 503)"), url);
        return NULL;
    }

    if (post_state->http_resp_code != 202
            && post_state->http_resp_code != 400
            && post_state->http_resp_code != 413)
    {
        /* can't print better error message */
        error_msg(_("Unexpected HTTP response from '%s': %d"), url, post_state->http_resp_code);
        log_notice("%s", post_state->body);
        return NULL;
    }

    json = json_tokener_parse(post_state->body);
    if (json == NULL)
    {
        error_msg(_("Unable to parse response from ureport server at '%s'"), url);
        log_notice("%s", post_state->body);
        json_object_put(json);
        return NULL;
    }

    response = ureport_server_parse_json(json);

    json_object_put(json);

    if (!response)
        error_msg(_("The response from '%s' has invalid format"), url);
    else if ((post_state->http_resp_code == 202 && response->is_error)
                || (post_state->http_resp_code != 202 && !response->is_error))
    {
        /* HTTP CODE 202 means that call was successful but the response */
        /* has an error message */
        error_msg(_("Type mismatch has been detected in the response from '%s'"), url);
    }

    return response;
}

UReportServerResponse *
ureport_server_response_new(void)
{
    return g_new0(UReportServerResponse, 1);
}

UReportServerResponse *
ureport_server_response_dup(UReportServerResponse *self)
{
    UReportServerResponse *response;

    g_return_val_if_fail (NULL != self, NULL);

    response = ureport_server_response_new();

    response->is_error = self->is_error;
    response->value = g_strdup(self->value);
    response->message = g_strdup(self->message);
    response->bthash = g_strdup(self->bthash);
    response->reported_to_list = g_list_copy_deep(self->reported_to_list,
                                                  report_string_list_copy_func,
                                                  NULL);

    return response;
}

void
ureport_server_response_destroy(UReportServerResponse *response)
{
    g_return_if_fail(NULL != response);

    g_clear_pointer(&response->solution, g_free);
    g_clear_pointer(&response->bthash, g_free);
    g_clear_pointer(&response->message, g_free);
    g_clear_pointer(&response->value, g_free);

    g_list_free_full(response->reported_to_list, g_free);
    response->reported_to_list = NULL;

    g_free(response);
}

bool
ureport_server_response_save_in_dump_dir(UReportServerResponse *response,
                                         const char            *dump_dir_path,
                                         UReportServerConfig   *config)
{
    struct dump_dir *dd = dd_opendir(dump_dir_path, /* flags */ 0);
    if (!dd)
        return false;

    if (response->bthash)
    {
        {
            report_result_t *result;

            result = report_result_new_with_label_from_env("uReport");

            report_result_set_bthash(result, response->bthash);

            add_reported_to_entry(dd, result);

            report_result_free(result);
        }

        {
            report_result_t *result;
            char *url;

            result = report_result_new_with_label_from_env("ABRT Server");
            url = ureport_server_response_get_report_url(response, config);

            report_result_set_url(result, url);

            add_reported_to_entry(dd, result);

            free(url);
            report_result_free(result);
        }
    }

    if (response->reported_to_list)
    {
        for (GList *e = response->reported_to_list; e; e = g_list_next(e))
        {
            char *workflow;

            workflow = getenv("LIBREPORT_WORKFLOW");
            if (NULL == workflow)
            {
                add_reported_to(dd, e->data);
            }
            else
            {
                g_autofree char *line = NULL;

                line = g_strdup_printf("%s WORKFLOW=%s", (const char *)e->data, workflow);

                add_reported_to(dd, line);
            }
        }
    }

    if (response->solution)
        dd_save_text(dd, FILENAME_NOT_REPORTABLE, response->solution);

    dd_close(dd);
    return true;
}

