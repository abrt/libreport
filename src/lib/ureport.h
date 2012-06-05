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
 * uReport server configuration
 */
struct ureport_server_config
{
    const char *ur_url; ///< Web service URL
    bool ur_ssl_verify; ///< Verify HOST and PEER certificates
};

struct abrt_post_state;

#define post_ureport libreport_post_ureport
struct abrt_post_state *post_ureport(problem_data_t *pd, struct ureport_server_config *config);

#ifdef __cplusplus
}
#endif

#endif
