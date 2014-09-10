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

#include "problem_data.h"

#ifdef __cplusplus
extern "C" {
#endif

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

    struct ureport_preferences ur_prefs; ///< configuration for uReport generation
};

struct abrt_post_state;

#define post_ureport libreport_post_ureport
struct abrt_post_state *post_ureport(const char *json_ureport,
                                struct ureport_server_config *config);

#define ureport_attach_rhbz libreport_ureport_attach_rhbz
struct abrt_post_state *ureport_attach_rhbz(const char *bthash, int rhbz_bug_id,
                                       struct ureport_server_config *config);

#define ureport_attach_email libreport_ureport_attach_email
struct abrt_post_state *ureport_attach_email(const char *bthash, const char *email,
                                        struct ureport_server_config *config);

#define ureport_from_dump_dir libreport_ureport_from_dump_dir
char *ureport_from_dump_dir(const char *dump_dir_path);

#define ureport_from_dump_dir_ext libreport_ureport_from_dump_dir_ext
char *ureport_from_dump_dir_ext(const char *dump_dir_path,
                                const struct ureport_preferences *preferences);

#ifdef __cplusplus
}
#endif

#endif
