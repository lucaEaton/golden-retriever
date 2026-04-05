//
// Created by luca eaton on 4/4/26.
//

#define PY_SSIZE_T_CLEAN
#include "cpp_email_classifier.h"
#include <Python.h>
#include <string>
#include <iostream>

std::string classify(const std::string &email) {
    static bool initialized = false;
    static PyObject *module = nullptr;
    static PyObject *func = nullptr;

    // we do this so that we aren't reinitializing
    if (!initialized) {
        Py_Initialize();
        PyRun_SimpleString("import sys; sys.path.append('../email_status_classifier')");
        PyObject *name = PyUnicode_FromString("test");
        module = PyImport_Import(name);
        Py_DECREF(name);
        if (!module) { PyErr_Print(); return "error: module not found"; }
        func = PyObject_GetAttrString(module, "pass_in_model");
        if (!func || !PyCallable_Check(func)) { PyErr_Print(); return "error: function not found"; }
        initialized = true;
    }

    PyObject *py_str = PyUnicode_FromString(email.c_str());
    if (!py_str) return "error: string conversion failed";
    PyObject *args = PyTuple_Pack(1, py_str);
    Py_DECREF(py_str);
    PyObject *result = PyObject_CallObject(func, args);
    Py_DECREF(args);
    if (!result) { PyErr_Print(); return "error: call failed"; }
    const char *raw = PyUnicode_AsUTF8(result);
    if (!raw) { PyErr_Print(); Py_DECREF(result); return "error: result not a string"; }
    std::string output = raw;
    Py_DECREF(result);
    return output;
}