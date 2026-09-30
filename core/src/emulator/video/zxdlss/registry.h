#pragma once

/// @file registry.h
/// @brief The algorithm registry. The built-in algorithms are registered
/// explicitly by registry() (a registerXxx() function per source file, listed
/// in registry.cpp): the core is a static archive, and the linker drops an
/// object file that registers itself from a static constructor because nothing
/// references it. Code outside the core (the verification tools' reference
/// implementation) may still add itself with
///   const Registration kRegistration("name", [] { return std::make_unique<MyAlgorithm>(); });

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "algorithm.h"

namespace zxdlss
{

using Factory = std::function<std::unique_ptr<Algorithm>()>;

std::map<std::string, Factory>& registry();

/// Built-in families (one per source file), called once by registry()
void registerModTpgw(std::map<std::string, Factory>& registry);

struct Registration
{
    Registration(const std::string& name, Factory factory) { registry()[name] = std::move(factory); }
};

}  // namespace zxdlss
