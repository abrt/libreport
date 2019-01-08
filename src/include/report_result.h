#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct report_result report_result_t;

char   *report_result_get_label    (report_result_t *result);
char   *report_result_get_url      (report_result_t *result);
char   *report_result_get_message  (report_result_t *result);
char   *report_result_get_bthash   (report_result_t *result);
char   *report_result_get_workflow (report_result_t *result);
time_t  report_result_get_timestamp(report_result_t *result);

void report_result_set_url      (report_result_t *result,
                                 const char      *url);
void report_result_set_message  (report_result_t *result,
                                 const char      *message);
void report_result_set_bthash   (report_result_t *result,
                                 const char      *bthash);
void report_result_set_workflow (report_result_t *result,
                                 const char      *workflow);
void report_result_set_timestamp(report_result_t *result,
                                 time_t           timestamp);

struct strbuf *report_result_to_string(report_result_t *result);

report_result_t *report_result_new_with_label         (const char *label);
report_result_t *report_result_new_with_label_from_env(const char *label);
report_result_t *report_result_parse                  (const char *line,
                                                       size_t      label_length);

void report_result_free(report_result_t *result);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(report_result_t, report_result_free)

G_END_DECLS
