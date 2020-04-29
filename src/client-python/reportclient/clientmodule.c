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

static PyMethodDef module_methods[] = {
    /* method_name, func, flags, doc_string */
    /* for include/client.h */
    { "alert"                     , p_alert                   , METH_VARARGS, NULL },
    { "ask"                       , p_ask                     , METH_VARARGS, NULL },
    { "ask_password"              , p_ask_password            , METH_VARARGS, NULL },
    { "ask_yes_no"                , p_ask_yes_no              , METH_VARARGS, NULL },
    { "ask_yes_no_yesforever"     , p_ask_yes_no_yesforever   , METH_VARARGS, NULL },
    { "ask_yes_no_save_result"    , p_ask_yes_no_save_result  , METH_VARARGS, NULL },
    { NULL, NULL, 0, NULL }
};

#if PY_MAJOR_VERSION >= 3
  #define MOD_ERROR_VAL NULL
  #define MOD_SUCCESS_VAL(val) val
  #define MOD_INIT PyMODINIT_FUNC PyInit__reportclient3(void)
  #define MOD_DEF(ob, name, doc, methods) \
            static struct PyModuleDef moduledef = { \
              PyModuleDef_HEAD_INIT, name, doc, -1, methods, \
              NULL, NULL, NULL, NULL}; \
            ob = PyModule_Create(&moduledef);
#else
  #define MOD_ERROR_VAL
  #define MOD_SUCCESS_VAL(val)
  #define MOD_INIT void init_reportclient(void)
  #define MOD_DEF(ob, name, doc, methods) \
            ob = Py_InitModule3(name, methods, doc);
#endif

MOD_INIT
{
    PyObject *m;
    MOD_DEF(m, "_reportclient", NULL, module_methods);
    if (!m)
    {
        printf("m == NULL\n");
        return MOD_ERROR_VAL;
    }

    return MOD_SUCCESS_VAL(m);
}
