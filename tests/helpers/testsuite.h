/*
    Copyright (C) 2015  ABRT team <crash-catcher@lists.fedorahosted.org>
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

    Feel free to add whatever macro you need but please try to keep this file
    short and useful.

    Bare in mind usability and print as much accurate log messages as possible:

        Example 1:

            int actual = 0;
            int expected = 1;
            TS_ASSERT_SIGNED_EQ(actual, expected)

            ----

            Assert (actual == expected): FAILED
                Actual  : 0
                Expected: 1


        Example 2:

            int get_runtime_number() {
                return 0;
            }

            TS_ASSERT_SIGNED_OP_MESSAGE(get_runtime_number(), 1, "Custom message")

            ----

            Custom messages (get_runtime_number() >= 1): FAILED
                Actual  : 0
                Expected: 1
*/
#ifndef LIBREPORT_TESTSUITE_H
#define LIBREPORT_TESTSUITE_H

/* For g_verbose */
#include "internal_libreport.h"

/* For convenience */
#include <assert.h>


/* Number of failed asserts and other failures. Can be used a return value of
 * the main function. */
long g_testsuite_fails = 0;

/* Number of successful asserts. For debugging purpose. */
long g_testsuite_ok = 0;

/* Enables additional log messages. */
int g_testsuite_debug = 0;

/* Can be used to change log messages destination. */
FILE *g_testsuite_output_stream = 0;


/*
 * Test case definition
 */

#define TS_MAIN \
    int main(int argc, char *argv[]) { g_verbose = 3; do

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
        TS_DEBUG_PRINTF(format, __VA_ARGS__); \
        ++g_testsuite_ok; \
    } while (0)

#define TS_FAILURE(format, ...) \
    do { \
        TS_PRINTF(format, __VA_ARGS__); \
        ++g_testsuite_fails; \
    } while (0)


/*
 * Testing of signed numbers
 */

#define TS_ASSERT_SIGNED_OP_MESSAGE(actual, operator, expected, message) \
    do { \
        long long l_ts_lhs = (actual); \
        long long l_ts_rhs = (expected); \
        if (l_ts_lhs operator l_ts_rhs) { \
            TS_SUCCESS("%s ("#actual" "#operator" "#expected"): OK\n\tActual  : %lld\n", message ? message : "Assert", l_ts_lhs); \
        } \
        else { \
            TS_FAILURE("%s ("#actual" "#operator" "#expected"): FAILED\n\tActual  : %lld\n\tExpected: %lld\n", message ? message : "Assert", l_ts_lhs, l_ts_rhs); \
        } \
    } while(0)

#define TS_ASSERT_SIGNED_EQ(actual, expected) \
    TS_ASSERT_SIGNED_OP_MESSAGE(actual, ==, expected, NULL)

#define TS_ASSERT_SIGNED_GE(actual, expected) \
    TS_ASSERT_SIGNED_OP_MESSAGE(actual, >=, expected, NULL)


#endif/*LIBREPORT_TESTSUITE_H*/
