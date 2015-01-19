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

#ifndef MANTISBT_H
#define MANTISBT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libxml/encoding.h>
#include "problem_report.h"

#define SOAP_STRING "ns2:string"
#define SOAP_INTEGER "ns2:integer"
#define SOAP_ISSUEDATA "ns3:IssueData"
#define SOAP_OBJECTREF "ns3:ObjectRef"
#define SOAP_CUSTOMFIELD_ARRAY "ns2:CustomFieldValueForIssueDataArray"
#define SOAP_CUSTOMFIELD "ns2:CustomFieldValueForIssueData"
#define SOAP_BASE64 "SOAP-ENC:base64"
#define SOAP_ISSUENOTE "ns3:IssueNoteData"

#define PR_SEC_ADDITIONAL_INFO "Additional info"

typedef struct soap_request
{
    xmlNodePtr sr_root;
    xmlNodePtr sr_body;
    xmlNodePtr sr_method;
} soap_request_t;

typedef struct mantisbt_settings
{
    char *m_login;
    char *m_password;
    const char *m_mantisbt_url;
    const char *m_mantisbt_soap_url;
    char *m_project;
    char *m_project_version;
    const char *m_DontMatchComponents;
    int         m_ssl_verify;
    int         m_create_private;
} mantisbt_settings_t;

typedef struct mantisbt_result
{
    int mr_http_resp_code;
    int mr_error;
    char *mr_msg;
    char *mr_url;
    char *mr_body;
} mantisbt_result_t;

typedef struct mantisbt_issue_info
{
    int mii_id;
    int mii_dup_id;
    unsigned mii_best_bt_rating;

    char *mii_status;
    char *mii_resolution;
    char *mii_reporter;
    char *mii_project;

    GList *mii_notes;
    GList *mii_attachments;
} mantisbt_issue_info_t;

void mantisbt_settings_free(mantisbt_settings_t *settings);

mantisbt_issue_info_t * mantisbt_issue_info_new();
void mantisbt_issue_info_free(mantisbt_issue_info_t *info);
mantisbt_issue_info_t * mantisbt_find_origin_bug_closed_duplicate(mantisbt_settings_t *settings, mantisbt_issue_info_t *info);

void soap_request_free(soap_request_t *req);

soap_request_t *soap_request_new_for_method(const char *method);

void soap_request_add_method_parameter(soap_request_t *req, const char *name, const char *type, const char *value);
void soap_request_add_credentials_parameter(soap_request_t *req, const mantisbt_settings_t *settings);

char *soap_request_to_str(const soap_request_t *req);

#if 0
void soap_request_print(soap_request_t *req);
#endif

GList * response_get_main_ids_list(const char *xml);
int response_get_main_id(const char *xml);
void response_values_free(GList *values);

void mantisbt_result_free(mantisbt_result_t *result);
mantisbt_result_t *mantisbt_soap_call(const mantisbt_settings_t *settings, const soap_request_t *req);

int mantisbt_attach_data(const mantisbt_settings_t *settings, const char *bug_id,
                         const char *att_name, const char *data, int size);

int mantisbt_attach_file(const mantisbt_settings_t *settings, const char *bug_id,
                    const char *att_name, const char *data);

GList * mantisbt_search_by_abrt_hash(mantisbt_settings_t *settings, const char *abrt_hash);
GList * mantisbt_search_duplicate_issues(mantisbt_settings_t *settings, const char *project,
                            const char *category, const char *version, const char *abrt_hash);

int mantisbt_create_new_issue(const mantisbt_settings_t *settings, problem_data_t *problem_data,
                       const problem_report_t *pr, const char *tracker_url);

mantisbt_issue_info_t * mantisbt_get_issue_info(const mantisbt_settings_t *settings, int issue_id);
int mantisbt_add_issue_note(const mantisbt_settings_t *settings, int issue_id, const char *note);



#ifdef __cplusplus
}
#endif

#endif

