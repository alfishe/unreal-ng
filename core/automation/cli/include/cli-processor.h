#pragma once

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>

#include <iomanip>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "batch-command-processor.h"
#include "platform-sockets.h"

/**
 * @brief Session context for a client connection
 */
class ClientSession
{
public:
    ClientSession(SOCKET clientSocket) : _clientSocket(clientSocket), _shouldClose(false) {}

    SOCKET GetSocket() const
    {
        return _clientSocket;
    }

    // Send a response to the client
    void SendResponse(const std::string& message) const;

    // Mark session for closure
    void MarkForClosure()
    {
        _shouldClose = true;
    }

    // Check if session should be closed
    bool ShouldClose() const
    {
        return _shouldClose;
    }

    /// @brief Batch mode state
    mutable bool batchModeActive = false;             ///< True when collecting batch commands
    mutable std::vector<BatchCommand> batchCommands;  ///< Queued batch commands
    mutable std::string batchPrompt;                  ///< Prompt to display during batch mode

private:
    SOCKET _clientSocket;
    bool _shouldClose;  // Flag indicating session should be closed
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
    void HandleCreate(const ClientSession& session, const std::vector<std::string>& args);
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

    // BASIC command handlers
    void HandleBasic(const ClientSession& session, const std::vector<std::string>& args);

    // Settings command handlers
    void HandleSetting(const ClientSession& session, const std::vector<std::string>& args);

    // State inspection command handlers
    void HandleState(const ClientSession& session, const std::vector<std::string>& args);
    void HandleStateMemory(const ClientSession& session, EmulatorContext* context);
    void HandleStateMemoryRAM(const ClientSession& session, EmulatorContext* context);
    void HandleStateMemoryROM(const ClientSession& session, EmulatorContext* context);
    void HandleStateScreen(const ClientSession& session, EmulatorContext* context);
    void HandleStateScreenVerbose(const ClientSession& session, EmulatorContext* context);
    void HandleStateScreenMode(const ClientSession& session, EmulatorContext* context);
    void HandleStateScreenFlash(const ClientSession& session, EmulatorContext* context);

    // Audio state handlers
    void HandleStateAudio(const ClientSession& session, EmulatorContext* context);
    void HandleStateAudioAY(const ClientSession& session, EmulatorContext* context);
    void HandleStateAudioAYIndex(const ClientSession& session, EmulatorContext* context, const std::string& indexStr);
    void HandleStateAudioAYRegister(const ClientSession& session, EmulatorContext* context, const std::string& chipStr,
                                    const std::string& regStr);
    void HandleStateAudioBeeper(const ClientSession& session, EmulatorContext* context);
    void HandleStateAudioGS(const ClientSession& session, EmulatorContext* context);
    void HandleStateAudioCovox(const ClientSession& session, EmulatorContext* context);
    void HandleStateAudioChannels(const ClientSession& session, EmulatorContext* context);

    // Instance management command handlers
    void HandleStart(const ClientSession& session, const std::vector<std::string>& args);
    void HandleStop(const ClientSession& session, const std::vector<std::string>& args);
    void HandleModels(const ClientSession& session, const std::vector<std::string>& args);

    // Helper method to get the currently selected emulator
    std::shared_ptr<Emulator> GetSelectedEmulator(const ClientSession& session);

    // Helper method to resolve emulator from optional argument or stateless auto-selection
    // If args is not empty and first arg is an ID/index, resolves that emulator
    // Otherwise uses GetSelectedEmulator logic
    std::shared_ptr<Emulator> ResolveEmulator(const ClientSession& session, const std::vector<std::string>& args,
                                              std::string& errorMessage);

    // Universal address parsing method for memory addresses and port numbers
    // Returns true if parsing was successful, false otherwise
    // The parsed address is stored in the 'result' parameter
    // maxValue specifies the maximum allowed value (default: 0xFFFF for 16-bit addresses)
    bool ParseAddress(const std::string& addressStr, uint16_t& result, uint16_t maxValue = 0xFFFF) const;

    // Helper method to format text for terminal output (converts \n to \r\n)
    // This ensures proper line breaks in telnet/terminal clients
    static std::string FormatForTerminal(const std::string& text);

    // Helper method to notify UI components that breakpoints have changed
    void onBreakpointsChanged();

    // Tape control command handlers
    void HandleTape(const ClientSession& session, const std::vector<std::string>& args);
    void ShowTapeHelp(const ClientSession& session);
    void HandleTapeLoad(const ClientSession& session, std::shared_ptr<Emulator> emulator, EmulatorContext* context,
                        const std::vector<std::string>& args);
    void HandleTapeEject(const ClientSession& session, std::shared_ptr<Emulator> emulator, EmulatorContext* context);
    void HandleTapePlay(const ClientSession& session, std::shared_ptr<Emulator> emulator, EmulatorContext* context);
    void HandleTapeStop(const ClientSession& session, std::shared_ptr<Emulator> emulator, EmulatorContext* context);
    void HandleTapeRewind(const ClientSession& session, std::shared_ptr<Emulator> emulator, EmulatorContext* context);
    void HandleTapeInfo(const ClientSession& session, EmulatorContext* context);

    // Disk control command handlers
    void HandleDisk(const ClientSession& session, const std::vector<std::string>& args);
    void ShowDiskHelp(const ClientSession& session);
    uint8_t ParseDriveParameter(const std::string& driveStr, std::string& errorMsg);
    void HandleDiskInsert(const ClientSession& session, std::shared_ptr<Emulator> emulator, EmulatorContext* context,
                          const std::vector<std::string>& args);
    void HandleDiskEject(const ClientSession& session, std::shared_ptr<Emulator> emulator, EmulatorContext* context,
                         const std::vector<std::string>& args);
    void HandleDiskInfo(const ClientSession& session, EmulatorContext* context, const std::vector<std::string>& args);
    
    // Disk inspection command handlers
    void HandleDiskList(const ClientSession& session, EmulatorContext* context);
    void HandleDiskSector(const ClientSession& session, EmulatorContext* context, const std::vector<std::string>& args);
    void HandleDiskTrack(const ClientSession& session, EmulatorContext* context, const std::vector<std::string>& args);
    void HandleDiskSysinfo(const ClientSession& session, EmulatorContext* context, const std::vector<std::string>& args);
    void HandleDiskCatalog(const ClientSession& session, EmulatorContext* context, const std::vector<std::string>& args);
    void HandleDiskCreate(const ClientSession& session, std::shared_ptr<Emulator> emulator, EmulatorContext* context,
                          const std::vector<std::string>& args);

    // Snapshot control command handlers
    void HandleSnapshot(const ClientSession& session, const std::vector<std::string>& args);
    void ShowSnapshotHelp(const ClientSession& session);
    void HandleSnapshotLoad(const ClientSession& session, std::shared_ptr<Emulator> emulator, EmulatorContext* context,
                            const std::vector<std::string>& args);
    void HandleSnapshotSave(const ClientSession& session, std::shared_ptr<Emulator> emulator, 
                            const std::vector<std::string>& args);
    void HandleSnapshotInfo(const ClientSession& session, EmulatorContext* context);

    // Batch command handlers
    void HandleBatch(const ClientSession& session, const std::vector<std::string>& args);
    void ShowBatchHelp(const ClientSession& session);
    void HandleBatchStart(const ClientSession& session, const std::vector<std::string>& args);
    void HandleBatchExecute(const ClientSession& session, const std::vector<std::string>& args);
    void HandleBatchCancel(const ClientSession& session, const std::vector<std::string>& args);
    void HandleBatchList(const ClientSession& session, const std::vector<std::string>& args);
    void HandleBatchStatus(const ClientSession& session, const std::vector<std::string>& args);
    void HandleBatchCommands(const ClientSession& session, const std::vector<std::string>& args);
    void AddToBatch(const ClientSession& session, const std::string& emulatorId, const std::string& command,
                    const std::string& arg1, const std::string& arg2);

    // Command map
    std::unordered_map<std::string, CommandHandler> _commandHandlers;

    // Reference to the current emulator (if any)
    std::shared_ptr<Emulator> _emulator;

    // Flag to track if this is the first command
    bool _isFirstCommand;
};
