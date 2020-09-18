#include <dlfcn.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <iostream>
#include "interpreter_impl.h"

#include <assert.h>
#include <pybind11/embed.h>
#include <stdio.h>
#include <torch/csrc/autograd/generated/variable_factories.h>
#include <torch/csrc/jit/python/pybind_utils.h>
#include <iostream>
#include <map>
#include <thread>

#include <fmt/format.h>

namespace py = pybind11;
using namespace py::literals;

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

static wchar_t* program;

#define FOREACH_LIBRARY(_) \
  _(array)                 \
  _(_asyncio)              \
  _(audioop)               \
  _(binascii)              \
  _(_bisect)               \
  _(_blake2)               \
  _(_bz2)                  \
  _(cmath)                 \
  _(_codecs_cn)            \
  _(_codecs_hk)            \
  _(_codecs_iso2022)       \
  _(_codecs_jp)            \
  _(_codecs_kr)            \
  _(_codecs_tw)            \
  _(_contextvars)          \
  _(_crypt)                \
  _(_csv)                  \
  _(_ctypes)               \
  _(_ctypes_test)          \
  _(_curses)               \
  _(_curses_panel)         \
  _(_datetime)             \
  _(_decimal)              \
  _(_elementtree)          \
  _(fcntl)                 \
  _(grp)                   \
  _(_hashlib)              \
  _(_heapq)                \
  _(_json)                 \
  _(_lsprof)               \
  _(_lzma)                 \
  _(math)                  \
  _(_md5)                  \
  _(mmap)                  \
  _(_multibytecodec)       \
  _(_multiprocessing)      \
  _(nis)                   \
  _(_opcode)               \
  _(ossaudiodev)           \
  _(parser)                \
  _(_pickle)               \
  _(_posixsubprocess)      \
  _(pyexpat)               \
  _(_queue)                \
  _(_random)               \
  _(readline)              \
  _(resource)              \
  _(select)                \
  _(_sha1)                 \
  _(_sha256)               \
  _(_sha3)                 \
  _(_sha512)               \
  _(_socket)               \
  _(spwd)                  \
  _(_ssl)                  \
  _(_struct)               \
  _(syslog)                \
  _(termios)               \
  _(_testbuffer)           \
  _(_testcapi)             \
  _(_testimportmultiple)   \
  _(_testmultiphase)       \
  _(unicodedata)           \
  _(xxlimited)             \
  _(_xxtestfuzz)           \
  _(zlib)

#define DECLARE_LIBRARY_INIT(name) extern "C" PyObject* PyInit_##name(void);
FOREACH_LIBRARY(DECLARE_LIBRARY_INIT)
#undef DECLARE_LIBRARY_INIT

extern "C" __attribute__((visibility("default"))) void initialize_interface(
    InterpreterImpl* s) {
#define INITIALIZE_MEMBER(func) s->func = func;
  FOREACH_INTERFACE_FUNCTION(INITIALIZE_MEMBER)
#undef INITIALIZE_MEMBER
}

// We need to preserve the existing FrozenModules list, since it includes
// important importlib machinery. This code is adapted from the similar
// `PyImport_ExtendInittab`.
int extendFrozenModules(struct _frozen *newfrozen) {
    struct _frozen *p = NULL;
    size_t i, n;
    int res = 0;

    /* Count the number of entries in both tables */
    for (n = 0; newfrozen[n].name != NULL; n++)
        ;
    for (i = 0; PyImport_FrozenModules[i].name != NULL; i++)
        ;

    /* Allocate new memory for the combined table */
    if (i + n <= SIZE_MAX / sizeof(struct _frozen) - 1) {
        size_t size = sizeof(struct _frozen) * (i + n + 1);
        p = (_frozen*)PyMem_Realloc(p, size);
    }
    if (p == NULL) {
      return -1;
    }

    /* Copy the tables into the new memory */
    memcpy(p, PyImport_FrozenModules, (i+1) * sizeof(struct _frozen));
    memcpy(p + i, newfrozen, (n + 1) * sizeof(struct _frozen));
    PyImport_FrozenModules = p;
    return res;
}

// We need to register a custom finder because we are registering `torch._C` as
// a built-in module, and it will otherwise get skipped by the default importer.
const char* finder = R"RAW(
import sys
sys.meta_path = sys.meta_path[:-1]
class F:
    def find_spec(self, fullname, path, target=None):
        if fullname == 'torch._C':
            return sys.meta_path[1].find_spec('torch._C', None, None)
        return None
sys.meta_path.insert(0, F())

# make loader importable
)RAW";

const char* sysprint = R"RAW(
import sys
print("exec_prefix:", sys.base_exec_prefix)
print("_base_executable:", sys._base_executable)
print("base_prefix:", sys.base_prefix)
print("exec_prefix:", sys.exec_prefix)
print("executable:", sys.executable)
print("path:", sys.path)
print("prefix:", sys.prefix)

)RAW";

extern "C" PyObject* initModule(void);
extern struct _frozen _PyImport_FrozenModules[];

static std::atomic<size_t> s_id;
std::map<size_t, py::object> forwards;

__attribute__((constructor)) void init() {
  // some dependency in mkl requires this...
  void* result = dlopen("libz.so", RTLD_GLOBAL | RTLD_LAZY);
  assert(result);

#define APPEND_INIT(name) PyImport_AppendInittab(#name, PyInit_##name);
  FOREACH_LIBRARY(APPEND_INIT)
#undef APPEND_INIT
  PyImport_AppendInittab("torch._C", initModule);

  int ret = extendFrozenModules(_PyImport_FrozenModules);
  TORCH_INTERNAL_ASSERT(ret == 0);

  PyPreConfig preconfig;
  PyPreConfig_InitIsolatedConfig(&preconfig);
  PyStatus status = Py_PreInitialize(&preconfig);
  TORCH_INTERNAL_ASSERT(!PyStatus_Exception(status))

  PyConfig config;
  PyConfig_InitIsolatedConfig(&config);

  // Completely blank out the path configuration. This ensures we have complete
  // control of how our embedded Python searches for modules, and we will never
  // consult the external filesystem. See:
  // https://docs.python.org/3/c-api/init_config.html#path-configuration
  config.site_import = 0;
  status = PyConfig_SetString(&config, &config.base_exec_prefix, L"");
  status = PyConfig_SetString(&config, &config.base_executable, L"i_am_torchpy");
  status = PyConfig_SetString(&config, &config.base_prefix, L"");
  status = PyConfig_SetString(&config, &config.exec_prefix, L"");
  status = PyConfig_SetString(&config, &config.executable, L"i_am_torchpy");
  status = PyConfig_SetString(&config, &config.prefix, L"");
  config.module_search_paths_set = 1;
  wchar_t* module_search_paths[0] = {};
  status = PyConfig_SetWideStringList(
      &config, &config.module_search_paths, 0, module_search_paths);

  status = Py_InitializeFromConfig(&config);
  PyConfig_Clear(&config);
  TORCH_INTERNAL_ASSERT(!PyStatus_Exception(status))

  PyRun_SimpleString(sysprint);
  PyRun_SimpleString(finder);
  // Release the GIL that PyInitialize acquires
  PyEval_SaveThread();
}

static void startup() {
}

static void teardown() {
  PyGILState_Ensure();

  if (Py_FinalizeEx() < 0) {
    std::cout << "IT BROKE SO WE ARE EXITING\n";
    exit(120);
  }
  PyMem_RawFree(program);
}

__attribute__((destructor)) void deinit() {}

static void run_some_python(const char* code) {
  PyGILState_STATE gstate = PyGILState_Ensure();

  std::cout << "BEGIN\n";
  if (PyRun_SimpleString(code) == -1) {
    throw std::runtime_error("python eval failed\n");
  }
  std::cout << "END\n";
  PyGILState_Release(gstate);
}

static void run_python_file(const char* code) {
  PyGILState_STATE gstate = PyGILState_Ensure();

  FILE* f = fopen(code, "r");
  if (PyRun_SimpleFile(f, code) == -1) {
    throw std::runtime_error("python eval failed\n");
  }
  fclose(f);

  PyGILState_Release(gstate);
}


static size_t load_model(const char* filename, bool hermetic) {
  PyGILState_STATE gstate = PyGILState_Ensure();
  assert(PyGILState_Check() == 1);
  std::string code;

  if (hermetic) {
    code = fmt::format(R"(
from torch.hermetic import HermeticImporter

i = HermeticImporter('{}')
model = i.load_pickle('model', 'model.pkl')
)", filename);
  } else {
    code = std::string("model = torch.jit.load('") +
        std::string(filename) + std::string("')");
  }
    py::exec(code);

  auto id = ++s_id;

  PyGILState_Release(gstate);
  return id;
}

static at::Tensor forward_model(size_t model_id, at::Tensor input) {
  at::Tensor output;
  PyGILState_STATE gstate = PyGILState_Ensure();
  {
    assert(PyGILState_Check() == 1);
    auto forward = py::globals()["model"].attr("forward");

    py::object py_output = forward(input);
    // TODO is this going to leak?
    // added it to prevent crash wehn using 'output' tensor in callee of
    // forward()
    py_output.inc_ref();
    output = py::cast<at::Tensor>(py_output);
  }

  PyGILState_Release(gstate);

  return output;
  // return input;
}
