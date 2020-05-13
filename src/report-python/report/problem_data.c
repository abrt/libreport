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
#include <structmember.h>

#include <errno.h>
#include "common.h"

static void
p_problem_data_dealloc(PyObject *pself)
{
    p_problem_data *self = (p_problem_data*)pself;
    problem_data_free(self->cd);
    self->cd = NULL;
    Py_TYPE(self)->tp_free(pself);
}

static PyObject *
p_problem_data_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    p_problem_data *self = (p_problem_data *)type->tp_alloc(type, 0);
    if (self)
        self->cd = problem_data_new();
    return (PyObject *)self;
}

static PyObject *p_problem_data_add(PyObject *pself, PyObject *args)
{
    p_problem_data *self = (p_problem_data*)pself;

    const char *name;
    const char *content;
    int flags = 0;
    if (!PyArg_ParseTuple(args, "ss|i", &name, &content, &flags))
    {
        /* PyArg_ParseTuple raises the exception saying why it fails
         * eg: TypeError: function takes exactly 2 arguments (1 given)
         */
        return NULL;
    }
    problem_data_add(self->cd, name, content, flags);

    /* every function returns PyObject, to return void we need to do this */
    Py_RETURN_NONE;
}

/* struct problem_item *problem_data_get_item_or_NULL(problem_data_t *problem_data, const char *key); */
static PyObject *p_problem_data_get_item(PyObject *pself, PyObject *args)
{
    p_problem_data *self = (p_problem_data*)pself;
    const char *key;
    if (!PyArg_ParseTuple(args, "s", &key))
    {
        return NULL;
    }
    struct problem_item *ci = problem_data_get_item_or_NULL(self->cd, key);
    if (ci == NULL)
    {
        Py_RETURN_NONE;
    }
    return Py_BuildValue("sI", ci->content, ci->flags);
}

/* struct dump_dir *create_dump_dir_from_problem_data(problem_data_t *problem_data, const char *base_dir_name); */
static PyObject *p_create_dump_dir_from_problem_data(PyObject *pself, PyObject *args)
{
    p_problem_data *self = (p_problem_data*)pself;
    const char *base_dir_name = NULL;
    if (!PyArg_ParseTuple(args, "|s", &base_dir_name))
    {
        return NULL;
    }
    p_dump_dir *new_dd = PyObject_New(p_dump_dir, &p_dump_dir_type);
    if (!new_dd)
        return NULL;
    struct dump_dir *dd = create_dump_dir_from_problem_data(self->cd, base_dir_name);
    if (!dd)
    {
        PyObject_Del((PyObject*)new_dd);
        PyErr_SetString(ReportError, "Can't create the dump dir");
        return NULL;
    }
    new_dd->dd = dd;
    return (PyObject*)new_dd;
}

static PyObject *p_problem_data_add_basics(PyObject *pself, PyObject *always_null)
{
    p_problem_data *self = (p_problem_data*)pself;
    problem_data_add_basics(self->cd);

    Py_RETURN_NONE;
}

static PyObject *p_problem_data_add_current_process(PyObject *pself, PyObject *always_null)
{
    p_problem_data *self = (p_problem_data*)pself;
    problem_data_add_current_process_data(self->cd);

    Py_RETURN_NONE;
}

static PyObject *p_problem_data_send_to_abrt(PyObject *pself, PyObject *always_null)
{
    p_problem_data *self = (p_problem_data*)pself;
    int result = problem_data_send_to_abrt(self->cd);

    return Py_BuildValue("i", result);
}

//static PyMemberDef p_problem_data_members[] = {
//    { NULL }
//};

static PyMethodDef p_problem_data_methods[] = {
    /* method_name, func, flags, doc_string */
    { "add"                 , p_problem_data_add                 , METH_VARARGS, NULL },
    { "get"                 , p_problem_data_get_item            , METH_VARARGS, NULL },
    { "create_dump_dir"     , p_create_dump_dir_from_problem_data, METH_VARARGS, NULL },
    { "add_basics"          , p_problem_data_add_basics          , METH_NOARGS, NULL },
    { "add_current_proccess", p_problem_data_add_current_process , METH_NOARGS, NULL },
    { "send_to_abrt"        , p_problem_data_send_to_abrt        , METH_NOARGS, NULL },
    { NULL, NULL, 0, NULL }
};

PyTypeObject p_problem_data_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "report.problem_data",
    .tp_basicsize = sizeof(p_problem_data),
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new       = p_problem_data_new,
    .tp_dealloc   = p_problem_data_dealloc,
    //.tp_init      = p_problem_data_init,
    //.tp_members   = p_problem_data_members,
    .tp_methods   = p_problem_data_methods,
};
