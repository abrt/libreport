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
#ifndef ABRT_RH_SUPPORT_H_
#define ABRT_RH_SUPPORT_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque struct */
typedef struct reportfile reportfile_t;

reportfile_t *new_reportfile(void);
void free_reportfile(reportfile_t* file);

void reportfile_add_binding_from_string(reportfile_t* file, const char* name, const char* value);
void reportfile_add_binding_from_namedfile(reportfile_t* file,
                const char* on_disk_filename, /* unused so far */
                const char* binding_name,
                const char* recorded_filename,
                int isbinary);

const char* reportfile_as_string(reportfile_t* file);

/* Used to return result of RHTS submission */
struct rhts_result {
    int error; /* 0: no error; else: error code, msg contains error msg. */
    char *msg;
    char *url; /* URL to created case, or NULL */
};
typedef struct rhts_result rhts_result_t;

void free_rhts_result(rhts_result_t *p);

rhts_result_t*
send_report_to_new_case(const char* baseURL,
                const char* username,
                const char* password,
                bool ssl_verify,
                const char* release,
                const char* summary,
                const char* description,
                const char* component,
                const char* report_file_name);

#ifdef __cplusplus
}
#endif

#endif
