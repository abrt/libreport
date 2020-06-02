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
#ifndef UREPORT_H_
#define UREPORT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "internal_libreport.h"

#define UREPORT_CONF_FILE_PATH PLUGINS_CONF_DIR"/ureport.conf"

#define UREPORT_OPTION_VALUE_FROM_CONF(settings, opt, var, tr) do { const char *value = getenv("uReport_"opt); \
        if (!value) { value = g_hash_table_lookup(settings, opt); } if (value) { var = tr(value); } \
    } while(0)

#define UREPORT_SUBMIT_ACTION "reports/new/"
#define UREPORT_ATTACH_ACTION "reports/attach/"

/*
 * Flags for tweaking the way how uReports are generated.
 */
enum ureport_preferences_flags
{
    UREPORT_PREF_FLAG_RETURN_ON_FAILURE = 0x1, ///< Do not exit on failures
};

/*
 * uReport generation configuration
 */
struct ureport_preferences
{
    GList *urp_auth_items;    ///< list of file names included in 'auth' key
    int urp_flags;            ///< See enum ureport_preferences_flags
};

/*
 * uReport server configuration
 */
struct ureport_server_config
{
    char *ur_url;         ///< Web service URL
    bool ur_ssl_verify;   ///< Verify HOST and PEER certificates
    char *ur_client_cert; ///< Path to certificate used for client
                          ///< authentication (or NULL)
    char *ur_client_key;  ///< Private key for the certificate
    char *ur_cert_authority_cert; ///< Certificate authority certificate
    char *ur_username;    ///< username for basic HTTP auth
    char *ur_password;    ///< password for basic HTTP auth

    struct ureport_preferences ur_prefs; ///< configuration for uReport generation
};

/*
 * Initialize structure members
 *
 * @param config Initialized structure
 */
void
libreport_ureport_server_config_init(struct ureport_server_config *config);

/*
 * Release all allocated resources
 *
 * @param config Released structure
 */
void
libreport_ureport_server_config_destroy(struct ureport_server_config *config);

/*
 * Loads uReport configuration from various sources.
 *
 * Replaces a value of an already configured option only if the
 * option was found in a configuration source.
 *
 * @param config a server configuration to be populated
 */
void
libreport_ureport_server_config_load(struct ureport_server_config *config,
                           GHashTable *settings);

/*
 * Configure HTTP(S) URL to server's index page
 *
 * @param config Where the url is stored
 * @param server_url Index URL
 */
void
libreport_ureport_server_config_set_url(struct ureport_server_config *config,
                              char *server_url);

/*
 * Configure client certificate paths
 *
 * @param config Where the paths are stored
 * @param client_path Path in form of cert_full_path:key_full_path or 'puppet'.
 */
void
libreport_ureport_server_config_set_client_auth(struct ureport_server_config *config,
                                      const char *client_auth);

/*
 * Configure user name and password for HTTP Basic authentication
 *
 * @param config Configured structure
 * @param username User name
 * @param password Password
 */
void
libreport_ureport_server_config_set_basic_auth(struct ureport_server_config *config,
                                     const char *username, const char *password);

/*
 * Configure user name and password for HTTP Basic authentication according to
 * user preferences.
 *
 *  "<user_name>:<password>" - Manually supply user name and password.
 *  "<user_name>" - Manually supply user name and be asked for password.
 *
 * The function uses libreport_ask_password() function from client.h
 *
 * @param config Configured structure
 * @param http_auth_pref User HTTP Authentication preferences
 */
void
ureport_server_config_load_basic_auth(struct ureport_server_config *config,
                                      const char *http_auth_pref);

/*
 * uReport server response
 */
struct ureport_server_response
{
    bool urr_is_error;  ///< True if server replied with error response
    char *urr_value;    ///< Value of the response
    char *urr_message;  ///< Additional message
    char *urr_bthash;   ///< uReport's server side identifier
    GList *urr_reported_to_list; ///< Known external reports for uReport
                                 ///< in *reported_to* format
    char *urr_solution; ///< URL pointing to solution for uReport
};

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
struct ureport_server_response *
libreport_ureport_server_response_from_reply(struct post_state *post_state,
                                   struct ureport_server_config *config);

/*
 * Save response in dump dir files
 *
 * @param resp Parsed server response
 * @param dump_dir_pat Path to dump directory
 * @param config Configuration used in communication
 * @return False in case of any error; otherwise True.
 */
bool
libreport_ureport_server_response_save_in_dump_dir(struct ureport_server_response *resp,
                                         const char *dump_dir_path,
                                         struct ureport_server_config *config);

/*
 * Build URL to submitted uReport
 *
 * @param resp Parsed server response
 * @param config Configuration used in communication
 * @return Malloced zero-terminated string
 */
char *
libreport_ureport_server_response_get_report_url(struct ureport_server_response *resp,
                                       struct ureport_server_config *config);

/*
 * Release allocated resources
 *
 * @param resp Released structured
 */
void
libreport_ureport_server_response_free(struct ureport_server_response *resp);

/*
 * Send JSON to server and obtain reply
 *
 * @param json Sent data
 * @param config Configuration used in communication
 * @param url_sfx Local part of the upload URL
 * @return Malloced server reply or NULL in case of communication errors
 */
struct post_state *
libreport_ureport_do_post(const char *json, struct ureport_server_config *config,
                const char *url_sfx);

/*
 * Submit uReport on server
 *
 * @param json Sent data
 * @param config Configuration used in communication
 * @return Malloced, parsed server response
 */
struct ureport_server_response *
libreport_ureport_submit(const char *json_ureport, struct ureport_server_config *config);

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
ureport_attach_string(struct ureport_server_config *config,
                      const char                   *bthash,
                      const char                   *type,
                      const char                   *data);

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
ureport_attach(struct ureport_server_config *config,
               const char                   *bthash,
               const char                   *type,
               const char                   *format,
               ...) G_GNUC_PRINTF(4, 5);

/*
 * Build uReport from dump dir
 *
 * @param dump_dir_path FS path to dump dir
 * @return Malloced JSON string
 */
char *libreport_ureport_from_dump_dir_ext(const char *dump_dir_path,
                                const struct ureport_preferences *preferences);

#ifdef __cplusplus
}
#endif

#endif
