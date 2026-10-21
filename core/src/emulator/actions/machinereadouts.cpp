#include "emulator/actions/machinereadouts.h"

#include "emulator/capture/recordingcontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/io/controllers/controllertypes.h"
#include "emulator/machinenames.h"
#include "emulator/media/recentfiles.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/rzx/rzxlauncher.h"

namespace actions
{

namespace
{
StateNode Previous(Emulator& emulator)
{
    StateNode node = StateNode::Object();
    EmulatorContext* context = emulator.GetContext();
    machines::MachineHistory::Entry previous;
    const std::string current = context != nullptr ? machines::NameOf(context->config) : std::string();
    if (machines::MachineHistory::Instance().Previous(current, previous))
    {
        node["model"] = previous.name;
        node["ram_kb"] = static_cast<int64_t>(previous.ramKb);
    }
    else
    {
        node["model"] = StateNode();
        node["ram_kb"] = StateNode();
    }
    node["current"] = current;
    return node;
}

StateNode Switches(Emulator& emulator)
{
    StateNode node = StateNode::Object();
    StateNode& list = node["switches"];
    list = StateNode::Array();
    for (FrontPanelSwitch sw : {FrontPanelSwitch::Turbo, FrontPanelSwitch::Cpm})
    {
        const int value = emulator.GetFrontPanelSwitch(sw);
        if (value < 0)
            continue;
        StateNode item = StateNode::Object();
        item["name"] = FrontPanelSwitchName(sw);
        item["on"] = value != 0;
        list.push(std::move(item));
    }
    return node;
}

StateNode Rzx(Emulator& emulator)
{
    const rzx::SessionStatus status = emulator.GetRzxStatus();
    StateNode node = StateNode::Object();
    node["loaded"] = status.loaded;
    node["active"] = status.active;
    if (!status.loaded)
        return node;
    const size_t slash = status.path.find_last_of("/\\");
    node["name"] = slash == std::string::npos ? status.path : status.path.substr(slash + 1);
    node["state"] = rzx::StateName(status.player.state);
    node["frame"] = static_cast<uint64_t>(status.player.frame);
    node["total_frames"] = static_cast<uint64_t>(status.player.totalFrames);
    node["progress"] = status.player.totalFrames ? static_cast<double>(status.player.frame) /
                                                       static_cast<double>(status.player.totalFrames)
                                                 : 0.0;
    node["desyncs"] = static_cast<uint64_t>(status.player.desyncs);
    return node;
}

StateNode Connectors(Emulator& emulator)
{
    StateNode node = StateNode::Object();
    StateNode& list = node["connectors"];
    list = StateNode::Array();
    EmulatorContext* context = emulator.GetContext();
    if (context == nullptr || context->pControllerManager == nullptr)
        return node;
    for (const controllers::Connector& connector : context->pControllerManager->Connectors())
    {
        StateNode item = StateNode::Object();
        item["id"] = connector.id;
        item["kind"] = controllers::ConnectorKindName(connector.kind);
        item["fixed"] = connector.fixed;
        item["device"] = connector.device == controllers::RetroDeviceKind::None
                             ? StateNode()
                             : StateNode(controllers::RetroDeviceKindName(connector.device));
        if (connector.device == controllers::RetroDeviceKind::SinclairJoystick)
            item["port"] = static_cast<unsigned>(connector.options.sinclairPort);
        list.push(std::move(item));
    }
    return node;
}
}  // namespace

const std::vector<std::string>& MachineReadoutNames()
{
    static const std::vector<std::string> names = {"machine.previous", "machine.switches", "media.recent",
                                                   "media.rzx",        "input.connectors", "cap.record"};
    return names;
}

StateNode ReadMachineReadout(Emulator& emulator, const std::string& name)
{
    if (name == "machine.previous")
        return Previous(emulator);
    if (name == "machine.switches")
        return Switches(emulator);
    if (name == "media.recent")
        return RecentFiles::Instance().ToValue();
    if (name == "media.rzx")
        return Rzx(emulator);
    if (name == "input.connectors")
        return Connectors(emulator);
    if (name == "cap.record")
        return capture::RecordingReadout(emulator);
    return {};
}

}  // namespace actions
