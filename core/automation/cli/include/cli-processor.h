#pragma once

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Session context for a client connection
 */
class ClientSession
{
public:
    ClientSession(int clientSocket) : _clientSocket(clientSocket), _selectedEmulatorId("") {}

    int GetSocket() const
    {
        return _clientSocket;
    }

    const std::string& GetSelectedEmulatorId() const
    {
        return _selectedEmulatorId;
    }
    void SetSelectedEmulatorId(const std::string& id)
    {
        _selectedEmulatorId = id;
    }

    // Send a response to the client
    void SendResponse(const std::string& message) const;

private:
    int _clientSocket;
    std::string _selectedEmulatorId;  // ID of the currently selected emulator
};

/**
 * @brief CLI processor that handles all command processing for a client session
 */
class CLIProcessor
{
public:
    // Define a member function pointer type for command handlers
    using CommandHandler = void (CLIProcessor::*)(const ClientSession&, const std::vector<std::string>&);

    CLIProcessor();
    virtual ~CLIProcessor() = default;

    // Process a command from a client
    void ProcessCommand(ClientSession& session, const std::string& command);

    // Initialize the processor without sending any response
    void InitializeProcessor();

    // Set the current emulator reference
    void SetEmulator(std::shared_ptr<Emulator> emulator)
    {
        _emulator = emulator;
    }

private:
    // Command handlers
    void HandleHelp(const ClientSession& session, const std::vector<std::string>& args);
    void HandleStatus(const ClientSession& session, const std::vector<std::string>& args);
    void HandleList(const ClientSession& session, const std::vector<std::string>& args);
    void HandleSelect(const ClientSession& session, const std::vector<std::string>& args);
    void HandleReset(const ClientSession& session, const std::vector<std::string>& args);
    void HandlePause(const ClientSession& session, const std::vector<std::string>& args);
    void HandleResume(const ClientSession& session, const std::vector<std::string>& args);
    void HandleStep(const ClientSession& session, const std::vector<std::string>& args);
    void HandleMemory(const ClientSession& session, const std::vector<std::string>& args);
    void HandleRegisters(const ClientSession& session, const std::vector<std::string>& args);
    void HandleBreakpoint(const ClientSession& session, const std::vector<std::string>& args);
    void HandleExit(const ClientSession& session, const std::vector<std::string>& args);
    void HandleDummy(const ClientSession& session, const std::vector<std::string>& args);

    // Helper method to get the currently selected emulator
    std::shared_ptr<Emulator> GetSelectedEmulator(const ClientSession& session);

    // Command map
    std::unordered_map<std::string, CommandHandler> _commandHandlers;

    // Reference to the current emulator (if any)
    std::shared_ptr<Emulator> _emulator;

    // Flag to track if this is the first command
    bool _isFirstCommand;
};
