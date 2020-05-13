/*
    Copyright (C) 2010  Abrt team.
    Copyright (C) 2010  RedHat inc.

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
#include <Python.h>

#include "common.h"
#include "internal_libreport.h"

#if PY_MAJOR_VERSION >= 3
  #define MOD_ERROR_VAL NULL
  #define MOD_SUCCESS_VAL(val) val
  #define MOD_INIT PyMODINIT_FUNC PyInit__py3report(void)
  #define MOD_DEF(ob, name, doc, methods) \
            static struct PyModuleDef moduledef = { \
                          PyModuleDef_HEAD_INIT, name, doc, -1, methods, \
                          NULL, NULL, NULL, NULL }; \
          ob = PyModule_Create(&moduledef);
#else
  #define MOD_ERROR_VAL
  #define MOD_SUCCESS_VAL(val)
  #define MOD_INIT void init_pyreport(void)
  #define MOD_DEF(ob, name, doc, methods) \
            ob = Py_InitModule3(name, methods, doc);
#endif

static char module_doc[] = "Python wrapper for libreport";

PyObject *ReportError;

static PyMethodDef module_methods[] = {
    /* method_name, func, flags, doc_string */
    /* for include/report/dump_dir.h */
    { "dd_opendir"                , p_dd_opendir              , METH_VARARGS, NULL },
    { "dd_create"                 , p_dd_create               , METH_VARARGS, NULL },
    { "delete_dump_dir"           , p_delete_dump_dir         , METH_VARARGS, NULL },
    /* for include/report/report.h */
    { "report_problem_in_dir"     , p_report_problem_in_dir   , METH_VARARGS, NULL },
    { "report_problem_in_memory"  , p_report_problem_in_memory, METH_VARARGS, NULL },
    { "report_problem"            , p_report_problem          , METH_VARARGS, NULL },
    { NULL, NULL, 0, NULL }
};

MOD_INIT
{
    if (PyType_Ready(&p_problem_data_type) < 0)
    {
        printf("PyType_Ready(&p_problem_data_type) < 0\n");
        return MOD_ERROR_VAL;
    }
    if (PyType_Ready(&p_dump_dir_type) < 0)
    {
        printf("PyType_Ready(&p_dump_dir_type) < 0\n");
        return MOD_ERROR_VAL;
    }
    if (PyType_Ready(&p_run_event_state_type) < 0)
    {
        printf("PyType_Ready(&p_run_event_state_type) < 0\n");
        return MOD_ERROR_VAL;
    }


    PyObject *m;
    MOD_DEF(m, "_pyreport", module_doc, module_methods);
    if (m == NULL)
    {
        return MOD_ERROR_VAL;
    }

    /* init the exception object */
    ReportError = PyErr_NewException((char*) "_pyreport.error", NULL, NULL);
    Py_INCREF(ReportError);
    PyModule_AddObject(m, "error", ReportError);

    /* init type objects and constants */
    /* for include/report/problem_data.h */
    Py_INCREF(&p_problem_data_type);
    PyModule_AddObject(m, "problem_data", (PyObject *)&p_problem_data_type);
    PyModule_AddObject(m, "CD_FLAG_BIN"          , Py_BuildValue("i", CD_FLAG_BIN          ));
    PyModule_AddObject(m, "CD_FLAG_TXT"          , Py_BuildValue("i", CD_FLAG_TXT          ));
    PyModule_AddObject(m, "CD_FLAG_ISEDITABLE"   , Py_BuildValue("i", CD_FLAG_ISEDITABLE   ));
    PyModule_AddObject(m, "CD_FLAG_ISNOTEDITABLE", Py_BuildValue("i", CD_FLAG_ISNOTEDITABLE));
    /* for include/report/dump_dir.h */
    Py_INCREF(&p_dump_dir_type);
    PyModule_AddObject(m, "dump_dir", (PyObject *)&p_dump_dir_type);
    PyModule_AddObject(m, "DD_FAIL_QUIETLY_ENOENT"             , Py_BuildValue("i", DD_FAIL_QUIETLY_ENOENT             ));
    PyModule_AddObject(m, "DD_FAIL_QUIETLY_EACCES"             , Py_BuildValue("i", DD_FAIL_QUIETLY_EACCES             ));
    PyModule_AddObject(m, "DD_OPEN_READONLY"                   , Py_BuildValue("i", DD_OPEN_READONLY                   ));
    PyModule_AddObject(m, "DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE", Py_BuildValue("i", DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE));
    /* for include/report/run_event.h */
    Py_INCREF(&p_run_event_state_type);
    PyModule_AddObject(m, "run_event_state", (PyObject *)&p_run_event_state_type);
    /* for include/report/report.h */
    PyModule_AddObject(m, "LIBREPORT_NOWAIT"     , Py_BuildValue("i", LIBREPORT_NOWAIT     ));
    PyModule_AddObject(m, "LIBREPORT_WAIT"       , Py_BuildValue("i", LIBREPORT_WAIT       ));
    PyModule_AddObject(m, "LIBREPORT_GETPID"     , Py_BuildValue("i", LIBREPORT_GETPID     ));
    PyModule_AddObject(m, "LIBREPORT_RELOAD_DATA", Py_BuildValue("i", LIBREPORT_RELOAD_DATA));
    PyModule_AddObject(m, "LIBREPORT_DEL_DIR"    , Py_BuildValue("i", LIBREPORT_DEL_DIR    ));
    PyModule_AddObject(m, "LIBREPORT_RUN_CLI"    , Py_BuildValue("i", LIBREPORT_RUN_CLI    ));
    PyModule_AddObject(m, "LIBREPORT_RUN_NEWT"   , Py_BuildValue("i", LIBREPORT_RUN_NEWT  ));
    PyModule_AddObject(m, "LIBREPORT_IGNORE_NOT_REPORTABLE", Py_BuildValue("i", LIBREPORT_IGNORE_NOT_REPORTABLE));
    PyModule_AddObject(m, "EXIT_CANCEL_BY_USER", Py_BuildValue("i", EXIT_CANCEL_BY_USER));
    PyModule_AddObject(m, "EXIT_STOP_EVENT_RUN", Py_BuildValue("i", EXIT_STOP_EVENT_RUN));

    return MOD_SUCCESS_VAL(m);
}
