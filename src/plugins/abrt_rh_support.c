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
#include "abrt_curl.h"
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
// End the reportfile, and prepare it for delivery.
// No more bindings can be added after this.
//
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

//
// This allocates a reportfile_t structure and initializes it.
//
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

//
// Add a new text binding
//
void
reportfile_add_binding_from_string(reportfile_t* file, const char* name, const char* value)
{
    // <binding name=NAME type=text value=VALUE>
    internal_reportfile_start_binding(file, name, /*isbinary:*/ 0, /*filename:*/ NULL);
    xxmlTextWriterWriteAttribute(file->writer, "value", value);
    xxmlTextWriterEndElement(file->writer);
}

//
// Add a new binding to a report whose value is represented as a file.
//
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

//
// Return the contents of the reportfile as a string.
//
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
// create_new_case()
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

#if 0 //unused
static char*
make_response(const char* title, const char* body,
              const char* actualURL, const char* displayURL)
{
    char* retval;
    xmlTextWriterPtr writer;
    xmlBufferPtr buf;

    buf = xxmlBufferCreate();
    writer = xxmlNewTextWriterMemory(buf);

    xxmlTextWriterStartDocument(writer, NULL, "UTF-8", "yes");
    xxmlTextWriterStartElement(writer, "response");
    if (title) {
        xxmlTextWriterWriteElement(writer, "title", title);
    }
    if (body) {
        xxmlTextWriterWriteElement(writer, "body", body);
    }
    if (actualURL || displayURL) {
        xxmlTextWriterStartElement(writer, "URL");
        if (actualURL) {
            xxmlTextWriterWriteAttribute(writer, "href", actualURL);
        }
        if (displayURL) {
            xxmlTextWriterWriteString(writer, displayURL);
        }
    }

    xxmlTextWriterEndDocument(writer);
    retval = xstrdup((const char*)buf->content);
    xmlFreeTextWriter(writer);
    xmlBufferFree(buf);
    return retval;
}
//Example:
//<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
//<response><title>Case Created and Report Attached</title><body></body><URL href="http://support-services-devel.gss.redhat.com:8080/Strata/cases/00005129/attachments/ccbf3e65-b941-3db7-a016-6a3831691a32">New Case URL</URL></response>
#endif

static const char *const text_plain_header[] = {
    "Accept: text/plain",
    NULL
};

static rhts_result_t*
post_case_to_url(const char* url,
                const char* username,
                const char* password,
                bool ssl_verify,
                const char **additional_headers,
                const char* release,
                const char* summary,
                const char* description,
                const char* component)
{
    rhts_result_t *result = xzalloc(sizeof(*result));
    char *url_copy = NULL;

    char *product = NULL;
    char *version = NULL;
    parse_release_for_rhts(release, &product, &version);
    char *case_data = make_case_data(summary, description,
                                         product, version,
                                         component);
    free(product);
    free(version);

    int redirect_count = 0;
    char *errmsg;
    char *allocated = NULL;
    abrt_post_state_t *case_state;

 redirect_case:
    case_state = new_abrt_post_state(0
            + ABRT_POST_WANT_HEADERS
            + ABRT_POST_WANT_BODY
            + ABRT_POST_WANT_ERROR_MSG
            + (ssl_verify ? ABRT_POST_WANT_SSL_VERIFY : 0)
    );
    case_state->username = username;
    case_state->password = password;

    abrt_post_string(case_state, url, "application/xml", additional_headers, case_data);

    char *case_location = find_header_in_abrt_post_state(case_state, "Location:");
    result->http_resp_code = case_state->http_resp_code;

    switch (case_state->http_resp_code)
    {
    case 404:
        /* Not strictly necessary (default branch would deal with it too),
         * but makes this typical error less cryptic:
         * instead of returning html-encoded body, we show short concise message,
         * and show offending URL (typos in which is a typical cause) */
        result->error = -1;
        result->msg = xasprintf("error in HTTP POST, "
                        "HTTP code: 404 (Not found), URL:'%s'", url);
        break;

    case 301: /* "301 Moved Permanently" (for example, used to move http:// to https://) */
    case 302: /* "302 Found" (just in case) */
    case 305: /* "305 Use Proxy" */
        if (++redirect_count < 10 && case_location)
        {
            free(url_copy);
            url = url_copy = xstrdup(case_location);
            free_abrt_post_state(case_state);
            goto redirect_case;
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
        errmsg = case_state->curl_error_msg;
        if (errmsg && errmsg[0])
        {
            result->msg = xasprintf("error in case creation: %s", errmsg);
        }
        else
        {
            errmsg = case_state->body;
            if (errmsg && errmsg[0])
                result->msg = xasprintf("error in case creation, HTTP code: %d, server says: '%s'",
                        case_state->http_resp_code, errmsg);
            else
                result->msg = xasprintf("error in case creation, HTTP code: %d",
                        case_state->http_resp_code);
        }
        result->body = case_state->body;
        case_state->body = NULL;
        break;

    case 200:
    case 201:
        /* Cose created successfully */
        result->url = xstrdup(case_location);
        //result->msg = xstrdup("Case created");
        result->body = case_state->body;
        case_state->body = NULL;
    } /* switch (case HTTP code) */

    free_abrt_post_state(case_state);
    free(allocated);
    free(case_data);
    free(url_copy);
    return result;
}

rhts_result_t*
create_new_case(const char* base_url,
                const char* username,
                const char* password,
                bool ssl_verify,
                const char* release,
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
                release,
                summary,
                description,
                component
    );
    free(url);

    if (!result->url)
    {
        /* Case Creation returned valid code, but no location */
        result->error = -1;
        free(result->msg);
        result->msg = xasprintf("error in case creation: no Location URL, HTTP code: %d",
                result->http_resp_code
        );
    }

    return result;
}

rhts_result_t*
get_rhts_hints(const char* base_url,
                const char* username,
                const char* password,
                bool ssl_verify,
                const char* release,
                const char* summary,
                const char* description,
                const char* component)
{
    char *url = concat_path_file(base_url, "problems");
    rhts_result_t *result = post_case_to_url(url,
                username,
                password,
                ssl_verify,
                NULL,
                release,
                summary,
                description,
                component
    );
    free(url);
    return result;
}

rhts_result_t*
attach_file_to_case(const char* baseURL,
                const char* username,
                const char* password,
                bool ssl_verify,
                const char *file_name)
{
    rhts_result_t *result = xzalloc(sizeof(*result));

    int redirect_count = 0;
    char *atch_url = concat_path_file(baseURL, "attachments");
    abrt_post_state_t *atch_state;

 redirect_attach:
    atch_state = new_abrt_post_state(0
            + ABRT_POST_WANT_HEADERS
            + ABRT_POST_WANT_BODY
            + ABRT_POST_WANT_ERROR_MSG
            + (ssl_verify ? ABRT_POST_WANT_SSL_VERIFY : 0)
    );
    atch_state->username = username;
    atch_state->password = password;
    abrt_post_file_as_form(atch_state,
        atch_url,
        "application/binary",
        (const char **) text_plain_header,
        file_name
    );

    switch (atch_state->http_resp_code)
    {
    case 305: /* "305 Use Proxy" */
        {
            char *atch_location = find_header_in_abrt_post_state(atch_state, "Location:");
            if (++redirect_count < 10 && atch_location)
            {
                free(atch_url);
                atch_url = xstrdup(atch_location);
                free_abrt_post_state(atch_state);
                goto redirect_attach;
            }
        }
        /* fall through */

    default:
        /* Error */
        {
            char *allocated = NULL;
            const char *errmsg = atch_state->curl_error_msg;
            if (atch_state->body && atch_state->body[0])
            {
                if (errmsg && errmsg[0]
                 && strcmp(errmsg, atch_state->body) != 0
                ) /* both strata/curl error and body are present (and aren't the same) */
                    errmsg = allocated = xasprintf("%s. %s",
                            atch_state->body,
                            errmsg);
                else /* only body exists */
                    errmsg = atch_state->body;
            }
            result->error = -1;
            result->msg = xasprintf("Attachment failed (HTTP code %d)%s%s",
                    atch_state->http_resp_code,
                    errmsg ? ": " : "",
                    errmsg ? errmsg : ""
            );
            free(allocated);
        }
        break;

    case 200:
    case 201:
        {
            char *loc = find_header_in_abrt_post_state(atch_state, "Location:");
            if (loc)
                result->url = xstrdup(loc);
            //result->msg = xstrdup("File attached successfully");
        }
    } /* switch */

    free_abrt_post_state(atch_state);
    free(atch_url);
    return result;
}
