// The one translation unit that compiles rapidyaml's definitions (single-header
// build, see the INSTRUCTIONS at the top of ryml_all.hpp). Built without the
// project PCH and with warnings relaxed (core/src/CMakeLists.txt): the vendored
// code is not ours to change.
#define RYML_SINGLE_HDR_DEFINE_NOW
#include "ryml_all.hpp"
