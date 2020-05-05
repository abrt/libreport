/*
    Copyright (C) 2020  ABRT team
    Copyright (C) 2020  RedHat Inc

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
#include "client.h"
#include "internal_libreport.h"
#include "libreport_curl.h"
#include "problem_report.h"
#include <json.h>

static void add_reported_to_entry(const char *dump_dir_name, const char *issue_url);
static bool check_if_reported(const char *dump_dir_name);
static char *get_new_issue_url(const char *response);
static bool is_reportable(problem_data_t *problem_data, GHashTable *settings);
static GHashTable *load_settings(const char *conf_file);
static char *make_request_body(problem_report_t *report);
static char *make_url(const char *component, GHashTable *settings);
static void submit_issue(const char *dump_dir_name, problem_data_t *problem_data,
        GHashTable *settings, const char *format_file);

int main(int argc, char **argv)
{
    abrt_init(argv);

    /* I18n */
    setlocale(LC_ALL, "");
#if ENABLE_NLS
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
#endif

    const char *conf_file = CONF_DIR"/plugins/gitlab.conf";
    const char *dump_dir_name = ".";
    const char *format_file = CONF_DIR"/plugins/gitlab_format.conf";

    const char *program_usage_string = _(
        "\n& [-v] [-c CONFFILE] [-F FMTFILE] -d DIR"
        "\n"
        "\nReports problem from dump directory DIR to a GitLab instance."
        "\n"
        "\nIf no open issue with the same duphash is found, a new one is created."
        "\nElements of DIR are uploaded and referenced in the issue description."
        "\n"
        "\nIf an open issue with the same duphash is found, the tool adds a new comment"
        "\nto the issue."
        "\n"
        "\nThe URL to new or modified bug is printed to stdout and recorded in"
        "\n'reported_to' element."
        "\n"
        "\nIf not specified, CONFFILE defaults to "CONF_DIR"/plugins/gitlab.conf"
        "\nand user's local ~"USER_HOME_CONFIG_PATH"/gitlab.conf."
        "\nIts lines should conform to the 'PARAM = VALUE' format."
        "\n"
        "\nRecognized string parameters: GitlabProject, GitLabURL, PrivateToken, Allowlist."
        "\nRecognized boolean parameter (VALUE should be 1/0, yes/no): SSLVerify."
        "\n"
        "\nUser's local configuration overrides the system-wide configuration."
        "\nParameters can also be overridden via $Gitlab_PARAM environment variables."
        "\n"
        "\nFMTFILE defaults to "CONF_DIR"/plugins/gitlab_format.conf"
    );

    enum {
        OPT_v = 1 << 0,
        OPT_c = 1 << 1,
        OPT_d = 1 << 2,
        OPT_F = 1 << 3,
    };

    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&libreport_g_verbose),
        OPT_STRING('c', NULL, &conf_file,     "FILE", _("Configuration file")),
        OPT_STRING('d', NULL, &dump_dir_name, "DIR",  _("Problem directory")),
        OPT_STRING('F', NULL, &format_file,   "FILE", _("Formatting file for initial comment")),
        OPT_END()
    };

    libreport_parse_opts(argc, argv, program_options, program_usage_string);

    libreport_export_abrt_envvars(0);

    /* Load defaults and user settings from configuration files. */
    g_autoptr(GHashTable) settings = load_settings(conf_file);
    if (!settings)
    {
        /* Error message has already been logged. */
        return 1;
    }

    if (check_if_reported(dump_dir_name))
    {
        /* The problem has already been reported and the user does not wish to create
         * a new issue.
         */
        return 0;
    }

    /* Check if this problem is reportable to GitLab. */
    g_autoptr(problem_data_t) problem_data = create_problem_data_for_reporting(dump_dir_name);
    if (!problem_data)
    {
        /* create_problem_data_for_reporting has already emitted an error message. */
        libreport_xfunc_die();
    }

    if (!is_reportable(problem_data, settings))
    {
        /* Error message has already been logged. */
        return 1;
    }

    /* TODO: Search for duplicates. */

    /* Submit the report as a new issue. */
    submit_issue(dump_dir_name, problem_data, settings, format_file);

    return 0;
}

static void add_reported_to_entry(const char *dump_dir_name, const char *issue_url)
{
    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        libreport_xfunc_die();

    g_autoptr(report_result_t) result = report_result_new_with_label_from_env("Gitlab");
    report_result_set_url(result, issue_url);
    libreport_add_reported_to_entry(dd, result);

    dd_close(dd);
}

static bool check_if_reported(const char *dump_dir_name)
{
    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    g_autoptr(report_result_t) reported_to = NULL;
    g_autofree const char *url = NULL;

    if (!dd)
        libreport_xfunc_die();

    reported_to = libreport_find_in_reported_to(dd, "Gitlab");
    dd_close(dd);

    if (!reported_to)
        return false;

    url = report_result_get_url(reported_to);

    if (!url)
        return false;

    g_autofree char *msg = g_strdup_printf(
            _("This problem has already been reported to GitLab, see %s"
              " Do you still want to create a new bug?"),
            url);

    return !libreport_ask_yes_no(msg);
}

static char *get_new_issue_url(const char *response)
{
    enum json_tokener_error error;
    json_object *json_root = json_tokener_parse_verbose(response, &error);
    if (!json_root)
        error_msg_and_die(_("Could not parse JSON response from API. Reason: %s"),
                          json_tokener_error_desc(error));

    json_object *child = NULL;
    if (!json_object_object_get_ex(json_root, "web_url", &child))
        error_msg_and_die(_("Error parsing JSON response: Entry 'web_url' not found"));

    if (!json_object_is_type(child, json_type_string))
        error_msg_and_die(_("Error parsing JSON response: Entry 'web_url' is not a string"));

    char *url = g_strdup(json_object_get_string(child));

    json_object_put(json_root);
    return url;
}

static bool is_reportable(problem_data_t *problem_data, GHashTable *settings)
{
    const char *component = problem_data_get_content_or_NULL(problem_data,
            FILENAME_COMPONENT);
    if (!component)
    {
        const char *executable = problem_data_get_content_or_NULL(problem_data,
                FILENAME_EXECUTABLE);
        error_msg_and_die(_("Cannot report problem to GitLab. Executable '%s' does"
                            " not belong to any package"), executable);
    }

    const char *allowlist = g_hash_table_lookup(settings, "Allowlist");
    /* This is assured by load_settings(). */
    assert(allowlist != NULL);
    if (strlen(allowlist) == 0)
        error_msg_and_die(_("Cannot report problem to GitLab. No component allowlist"
                            " specified (setting 'Allowlist' is empty)"));

    if (!libreport_is_in_comma_separated_list(component, allowlist))
        error_msg_and_die(_("Cannot report problem to GitLab. Component '%s' is not"
                            " on the allowlist"), component);

    return true;
}

static GHashTable *load_settings(const char *conf_file)
{
    /* TODO: Store in a struct rather than in a map? */
    g_autoptr(GHashTable) settings = g_hash_table_new_full(g_str_hash, g_str_equal,
            g_free, g_free);

    if (!libreport_load_conf_file(conf_file, settings, /*skip keys w/o value:*/ false))
        log_warning(_("Could not load settings from '%s'"), conf_file);

    /* TODO: Too repetitive. Fact our for wider use? */
    const char *value = getenv("Gitlab_GitlabProject");
    if (value != NULL)
        g_hash_table_replace(settings, g_strdup("GitlabProject"), g_strdup(value));

    value = getenv("Gitlab_GitlabURL");
    if (value != NULL)
        g_hash_table_replace(settings, g_strdup("GitlabURL"), g_strdup(value));

    value = getenv("Gitlab_PrivateToken");
    if (value != NULL)
        g_hash_table_replace(settings, g_strdup("PrivateToken"), g_strdup(value));

    value = getenv("Gitlab_SSLVerify");
    if (value != NULL)
        g_hash_table_replace(settings, g_strdup("SSLVerify"), g_strdup(value));

    value = getenv("Gitlab_Allowlist");
    if (value != NULL)
        g_hash_table_replace(settings, g_strdup("Allowlist"), g_strdup(value));

    /* TODO: Check for emptiness. */
    if (!g_hash_table_contains(settings, "GitlabProject") ||
        !g_hash_table_contains(settings, "GitlabURL") ||
        !g_hash_table_contains(settings, "PrivateToken") ||
        !g_hash_table_contains(settings, "Allowlist"))
    {
        error_msg_and_die(_("One or more of the required settings are missing:"
                            " 'GitlabProject', 'GitlabURL', 'PrivateToken' and 'Allowlist'"
                            " need to be set and nonempty"));
    }

    return g_steal_pointer(&settings);
}

static char *make_request_body(problem_report_t *report)
{
    const char *subject = problem_report_get_summary(report);
    if (!subject)
        error_msg_and_die(_("Could not obtain problem report subject"));

    const char *description = problem_report_get_description(report);
    if (!description)
        error_msg_and_die(_("Could not obtain problem report description"));

    g_autofree const char *issue_title = g_uri_escape_string(subject, NULL, false);
    g_autofree const char *issue_description = g_uri_escape_string(description, NULL, false);

    return g_strdup_printf("title=%s&description=%s", issue_title, issue_description);
}

static char *make_url(const char *component, GHashTable *settings)
{
    const char *root = g_hash_table_lookup(settings, "GitlabURL");
    const char *project = g_hash_table_lookup(settings, "GitlabProject");

    g_autofree const char *path = g_strdup_printf("/api/v4/projects/%s%%2F%s/issues",
            project, component);

    return g_build_path("/", root, path, NULL);
}

static void submit_issue(const char *dump_dir_name, problem_data_t *problem_data,
        GHashTable *settings, const char *format_file)
{
    /* Prepare problem formatter. */
    g_autoptr(problem_formatter_t) formatter = problem_formatter_new();
    if (problem_formatter_load_file(formatter, format_file))
        error_msg_and_die("Invalid format file: %s", format_file);

    /* Generate the report according to the specified template. */
    g_autoptr(problem_report_t) report = NULL;
    if (problem_formatter_generate_report(formatter, problem_data, &report))
        error_msg_and_die("Failed to format bug report from problem data");

    const char *component = problem_data_get_content_or_NULL(problem_data,
            FILENAME_COMPONENT);
    /* Shouldn't be NULL. Already checked in is_reportable(). */
    assert(component != NULL);

    /* Prepare Private-Token request header from user's setting. */
    const char *private_token = g_hash_table_lookup(settings, "PrivateToken");
    if (private_token == NULL)
        error_msg_and_die(_("GitLab personal access token not found."
                            " Please check the 'PrivateToken' setting"));

    g_autofree const char *header_private_token = g_strdup_printf("Private-Token: %s",
            private_token);
    const char *headers[] = {
        "Accept: application/json",
        header_private_token,
        NULL
    };

    /* Prepare the request and POST it. */
    g_autofree char *url = make_url(component, settings);
    g_autofree char *body = make_request_body(report);

    log_notice("Creating a new GitLab issue");
    post_state_t *post_state = new_post_state(POST_WANT_BODY | POST_WANT_HEADERS |
                                              POST_WANT_ERROR_MSG | POST_WANT_SSL_VERIFY);
    int response = post_string(post_state, url, "application/x-www-form-urlencoded",
                               headers, body);

    if (response < 0)
    {
        const char *error_msg = post_state->curl_error_msg;
        error_msg_and_die(_("Issue could not be submitted to GitLab. %s"),
                        error_msg ? error_msg : "Reason unknown");
    }

    int response_code = post_state->http_resp_code;
    char *response_body = g_steal_pointer(&post_state->body);

    /* Let GLib autocleanup take care of the dynamically allocated headers. */
    post_state->headers = NULL;
    free_post_state(post_state);

    /* Check for erroneus responses. 201 Created is the expected response. */
    if (response_code == 404 || response_code == 403)
        error_msg_and_die(_("The GitLab API could not be reached (response code %d)."
                            " Please make sure the URL for the GitLab instance is set"
                            " up correctly (check the 'GitlabURL' and 'GitlabProject'"
                            " settings)"),
                          response_code);
    if (response_code == 401)
        error_msg_and_die(_("The GitLab API returned a 401 Unauthorized response."
                            " Please make sure you have entered the correct GitLab"
                            " project and credentials (check the 'GitlabProject' and"
                            " 'PrivateToken' settings)"));
    if (response_code != 201)
        error_msg_and_die(_("Unexepected server reponse. Got status code %d"),
                          response_code);

    log_notice("Server response OK.");

    g_autofree const char *issue_url = get_new_issue_url(response_body);
    log_warning("New issue created at %s", issue_url);

    /* Store a link to the created issue in the reported_to entry. */
    add_reported_to_entry(dump_dir_name, issue_url);
}
