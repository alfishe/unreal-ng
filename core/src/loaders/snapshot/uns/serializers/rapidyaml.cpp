// This file is used to instantiate single-header rapidyml for linking.
// It is not used in the project, but is required to link the project.

// ryml's default error handler
// to throw std::runtime_error instead of calling abort().
#define RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS

// Instantiate rapidyml for linking
#define RYML_SINGLE_HDR_DEFINE_NOW
#include "3rdparty/rapidyaml/ryml_all.hpp"