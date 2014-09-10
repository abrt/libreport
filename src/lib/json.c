/*
    Copyright (C) 2012  ABRT team
    Copyright (C) 2012  RedHat Inc

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

#include <satyr/abrt.h>
#include <satyr/report.h>

#include "internal_libreport.h"
#include "ureport.h"
#include "abrt_curl.h"


static void ureport_add_str(struct json_object *ur, const char *key,
                            const char *s)
{
    struct json_object *jstring = json_object_new_string(s);
    if (!jstring)
        die_out_of_memory();

    json_object_object_add(ur, key, jstring);
}

char *ureport_from_dump_dir_ext(const char *dump_dir_path, const struct ureport_preferences *preferences)
{
    char *error_message;
    struct sr_report *report = sr_abrt_report_from_dir(dump_dir_path,
                                                       &error_message);

    if (!report)
        error_msg_and_die("%s", error_message);

    if (preferences != NULL && preferences->urp_auth_items != NULL)
    {
        struct dump_dir *dd = dd_opendir(dump_dir_path, DD_OPEN_READONLY);
        if (!dd)
            xfunc_die(); /* dd_opendir() already printed an error message */

        GList *iter = preferences->urp_auth_items;
        for ( ; iter != NULL; iter = g_list_next(iter))
        {
            const char *key = (const char *)iter->data;
            char *value = dd_load_text_ext(dd, key,
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

char *ureport_from_dump_dir(const char *dump_dir_path)
{
    return ureport_from_dump_dir_ext(dump_dir_path, /*no preferences*/NULL);
}

char *new_json_attachment(const char *bthash, const char *type, const char *data)
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

struct abrt_post_state *post_ureport(const char *json, struct ureport_server_config *config)
{

    int flags = ABRT_POST_WANT_BODY | ABRT_POST_WANT_ERROR_MSG;

    if (config->ur_ssl_verify)
        flags |= ABRT_POST_WANT_SSL_VERIFY;

    struct abrt_post_state *post_state = new_abrt_post_state(flags);

    if (config->ur_client_cert && config->ur_client_key)
    {
        post_state->client_cert_path = config->ur_client_cert;
        post_state->client_key_path = config->ur_client_key;
    }

    if (config->ur_client_cert && config->ur_client_key)
    {
        post_state->client_cert_path = config->ur_client_cert;
        post_state->client_key_path = config->ur_client_key;
    }

    static const char *headers[] = {
        "Accept: application/json",
        "Connection: close",
        NULL,
    };

    abrt_post_string_as_form_data(post_state, config->ur_url, "application/json",
                     (const char **)headers, json);

    /* Client authentication failed. Try again without client auth.
     * CURLE_SSL_CONNECT_ERROR - cert not found/server doesnt trust the CA
     * CURLE_SSL_CERTPROBLEM - malformed certificate/no permission
     */
    if ((post_state->curl_result == CURLE_SSL_CONNECT_ERROR
         || post_state->curl_result == CURLE_SSL_CERTPROBLEM)
            && config->ur_client_cert && config->ur_client_key)
    {
        log(_("Authentication failed. Retrying unauthenticated."));
        free_abrt_post_state(post_state);
        post_state = new_abrt_post_state(flags);

        abrt_post_string_as_form_data(post_state, config->ur_url, "application/json",
                         (const char **)headers, json);

    }

    return post_state;
}

struct abrt_post_state *ureport_attach_rhbz(const char *bthash, int rhbz_bug_id,
                                       struct ureport_server_config *config)
{
    char *str_bug_id = xasprintf("%d", rhbz_bug_id);
    char *json_attachment = new_json_attachment(bthash, "RHBZ", str_bug_id);
    struct abrt_post_state *post_state = post_ureport(json_attachment, config);
    free(str_bug_id);
    free(json_attachment);

    return post_state;
}

struct abrt_post_state *ureport_attach_email(const char *bthash, const char *email,
                                       struct ureport_server_config *config)
{
    char *json_attachment = new_json_attachment(bthash, "email", email);
    struct abrt_post_state *post_state = post_ureport(json_attachment, config);
    free(json_attachment);

    return post_state;
}
