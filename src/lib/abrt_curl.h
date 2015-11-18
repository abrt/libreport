/*
    Copyright (C) 2010  ABRT team
    Copyright (C) 2010  RedHat Inc

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
#ifndef ABRT_CURL_H_
#define ABRT_CURL_H_

#include "internal_libreport.h"
#include <curl/curl.h>

#ifdef __cplusplus
extern "C" {
#endif

CURL* xcurl_easy_init();

/* Set proxy according to the url and call curl_easy_perform */
CURLcode curl_easy_perform_with_proxy(CURL *handle, const char *url);

typedef struct abrt_post_state {
    /* Supplied by caller: */
    int         flags;
    const char  *username;
    const char  *password;
    const char  *client_cert_path;
    const char  *client_key_path;
    /* SSH key files */
    const char  *client_ssh_public_keyfile;
    const char  *client_ssh_private_keyfile;
    /* Results of POST transaction: */
    int         http_resp_code;
    /* cast from CURLcode enum.
     * 0 = success.
     * -1 = curl_easy_perform wasn't even reached (file open error, etc).
     * Else curl_easy_perform's error (which is positive, see curl/curl.h).
     */
    int         curl_result;
    unsigned    header_cnt;
    char        **headers;
    char        *curl_error_msg;
    char        *body;
    size_t      body_size;
    char        errmsg[CURL_ERROR_SIZE];
} abrt_post_state_t;

abrt_post_state_t *new_abrt_post_state(int flags);
void free_abrt_post_state(abrt_post_state_t *state);
char *find_header_in_abrt_post_state(abrt_post_state_t *state, const char *str);

enum {
    ABRT_POST_WANT_HEADERS    = (1 << 0),
    ABRT_POST_WANT_ERROR_MSG  = (1 << 1),
    ABRT_POST_WANT_BODY       = (1 << 2),
    ABRT_POST_WANT_SSL_VERIFY = (1 << 3),
};
enum {
    /* Must be -1! CURLOPT_POSTFIELDSIZE interprets -1 as "use strlen" */
    ABRT_POST_DATA_STRING = -1,
    ABRT_POST_DATA_FROMFILE = -2,
    ABRT_POST_DATA_FROMFILE_PUT = -3,
    ABRT_POST_DATA_FROMFILE_AS_FORM_DATA = -4,
    ABRT_POST_DATA_STRING_AS_FORM_DATA = -5,
};
int
abrt_post(abrt_post_state_t *state,
                const char *url,
                const char *content_type,
                const char **additional_headers,
                const char *data,
                off_t data_size);
static inline int
abrt_post_string(abrt_post_state_t *state,
                const char *url,
                const char *content_type,
                const char **additional_headers,
                const char *str)
{
    return abrt_post(state, url, content_type, additional_headers,
                     str, ABRT_POST_DATA_STRING);
}
static inline int
abrt_post_string_as_form_data(abrt_post_state_t *state,
                const char *url,
                const char *content_type,
                const char **additional_headers,
                const char *str)
{
    return abrt_post(state, url, content_type, additional_headers,
                     str, ABRT_POST_DATA_STRING_AS_FORM_DATA);
}
static inline int
abrt_post_file(abrt_post_state_t *state,
                const char *url,
                const char *content_type,
                const char **additional_headers,
                const char *filename)
{
    return abrt_post(state, url, content_type, additional_headers,
                     filename, ABRT_POST_DATA_FROMFILE);
}
static inline int
abrt_post_file_as_form(abrt_post_state_t *state,
                const char *url,
                const char *content_type,
                const char **additional_headers,
                const char *filename)
{
    return abrt_post(state, url, content_type, additional_headers,
                     filename, ABRT_POST_DATA_FROMFILE_AS_FORM_DATA);
}

#define upload_file libreport_upload_file
char *upload_file(const char *url, const char *filename, map_string_t *settings);

#ifdef __cplusplus
}
#endif

#endif
