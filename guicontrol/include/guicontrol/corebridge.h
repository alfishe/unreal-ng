#pragma once

/// @file corebridge.h
/// @brief The plane's methods for a program's core (design §4.10): what the core's GuiPlane takes, two callbacks of
/// standard types, so the core includes nothing of the plane and the plane nothing of the core. The program joins
/// them before Automation::start():
///
///     auto methods = std::make_shared<GuiMethods>();
///     methods->catalogueJson = guicontrol::corebridge::CatalogueJson;
///     methods->call = guicontrol::corebridge::Call;
///     GuiPlane::Set(methods);
///
/// A call goes where `unreal-gui` sends it (route.h): to any GUI program on the machine, this one included, over the
/// hosts' local sockets. So the WebAPI port and the MCP of unreal-qt also reach unreal-debugger --connect and the
/// screen viewer, and the activity, the focus and the refusals are those of every other client.

#include <functional>
#include <string>

namespace guicontrol::corebridge
{

/// How long a call waits for the host's answer
inline constexpr int kCallTimeoutMs = 10000;

/// Every declared interface (CatalogueJson of interfaces.h) as text
std::string CatalogueJson();

/// Runs `method` ("list", "state", or a command) at `target` with the parameters (a JSON object as text) and answers
/// the result JSON ({ok, error?, message?, body?}) through `done`, once. From any thread: the work goes to a thread of
/// the global pool; on the application's thread (a script the GUI runs) it runs there, serving the own host meanwhile
void Call(const std::string& method, const std::string& target, const std::string& paramsJson,
          std::function<void(const std::string& resultJson)> done);

}  // namespace guicontrol::corebridge
