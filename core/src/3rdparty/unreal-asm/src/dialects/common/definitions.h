#pragma once

// How often a program defines each name, shared by the backends that choose between a fixed and a redefinable name

#include <functional>
#include <map>
#include <optional>
#include <string>

#include "unrealasm/ir.h"

namespace unrealasm::dialects
{
/// The definitions of each label of the program: the branches of an IF exclude each other, so a name defined once in
/// each is defined once (IF x / N=$ / ELSE / N=#3D13 / ENDIF), the largest count of the branches; a definition in a
/// macro body counts twice (one at every call). An IF whose condition `decide` knows (given the condition and the
/// index of its line) counts its taken branch only (RAR's IFN fastCPIQ / MAXPOSL=$+1 / ENDIF and IF0 fastCPIQ /
/// MAXPOSL=$+1 / ENDIF define it once)
std::map<std::string, int> CountDefinitions(const ir::Program& program,
                                            const std::function<std::optional<bool>(const ir::Expr&, size_t)>& decide = {});
}  // namespace unrealasm::dialects
