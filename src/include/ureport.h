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

#define UREPORT_CONF_FILE_PATH PLUGINS_CONF_DIR"/ureport.conf"

#define UREPORT_OPTION_VALUE_FROM_CONF(settings, opt, var, tr) do { const char *value = getenv("uReport_"opt); \
        if (!value) { value = get_map_string_item_or_NULL(settings, opt); } if (value) { var = tr(value); } \
    } while(0)

#define UREPORT_SUBMIT_ACTION "reports/new/"
#define UREPORT_ATTACH_ACTION "reports/attach/"

/*
 * uReport generation configuration
 */
struct ureport_preferences
{
    GList *urp_auth_items;  ///< list of file names included in 'auth' key
};

/*
 * uReport server configuration
 */
struct ureport_server_config
{
    const char *ur_url;   ///< Web service URL
    bool ur_ssl_verify;   ///< Verify HOST and PEER certificates
    char *ur_client_cert; ///< Path to certificate used for client
                          ///< authentication (or NULL)
    char *ur_client_key;  ///< Private key for the certificate
    map_string_t *ur_http_headers; ///< Additional HTTP headers

    struct ureport_preferences ur_prefs; ///< configuration for uReport generation
};

/*
 * Initialize structure members
 *
 * @param config Initialized structure
 */
#define ureport_server_config_init libreport_ureport_server_config_init
void
ureport_server_config_init(struct ureport_server_config *config);

/*
 * Release all allocated resources
 *
 * @param config Released structure
 */
#define ureport_server_config_destroy libreport_ureport_server_config_destroy
void
ureport_server_config_destroy(struct ureport_server_config *config);

/*
 * Loads uReport configuration from various sources.
 *
 * Replaces a value of an already configured option only if the
 * option was found in a configuration source.
 *
 * @param config a server configuration to be populated
 */
#define ureport_server_config_load libreport_ureport_server_config_load
void
ureport_server_config_load(struct ureport_server_config *config,
                           map_string_t *settings);

/*
 * Configure client certificate paths
 *
 * @param config Where the paths are stored
 * @param client_path Path in form of cert_full_path:key_full_path or one of
 *        the following string: 'rhsm', 'puppet'.
 */
#define ureport_server_config_set_client_auth libreport_ureport_server_config_set_client_auth
void
ureport_server_config_set_client_auth(struct ureport_server_config *config,
                                      const char *client_auth);

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
struct abrt_post_state;

/*
 * Parse server reply
 *
 * @param post_state Server reply
 * @param config Configuration used in communication
 * @return Pointer to malloced memory or NULL in case of error in communication
 */
#define ureport_server_response_from_reply libreport_ureport_server_response_from_reply
struct ureport_server_response *
ureport_server_response_from_reply(struct abrt_post_state *post_state,
                                   struct ureport_server_config *config);

/*
 * Save response in dump dir files
 *
 * @param resp Parsed server response
 * @param dump_dir_pat Path to dump directory
 * @param config Configuration used in communication
 * @return False in case of any error; otherwise True.
 */
#define ureport_server_response_save_in_dump_dir libreport_ureport_server_response_save_in_dump_dir
bool
ureport_server_response_save_in_dump_dir(struct ureport_server_response *resp,
                                         const char *dump_dir_path,
                                         struct ureport_server_config *config);

/*
 * Build URL to submitted uReport
 *
 * @param resp Parsed server response
 * @param config Configuration used in communication
 * @return Malloced zero-terminated string
 */
#define ureport_server_response_get_report_url libreport_ureport_server_response_get_report_url
char *
ureport_server_response_get_report_url(struct ureport_server_response *resp,
                                       struct ureport_server_config *config);

/*
 * Release allocated resources
 *
 * @param resp Released structured
 */
#define ureport_server_response_free libreport_ureport_server_response_free
void
ureport_server_response_free(struct ureport_server_response *resp);

/*
 * Send JSON to server and obtain reply
 *
 * @param json Sent data
 * @param config Configuration used in communication
 * @param url_sfx Local part of the upload URL
 * @return Malloced server reply or NULL in case of communication errors
 */
#define ureport_do_post libreport_ureport_do_post
struct abrt_post_state *
ureport_do_post(const char *json, struct ureport_server_config *config,
                const char *url_sfx);

/*
 * Submit uReport on server
 *
 * @param json Sent data
 * @param config Configuration used in communication
 * @return Malloced, parsed server response
 */
#define ureport_submit libreport_ureport_submit
struct ureport_server_response *
ureport_submit(const char *json_ureport, struct ureport_server_config *config);

/*
 * Attach given string to uReport
 *
 * @param bthash uReport identifier
 * @param type Type of attachment
 * @param data Attached data
 * @param config Configuration used in communication
 * @return False in case of any error; otherwise True
 */
#define ureport_attach_string libreport_ureport_attach_string
bool
ureport_attach_string(const char *bthash, const char *type, const char *data,
               struct ureport_server_config *config);

/*
 * Attach given integer to uReport
 *
 * @param bthash uReport identifier
 * @param type Type of attachment
 * @param data Attached data
 * @param config Configuration used in communication
 * @return False in case of any error; otherwise True
 */
#define ureport_attach_int libreport_ureport_attach_int
bool
ureport_attach_int(const char *bthash, const char *type, int data,
                   struct ureport_server_config *config);

/*
 * Build uReport from dump dir
 *
 * @param dump_dir_path FS path to dump dir
 * @return Malloced JSON string
 */
#define ureport_from_dump_dir libreport_ureport_from_dump_dir
char *
ureport_from_dump_dir(const char *dump_dir_path);

#define ureport_from_dump_dir_ext libreport_ureport_from_dump_dir_ext
char *ureport_from_dump_dir_ext(const char *dump_dir_path,
                                const struct ureport_preferences *preferences);

#ifdef __cplusplus
}
#endif

#endif
