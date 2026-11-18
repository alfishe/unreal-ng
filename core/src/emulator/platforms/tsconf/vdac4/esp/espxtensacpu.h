#pragma once

/// @file espxtensacpu.h
/// @brief IEspCpu on the unreal-xtensa interpreter (core/src/3rdparty/unreal-xtensa).

#include <memory>

#include "espcpu.h"

namespace vdac4
{

std::unique_ptr<IEspCpu> CreateEspXtensaCpu();

}  // namespace vdac4
