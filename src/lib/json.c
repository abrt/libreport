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
#include "libreport_curl.h"


static void ureport_add_str(struct json_object *ur, const char *key,
                            const char *s)
{
    struct json_object *jstring = json_object_new_string(s);
    if (!jstring)
        die_out_of_memory();

    json_object_object_add(ur, key, jstring);
}

char *ureport_from_dump_dir(const char *dump_dir_path)
{
    char *error_message;
    struct sr_report *report = sr_abrt_report_from_dir(dump_dir_path,
                                                       &error_message);

    if (!report)
        error_msg_and_die("%s", error_message);

    char *json_ureport = sr_report_to_json(report);
    sr_report_free(report);

    return json_ureport;
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

struct post_state *post_ureport(const char *json_ureport, struct ureport_server_config *config)
{
    int flags = POST_WANT_BODY | POST_WANT_ERROR_MSG;

    if (config->ur_ssl_verify)
        flags |= POST_WANT_SSL_VERIFY;

    struct post_state *post_state = new_post_state(flags);

    static const char *headers[] = {
        "Accept: application/json",
        "Connection: close",
        NULL,
    };

    post_string_as_form_data(post_state, config->ur_url, "application/json",
                     headers, json_ureport);

    return post_state;
}

struct post_state *ureport_attach_rhbz(const char *bthash, int rhbz_bug_id,
                                       struct ureport_server_config *config)
{
    int flags = POST_WANT_BODY | POST_WANT_ERROR_MSG;

    if (config->ur_ssl_verify)
        flags |= POST_WANT_SSL_VERIFY;

    struct post_state *post_state = new_post_state(flags);

    static const char *headers[] = {
        "Accept: application/json",
        "Connection: close",
        NULL,
    };

    char *str_bug_id = xasprintf("%d", rhbz_bug_id);
    char *json_attachment = new_json_attachment(bthash, "RHBZ", str_bug_id);
    post_string_as_form_data(post_state, config->ur_url, "application/json",
                             headers, json_attachment);
    free(str_bug_id);
    free(json_attachment);

    return post_state;
}
