#pragma once

/// @file guicall.h
/// @brief The GUI control plane's methods for the surfaces that wait for the answer: the CLI `gui`, Lua and Python
/// gui_list() / gui_state() / gui_call() (gui-control-plane design §4.10). Over GuiPlane (guimethods.h), the answer as
/// a StateNode.

#include "guimethods.h"

#include "emulator/state/statenode.h"
#include "emulator/state/statenodejson.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gui_call
{
/// How long a call waits for its answer
inline constexpr int kWaitMs = 15000;

/// A result's JSON text as a StateNode; text that is not an object becomes a `failed` result
inline StateNode Parse(const std::string& resultJson)
{
    StateNode node;
    std::string error;
    if (!StateNodeFromJsonText(resultJson, node, &error) || node.kind != StateNode::Kind::Object)
        StateNodeFromJsonText(GuiPlane::Failure("failed", "the GUI answered no result: " + error), node);
    return node;
}

/// Calls and waits up to `waitMs` (the CLI, Lua and Python): the result as a StateNode; `timeout` when no answer
/// came (a late one is dropped). Never on the thread the program answers on
inline StateNode CallAndWait(const std::string& method, const std::string& target, const StateNode& params,
                             int waitMs = kWaitMs)
{
    struct Pending
    {
        std::mutex mutex;
        std::condition_variable answered;
        bool done = false;
        std::string result;
    };
    auto pending = std::make_shared<Pending>();
    const std::string paramsJson =
        params.kind == StateNode::Kind::Null ? std::string("{}") : StateNodeToJsonText(params);
    GuiPlane::Call(method, target, paramsJson, [pending](const std::string& resultJson) {
        {
            std::lock_guard<std::mutex> lock(pending->mutex);
            pending->done = true;
            pending->result = resultJson;
        }
        pending->answered.notify_all();
    });
    std::string result;
    {
        std::unique_lock<std::mutex> lock(pending->mutex);
        if (!pending->answered.wait_for(lock, std::chrono::milliseconds(waitMs), [&] { return pending->done; }))
            return Parse(GuiPlane::Failure("timeout", "no answer from the GUI in " + std::to_string(waitMs) + " ms"));
        result = pending->result;
    }
    return Parse(result);
}

/// The words after a method ("addr=0x6000", "on=true", "target=debugger/memory") as the call's target and
/// parameters: a whole number becomes an Int, true / false a Bool, the rest text (an address stays text: the
/// plane parses 0x / # / $ itself). False with `error` for a word without '='
inline bool ParseWords(const std::vector<std::string>& words, std::string& target, StateNode& params,
                       std::string& error)
{
    params = StateNode::Object();
    for (const std::string& word : words)
    {
        const size_t equals = word.find('=');
        if (equals == std::string::npos || equals == 0)
        {
            error = "'" + word + "': a parameter is name=value";
            return false;
        }
        const std::string name = word.substr(0, equals);
        const std::string value = word.substr(equals + 1);
        if (name == "target")
        {
            target = value;
            continue;
        }
        char* end = nullptr;
        const long long integer = std::strtoll(value.c_str(), &end, 10);
        if (!value.empty() && end != nullptr && *end == '\0')
            params[name] = static_cast<int64_t>(integer);
        else if (value == "true" || value == "false")
            params[name] = value == "true";
        else
            params[name] = value;
    }
    return true;
}
}  // namespace gui_call
