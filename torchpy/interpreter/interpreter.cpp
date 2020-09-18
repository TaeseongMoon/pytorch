#include <dlfcn.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <iostream>
#include "interpreter_impl.h"

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

// const char* finder = R"RAW(
// import sys
// class F:
//     def find_spec(self, fullname, path, target=None):
//         if fullname == 'torch._C':
//             return sys.meta_path[1].find_spec('torch._C', None, None)
//         return None
// sys.meta_path.insert(0, F())

// )RAW";

extern "C" PyObject* initModule(void);

__attribute__((constructor)) void init() {
  // some dependency in mkl requires this...
  void* result = dlopen("libz.so", RTLD_GLOBAL | RTLD_LAZY);
  assert(result);

  // std::cout << "INIT\n";
  program = Py_DecodeLocale("main", NULL);
  if (program == NULL) {
    fprintf(stderr, "Fatal error: cannot decode argv[0]\n");
    exit(1);
  }
  Py_SetProgramName(program);
#define APPEND_INIT(name) PyImport_AppendInittab(#name, PyInit_##name);
  FOREACH_LIBRARY(APPEND_INIT)
#undef APPEND_INIT
  // PyImport_AppendInittab("torch._C", initModule);
  Py_Initialize();

  // TODO get string from cmake?
  // TODO add pytorch include path back in
  PyRun_SimpleString(
      "import sys; sys.path = ['',"
      "'/data/users/whc/pytorch/torchpy/interpreter/cpython/lib/python37.zip',"
      "'/data/users/whc/pytorch/torchpy/interpreter/cpython/lib/python3.7',"
      "'/data/users/whc/pytorch/torchpy/interpreter/cpython/lib/lib-dynload',"
      "'/data/users/whc/pytorch/torchpy/interpreter/cpython/lib/site-packages']");
  // PyRun_SimpleString(finder);
  PyEval_ReleaseThread(PyThreadState_Get());
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

  if (PyRun_SimpleString(code) == -1) {
    throw std::runtime_error("python eval failed\n");
  }

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

extern "C" void initialize_interface(InterpreterImpl* s) {
#define INITIALIZE_MEMBER(func) s->func = func;
  FOREACH_INTERFACE_FUNCTION(INITIALIZE_MEMBER)
#undef INITIALIZE_MEMBER
}