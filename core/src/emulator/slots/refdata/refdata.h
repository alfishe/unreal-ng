#pragma once

/// @file refdata.h
/// @brief Accessors of the ZX-bus slot reference data collection (docs/inprogress/2026-10-03-zx-bus-slots/
/// reference-data.md §3). The tables themselves are constexpr arrays in sources.cpp, cards.cpp, machines.cpp,
/// adapters.cpp and exceptions.cpp: data only, adding a card is adding one table entry.

#include <span>
#include <string_view>

#include "emulator/slots/slottypes.h"

namespace slots::refdata
{

std::span<const SourceRef> Sources();
std::span<const CardDef> Cards();
std::span<const MachineDef> Machines();
std::span<const AdapterDef> Adapters();
std::span<const ExceptionDef> Exceptions();

/// The whole collection as one value for the plan engine
const Collection& All();

} // namespace slots::refdata
