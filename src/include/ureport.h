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

#include "internal_libreport.h"
#include "types.h"

#include <glib-object.h>

#pragma once

G_BEGIN_DECLS

#define UREPORT_CONF_FILE_PATH PLUGINS_CONF_DIR"/ureport.conf"

#define UREPORT_OPTION_VALUE_FROM_CONF(settings, opt, var, tr) \
    do { \
        const char *value = getenv("uReport_"opt); \
        if (!value) \
        { \
            value = get_map_string_item_or_NULL(settings, opt); \
        } if (value) { var = tr(value); } \
    } while(0)

#define UREPORT_SUBMIT_ACTION "reports/new/"
#define UREPORT_ATTACH_ACTION "reports/attach/"

/*
 * Flags for tweaking the way how uReports are generated.
 */
typedef enum
{
    UREPORT_PREF_FLAG_RETURN_ON_FAILURE = 0x1, ///< Do not exit on failures
} UReportPreferencesFlags;

GType ureport_server_config_get_type(void) G_GNUC_CONST;
GType ureport_server_response_get_type(void) G_GNUC_CONST;

/*
 * Allocate new instance
 */
UReportServerConfig *
ureport_server_config_new(void);

/*
 * Replaces defaults with values found in the provided configuration.
 */
UReportServerConfig *
ureport_server_config_new_for_settings(GHashTable *settings);

UReportServerConfig *
ureport_server_config_dup(UReportServerConfig *config);

/*
 * Release all allocated resources
 *
 * @param config Released structure
 */
void
ureport_server_config_destroy(UReportServerConfig *config);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(UReportServerConfig, ureport_server_config_destroy)

GList *
ureport_server_config_get_auth_items(UReportServerConfig *config);
/*
 * Set files to be included in the “auth” key.
 *
 * @param config A #UReportServerConfig instance
 * @param auth_items A list of file names
 */
void
ureport_server_config_set_auth_items(UReportServerConfig *config,
                                     GList *auth_items);

char *
ureport_server_config_get_cert_authority_cert(UReportServerConfig *config);

char *
ureport_server_config_get_client_cert(UReportServerConfig *config);

char *
ureport_server_config_get_client_key(UReportServerConfig *config);

UReportPreferencesFlags
ureport_server_config_get_flags(UReportServerConfig *config);

void
ureport_server_config_set_flags(UReportServerConfig *config,
                                UReportPreferencesFlags flags);

char *
ureport_server_config_get_password(UReportServerConfig *config);

void
ureport_server_config_set_password(UReportServerConfig *config,
                                   const char          *password);

bool
ureport_server_config_get_ssl_verify(UReportServerConfig *config);

void
ureport_server_config_set_ssl_verify(UReportServerConfig *config,
                                     bool ssl_verify);

char *
ureport_server_config_get_url(UReportServerConfig *config);

/*
 * Configure HTTP(S) URL to server's index page
 *
 * @param config Where the url is stored
 * @param server_url Index URL
 */
void
ureport_server_config_set_url(UReportServerConfig *config,
                              const char *server_url);

char *
ureport_server_config_get_username(UReportServerConfig *config);

void
ureport_server_config_set_username(UReportServerConfig *config,
                                   const char          *username);

/*
 * Configure client certificate paths
 *
 * @param config Where the paths are stored
 * @param client_path Path in form of cert_full_path:key_full_path or one of
 *        the following string: 'rhsm', 'puppet'.
 */
void
ureport_server_config_set_client_auth(UReportServerConfig *config,
                                      const char *client_auth);

/*
 * Configure user name and password for HTTP Basic authentication
 *
 * @param config Configured structure
 * @param username User name
 * @param password Password
 */
void
ureport_server_config_set_basic_auth(UReportServerConfig *config,
                                     const char *username, const char *password);

/*
 * Configure user name and password for HTTP Basic authentication according to
 * user preferences.
 *
 *  "rhts-credentials" - Uses Login= and Password= from rhtsupport.conf
 *  "<user_name>:<password>" - Manually supply user name and password.
 *  "<user_name>" - Manually supply user name and be asked for password.
 *
 * The function uses ask_password() function from client.h
 *
 * @param config Configured structure
 * @param http_auth_pref User HTTP Authentication preferences
 */
void
ureport_server_config_load_basic_auth(UReportServerConfig *config,
                                      const char *http_auth_pref);

/* Can't include "abrt_curl.h", it's not a public API.
 * Resorting to just forward-declaring the struct we need.
 */
struct post_state;

/*
 * Parse server reply
 *
 * @param post_state Server reply
 * @param config Configuration used in communication
 * @return Pointer to malloced memory or NULL in case of error in communication
 */
UReportServerResponse *
ureport_server_response_new_from_reply(struct post_state *post_state,
                                       UReportServerConfig *config);

/*
 * Save response in dump dir files
 *
 * @param resp Parsed server response
 * @param dump_dir_pat Path to dump directory
 * @param config Configuration used in communication
 * @return False in case of any error; otherwise True.
 */
bool
ureport_server_response_save_in_dump_dir(UReportServerResponse *response,
                                         const char *dump_dir_path,
                                         UReportServerConfig *config);

char *
ureport_server_response_get_bthash(UReportServerResponse *response);

bool
ureport_server_response_get_is_error(UReportServerResponse *response);

char *
ureport_server_response_get_message(UReportServerResponse *response);

/*
 * Build URL to submitted uReport
 *
 * @param resp Parsed server response
 * @param config Configuration used in communication
 * @return Malloced zero-terminated string
 */
char *
ureport_server_response_get_report_url(UReportServerResponse *response,
                                       UReportServerConfig *config);

char *
ureport_server_response_get_value(UReportServerResponse *response);

GList *
ureport_server_response_get_reported_to_list(UReportServerResponse *response);

UReportServerResponse *
ureport_server_response_new(void);

UReportServerResponse *
ureport_server_response_dup(UReportServerResponse *response);

/*
 * Release allocated resources
 *
 * @param resp Released structured
 */
void
ureport_server_response_destroy(UReportServerResponse *response);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(UReportServerResponse, ureport_server_response_destroy)

/*
 * Send JSON to server and obtain reply
 *
 * @param json Sent data
 * @param config Configuration used in communication
 * @param url_sfx Local part of the upload URL
 * @return Malloced server reply or NULL in case of communication errors
 */
struct post_state *
ureport_do_post(const char *json, UReportServerConfig *config,
                const char *url_sfx);

/*
 * Submit uReport on server
 *
 * @param json Sent data
 * @param config Configuration used in communication
 * @return Malloced, parsed server response
 */
UReportServerResponse *
ureport_submit(const char *json_ureport, UReportServerConfig *config);

/*
 * Build a new uReport attachement from give arguments
 *
 * @param bthash ID of uReport
 * @param type Type of attachement recognized by uReport Server
 * @param data Attached data
 * @returm Malloced JSON string
 */
char *
ureport_json_attachment_new(const char *bthash, const char *type, const char *data);

/*
 * Attach given string to uReport
 *
 * @param config Configuration used in communication
 * @param bthash uReport identifier
 * @param type Type of attachment
 * @param data Attached data
 * @return True in case of any error; otherwise False
 */
bool
ureport_attach_string(UReportServerConfig *config,
                      const char          *bthash,
                      const char          *type,
                      const char          *data);

/*
 * Attach formatted data to uReport
 *
 * @param config Configuration used in communication
 * @param bthash uReport identifier
 * @param type Type of attachment
 * @param format Data format string
 * @param ... Values to replace format specifiers
 * @return True in case of any error; otherwise False
 */
bool
ureport_attach(UReportServerConfig *config,
               const char          *bthash,
               const char          *type,
               const char          *format,
               ...) G_GNUC_PRINTF(4, 5);

/*
 * Build uReport from dump dir
 *
 * @param dump_dir_path FS path to dump dir
 * @param auth_items additional problem directory items to include in the report
 * @param flags flags for generating reports
 *
 * @return Malloced JSON string
 */
char *ureport_from_dump_dir(const char *dump_dir_path, GList *auth_items, UReportPreferencesFlags flags);

G_END_DECLS
