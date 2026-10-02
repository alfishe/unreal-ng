// CLI Instance Management Commands
// Extracted from cli-processor.cpp - 2026-01-08

#include <emulator/ports/models/sprinter/sprinterbios.h>
#include <iomanip>
#include "emulator/zxpoly/zxpolygroup.h"
#include "emulator/machinevariants.h"
#include <emulator/buildinfo.h>
#include <emulator/config.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/media/modelswitch.h>
#include <emulator/notifications.h>
#include <emulator/platform.h>
#include <emulator/ports/portdecoder.h>

#include <algorithm>
#include <iostream>
#include <sstream>

#include "cli-processor.h"
#include "automation.h"

#include <functional>
#include <optional>

namespace
{
/// Takes "--ram-power-on random|zero" out of the arguments of a command that
/// creates a machine (create, start, zxpoly start). False with a message when
/// the value is missing or not one of the two
bool TakeRamPowerOnOption(std::vector<std::string>& args, std::optional<RamPowerOn>& mode, std::string& error)
{
    for (size_t i = 0; i < args.size(); i++)
    {
        if (args[i] != "--ram-power-on")
            continue;
        RamPowerOn parsed = RamPowerOn::Random;
        if (i + 1 >= args.size() || !Config::ParseRamPowerOn(args[i + 1], parsed))
        {
            error = "--ram-power-on expects random or zero";
            return false;
        }
        mode = parsed;
        args.erase(args.begin() + static_cast<std::ptrdiff_t>(i), args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
        i--;
    }
    return true;
}

/// The create-time override for a parsed --ram-power-on (none when absent:
/// the model's unreal.ini decides)
std::function<void(CONFIG&)> RamPowerOnOverride(const std::optional<RamPowerOn>& mode)
{
    return mode ? Config::RamPowerOnOverride(*mode) : std::function<void(CONFIG&)>();
}

/// --sprinter-bios <3.04|3.06|3.07|file>, --fast-start 0|1, --accel-int-suspend 0|1 (a new SPRINTER's firmware
/// and start options, core SprinterBios - the WebAPI's "sprinter": {...}); removed from args
bool TakeSprinterOptions(std::vector<std::string>& args, std::function<void(CONFIG&)>& out, std::string& error)
{
    std::string bios, fastStart, intSuspend;
    for (size_t i = 0; i < args.size(); i++)
    {
        std::string* target = args[i] == "--sprinter-bios"        ? &bios
                              : args[i] == "--fast-start"          ? &fastStart
                              : args[i] == "--accel-int-suspend"   ? &intSuspend
                                                                   : nullptr;
        if (!target)
            continue;
        if (i + 1 >= args.size())
        {
            error = args[i] + " expects a value";
            return false;
        }
        *target = args[i + 1];
        args.erase(args.begin() + static_cast<std::ptrdiff_t>(i), args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
        i--;
    }
    if (bios.empty() && fastStart.empty() && intSuspend.empty())
        return true;
    SprinterBios::Options options;
    std::string path;
    if (!SprinterBios::OptionsFromStrings(bios, fastStart, intSuspend, "", options, error) ||
        (!options.bios.empty() && !SprinterBios::Resolve(options.bios, path, error)))
        return false;
    out = SprinterBios::CreateOverride(options);
    return true;
}

/// Both create-time overrides (either may be empty)
std::function<void(CONFIG&)> CreateOverride(const std::optional<RamPowerOn>& mode, const std::function<void(CONFIG&)>& sprinter)
{
    std::function<void(CONFIG&)> ram = RamPowerOnOverride(mode);
    if (!sprinter)
        return ram;
    if (!ram)
        return sprinter;
    return [ram, sprinter](CONFIG& config) {
        ram(config);
        sprinter(config);
    };
}
}  // namespace

// HandleStatus - lines 495-543
void CLIProcessor::HandleStatus(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string status;

    // Build fingerprint first (P0-4): attribute everything below to a
    // specific branch/commit. Mirrors the "server" block of
    // GET /api/v1/emulator/status.
    status = std::string("Build: v") + buildinfo::kVersion + " (" + buildinfo::kGitBranch + " @ " +
             buildinfo::kGitCommit + ", " + buildinfo::kBuildType + ")" + NEWLINE;

    // Get all emulator instances from EmulatorManager
    auto* emulatorManager = EmulatorManager::GetInstance();
    auto emulatorIds = emulatorManager->GetEmulatorIds();

    if (emulatorIds.empty())
    {
        status += std::string("No emulator instances found") + NEWLINE;
    }
    else
    {
        status += std::string("Emulator Instances:") + NEWLINE;
        status += std::string("==================") + NEWLINE;

        for (const auto& id : emulatorIds)
        {
            auto emulator = emulatorManager->GetEmulator(id);
            if (emulator)
            {
                status += "ID: " + id + NEWLINE;
                status += "Status: " + std::string(emulator->IsRunning() ? "Running" : "Stopped") + NEWLINE;
                status += "Debug: " + std::string(emulator->IsDebug() ? "On" : "Off") + NEWLINE;

                // Indicate if this is the currently selected emulator
                // Check the global selection from EmulatorManager
                std::string selectedId = emulatorManager->GetSelectedEmulatorId();
                bool isSelected = (selectedId == id) || (_emulator && _emulator->GetId() == id && selectedId.empty());
                if (isSelected)
                {
                    status += std::string("SELECTED") + NEWLINE;
                }

                status += std::string("------------------");
            }
        }

        // Add current active emulator status if available
        if (_emulator)
        {
            status += std::string(NEWLINE) + "Current CLI Emulator: " + _emulator->GetId() + NEWLINE;
            status += "Status: " + std::string(_emulator->IsRunning() ? "Running" : "Stopped");
        }
    }

    session.SendResponse(status);
}

void CLIProcessor::HandleVideowall(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.empty())
    {
        session.SendResponse(std::string("Usage: videowall singlesync <on|off> [emulator_id]") + NEWLINE);
        return;
    }

    std::string subcmd = args[0];
    if (subcmd == "singlesync")
    {
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Usage: videowall singlesync <on|off> [emulator_id]") + NEWLINE);
            return;
        }

        std::string mode = args[1];
        bool enable = (mode == "on" || mode == "1" || mode == "true");
        std::string targetId = args.size() > 2 ? args[2] : "";

        if (Automation::GetInstance().SetVideowallSingleSyncMode(enable, targetId))
        {
            session.SendResponse(std::string("Videowall single sync mode ") + (enable ? "enabled" : "disabled") + NEWLINE);
        }
        else
        {
            session.SendResponse(std::string("Failed to set videowall single sync mode.") + NEWLINE);
        }
    }
    else
    {
        session.SendResponse(std::string("Unknown videowall command: ") + subcmd + NEWLINE);
    }
}

// HandleList - lines 545-613
void CLIProcessor::HandleList(const ClientSession& session, const std::vector<std::string>& args)
{
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (!emulatorManager)
    {
        session.SendResponse(std::string("Error: Unable to access emulator manager."));
        return;
    }

    // Force a refresh of emulator instances by calling a method that updates the internal state
    auto mostRecent = emulatorManager->GetMostRecentEmulator();

    // Now get the updated list of emulator IDs
    auto emulatorIds = emulatorManager->GetEmulatorIds();

    if (emulatorIds.empty())
    {
        session.SendResponse(std::string("No emulator instances found."));
        return;
    }

    std::string response = std::string("Available emulator instances:") + NEWLINE;
    response += std::string("============================") + NEWLINE;

    // Display emulators with index, ID, and status
    for (size_t i = 0; i < emulatorIds.size(); ++i)
    {
        const auto& id = emulatorIds[i];
        auto emulator = emulatorManager->GetEmulator(id);

        if (emulator)
        {
            // Mark the selected emulator using global selection from EmulatorManager
            std::string selectedId = emulatorManager->GetSelectedEmulatorId();
            bool isSelected = (selectedId == id);
            std::string selectedMarker = isSelected ? "* " : "  ";

            response += selectedMarker + "[" + std::to_string(i + 1) + "] ";
            response += "ID: " + id;

            // Add symbolic name if available
            // Note: We'd need to add this to the Emulator class if it's not already there
            // response += " (" + emulator->GetSymbolicName() + ")";

            std::string status;
            if (emulator->IsPaused())
            {
                status = "Paused";
            }
            else if (emulator->IsRunning())
            {
                status = "Running";
            }
            else
            {
                status = "Stopped";
            }

            response += std::string(NEWLINE) + "     Status: " + status;
            response += std::string(NEWLINE) + "     Debug: " + std::string(emulator->IsDebug() ? "On" : "Off");
            response += NEWLINE;
        }
    }

    response += std::string(NEWLINE) + "Use 'select <index>' or 'select <id>' to choose an emulator.";

    session.SendResponse(response);
}

// HandleSelect - lines 615-782
void CLIProcessor::HandleSelect(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.empty())
    {
        session.SendResponse(std::string("Usage: select <index|id|name>") + NEWLINE);
        session.SendResponse("Use 'list' to see available emulators.");
        return;
    }

    const std::string& selector = args[0];
    auto* emulatorManager = EmulatorManager::GetInstance();

    // Force a refresh of emulator instances by calling a method that updates the internal state
    emulatorManager->GetMostRecentEmulator();

    // Now get the updated list of emulator IDs
    auto emulatorIds = emulatorManager->GetEmulatorIds();

    if (emulatorIds.empty())
    {
        session.SendResponse(std::string("No emulator instances available."));
        return;
    }

    std::string selectedId;

    // Try to interpret as an index first
    // IMPORTANT: Must validate entire string is numeric, not just prefix
    bool allDigits = !selector.empty() && std::all_of(selector.begin(), selector.end(), ::isdigit);

    if (allDigits)
    {
        try
        {
            int index = std::stoi(selector);
            if (index > 0 && index <= static_cast<int>(emulatorIds.size()))
            {
                // Convert to 0-based index
                size_t arrayIndex = static_cast<size_t>(index - 1);
                if (arrayIndex < emulatorIds.size())
                {
                    selectedId = emulatorIds[arrayIndex];
                }
                else
                {
                    session.SendResponse(std::string("Error: Index out of bounds"));
                    return;
                }
            }
            else
            {
                session.SendResponse(std::string("Invalid emulator index. Use 'list' to see available emulators."));
                return;
            }
        }
        catch (const std::exception&)
        {
            // Should not happen since we validated all digits, but handle gracefully
            session.SendResponse(std::string("Invalid emulator index. Use 'list' to see available emulators."));
            return;
        }
    }
    else
    {
        // Not a valid index, try as UUID or name

        // First check if it's a direct UUID match
        if (emulatorManager->HasEmulator(selector))
        {
            selectedId = selector;
        }
        else
        {
            // Try to find by partial ID or name match
            bool found = false;
            for (const auto& id : emulatorIds)
            {
                // Check if the selector is a substring of the ID
                if (id.find(selector) != std::string::npos)
                {
                    selectedId = id;
                    found = true;
                    break;
                }

                // TODO: If emulators have symbolic names, we could check those too
                // auto emulator = emulatorManager->GetEmulator(id);
                // if (emulator && emulator->GetSymbolicName().find(selector) != std::string::npos)
                // {
                //     selectedId = id;
                //     found = true;
                //     break;
                // }
            }

            if (!found)
            {
                session.SendResponse("No emulator found matching: " + selector + NEWLINE);
                session.SendResponse("Use 'list' to see available emulators.");
                return;
            }
        }
    }

    // Track the previous selection for the notification
    std::string previousId = emulatorManager->GetSelectedEmulatorId();

    // Update global selection in EmulatorManager (this sends notification automatically)
    bool success = emulatorManager->SetSelectedEmulatorId(selectedId);

    if (!success)
    {
        session.SendResponse("Error: Failed to select emulator: " + selectedId + std::string(NEWLINE));
        return;
    }

    // Also update our local reference to the emulator
    _emulator = emulatorManager->GetEmulator(selectedId);

    std::stringstream ss;
    ss << "Selected emulator: " << selectedId;
    if (_emulator)
    {
        ss << " (" + std::string(_emulator->IsRunning() ? "Running" : "Stopped") + ")";
    }

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleExit(const ClientSession& session, const std::vector<std::string>& args)
{
    std::stringstream ss;
    ss << "Goodbye!" << NEWLINE;
    session.SendResponse(ss.str());

    // Mark the session for closure - it will be closed after command processing
    const_cast<ClientSession&>(session).MarkForClosure();
}

void CLIProcessor::HandleDummy(const ClientSession& session, const std::vector<std::string>& args)
{
    // This is a silent command used for initialization
    // It doesn't send any response to the client
}

void CLIProcessor::InitializeProcessor()
{
    // Force initialization of the EmulatorManager
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (emulatorManager)
    {
        // Force a refresh of emulator instances
        auto mostRecent = emulatorManager->GetMostRecentEmulator();
        auto emulatorIds = emulatorManager->GetEmulatorIds();

        // Auto-select the first emulator if any exist
        if (!emulatorIds.empty())
        {
            // Use the most recent emulator if available, otherwise use the first one
            std::string selectedId;
            if (mostRecent)
            {
                selectedId = mostRecent->GetId();
            }
            else
            {
                selectedId = emulatorIds[0];
            }

            // Update our local reference to the emulator
            _emulator = emulatorManager->GetEmulator(selectedId);
        }

        // Reset the first command flag so that the first real command works properly
        _isFirstCommand = false;
    }
    else
    {
        std::cerr << "Failed to initialize EmulatorManager" << std::endl;
    }
}

// HandleReset - lines 784-804
void CLIProcessor::HandleReset(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string errorMessage;
    auto emulator = ResolveEmulator(session, args, errorMessage);

    if (!emulator)
    {
        if (!errorMessage.empty())
        {
            session.SendResponse(errorMessage);
        }
        else
        {
            session.SendResponse("No emulator selected. Use 'select <id>' or 'list' to see available emulators.");
        }
        return;
    }

    emulator->Reset();
    session.SendResponse("Emulator reset\n");
}

// HandleNmi - pulse the Z80 NMI line (accepted at the next instruction boundary)
void CLIProcessor::HandleNmi(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string errorMessage;
    auto emulator = ResolveEmulator(session, args, errorMessage);

    if (!emulator)
    {
        if (!errorMessage.empty())
        {
            session.SendResponse(errorMessage);
        }
        else
        {
            session.SendResponse("No emulator selected. Use 'select <id>' or 'list' to see available emulators.");
        }
        return;
    }

    emulator->RequestNMI();
    session.SendResponse("NMI requested\n");
}

// HandleMni - the Scorpion "magic button": page the Shadow Monitor, then NMI
// (non-Scorpion models fall back to a plain NMI)
void CLIProcessor::HandleMni(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string errorMessage;
    auto emulator = ResolveEmulator(session, args, errorMessage);

    if (!emulator)
    {
        if (!errorMessage.empty())
        {
            session.SendResponse(errorMessage);
        }
        else
        {
            session.SendResponse("No emulator selected. Use 'select <id>' or 'list' to see available emulators.");
        }
        return;
    }

    emulator->RequestMNI();
    session.SendResponse("MNI requested (magic button: NMI + service monitor)\n");
}

// HandleSwitch - the machine's front-panel switches: `switch` lists them, `switch turbo` reads one,
// `switch turbo on|off` flips it (through the TTD input journal, like a key)
void CLIProcessor::HandleSwitch(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string errorMessage;
    auto emulator = ResolveEmulator(session, {}, errorMessage);
    if (!emulator)
    {
        session.SendResponse(errorMessage.empty() ? "No emulator selected. Use 'select <id>' or 'list' to see available emulators.\n"
                                                  : errorMessage + "\n");
        return;
    }

    const FrontPanelSwitch all[] = {FrontPanelSwitch::Turbo, FrontPanelSwitch::Cpm};
    if (args.empty())
    {
        std::ostringstream oss;
        bool any = false;
        for (FrontPanelSwitch sw : all)
        {
            const int value = emulator->GetFrontPanelSwitch(sw);
            if (value < 0)
                continue;
            any = true;
            oss << FrontPanelSwitchName(sw) << ": " << (value ? "on" : "off") << NEWLINE;
        }
        session.SendResponse(any ? oss.str() : std::string("This machine has no front-panel switches\n"));
        return;
    }

    FrontPanelSwitch sw;
    if (!ParseFrontPanelSwitch(args[0], sw))
    {
        session.SendResponse("Unknown switch '" + args[0] + "'. Known: turbo, cpm\n");
        return;
    }
    if (emulator->GetFrontPanelSwitch(sw) < 0)
    {
        session.SendResponse(std::string("This machine has no ") + FrontPanelSwitchName(sw) + " switch\n");
        return;
    }
    if (args.size() >= 2)
    {
        const std::string& v = args[1];
        const bool on = v == "on" || v == "1" || v == "true";
        if (!on && v != "off" && v != "0" && v != "false")
        {
            session.SendResponse("Usage: switch " + std::string(FrontPanelSwitchName(sw)) + " [on|off]\n");
            return;
        }
        emulator->SetFrontPanelSwitch(sw, on);
    }
    session.SendResponse(std::string(FrontPanelSwitchName(sw)) + ": " + (emulator->GetFrontPanelSwitch(sw) ? "on" : "off") + "\n");
}

// HandlePause - lines 806-844
void CLIProcessor::HandlePause(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string errorMessage;
    auto emulator = ResolveEmulator(session, args, errorMessage);

    if (!emulator)
    {
        if (!errorMessage.empty())
        {
            session.SendResponse(errorMessage);
        }
        else
        {
            session.SendResponse("No emulator selected. Use 'select <id>' or 'list' to see available emulators.");
        }
        return;
    }

    // Check if the emulator is running
    if (!emulator->IsRunning())
    {
        session.SendResponse("Emulator is not running. Cannot pause.");
        return;
    }

    // Check if the emulator is already paused
    if (emulator->IsPaused())
    {
        session.SendResponse("Emulator is already paused.");
        return;
    }

    // Pause the emulator - this will trigger MessageCenter notifications
    // that the GUI will respond to (enabling/disabling buttons)
    emulator->Pause();

    // Confirm to the user
    session.SendResponse("Emulation paused. Use 'resume' to continue execution.");
}

// HandleResume - lines 846-877
void CLIProcessor::HandleResume(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string errorMessage;
    auto emulator = ResolveEmulator(session, args, errorMessage);

    if (!emulator)
    {
        if (!errorMessage.empty())
        {
            session.SendResponse(errorMessage);
        }
        else
        {
            session.SendResponse("No emulator selected. Use 'select <id>' or 'list' to see available emulators.");
        }
        return;
    }

    // Check if the emulator is already running
    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator is already running.");
        return;
    }

    // Check run-control claim (GDB TDD §3.3 / 1A.7.2)
    auto* ctx = emulator->GetContext();
    if (ctx && ctx->IsRunControlClaimed())
    {
        auto state = ctx->GetRunControlState();
        std::stringstream ss;
        ss << "Error: Run-control held by " << state.surfaceLabel << ". Use that surface to resume.";
        session.SendResponse(ss.str());
        return;
    }

    // Resume the emulator - this will trigger MessageCenter notifications
    // that the GUI will respond to (enabling/disabling buttons)
    emulator->Resume();

    // Confirm to the user
    session.SendResponse("Emulation resumed. Use 'pause' to suspend execution.");
}

// HandleCreate - Create emulator without starting
void CLIProcessor::HandleCreate(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    auto* emulatorManager = EmulatorManager::GetInstance();

    std::vector<std::string> args = rawArgs;
    std::optional<RamPowerOn> ramPowerOn;
    std::string optionError;
    std::function<void(CONFIG&)> sprinterOptions;
    if (!TakeRamPowerOnOption(args, ramPowerOn, optionError) || !TakeSprinterOptions(args, sprinterOptions, optionError))
    {
        session.SendResponse("Error: " + optionError + NEWLINE);
        return;
    }

    if (!args.empty())
    {
        // create <model> - create emulator with specific model
        std::string modelName = args[0];
        std::string createError;
        auto emulator = emulatorManager->CreateEmulatorWithModel("", modelName, LoggerLevel::LogWarning, &createError,
                                                                 CreateOverride(ramPowerOn, sprinterOptions));

        if (emulator)
        {
            std::stringstream ss;
            ss << "Created emulator instance: " << emulator->GetId() << NEWLINE;
            // Echo the RESOLVED machine, not the requested string, so a
            // misresolved model is visible immediately. Identity comes from
            // EmulatorManager::GetMachineIdentity - the same single source
            // the WebAPI responses are built from.
            MachineIdentity identity = EmulatorManager::GetMachineIdentity(*emulator);
            if (identity.Valid)
            {
                ss << "Model: " << identity.Model << " - " << identity.ModelFullName
                   << " (" << identity.RamKb << "KB)" << NEWLINE;
                if (!identity.Variant.empty())
                    ss << "Machine variant: " << identity.Variant << " - " << identity.VariantTitle << NEWLINE;
                ss << "Config folder: " << identity.ConfigFolder << NEWLINE;
                ss << "Power-on RAM: " << identity.RamPowerOn << NEWLINE;
                if (identity.HasVideoMode)
                {
                    ss << "Video mode: " << identity.VideoMode << NEWLINE;
                }
            }
            else
            {
                ss << "Model: " << modelName << NEWLINE;
            }
            ss << "State: initialized (not started)" << NEWLINE;
            ss << "Note: Instance will be auto-selected when started" << NEWLINE;

            ss << "Use 'resume' or 'start' command to begin emulation" << NEWLINE;
            session.SendResponse(ss.str());
        }
        else
        {
            std::stringstream ss;
            ss << "Error: Failed to create emulator with model '" << modelName << "'" << NEWLINE;
            if (!createError.empty())
            {
                ss << "Reason: " << createError << NEWLINE;
            }
            ss << "Available models: ";

            auto models = emulatorManager->GetAvailableModels();
            for (size_t i = 0; i < models.size(); ++i)
            {
                if (i > 0)
                    ss << ", ";
                // Guard against NULL ShortName
                ss << (models[i].ShortName ? models[i].ShortName : "<unknown>");
            }
            ss << NEWLINE;

            session.SendResponse(ss.str());
        }
    }
    else
    {
        // create - create default emulator
        auto emulator = emulatorManager->CreateEmulator("", LoggerLevel::LogInfo, CreateOverride(ramPowerOn, sprinterOptions));

        if (emulator)
        {
            std::stringstream ss;
            ss << "Created emulator instance: " << emulator->GetId() << NEWLINE;
            ss << "Model: 48K (default)" << NEWLINE;
            ss << "State: initialized (not started)" << NEWLINE;
            ss << "Note: Instance will be auto-selected when started" << NEWLINE;

            ss << "Use 'resume' or 'start' command to begin emulation" << NEWLINE;

            // Send notification
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            SimpleTextPayload* payload = new SimpleTextPayload(emulator->GetId());
            messageCenter.Post(NC_EMULATOR_INSTANCE_CREATED, payload);

            session.SendResponse(ss.str());
        }
        else
        {
            session.SendResponse("Error: Failed to create default emulator instance" + std::string(NEWLINE));
        }
    }
}

// HandleStart - lines 3390-3511
void CLIProcessor::HandleStart(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    std::vector<std::string> args = rawArgs;
    std::optional<RamPowerOn> ramPowerOn;
    std::string optionError;
    std::function<void(CONFIG&)> sprinterOptions;
    if (!TakeRamPowerOnOption(args, ramPowerOn, optionError) || !TakeSprinterOptions(args, sprinterOptions, optionError))
    {
        session.SendResponse("Error: " + optionError + NEWLINE);
        return;
    }

    if (!args.empty())
    {
        auto* emulatorManager = EmulatorManager::GetInstance();
        auto emulatorIds = emulatorManager->GetEmulatorIds();
        std::string arg = args[0];
        std::string targetId;
        bool isExistingEmulator = false;

        // Try to interpret as an index first
        // IMPORTANT: Must validate entire string is numeric, not just prefix
        bool allDigits = !arg.empty() && std::all_of(arg.begin(), arg.end(), ::isdigit);

        if (allDigits)
        {
            try
            {
                int index = std::stoi(arg);
                if (index > 0 && static_cast<size_t>(index) <= emulatorIds.size())
                {
                    targetId = emulatorIds[index - 1];  // Convert to 0-based
                    isExistingEmulator = true;
                }
            }
            catch (const std::exception&)
            {
                // Should not happen since we validated all digits
            }
        }

        // If not a valid index, check if it's a UUID
        if (!isExistingEmulator && emulatorManager->HasEmulator(arg))
        {
            targetId = arg;
            isExistingEmulator = true;
        }

        if (isExistingEmulator && ramPowerOn)
        {
            session.SendResponse("Error: --ram-power-on applies to a new machine, not to an existing one" +
                                 std::string(NEWLINE));
            return;
        }

        if (isExistingEmulator)
        {
            // Start existing emulator
            auto emulator = emulatorManager->GetEmulator(targetId);
            if (!emulator)
            {
                session.SendResponse("Error: Emulator not found: " + targetId + std::string(NEWLINE));
                return;
            }

            if (emulator->IsRunning())
            {
                session.SendResponse("Emulator is already running: " + targetId + std::string(NEWLINE));
                return;
            }

            bool startSuccess = emulatorManager->StartEmulatorAsync(targetId);
            std::stringstream ss;
            if (startSuccess)
            {
                ss << "Started emulator instance: " << targetId << NEWLINE;
            }
            else
            {
                ss << "Warning: Failed to start emulator: " << targetId << NEWLINE;
            }
            session.SendResponse(ss.str());
            return;
        }

        // Not an existing emulator - treat as model name and create new emulator
        std::string modelName = arg;
        std::string createError;
        auto emulator = emulatorManager->CreateEmulatorWithModel("", modelName, LoggerLevel::LogWarning, &createError,
                                                                 CreateOverride(ramPowerOn, sprinterOptions));

        if (emulator)
        {
            // Start the emulator
            bool startSuccess = emulatorManager->StartEmulatorAsync(emulator->GetId());

            // Auto-select only if this is the first emulator, otherwise keep current selection
            emulatorIds = emulatorManager->GetEmulatorIds();
            bool shouldAutoSelect = (emulatorIds.size() == 1);  // Only auto-select if this is the only emulator

            // Echo the RESOLVED machine, not the requested string (identity
            // comes from EmulatorManager::GetMachineIdentity - the same single
            // source the WebAPI responses are built from)
            MachineIdentity identity = EmulatorManager::GetMachineIdentity(*emulator);
            std::string resolvedModel = modelName;
            if (identity.Valid)
            {
                resolvedModel = identity.Model + " - " + identity.ModelFullName + " (" +
                                std::to_string(identity.RamKb) + "KB, config: " + identity.ConfigFolder +
                                ", power-on RAM: " + identity.RamPowerOn + ")";
            }

            std::stringstream ss;
            if (startSuccess)
            {
                ss << "Started emulator instance: " << emulator->GetId() << NEWLINE;
                ss << "Model: " << resolvedModel << NEWLINE;
                // Note: EmulatorManager handles auto-selection automatically
            }
            else
            {
                ss << "Created emulator instance: " << emulator->GetId() << NEWLINE;
                ss << "Model: " << resolvedModel << NEWLINE;
                ss << "Warning: Failed to start emulator automatically" << NEWLINE;
            }

            // Note: NC_EMULATOR_INSTANCE_CREATED notification is now automatically sent by EmulatorManager

            session.SendResponse(ss.str());
        }
        else
        {
            std::stringstream ss;
            ss << "Error: Failed to create emulator with model '" << modelName << "'" << NEWLINE;
            if (!createError.empty())
            {
                ss << "Reason: " << createError << NEWLINE;
            }
            ss << "Use 'start' without arguments for default 48K, or specify a valid model name" << NEWLINE;
            ss << "Available models: ";

            // List available models
            auto models = emulatorManager->GetAvailableModels();
            for (size_t i = 0; i < models.size(); ++i)
            {
                if (i > 0)
                    ss << ", ";
                // Guard against NULL ShortName
                ss << (models[i].ShortName ? models[i].ShortName : "<unknown>");
            }
            ss << NEWLINE;

            session.SendResponse(ss.str());
        }
    }
    else
    {
        // start - create default emulator
        auto emulator = EmulatorManager::GetInstance()->CreateEmulator("", LoggerLevel::LogInfo,
                                                                       CreateOverride(ramPowerOn, sprinterOptions));

        if (emulator)
        {
            // Start the emulator
            bool startSuccess = EmulatorManager::GetInstance()->StartEmulatorAsync(emulator->GetId());

            // Auto-select only if this is the first emulator, otherwise keep current selection
            auto* emulatorManager = EmulatorManager::GetInstance();
            auto emulatorIds = emulatorManager->GetEmulatorIds();
            bool shouldAutoSelect = (emulatorIds.size() == 1);  // Only auto-select if this is the only emulator

            std::stringstream ss;
            if (startSuccess)
            {
                ss << "Started emulator instance: " << emulator->GetId() << NEWLINE;
                ss << "Model: 48K (default)" << NEWLINE;
                // Note: EmulatorManager handles auto-selection automatically
            }
            else
            {
                ss << "Created emulator instance: " << emulator->GetId() << NEWLINE;
                ss << "Model: 48K (default)" << NEWLINE;
                ss << "Warning: Failed to start emulator automatically" << NEWLINE;

                if (shouldAutoSelect)
                {
                    // Use EmulatorManager to set selection (sends notification automatically)
                    emulatorManager->SetSelectedEmulatorId(emulator->GetId());
                    ss << "Auto-selected as current emulator" << NEWLINE;
                }
            }

            // Send notification about instance creation
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            SimpleTextPayload* payload = new SimpleTextPayload(emulator->GetId());
            messageCenter.Post(NC_EMULATOR_INSTANCE_CREATED, payload);

            session.SendResponse(ss.str());
        }
        else
        {
            session.SendResponse("Error: Failed to create default emulator instance" + std::string(NEWLINE));
        }
    }
}

// HandleStop - lines 3513-3712
void CLIProcessor::HandleStop(const ClientSession& session, const std::vector<std::string>& args)
{
    auto* emulatorManager = EmulatorManager::GetInstance();
    auto emulatorIds = emulatorManager->GetEmulatorIds();

    if (args.empty())
    {
        // If no arguments provided, check if there's exactly one emulator
        if (emulatorIds.size() == 1)
        {
            // Stop the single emulator directly
            std::string actualId = emulatorIds[0];

            if (emulatorManager->StopEmulator(actualId))
            {
                emulatorManager->RemoveEmulator(actualId);
                std::stringstream ss;
                ss << "Stopped emulator instance: " << actualId << NEWLINE;

                // Clear selection if it was pointing to the stopped emulator
                // Check both the global selection and our local _emulator reference
                std::string currentSelected = emulatorManager->GetSelectedEmulatorId();
                bool wasSelected = (currentSelected == actualId) ||
                                   (_emulator && _emulator->GetId() == actualId && currentSelected.empty());

                if (wasSelected)
                {
                    emulatorManager->SetSelectedEmulatorId("");
                    _emulator.reset();

                    // Auto-select the first remaining emulator (by creation time)
                    auto remainingIds = emulatorManager->GetEmulatorIds();
                    if (!remainingIds.empty())
                    {
                        emulatorManager->SetSelectedEmulatorId(remainingIds[0]);
                        ss << "Auto-selected first emulator: " << remainingIds[0] << NEWLINE;
                    }
                    else
                    {
                        ss << "Cleared emulator selection" << NEWLINE;
                    }
                }

                session.SendResponse(ss.str());
            }
            else
            {
                session.SendResponse("Error: Emulator instance '" + actualId + "' not found or could not be stopped" +
                                     std::string(NEWLINE));
            }
            return;
        }
        else if (emulatorIds.empty())
        {
            session.SendResponse("No emulators running." + std::string(NEWLINE));
            return;
        }
        else
        {
            session.SendResponse(
                "Usage: stop <emulator-id> | stop all | stop (stops single emulator if only one is running)" +
                std::string(NEWLINE));
            return;
        }
    }

    std::string targetId = args[0];

    if (targetId == "all")
    {
        // Stop all emulators
        auto* emulatorManager = EmulatorManager::GetInstance();
        auto emulatorIds = emulatorManager->GetEmulatorIds();
        size_t stoppedCount = 0;

        for (const auto& id : emulatorIds)
        {
            if (emulatorManager->StopEmulator(id))
            {
                // Remove from manager after stopping
                // Note: NC_EMULATOR_INSTANCE_DESTROYED notification is now automatically sent by EmulatorManager
                emulatorManager->RemoveEmulator(id);

                stoppedCount++;
            }
        }

        std::stringstream ss;
        ss << "Stopped " << stoppedCount << " emulator instance(s)" << NEWLINE;

        // Clear selection if it was pointing to a stopped emulator
        std::string currentSelected = emulatorManager->GetSelectedEmulatorId();
        if (!currentSelected.empty() &&
            std::find(emulatorIds.begin(), emulatorIds.end(), currentSelected) != emulatorIds.end())
        {
            emulatorManager->SetSelectedEmulatorId("");
            // Also clear our cached emulator reference
            _emulator.reset();
            ss << "Cleared emulator selection" << NEWLINE;
        }

        session.SendResponse(ss.str());
    }
    else
    {
        // Check if targetId is a number (index)
        // IMPORTANT: Must validate entire string is numeric, not just prefix
        // std::stoi("80c1a5ce-...") would succeed with value 80, incorrectly treating UUID as index
        bool isIndex = false;
        int index = -1;

        // First check if the string contains ONLY digits
        bool allDigits = !targetId.empty() && std::all_of(targetId.begin(), targetId.end(), ::isdigit);

        if (allDigits)
        {
            try
            {
                index = std::stoi(targetId);
                if (index >= 1)
                    isIndex = true;
            }
            catch (const std::exception&)
            {
                isIndex = false;
            }
        }

        std::string actualId = targetId;

        if (isIndex)
        {
            // Convert index to emulator ID
            auto* emulatorManager = EmulatorManager::GetInstance();
            auto emulatorIds = emulatorManager->GetEmulatorIds();

            if (index > 0 && static_cast<size_t>(index) <= emulatorIds.size())
            {
                actualId = emulatorIds[index - 1];  // Convert to 0-based index
            }
            else
            {
                std::stringstream ss;
                ss << "Error: Invalid index '" << index << "'. Valid range: 1-" << emulatorIds.size() << NEWLINE;
                ss << "Use 'list' to see available instances" << NEWLINE;
                session.SendResponse(ss.str());
                return;
            }
        }

        // Stop specific emulator
        auto* emulatorManager = EmulatorManager::GetInstance();
        if (emulatorManager->StopEmulator(actualId))
        {
            // Remove from manager after stopping
            // Note: NC_EMULATOR_INSTANCE_DESTROYED notification is now automatically sent by EmulatorManager
            emulatorManager->RemoveEmulator(actualId);

            std::stringstream ss;
            ss << "Stopped emulator instance: " << actualId << NEWLINE;

            // Clear selection if it was pointing to this emulator
            // Check both the global selection and our local _emulator reference
            std::string currentSelected = emulatorManager->GetSelectedEmulatorId();
            bool wasSelected = (currentSelected == actualId) ||
                               (_emulator && _emulator->GetId() == actualId && currentSelected.empty());

            if (wasSelected)
            {
                // Use EmulatorManager to clear selection (sends notification automatically)
                emulatorManager->SetSelectedEmulatorId("");
                // Also clear our cached emulator reference
                _emulator.reset();

                // Auto-select the first remaining emulator (by creation time)
                auto remainingIds = emulatorManager->GetEmulatorIds();
                if (!remainingIds.empty())
                {
                    // Use EmulatorManager to set selection (sends notification automatically)
                    emulatorManager->SetSelectedEmulatorId(remainingIds[0]);
                    ss << "Auto-selected first emulator: " << remainingIds[0] << NEWLINE;
                }
                else
                {
                    ss << "Cleared emulator selection" << NEWLINE;
                }
            }

            session.SendResponse(ss.str());
        }
        else
        {
            std::stringstream ss;
            if (isIndex)
            {
                ss << "Error: Could not stop emulator at index " << index << NEWLINE;
            }
            else
            {
                ss << "Error: Emulator instance '" << actualId << "' not found or could not be stopped" << NEWLINE;
            }
            ss << "Use 'list' to see available instances" << NEWLINE;
            session.SendResponse(ss.str());
        }
    }
}

// HandleOpen - File loading for emulator instances
void CLIProcessor::HandleOpen(const ClientSession& session, const std::vector<std::string>& args)
{
    // Get the MessageCenter instance
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

    if (args.empty())
    {
        // No filepath provided, send a message to open the file dialog
        session.SendResponse("Requesting file open dialog...\n");
        messageCenter.Post(NC_FILE_OPEN_REQUEST, nullptr, true);
    }
    else
    {
        // Filepath provided, check if it exists
        std::string filepath = args[0];

        // Send the filepath in the message payload using the existing SimpleTextPayload
        session.SendResponse("Requesting to open file: " + filepath);
        messageCenter.Post(NC_FILE_OPEN_REQUEST, new SimpleTextPayload(filepath), true);
    }
}

// HandleModels - List available ZX Spectrum models
void CLIProcessor::HandleModels(const ClientSession& session, const std::vector<std::string>& args)
{
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (!emulatorManager)
    {
        session.SendResponse("Error: EmulatorManager not available." + std::string(NEWLINE));
        return;
    }

    auto models = emulatorManager->GetAvailableModels();
    
    if (models.empty())
    {
        session.SendResponse("No models available." + std::string(NEWLINE));
        return;
    }

    std::stringstream ss;
    ss << "Available ZX Spectrum models:" << NEWLINE;
    ss << "=============================" << NEWLINE;
    
    for (size_t i = 0; i < models.size(); ++i)
    {
        const auto& model = models[i];
        ss << "  " << (model.ShortName ? model.ShortName : "<unknown>");
        if (model.FullName && model.FullName[0] != '\0')
        {
            ss << " - " << model.FullName;
        }
        // Mark machines this build cannot create (missing port decoder or
        // config folder) so users see it before typing 'start <model>'
        if (!Config::IsModelCreatable(model))
        {
            ss << " (not creatable on this build)";
        }
        ss << NEWLINE;
    }
    
    // ZX-Poly configurations: four synchronized instances of a base model
    ss << NEWLINE << "ZX-Poly configurations:" << NEWLINE;
    for (const ZXPolyGroup::Configuration& configuration : ZXPolyGroup::Configurations())
    {
        ss << "  " << configuration.name << " - " << configuration.title << " (4x " << configuration.baseModel << ")";
        const TMemModel* base = Config::FindModelByShortName(configuration.baseModel);
        if (base == nullptr || !Config::IsModelCreatable(*base))
            ss << " (not creatable on this build)";
        ss << NEWLINE;
    }

    // Machine variants: a base model with a fixed board, started by name
    ss << NEWLINE << "Machine variants:" << NEWLINE;
    for (const MachineVariant& variant : MachineVariants::All())
    {
        ss << "  " << variant.name << " - " << variant.title << " (" << variant.baseModel << ", " << variant.ramKb
           << "KB)";
        const TMemModel* base = Config::FindModelByShortName(variant.baseModel);
        std::string reason;
        if (base == nullptr || !Config::IsModelCreatable(*base))
            ss << " (not creatable on this build)";
        else if (!variant.supported(&reason))
            ss << " (" << reason << ")";
        ss << NEWLINE;
    }

    ss << NEWLINE << "Use 'start <model>' to create emulator with specific model.";
    session.SendResponse(ss.str());
}
/// zxpoly start <model> [file] [--ram-power-on random|zero] | zxpoly status [id|index]
/// ZX-Poly machines through the same EmulatorManager entry points the WebAPI,
/// MCP, Lua, Python and the Qt UI use (CreateZXPolyMachine, GetZXPolyGroup)
void CLIProcessor::HandleZXPoly(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    std::vector<std::string> args = rawArgs;
    std::optional<RamPowerOn> ramPowerOn;
    std::string optionError;
    if (!TakeRamPowerOnOption(args, ramPowerOn, optionError))
    {
        session.SendResponse("Error: " + optionError + NEWLINE);
        return;
    }

    auto* manager = EmulatorManager::GetInstance();
    const std::string sub = args.empty() ? std::string("status") : args[0];
    std::stringstream ss;

    if (sub == "start")
    {
        const std::string model = args.size() > 1 ? args[1] : std::string("PENTAGON");
        const std::string file = args.size() > 2 ? args[2] : std::string();
        std::string error;
        auto master = manager->CreateZXPolyMachine("", model, file, &error, RamPowerOnOverride(ramPowerOn));
        if (!master)
        {
            ss << "Error: cannot start ZX-Poly on " << model << ": " << error << NEWLINE;
            session.SendResponse(ss.str());
            return;
        }
        const bool started = manager->StartEmulatorAsync(master->GetId());
        manager->SetSelectedEmulatorId(master->GetId());
        ss << (started ? "Started" : "Created") << " ZX-Poly machine: " << master->GetId() << NEWLINE;
        ss << "Model: 4 x " << model << (file.empty() ? std::string() : ", media: " + file) << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    if (sub != "status")
    {
        ss << "Usage: zxpoly start <model> [file] [--ram-power-on random|zero] | zxpoly status [id|index]" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    const std::vector<std::string> rest(args.begin() + (args.empty() ? 0 : 1), args.end());
    std::string resolveError;
    auto emulator = ResolveEmulator(session, rest, resolveError);
    ZXPolyGroup* group = emulator ? manager->GetZXPolyGroup(emulator->GetId()) : nullptr;
    if (!group)
    {
        ss << "Error: " << (emulator ? std::string("the emulator is not a ZX-Poly machine") : resolveError) << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    const ZXPolyGroup::Status status = group->GetStatus();
    ss << "ZX-Poly machine " << status.memberIds[0] << NEWLINE;
    ss << "  #3D00: #" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
       << static_cast<int>(status.port3D00) << std::dec << "  locked: " << (status.locked ? "yes" : "no")
       << "  video mode: " << static_cast<int>(status.videoMode)
       << "  slaves: " << (status.locked ? "running (locked)" : (status.slavesRunning ? "running" : "waiting"))
       << NEWLINE;
    if (status.locked)
        ss << "  schedule: " << (status.pipelinedSlaves ? "pipelined (unlimited speed)"
                                 : status.parallelSlaves ? "parallel" : "sequential")
           << NEWLINE;
    for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
    {
        ss << "  CPU" << m << " " << status.memberIds[m] << "  R0-R3:";
        for (uint8_t value : status.registers[m])
            ss << " #" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(value)
               << std::dec;
        ss << NEWLINE;
    }
    ss << "  lockstep: "
       << (status.divergence.diverged
               ? "CPU" + std::to_string(status.divergence.module) + " diverged: " + status.divergence.what
               : std::string("ok"))
       << NEWLINE;
    session.SendResponse(ss.str());
}

// HandleModel - switch the selected emulator to another model; the media follow
void CLIProcessor::HandleModel(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.empty() || args[0] == "help")
    {
        std::stringstream ss;
        ss << "Usage: model <name> [--ram <kb>] [--stranded save|discard|keep] [--ram-power-on random|zero]" << NEWLINE
           << "Switch the selected emulator to another model (see 'models'). The machine state is lost; disks, tapes" << NEWLINE
           << "and cards go into the slot with the same id on the new machine, unsaved writes included. Media with" << NEWLINE
           << "unsaved writes the new model has no slot for need --stranded: save (into their files), discard, or" << NEWLINE
           << "keep (detached media on the new machine, see 'media list')." << NEWLINE
           << "--ram-power-on: RAM contents of the new machine (default: the current machine's mode)." << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    ModelSwitchRequest request;
    request.emulatorId = emulator->GetId();
    request.model = args[0];
    for (size_t i = 1; i < args.size(); i++)
    {
        const std::string& option = args[i];
        const bool hasValue = i + 1 < args.size();
        if (option == "--ram" && hasValue)
        {
            try
            {
                request.ramKb = static_cast<uint32_t>(std::stoul(args[++i]));
            }
            catch (const std::exception&)
            {
                session.SendResponse("Error: --ram '" + args[i] + "': expected KB" + NEWLINE);
                return;
            }
        }
        else if (option == "--ram-power-on" && hasValue)
        {
            RamPowerOn mode = RamPowerOn::Random;
            if (!Config::ParseRamPowerOn(args[++i], mode))
            {
                session.SendResponse("Error: --ram-power-on '" + args[i] + "': expected random or zero" + NEWLINE);
                return;
            }
            request.ramPowerOn = mode;
        }
        else if (option == "--stranded" && hasValue)
        {
            if (!ModelSwitch::ParseStranded(args[++i], request.stranded))
            {
                session.SendResponse("Error: --stranded '" + args[i] + "': expected save, discard or keep" + NEWLINE);
                return;
            }
        }
        else
        {
            session.SendResponse("Error: unknown option '" + option + "' (see 'model help')" + NEWLINE);
            return;
        }
    }

    // Nothing here may keep the old machine alive
    const bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    emulator.reset();
    _emulator.reset();

    const ModelSwitchResult switched = ModelSwitch::Run(request);
    std::stringstream ss;
    if (!switched.result.Ok())
    {
        ss << "Error: " << switched.result.message << NEWLINE;
        for (const SlotInfo& info : switched.stranded)
            ss << "  " << info.descriptor.id << ": " << info.source << " (" << info.changes << ")" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    if (wasRunning)
        EmulatorManager::GetInstance()->StartEmulatorAsync(switched.emulator->GetId());
    const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*switched.emulator);
    ss << "Switched to " << identity.Model << " - " << identity.ModelFullName << " (" << identity.RamKb
       << "KB, power-on RAM: " << identity.RamPowerOn << ")" << NEWLINE
       << "New emulator instance: " << switched.emulator->GetId() << NEWLINE;
    for (const std::string& line : switched.media.lines)
        ss << "  " << line << NEWLINE;
    session.SendResponse(ss.str());
}
