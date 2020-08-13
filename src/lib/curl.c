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
#include "internal_libreport.h"
#include "client.h"
#include "libreport_curl.h"
#include "proxies.h"

/*
 * Utility functions
 */
CURL* xcurl_easy_init()
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        error_msg_and_die("Can't create curl handle");
    }
    return curl;
}

static char*
check_curl_error(CURLcode err, const char* msg)
{
    if (err)
        return g_strdup_printf("%s: %s", msg, curl_easy_strerror(err));
    return NULL;
}

static void
die_if_curl_error(CURLcode err)
{
    if (err) {
        char *msg = check_curl_error(err, "curl");
        error_msg_and_die("%s", msg);
    }
}

static void
xcurl_easy_setopt_ptr(CURL *handle, CURLoption option, const void *parameter)
{
    CURLcode err = curl_easy_setopt(handle, option, parameter);
    if (err) {
        char *msg = check_curl_error(err, "curl");
        error_msg_and_die("%s", msg);
    }
}
static inline void
xcurl_easy_setopt_long(CURL *handle, CURLoption option, long parameter)
{
    xcurl_easy_setopt_ptr(handle, option, (void*)parameter);
}

static void
xcurl_easy_setopt_off_t(CURL *handle, CURLoption option, curl_off_t parameter)
{
    /* Can't reuse xcurl_easy_setopt_ptr: paramter is too wide */
    CURLcode err = curl_easy_setopt(handle, option, parameter);
    if (err) {
        char *msg = check_curl_error(err, "curl");
        error_msg_and_die("%s", msg);
    }
}

CURLcode curl_easy_perform_with_proxy(CURL *handle, const char *url)
{
    GList *proxy_list = NULL;
    GList *li = NULL;
    CURLcode curl_err;

    proxy_list = get_proxy_list(url);

    if (proxy_list)
    {
        /* Try with each proxy before giving up. */
        /* TODO: Should we repeat the perform call only on certain errors? */
        for (li = proxy_list, curl_err = 1; curl_err && li; li = g_list_next(li))
        {
            xcurl_easy_setopt_ptr(handle, CURLOPT_PROXY, li->data);
            log_notice("Connecting to %s (using proxy server %s)", url, (const char *)li->data);
            curl_err = curl_easy_perform(handle);
        }
    }
    else
    {
        log_notice("Connecting to %s", url);
        curl_err = curl_easy_perform(handle);
    }

    g_list_free_full(proxy_list, free);

    return curl_err;
}

/*
 * post_state utility functions
 */

post_state_t *new_post_state(int flags)
{
    post_state_t *state = g_new0(post_state_t, 1);
    state->flags = flags;
    return state;
}

void free_post_state(post_state_t *state)
{
    if (!state)
        return;

    char **headers = state->headers;
    if (headers)
    {
        while (*headers)
            free(*headers++);
        free(state->headers);
    }
    free(state->curl_error_msg);
    free(state->body);
    free(state);
}

char *find_header_in_post_state(post_state_t *state, const char *str)
{
    char **headers = state->headers;
    if (headers)
    {
        unsigned len = strlen(str);
        while (*headers)
        {
            if (strncmp(*headers, str, len) == 0)
                return libreport_skip_whitespace(*headers + len);
            headers++;
        }
    }
    return NULL;
}

/*
 * post: perform HTTP POST transaction
 */

/* "save headers" callback */
static size_t
save_headers(void *buffer_pv, size_t count, size_t nmemb, void *ptr)
{
    post_state_t* state = (post_state_t*)ptr;
    size_t size = count * nmemb;

    g_autofree char *h = g_strndup((char*)buffer_pv, size);
    strchrnul(h, '\r')[0] = '\0';
    strchrnul(h, '\n')[0] = '\0';

    unsigned cnt = state->header_cnt;

    /* Check for the case when curl follows a redirect:
     * header 0: 'HTTP/1.1 301 Moved Permanently'
     * header 1: 'Connection: close'
     * header 2: 'Location: NEW_URL'
     * header 3: ''
     * header 0: 'HTTP/1.1 200 OK' <-- we need to forget all hdrs and start anew
     */
    if (cnt != 0
     && strncmp(h, "HTTP/", 5) == 0
     && state->headers[cnt-1][0] == '\0' /* prev header is an empty string */
    ) {
        char **headers = state->headers;
        if (headers)
        {
            while (*headers)
                free(*headers++);
        }
        cnt = 0;
    }

    log_debug("save_headers: header %d: '%s'", cnt, h);
    state->headers = (char**)g_realloc(state->headers, (cnt+2) * sizeof(state->headers[0]));
    state->headers[cnt] = h;
    state->header_cnt = ++cnt;
    state->headers[cnt] = NULL;

    return size;
}

/* "read local data from a file" callback */
static size_t fread_with_reporting(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    static time_t last_t; // hack
    static time_t report_interval;

    FILE *fp = (FILE*)userdata;

    off_t cur_pos = ftello(fp);
    if (cur_pos == -1)
        goto skip; /* paranoia */

    time_t t = time(NULL);

    if (cur_pos == 0) /* first call */
    {
        last_t = t;
        report_interval = 15;
    }

    /* Report current file position after 15 seconds,
     * then after 30 seconds, then after 60 seconds and so on.
     */
    if ((t - last_t) >= report_interval)
    {
        last_t = t;
        report_interval *= 2;
        off_t sz = libreport_fstat_st_size_or_die(fileno(fp));
        log_warning(_("Uploaded: %llu of %llu kbytes"),
                (unsigned long long)cur_pos / 1024,
                (unsigned long long)sz / 1024);
    }

 skip:
    return fread(ptr, size, nmemb, fp);
}

static int curl_debug(CURL *handle, curl_infotype it, char *buf, size_t bufsize, void *unused)
{
    if (libreport_logmode == 0)
        return 0;

    unsigned orig_bufsize = bufsize;

    /* curl rcvd header: 'HTTP/1.1 400 BAD REQUEST
     * '
     * ^^^^^^^ one-liners are typical, and they do not look nice.
     * Let's replace trailing CRs and/or LFs with caret notation.
     */
    char *end = buf + bufsize;
    char *p = end;
    while (p > buf)
    {
        if (p[-1] != '\r' && p[-1] != '\n')
            break;
        p--;
        bufsize--;
    }
    char eol[(end - p + 2) * 2];
    char *e = eol;
    while (p < end)
    {
        *e++ = '^';
        *e++ = (*p == '\r' ? 'M' : 'J'); /* CR = ^M, LF = ^J */
        p++;
    }
    *e = '\0';

    switch (it) {
    case CURLINFO_TEXT: /* The data is informational text. */
        /* Here eol is always "^J" or "", not printing it */
        log_warning("curl: %.*s", (int) bufsize, buf);
        break;
    case CURLINFO_HEADER_IN: /* The data is header (or header-like) data received from the peer. */
        log_warning("curl rcvd header: '%.*s%s'", (int) bufsize, buf, eol);
        break;
    case CURLINFO_HEADER_OUT: /* The data is header (or header-like) data sent to the peer. */
        log_warning("curl sent header: '%.*s%s'", (int) bufsize, buf, eol);
        break;
    case CURLINFO_DATA_IN: /* The data is protocol data received from the peer. */
        if (libreport_g_verbose >= 3)
            log_warning("curl rcvd data: '%.*s%s'", (int) bufsize, buf, eol);
        else
            log_warning("curl rcvd data %u bytes", orig_bufsize);
        break;
    case CURLINFO_DATA_OUT: /* The data is protocol data sent to the peer. */
        if (libreport_g_verbose >= 3)
            log_warning("curl sent data: '%.*s%s'", (int) bufsize, buf, eol);
        else
            log_warning("curl sent data %u bytes", orig_bufsize);
        break;
    default:
        break;
    }

    return 0;
}

int
post(post_state_t *state,
                const char *url,
                const char *content_type,
                const char **additional_headers,
                const char *data,
                off_t data_size)
{
    INITIALIZE_LIBREPORT();

    CURLcode curl_err;
    long response_code;
    post_state_t localstate;

    log_debug("%s('%s','%s')", __func__, url, data);

    if (!state)
    {
        memset(&localstate, 0, sizeof(localstate));
        state = &localstate;
    }

    state->curl_result = state->http_resp_code = response_code = -1;

    CURL *handle = xcurl_easy_init();

    // Buffer[CURL_ERROR_SIZE] curl stores human readable error messages in.
    // This may be more helpful than just return code from curl_easy_perform.
    // curl will need it until curl_easy_cleanup.
    state->errmsg[0] = '\0';
    xcurl_easy_setopt_ptr(handle, CURLOPT_ERRORBUFFER, state->errmsg);
    // Shut off the built-in progress meter completely
    xcurl_easy_setopt_long(handle, CURLOPT_NOPROGRESS, 1);

    if (libreport_g_verbose >= 2)
    {
        // "Display a lot of verbose information about its operations.
        // Very useful for libcurl and/or protocol debugging and understanding.
        // The verbose information will be sent to stderr, or the stream set
        // with CURLOPT_STDERR"
        xcurl_easy_setopt_long(handle, CURLOPT_VERBOSE, 1);
        xcurl_easy_setopt_ptr(handle, CURLOPT_DEBUGFUNCTION, curl_debug);
    }

    // TODO: do we need to check for CURLE_URL_MALFORMAT error *here*,
    // not in curl_easy_perform?
    xcurl_easy_setopt_ptr(handle, CURLOPT_URL, url);

    // Auth if configured
    if (state->username)
    {
        // bitmask of allowed auth methods
        xcurl_easy_setopt_long(handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        xcurl_easy_setopt_ptr(handle, CURLOPT_USERNAME, state->username);
        xcurl_easy_setopt_ptr(handle, CURLOPT_PASSWORD, (state->password ? state->password : ""));
    }

    /* set SSH public and private keyfile if configured */
    if (state->client_ssh_public_keyfile)
        xcurl_easy_setopt_ptr(handle, CURLOPT_SSH_PUBLIC_KEYFILE, state->client_ssh_public_keyfile);
    if (state->client_ssh_private_keyfile)
        xcurl_easy_setopt_ptr(handle, CURLOPT_SSH_PRIVATE_KEYFILE, state->client_ssh_private_keyfile);

    if (data_size != POST_DATA_FROMFILE_PUT && data_size != POST_DATA_GET)
    {
        // Do a HTTP POST. This also makes curl use
        // a "Content-Type: application/x-www-form-urlencoded" header.
        // (This is by far the most commonly used POST method).
        xcurl_easy_setopt_long(handle, CURLOPT_POST, 1);
    }
    // else (only POST_DATA_FROMFILE_PUT): do HTTP PUT.

    struct curl_httppost *post = NULL;
    struct curl_httppost *last = NULL;
    FILE *data_file = NULL;
    FILE *body_stream = NULL;
    struct curl_slist *httpheader_list = NULL;

    // Supply data...
    if (data_size == POST_DATA_FROMFILE
     || data_size == POST_DATA_FROMFILE_PUT
    ) {
        // ...from a file
        data_file = fopen(data, "r");
        if (!data_file)
        {
            perror_msg("Can't open '%s'", data);
            goto ret; // return -1
        }

        xcurl_easy_setopt_ptr(handle, CURLOPT_READDATA, data_file);
        // Want to use custom read function
        xcurl_easy_setopt_ptr(handle, CURLOPT_READFUNCTION, (const void*)fread_with_reporting);
        fseeko(data_file, 0, SEEK_END);
        off_t sz = ftello(data_file);
        fseeko(data_file, 0, SEEK_SET);
        if (data_size == POST_DATA_FROMFILE)
        {
            // Without this, curl would send "Content-Length: -1"
            // servers don't like that: "413 Request Entity Too Large"
            xcurl_easy_setopt_off_t(handle, CURLOPT_POSTFIELDSIZE_LARGE, sz);
        }
        else
        {
            xcurl_easy_setopt_long(handle, CURLOPT_UPLOAD, 1);
            xcurl_easy_setopt_off_t(handle, CURLOPT_INFILESIZE_LARGE, sz);
        }
    }
    else if (data_size == POST_DATA_FROMFILE_AS_FORM_DATA)
    {
        // ...from a file, in multipart/formdata format
        const char *basename = strrchr(data, '/');
        if (basename) basename++;
        else basename = data;

        data_file = fopen(data, "r");
        if (!data_file)
        {
            perror_msg("Can't open '%s'", data);
            goto ret; // return -1
        }
        // Want to use custom read function
        xcurl_easy_setopt_ptr(handle, CURLOPT_READFUNCTION, (const void*)fread_with_reporting);
        // Need to know file size
        fseeko(data_file, 0, SEEK_END);
        off_t sz = ftello(data_file);
        fseeko(data_file, 0, SEEK_SET);
        // Create formdata
        CURLFORMcode curlform_err = curl_formadd(&post, &last,
                        CURLFORM_PTRNAME, "file", // element name
                        // use CURLOPT_READFUNCTION for reading, pass data_file as its last param:
                        CURLFORM_STREAM, data_file,
                        CURLFORM_CONTENTSLENGTH, (long)sz, // a must if we use CURLFORM_STREAM option
//FIXME: what if file size doesn't fit in long?
                        CURLFORM_CONTENTTYPE, content_type,
                        CURLFORM_FILENAME, basename, // filename to put in the form
                        CURLFORM_END);

        if (curlform_err != 0)
//FIXME:
            error_msg_and_die("out of memory or read error (curl_formadd error code: %d)", (int)curlform_err);
        xcurl_easy_setopt_ptr(handle, CURLOPT_HTTPPOST, post);
    }
    else if (data_size == POST_DATA_STRING_AS_FORM_DATA)
    {
        CURLFORMcode curlform_err = curl_formadd(&post, &last,
                        CURLFORM_PTRNAME, "file", // element name
                        // curl bug - missing filename
                        // http://curl.haxx.se/mail/lib-2011-07/0176.html
                        // https://github.com/bagder/curl/commit/45d883d
                        // fixed in curl-7.22.0~144
                        // tested with curl-7.24.0-3
                        // should be working on F17
                        CURLFORM_BUFFER, "*buffer*", // provides filename
                        CURLFORM_BUFFERPTR, data,
                        CURLFORM_BUFFERLENGTH, (long)strlen(data),
//FIXME: what if file size doesn't fit in long?
                        CURLFORM_CONTENTTYPE, content_type,
                        CURLFORM_END);
        if (curlform_err != 0)
            error_msg_and_die("out of memory or read error (curl_formadd error code: %d)", (int)curlform_err);
        xcurl_easy_setopt_ptr(handle, CURLOPT_HTTPPOST, post);
    }
    else if (data_size != POST_DATA_GET)
    {
        // ...from a blob in memory
        xcurl_easy_setopt_ptr(handle, CURLOPT_POSTFIELDS, data);
        // note1: if data_size == POST_DATA_STRING == -1, curl will use strlen(data)
        xcurl_easy_setopt_long(handle, CURLOPT_POSTFIELDSIZE, data_size);
        // We don't use CURLOPT_POSTFIELDSIZE_LARGE because
        // I'm not sure CURLOPT_POSTFIELDSIZE_LARGE special-cases -1.
        // Not a big problem: memory blobs >4GB are very unlikely.
    }

    // Override "Content-Type:"
    if (data_size != POST_DATA_FROMFILE_AS_FORM_DATA
        && data_size != POST_DATA_STRING_AS_FORM_DATA)
    {
        g_autofree char *content_type_header = g_strdup_printf("Content-Type: %s", content_type);
        // Note: curl_slist_append() copies content_type_header
        httpheader_list = curl_slist_append(httpheader_list, content_type_header);
        if (!httpheader_list)
            error_msg_and_die("out of memory");
    }

    for (; additional_headers && *additional_headers; additional_headers++)
    {
        httpheader_list = curl_slist_append(httpheader_list, *additional_headers);
        if (!httpheader_list)
            error_msg_and_die("out of memory");
    }

    // Add User-Agent: ABRT/N.M
    httpheader_list = curl_slist_append(httpheader_list, "User-Agent: ABRT/"VERSION);
    if (!httpheader_list)
        error_msg_and_die("out of memory");

    if (httpheader_list)
        xcurl_easy_setopt_ptr(handle, CURLOPT_HTTPHEADER, httpheader_list);

// Disabled: was observed to also handle "305 Use proxy" redirect,
// apparently with POST->GET remapping - which server didn't like at all.
// Attempted to suppress remapping on 305 using CURLOPT_POSTREDIR of -1,
// but it still did not work.

    // Prepare for saving information
    if (state->flags & POST_WANT_HEADERS)
    {
        xcurl_easy_setopt_ptr(handle, CURLOPT_HEADERFUNCTION, (void*)save_headers);
        xcurl_easy_setopt_ptr(handle, CURLOPT_WRITEHEADER, state);
    }
    if (state->flags & POST_WANT_BODY)
    {
        body_stream = open_memstream(&state->body, &state->body_size);
        if (!body_stream)
            error_msg_and_die("out of memory");
        xcurl_easy_setopt_ptr(handle, CURLOPT_WRITEDATA, body_stream);
    }
    if (!(state->flags & POST_WANT_SSL_VERIFY))
    {
        xcurl_easy_setopt_long(handle, CURLOPT_SSL_VERIFYPEER, 0);
        xcurl_easy_setopt_long(handle, CURLOPT_SSL_VERIFYHOST, 0);
    }
    if (state->client_cert_path && state->client_key_path)
    {
        xcurl_easy_setopt_ptr(handle, CURLOPT_SSLCERTTYPE, "PEM");
        xcurl_easy_setopt_ptr(handle, CURLOPT_SSLKEYTYPE, "PEM");
        xcurl_easy_setopt_ptr(handle, CURLOPT_SSLCERT, state->client_cert_path);
        xcurl_easy_setopt_ptr(handle, CURLOPT_SSLKEY, state->client_key_path);
    }
    if (state->cert_authority_cert_path)
        xcurl_easy_setopt_ptr(handle, CURLOPT_CAINFO, state->cert_authority_cert_path);

    // This is the place where everything happens.
    // Here errors are not limited to "out of memory", can't just die.
    state->curl_result = curl_err = curl_easy_perform_with_proxy(handle, url);
    if (curl_err)
    {
        log_info("curl_easy_perform: error %d", (int)curl_err);
        if (state->flags & POST_WANT_ERROR_MSG)
        {
            state->curl_error_msg = check_curl_error(curl_err, "curl_easy_perform");
            log_debug("curl_easy_perform: error_msg: %s", state->curl_error_msg);
        }
        goto ret;
    }

    // curl-7.20.1 doesn't do it, we get NULL body in the log message below
    // unless we fflush the body memstream ourself
    if (body_stream)
        fflush(body_stream);

    // Headers/body are already saved (if requested), extract more info
    curl_err = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);
    die_if_curl_error(curl_err);
    state->http_resp_code = response_code;
    log_debug("after curl_easy_perform: response_code:%ld body:'%s'", response_code, state->body);

 ret:
    curl_easy_cleanup(handle);
    if (httpheader_list)
        curl_slist_free_all(httpheader_list);
    if (body_stream)
        fclose(body_stream);
    if (data_file)
        fclose(data_file);
    if (post)
        curl_formfree(post);

    return response_code;
}

/* Unlike post_file(),
 * this function will use PUT, not POST if url is "http(s)://..."
 */
char *libreport_upload_file(const char *url, const char *filename)
{
    post_state_t *state = new_post_state(POST_WANT_ERROR_MSG);
    char *retval = libreport_upload_file_ext(state, url, filename, UPLOAD_FILE_NOFLAGS);
    free_post_state(state);

    return retval;
}

char *libreport_upload_file_ext(post_state_t *state, const char *url, const char *filename, int flags)
{
    /* we don't want to print the whole url as it may contain password
     * rhbz#856960
     *
     * jfilak:
     * We want to print valid URLs in useful messages.
     *
     * The old code had this approach:
     *   there can be '@' in the login or password so let's try to find the
     *   first '@' from the end
     *
     * The new implementation decomposes URI to its base elements and uses only
     * scheme and hostname for the logging purpose. These elements should not
     * contain any sensitive information.
     */
    const char *username_bck = state->username;
    const char *password_bck = state->password;

    g_autofree char *whole_url = NULL;
    g_autofree char *scheme = NULL;
    g_autofree char *hostname = NULL;
    g_autofree char *username = NULL;
    g_autofree char *password = NULL;
    g_autofree char *clean_url = NULL;

    if (libreport_uri_userinfo_remove(url, &clean_url, &scheme, &hostname, &username, &password, NULL) != 0)
        goto finito;

    if (scheme == NULL || hostname == NULL)
    {
        log_warning(_("Ignoring URL without scheme and hostname"));
        goto finito;
    }

    if (username && (state->username == NULL || state->username[0] == '\0'))
    {
        state->username = username;
        state->password = password;
    }

    unsigned len = strlen(clean_url);
    if (len > 0 && clean_url[len-1] == '/')
        whole_url = g_build_filename(clean_url, strrchr(filename, '/') ? : filename, NULL);
    else
        whole_url = g_strdup(clean_url);

    /* work around bug in libssh2(curl with scp://)
     * libssh2_aget_disconnect() calls close(0)
     * https://bugzilla.redhat.com/show_bug.cgi?id=1147717
     */
    int stdin_bck = dup(0);

    /*
     * Well, goto seems to be the most elegant syntax form here :(
     * This label is used to re-try the upload with an updated credentials.
     */
  do_post:

    /* Do not include the path part of the URL as it can contain sensitive data
     * in case of typos */
    log_warning(_("Sending %s to %s//%s"), filename, scheme, hostname);
    post(state,
                whole_url,
                /*content_type:*/ "application/octet-stream",
                /*additional_headers:*/ NULL,
                /*data:*/ filename,
                POST_DATA_FROMFILE_PUT
    );

    dup2(stdin_bck, 0);

    int error = (state->curl_result != 0);
    if (error)
    {
        if (state->curl_error_msg)
            error_msg("Error while uploading: '%s'", state->curl_error_msg);
        else
            /* for example, when source file can't be opened */
            error_msg("Error while uploading");

        if ((flags & UPLOAD_FILE_HANDLE_ACCESS_DENIALS) &&
                (state->curl_result == CURLE_LOGIN_DENIED
                 || state->curl_result == CURLE_REMOTE_ACCESS_DENIED))
        {
            char *msg = g_strdup_printf(_("Please enter user name for '%s//%s':"), scheme, hostname);
            username = libreport_ask(msg);
            free(msg);
            if (username != NULL && username[0] != '\0')
            {
                msg = g_strdup_printf(_("Please enter password for '%s//%s@%s':"), scheme, username, hostname);
                password = libreport_ask_password(msg);
                free(msg);
                /* What about empty password? */
                if (password != NULL && password[0] != '\0')
                {
                    state->username = username;
                    state->password = password;
                    /*
                     * Re-try with new credentials
                     */
                    goto do_post;
                }
            }
        }
        whole_url = NULL;
    }
    else
    {
        /* This ends up a "reporting status message" in abrtd */
        log_warning(_("Successfully created %s"), whole_url);
    }

    close(stdin_bck);

finito:
    state->username = username_bck;
    state->password = password_bck;

    return whole_url;
}
