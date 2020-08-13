/*
    Copyright (C) 2015-2017  ABRT team <crash-catcher@lists.fedorahosted.org>
    Copyright (C) 2015  RedHat inc.

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

    ----

    libreport testsuite helpers

    These helpers should make writing of libreport tests very convenient. Here
    is an example of how a test case can look like.

        #include "testsuite.h"
        #include "testsuite_tools.h"

        TS_MAIN
        {
            struct dump_dir *dd = testsuite_dump_dir_create(getuid(), -1, 0);

            TS_ASSERT_PTR_IS_NOT_NULL(dd);

            testsuite_dump_dir_delete(dd);
        }
        TS_RETURN_MAIN

    Both failed and successful asserts updates relevant global counters and the
    TS_RETURN_MAIN macro includes the return statement with the value of the
    failed asserts counter. Hence, the example test case exits with 1 if dd is
    NULL.

    ----

    Feel free to add whatever macro you need but please try to keep this file
    short and useful.

    Please follow the rule where the actual value is on left side and the
    expected values is on right side.

    Bare in mind usability and print as much accurate log messages as possible:

        Example 1:

            int actual = 0;
            int expected = 1;
            TS_ASSERT_SIGNED_EQ(actual, expected)

            ----

            [ FAILED ] 12: Assert (actual == expected)
                Actual  : 0
                Expected: 1


        Example 2:

            int get_runtime_number() {
                return 0;
            }

            TS_ASSERT_SIGNED_OP_MESSAGE(get_runtime_number(), 1, "Custom message")

            ----

            [ FAILED ] 3: Custom messages (get_runtime_number() >= 1)
                Actual  : 0
                Expected: 1

    Note: the number right behind [ FAILED ] is line number where the failed
          assert is located.
*/
#ifndef LIBREPORT_TESTSUITE_H
#define LIBREPORT_TESTSUITE_H

/* For libreport_g_verbose */
#include "internal_libreport.h"

/* For convenience */
#include <assert.h>


/* Number of failed asserts and other failures. Can be used a return value of
 * the main function. */
long g_testsuite_fails = 0;

/* Number of successful asserts. For debugging purpose. */
long g_testsuite_ok = 0;

/* 1 if the last check succeeded, 0 of the last check failed */
int g_testsuite_last_ok = 0;

/* Enables additional log messages. */
int g_testsuite_debug = 0;

/* Can be used to change log messages destination. */
FILE *g_testsuite_output_stream = 0;


/*
 * Test case definition
 */

#define TS_MAIN \
    int main(int argc, char *argv[]) { libreport_g_verbose = 3; do

#define TS_RETURN_MAIN \
    while (0) ;\
    return g_testsuite_fails; }


/*
 * Logging
 */

#define TS_PRINTF(format, ...) \
    fprintf(g_testsuite_output_stream != NULL ? g_testsuite_output_stream : stderr, format, __VA_ARGS__)

#define TS_DEBUG_PRINTF(format, ...) \
    do { if (g_testsuite_debug) { TS_PRINTF(format, __VA_ARGS__); } } while (0)


/*
 * Handling of test results
 */

#define TS_SUCCESS(format, ...) \
    do { \
        TS_DEBUG_PRINTF("[   OK   ] %d: ", __LINE__); \
        TS_DEBUG_PRINTF(format, __VA_ARGS__); \
        ++g_testsuite_ok; \
        g_testsuite_last_ok = 1; \
    } while (0)

#define TS_FAILURE(format, ...) \
    do { \
        TS_PRINTF("[ FAILED ] %d: ", __LINE__); \
        TS_PRINTF(format, __VA_ARGS__); \
        ++g_testsuite_fails; \
        g_testsuite_last_ok = 0; \
    } while (0)


/*
 * Logical conditions
 */

#define _TS_ASSERT_BOOLEAN(expression, expected, message) \
    do { \
        const int l_ts_result = (expression); \
        if (l_ts_result == expected) { \
            TS_SUCCESS("%s ("#expression" == %s)\n", message ? message : "Assert", expected ? "TRUE" : "FALSE"); \
        }\
        else { \
            TS_FAILURE("%s ("#expression" == %s)\n", message ? message : "Assert", expected ? "TRUE" : "FALSE"); \
        }\
    } while(0)


#define TS_ASSERT_TRUE_MESSAGE(expression, message) \
    _TS_ASSERT_BOOLEAN(expression, 1, message)

#define TS_ASSERT_TRUE(expression) \
    TS_ASSERT_TRUE_MESSAGE(expression, NULL)

#define TS_ASSERT_FALSE_MESSAGE(expression, message) \
    _TS_ASSERT_BOOLEAN(expression, 0, message)

#define TS_ASSERT_FALSE(expression) \
    TS_ASSERT_FALSE_MESSAGE(expression, NULL)

/*
 * Testing of signed numbers
 */

#define TS_ASSERT_SIGNED_OP_MESSAGE(actual, operator, expected, message) \
    do { \
        long long l_ts_lhs = (actual); \
        long long l_ts_rhs = (expected); \
        if (l_ts_lhs operator l_ts_rhs) { \
            TS_SUCCESS("%s ("#actual" "#operator" "#expected")\n\tActual  : %lld\n", message ? message : "Assert", l_ts_lhs); \
        } \
        else { \
            TS_FAILURE("%s ("#actual" "#operator" "#expected")\n\tActual  : %lld\n\tExpected: %lld\n", message ? message : "Assert", l_ts_lhs, l_ts_rhs); \
        } \
    } while(0)

#define TS_ASSERT_SIGNED_EQ(actual, expected) \
    TS_ASSERT_SIGNED_OP_MESSAGE(actual, ==, expected, NULL)

#define TS_ASSERT_SIGNED_NEQ(actual, expected) \
    TS_ASSERT_SIGNED_OP_MESSAGE(actual, !=, expected, NULL)

#define TS_ASSERT_SIGNED_GE(actual, expected) \
    TS_ASSERT_SIGNED_OP_MESSAGE(actual, >=, expected, NULL)

#define TS_ASSERT_SIGNED_GT(actual, expected) \
    TS_ASSERT_SIGNED_OP_MESSAGE(actual, >, expected, NULL)

#define TS_ASSERT_SIGNED_LE(actual, expected) \
    TS_ASSERT_SIGNED_OP_MESSAGE(actual, <=, expected, NULL)

#define TS_ASSERT_SIGNED_LT(actual, expected) \
    TS_ASSERT_SIGNED_OP_MESSAGE(actual, <, expected, NULL)


/*
 * Testing of chars
 */

#define TS_ASSERT_CHAR_OP_MESSAGE(actual, operator, expected, message) \
    do { \
        char l_ts_lhs = (actual); \
        char l_ts_rhs = (expected); \
        if (l_ts_lhs operator l_ts_rhs) { \
            TS_SUCCESS("%s ("#actual" "#operator" "#expected")\n\tActual  : %c\n", message ? message : "Assert", l_ts_lhs); \
        } \
        else { \
            TS_FAILURE("%s ("#actual" "#operator" "#expected")\n\tActual  : %c\n\tExpected: %c\n", message ? message : "Assert", l_ts_lhs, l_ts_rhs); \
        } \
    } while(0)

#define TS_ASSERT_CHAR_EQ_MESSAGE(actual, expected, message) \
    TS_ASSERT_CHAR_OP_MESSAGE(actual, ==, expected, message)

#define TS_ASSERT_CHAR_EQ(actual, expected) \
    TS_ASSERT_CHAR_EQ_MESSAGE(actual, expected, NULL)


/*
 * Testing of strings
 */

#define TS_ASSERT_STRING_EQ(actual, expected, message) \
    do { \
        const char *l_ts_lhs = (actual); \
        const char *l_ts_rhs = (expected); \
        if (l_ts_lhs == NULL && l_ts_rhs != NULL) { \
            TS_FAILURE("%s ("#actual" == "#expected")\n\tActual  : NULL\n\tExpected: %s\n", message ? message : "Assert", l_ts_rhs); \
        } \
        else if (l_ts_lhs != NULL && l_ts_rhs == NULL) { \
            TS_FAILURE("%s ("#actual" == "#expected")\n\tActual  : %s\n\tExpected: NULL\n", message ? message : "Assert", l_ts_lhs); \
        } \
        else if ((l_ts_rhs == NULL && l_ts_rhs == NULL)) { \
            TS_SUCCESS("%s ("#actual" == "#expected")\n\tActual  : NULL\n", message ? message : "Assert"); \
        } \
        else if (strcmp(l_ts_lhs, l_ts_rhs) == 0) { \
            TS_SUCCESS("%s ("#actual" == "#expected")\n\tActual  : %s\n", message ? message : "Assert", l_ts_lhs); \
        } \
        else { \
            TS_FAILURE("%s ("#actual" == "#expected")\n\tActual  : %s\n\tExpected: %s\n", message ? message : "Assert", l_ts_lhs, l_ts_rhs); \
        } \
    } while(0)

#define TS_ASSERT_STRING_BEGINS_WITH(actual, prefix, message) \
    do { \
        const char *l_ts_lhs = (actual); \
        const char *l_ts_rhs = (prefix); \
        if (l_ts_lhs == NULL && l_ts_rhs != NULL) { \
            TS_FAILURE("%s ("#actual" begins with "#prefix")\n\tActual  : NULL\n\tExpected: %p\n", message ? message : "Assert", l_ts_rhs); \
        } \
        else if (l_ts_lhs != NULL && l_ts_rhs == NULL) { \
            TS_FAILURE("%s ("#actual" begins with "#prefix")\n\tActual  : %s\n\tExpected: NULL\n", message ? message : "Assert", l_ts_lhs); \
        } \
        else if ((l_ts_rhs == NULL && l_ts_rhs == NULL)) { \
            TS_SUCCESS("%s ("#actual" begins with "#prefix")\n\tActual  : NULL\n", message ? message : "Assert"); \
        } \
        else if (strncmp(l_ts_lhs, l_ts_rhs, strlen(l_ts_rhs)) == 0) { \
            TS_SUCCESS("%s ("#actual" begins with "#prefix")\n\tActual  : %s\n", message ? message : "Assert", l_ts_lhs); \
        } \
        else { \
            TS_FAILURE("%s ("#actual" begins with "#prefix")\n\tActual  : %s\n\tExpected: %s\n", message ? message : "Assert", l_ts_lhs, l_ts_rhs); \
        } \
    } while(0)

#define TS_ASSERT_STRING_NULL_OR_EMPTY(actual, message) \
    do { \
        const char *l_ts_lhs = (actual); \
        if (l_ts_lhs != NULL && l_ts_lhs[0] != '\0') { \
            TS_FAILURE("%s ("#actual" is NULL or empty)\n\tActual  : %s\n", message ? message : "Assert", l_ts_lhs); \
        } \
        else if ((l_ts_lhs != NULL && l_ts_lhs[0] == '\0')) { \
            TS_SUCCESS("%s ("#actual" is NULL or empty)\n\tActual  : is empty\n", message ? message : "Assert"); \
        } \
        else if (l_ts_lhs == NULL) { \
            TS_SUCCESS("%s ("#actual" is NULL or empty)\n\tActual  : is NULL\n", message ? message : "Assert"); \
        } \
        else { \
            TS_PRINTF("%s", "Invalid conditions in TS_ASSERT_STRING_NULL_OR_EMPTY"); \
            abort(); \
        } \
    } while(0)

/*
 * Testing of pointers
 */

#define TS_ASSERT_PTR_OP_MESSAGE(actual, operator, expected, message) \
    do { \
        const void *l_ts_lhs = (actual); \
        const void *l_ts_rhs = (expected); \
        if (l_ts_lhs operator l_ts_rhs) { \
            TS_SUCCESS("%s ("#actual" "#operator" "#expected")\n\tActual  : %p\n", message ? message : "Assert", l_ts_lhs); \
        } \
        else { \
            TS_FAILURE("%s ("#actual" "#operator" "#expected")\n\tActual  : %p\n\tExpected: %p\n", message ? message : "Assert", l_ts_lhs, l_ts_rhs); \
        } \
    } while(0)


#define TS_ASSERT_PTR_IS_NULL_MESSAGE(actual, message) \
    TS_ASSERT_PTR_OP_MESSAGE(actual, ==, NULL, message);

#define TS_ASSERT_PTR_IS_NULL(actual) \
    TS_ASSERT_PTR_IS_NULL_MESSAGE(actual, NULL);


#define TS_ASSERT_PTR_IS_NOT_NULL_MESSAGE(actual, message) \
    TS_ASSERT_PTR_OP_MESSAGE(actual, !=, NULL, message);

#define TS_ASSERT_PTR_IS_NOT_NULL(actual) \
    TS_ASSERT_PTR_IS_NOT_NULL_MESSAGE(actual, NULL);


#define TS_ASSERT_PTR_EQ(actual, expected) \
    TS_ASSERT_PTR_OP_MESSAGE(actual, ==, expected, NULL);



/*
 * Standard functions returning non-0 on errors
 */

#define TS_ASSERT_FUNCTION_MESSAGE(call, message) \
    do { \
        const int l_ts_lhs = call; \
        if (l_ts_lhs == 0) { \
            TS_SUCCESS("%s ('"#call"')\n", message ? message : "Function SUCCEEDED"); \
        } \
        else { \
            TS_FAILURE("%s ('"#call"')\n\tCode  : %d\n", message ? message : "Function FAILED", l_ts_lhs); \
        } \
    } while(0)


#define TS_ASSERT_FUNCTION(call) \
    TS_ASSERT_FUNCTION_MESSAGE(call, NULL)



/*
 * Testing of contents of file descriptors
 */

#define TS_ASSERT_STREAM_FD_CONTENTS_EQ_BEGIN(cfd) \
    do { \
        const int ts_bck_fd = cfd; \
        const int ts_old_fd = libreport_xdup(ts_bck_fd); \
        int ts_pipefd[2]; \
        pipe(ts_pipefd); \
        fcntl(ts_pipefd[0], F_SETFL, O_NONBLOCK); \
        libreport_xmove_fd(ts_pipefd[1], ts_bck_fd); \

#define TS_ASSERT_STREAM_FD_CONTENTS_EQ_END(expected, message) \
        libreport_xmove_fd(ts_old_fd, ts_bck_fd); \
        char *ts_fd_contents = libreport_xmalloc_read(ts_pipefd[0], NULL); \
        close(ts_pipefd[0]); \
        TS_ASSERT_STRING_EQ(ts_fd_contents, expected, message); \
        free(ts_fd_contents); \
    } while (0)

#define TS_ASSERT_STDERR_EQ_BEGIN \
    TS_ASSERT_STREAM_FD_CONTENTS_EQ_BEGIN(STDERR_FILENO)

#define TS_ASSERT_STDERR_EQ_END(expected, message) \
    TS_ASSERT_STREAM_FD_CONTENTS_EQ_END(expected, message)

#endif/*LIBREPORT_TESTSUITE_H*/
