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

/* C: void alert(const char *message); */
PyObject *p_alert(PyObject *pself, PyObject *args)
{
    const char *message;
    if (!PyArg_ParseTuple(args, "s", &message))
    {
        return NULL;
    }
    alert(message);
    Py_RETURN_NONE;
}

/* C: char *ask(const char *question, char *response, int response_len); */
PyObject *p_ask(PyObject *pself, PyObject *args)
{
    const char *question;
    if (!PyArg_ParseTuple(args, "s", &question))
    {
        return NULL;
    }

    char *response = ask(question);
    if (!response)
    {
        Py_RETURN_NONE;
    }

    PyObject *r = Py_BuildValue("s", response);
    free(response);
    return r;
}

/* C: char *ask_password(const char *question, char *response, int response_len); */
PyObject *p_ask_password(PyObject *pself, PyObject *args)
{
    const char *question;
    if (!PyArg_ParseTuple(args, "s", &question))
    {
        return NULL;
    }

    char *response = ask_password(question);
    if (!response)
    {
        Py_RETURN_NONE;
    }

    PyObject *r = Py_BuildValue("s", response);
    free(response);
    return r;
}

/* C: int ask_yes_no(const char *question); */
PyObject *p_ask_yes_no(PyObject *pself, PyObject *args)
{
    const char *question;
    if (!PyArg_ParseTuple(args, "s", &question))
    {
        return NULL;
    }

    int response = ask_yes_no(question);

    return Py_BuildValue("i", response);
}

/* C: int ask_yes_no_yesforever(const char *key, const char *question); */
PyObject *p_ask_yes_no_yesforever(PyObject *pself, PyObject *args)
{
    const char *key;
    const char *question;
    if (!PyArg_ParseTuple(args, "ss", &key, &question))
    {
        return NULL;
    }

    int response = ask_yes_no_yesforever(key, question);

    return Py_BuildValue("i", response);
}
