/*
    Copyright (C) 2012,2014  ABRT team
    Copyright (C) 2012,2014  RedHat Inc

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
#include <json.h>

#include <satyr/abrt.h>
#include <satyr/report.h>

#include "internal_libreport.h"
#include "client.h"
#include "ureport.h"
#include "libreport_curl.h"

#define DESTROYED_POINTER (void *)0xdeadbeef

#define BTHASH_URL_SFX "reports/bthash/"

static char *
puppet_config_print(const char *key)
{
    g_autofree char *command = g_strdup_printf("puppet config print %s", key);
    char *result = libreport_run_in_shell_and_save_output(0, command, NULL, NULL);

    /* libreport_run_in_shell_and_save_output always returns non-NULL */
    if (result[0] != '/')
        goto error;

    char *newline = strchrnul(result, '\n');
    if (!newline)
        goto error;

    *newline = '\0';
    return result;
error:
    free(result);
    error_msg_and_die("Unable to determine puppet %s path (puppet not installed?)", key);
}

void
libreport_ureport_server_config_set_url(struct ureport_server_config *config,
                              char *server_url)
{
    free(config->ur_url);
    config->ur_url = server_url;
}

void
libreport_ureport_server_config_set_client_auth(struct ureport_server_config *config,
                                      const char *client_auth)
{
    if (client_auth == NULL)
        return;

    if (strcmp(client_auth, "") == 0)
    {
        free(config->ur_client_cert);
        config->ur_client_cert = NULL;

        free(config->ur_client_key);
        config->ur_client_key = NULL;

        log_notice("Not using client authentication");
    }
    else if (strcmp(client_auth, "puppet") == 0)
    {
        config->ur_client_cert = puppet_config_print("hostcert");
        config->ur_client_key = puppet_config_print("hostprivkey");
    }
    else
    {
        g_autofree char *scratch = g_strdup(client_auth);
        config->ur_client_cert = g_strdup(strtok(scratch, ":"));
        config->ur_client_key = g_strdup(strtok(NULL, ":"));

        if (config->ur_client_cert == NULL || config->ur_client_key == NULL)
            error_msg_and_die("Invalid client authentication specification");
    }

    if (config->ur_client_cert && config->ur_client_key)
    {
        log_notice("Using client certificate: %s", config->ur_client_cert);
        log_notice("Using client private key: %s", config->ur_client_key);

        free(config->ur_username);
        config->ur_username = NULL;

        free(config->ur_password);
        config->ur_password = NULL;
    }
}

void
libreport_ureport_server_config_set_basic_auth(struct ureport_server_config *config,
                                     const char *login, const char *password)
{
    libreport_ureport_server_config_set_client_auth(config, "");

    free(config->ur_username);
    config->ur_username = g_strdup(login);

    free(config->ur_password);
    config->ur_password = g_strdup(password);
}

void
ureport_server_config_load_basic_auth(struct ureport_server_config *config,
                                      const char *http_auth_pref)
{
    if (http_auth_pref == NULL)
        return;

    g_autoptr(GHashTable) settings = NULL;

    g_autofree const char *username = NULL;
    const char *password_from_http_auth_pref = NULL;

    username = g_strdup(http_auth_pref);
    password_from_http_auth_pref = strchr(username, ':');

    if (password_from_http_auth_pref != NULL) {
        /* It is "char *", see strchr() few lines above. */
        *((char *)(password_from_http_auth_pref++)) = '\0';
    }

    if (password_from_http_auth_pref == NULL) {
        g_autofree char *message = g_strdup_printf("Please provide uReport server password for user '%s':", username);
        g_autofree char *password_from_input = libreport_ask_password(message);

        if (strcmp(password_from_input, "") == 0) {
            error_msg_and_die("Cannot continue without uReport server password!");
        }

        libreport_ureport_server_config_set_basic_auth(config, username, password_from_input);
    } else {
        libreport_ureport_server_config_set_basic_auth(config, username, password_from_http_auth_pref);
    }

}

void
libreport_ureport_server_config_load(struct ureport_server_config *config,
                           GHashTable *settings)
{
    UREPORT_OPTION_VALUE_FROM_CONF(settings, "URL", config->ur_url, g_strdup);
    UREPORT_OPTION_VALUE_FROM_CONF(settings, "SSLVerify", config->ur_ssl_verify, libreport_string_to_bool);

    const char *http_auth_pref = NULL;
    UREPORT_OPTION_VALUE_FROM_CONF(settings, "HTTPAuth", http_auth_pref, (const char *));
    ureport_server_config_load_basic_auth(config, http_auth_pref);

    const char *client_auth = NULL;
    if (http_auth_pref == NULL)
    {
        UREPORT_OPTION_VALUE_FROM_CONF(settings, "SSLClientAuth", client_auth, (const char *));
        libreport_ureport_server_config_set_client_auth(config, client_auth);
    }

    /* If SSLClientAuth is configured, include the auth items by default. */
    bool include_auth = config->ur_client_cert != NULL || config->ur_username != NULL;
    UREPORT_OPTION_VALUE_FROM_CONF(settings, "IncludeAuthData", include_auth, libreport_string_to_bool);

    if (include_auth)
    {
        const char *auth_items = NULL;
        UREPORT_OPTION_VALUE_FROM_CONF(settings, "AuthDataItems", auth_items, (const char *));
        config->ur_prefs.urp_auth_items = libreport_parse_delimited_list(auth_items, ",");

        if (config->ur_prefs.urp_auth_items == NULL)
            log_warning("IncludeAuthData set to 'yes' but AuthDataItems is empty.");
    }
}

void
libreport_ureport_server_config_init(struct ureport_server_config *config)
{
    config->ur_url = NULL;
    config->ur_ssl_verify = true;
    config->ur_client_cert = NULL;
    config->ur_client_key = NULL;
    config->ur_cert_authority_cert = NULL;
    config->ur_username = NULL;
    config->ur_password = NULL;

    config->ur_prefs.urp_auth_items = NULL;
    config->ur_prefs.urp_flags = 0;
}

void
libreport_ureport_server_config_destroy(struct ureport_server_config *config)
{
    free(config->ur_url);
    config->ur_url = DESTROYED_POINTER;

    free(config->ur_client_cert);
    config->ur_client_cert = DESTROYED_POINTER;

    free(config->ur_client_key);
    config->ur_client_key = DESTROYED_POINTER;

    free(config->ur_cert_authority_cert);
    config->ur_cert_authority_cert = DESTROYED_POINTER;

    free(config->ur_username);
    config->ur_username = DESTROYED_POINTER;

    free(config->ur_password);
    config->ur_password = DESTROYED_POINTER;

    g_list_free_full(config->ur_prefs.urp_auth_items, g_free);
    config->ur_prefs.urp_auth_items = DESTROYED_POINTER;
}

void
libreport_ureport_server_response_free(struct ureport_server_response *resp)
{
    if (!resp)
        return;

    free(resp->urr_solution);
    resp->urr_solution = DESTROYED_POINTER;

    g_list_free_full(resp->urr_reported_to_list, g_free);
    resp->urr_reported_to_list = DESTROYED_POINTER;

    free(resp->urr_bthash);
    resp->urr_bthash = DESTROYED_POINTER;

    free(resp->urr_message);
    resp->urr_message = DESTROYED_POINTER;

    free(resp->urr_value);
    resp->urr_value = DESTROYED_POINTER;

    free(resp);
}

static char *
parse_solution_from_json_list(struct json_object *list,
                              GList **reported_to)
{
    json_object *list_elem, *struct_elem;
    const char *cause, *note, *url;
    GString *solution_buf = g_string_new(NULL);

    const unsigned length = json_object_array_length(list);

    const char *one_format = _("Your problem seems to be caused by %s\n\n%s\n");
    if (length > 1)
    {
        g_string_append(solution_buf, _("Your problem seems to be caused by one of the following:\n"));
        one_format = "\n* %s\n\n%s\n";
    }

    bool empty = true;
    for (unsigned i = 0; i < length; ++i)
    {
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
        g_string_append_printf(solution_buf, one_format, cause, note);

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
        g_string_free(solution_buf, TRUE);
        return NULL;
    }

    return g_string_free(solution_buf, FALSE);
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
    char *reported_to_line = NULL;
    g_autofree char *prefix = NULL;
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
                prefix = g_strdup("URL=");
            else if (strcasecmp("bthash", type) == 0)
                prefix = g_strdup("BTHASH=");
        }

        if (!prefix)
            prefix = g_strdup("");

        reported_to_line = g_strdup_printf("%s: %s%s", reporter, prefix, value);

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
static struct ureport_server_response *
ureport_server_parse_json(json_object *json)
{
    json_object *obj = NULL;
    if (json_object_object_get_ex(json, "error", &obj))
    {
        struct ureport_server_response *out_response = g_new0(struct ureport_server_response, 1);
        out_response->urr_is_error = true;
        /*
         * Used to use json_object_to_json_string(obj), but it returns
         * the string in quote marks (") - IOW, json-formatted string.
         */
        out_response->urr_value = g_strdup(json_object_get_string(obj));
        return out_response;
    }

    if (json_object_object_get_ex(json, "result", &obj))
    {
        struct ureport_server_response *out_response = g_new0(struct ureport_server_response, 1);
        out_response->urr_value = g_strdup(json_object_get_string(obj));

        json_object *message = NULL;
        if (json_object_object_get_ex(json, "message", &message))
            out_response->urr_message = g_strdup(json_object_get_string(message));

        json_object *bthash = NULL;
        if (json_object_object_get_ex(json, "bthash", &bthash))
            out_response->urr_bthash = g_strdup(json_object_get_string(bthash));

        json_object *reported_to_list = NULL;
        if (json_object_object_get_ex(json, "reported_to", &reported_to_list))
            out_response->urr_reported_to_list =
                parse_reported_to_from_json_list(reported_to_list);

        json_object *solutions = NULL;
        if (json_object_object_get_ex(json, "solutions", &solutions))
            out_response->urr_solution =
                parse_solution_from_json_list(solutions, &(out_response->urr_reported_to_list));

        return out_response;
    }

    return NULL;
}

struct ureport_server_response *
libreport_ureport_server_response_from_reply(post_state_t *post_state,
                                   struct ureport_server_config *config)
{
    /* Previously, the condition here was (post_state->errmsg[0] != '\0')
     * however when the server asks for optional client authentication and we do not have the certificates,
     * then post_state->errmsg contains "NSS: client certificate not found (nickname not specified)" even though
     * the request succeeded.
     */
    if (post_state->curl_result != CURLE_OK)
    {
        if (strcmp(post_state->errmsg, "") != 0)
            error_msg(_("Failed to upload uReport to the server '%s' with curl: %s"),
                                                                    config->ur_url,
                                                                    post_state->errmsg);
        else
            error_msg(_("Failed to upload uReport to the server '%s'"), config->ur_url);

        if (post_state->curl_error_msg != NULL && strcmp(post_state->curl_error_msg, "") != 0)
            error_msg(_("Error: %s"), post_state->curl_error_msg);

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
        error_msg(_("The server at '%s' currently can't handle the request (got error 503)"), config->ur_url);
        return NULL;
    }

    if (post_state->http_resp_code != 202
            && post_state->http_resp_code != 400
            && post_state->http_resp_code != 413)
    {
        /* can't print better error message */
        error_msg(_("Unexpected HTTP response from '%s': %d"), config->ur_url, post_state->http_resp_code);
        log_notice("%s", post_state->body);
        return NULL;
    }

    json_object *const json = json_tokener_parse(post_state->body);

    if (json == NULL)
    {
        error_msg(_("Unable to parse response from ureport server at '%s'"), config->ur_url);
        log_notice("%s", post_state->body);
        json_object_put(json);
        return NULL;
    }

    struct ureport_server_response *response = ureport_server_parse_json(json);
    json_object_put(json);

    if (!response)
        error_msg(_("The response from '%s' has invalid format"), config->ur_url);
    else if ((post_state->http_resp_code == 202 && response->urr_is_error)
                || (post_state->http_resp_code != 202 && !response->urr_is_error))
    {
        /* HTTP CODE 202 means that call was successful but the response */
        /* has an error message */
        error_msg(_("Type mismatch has been detected in the response from '%s'"), config->ur_url);
    }

    return response;
}

bool
libreport_ureport_server_response_save_in_dump_dir(struct ureport_server_response *resp,
                                         const char *dump_dir_path,
                                         struct ureport_server_config *config)
{
    struct dump_dir *dd = dd_opendir(dump_dir_path, /* flags */ 0);
    if (!dd)
        return false;

    if (resp->urr_bthash)
    {
        {
            report_result_t *result;

            result = report_result_new_with_label_from_env("uReport");

            report_result_set_bthash(result, resp->urr_bthash);

            libreport_add_reported_to_entry(dd, result);

            report_result_free(result);
        }

        {
            report_result_t *result;
            g_autofree char *url = NULL;

            result = report_result_new_with_label_from_env("ABRT Server");
            url = libreport_ureport_server_response_get_report_url(resp, config);

            report_result_set_url(result, url);

            libreport_add_reported_to_entry(dd, result);

            report_result_free(result);
        }
    }

    if (resp->urr_reported_to_list)
    {
        for (GList *e = resp->urr_reported_to_list; e; e = g_list_next(e))
        {
            char *workflow;

            workflow = getenv("LIBREPORT_WORKFLOW");
            if (NULL == workflow)
            {
                libreport_add_reported_to(dd, e->data);
            }
            else
            {
                g_autofree char *line = NULL;

                line = g_strdup_printf("%s WORKFLOW=%s", (const char *)e->data, workflow);

                libreport_add_reported_to(dd, line);
            }
        }
    }

    if (resp->urr_solution)
        dd_save_text(dd, FILENAME_NOT_REPORTABLE, resp->urr_solution);

    dd_close(dd);
    return true;
}

char *
libreport_ureport_server_response_get_report_url(struct ureport_server_response *resp,
                                       struct ureport_server_config *config)
{
    g_autofree char *bthash_url = g_build_filename(config->ur_url ? config->ur_url : "", BTHASH_URL_SFX, NULL);
    char *report_url = g_build_filename(bthash_url, resp->urr_bthash, NULL);
    return report_url;
}

static void
ureport_add_str(struct json_object *ur, const char *key, const char *s)
{
    struct json_object *jstring = json_object_new_string(s);
    if (!jstring)
        libreport_die_out_of_memory();

    json_object_object_add(ur, key, jstring);
}

char *
libreport_ureport_from_dump_dir_ext(const char *dump_dir_path, const struct ureport_preferences *preferences)
{
    g_autofree char *error_message = NULL;
    struct sr_report *report = sr_abrt_report_from_dir(dump_dir_path,
                                                       &error_message);

    if (!report)
    {
        if (NULL == preferences || !(preferences->urp_flags & UREPORT_PREF_FLAG_RETURN_ON_FAILURE))
            error_msg_and_die("%s", error_message);

        log_notice("%s", error_message);
        return NULL;
    }

    if (preferences != NULL && preferences->urp_auth_items != NULL)
    {
        struct dump_dir *dd = dd_opendir(dump_dir_path, DD_OPEN_READONLY);
        if (!dd)
            libreport_xfunc_die(); /* dd_opendir() already printed an error message */

        GList *iter = preferences->urp_auth_items;
        for ( ; iter != NULL; iter = g_list_next(iter))
        {
            const char *key = (const char *)iter->data;
            g_autofree char *value = dd_load_text_ext(dd, key,
                    DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE | DD_FAIL_QUIETLY_ENOENT);

            if (value == NULL)
            {
                perror_msg("Cannot include '%s' in 'auth'", key);
                continue;
            }

            sr_report_add_auth(report, key, value);
        }

        dd_close(dd);
    }

    char *json_ureport = sr_report_to_json(report);
    sr_report_free(report);

    return json_ureport;
}

struct post_state *
libreport_ureport_do_post(const char *json, struct ureport_server_config *config,
                const char *url_sfx)
{
    int flags = POST_WANT_BODY | POST_WANT_ERROR_MSG;

    if (config->ur_ssl_verify)
        flags |= POST_WANT_SSL_VERIFY;

    struct post_state *post_state = new_post_state(flags);

    if (config->ur_client_cert && config->ur_client_key)
    {
        post_state->client_cert_path = config->ur_client_cert;
        post_state->client_key_path = config->ur_client_key;
        post_state->cert_authority_cert_path = config->ur_cert_authority_cert;
    }
    else if (config->ur_username && config->ur_password)
    {
        post_state->username = config->ur_username;
        post_state->password = config->ur_password;
    }

    const char *headers[] =
    {
        "Accept: application/json",
        "Connection: close",
        NULL,
    };
    g_autofree char *dest_url = g_build_filename(config->ur_url ? config->ur_url : "", url_sfx, NULL);

    post_string_as_form_data(post_state, dest_url, "application/json",
                             headers, json);

    /* Client authentication failed. Try again without client auth.
     * CURLE_SSL_CONNECT_ERROR - cert not found/server doesnt trust the CA
     * CURLE_SSL_CERTPROBLEM - malformed certificate/no permission
     */
    if ((post_state->curl_result == CURLE_SSL_CONNECT_ERROR
         || post_state->curl_result == CURLE_SSL_CERTPROBLEM)
            && config->ur_client_cert && config->ur_client_key)
    {
        warn_msg("Authentication failed. Retrying unauthenticated.");
        free_post_state(post_state);
        post_state = new_post_state(flags);

        post_string_as_form_data(post_state, dest_url, "application/json",
                                 headers, json);

    }

    return post_state;
}

struct ureport_server_response *
libreport_ureport_submit(const char *json, struct ureport_server_config *config)
{
    struct post_state *post_state = libreport_ureport_do_post(json, config, UREPORT_SUBMIT_ACTION);

    if (post_state == NULL)
    {
        error_msg(_("Failed on submitting the problem"));
        return NULL;
    }

    struct ureport_server_response *resp = libreport_ureport_server_response_from_reply(post_state, config);
    free_post_state(post_state);

    return resp;
}

char *
ureport_json_attachment_new(const char *bthash, const char *type, const char *data)
{
    struct json_object *attachment = json_object_new_object();
    if (!attachment)
        libreport_die_out_of_memory();

    ureport_add_str(attachment, "bthash", bthash);
    ureport_add_str(attachment, "type", type);
    ureport_add_str(attachment, "data", data);

    char *result = g_strdup(json_object_to_json_string(attachment));
    json_object_put(attachment);

    return result;
}

bool
ureport_attach_string(struct ureport_server_config *config,
                      const char *bthash,
                      const char *type,
                      const char *data)
{
    g_autofree char *json = NULL;

    json = ureport_json_attachment_new(bthash, type, data);

    post_state_t *post_state = libreport_ureport_do_post(json, config, UREPORT_ATTACH_ACTION);

    struct ureport_server_response *resp =
        libreport_ureport_server_response_from_reply(post_state, config);
    free_post_state(post_state);
    /* don't use str_bo_bool() because we require "true" string */
    const int result = !resp || resp->urr_is_error || strcmp(resp->urr_value, "true") != 0;

    if (resp && resp->urr_is_error)
        error_msg(_("The server at '%s' responded with an error: '%s'"),
                config->ur_url, resp->urr_value);

    libreport_ureport_server_response_free(resp);

    return result;
}

bool
ureport_attach(struct ureport_server_config *config,
               const char *bthash,
               const char *type,
               const char *format,
               ...)
{
    va_list args;
    g_autofree char *string = NULL;

    va_start(args, format);

    string = g_strdup_vprintf(format, args);

    return ureport_attach_string(config, bthash, type, string);
}
