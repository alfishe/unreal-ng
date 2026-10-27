#pragma once

/// @file slotmatrix.h
/// @brief Renders the readable compatibility matrix (docs/inprogress/2026-10-03-zx-bus-slots/compatibility-matrix.md
/// §1-§4) from the reference data collection, the outcome cells by running the plan engine. The committed document
/// carries each table between `<!-- slots:generated:<name>:begin -->` / `<!-- slots:generated:<name>:end -->`
/// markers; SlotMatrix_Test.MatrixMatchesDocs fails when they drift apart.

#include <span>
#include <string>
#include <string_view>

#include "emulator/slots/refdata/refdata.h"

namespace slots
{

/// The generated tables in document order: "functions" (§1), "cards" (§2), "card-x-card" (§3), "machines" and
/// "card-x-machine" (§4)
std::span<const std::string_view> MatrixTableNames();

/// One table as markdown, ending with a newline; "" for an unknown name
std::string RenderMatrixTable(std::string_view name, const Collection& collection = refdata::All());

/// The begin / end markers of a table in the document
std::string MatrixBeginMarker(std::string_view name);
std::string MatrixEndMarker(std::string_view name);

} // namespace slots
