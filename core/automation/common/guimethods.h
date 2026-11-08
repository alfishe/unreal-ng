#pragma once

/// @file guimethods.h
/// @brief The GUI control plane's methods in the core's API (docs/inprogress/2026-11-07-gui-control-plane §4.10).
/// Header-only, the standard library only: the WebAPI (REST, WebSocket calls) and the MCP tools use it; the CLI
/// `gui`, Lua and Python gui_*() wait through guicall.h.
///
/// A program with a core and a GUI control plane registers two callbacks before Automation::start(); the core knows
/// no GUI command and links nothing of the plane: it hands a call over and passes the answer back. Without them
/// (headless, tests, a program without the plane) every call answers `no_gui`.
///
/// The answer is the plane's `result` without op / rid: {"ok": true, "body": {...}} or {"ok": false, "error":
/// "not_found" | "bad_request" | "ambiguous" | "not_now" | "failed" | "timeout", "message": "..."}. The methods
/// `list` (every GUI program on the machine) and `state` (a node's state; params {"target"}) are the plane's own.

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

/// What the program registers. Both are called from any thread, never the emulation thread's work
struct GuiMethods
{
    /// Answers the call once, from any thread: the result JSON (see the file comment)
    using Done = std::function<void(const std::string& resultJson)>;

    /// The interfaces as JSON text: format "unreal-ng-gui-catalogue", interfaces[{name, level, description, state[],
    /// commands[{method, label, description, repeats, params[{name, kind, optional, description}]}]}]
    std::function<std::string()> catalogueJson;
    /// Runs `method` at `target` ("" = the plane chooses) with the parameters (a JSON object as text). Must not
    /// wait for the GUI: the answer comes through `done`
    std::function<void(const std::string& method, const std::string& target, const std::string& paramsJson,
                       Done done)>
        call;
};

class GuiPlane
{
public:
    /// The error of every call without registered methods
    static constexpr const char* kNoGui = "no_gui";

    /// Registered by the program before Automation::start() (the MCP tools are made from the catalogue then);
    /// null withdraws them. A call in flight keeps the methods it started with alive
    static void Set(std::shared_ptr<const GuiMethods> methods)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        Slot() = std::move(methods);
    }

    static std::shared_ptr<const GuiMethods> Get()
    {
        std::lock_guard<std::mutex> lock(Mutex());
        return Slot();
    }

    /// The catalogue's JSON text; "" without methods
    static std::string Catalogue()
    {
        const std::shared_ptr<const GuiMethods> methods = Get();
        return methods && methods->catalogueJson ? methods->catalogueJson() : std::string();
    }

    /// Hands the call over; without methods `done` answers no_gui at once
    static void Call(const std::string& method, const std::string& target, const std::string& paramsJson,
                     GuiMethods::Done done)
    {
        const std::shared_ptr<const GuiMethods> methods = Get();
        if (!methods || !methods->call)
        {
            done(Failure(kNoGui, "this program has no GUI control plane"));
            return;
        }
        methods->call(method, target, paramsJson.empty() ? std::string("{}") : paramsJson, std::move(done));
    }

    /// {"ok": false, "error", "message"} as JSON text
    static std::string Failure(const std::string& error, const std::string& message)
    {
        return "{\"ok\":false,\"error\":" + Quote(error) + ",\"message\":" + Quote(message) + "}";
    }

    /// A JSON string literal
    static std::string Quote(const std::string& text)
    {
        static const char* kHex = "0123456789abcdef";
        std::string out = "\"";
        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (c == '"' || c == '\\')
            {
                out += '\\';
                out += c;
            }
            else if (u < 0x20)
            {
                out += "\\u00";
                out += kHex[u >> 4];
                out += kHex[u & 0x0F];
            }
            else
                out += c;
        }
        return out + "\"";
    }

private:
    static std::mutex& Mutex()
    {
        static std::mutex mutex;
        return mutex;
    }
    static std::shared_ptr<const GuiMethods>& Slot()
    {
        static std::shared_ptr<const GuiMethods> methods;
        return methods;
    }
};
