set(WIN32_BUILTIN )
if(WIN32)
    set(WIN32_BUILTIN BUILTIN)
endif()

set(PY3_BUILTIN )
set(PY3_ALWAYS_BUILTIN )
if(IS_PY3)
    set(PY3_BUILTIN BUILTIN)
    set(PY3_ALWAYS_BUILTIN ALWAYS_BUILTIN)
endif()

add_python_extension(array ${WIN32_BUILTIN} SOURCES arraymodule.c)
add_python_extension(audioop ${WIN32_BUILTIN} REQUIRES HAVE_LIBM SOURCES audioop.c LIBRARIES ${M_LIBRARIES})
add_python_extension(_bisect ${WIN32_BUILTIN} SOURCES _bisectmodule.c)
add_python_extension(cmath REQUIRES HAVE_LIBM ${WIN32_BUILTIN} SOURCES _math.c cmathmodule.c LIBRARIES ${M_LIBRARIES})
add_python_extension(_codecs_cn ${WIN32_BUILTIN} SOURCES cjkcodecs/_codecs_cn.c)
add_python_extension(_codecs_hk ${WIN32_BUILTIN} SOURCES cjkcodecs/_codecs_hk.c)
add_python_extension(_codecs_iso2022 ${WIN32_BUILTIN} SOURCES cjkcodecs/_codecs_iso2022.c)
add_python_extension(_codecs_jp ${WIN32_BUILTIN} SOURCES cjkcodecs/_codecs_jp.c)
add_python_extension(_codecs_kr ${WIN32_BUILTIN} SOURCES cjkcodecs/_codecs_kr.c)
add_python_extension(_codecs_tw ${WIN32_BUILTIN} SOURCES cjkcodecs/_codecs_tw.c)
add_python_extension(_collections ${WIN32_BUILTIN} ${PY3_BUILTIN} SOURCES _collectionsmodule.c) # Container types
add_python_extension(cPickle ${WIN32_BUILTIN} REQUIRES IS_PY2 SOURCES cPickle.c)
set(crypt2_NAME crypt)
set(crypt2_SOURCES cryptmodule.c)
set(crypt3_NAME _crypt)
set(crypt3_SOURCES _cryptmodule.c)
add_python_extension(${crypt${PY_VERSION_MAJOR}_NAME} REQUIRES HAVE_LIBCRYPT SOURCES ${crypt${PY_VERSION_MAJOR}_SOURCES} LIBRARIES ${HAVE_LIBCRYPT})
add_python_extension(cStringIO ${WIN32_BUILTIN} REQUIRES IS_PY2 SOURCES cStringIO.c)
add_python_extension(_csv ${WIN32_BUILTIN} SOURCES _csv.c)
add_python_extension(_ctypes_test NEVER_BUILTIN REQUIRES HAVE_LIBM SOURCES _ctypes/_ctypes_test.c LIBRARIES ${M_LIBRARIES})
set(datetime2_NAME datetime)
set(datetime2_SOURCES datetimemodule.c)
set(datetime3_NAME _datetime)
set(datetime3_SOURCES _datetimemodule.c)
if(PY_VERSION VERSION_EQUAL "3.2")
  list(APPEND datetime${PY_VERSION_MAJOR}_SOURCES _time.c)
elseif(PY_VERSION VERSION_LESS_EQUAL "3.1")
  list(APPEND datetime${PY_VERSION_MAJOR}_SOURCES timemodule.c)
endif()
add_python_extension(${datetime${PY_VERSION_MAJOR}_NAME} ${WIN32_BUILTIN} REQUIRES HAVE_LIBM SOURCES ${datetime${PY_VERSION_MAJOR}_SOURCES} LIBRARIES ${M_LIBRARIES})
#if(ENABLE_DATETIME AND CMAKE_C_COMPILER_ID MATCHES GNU)
#    set_property(SOURCE ${SRC_DIR}/Modules/datetimemodule.c PROPERTY COMPILE_FLAGS -Wno-unused-value)
#endif()
add_python_extension(_functools ${WIN32_BUILTIN} ${PY3_BUILTIN} SOURCES _functoolsmodule.c DEFINITIONS Py_BUILD_CORE) # Tools for working with functions and callable objects
add_python_extension(future_builtins ${WIN32_BUILTIN} REQUIRES IS_PY2 SOURCES future_builtins.c)
add_python_extension(_heapq ${WIN32_BUILTIN} SOURCES _heapqmodule.c)
add_python_extension(_hotshot ${WIN32_BUILTIN} REQUIRES IS_PY2 SOURCES _hotshot.c)

set(_io_SOURCES
    _io/_iomodule.c
    _io/bufferedio.c
    _io/bytesio.c
    _io/fileio.c
    _io/iobase.c
    _io/stringio.c
    _io/textio.c
    )
if(WIN32 AND EXISTS ${SRC_DIR}/Modules/_io/winconsoleio.c)
    list(APPEND _io_SOURCES
        ${SRC_DIR}/Modules/_io/winconsoleio.c
        )
endif()
add_python_extension(_io ${WIN32_BUILTIN} ${PY3_ALWAYS_BUILTIN} SOURCES ${_io_SOURCES} DEFINITIONS Py_BUILD_CORE)

add_python_extension(itertools ${WIN32_BUILTIN} ${PY3_BUILTIN} SOURCES itertoolsmodule.c) # Functions creating iterators for efficient looping
add_python_extension(_json ${WIN32_BUILTIN} SOURCES _json.c)
add_python_extension(_locale ${WIN32_BUILTIN} ${PY3_BUILTIN} SOURCES _localemodule.c) # access to ISO C locale support
add_python_extension(_lsprof ${WIN32_BUILTIN} SOURCES _lsprof.c rotatingtree.c)
add_python_extension(math ${WIN32_BUILTIN} REQUIRES HAVE_LIBM SOURCES _math.c mathmodule.c LIBRARIES ${M_LIBRARIES})
add_python_extension(mmap ${WIN32_BUILTIN} SOURCES mmapmodule.c)
add_python_extension(_multibytecodec ${WIN32_BUILTIN} SOURCES cjkcodecs/multibytecodec.c)
add_python_extension(operator ${WIN32_BUILTIN} REQUIRES IS_PY2 SOURCES operator.c)
add_python_extension(parser ${WIN32_BUILTIN} SOURCES parsermodule.c)
add_python_extension(_random ${WIN32_BUILTIN} SOURCES _randommodule.c)
add_python_extension(strop ${WIN32_BUILTIN} REQUIRES IS_PY2 SOURCES stropmodule.c)
add_python_extension(_struct ${WIN32_BUILTIN} SOURCES _struct.c)
add_python_extension(_testcapi NEVER_BUILTIN SOURCES _testcapimodule.c)
set(thread2_NAME thread)
set(thread3_NAME _thread)
set(thread2_SOURCES ${SRC_DIR}/Modules/threadmodule.c)
set(thread3_SOURCES ${SRC_DIR}/Modules/_threadmodule.c)
set(thread_REQUIRES)
if(PY_VERSION VERSION_LESS "3.7")
  list(APPEND thread_REQUIRES WITH_THREAD)
endif()
add_python_extension(${thread${PY_VERSION_MAJOR}_NAME} ALWAYS_BUILTIN REQUIRES ${thread_REQUIRES} SOURCES ${thread${PY_VERSION_MAJOR}_SOURCES} DEFINITIONS Py_BUILD_CORE)
set(time_SOURCES timemodule.c)
if(PY_VERSION VERSION_EQUAL "3.2")
  list(APPEND datetime${PY_VERSION_MAJOR}_SOURCES _time.c)
endif()
add_python_extension(time ${WIN32_BUILTIN} ${PY3_BUILTIN} REQUIRES HAVE_LIBM SOURCES ${time_SOURCES} DEFINITIONS Py_BUILD_CORE LIBRARIES ${M_LIBRARIES} ${TIMEMODULE_LIB})
add_python_extension(unicodedata SOURCES unicodedata.c)

# Python3
add_python_extension(atexit BUILTIN REQUIRES IS_PY3 SOURCES atexitmodule.c) # Register functions to be run at interpreter-shutdown
add_python_extension(_codecs BUILTIN SOURCES _codecsmodule.c) # access to the builtin codecs and codec registry
add_python_extension(faulthandler ALWAYS_BUILTIN REQUIRES IS_PY3 SOURCES faulthandler.c)
add_python_extension(_opcode ${WIN32_BUILTIN} REQUIRES IS_PY3 SOURCES _opcode.c)
add_python_extension(_operator BUILTIN REQUIRES IS_PY3 SOURCES _operator.c)
add_python_extension(_pickle ${WIN32_BUILTIN} REQUIRES IS_PY3 SOURCES _pickle.c)
add_python_extension(_sre BUILTIN SOURCES _sre.c) # Fredrik Lundh's new regular expressions
add_python_extension(_stat BUILTIN REQUIRES IS_PY3 SOURCES _stat.c) # stat.h interface
add_python_extension(_symtable BUILTIN SOURCES symtablemodule.c)
# Python PEP-3118 (buffer protocol) test module
add_python_extension(_testbuffer REQUIRES IS_PY3 SOURCES _testbuffer.c)
# Test loading multiple modules from one compiled file (http://bugs.python.org/issue16421)
add_python_extension(_testimportmultiple REQUIRES IS_PY3 SOURCES _testimportmultiple.c)
# Test multi-phase extension module init (PEP 489)
add_python_extension(_testmultiphase REQUIRES IS_PY3 SOURCES _testmultiphase.c)
# debug tool to trace memory blocks allocated by Python
add_python_extension(_tracemalloc ALWAYS_BUILTIN REQUIRES IS_PY3
    SOURCES ${SRC_DIR}/$<IF:$<VERSION_GREATER_EQUAL:${PY_VERSION},3.9>,Python,Modules>/hashtable.c _tracemalloc.c
)
add_python_extension(_weakref ALWAYS_BUILTIN SOURCES _weakref.c)
math(EXPR _limited_api_version "${PY_VERSION_MAJOR} * 100 + ${PY_VERSION_MINOR}")
add_python_extension(xxlimited REQUIRES IS_PY3 BUILD_TESTING
    SOURCES xxlimited.c
    DEFINITIONS Py_LIMITED_API=0x${_limited_api_version}0000
    NO_INSTALL
)
add_python_extension(xxsubtype BUILTIN SOURCES xxsubtype.c)
if(PY_VERSION VERSION_LESS "3.8")
# The zipimport module is always imported at startup. Having it as a
# builtin module avoids some bootstrapping problems and reduces overhead.
add_python_extension(zipimport ALWAYS_BUILTIN SOURCES zipimport.c DEFINITIONS Py_BUILD_CORE)
endif()

# Python 3.6
if(PY_VERSION VERSION_GREATER_EQUAL "3.6")
    # asyncio speedups
    add_python_extension(_asyncio SOURCES _asynciomodule.c)
    # blake module
    set(_blake2_SOURCES
        _blake2/blake2module.c
        _blake2/blake2b_impl.c
        _blake2/blake2s_impl.c
    )
    add_python_extension(_blake2 ${WIN32_BUILTIN} SOURCES ${_blake2_SOURCES})
    add_python_extension(_sha3 ${WIN32_BUILTIN} SOURCES _sha3/sha3module.c)
    add_python_extension(_testconsole ${WIN32_BUILTIN} REQUIRES WIN32 SOURCES ../PC/_testconsole.c)
endif()

# Python 3.7
if(PY_VERSION VERSION_GREATER_EQUAL "3.7")
    add_python_extension(_abc BUILTIN SOURCES _abc.c)
    add_python_extension(_contextvars ${WIN32_BUILTIN} SOURCES _contextvarsmodule.c)
    add_python_extension(_queue SOURCES _queuemodule.c)
    add_python_extension(_uuid REQUIRES UUID_LIBRARY "HAVE_UUID_H OR HAVE_UUID_UUID_H" SOURCES _uuidmodule.c LIBRARIES ${UUID_LIBRARY})
    add_python_extension(_xxtestfuzz SOURCES _xxtestfuzz/_xxtestfuzz.c
                                             _xxtestfuzz/fuzzer.c
    )
endif()

# Python 3.8
if(PY_VERSION VERSION_GREATER_EQUAL "3.8")
    add_python_extension(_testinternalcapi SOURCES _testinternalcapi.c DEFINITIONS "PY3_DLLNAME=\"python3$<$<CONFIG:Debug>:_d>\"")
endif()

# Python 3.9
if(PY_VERSION VERSION_GREATER_EQUAL "3.9")
    add_python_extension(_peg_parser ALWAYS_BUILTIN SOURCES _peg_parser.c)
endif()

# UNIX-only extensions
add_python_extension(fcntl REQUIRES UNIX SOURCES fcntlmodule.c)
add_python_extension(grp REQUIRES UNIX SOURCES grpmodule.c)

set(nis_REQUIRES UNIX HAVE_LIBNSL)
set(nis_LIBRARIES ${HAVE_LIBNSL})
set(nis_INCLUDEDIRS )
if(TIRPC_LIBRARY AND TIRPC_RPC_INCLUDE_PATH)
    # if rpc.h is provided by libtirpc (instead of being provided by glibc). See python/cpython#5137
    list(APPEND nis_LIBRARIES ${TIRPC_LIBRARY})
    list(APPEND nis_INCLUDEDIRS ${TIRPC_RPC_INCLUDE_PATH}/../)
    if(WITH_STATIC_DEPENDENCIES AND TIRPC_LIBRARY_STATIC_DEPENDENCY_REQUIRED)
        list(APPEND nis_REQUIRES TIRPC_STATIC_LIBRARIES)
        list(APPEND nis_LIBRARIES ${TIRPC_STATIC_LIBRARIES})
    endif()
endif()
add_python_extension(nis
    REQUIRES ${nis_REQUIRES}
    SOURCES nismodule.c
    LIBRARIES ${nis_LIBRARIES}
    INCLUDEDIRS ${nis_INCLUDEDIRS}
)

add_python_extension(posix REQUIRES UNIX ALWAYS_BUILTIN SOURCES posixmodule.c DEFINITIONS Py_BUILD_CORE)
add_python_extension(pwd REQUIRES UNIX BUILTIN SOURCES pwdmodule.c) # this is needed to find out the user's home dir if $HOME is not set
add_python_extension(resource REQUIRES UNIX SOURCES resource.c)
add_python_extension(spwd REQUIRES UNIX HAVE_GETSPNAM HAVE_GETSPENT SOURCES spwdmodule.c)
add_python_extension(syslog REQUIRES UNIX SOURCES syslogmodule.c)
add_python_extension(termios REQUIRES UNIX SOURCES termios.c)

# Python3: UNIX-only extensions
add_python_extension(errno BUILTIN UNIX SOURCES errnomodule.c)
add_python_extension(_posixsubprocess REQUIRES IS_PY3 UNIX SOURCES _posixsubprocess.c)

# MacOSX-only extensions
set(_scproxy2_SOURCES ${SRC_DIR}/Mac/Modules/_scproxy.c)
set(_scproxy3_SOURCES ${SRC_DIR}/Modules/_scproxy.c)
add_python_extension(_scproxy
    REQUIRES APPLE HAVE_LIBCOREFOUNDATION HAVE_LIBSYSTEMCONFIGURATION
    SOURCES ${_scproxy${PY_VERSION_MAJOR}_SOURCES}
    LIBRARIES ${HAVE_LIBSYSTEMCONFIGURATION} ${HAVE_LIBCOREFOUNDATION}
)

# Linux-only extensions
add_python_extension(linuxaudiodev REQUIRES LINUX IS_PY2 SOURCES linuxaudiodev.c)
add_python_extension(ossaudiodev REQUIRES LINUX SOURCES ossaudiodev.c)

# Python 3.8: UNIX-only extensions
if(PY_VERSION VERSION_GREATER_EQUAL "3.8")
    set(_posixshmem_LIBRARIES)
    if(HAVE_SHM_OPEN_IN_RT)
        list(APPEND _posixshmem_LIBRARIES rt)
    endif()
    add_python_extension(_posixshmem REQUIRES UNIX SOURCES _multiprocessing/posixshmem.c LIBRARIES ${_posixshmem_LIBRARIES})
endif()

# Windows-only extensions
add_python_extension(_msi
    REQUIRES WIN32
    SOURCES ${SRC_DIR}/PC/_msi.c
    LIBRARIES cabinet.lib msi.lib rpcrt4.lib
)
add_python_extension(msvcrt REQUIRES MSVC BUILTIN SOURCES ${SRC_DIR}/PC/msvcrtmodule.c)
add_python_extension(nt REQUIRES WIN32 ALWAYS_BUILTIN SOURCES posixmodule.c)
add_python_extension(_subprocess REQUIRES WIN32 IS_PY2 BUILTIN SOURCES ${SRC_DIR}/PC/_subprocess.c)

set(winreg2_NAME _winreg)
set(winreg3_NAME winreg)
add_python_extension(${winreg${PY_VERSION_MAJOR}_NAME} REQUIRES WIN32 BUILTIN SOURCES ${SRC_DIR}/PC/${winreg${PY_VERSION_MAJOR}_NAME}.c)

# Python3: Windows-only extensions
add_python_extension(_overlapped
    REQUIRES WIN32 IS_PY3
    SOURCES ${SRC_DIR}/Modules/overlapped.c
    LIBRARIES ws2_32 $<$<VERSION_GREATER_EQUAL:${PY_VERSION},3.9>:pathcch>
)
add_python_extension(_winapi REQUIRES WIN32 IS_PY3 BUILTIN SOURCES ${SRC_DIR}/Modules/_winapi.c)

set(HAS_DISTUTILS_FINDVS_MODULE_SRC 0)
set(module_src ${SRC_DIR}/PC/_findvs.cpp)
if(EXISTS ${module_src})
  set(HAS_DISTUTILS_FINDVS_MODULE_SRC 1)
endif()
add_python_extension(_distutils_findvs REQUIRES WIN32 IS_PY3 HAS_DISTUTILS_FINDVS_MODULE_SRC SOURCES ${module_src})

# Multiprocessing is different on unix and windows
if(UNIX)
    set(_multiprocessing2_SOURCES
        _multiprocessing/socket_connection.c
    )
    set(_multiprocessing3_SOURCES )
    if(HAVE_SEM_OPEN AND NOT POSIX_SEMAPHORES_NOT_ENABLED)
        list(APPEND _multiprocessing${PY_VERSION_MAJOR}_SOURCES
            _multiprocessing/semaphore.c
        )
    endif()
    set(_multiprocessing_REQUIRES)
    if(PY_VERSION VERSION_LESS "3.7")
      list(APPEND _multiprocessing_REQUIRES WITH_THREAD)
    endif()
    add_python_extension(_multiprocessing
        SOURCES _multiprocessing/multiprocessing.c
                ${_multiprocessing${PY_VERSION_MAJOR}_SOURCES}
        REQUIRES ${_multiprocessing_REQUIRES}
    )
elseif(WIN32)
    set(_multiprocessing2_SOURCES
        _multiprocessing/pipe_connection.c
        _multiprocessing/socket_connection.c
        _multiprocessing/win32_functions.c
    )
    set(_multiprocessing3_SOURCES )
    add_python_extension(_multiprocessing
        SOURCES _multiprocessing/multiprocessing.c
                _multiprocessing/semaphore.c
                ${_multiprocessing${PY_VERSION_MAJOR}_SOURCES}
        LIBRARIES ws2_32
    )
endif()

# Select needs winsock on Windows
if(WIN32)
    add_python_extension(select SOURCES selectmodule.c LIBRARIES ws2_32)
else()
    add_python_extension(select SOURCES selectmodule.c)
endif()

# Socket is different on Windows
set(_socket_SOURCES socketmodule.c)
if(WIN32)
    add_python_extension(_socket
        REQUIRES HAVE_LIBM
        SOURCES ${_socket_SOURCES}
        DEFINITIONS EAI_ADDRFAMILY
                    HAVE_SOCKADDR_STORAGE
                    HAVE_ADDRINFO
        LIBRARIES ws2_32 ${M_LIBRARIES} $<$<VERSION_GREATER_EQUAL:${PY_VERSION},3.8>:iphlpapi>
    )
else()
    if(PY_VERSION VERSION_GREATER_EQUAL "2.7.4" AND PY_VERSION VERSION_LESS "3")
    list(APPEND _socket_SOURCES timemodule.c)
    endif()
    add_python_extension(_socket
        REQUIRES HAVE_LIBM
        SOURCES ${_socket_SOURCES}
        LIBRARIES ${M_LIBRARIES}
    )
endif()

set(_libffi_system_dir ${CMAKE_SYSTEM_PROCESSOR})
if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "i686")
  set(_libffi_system_dir "x86")
elseif("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64" OR "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "amd64")
  set(_libffi_system_dir "x86")
elseif("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "arm")
  set(_libffi_system_dir "arm")
endif()
# Common ctypes sources
set(ctypes_COMMON_SOURCES
    _ctypes/_ctypes.c
    _ctypes/callbacks.c
    _ctypes/callproc.c
    _ctypes/stgdict.c
    _ctypes/cfield.c
)

if(
    (WIN32 AND PY_VERSION VERSION_GREATER_EQUAL "3.8")
    OR
    (LINUX AND PY_VERSION VERSION_GREATER_EQUAL "3.7")
  )
    # If installed, build ctypes using the system ffi
    # * on Windows for python >= 3.8
    # * on Linux for python >= 3.7
    if(NOT USE_SYSTEM_LibFFI)
        message(AUTHOR_WARNING "Disabling USE_SYSTEM_LibFFI option is *NOT* supported with Python >= 3.8 on windows and with Python >= 3.7 on Linux. Current version is ${PY_VERSION}")
    endif()
    add_python_extension(_ctypes
        REQUIRES LibFFI_INCLUDE_DIR LibFFI_LIBRARY
        SOURCES ${ctypes_COMMON_SOURCES}
        DEFINITIONS Py_BUILD_CORE_MODULE FFI_BUILDING
        INCLUDEDIRS ${LibFFI_INCLUDE_DIR}
        LIBRARIES ${LibFFI_LIBRARY}
    )
else()
# ctypes is different on Windows
if(WIN32)
    if(MINGW)
      add_python_extension(_ctypes
          SOURCES ${ctypes_COMMON_SOURCES}
                  _ctypes/libffi_mingw/closures.c
                  _ctypes/libffi_mingw/debug.c
                  _ctypes/libffi_mingw/prep_cif.c
                  _ctypes/libffi_mingw/raw_api.c
                  _ctypes/libffi_mingw/${_libffi_system_dir}/ffi.c
                  _ctypes/libffi_mingw/${_libffi_system_dir}/win32.S
          INCLUDEDIRS ${SRC_DIR}/Modules/_ctypes/libffi/src/${_libffi_system_dir}
                      ${SRC_DIR}/Modules/_ctypes/libffi/include
          DEFINITIONS X86_WIN32
        )
    else() # Visual Studio
      if(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
          enable_language(ASM_MASM)
          set(_ctype_arch_impl win64.asm)
      else()
          set(_ctype_arch_impl win32.c)
      endif()
      add_python_extension(_ctypes
          SOURCES ${ctypes_COMMON_SOURCES}
                  _ctypes/malloc_closure.c
                  _ctypes/libffi_msvc/prep_cif.c
                  _ctypes/libffi_msvc/ffi.c
                  _ctypes/libffi_msvc/${_ctype_arch_impl}
          REQUIRES BUILD_LIBPYTHON_SHARED
          INCLUDEDIRS ${SRC_DIR}/Modules/_ctypes/libffi_msvc
        )
    endif()
else()
    if(APPLE)
        add_python_extension(_ctypes
            SOURCES ${ctypes_COMMON_SOURCES}
                    _ctypes/malloc_closure.c
                    _ctypes/darwin/dlfcn_simple.c
                    _ctypes/libffi_osx/ffi.c
                    _ctypes/libffi_osx/x86/darwin64.S
                    _ctypes/libffi_osx/x86/x86-darwin.S
                    _ctypes/libffi_osx/x86/x86-ffi_darwin.c
                    _ctypes/libffi_osx/x86/x86-ffi64.c
            INCLUDEDIRS ${SRC_DIR}/Modules/_ctypes/libffi_osx/include
                        ${SRC_DIR}/Modules/_ctypes/darwin
            DEFINITIONS MACOSX
        )
    else()
        if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64")
          set(_libffi_system_extra_src
                _ctypes/libffi/src/${_libffi_system_dir}/ffi64.c
                _ctypes/libffi/src/${_libffi_system_dir}/unix64.S
                )
        elseif("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "i686")
          set(_libffi_system_extra_src
                _ctypes/libffi/src/${_libffi_system_dir}/win32.S
                )
        endif()
        # To facilitate an eventual contribution of the configuration
        # of fficonfig.h to the upstream project, corresponding tests
        # in ConfigureChecks.cmake are labeled using this convention:
        # * "libffi specific"
        # * "libffi and cpython"
        set(LIBFFI_VERSION "3.1")
        configure_file(
          ${PROJECT_SOURCE_DIR}/cmake/fficonfig.h.in
          ${INCLUDE_BUILD_DIR}/fficonfig.h
          )
        add_python_extension(_ctypes
            SOURCES ${ctypes_COMMON_SOURCES}
                    _ctypes/libffi/src/closures.c
                    _ctypes/libffi/src/prep_cif.c
                    _ctypes/libffi/src/${_libffi_system_dir}/ffi.c
                    _ctypes/libffi/src/${_libffi_system_dir}/sysv.S
                    ${_libffi_system_extra_src}
            INCLUDEDIRS ${SRC_DIR}/Modules/_ctypes/libffi/src/${_libffi_system_dir}
                        ${SRC_DIR}/Modules/_ctypes/libffi/include
                        ${INCLUDE_BUILD_DIR}      # For fficonfig.h
                        ${PROJECT_SOURCE_DIR}/cmake # For ffi.h
        )
    endif()
endif()
endif()

# Python3: _decimal
if(IS_PY3)

set(libmpdec_config_x64          CONFIG_64 ASM)
set(libmpdec_config_uint128      CONFIG_64 ANSI HAVE_UINT128_T)
set(libmpdec_config_ansi64       CONFIG_64 ANSI)
set(libmpdec_config_ppro         CONFIG_32 PPRO ASM)
set(libmpdec_config_ansi32       CONFIG_32 PPRO) # XXX Is ANSI needed here ?
set(libmpdec_config_ansi_legacy  CONFIG_32 ANSI LEGACY_COMPILER)
set(libmpdec_config_universal    UNIVERSAL)

if(USE_SYSTEM_LIBMPDEC)
    set(_decimal_REQUIRES REQUIRES LIBMPDEC_LIBRARIES)
    set(_decimal_EXTRA_SOURCES )
    set(_decimal_INCLUDEDIRS )
else()
    set(_decimal_REQUIRES )
    set(_decimal_EXTRA_SOURCES
        _decimal/libmpdec/basearith.c
        _decimal/libmpdec/constants.c
        _decimal/libmpdec/context.c
        _decimal/libmpdec/convolute.c
        _decimal/libmpdec/crt.c
        _decimal/libmpdec/difradix2.c
        _decimal/libmpdec/fnt.c
        _decimal/libmpdec/fourstep.c
        _decimal/libmpdec/io.c
        _decimal/libmpdec/mpdecimal.c
        _decimal/libmpdec/numbertheory.c
        _decimal/libmpdec/sixstep.c
        _decimal/libmpdec/transpose.c
    )
    if(PY_VERSION VERSION_GREATER_EQUAL "3.9")
        list(APPEND _decimal_EXTRA_SOURCES
            _decimal/libmpdec/mpalloc.c
        )
    else()
        list(APPEND _decimal_EXTRA_SOURCES
            _decimal/libmpdec/memory.c
        )
    endif()
    if(WIN32)
        if(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
            enable_language(ASM_MASM)
            list(APPEND _decimal_EXTRA_SOURCES
                _decimal/libmpdec/vcdiv64.asm
            )
        endif()
    endif()
    set(_decimal_INCLUDEDIRS INCLUDEDIRS ${SRC_DIR}/Modules/_decimal/libmpdec)
endif()
set(libmpdec_config )
set(_decimal_compile_flags )
if(APPLE)
    # Universal here means: build with the same options Python
    # was built with.
    set(libmpdec_config universal)
elseif(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
    if(HAVE_GCC_ASM_FOR_X64)
        set(libmpdec_config x64)
    elseif(HAVE_GCC_UINT128_T)
        set(libmpdec_config uint128)
    else()
        set(libmpdec_config ansi64)
    endif()
elseif(${CMAKE_SIZEOF_VOID_P} EQUAL 4)
    if(HAVE_GCC_ASM_FOR_X87 AND
        (CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
        AND NOT CMAKE_SYSTEM MATCHES SunOS
    )
        # solaris: problems with register allocation.
        # icc >= 11.0 works as well.
        set(libmpdec_config ppro)
        list(APPEND _decimal_compile_flags "-Wno-unknown-pragmas")
    else()
      set(libmpdec_config ansi32)
    endif()
else()
    message(FATAL_ERROR "extension_decimal: unsupported architecture")
endif()

# Workarounds for toolchain bugs:
if(HAVE_IPA_PURE_CONST_BUG)
    # Some versions of gcc miscompile inline asm:
    # http://gcc.gnu.org/bugzilla/show_bug.cgi?id=46491
    # http://gcc.gnu.org/ml/gcc/2010-11/msg00366.html
    list(APPEND _decimal_compile_flags "-fno-ipa-pure-const")
endif()
if(HAVE_GLIBC_MEMMOVE_BUG)
    # _FORTIFY_SOURCE wrappers for memmove and bcopy are incorrect:
    # http://sourceware.org/ml/libc-alpha/2010-12/msg00009.html
    # XXX Implement missing logic
#    undef_macros.append('_FORTIFY_SOURCE')
endif()

if(NOT WITH_THREAD AND PY_VERSION VERSION_LESS "3.7")
    # Faster version without thread local contexts:
    list(APPEND libmpdec_config_${libmpdec_config}
        WITHOUT_THREADS
    )
endif()

if(WIN32)
    list(APPEND libmpdec_config_${libmpdec_config}
        _CRT_SECURE_NO_WARNINGS MASM
    )
endif()

# Uncomment for extra functionality:
#list(APPEND libmpdec_config_${libmpdec_config} EXTRA_FUNCTIONALITY)

if(ENABLE_DECIMAL)
    message(STATUS "extension_decimal: libmpdec_config [${libmpdec_config}]")
endif()
add_python_extension(_decimal
    SOURCES
        _decimal/_decimal.c
        _decimal/docstrings.h
        ${_decimal_EXTRA_SOURCES}
    DEFINITIONS ${libmpdec_config_${libmpdec_config}}
    ${_decimal_REQUIRES}
    ${_decimal_LIBRARIES}
    ${_decimal_INCLUDEDIRS}
)
if(_decimal_compile_flags AND ENABLE_DECIMAL AND NOT BUILTIN_DECIMAL)
    set_target_properties(extension_decimal PROPERTIES COMPILE_FLAGS ${_decimal_compile_flags})
endif()

endif()

# Build expat using the system expat if it's installed, otherwise use the
# builtin version.
if(EXPAT_LIBRARIES AND EXPAT_INCLUDE_DIRS)
    add_python_extension(pyexpat
        SOURCES pyexpat.c
        LIBRARIES ${EXPAT_LIBRARIES}
        INCLUDEDIRS ${EXPAT_INCLUDE_DIRS}
    )
    add_python_extension(_elementtree
        SOURCES _elementtree.c
        LIBRARIES ${EXPAT_LIBRARIES}
        INCLUDEDIRS ${EXPAT_INCLUDE_DIRS}
    )
else()
    set(_pyexpat_definitions XML_STATIC)
    set(_pyexpat_SOURCES
        pyexpat.c
        expat/xmlparse.c
        expat/xmlrole.c
        expat/xmltok.c
        )
    if(UNIX)
        list(APPEND _pyexpat_definitions HAVE_EXPAT_CONFIG_H)
        # bpo-30947: Python uses best available entropy sources to
        # call XML_SetHashSalt(), expat entropy sources are not needed
        list(APPEND _pyexpat_definitions XML_POOR_ENTROPY)
    elseif(WIN32)
        list(APPEND _pyexpat_definitions COMPILED_FROM_DSP)
        if(EXISTS "${SRC_DIR}/Modules/expat/loadlibrary.c")
            list(APPEND _pyexpat_SOURCES "${SRC_DIR}/Modules/expat/loadlibrary.c")
        endif()
    endif()
    add_python_extension(pyexpat
        SOURCES ${_pyexpat_SOURCES}
        DEFINITIONS ${_pyexpat_definitions}
        INCLUDEDIRS ${SRC_DIR}/Modules/expat
    )
    set(_elementtree_definitions USE_PYEXPAT_CAPI)
    if(WIN32)
        list(APPEND _elementtree_definitions COMPILED_FROM_DSP)
    endif()
    add_python_extension(_elementtree
        SOURCES _elementtree.c
        DEFINITIONS ${_elementtree_definitions}
        INCLUDEDIRS ${SRC_DIR}/Modules/expat
    )
endif()

if(IS_PY2)

# If openssl is NOT available then build some other hash implementations on UNIX
set(HASH_NOT_AVAILABLE ON)
if(NOT WIN32 AND OPENSSL_LIBRARIES)
  set(HASH_NOT_AVAILABLE OFF)
endif()

add_python_extension(_md5 REQUIRES HASH_NOT_AVAILABLE ${WIN32_BUILTIN} SOURCES md5.c md5module.c)
add_python_extension(_sha REQUIRES HASH_NOT_AVAILABLE ${WIN32_BUILTIN} SOURCES shamodule.c)
add_python_extension(_sha256 REQUIRES HASH_NOT_AVAILABLE ${WIN32_BUILTIN} SOURCES sha256module.c)
add_python_extension(_sha512 REQUIRES HASH_NOT_AVAILABLE ${WIN32_BUILTIN} SOURCES sha512module.c)

else()

# We always compile these even when OpenSSL is available (issue #14693).
# It's harmless and the object code is tiny (40-50 KB per module,
# only loaded when actually used).
add_python_extension(_md5 ${WIN32_BUILTIN} SOURCES md5module.c)
add_python_extension(_sha1 ${WIN32_BUILTIN} SOURCES sha1module.c)
add_python_extension(_sha256 ${WIN32_BUILTIN} SOURCES sha256module.c)
add_python_extension(_sha512 ${WIN32_BUILTIN} SOURCES sha512module.c)

endif()


# Extensions that depend on other libraries
set(binascii_REQUIRES "")
set(binascii_DEFINITIONS "")
set(binascii_LIBRARIES "")
set(binascii_INCLUDEDIRS "")
if(ZLIB_LIBRARY)
  list(APPEND binascii_REQUIRES ZLIB_INCLUDE_DIR)
  list(APPEND binascii_DEFINITIONS USE_ZLIB_CRC32)
  list(APPEND binascii_LIBRARIES ${ZLIB_LIBRARY})
  list(APPEND binascii_INCLUDEDIRS ${ZLIB_INCLUDE_DIR})
endif()
add_python_extension(binascii
    REQUIRES ${binascii_REQUIRES}
    ${WIN32_BUILTIN}
    SOURCES binascii.c
    DEFINITIONS ${binascii_DEFINITIONS}
    LIBRARIES ${binascii_LIBRARIES}
    INCLUDEDIRS ${binascii_INCLUDEDIRS}
)
add_python_extension(_bsddb
    REQUIRES IS_PY2 DB_INCLUDE_PATH DB_LIBRARY
    SOURCES _bsddb.c
    LIBRARIES ${DB_LIBRARIES}
    INCLUDEDIRS ${DB_INCLUDE_PATH}
)
set(bz2_2_NAME bz2)
set(bz2_3_NAME _bz2)
add_python_extension(${bz2_${PY_VERSION_MAJOR}_NAME}
    REQUIRES BZIP2_LIBRARIES BZIP2_INCLUDE_DIR
    SOURCES ${bz2_${PY_VERSION_MAJOR}_NAME}module.c
    LIBRARIES ${BZIP2_LIBRARIES}
    INCLUDEDIRS ${BZIP2_INCLUDE_DIR}
)
set(curses_common_REQUIRES CURSES_LIBRARIES)
set(curses_common_LIBRARIES ${CURSES_LIBRARIES})
if(WITH_STATIC_DEPENDENCIES)
    list(APPEND curses_common_REQUIRES TINFO_LIBRARY GPM_LIBRARY)
    list(APPEND curses_common_LIBRARIES ${TINFO_LIBRARY} ${GPM_LIBRARY})
endif()
add_python_extension(_curses_panel
    REQUIRES ${curses_common_REQUIRES} PANEL_LIBRARIES "HAVE_PANEL_H OR HAVE_NCURSES_PANEL_H"
    SOURCES _curses_panel.c
    LIBRARIES ${curses_common_LIBRARIES} ${PANEL_LIBRARIES}
)
add_python_extension(_curses
    REQUIRES ${curses_common_REQUIRES}
    SOURCES _cursesmodule.c
    LIBRARIES ${curses_common_LIBRARIES}
)
set(dbm2_SOURCES dbmmodule.c)
set(dbm3_SOURCES _dbmmodule.c)
if(IS_PY3)
  set(dbm_name _dbm)
else()
  set(dbm_name dbm)
endif()
add_python_extension(${dbm_name}
    REQUIRES NDBM_TAG GDBM_LIBRARY GDBM_COMPAT_LIBRARY
    SOURCES ${dbm${PY_VERSION_MAJOR}_SOURCES}
    DEFINITIONS HAVE_${NDBM_TAG}_H
    LIBRARIES ${GDBM_LIBRARY} ${GDBM_COMPAT_LIBRARY}
    INCLUDEDIRS ${${NDBM_TAG}_INCLUDE_PATH}
)
set(gdbm2_SOURCES gdbmmodule.c)
set(gdbm3_SOURCES _gdbmmodule.c)
if(IS_PY3)
  set(gdbm_name _gdbm)
else()
  set(gdbm_name gdbm)
endif()
add_python_extension(${gdbm_name}
    REQUIRES GDBM_INCLUDE_PATH GDBM_LIBRARY GDBM_COMPAT_LIBRARY
    SOURCES ${gdbm${PY_VERSION_MAJOR}_SOURCES}
    DEFINITIONS HAVE_GDBM_DASH_NDBM_H
    LIBRARIES ${GDBM_LIBRARY}
    INCLUDEDIRS ${GDBM_INCLUDE_PATH}
)
add_python_extension(_hashlib
    REQUIRES OPENSSL_INCLUDE_DIR OPENSSL_LIBRARIES
    SOURCES _hashopenssl.c
    LIBRARIES ${OPENSSL_LIBRARIES}
    INCLUDEDIRS ${OPENSSL_INCLUDE_DIR}
)
if(ENABLE_HASHLIB AND CMAKE_C_COMPILER_ID MATCHES GNU)
    set_property(SOURCE ${SRC_DIR}/Modules/_hashopenssl.c PROPERTY COMPILE_FLAGS -Wno-deprecated-declarations)
endif()
# lzma module was introduced in Python 3.3
set(is_py33_or_greater 0)
if(IS_PY3 AND PY_VERSION_MINOR VERSION_GREATER 2)
  set(is_py33_or_greater 1)
endif()
add_python_extension(_lzma
    REQUIRES LZMA_INCLUDE_PATH LZMA_LIBRARY is_py33_or_greater
    SOURCES _lzmamodule.c
    DEFINITIONS MODULE_NAME="lzma" LZMA_API_STATIC=1
    INCLUDEDIRS ${LZMA_INCLUDE_PATH}
    LIBRARIES ${LZMA_LIBRARY}
)
add_python_extension(readline
    REQUIRES READLINE_INCLUDE_PATH READLINE_LIBRARY CURSES_LIBRARIES HAVE_READLINE_READLINE_H
    SOURCES readline.c
    LIBRARIES ${READLINE_LIBRARY} ${CURSES_LIBRARIES}
    INCLUDEDIRS ${READLINE_INCLUDE_PATH}
)
add_python_extension(_sqlite3
    REQUIRES SQLite3_INCLUDE_DIRS SQLite3_LIBRARIES
    SOURCES _sqlite/cache.c
            _sqlite/connection.c
            _sqlite/cursor.c
            _sqlite/microprotocols.c
            _sqlite/module.c
            _sqlite/prepare_protocol.c
            _sqlite/row.c
            _sqlite/statement.c
            _sqlite/util.c
    DEFINITIONS MODULE_NAME="sqlite3" SQLITE_OMIT_LOAD_EXTENSION=1
    INCLUDEDIRS ${SQLite3_INCLUDE_DIRS}
    LIBRARIES ${SQLite3_LIBRARIES}
)
if(ENABLE_SQLITE3 AND CMAKE_C_COMPILER_ID MATCHES GNU)
    set_property(SOURCE ${SRC_DIR}/Modules/_sqlite/module.c PROPERTY COMPILE_FLAGS -Wno-deprecated-declarations)
endif()
set(_ssl_LIBRARIES ${OPENSSL_LIBRARIES})
if(WIN32)
    list(APPEND _ssl_LIBRARIES Crypt32 ws2_32)
endif()
set(_ssl_SOURCES _ssl.c)
if(WIN32)
    list(APPEND _ssl_SOURCES ${OPENSSL_INCLUDE_DIR}/openssl/applink.c)
endif()
add_python_extension(_ssl
    REQUIRES OPENSSL_INCLUDE_DIR OPENSSL_LIBRARIES
    SOURCES ${_ssl_SOURCES}
    LIBRARIES ${_ssl_LIBRARIES}
    INCLUDEDIRS ${OPENSSL_INCLUDE_DIR}
)
set(_tkinter_REQUIRES TCL_LIBRARY TK_LIBRARY TCL_INCLUDE_PATH TK_INCLUDE_PATH)
set(_tkinter_LIBRARIES ${TCL_LIBRARY} ${TK_LIBRARY})
set(_tkinter_INCLUDEDIRS ${TCL_INCLUDE_PATH} ${TK_INCLUDE_PATH})
if(UNIX)
  list(APPEND _tkinter_REQUIRES X11_INCLUDE_DIR X11_LIBRARIES)
  list(APPEND _tkinter_LIBRARIES ${X11_LIBRARIES})
  list(APPEND _tkinter_INCLUDEDIRS ${X11_INCLUDE_DIR})
endif()
add_python_extension(_tkinter
    REQUIRES ${_tkinter_REQUIRES}
    SOURCES _tkinter.c tkappinit.c
    DEFINITIONS WITH_APPINIT=1
    LIBRARIES ${_tkinter_LIBRARIES}
    INCLUDEDIRS ${_tkinter_INCLUDEDIRS}
)
if(NOT USE_BUILTIN_ZLIB)
    add_python_extension(zlib
        REQUIRES ZLIB_LIBRARY ZLIB_INCLUDE_DIR
        SOURCES zlibmodule.c
        LIBRARIES ${ZLIB_LIBRARY}
        INCLUDEDIRS ${ZLIB_INCLUDE_DIR}
    )
else()
    add_python_extension(zlib BUILTIN
        REQUIRES
        SOURCES
            zlibmodule.c
            zlib/adler32.c
            zlib/compress.c
            zlib/crc32.c
            zlib/deflate.c
            zlib/infback.c
            zlib/inffast.c
            zlib/inflate.c
            zlib/inftrees.c
            zlib/trees.c
            zlib/uncompr.c
            zlib/zutil.c
        INCLUDEDIRS ${SRC_DIR}/Modules/zlib
    )
endif()

if(USE_LIBEDIT AND ENABLE_READLINE AND NOT BUILTIN_READLINE)
    set_target_properties(extension_readline PROPERTIES
        COMPILE_DEFINITIONS "USE_LIBEDIT")
endif()
