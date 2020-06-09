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

    Authors:
       Anton Arapov <anton@redhat.com>
       Arjan van de Ven <arjan@linux.intel.com>
 */
#include "libreport_curl.h"
#include "internal_libreport.h"

/* helpers */
static size_t writefunction(void *ptr, size_t size, size_t nmemb, void *stream)
{
    size *= nmemb;
/*
    char *c, *c1, *c2;

    log_warning("received: '%*.*s'", (int)size, (int)size, (char*)ptr);
    c = (char*)g_malloc0(size + 1);
    memcpy(c, ptr, size);
    c1 = strstr(c, "201 ");
    if (c1)
    {
        c1 += 4;
        c2 = strchr(c1, '\n');
        if (c2)
            *c2 = 0;
    }
    free(c);
*/

    return size;
}

/* Send oops data to kerneloops.org-style site, using HTTP POST */
/* Returns 0 on success */
static CURLcode http_post_to_kerneloops_site(const char *url, const char *oopsdata)
{
    CURLcode ret;
    CURL *handle;
    struct curl_httppost *post = NULL;
    struct curl_httppost *last = NULL;
    struct curl_slist *headers = NULL;

    handle = curl_easy_init();
    if (!handle)
        error_msg_and_die("Can't create curl handle");

    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Expect:");

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);

    curl_formadd(&post, &last,
            CURLFORM_COPYNAME, "oopsdata",
            CURLFORM_COPYCONTENTS, oopsdata,
            CURLFORM_END);
    curl_formadd(&post, &last,
            CURLFORM_COPYNAME, "pass_on_allowed",
            CURLFORM_COPYCONTENTS, "yes",
            CURLFORM_END);

    curl_easy_setopt(handle, CURLOPT_HTTPPOST, post);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writefunction);

    ret = curl_easy_perform_with_proxy(handle, url);

    curl_formfree(post);
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);

    return ret;
}

static void report_to_kerneloops(
                const char *dump_dir_name,
                map_string_t *settings)
{
    problem_data_t *problem_data = create_problem_data_for_reporting(dump_dir_name);
    if (!problem_data)
        libreport_xfunc_die(); /* create_problem_data_for_reporting already emitted error msg */

    const char *backtrace = problem_data_get_content_or_NULL(problem_data, FILENAME_BACKTRACE);
    if (!backtrace)
        error_msg_and_die("Error sending kernel oops due to missing backtrace");

    const char *env = getenv("KerneloopsReporter_SubmitURL");
    const char *submitURL = (env ? env : libreport_get_map_string_item_or_empty(settings, "SubmitURL"));
    if (!submitURL[0])
        submitURL = "http://oops.kernel.org/submitoops.php";

    log_warning(_("Submitting oops report to %s"), submitURL);

    CURLcode ret = http_post_to_kerneloops_site(submitURL, backtrace);
    if (ret != CURLE_OK)
        error_msg_and_die("Kernel oops has not been sent due to %s", curl_easy_strerror(ret));

    problem_data_free(problem_data);

    /* Server replies with:
     * 200 thank you for submitting the kernel oops information
     * RemoteIP: 34192fd15e34bf60fac6a5f01bba04ddbd3f0558
     * - no URL or bug ID apparently...
     */
    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (dd)
    {
        report_result_t *result;

        result = report_result_new_with_label_from_env("kerneloops");

        report_result_set_url(result, submitURL);

        libreport_add_reported_to_entry(dd, result);

        report_result_free(result);

        dd_close(dd);
    }

    log_warning("Kernel oops report was uploaded");
}

int main(int argc, char **argv)
{
    abrt_init(argv);

    /* I18n */
    setlocale(LC_ALL, "");
#if ENABLE_NLS
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
#endif

    map_string_t *settings = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
    const char *dump_dir_name = ".";
    GList *conf_file = NULL;

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "& [-v] [-c CONFFILE]... -d DIR\n"
        "\n"
        "Reports kernel oops to kerneloops.org (or similar) site.\n"
        "\n"
        "Files with names listed in $EXCLUDE_FROM_REPORT are not included\n"
        "into the tarball.\n"
        "\n"
        "CONFFILE lines should have 'PARAM = VALUE' format.\n"
        "Recognized string parameter: SubmitURL.\n"
        "Parameter can be overridden via $KerneloopsReporter_SubmitURL."
    );
    enum {
        OPT_v = 1 << 0,
        OPT_d = 1 << 1,
        OPT_c = 1 << 2,
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&libreport_g_verbose),
        OPT_STRING('d', NULL, &dump_dir_name, "DIR" , _("Problem directory")),
        OPT_LIST(  'c', NULL, &conf_file    , "FILE", _("Configuration file")),
        OPT_END()
    };
    /*unsigned opts =*/ libreport_parse_opts(argc, argv, program_options, program_usage_string);

    libreport_export_abrt_envvars(0);

    while (conf_file)
    {
        char *fn = (char *)conf_file->data;
        log_notice("Loading settings from '%s'", fn);
        libreport_load_conf_file(fn, settings, /*skip key w/o values:*/ false);
        log_debug("Loaded '%s'", fn);
        conf_file = g_list_remove(conf_file, fn);
    }

    report_to_kerneloops(dump_dir_name, settings);

    if (settings)
        g_hash_table_destroy(settings);
    return 0;
}
