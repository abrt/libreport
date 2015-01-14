/*
    Copyright (C) 2014  ABRT team
    Copyright (C) 2014  RedHat Inc

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

#include <curl/curl.h>

#include <libxml/xmlreader.h>

#include "internal_libreport.h"
#include "libreport_curl.h"
#include "mantisbt.h"

/*
 * SOAP
*/

#define XML_VERSION "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"

/* fprint string */
#define SOAP_TEMPLATE \
    "<SOAP-ENV:Envelope xmlns:ns3=\"http://futureware.biz/mantisconnect\" " \
        "xmlns:SOAP-ENC=\"http://schemas.xmlsoap.org/soap/encoding/\" " \
        "xmlns:ns0=\"http://schemas.xmlsoap.org/soap/encoding/\" " \
        "xmlns:ns1=\"http://schemas.xmlsoap.org/soap/envelope/\" " \
        "xmlns:ns2=\"http://www.w3.org/2001/XMLSchema\" " \
        "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" " \
        "xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" " \
        "SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">" \
    "<SOAP-ENV:Header/>" \
    "<ns1:Body>" \
        "<ns3:%s>" \
        "</ns3:%s>" \
    "</ns1:Body>" \
    "</SOAP-ENV:Envelope>"

#define MAX_SUMMARY_LENGTH  128
#define CUSTOMFIELD_DUPHASH "abrt_hash"
#define CUSTOMFIELD_DUPHASH_ID "1"
#define CUSTOMFIELD_URL_ID "2"
#define MAX_HOPS 5

/* MantisBT limit is 2MB by default
 */
#define MANTISBT_MAX_FILE_UPLOAD_SIZE (2 * 1024 * 1024)

/*
 * MantisBT settings issue info
 */
void
mantisbt_settings_free(mantisbt_settings_t *s)
{
    if (s == NULL)
        return;

    free(s->m_login);
    free(s->m_password);
    free(s->m_project);
    free(s->m_project_version);
}

/*
 * MantisBT issue info
 */
mantisbt_issue_info_t *
mantisbt_issue_info_new()
{
    mantisbt_issue_info_t *info = xzalloc(sizeof(mantisbt_issue_info_t));
    info->mii_id = -1;
    info->mii_dup_id = -1;

    return info;
}

void
mantisbt_issue_info_free(mantisbt_issue_info_t *info)
{
    if (info == NULL)
        return;

    free(info->mii_status);
    free(info->mii_resolution);
    free(info->mii_reporter);
    free(info->mii_project);

    list_free_with_free(info->mii_notes);
    list_free_with_free(info->mii_attachments);

    free(info);
}

mantisbt_issue_info_t *
mantisbt_find_origin_bug_closed_duplicate(mantisbt_settings_t *settings, mantisbt_issue_info_t *info)
{
    mantisbt_issue_info_t *info_tmp = mantisbt_issue_info_new();
    info_tmp->mii_id = info->mii_id;
    info_tmp->mii_dup_id = info->mii_dup_id;

    for (int ii = 0; ii <= MAX_HOPS; ii++)
    {
        if (ii == MAX_HOPS)
            error_msg_and_die(_("MantisBT couldn't find parent of issue %d"), info->mii_id);

        log("Issue %d is a duplicate, using parent issue %d", info_tmp->mii_id, info_tmp->mii_dup_id);
        int issue_id = info_tmp->mii_dup_id;

        mantisbt_issue_info_free(info_tmp);
        info_tmp = mantisbt_get_issue_info(settings, issue_id);

        // found a issue which is not CLOSED as DUPLICATE
        if (info_tmp->mii_dup_id == -1)
            break;
    }

    return info_tmp;
}

/*
 * SOAP request
 */
static soap_request_t *
soap_request_new()
{
    soap_request_t *req = xzalloc(sizeof(*req));

    return req;
}

void
soap_request_free(soap_request_t *req)
{
    if (req == NULL)
        return;

    if (req->sr_root != NULL)
        xmlFreeDoc(req->sr_root->doc);

    free(req);

    return;
}

static xmlNodePtr
soap_node_get_next_element_node(xmlNodePtr node)
{
    for (; node != NULL; node = node->next)
        if (node->type == XML_ELEMENT_NODE)
            break;

    return node;
}

static xmlNodePtr
soap_node_get_child_element(xmlNodePtr node)
{
    if (node == NULL)
        error_msg_and_die(_("SOAP: Failed to get child element because of no parent."));

    return soap_node_get_next_element_node(node->xmlChildrenNode);
}

static xmlNodePtr
soap_node_get_next_sibling(xmlNodePtr node)
{
    if (node == NULL)
        error_msg_and_die(_("SOAP: Failed to get next element because of no node."));

    return soap_node_get_next_element_node(node->next);
}

static xmlNodePtr
soap_node_get_child_node(xmlNodePtr parent, const char *name)
{
    if (parent == NULL)
        error_msg_and_die(_("SOAP: Failed to get child node because of no parent."));

    xmlNodePtr node;
    for (node = soap_node_get_child_element(parent); node != NULL; node = soap_node_get_next_sibling(node))
    {
        if (xmlStrcmp(node->name, BAD_CAST name) == 0)
            return node;
    }

    return NULL;
}

soap_request_t *
soap_request_new_for_method(const char *method)
{
    char *xml_str = xasprintf(SOAP_TEMPLATE, method, method);

    xmlDocPtr doc = xmlParseDoc(BAD_CAST xml_str);
    free(xml_str);

    if (doc == NULL)
        error_msg_and_die(_("SOAP: Failed to parse xml during creating request."));

    soap_request_t *req = soap_request_new();

    req->sr_root = xmlDocGetRootElement(doc);
    if (req->sr_root == NULL)
    {
        soap_request_free(req);
        error_msg_and_die(_("SOAP: Failed to get xml root element."));
    }

    req->sr_body = soap_node_get_child_node(req->sr_root, "Body");
    req->sr_method = soap_node_get_child_node(req->sr_body, method);

    return req;
}

static xmlNodePtr
soap_node_add_child_node(xmlNodePtr node, const char *name, const char *type, const char *value)
{
    if (node == NULL || name == NULL)
        error_msg_and_die(_("SOAP: Failed to add a new child node because of no node or no child name."));

    xmlNodePtr new_node = xmlNewTextChild(node, /* namespace */ NULL, BAD_CAST name, BAD_CAST value);

    if (new_node == NULL)
        error_msg_and_die(_("SOAP: Failed to create a new xml child item."));

    if (type != NULL)
    {
        if (xmlNewProp(new_node, BAD_CAST "xsi:type", BAD_CAST type) == NULL)
            error_msg_and_die(_("SOAP: Failed to create a new property."));
    }

    return new_node;
}

void
soap_request_add_method_parameter(soap_request_t *req, const char *name, const char *type, const char *value)
{
    if (req == NULL || req->sr_method == NULL)
        error_msg_and_die(_("SOAP: Failed to add method parametr."));

    soap_node_add_child_node(req->sr_method, name, type, value);
    return;
}

void
soap_request_add_credentials_parameter(soap_request_t *req, const mantisbt_settings_t *settings)
{
    soap_request_add_method_parameter(req, "username", SOAP_STRING, settings->m_login);
    soap_request_add_method_parameter(req, "password", SOAP_STRING, settings->m_password);

    return;
}

static void
soap_add_new_issue_parameters(soap_request_t *req,
                               const char *project,
                               const char *version,
                               const char *category,
                               const char *summary,
                               const char *description,
                               const char *additional_information,
                               bool private,
                               const char *duphash,
                               const char *tracker_url)
{
    if (req == NULL || req->sr_method == NULL)
        error_msg_and_die(_("SOAP: Failed to add new issue parametrs."));

    if (project == NULL || category == NULL || summary == NULL || description == NULL)
        error_msg_and_die(_("SOAP: Failed to add new issue parameters because the required items are missing."));

    xmlNodePtr issue_node = soap_node_add_child_node(req->sr_method, "issue", SOAP_ISSUEDATA, /* content */ NULL);

    // project
    xmlNodePtr project_node = soap_node_add_child_node(issue_node, "project", SOAP_OBJECTREF, /* content */ NULL);
    soap_node_add_child_node(project_node, "name", SOAP_STRING, project);

    // view status
    xmlNodePtr view_node = soap_node_add_child_node(issue_node, "view_state", SOAP_OBJECTREF, /* content */ NULL);
    soap_node_add_child_node(view_node, "name", SOAP_STRING, (private) ? "private" : "public");

    // custom fields (duphash and URL to tracker)
    xmlNodePtr duphash_node = soap_node_add_child_node(issue_node, "custom_fields", SOAP_CUSTOMFIELD_ARRAY, /* content */ NULL);
    xmlNodePtr item_node = soap_node_add_child_node(duphash_node, "item", SOAP_CUSTOMFIELD, /* content */ NULL);
    xmlNodePtr field_node = soap_node_add_child_node(item_node, "field", SOAP_OBJECTREF, /* content */ NULL);
    soap_node_add_child_node(field_node, "id", SOAP_INTEGER, /* custom_field name */ CUSTOMFIELD_DUPHASH_ID);
    soap_node_add_child_node(item_node, "value", SOAP_STRING, duphash);

    // if tracker url exists, attach it to the issue
    if (tracker_url != NULL)
    {
        item_node = soap_node_add_child_node(duphash_node, "item", SOAP_CUSTOMFIELD, /* content */ NULL);
        field_node = soap_node_add_child_node(item_node, "field", SOAP_OBJECTREF, /* content */ NULL);
        soap_node_add_child_node(field_node, "id", SOAP_INTEGER, /* custom_field name */ CUSTOMFIELD_URL_ID);
        soap_node_add_child_node(item_node, "value", SOAP_STRING, tracker_url);
    }

    soap_node_add_child_node(issue_node, "os_build", SOAP_STRING, version);
    soap_node_add_child_node(issue_node, "category", SOAP_STRING, category);
    soap_node_add_child_node(issue_node, "summary", SOAP_STRING, summary);
    soap_node_add_child_node(issue_node, "description", SOAP_STRING, description);
    soap_node_add_child_node(issue_node, "additional_information", SOAP_STRING, additional_information);

    return;
}

char *
soap_request_to_str(const soap_request_t *req)
{
    if (req == NULL || req->sr_root == NULL || req->sr_root->doc == NULL)
        error_msg_and_die(_("SOAP: Failed to create SOAP string because of invalid function arguments."));

    xmlBufferPtr buffer = xmlBufferCreate();
    int err = xmlNodeDump(buffer, req->sr_root->doc, req->sr_root, 1, /* formatting */ 0);
    if (err == -1)
    {
        xmlBufferFree(buffer);
        error_msg_and_die(_("SOAP: Failed to dump xml node."));
    }

    char *ret = xasprintf("%s%s", XML_VERSION, (const char *) xmlBufferContent(buffer));
    xmlBufferFree(buffer);

    return ret;
}

#if 0
void
soap_request_print(soap_request_t *req)
{
    if (req == NULL || req->sr_root == NULL || req->sr_root->doc == NULL)
        error_msg_and_die(_("SOAP: Failed to print SOAP string."));

    xmlBufferPtr buffer = xmlBufferCreate();
    int err = xmlNodeDump(buffer, req->sr_root->doc, req->sr_root, 1, /* formatting */ 0);
    if (err == -1)
    {
        xmlBufferFree(buffer);
        error_msg_and_die(_("Failed to dump xml node."));
    }

    puts((const char *) xmlBufferContent(buffer));

    xmlBufferFree(buffer);
    return;
}
#endif

static bool
reader_move_reader_if_node_type_is_element_with_name_and_verify_its_value(xmlTextReaderPtr reader, const char *name)
{
    /* is not element node */
    if (xmlTextReaderNodeType(reader) != XML_ELEMENT_NODE)
        return false;

    /* is not required name */
    if (xmlStrcmp(xmlTextReaderConstName(reader), BAD_CAST name) != 0)
        return false;

    /* read next node */
    if (xmlTextReaderRead(reader) != 1)
        return false;

    /* no value node */
    if (xmlTextReaderHasValue(reader) == 0)
        return false;

    /* no text node */
    if (xmlTextReaderNodeType(reader) != XML_TEXT_NODE)
        return false;

    return true;
}

static void
reader_find_element_by_name(xmlTextReaderPtr reader, const char *name)
{
    while (xmlTextReaderRead(reader) == 1)
    {
        /* is not element node */
        if (xmlTextReaderNodeType(reader) != XML_ELEMENT_NODE)
            continue;

        /* is not required name */
        if (xmlStrcmp(xmlTextReaderConstName(reader), BAD_CAST name) != 0)
            continue;

        break;
    }

    return;
}

/* It is not possible to search only by name because the response contains
 * different node with the same name. (e.g. id - user id, project id, issue id etc.)
 * We are interested in only about issues id which is located at a different depth than others.
 * ...
 *   <item xsi:type="ns1:IssueData">
 *      <id xsi:type="xsd:integer">10</id>      <-- This is issue ID (required)
 *      <view_state xsi:type="ns1:ObjectRef">
 *          <id xsi:type="xsd:integer">10</id>  <-- This is view_state ID (not required)
 *          <name xsi:type="xsd:string">public</name>
 *      </view_state>
 *      <project xsi:type="ns1:ObjectRef">
 *          <id xsi:type="xsd:integer">1</id>   <-- This is project ID (not required)
 *          <name xsi:type="xsd:string">test</name>
 *      </project>
 * ...
 */
static GList *
response_values_at_depth_by_name(const char *xml, const char *name, int depth)
{
    xmlDocPtr doc = xmlParseDoc(BAD_CAST xml);
    if (doc == NULL)
        error_msg_and_die(_("SOAP: Failed to parse xml (searching value at depth by name)."));

    xmlTextReaderPtr reader = xmlReaderWalker(doc);
    if (reader == NULL)
        error_msg_and_die(_("SOAP: Failed to create xml text reader."));

    GList *result = NULL;

    const xmlChar *value;
    while (xmlTextReaderRead(reader) == 1)
    {
        /* is not right depth */
        if (depth != -1 && xmlTextReaderDepth(reader) != depth)
            continue;

        if (reader_move_reader_if_node_type_is_element_with_name_and_verify_its_value(reader, name) == false)
            continue;

        if ((value = xmlTextReaderConstValue(reader)) != NULL)
            result = g_list_append(result, xstrdup((const char *) value));
    }
    xmlFreeTextReader(reader);

    return result;
}

/*
 * Finds an element named 'elem' and returns a text of a child named 'name'
 *
 * Example:
 *  For
 *  <elem>
 *      <id>1</id>
 *      <name>foo</name>
 *  </elem>
 *
 *  returns "foo"
 */
static char *
response_get_name_value_of_element(const char *xml, const char *element)
{
    xmlDocPtr doc = xmlParseDoc(BAD_CAST xml);
    if (doc == NULL)
        error_msg_and_die(_("SOAP: Failed to parse xml."));

    xmlTextReaderPtr reader = xmlReaderWalker(doc);
    if (reader == NULL)
        error_msg_and_die(_("SOAP: Failed to create xml text reader."));

    const xmlChar *value = NULL;

    reader_find_element_by_name(reader, element);

    /* find 'name' element and return its text */
    while (xmlTextReaderRead(reader) == 1)
    {
        if (reader_move_reader_if_node_type_is_element_with_name_and_verify_its_value(reader, "name") == false)
            continue;

        if ((value = xmlTextReaderConstValue(reader)) != NULL)
            break;
    }
    xmlFreeTextReader(reader);

    return (char *) value;
}

static int
response_get_id_of_relatedto_issue(const char *xml)
{
    xmlDocPtr doc = xmlParseDoc(BAD_CAST xml);
    if (doc == NULL)
        error_msg_and_die(_("SOAP: Failed to parse xml (get related to issue)."));

    xmlTextReaderPtr reader = xmlReaderWalker(doc);
    if (reader == NULL)
        error_msg_and_die(_("SOAP: Failed to create xml text reader."));

    const xmlChar *value = NULL;
    const xmlChar *id = NULL;

    /* find relationships section */
    reader_find_element_by_name(reader, "relationships");

    /* find "name" value of 'name' element */
    while (xmlTextReaderRead(reader) == 1)
    {
        /* find type of relattionship */
        if (reader_move_reader_if_node_type_is_element_with_name_and_verify_its_value(reader, "name") == false)
            continue;

        if ((value = xmlTextReaderConstValue(reader)) == NULL)
            continue;

        /* we need 'duplicate of' realtionship type */
        if (xmlStrcmp(value, BAD_CAST "duplicate of") != 0)
            continue;

        /* find id of duplicate issues */
        reader_find_element_by_name(reader, "target_id");

        /* verify target_id node */
        if (reader_move_reader_if_node_type_is_element_with_name_and_verify_its_value(reader, "target_id") == false)
            continue;

        /* get its value */
        if ((id = xmlTextReaderConstValue(reader)) != NULL)
            break;
    }
    xmlFreeTextReader(reader);

    return (id == NULL) ? -1 : atoi((const char *) id);
}

GList *
response_get_main_ids_list(const char *xml)
{
    return response_values_at_depth_by_name(xml, "id", 5);
}

int
response_get_main_id(const char *xml)
{
    GList *l = response_values_at_depth_by_name(xml, "id", 5);
    return (l != NULL) ? atoi(l->data) : -1;
}

static int
response_get_return_value(const char *xml)
{
    GList *l = response_values_at_depth_by_name(xml, "return", 3);
    return (l != NULL) ? atoi(l->data) : -1;
}

static char *
response_get_error_msg(const char *xml)
{
    GList *l = response_values_at_depth_by_name(xml, "faultstring", 3);
    return (l != NULL) ? xstrdup(l->data) : NULL;
}

void
response_values_free(GList *values)
{
    g_list_free_full(values, free);
}

/*
 * POST
 */

void
mantisbt_result_free(mantisbt_result_t *result)
{
    if (result == NULL)
        return;

    free(result->mr_url);
    free(result->mr_msg);
    free(result->mr_body);
    free(result);
}

mantisbt_result_t *
mantisbt_soap_call(const mantisbt_settings_t *settings, const soap_request_t *req)
{
    char *request = soap_request_to_str(req);

    const char *url = settings->m_mantisbt_soap_url;

    mantisbt_result_t *result = xzalloc(sizeof(*result));

    if (url == NULL || request == NULL)
    {
        result->mr_error = -2;
        result->mr_msg = xasprintf(_("Url or request isn't specified."));
        free(request);

        return result;
    }

    char *url_copy = NULL;

    int redirect_count = 0;
    char *errmsg;
    post_state_t *post_state;

redirect:
    post_state = new_post_state(0
            + POST_WANT_HEADERS
            + POST_WANT_BODY
            + POST_WANT_ERROR_MSG
            + (settings->m_ssl_verify ? POST_WANT_SSL_VERIFY : 0)
    );

    post_string(post_state, settings->m_mantisbt_soap_url, "text/xml", NULL, request);

    char *location = find_header_in_post_state(post_state, "Location:");

    switch (post_state->http_resp_code)
    {
    case 404:
        result->mr_error = -1;
        result->mr_msg = xasprintf(_("Error in HTTP POST, "
                        "HTTP code: 404 (Not found), URL:'%s'"), url);
        break;
    case 500:
        result->mr_error = -1;
        result->mr_msg = response_get_error_msg(post_state->body);

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
        result->mr_error = -1;
        errmsg = post_state->curl_error_msg;
        if (errmsg && errmsg[0])
            result->mr_msg = xasprintf(_("Error in MantisBT request at '%s': %s"), url, errmsg);
        else
            result->mr_msg = xasprintf(_("Error in MantisBT request at '%s'"), url);
        break;

    case 200:
    case 201:
        /* sent successfully */
        result->mr_url = xstrdup(location); /* note: xstrdup(NULL) returns NULL */
    } /* switch (HTTP code) */

    result->mr_http_resp_code = post_state->http_resp_code;
    result->mr_body = post_state->body;
    post_state->body = NULL;

    free_post_state(post_state);
    free(url_copy);
    free(request);

    return result;
}

int
mantisbt_attach_data(const mantisbt_settings_t *settings, const char *bug_id,
                    const char *att_name, const char *data, int size)
{
    soap_request_t *req = soap_request_new_for_method("mc_issue_attachment_add");
    soap_request_add_credentials_parameter(req, settings);

    soap_request_add_method_parameter(req, "issue_id", SOAP_INTEGER, bug_id);
    soap_request_add_method_parameter(req, "name", SOAP_STRING, att_name);

    soap_request_add_method_parameter(req, "file_type", SOAP_STRING, "text");
    soap_request_add_method_parameter(req, "content", SOAP_BASE64, encode_base64(data, size));

    mantisbt_result_t *result = mantisbt_soap_call(settings, req);
    soap_request_free(req);

    if (result->mr_http_resp_code != 200)
    {
        int ret = -1;
        if (strcmp(result->mr_msg, "Duplicate filename.") == 0)
            ret = -2;

        error_msg(_("Failed to attach file: '%s'"), result->mr_msg);
        mantisbt_result_free(result);
        return ret;
    }

    int id = response_get_return_value(result->mr_body);

    mantisbt_result_free(result);

    return id;
}

static int
mantisbt_attach_fd(const mantisbt_settings_t *settings, const char *bug_id,
                const char *att_name, int fd)
{
    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0)
    {
        perror_msg(_("Can't lseek '%s'"), att_name);
        return -1;
    }

    if (size >= MANTISBT_MAX_FILE_UPLOAD_SIZE)
    {
        error_msg(_("Can't upload '%s', it's too large (%llu bytes)"), att_name, (long long)size);
        return -1;
    }
    lseek(fd, 0, SEEK_SET);

    char *data = xmalloc(size + 1);
    ssize_t r = full_read(fd, data, size);
    if (r < 0)
    {
        free(data);
        perror_msg(_("Can't read '%s'"), att_name);
        return -1;
    }

    int res = mantisbt_attach_data(settings, bug_id, att_name, data, size);
    free(data);
    return res;
}

int
mantisbt_attach_file(const mantisbt_settings_t *settings, const char *bug_id,
                    const char *att_name, const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        perror_msg(_("Can't open '%s'"), path);
        return 0;
    }
    errno = 0;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
    {
        perror_msg("'%s': not a regular file", path);
        close(fd);
        return 0;
    }
    log_debug("attaching '%s' as file", att_name);
    int ret = mantisbt_attach_fd(settings, bug_id, att_name, fd);
    close(fd);
    return ret;
}

GList *
mantisbt_search_by_abrt_hash(mantisbt_settings_t *settings, const char *abrt_hash)
{
    soap_request_t *req = soap_request_new_for_method("mc_search_issues");
    soap_request_add_credentials_parameter(req, settings);

    soap_request_add_method_parameter(req, "project_id", SOAP_INTEGER, "1");

    /* 'hide_status : -2' means, searching within all status */
    char *filter = xasprintf("{\"hide_status\":\"-2\",\"abrt_hash\":\"%s\"}", abrt_hash);
    soap_request_add_method_parameter(req, "filter", SOAP_STRING, filter);
    soap_request_add_method_parameter(req, "page_number", SOAP_INTEGER, "1");
    soap_request_add_method_parameter(req, "per_page", SOAP_INTEGER, /* -1 means get all issues */"-1");

    mantisbt_result_t *result = mantisbt_soap_call(settings, req);
    soap_request_free(req);

    if (result->mr_error == -1)
    {
        error_msg(_("Failed to search MantisBT issue by duphash: '%s'"), result->mr_msg);
        free(filter);
        mantisbt_result_free(result);
        return NULL;
    }

    GList *ids = response_get_main_ids_list(result->mr_body);

    free(filter);

    return ids;
}

GList *
mantisbt_search_duplicate_issues(mantisbt_settings_t *settings, const char *project, const char *category,
                        const char *version, const char *abrt_hash)
{
    soap_request_t *req = soap_request_new_for_method("mc_search_issues");
    soap_request_add_credentials_parameter(req, settings);

    soap_request_add_method_parameter(req, "project_id", SOAP_INTEGER, project);

    char *filter;

    /* 'hide_status : -2' means, searching within all status */
    if (version == NULL)
       filter = xasprintf("{\"hide_status\":\"-2\",\"show_category\":\"%s\", \"abrt_hash\":\"%s\"}", category, abrt_hash);
    else
       filter = xasprintf("{\"hide_status\":\"-2\",\"os_build\":\"%s\", \"show_category\":\"%s\", \"abrt_hash\":\"%s\"}", version, category, abrt_hash);

    soap_request_add_method_parameter(req, "filter", SOAP_STRING, filter);
    soap_request_add_method_parameter(req, "page_number", SOAP_INTEGER, "1");
    soap_request_add_method_parameter(req, "per_page", SOAP_INTEGER, /* -1 means get all issues */"-1");

    mantisbt_result_t *result = mantisbt_soap_call(settings, req);
    soap_request_free(req);

    if (result->mr_error == -1)
    {
        error_msg(_("Failed to search MantisBT duplicate issue: '%s'"), result->mr_msg);
        mantisbt_result_free(result);
        free(filter);
        return NULL;
    }

    GList *ids = response_get_main_ids_list(result->mr_body);

    free(filter);

    return ids;
}

int
mantisbt_create_new_issue(const mantisbt_settings_t *settings,
                   problem_data_t *problem_data,
                   const problem_report_t *pr,
                   const char *tracker_url)
{

    const char *category = problem_data_get_content_or_NULL(problem_data, FILENAME_COMPONENT);
    const char *duphash = problem_data_get_content_or_NULL(problem_data, FILENAME_DUPHASH);

    char *summary = shorten_string_to_length(problem_report_get_summary(pr), MAX_SUMMARY_LENGTH);

    const char *description = problem_report_get_description(pr);
    const char *additional_information = problem_report_get_section(pr, PR_SEC_ADDITIONAL_INFO);

    soap_request_t *req = soap_request_new_for_method("mc_issue_add");
    soap_request_add_credentials_parameter(req, settings);
    soap_add_new_issue_parameters(req, settings->m_project, settings->m_project_version, category, summary, description, additional_information, settings->m_create_private, duphash, tracker_url);

    mantisbt_result_t *result = mantisbt_soap_call(settings, req);
    soap_request_free(req);
    free(summary);

    if (result->mr_error == -1)
    {
        error_msg(_("Failed to create a new issue: '%s'"), result->mr_msg);
        mantisbt_result_free(result);
        return -1;
    }

    int id = response_get_return_value(result->mr_body);

    mantisbt_result_free(result);
    return id;
}

mantisbt_issue_info_t *
mantisbt_get_issue_info(const mantisbt_settings_t *settings, int issue_id)
{
    soap_request_t *req = soap_request_new_for_method("mc_issue_get");
    soap_request_add_credentials_parameter(req, settings);

    char *issue_id_str = xasprintf("%d", issue_id);
    soap_request_add_method_parameter(req, "issue_id", SOAP_INTEGER, issue_id_str);
    free(issue_id_str);

    mantisbt_result_t *result = mantisbt_soap_call(settings, req);
    soap_request_free(req);

    if (result->mr_error == -1)
    {
        error_msg(_("Failed to get MantisBT issue: '%s'"), result->mr_msg);
        mantisbt_result_free(result);
        return NULL;
    }

    mantisbt_issue_info_t *issue_info = mantisbt_issue_info_new();

    issue_info->mii_id = issue_id;
    issue_info->mii_status = response_get_name_value_of_element(result->mr_body, "status");
    issue_info->mii_resolution = response_get_name_value_of_element(result->mr_body, "resolution");
    issue_info->mii_reporter = response_get_name_value_of_element(result->mr_body, "reporter");
    issue_info->mii_project = response_get_name_value_of_element(result->mr_body, "project");

    if (strcmp(issue_info->mii_status, "closed") == 0 && !issue_info->mii_resolution)
        error_msg(_("Issue %i is CLOSED, but it has no RESOLUTION"), issue_info->mii_id);

    issue_info->mii_dup_id = response_get_id_of_relatedto_issue(result->mr_body);

    if (strcmp(issue_info->mii_status, "closed") == 0
        && strcmp(issue_info->mii_resolution, "duplicate") == 0
        && issue_info->mii_dup_id == -1 )
    {
        error_msg(_("Issue %i is CLOSED as DUPLICATE, but it has no DUPLICATE_ID"),
                            issue_info->mii_id);
    }

    /* notes are stored in <text> element */
    issue_info->mii_notes = response_values_at_depth_by_name(result->mr_body, "text", -1);
    issue_info->mii_attachments = response_values_at_depth_by_name(result->mr_body, "filename", -1);
    issue_info->mii_best_bt_rating = comments_find_best_bt_rating(issue_info->mii_notes);

    mantisbt_result_free(result);
    return issue_info;
}

int
mantisbt_add_issue_note(const mantisbt_settings_t *settings, int issue_id, const char *note)
{
    soap_request_t *req = soap_request_new_for_method("mc_issue_note_add");
    soap_request_add_credentials_parameter(req, settings);

    char *issue_id_str = xasprintf("%i", issue_id);
    soap_node_add_child_node(req->sr_method, "issue_id", SOAP_INTEGER, issue_id_str);

    xmlNodePtr note_node = soap_node_add_child_node(req->sr_method, "note", SOAP_ISSUENOTE, /* content */ NULL);
    soap_node_add_child_node(note_node, "text", SOAP_STRING, note);

    mantisbt_result_t *result = mantisbt_soap_call(settings, req);

    free(issue_id_str);
    soap_request_free(req);

    if (result->mr_error == -1)
    {
        error_msg(_("Failed to add MantisBT issue note: '%s'"), result->mr_msg);
        mantisbt_result_free(result);
        return -1;
    }
    int id = response_get_return_value(result->mr_body);

    mantisbt_result_free(result);
    return id;
}
