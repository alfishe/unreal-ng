#pragma once

/// @file registry.h
/// @brief Self-registration of algorithms: a file defining an algorithm adds
///   const Registration kRegistration("name", [] { return std::make_unique<MyAlgorithm>(); });
/// and createAlgorithm("name") finds it. New algorithms need no other change.

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "zxdlss/algorithm.h"

namespace zxdlss
{

using Factory = std::function<std::unique_ptr<Algorithm>()>;

std::map<std::string, Factory>& registry();

struct Registration
{
    Registration(const std::string& name, Factory factory) { registry()[name] = std::move(factory); }
};

}  // namespace zxdlss
