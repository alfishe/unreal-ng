#ifdef __MINGW32__
#include <Python.h>

extern "C"
{
    /**
     * @brief MinGW Debug ABI Stubs
     *
     * MSYS2 only provides Release builds of Python. When building Unreal-NG in Debug mode,
     * the Python headers (via pybind11) expect debug reference counting symbols to be
     * exported by the Python library. Since they are missing in the Release DLL, we
     * provide empty stubs here to satisfy the linker.
     */

    void _Py_INCREF_IncRefTotal(void)
    {
        // No-op in release library
    }

    void _Py_DECREF_DecRefTotal(void)
    {
        // No-op in release library
    }

    void _Py_NegativeRefcount(const char* filename, int lineno, PyObject* op)
    {
        // No-op in release library
    }
}
#endif
