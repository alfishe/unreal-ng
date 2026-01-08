// CLI Instance Management Commands
// Extracted from cli-processor.cpp - 2026-01-08

#include "cli-processor.h"

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/notifications.h>
#include <emulator/platform.h>

#include <iostream>
#include <sstream>

// HandleStatus - lines 495-543
void CLIProcessor::HandleStatus(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string status;

    // Get all emulator instances from EmulatorManager
    auto* emulatorManager = EmulatorManager::GetInstance();
    auto emulatorIds = emulatorManager->GetEmulatorIds();

    if (emulatorIds.empty())
    {
        status = std::string("No emulator instances found") + NEWLINE;
    }
    else
    {
        status = std::string("Emulator Instances:") + NEWLINE;
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
                // Check both the session's selected ID and our local _emulator reference
                bool isSelected = (session.GetSelectedEmulatorId() == id) ||
                                  (_emulator && _emulator->GetId() == id && session.GetSelectedEmulatorId().empty());
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
            // Mark the currently selected emulator
            // Check both the session's selected ID and our local _emulator reference
            bool isSelected = (session.GetSelectedEmulatorId() == id) ||
                              (_emulator && _emulator->GetId() == id && session.GetSelectedEmulatorId().empty());
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
    std::string previousId = session.GetSelectedEmulatorId();

    // We have a valid ID at this point
    const_cast<ClientSession&>(session).SetSelectedEmulatorId(selectedId);

    // Also update our local reference to the emulator
    _emulator = emulatorManager->GetEmulator(selectedId);

    // Send notification about selection change
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    EmulatorSelectionPayload* payload = new EmulatorSelectionPayload(previousId, selectedId);
    messageCenter.Post(NC_EMULATOR_SELECTION_CHANGED, payload);

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

    // Resume the emulator - this will trigger MessageCenter notifications
    // that the GUI will respond to (enabling/disabling buttons)
    emulator->Resume();

    // Confirm to the user
    session.SendResponse("Emulation resumed. Use 'pause' to suspend execution.");
}

// HandleCreate - Create emulator without starting
void CLIProcessor::HandleCreate(const ClientSession& session, const std::vector<std::string>& args)
{
    auto* emulatorManager = EmulatorManager::GetInstance();
    
    if (!args.empty())
    {
        // create <model> - create emulator with specific model
        std::string modelName = args[0];
        auto emulator = emulatorManager->CreateEmulatorWithModel("", modelName);

        if (emulator)
        {
            // Auto-select only if this is the first emulator
            auto emulatorIds = emulatorManager->GetEmulatorIds();
            bool shouldAutoSelect = (emulatorIds.size() == 1);

            std::stringstream ss;
            ss << "Created emulator instance: " << emulator->GetId() << NEWLINE;
            ss << "Model: " << modelName << NEWLINE;
            ss << "State: initialized (not started)" << NEWLINE;

            if (shouldAutoSelect)
            {
                const_cast<ClientSession&>(session).SetSelectedEmulatorId(emulator->GetId());
                ss << "Auto-selected as current emulator" << NEWLINE;
            }

            ss << "Use 'resume' or 'start' command to begin emulation" << NEWLINE;
            session.SendResponse(ss.str());
        }
        else
        {
            std::stringstream ss;
            ss << "Error: Failed to create emulator with model '" << modelName << "'" << NEWLINE;
            ss << "Available models: ";

            auto models = emulatorManager->GetAvailableModels();
            for (size_t i = 0; i < models.size(); ++i)
            {
                if (i > 0)
                    ss << ", ";
                ss << models[i].ShortName;
            }
            ss << NEWLINE;

            session.SendResponse(ss.str());
        }
    }
    else
    {
        // create - create default emulator
        auto emulator = emulatorManager->CreateEmulator("", LoggerLevel::LogInfo);

        if (emulator)
        {
            // Auto-select only if this is the first emulator
            auto emulatorIds = emulatorManager->GetEmulatorIds();
            bool shouldAutoSelect = (emulatorIds.size() == 1);

            std::stringstream ss;
            ss << "Created emulator instance: " << emulator->GetId() << NEWLINE;
            ss << "Model: 48K (default)" << NEWLINE;
            ss << "State: initialized (not started)" << NEWLINE;

            if (shouldAutoSelect)
            {
                const_cast<ClientSession&>(session).SetSelectedEmulatorId(emulator->GetId());
                ss << "Auto-selected as current emulator" << NEWLINE;
            }

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
void CLIProcessor::HandleStart(const ClientSession& session, const std::vector<std::string>& args)
{
    if (!args.empty())
    {
        // start <model> - create emulator with specific model
        std::string modelName = args[0];

        // Create emulator with the specified model
        auto* emulatorManager = EmulatorManager::GetInstance();
        auto emulator = emulatorManager->CreateEmulatorWithModel("", modelName);

        if (emulator)
        {
            // Start the emulator
            bool startSuccess = emulatorManager->StartEmulatorAsync(emulator->GetId());

            // Auto-select only if this is the first emulator, otherwise keep current selection
            auto emulatorIds = emulatorManager->GetEmulatorIds();
            bool shouldAutoSelect = (emulatorIds.size() == 1);  // Only auto-select if this is the only emulator

            std::stringstream ss;
            if (startSuccess)
            {
                ss << "Started emulator instance: " << emulator->GetId() << NEWLINE;
                ss << "Model: " << modelName << NEWLINE;

                if (shouldAutoSelect)
                {
                    const_cast<ClientSession&>(session).SetSelectedEmulatorId(emulator->GetId());
                    ss << "Auto-selected as current emulator" << NEWLINE;
                }
            }
            else
            {
                ss << "Created emulator instance: " << emulator->GetId() << NEWLINE;
                ss << "Model: " << modelName << NEWLINE;
                ss << "Warning: Failed to start emulator automatically" << NEWLINE;

                if (shouldAutoSelect)
                {
                    const_cast<ClientSession&>(session).SetSelectedEmulatorId(emulator->GetId());
                    ss << "Auto-selected as current emulator" << NEWLINE;
                }
            }

            // Note: NC_EMULATOR_INSTANCE_CREATED notification is now automatically sent by EmulatorManager

            session.SendResponse(ss.str());
        }
        else
        {
            std::stringstream ss;
            ss << "Error: Failed to create emulator with model '" << modelName << "'" << NEWLINE;
            ss << "Use 'start' without arguments for default 48K, or specify a valid model name" << NEWLINE;
            ss << "Available models: ";

            // List available models
            auto models = emulatorManager->GetAvailableModels();
            for (size_t i = 0; i < models.size(); ++i)
            {
                if (i > 0)
                    ss << ", ";
                ss << models[i].ShortName;
            }
            ss << NEWLINE;

            session.SendResponse(ss.str());
        }
    }
    else
    {
        // start - create default emulator
        auto emulator = EmulatorManager::GetInstance()->CreateEmulator("", LoggerLevel::LogInfo);

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

                if (shouldAutoSelect)
                {
                    const_cast<ClientSession&>(session).SetSelectedEmulatorId(emulator->GetId());
                    ss << "Auto-selected as current emulator" << NEWLINE;
                }
            }
            else
            {
                ss << "Created emulator instance: " << emulator->GetId() << NEWLINE;
                ss << "Model: 48K (default)" << NEWLINE;
                ss << "Warning: Failed to start emulator automatically" << NEWLINE;

                if (shouldAutoSelect)
                {
                    const_cast<ClientSession&>(session).SetSelectedEmulatorId(emulator->GetId());
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
                // Check both the session's selected ID and our local _emulator reference (same logic as list command)
                bool wasSelected =
                    (session.GetSelectedEmulatorId() == actualId) ||
                    (_emulator && _emulator->GetId() == actualId && session.GetSelectedEmulatorId().empty());

                if (wasSelected)
                {
                    const_cast<ClientSession&>(session).SetSelectedEmulatorId("");
                    _emulator.reset();

                    // Auto-select the first remaining emulator (by creation time)
                    auto remainingIds = emulatorManager->GetEmulatorIds();
                    if (!remainingIds.empty())
                    {
                        const_cast<ClientSession&>(session).SetSelectedEmulatorId(remainingIds[0]);
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
        std::string currentSelected = session.GetSelectedEmulatorId();
        if (currentSelected != "none" &&
            std::find(emulatorIds.begin(), emulatorIds.end(), currentSelected) != emulatorIds.end())
        {
            const_cast<ClientSession&>(session).SetSelectedEmulatorId("none");
            // Also clear our cached emulator reference
            _emulator.reset();
            ss << "Cleared emulator selection" << NEWLINE;
        }

        session.SendResponse(ss.str());
    }
    else
    {
        // Check if targetId is a number (index)
        bool isIndex = true;
        int index = -1;
        try
        {
            index = std::stoi(targetId);
            if (index < 1)
                isIndex = false;  // Indices start from 1
        }
        catch (const std::exception&)
        {
            isIndex = false;
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
            // Check both the session's selected ID and our local _emulator reference (same logic as list command)
            bool wasSelected = (session.GetSelectedEmulatorId() == actualId) ||
                               (_emulator && _emulator->GetId() == actualId && session.GetSelectedEmulatorId().empty());

            if (wasSelected)
            {
                const_cast<ClientSession&>(session).SetSelectedEmulatorId("");
                // Also clear our cached emulator reference
                _emulator.reset();

                // Auto-select the first remaining emulator (by creation time)
                auto remainingIds = emulatorManager->GetEmulatorIds();
                if (!remainingIds.empty())
                {
                    const_cast<ClientSession&>(session).SetSelectedEmulatorId(remainingIds[0]);

                    // Send notification about selection change
                    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
                    EmulatorSelectionPayload* payload = new EmulatorSelectionPayload(actualId, remainingIds[0]);
                    messageCenter.Post(NC_EMULATOR_SELECTION_CHANGED, payload);

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

