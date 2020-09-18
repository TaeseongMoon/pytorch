#pragma once
#include <ATen/ATen.h>
#include <pybind11/pybind11.h>
#include <iostream>
#include <map>
#include <thread>
#include <vector>
namespace py = pybind11;

// TODO this should come from cmake
#define DEBUG 1

#if (DEBUG == 1)
#define PYOBJ_ASSERT(obj) \
  if (NULL == obj) {      \
    PyErr_Print();        \
  }                       \
  assert(NULL != obj);
#elif (DEBUG == 0)
#define PYOBJ_ASSERT(obj) assert(NULL != obj);
#endif

namespace torchpy {

// TODO fix symbol visibility issue
// https://stackoverflow.com/questions/2828738/c-warning-declared-with-greater-visibility-than-the-type-of-its-field

void init();
void finalize();

bool load(const char* filename);
at::Tensor forward(at::Tensor input);
}
