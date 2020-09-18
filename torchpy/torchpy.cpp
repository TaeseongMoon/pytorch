#include <torchpy.h>
#include <Python.h>
#include <assert.h>
#include <stdio.h>
#include <torch/script.h>
#include <torch/torch.h>
#include <iostream>
#include "torch/csrc/autograd/python_variable.h"

namespace torchpy {

void init() {
  Py_Initialize();
}

void finalize() {
  Py_Finalize();
}
// https://docs.python.org/3/extending/extending.html
// https://docs.python.org/3/c-api/
std::string hello() {
  PyObject* globals = PyDict_New();
  assert(PyDict_Check(globals) == true);
  PyObject* builtins = PyEval_GetBuiltins();
  assert(NULL != builtins);
  PyDict_SetItemString(globals, "__builtins__", builtins);

  PyRun_String(
      "def hello():\n"
      "   return \"Hello Py\"\n"
      "print(hello())\n",
      Py_file_input,
      globals,
      globals);

  PyObject* hello = PyDict_GetItemString(globals, "hello");
  assert(PyFunction_Check(hello) == true);
  PyObject* pyresult = PyObject_CallObject(hello, NULL);
  auto c_str = PyUnicode_AsUTF8(pyresult);
  assert(NULL != c_str);
  auto result = std::string(c_str);

  return result;
}

// TODO decref the python thigns
PyModule load(const std::string& filename) {
  PyObject* globals = PyDict_New();

  assert(PyDict_Check(globals) == true);
  PyObject* builtins = PyEval_GetBuiltins();
  assert(NULL != builtins);
  PyDict_SetItemString(globals, "__builtins__", builtins);
  FILE* fp = fopen("torchpy/loader.py", "r");
  PyRun_File(fp, "loader.py", Py_file_input, globals, globals);

  PyObject* load = PyDict_GetItemString(globals, "load");
  assert(PyFunction_Check(load) == true);

  PyObject* args = Py_BuildValue("(s)", filename.c_str());
  PyObject* module = PyObject_CallObject(load, args);
  assert(NULL != module);
  auto mod = PyModule(globals, module);
  return mod;
}

at::Tensor PyModule::forward(std::vector<at::Tensor> inputs) {
  assert(NULL != _module);

  std::vector<PyObject*> py_inputs;
  for (at::Tensor& tensor : inputs) {
    PyObject* py_tensor = THPVariable_Wrap(tensor);
    assert(NULL != py_tensor);
    py_inputs.push_back(py_tensor);
  }
  assert(py_inputs.size() == 1); // need to figure out variable arg input
  PyObject* py_output =
      PyObject_CallMethod(_module, "forward", "(O)", py_inputs[0]);
  assert(NULL != py_output);
  at::Tensor& output = THPVariable_Unpack(py_output);
  return output;
}

} // namespace torchpy