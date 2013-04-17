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
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#include <curl/curl.h>
#include "internal_libreport.h"
#include "libreport_curl.h"
#include "abrt_rh_support.h"

struct reportfile {
    xmlTextWriterPtr writer;
    xmlBufferPtr     buf;
};

static void __attribute__((__noreturn__))
die_xml_oom(void)
{
    error_msg_and_die("Can't create XML attribute (out of memory?)");
}

static xmlBufferPtr
xxmlBufferCreate(void)
{
    xmlBufferPtr r = xmlBufferCreate();
    if (!r)
        die_xml_oom();
    return r;
}

static xmlTextWriterPtr
xxmlNewTextWriterMemory(xmlBufferPtr buf /*, int compression*/)
{
    xmlTextWriterPtr r = xmlNewTextWriterMemory(buf, /*compression:*/ 0);
    if (!r)
        die_xml_oom();
    return r;
}

static void
xxmlTextWriterStartDocument(xmlTextWriterPtr writer,
    const char * version,
    const char * encoding,
    const char * standalone)
{
    if (xmlTextWriterStartDocument(writer, version, encoding, standalone) < 0)
        die_xml_oom();
}

static void
xxmlTextWriterEndDocument(xmlTextWriterPtr writer)
{
    if (xmlTextWriterEndDocument(writer) < 0)
        die_xml_oom();
}

static void
xxmlTextWriterStartElement(xmlTextWriterPtr writer, const char *name)
{
    // these bright guys REDEFINED CHAR (!) to unsigned char...
    if (xmlTextWriterStartElement(writer, (unsigned char*)name) < 0)
        die_xml_oom();
}

static void
xxmlTextWriterEndElement(xmlTextWriterPtr writer)
{
    if (xmlTextWriterEndElement(writer) < 0)
        die_xml_oom();
}

static void
xxmlTextWriterWriteElement(xmlTextWriterPtr writer, const char *name, const char *content)
{
    if (xmlTextWriterWriteElement(writer, (unsigned char*)name, (unsigned char*)content) < 0)
        die_xml_oom();
}

static void
xxmlTextWriterWriteAttribute(xmlTextWriterPtr writer, const char *name, const char *content)
{
    if (xmlTextWriterWriteAttribute(writer, (unsigned char*)name, (unsigned char*)content) < 0)
        die_xml_oom();
}

#if 0 //unused
static void
xxmlTextWriterWriteString(xmlTextWriterPtr writer, const char *content)
{
    if (xmlTextWriterWriteString(writer, (unsigned char*)content) < 0)
        die_xml_oom();
}
#endif

//
// Reportfile helpers
//

// End the reportfile, and prepare it for delivery.
// No more bindings can be added after this.
static void
close_writer(reportfile_t* file)
{
    if (!file->writer)
        return;

    // close off the end of the xml file
    xxmlTextWriterEndDocument(file->writer);
    xmlFreeTextWriter(file->writer);
    file->writer = NULL;
}

// This allocates a reportfile_t structure and initializes it.
reportfile_t*
new_reportfile(void)
{
    // create a new reportfile_t
    reportfile_t* file = (reportfile_t*)xmalloc(sizeof(*file));

    // set up a libxml 'buffer' and 'writer' to that buffer
    file->buf = xxmlBufferCreate();
    file->writer = xxmlNewTextWriterMemory(file->buf);

    // start a new xml document:
    // <report xmlns="http://www.redhat.com/gss/strata">...
    xxmlTextWriterStartDocument(file->writer, /*version:*/ NULL, /*encoding:*/ NULL, /*standalone:*/ NULL);
    xxmlTextWriterStartElement(file->writer, "report");
    xxmlTextWriterWriteAttribute(file->writer, "xmlns", "http://www.redhat.com/gss/strata");

    return file;
}

static void
internal_reportfile_start_binding(reportfile_t* file, const char* name, int isbinary, const char* filename)
{
    // <binding name=NAME [fileName=FILENAME] type=text/binary...
    xxmlTextWriterStartElement(file->writer, "binding");
    xxmlTextWriterWriteAttribute(file->writer, "name", name);
    if (filename)
        xxmlTextWriterWriteAttribute(file->writer, "fileName", filename);
    if (isbinary)
        xxmlTextWriterWriteAttribute(file->writer, "type", "binary");
    else
        xxmlTextWriterWriteAttribute(file->writer, "type", "text");
}

// Add a new text binding
void
reportfile_add_binding_from_string(reportfile_t* file, const char* name, const char* value)
{
    // <binding name=NAME type=text value=VALUE>
    internal_reportfile_start_binding(file, name, /*isbinary:*/ 0, /*filename:*/ NULL);
    xxmlTextWriterWriteAttribute(file->writer, "value", value);
    xxmlTextWriterEndElement(file->writer);
}

// Add a new binding to a report whose value is represented as a file.
void
reportfile_add_binding_from_namedfile(reportfile_t* file,
                const char* on_disk_filename, /* unused so far */
                const char* binding_name,
                const char* recorded_filename,
                int isbinary)
{
    // <binding name=NAME fileName=FILENAME type=text/binary...
    internal_reportfile_start_binding(file, binding_name, isbinary, recorded_filename);
    // ... href=content/NAME>
    char *href_name = concat_path_file("content", binding_name);
    xxmlTextWriterWriteAttribute(file->writer, "href", href_name);
    free(href_name);
}

// Return the contents of the reportfile as a string.
const char*
reportfile_as_string(reportfile_t* file)
{
    close_writer(file);
    // unsigned char -> char
    return (char*)file->buf->content;
}

void
free_reportfile(reportfile_t* file)
{
    if (!file)
        return;
    close_writer(file);
    xmlBufferFree(file->buf);
    free(file);
}


void free_rhts_result(rhts_result_t *p)
{
    if (!p)
        return;
    free(p->url);
    free(p->msg);
    free(p->body);
    free(p);
}

//
// Common
//
static const char *const text_plain_header[] = {
    "Accept: text/plain",
    NULL
};

//
// Creating new case
// See
// https://access.redhat.com/knowledge/docs/Red_Hat_Customer_Portal/integration_guide.html
//
// $ curl -X POST -H 'Content-Type: application/xml' --data
//  '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
//   <case xmlns="http://www.redhat.com/gss/strata">
//   <summary>Example Case</summary>
//   <description>Example created with cURL</description>
//   <product>Red Hat Enterprise Linux</product><version>6.0</version>
//   </case>'
//   https://api.access.redhat.com/rs/cases
//
static char*
make_case_data(const char* summary, const char* description,
               const char* product, const char* version,
               const char* component)
{
    char* retval;
    xmlTextWriterPtr writer;
    xmlBufferPtr buf;

    buf = xxmlBufferCreate();
    writer = xxmlNewTextWriterMemory(buf);

    xxmlTextWriterStartDocument(writer, NULL, "UTF-8", "yes");
    xxmlTextWriterStartElement(writer, "case");
    xxmlTextWriterWriteAttribute(writer, "xmlns",
                                   "http://www.redhat.com/gss/strata");

    xxmlTextWriterWriteElement(writer, "summary", summary);
    xxmlTextWriterWriteElement(writer, "description", description);
    if (product) {
        xxmlTextWriterWriteElement(writer, "product", product);
    }
    if (version) {
        xxmlTextWriterWriteElement(writer, "version", version);
    }
    if (component) {
        xxmlTextWriterWriteElement(writer, "component", component);
    }

    xxmlTextWriterEndDocument(writer);
    retval = xstrdup((const char*)buf->content);
    xmlFreeTextWriter(writer);
    xmlBufferFree(buf);
    return retval;
}

static rhts_result_t*
post_case_to_url(const char* url,
                const char* username,
                const char* password,
                bool ssl_verify,
                const char **additional_headers,
                const char* product,
                const char* version,
                const char* summary,
                const char* description,
                const char* component)
{
    rhts_result_t *result = xzalloc(sizeof(*result));
    char *url_copy = NULL;

    char *case_data = make_case_data(summary, description,
                                         product, version,
                                         component);

    int redirect_count = 0;
    char *errmsg;
    post_state_t *post_state;

 redirect:
    post_state = new_post_state(0
            + POST_WANT_HEADERS
            + POST_WANT_BODY
            + POST_WANT_ERROR_MSG
            + (ssl_verify ? POST_WANT_SSL_VERIFY : 0)
    );
    post_state->username = username;
    post_state->password = password;

    post_string(post_state, url, "application/xml", additional_headers, case_data);

    char *location = find_header_in_post_state(post_state, "Location:");

    switch (post_state->http_resp_code)
    {
    case 404:
        /* Not strictly necessary (default branch would deal with it too),
         * but makes this typical error less cryptic:
         * instead of returning html-encoded body, we show short concise message,
         * and show offending URL (typos in which is a typical cause) */
        result->error = -1;
        result->msg = xasprintf("Error in HTTP POST, "
                        "HTTP code: 404 (Not found), URL:'%s'", url);
        break;

    case 301: /* "301 Moved Permanently" (for example, used to move http:// to https://) */
    case 302: /* "302 Found" (just in case) */
    case 305: /* "305 Use Proxy" */
        if (++redirect_count < 10 && location)
        {
            free(url_copy);
            url = url_copy = xstrdup(location);
            free_post_state(post_state);
            goto redirect;
        }
        /* fall through */

    default:
        // TODO: error messages in headers
        // are observed to be more informative than the body:
        //
        // 'HTTP/1.1 400 Bad Request'
        // 'Date: Mon, 10 Oct 2011 13:31:56 GMT^M'
        // 'Server: Apache^M'
        // 'Strata-Message: The supplied parameter Fedora value  can not be processed^M'
        // ^^^^^^^^^^^^^^^^^^^^^^^^^ useful message
        // 'Strata-Code: BAD_PARAMETER^M'
        // 'Content-Length: 1^M'
        // 'Content-Type: text/plain; charset=UTF-8^M'
        // 'Connection: close^M'
        // '^M'
        // ' '  <------ body is useless
        result->error = -1;
        errmsg = post_state->curl_error_msg;
        if (errmsg && errmsg[0])
        {
            result->msg = xasprintf(_("Error in case creation at '%s': %s"),
                    url, errmsg);
        }
        else
        {
            errmsg = find_header_in_post_state(post_state, "Strata-Message:");
            if (!errmsg)
                errmsg = post_state->body;
            if (errmsg && errmsg[0])
                result->msg = xasprintf(_("Error in case creation at '%s',"
                        " HTTP code: %d, server says: '%s'"),
                        url, post_state->http_resp_code, errmsg);
            else
                result->msg = xasprintf(_("Error in case creation at '%s',"
                        " HTTP code: %d"),
                        url, post_state->http_resp_code);
        }
        break;

    case 200:
    case 201:
        /* Created successfully */
        result->url = xstrdup(location); /* note: xstrdup(NULL) returns NULL */
    } /* switch (HTTP code) */

    result->http_resp_code = post_state->http_resp_code;
    result->body = post_state->body;
    post_state->body = NULL;

    free_post_state(post_state);
    free(case_data);
    free(url_copy);
    return result;
}

rhts_result_t*
create_new_case(const char* base_url,
                const char* username,
                const char* password,
                bool ssl_verify,
                const char* product,
                const char* version,
                const char* summary,
                const char* description,
                const char* component)
{
    char *url = concat_path_file(base_url, "cases");
    rhts_result_t *result = post_case_to_url(url,
                username,
                password,
                ssl_verify,
                (const char **)text_plain_header,
                product,
                version,
                summary,
                description,
                component
    );

    if (!result->error && !result->url)
    {
        /* Case Creation returned valid code, but no location */
        result->error = -1;
        free(result->msg);
        result->msg = xasprintf(_("Error in case creation at '%s':"
                " no Location URL, HTTP code: %d"),
                url, result->http_resp_code
        );
    }
    free(url);

    return result;
}

//
// Add case comment
//
// $ curl -X POST -H 'Content-Type: application/xml' --data
//  '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
//   <comment xmlns="http://www.redhat.com/gss/strata">
//   <text>Test comment!  This can contain lots of information, etc.</text>
//   </comment>'
//   https://api.access.redhat.com/rs/cases/NNNNNNN/comments
//
static char*
make_comment_data(const char *comment_text)
{
    char *retval;
    xmlTextWriterPtr writer;
    xmlBufferPtr buf;

    buf = xxmlBufferCreate();
    writer = xxmlNewTextWriterMemory(buf);

    xxmlTextWriterStartDocument(writer, NULL, "UTF-8", "yes");
    xxmlTextWriterStartElement(writer, "comment");
    xxmlTextWriterWriteAttribute(writer, "xmlns",
                                   "http://www.redhat.com/gss/strata");

    xxmlTextWriterWriteElement(writer, "text", comment_text);

    xxmlTextWriterEndDocument(writer);
    retval = xstrdup((const char*)buf->content);
    xmlFreeTextWriter(writer);
    xmlBufferFree(buf);
    return retval;
}

static rhts_result_t*
post_comment_to_url(const char *url,
                const char *username,
                const char *password,
                bool ssl_verify,
                const char **additional_headers,
                const char *comment_text)
{
    rhts_result_t *result = xzalloc(sizeof(*result));
    char *url_copy = NULL;

    char *xml = make_comment_data(comment_text);

    int redirect_count = 0;
    char *errmsg;
    post_state_t *post_state;

 redirect:
    post_state = new_post_state(0
            + POST_WANT_HEADERS
            + POST_WANT_BODY
            + POST_WANT_ERROR_MSG
            + (ssl_verify ? POST_WANT_SSL_VERIFY : 0)
    );
    post_state->username = username;
    post_state->password = password;

    post_string(post_state, url, "application/xml", additional_headers, xml);

    char *location = find_header_in_post_state(post_state, "Location:");

    switch (post_state->http_resp_code)
    {
    case 404:
        /* Not strictly necessary (default branch would deal with it too),
         * but makes this typical error less cryptic:
         * instead of returning html-encoded body, we show short concise message,
         * and show offending URL (typos in which is a typical cause) */
        result->error = -1;
        result->msg = xasprintf("Error in HTTP POST, "
                        "HTTP code: 404 (Not found), URL:'%s'", url);
        break;

    case 301: /* "301 Moved Permanently" (for example, used to move http:// to https://) */
    case 302: /* "302 Found" (just in case) */
    case 305: /* "305 Use Proxy" */
        if (++redirect_count < 10 && location)
        {
            free(url_copy);
            url = url_copy = xstrdup(location);
            free_post_state(post_state);
            goto redirect;
        }
        /* fall through */

    default:
        result->error = -1;
        errmsg = post_state->curl_error_msg;
        if (errmsg && errmsg[0])
        {
            result->msg = xasprintf(_("Error in comment creation at '%s': %s"),
                        url, errmsg);
        }
        else
        {
            errmsg = find_header_in_post_state(post_state, "Strata-Message:");
            if (!errmsg)
                errmsg = post_state->body;
            if (errmsg && errmsg[0])
                result->msg = xasprintf(_("Error in comment creation at '%s',"
                        " HTTP code: %d, server says: '%s'"),
                        url, post_state->http_resp_code, errmsg);
            else
                result->msg = xasprintf(_("Error in comment creation at '%s',"
                        " HTTP code: %d"),
                        url, post_state->http_resp_code);
        }
        break;

    case 200:
    case 201:
        /* Created successfully */
        result->url = xstrdup(location); /* note: xstrdup(NULL) returns NULL */
    } /* switch (HTTP code) */

    result->http_resp_code = post_state->http_resp_code;
    result->body = post_state->body;
    post_state->body = NULL;

    free_post_state(post_state);
    free(xml);
    free(url_copy);
    return result;
}

rhts_result_t*
add_comment_to_case(const char* base_url,
                const char* username,
                const char* password,
                bool ssl_verify,
                const char* comment_text)
{
    char *url = concat_path_file(base_url, "comments");
    rhts_result_t *result = post_comment_to_url(url,
                username,
                password,
                ssl_verify,
    // NB! text_plain_header here was causing error 404 instead of 201 (Created)!
    // NULL makes curl use "Accept: */*" instead and creation works.
    // Likely a bug on the server!
                (const char **) NULL, //text_plain_header,
                comment_text
    );

    if (!result->error && !result->url)
    {
        /* Creation returned valid code, but no location */
        result->error = -1;
        free(result->msg);
        result->msg = xasprintf(_("Error in comment creation at '%s':"
                " no Location URL, HTTP code: %d"),
                url, result->http_resp_code
        );
    }
    free(url);

    return result;
}

//
// Attach file to case
//
static rhts_result_t*
post_file_to_url(const char* url,
                const char* username,
                const char* password,
                bool ssl_verify,
                bool post_as_form,
                const char **additional_headers,
                const char *file_name)
{
    rhts_result_t *result = xzalloc(sizeof(*result));
    char *url_copy = NULL;

    int redirect_count = 0;
    char *errmsg;
    post_state_t *atch_state;

 redirect_attach:
    atch_state = new_post_state(0
            + POST_WANT_HEADERS
            + POST_WANT_BODY
            + POST_WANT_ERROR_MSG
            + (ssl_verify ? POST_WANT_SSL_VERIFY : 0)
    );
    atch_state->username = username;
    atch_state->password = password;
    if (post_as_form)
    {
        /* Sends data in multipart/mixed document. One detail is that
	 * file *name* is also sent to the server.
	 */
        post_file_as_form(atch_state,
            url,
            "application/octet-stream",
            additional_headers,
            file_name
        );
    }
    else
    {
        /* Sends file's raw contents */
        post_file(atch_state,
            url,
            "application/octet-stream",
            additional_headers,
            file_name
        );
    }

    char *atch_location = find_header_in_post_state(atch_state, "Location:");

    switch (atch_state->http_resp_code)
    {
    case 305: /* "305 Use Proxy" */
        if (++redirect_count < 10 && atch_location)
        {
            free(url_copy);
            url = url_copy = xstrdup(atch_location);
            free_post_state(atch_state);
            goto redirect_attach;
        }
        /* fall through */

    default:
        /* Error */
        result->error = -1;
        errmsg = atch_state->curl_error_msg;
        if (errmsg && errmsg[0])
        {
            result->msg = xasprintf("Error in file upload at '%s': %s",
                    url, errmsg);
        }
        else
        {
            errmsg = atch_state->body;
            if (errmsg && errmsg[0])
                result->msg = xasprintf("Error in file upload at '%s',"
                        " HTTP code: %d, server says: '%s'",
                        url, atch_state->http_resp_code, errmsg);
            else
                result->msg = xasprintf("Error in file upload at '%s',"
                        " HTTP code: %d",
                        url, atch_state->http_resp_code);
        }
        break;

    case 200:
    case 201:
        result->url = xstrdup(atch_location); /* note: xstrdup(NULL) returns NULL */
        //result->msg = xstrdup("File uploaded successfully");
    } /* switch (HTTP code) */

    result->http_resp_code = atch_state->http_resp_code;
    result->body = atch_state->body;
    atch_state->body = NULL;

    free_post_state(atch_state);
    free(url_copy);
    return result;
}

rhts_result_t*
attach_file_to_case(const char* base_url,
                const char* username,
                const char* password,
                bool ssl_verify,
                const char *file_name)
{
    char *url = concat_path_file(base_url, "attachments");
    rhts_result_t *result = post_file_to_url(url,
                username,
                password,
                ssl_verify,
                /*post_as_form:*/ true,
                (const char **) text_plain_header,
                file_name
    );
    free(url);
    return result;
}

//
// Get hint
//
rhts_result_t*
get_rhts_hints(const char* base_url,
                const char* username,
                const char* password,
                bool ssl_verify,
                const char* file_name)
{
    char *url = concat_path_file(base_url, "problems");
//    rhts_result_t *result = post_case_to_url(url,
//                username,
//                password,
//                ssl_verify,
//                NULL,
//                release,
//                summary,
//                description,
//                component
//    );
    rhts_result_t *result = post_file_to_url(url,
                username,
                password,
                ssl_verify,
                /*post_as_form:*/ false,
                /*headers:*/ NULL,
                file_name
    );
    free(url);
    return result;
}
