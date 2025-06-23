#pragma once

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "platform-sockets.h"

/**
 * @brief Session context for a client connection
 */
class ClientSession
{
public:
    ClientSession(SOCKET clientSocket) : _clientSocket(clientSocket), _selectedEmulatorId("") {}

    SOCKET GetSocket() const
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
    SOCKET _clientSocket;
    std::string _selectedEmulatorId;  // ID of the currently selected emulator
};

/**
 * @brief CLI processor that handles all command processing for a client session
 */
class CLIProcessor
{
public:
    // Newline constant (using CRLF for all platforms)
        static constexpr const char* NEWLINE = "\r\n";
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
    void HandleStepIn(const ClientSession& session, const std::vector<std::string>& args);
    void HandleSteps(const ClientSession& session, const std::vector<std::string>& args);
    void HandleStepOver(const ClientSession& session, const std::vector<std::string>& args);
    void HandleMemory(const ClientSession& session, const std::vector<std::string>& args);
    void HandleRegisters(const ClientSession& session, const std::vector<std::string>& args);
    
    // Breakpoint command handlers
    void HandleBreakpoint(const ClientSession& session, const std::vector<std::string>& args);
    void HandleBPList(const ClientSession& session, const std::vector<std::string>& args);
    void HandleWatchpoint(const ClientSession& session, const std::vector<std::string>& args);
    void HandlePortBreakpoint(const ClientSession& session, const std::vector<std::string>& args);
    void HandleBPClear(const ClientSession& session, const std::vector<std::string>& args);
    void HandleBPGroup(const ClientSession& session, const std::vector<std::string>& args);
    void HandleBPActivate(const ClientSession& session, const std::vector<std::string>& args);
    void HandleBPDeactivate(const ClientSession& session, const std::vector<std::string>& args);
    
    void HandleOpen(const ClientSession& session, const std::vector<std::string>& args);
    void HandleExit(const ClientSession& session, const std::vector<std::string>& args);
    void HandleDummy(const ClientSession& session, const std::vector<std::string>& args);
    void HandleDebugMode(const ClientSession& session, const std::vector<std::string>& args);
    void HandleMemCounters(const ClientSession& session, const std::vector<std::string>& args);
    void HandleCallTrace(const ClientSession& session, const std::vector<std::string>& args);
    void HandleFeature(const ClientSession& session, const std::vector<std::string>& args);

    // Helper method to get the currently selected emulator
    std::shared_ptr<Emulator> GetSelectedEmulator(const ClientSession& session);
    
    // Universal address parsing method for memory addresses and port numbers
    // Returns true if parsing was successful, false otherwise
    // The parsed address is stored in the 'result' parameter
    // maxValue specifies the maximum allowed value (default: 0xFFFF for 16-bit addresses)
    bool ParseAddress(const std::string& addressStr, uint16_t& result, uint16_t maxValue = 0xFFFF) const;
    
    // Helper method to notify UI components that breakpoints have changed
    void onBreakpointsChanged();

    // Command map
    std::unordered_map<std::string, CommandHandler> _commandHandlers;

    // Reference to the current emulator (if any)
    std::shared_ptr<Emulator> _emulator;

    // Flag to track if this is the first command
    bool _isFirstCommand;
};
