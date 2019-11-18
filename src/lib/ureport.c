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

#include <glib-object.h>

#include <satyr/abrt.h>
#include <satyr/report.h>

#include "internal_libreport.h"
#include "client.h"
#include "ureport.h"
#include "libreport_curl.h"

static void
ureport_add_str(struct json_object *ur,
                const char         *key,
                const char         *s)
{
    struct json_object *jstring = json_object_new_string(s);
    if (!jstring)
        die_out_of_memory();

    json_object_object_add(ur, key, jstring);
}

/**
 * ureport_from_dump_dir:
 * @auth_items: (element-type utf8)
 */
char *
ureport_from_dump_dir(const char              *dump_dir_path,
                      GList                   *auth_items,
                      UReportPreferencesFlags  flags)
{
    char *error_message;
    struct sr_report *report;
    char *json_ureport;

    report = sr_abrt_report_from_dir(dump_dir_path, &error_message);

    if (NULL == report)
    {
        if (flags & UREPORT_PREF_FLAG_RETURN_ON_FAILURE)
            error_msg_and_die("%s", error_message);

        log_notice("%s", error_message);
        return NULL;
    }

    if (NULL != auth_items)
    {
        struct dump_dir *dd;

        dd = dd_opendir(dump_dir_path, DD_OPEN_READONLY);
        if (NULL == dd)
        {
            xfunc_die(); /* dd_opendir() already printed an error message */
        }

        for (GList *iter = auth_items; NULL != iter; iter = g_list_next(iter))
        {
            const char *key;
            g_autofree char *value = NULL;

            key = iter->data;
            value = dd_load_text_ext(dd, key,
                                     (DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE |
                                      DD_FAIL_QUIETLY_ENOENT));

            if (NULL == value)
            {
                perror_msg("Cannot include '%s' in 'auth'", key);
                continue;
            }

            sr_report_add_auth(report, key, value);
        }

        dd_close(dd);
    }

    json_ureport = sr_report_to_json(report);

    sr_report_free(report);

    return json_ureport;
}

struct post_state *
ureport_do_post(const char *json,
                UReportServerConfig *config,
                const char *url_sfx)
{
    char *client_cert;
    char *client_key;
    int flags;
    g_autofree char *url = NULL;
    struct post_state *post_state;
    const char *headers[] =
    {
        "Accept: application/json",
        "Connection: close",
        NULL,
    };
    g_autofree char *dest_url = NULL;

    client_cert = ureport_server_config_get_client_cert(config);
    client_key = ureport_server_config_get_client_key(config);
    flags = POST_WANT_BODY | POST_WANT_ERROR_MSG;
    url = ureport_server_config_get_url(config);
    post_state = new_post_state(flags);

    if (ureport_server_config_get_ssl_verify(config))
        flags |= POST_WANT_SSL_VERIFY;

    post_state = new_post_state(flags);

    post_state->client_cert_path = client_cert;
    post_state->client_key_path = client_key;
    post_state->cert_authority_cert_path = ureport_server_config_get_cert_authority_cert(config);
    post_state->username = ureport_server_config_get_username(config);
    post_state->password = ureport_server_config_get_password(config);

    dest_url = g_build_path("/", url, url_sfx, NULL);

    post_string_as_form_data(post_state, dest_url, "application/json", headers, json);

    /* Client authentication failed. Try again without client auth.
     * CURLE_SSL_CONNECT_ERROR - cert not found/server doesnt trust the CA
     * CURLE_SSL_CERTPROBLEM - malformed certificate/no permission
     */
    if ((post_state->curl_result == CURLE_SSL_CONNECT_ERROR
         || post_state->curl_result == CURLE_SSL_CERTPROBLEM)
            && client_cert && client_key)
    {
        warn_msg("Authentication failed. Retrying unauthenticated.");
        free_post_state(post_state);
        post_state = new_post_state(flags);

        post_string_as_form_data(post_state, dest_url, "application/json",
                                 headers, json);

    }

    return post_state;
}

UReportServerResponse *
ureport_submit(const char *json,
               UReportServerConfig *config)
{
    g_autofree struct post_state *post_state = NULL;

    post_state = ureport_do_post(json, config, UREPORT_SUBMIT_ACTION);
    if (post_state == NULL)
    {
        error_msg(_("Failed on submitting the problem"));
        return NULL;
    }

    return ureport_server_response_new_from_reply(post_state, config);
}

char *
ureport_json_attachment_new(const char *bthash, const char *type, const char *data)
{
    struct json_object *attachment = json_object_new_object();
    if (!attachment)
        die_out_of_memory();

    ureport_add_str(attachment, "bthash", bthash);
    ureport_add_str(attachment, "type", type);
    ureport_add_str(attachment, "data", data);

    char *result = xstrdup(json_object_to_json_string(attachment));
    json_object_put(attachment);

    return result;
}

bool
ureport_attach_string(UReportServerConfig *config,
                      const char          *bthash,
                      const char          *type,
                      const char          *data)
{
    g_autofree char *json = NULL;
    post_state_t *post_state;
    g_autoptr(UReportServerResponse) response = NULL;
    bool is_error;
    g_autofree char *value = NULL;

    json = ureport_json_attachment_new(bthash, type, data);
    post_state = ureport_do_post(json, config, UREPORT_ATTACH_ACTION);
    response = ureport_server_response_new_from_reply(post_state, config);
    if (NULL == response)
    {
        return false;
    }
    is_error = ureport_server_response_get_is_error(response);
    value = ureport_server_response_get_value(response);

    free_post_state(post_state);

    if (is_error)
    {
        g_autofree char *url = NULL;

        url = ureport_server_config_get_url(config);

        error_msg(_("The server at '%s' responded with an error: '%s'"), url, value);
    }

    return !is_error || strcmp(value, "true") == 0;
}

bool
ureport_attach(UReportServerConfig *config,
               const char          *bthash,
               const char          *type,
               const char          *format,
               ...)
{
    va_list args;
    g_autofree char *string = NULL;

    va_start(args, format);

    string = g_strdup_vprintf(format, args);

    return ureport_attach_string(config, bthash, type, string);
}
