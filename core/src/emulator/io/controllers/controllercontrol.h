#pragma once

/// @file controllercontrol.h
/// @brief The one place that implements the connector verbs for every surface, as MediaControl does for media: the
/// Qt dialog, WebAPI (and MCP through it), CLI, Lua and Python turn their input into a ControllerRequest and the
/// ControllerReply into their output. Selector resolution, device and option names, the automatic connector choice
/// and the error codes live here, so the surfaces cannot drift apart.
/// Design: docs/inprogress/2026-10-17-controller-manager/surfaces.md (§2 verbs, §3 selectors, §4 envelope).
///
/// Verbs: list, get, plug, unplug, options, targets, auto, input.
///
/// Example (what a surface does):
/// @code
///   ControllerRequest request{"plug", "auto", "sinclair-joystick", {{"port", "2"}}};
///   ControllerReply reply = ControllerControl(context).Execute(request);
///   http.status = reply.HttpStatus();  http.body = reply.ToJson();
/// @endcode

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "controllermanager.h"
#include "controllertypes.h"
#include "emulator/state/statenode.h"

class EmulatorContext;

namespace controllers
{

struct ControllerRequest
{
    std::string verb;
    std::string selector;  ///< a connector: id, alias, kind:index; "auto" for plug
    std::string device;    ///< plug, targets: the retro device kind (sinclair-joystick, ...)
    /// Options by name: port (1 / 2), shift (a flag), keys ("up:q,down:a,..."), async. A flag given without a value
    /// is an empty string and means true
    std::map<std::string, std::string> options;
};

struct ControllerReply
{
    ControllerResult result;
    std::string connector;  ///< the resolved connector id
    bool pending = false;   ///< the plug waits for the frame boundary (TTD records; async, or a sync timeout)
    uint64_t revision = 0;
    /// Verb-specific fields, merged into the envelope: "connectors", "info", "targets", "default", "refusal"
    StateNode body = StateNode::Object();

    /// The envelope every surface returns: ok, error, message, connector, pending, revision, then the body's fields
    StateNode ToValue() const;
    std::string ToJson() const;
    int HttpStatus() const
    {
        return ControllerErrorHttpStatus(result.error);
    }
};

class ControllerControl
{
public:
    /// How long a sync request waits for the frame boundary
    static constexpr uint32_t kSyncTimeoutMs = 5000;

    explicit ControllerControl(EmulatorContext* context);

    ControllerReply Execute(const ControllerRequest& request);

    static const std::vector<std::string>& Verbs();
    /// The options a verb accepts (help texts, the OpenAPI spec, tests)
    static const std::vector<std::string>& OptionsFor(const std::string& verb);

    /// One connector as the surfaces show it, with the controls it holds now
    static StateNode ConnectorValue(const Connector& connector, uint16_t state);
    /// The options of @p device from the request's map over @p base; an option the device does not take fails
    static ControllerResult ParseOptions(RetroDeviceKind device, const std::map<std::string, std::string>& options,
                                         OverlayOptions& base);

private:
    ControllerReply Run(const ControllerRequest& request);
    ControllerReply List();
    ControllerReply Get(const ControllerRequest& request);
    ControllerReply Plug(const ControllerRequest& request);
    ControllerReply Unplug(const ControllerRequest& request);
    ControllerReply Options(const ControllerRequest& request);
    ControllerReply Targets(const ControllerRequest& request);
    /// The port-read evidence and the decision drawn from it; apply starts the watch again
    ControllerReply Auto(const ControllerRequest& request);
    /// Live input on a connector: press / release / set the named controls, journaled like a host device's
    ControllerReply Input(const ControllerRequest& request);
    /// Sync: wait for a plug made while TTD records to land (async: report it pending)
    void Finish(ControllerReply& reply, const std::map<std::string, std::string>& options);

    /// Selector -> connector index; an unknown selector fails with the connector ids
    ControllerResult Resolve(const std::string& selector, size_t& index) const;
    /// The connectors that take @p device, in the order `plug auto` tries them, and the default (-1 = none)
    struct Target
    {
        size_t index = 0;
        std::string refusal;  ///< why a plug there would fail ("" = it would be accepted)
    };
    std::vector<Target> TargetsFor(RetroDeviceKind device, int& defaultTarget) const;

    ControllerManager* _manager = nullptr;
};

}  // namespace controllers
