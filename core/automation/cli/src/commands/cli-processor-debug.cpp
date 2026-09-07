// CLI Debug and Stepping Commands
// Extracted from cli-processor.cpp - 2026-01-08

#include "cli-processor.h"

#include <3rdparty/message-center/messagecenter.h>
#include <debugger/assembler/z80textassembler.h>
#include <debugger/debugmanager.h>
#include <debugger/disassembler/z80disasm.h>
#include <debugger/labels/labelmanager.h>
#include <debugger/listing/listingparser.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include <emulator/notifications.h>
#include <emulator/platform.h>

#include <cctype>
#include <iostream>
#include <sstream>

// HandleStepIn - lines 879-1033
void CLIProcessor::HandleStepIn(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    // Check if the emulator is paused
    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    // Parse step count argument
    int stepCount = 1;  // stepin always executes one instruction
    // Note: stepin ignores any count parameter - it always steps one instruction
    if (!args.empty())
    {
        // For stepin, we ignore the count parameter and always execute one instruction
        // This makes stepin behavior consistent and predictable
    }

    // Get memory and disassembler for instruction info
    Memory* memory = emulator->GetMemory();
    std::unique_ptr<Z80Disassembler>& disassembler = emulator->GetDebugManager()->GetDisassembler();

    if (!memory || !disassembler)
    {
        session.SendResponse("Error: Unable to access memory or disassembler.");
        return;
    }

    // Get the current PC and disassemble the instruction that's about to be executed
    Z80State* z80State = emulator->GetZ80State();
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state.\n");
        return;
    }

    // Store the current PC to show what's about to be executed
    uint16_t initialPC = z80State->pc;

    // Disassemble the instruction that's about to be executed
    uint8_t commandLen = 0;
    DecodedInstruction decodedBefore;
    std::vector<uint8_t> buffer(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
    for (int i = 0; i < buffer.size(); i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(initialPC + i);
    }
    std::string instructionBefore = disassembler->disassembleSingleCommandWithRuntime(buffer, initialPC, &commandLen,
                                                                                      z80State, memory, &decodedBefore);

    // Execute the requested number of CPU cycles
    for (int i = 0; i < stepCount; ++i)
    {
        emulator->RunSingleCPUCycle(false);  // false = don't skip breakpoints
    }

    // Get the Z80 state after execution
    z80State = emulator->GetZ80State();  // Refresh state after execution
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state after execution.");
        return;
    }

    // Get the new PC and disassemble the next instruction to be executed
    uint16_t newPC = z80State->pc;

    // Disassemble the next instruction to be executed
    for (int i = 0; i < Z80Disassembler::MAX_INSTRUCTION_LENGTH; i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(newPC + i);
    }

    DecodedInstruction decodedAfter;
    commandLen = 0;
    std::string instructionAfter =
        disassembler->disassembleSingleCommandWithRuntime(buffer, newPC, &commandLen, z80State, memory, &decodedAfter);

    // Format response with CPU state information
    std::stringstream ss;
    ss << "Executed " << stepCount << " instruction" << (stepCount != 1 ? "s" : "") << NEWLINE;

    // Show executed instruction
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "Executed: [$" << std::setw(4) << initialPC << "] ";

    // Add hex dump of the executed instruction
    if (decodedBefore.instructionBytes.size() > 0)
    {
        for (uint8_t byte : decodedBefore.instructionBytes)
        {
            ss << std::setw(2) << static_cast<int>(byte) << " ";
        }
        // Add padding for alignment if needed
        for (size_t i = decodedBefore.instructionBytes.size(); i < 4; i++)
        {
            ss << "   ";
        }
    }

    ss << instructionBefore << NEWLINE;

    // Show next instruction
    ss << "Next:     [$" << std::setw(4) << newPC << "] ";

    // Add hex dump of the next instruction
    if (decodedAfter.instructionBytes.size() > 0)
    {
        for (uint8_t byte : decodedAfter.instructionBytes)
        {
            ss << std::setw(2) << static_cast<int>(byte) << " ";
        }
        // Add padding for alignment if needed
        for (size_t i = decodedAfter.instructionBytes.size(); i < 4; i++)
        {
            ss << "   ";
        }
    }

    ss << instructionAfter << "\n\n";

    // Format current PC and registers
    ss << "PC: $" << std::setw(4) << z80State->pc << "  ";

    // Show main registers (compact format)
    ss << "AF: $" << std::setw(4) << z80State->af << "  ";
    ss << "BC: $" << std::setw(4) << z80State->bc << "  ";
    ss << "DE: $" << std::setw(4) << z80State->de << "  ";
    ss << "HL: $" << std::setw(4) << z80State->hl << NEWLINE;

    // Show flags
    ss << "Flags: ";
    ss << (z80State->f & 0x80 ? "S" : "-");
    ss << (z80State->f & 0x40 ? "Z" : "-");
    ss << (z80State->f & 0x20 ? "5" : "-");
    ss << (z80State->f & 0x10 ? "H" : "-");
    ss << (z80State->f & 0x08 ? "3" : "-");
    ss << (z80State->f & 0x04 ? "P" : "-");
    ss << (z80State->f & 0x02 ? "N" : "-");
    ss << (z80State->f & 0x01 ? "C" : "-");
    ss << NEWLINE;

    // Add note about viewing full register state
    ss << "\nUse 'registers' command to view full CPU state\n";

    session.SendResponse(ss.str());
}

// HandleStepOver - lines 1035-1200
void CLIProcessor::HandleStepOver(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    // Check if the emulator is paused
    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    // Get memory and disassembler for instruction info
    Memory* memory = emulator->GetMemory();
    std::unique_ptr<Z80Disassembler>& disassembler = emulator->GetDebugManager()->GetDisassembler();

    if (!memory || !disassembler)
    {
        session.SendResponse("Error: Unable to access memory or disassembler.");
        return;
    }

    // Get the current PC and disassemble the instruction that's about to be executed
    Z80State* z80State = emulator->GetZ80State();
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state.");
        return;
    }

    uint16_t initialPC = z80State->pc;

    // Disassemble the instruction that's about to be executed
    uint8_t commandLen = 0;
    DecodedInstruction decodedBefore;
    std::vector<uint8_t> buffer(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
    for (int i = 0; i < buffer.size(); i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(initialPC + i);
    }
    std::string instructionBefore = disassembler->disassembleSingleCommandWithRuntime(buffer, initialPC, &commandLen,
                                                                                      z80State, memory, &decodedBefore);

    // Execute the step-over operation
    emulator->StepOver();

    // Get the updated Z80 state
    z80State = emulator->GetZ80State();
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state after step-over.");
        return;
    }

    uint16_t newPC = z80State->pc;

    // Determine if this was a simple step or actual step-over
    bool wasStepOver = (newPC != initialPC + decodedBefore.fullCommandLen);
    std::string operationType = wasStepOver ? "Step-over" : "Step-in (instruction didn't require step-over)";

    // Add instruction type information
    std::string instructionType = "";
    if (decodedBefore.hasJump && !decodedBefore.hasRelativeJump)
    {
        if (decodedBefore.isRst)
            instructionType = " (RST instruction)";
        else if (decodedBefore.opcode.mnem && strstr(decodedBefore.opcode.mnem, "call"))
            instructionType = " (CALL instruction)";
        else
            instructionType = " (JUMP instruction)";
    }
    else if (decodedBefore.isDjnz)
    {
        instructionType = " (DJNZ instruction)";
    }
    else if (decodedBefore.isBlockOp)
    {
        instructionType = " (Block instruction)";
    }
    else if (decodedBefore.hasCondition)
    {
        instructionType = " (Conditional instruction)";
    }

    // Disassemble the next instruction to be executed
    for (int i = 0; i < Z80Disassembler::MAX_INSTRUCTION_LENGTH; i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(newPC + i);
    }

    DecodedInstruction decodedAfter;
    commandLen = 0;
    std::string instructionAfter =
        disassembler->disassembleSingleCommandWithRuntime(buffer, newPC, &commandLen, z80State, memory, &decodedAfter);

    // Format response with CPU state information
    std::stringstream ss;
    ss << operationType << instructionType << " completed" << NEWLINE;

    // Show executed instruction
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "Executed: [$" << std::setw(4) << initialPC << "] ";

    // Add hex dump of the executed instruction
    if (decodedBefore.instructionBytes.size() > 0)
    {
        for (uint8_t byte : decodedBefore.instructionBytes)
        {
            ss << std::setw(2) << static_cast<int>(byte) << " ";
        }
        // Add padding for alignment if needed
        for (size_t i = decodedBefore.instructionBytes.size(); i < 4; i++)
        {
            ss << "   ";
        }
    }

    ss << instructionBefore << NEWLINE;

    // Show next instruction to be executed
    ss << "Next:     [$" << std::setw(4) << newPC << "] ";

    // Add hex dump of the next instruction
    if (decodedAfter.instructionBytes.size() > 0)
    {
        for (uint8_t byte : decodedAfter.instructionBytes)
        {
            ss << std::setw(2) << static_cast<int>(byte) << " ";
        }
        // Add padding for alignment if needed
        for (size_t i = decodedAfter.instructionBytes.size(); i < 4; i++)
        {
            ss << "   ";
        }
    }

    ss << instructionAfter << NEWLINE;

    // Show register state
    ss << NEWLINE << "Registers:" << NEWLINE;
    ss << "  PC: $" << std::setw(4) << z80State->pc << NEWLINE;
    ss << "  AF: $" << std::setw(4) << z80State->af << NEWLINE;
    ss << "  BC: $" << std::setw(4) << z80State->bc << NEWLINE;
    ss << "  DE: $" << std::setw(4) << z80State->de << NEWLINE;
    ss << "  HL: $" << std::setw(4) << z80State->hl << NEWLINE;
    ss << "  SP: $" << std::setw(4) << z80State->sp << NEWLINE;
    ss << "  IX: $" << std::setw(4) << z80State->ix << NEWLINE;
    ss << "  IY: $" << std::setw(4) << z80State->iy << NEWLINE;
    ss << "  Flags: ";
    ss << (z80State->f & 0x80 ? "S" : "-");
    ss << (z80State->f & 0x40 ? "Z" : "-");
    ss << (z80State->f & 0x20 ? "5" : "-");
    ss << (z80State->f & 0x10 ? "H" : "-");
    ss << (z80State->f & 0x08 ? "3" : "-");
    ss << (z80State->f & 0x04 ? "P" : "-");
    ss << (z80State->f & 0x02 ? "N" : "-");
    ss << (z80State->f & 0x01 ? "C" : "-");
    ss << NEWLINE;

    session.SendResponse(ss.str());
}

// HandleDebugMode - lines 2230-2280
void CLIProcessor::HandleDebugMode(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("Error: No emulator selected" + std::string(NEWLINE));
        return;
    }

    if (args.size() < 1)
    {
        // Show current mode
        bool isDebugMode = emulator->GetContext()->pCore->GetZ80()->isDebugMode;
        std::string mode = isDebugMode ? "on" : "off";
        session.SendResponse("Debug mode is currently " + mode + NEWLINE);
        session.SendResponse("Usage: debugmode <on|off>" + std::string(NEWLINE));
        return;
    }

    const std::string& mode = args[0];
    Core* core = emulator->GetContext()->pCore;
    bool success = true;
    std::string response;

    if (mode == "on")
    {
        core->UseDebugMemoryInterface();
        core->GetZ80()->isDebugMode = true;
        response = "Debug mode enabled (slower, with breakpoint support)" + std::string(NEWLINE);
    }
    else if (mode == "off")
    {
        core->UseFastMemoryInterface();
        core->GetZ80()->isDebugMode = false;
        response = "Debug mode disabled (faster, no breakpoints)" + std::string(NEWLINE);
    }
    else
    {
        success = false;
        response = "Error: Invalid parameter. Use 'on' or 'off'" + std::string(NEWLINE);
    }

    session.SendResponse(response);
    if (success)
    {
        // Also show the current mode after changing it
        bool isDebugMode = emulator->GetContext()->pCore->GetZ80()->isDebugMode;
        std::string currentMode = isDebugMode ? "on" : "off";
        session.SendResponse("Debug mode is now " + currentMode + NEWLINE);
    }
}

// HandleSteps - lines 2875-3048
void CLIProcessor::HandleSteps(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    // Check if the emulator is paused
    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    // Parse step count argument
    int stepCount = 1;
    if (args.empty())
    {
        session.SendResponse("Usage: steps <count> - Execute 1 to N CPU instructions");
        return;
    }

    try
    {
        stepCount = std::stoi(args[0]);
        if (stepCount < 1)
        {
            session.SendResponse("Error: Step count must be at least 1");
            return;
        }
        if (stepCount > 1000)
        {
            session.SendResponse("Error: Step count cannot exceed 1000");
            return;
        }
    }
    catch (...)
    {
        session.SendResponse("Error: Invalid step count. Must be a number between 1 and 1000");
        return;
    }

    // Get memory and disassembler for instruction info
    Memory* memory = emulator->GetMemory();
    std::unique_ptr<Z80Disassembler>& disassembler = emulator->GetDebugManager()->GetDisassembler();

    if (!memory || !disassembler)
    {
        session.SendResponse("Error: Unable to access memory or disassembler.");
        return;
    }

    // Get the current PC and disassemble the instruction that's about to be executed
    Z80State* z80State = emulator->GetZ80State();
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state.");
        return;
    }

    // Store the current PC to show what's about to be executed
    uint16_t initialPC = z80State->pc;

    // Disassemble the instruction that's about to be executed
    uint8_t commandLen = 0;
    DecodedInstruction decodedBefore;
    std::vector<uint8_t> buffer(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
    for (int i = 0; i < buffer.size(); i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(initialPC + i);
    }
    std::string instructionBefore = disassembler->disassembleSingleCommandWithRuntime(buffer, initialPC, &commandLen,
                                                                                      z80State, memory, &decodedBefore);

    // Execute the requested number of CPU cycles
    for (int i = 0; i < stepCount; ++i)
    {
        emulator->RunSingleCPUCycle(false);  // false = don't skip breakpoints
    }

    // Get the Z80 state after execution
    z80State = emulator->GetZ80State();  // Refresh state after execution
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state after execution.");
        return;
    }

    // Get the new PC and disassemble the next instruction to be executed
    uint16_t newPC = z80State->pc;

    // Disassemble the next instruction to be executed
    for (int i = 0; i < Z80Disassembler::MAX_INSTRUCTION_LENGTH; i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(newPC + i);
    }

    DecodedInstruction decodedAfter;
    commandLen = 0;
    std::string instructionAfter =
        disassembler->disassembleSingleCommandWithRuntime(buffer, newPC, &commandLen, z80State, memory, &decodedAfter);

    // Format response with CPU state information
    std::stringstream ss;
    ss << "Executed " << stepCount << " instruction" << (stepCount != 1 ? "s" : "") << NEWLINE;

    // Show executed instruction
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "Executed: [$" << std::setw(4) << initialPC << "] ";

    // Add hex dump of the executed instruction
    if (decodedBefore.instructionBytes.size() > 0)
    {
        for (uint8_t byte : decodedBefore.instructionBytes)
        {
            ss << std::setw(2) << static_cast<int>(byte) << " ";
        }
        // Add padding for alignment if needed
        for (size_t i = decodedBefore.instructionBytes.size(); i < 4; i++)
        {
            ss << "   ";
        }
    }

    ss << instructionBefore << NEWLINE;

    // Show next instruction
    ss << "Next:     [$" << std::setw(4) << newPC << "] ";

    // Add hex dump of the next instruction
    if (decodedAfter.instructionBytes.size() > 0)
    {
        for (uint8_t byte : decodedAfter.instructionBytes)
        {
            ss << std::setw(2) << static_cast<int>(byte) << " ";
        }
        // Add padding for alignment if needed
        for (size_t i = decodedAfter.instructionBytes.size(); i < 4; i++)
        {
            ss << "   ";
        }
    }

    ss << instructionAfter << "\n\n";

    // Format current PC and registers
    ss << "PC: $" << std::setw(4) << z80State->pc << "  ";

    // Show main registers (compact format)
    ss << "AF: $" << std::setw(4) << z80State->af << "  ";
    ss << "BC: $" << std::setw(4) << z80State->bc << "  ";
    ss << "DE: $" << std::setw(4) << z80State->de << "  ";
    ss << "HL: $" << std::setw(4) << z80State->hl << NEWLINE;

    // Show flags
    ss << "Flags: ";
    ss << (z80State->f & 0x80 ? "S" : "-");
    ss << (z80State->f & 0x40 ? "Z" : "-");
    ss << (z80State->f & 0x20 ? "5" : "-");
    ss << (z80State->f & 0x10 ? "H" : "-");
    ss << (z80State->f & 0x08 ? "3" : "-");
    ss << (z80State->f & 0x04 ? "P" : "-");
    ss << (z80State->f & 0x02 ? "N" : "-");
    ss << (z80State->f & 0x01 ? "C" : "-");
    ss << NEWLINE;

    // Add note about viewing full register state
    ss << "\nUse 'registers' command to view full CPU state\n";

    session.SendResponse(ss.str());
}

// Atomic Stepping Commands

void CLIProcessor::HandleRunTStates(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    if (args.empty())
    {
        session.SendResponse("Usage: run_tstates <count> - Run N t-states (1 t-state = 1 ULA step / 2 pixels)");
        return;
    }

    unsigned count = 0;
    try
    {
        count = static_cast<unsigned>(std::stoul(args[0]));
        if (count < 1 || count > 10000000)
        {
            session.SendResponse("Error: T-state count must be between 1 and 10000000");
            return;
        }
    }
    catch (...)
    {
        session.SendResponse("Error: Invalid count. Must be a number between 1 and 10000000");
        return;
    }

    emulator->RunTStates(count);

    Z80State* z80 = emulator->GetZ80State();
    std::stringstream ss;
    ss << "Ran " << count << " t-states" << NEWLINE;
    if (z80)
    {
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "PC: $" << std::setw(4) << z80->pc << "  SP: $" << std::setw(4) << z80->sp << NEWLINE;
    }
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleRunToScanline(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    if (args.empty())
    {
        session.SendResponse("Usage: run_to_scanline <line> - Run until scanline N boundary (0-319)");
        return;
    }

    unsigned scanline = 0;
    try
    {
        scanline = static_cast<unsigned>(std::stoul(args[0]));
    }
    catch (...)
    {
        session.SendResponse("Error: Invalid scanline number");
        return;
    }

    emulator->RunUntilScanline(scanline);

    Z80State* z80 = emulator->GetZ80State();
    std::stringstream ss;
    ss << "Ran to scanline " << scanline << NEWLINE;
    if (z80)
    {
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "PC: $" << std::setw(4) << z80->pc << "  SP: $" << std::setw(4) << z80->sp << NEWLINE;
    }
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleRunNScanlines(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    if (args.empty())
    {
        session.SendResponse("Usage: run_scanlines <count> - Run N complete scanlines from current position");
        return;
    }

    unsigned count = 0;
    try
    {
        count = static_cast<unsigned>(std::stoul(args[0]));
        if (count < 1 || count > 1000)
        {
            session.SendResponse("Error: Scanline count must be between 1 and 1000");
            return;
        }
    }
    catch (...)
    {
        session.SendResponse("Error: Invalid count. Must be a number between 1 and 1000");
        return;
    }

    emulator->RunNScanlines(count);

    Z80State* z80 = emulator->GetZ80State();
    std::stringstream ss;
    ss << "Ran " << count << " scanlines" << NEWLINE;
    if (z80)
    {
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "PC: $" << std::setw(4) << z80->pc << "  SP: $" << std::setw(4) << z80->sp << NEWLINE;
    }
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleRunToPixel(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    emulator->RunUntilNextScreenPixel();

    Z80State* z80 = emulator->GetZ80State();
    std::stringstream ss;
    ss << "Ran to next screen pixel" << NEWLINE;
    if (z80)
    {
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "PC: $" << std::setw(4) << z80->pc << "  SP: $" << std::setw(4) << z80->sp << NEWLINE;
    }
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleRunToInterrupt(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    emulator->RunUntilInterrupt();

    Z80State* z80 = emulator->GetZ80State();
    std::stringstream ss;
    ss << "Ran to interrupt" << NEWLINE;
    if (z80)
    {
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "PC: $" << std::setw(4) << z80->pc << "  SP: $" << std::setw(4) << z80->sp << NEWLINE;
    }
    session.SendResponse(ss.str());
}

// Run one complete video frame
void CLIProcessor::HandleRunFrame(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    emulator->RunFrame();

    Z80State* z80 = emulator->GetZ80State();
    std::stringstream ss;
    ss << "Ran one frame" << NEWLINE;
    if (z80)
    {
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "PC: $" << std::setw(4) << z80->pc << "  SP: $" << std::setw(4) << z80->sp << NEWLINE;
    }
    session.SendResponse(ss.str());
}

// Run N complete video frames
void CLIProcessor::HandleRunFrames(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    if (args.empty())
    {
        session.SendResponse("Usage: run_frames <count> - Run N video frames");
        return;
    }

    unsigned count = 0;
    try
    {
        count = static_cast<unsigned>(std::stoul(args[0]));
        if (count < 1 || count > 10000)
        {
            session.SendResponse("Error: Frame count must be between 1 and 10000");
            return;
        }
    }
    catch (...)
    {
        session.SendResponse("Error: Invalid count. Must be a number between 1 and 10000");
        return;
    }

    emulator->RunNFrames(count);

    Z80State* z80 = emulator->GetZ80State();
    std::stringstream ss;
    ss << "Ran " << count << " frames" << NEWLINE;
    if (z80)
    {
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "PC: $" << std::setw(4) << z80->pc << "  SP: $" << std::setw(4) << z80->sp << NEWLINE;
    }
    session.SendResponse(ss.str());
}

// Run N CPU cycles (instructions)
void CLIProcessor::HandleRunNCycles(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    if (args.empty())
    {
        session.SendResponse("Usage: run_ncycles <count> - Run N CPU cycles (instructions)");
        return;
    }

    unsigned count = 0;
    try
    {
        count = static_cast<unsigned>(std::stoul(args[0]));
        if (count < 1 || count > 100000)
        {
            session.SendResponse("Error: Cycle count must be between 1 and 100000");
            return;
        }
    }
    catch (...)
    {
        session.SendResponse("Error: Invalid count. Must be a number between 1 and 100000");
        return;
    }

    emulator->RunNCPUCycles(count, false);

    Z80State* z80 = emulator->GetZ80State();
    std::stringstream ss;
    ss << "Ran " << count << " CPU cycles" << NEWLINE;
    if (z80)
    {
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "PC: $" << std::setw(4) << z80->pc << "  SP: $" << std::setw(4) << z80->sp << NEWLINE;
    }
    session.SendResponse(ss.str());
}

// HandleDisasm - disassemble Z80 code at address or PC
void CLIProcessor::HandleDisasm(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    Memory* memory = emulator->GetMemory();
    std::unique_ptr<Z80Disassembler>& disassembler = emulator->GetDebugManager()->GetDisassembler();

    if (!memory || !disassembler)
    {
        session.SendResponse("Error: Unable to access memory or disassembler.");
        return;
    }

    // Parse arguments: disasm [address] [count]
    uint16_t address = emulator->GetZ80State()->pc;  // Default: PC
    int count = 10;  // Default count

    try {
        if (args.size() >= 1)
        {
            const std::string& addrStr = args[0];
            if (addrStr.find("0x") == 0 || addrStr.find("0X") == 0 || addrStr.find("$") == 0)
                address = static_cast<uint16_t>(std::stoul(addrStr.substr(addrStr[0] == '$' ? 1 : 2), nullptr, 16));
            else
                address = static_cast<uint16_t>(std::stoul(addrStr));
        }
        if (args.size() >= 2)
        {
            count = std::stoi(args[1]);
            if (count < 1) count = 1;
            if (count > 100) count = 100;
        }
    } catch (...) {
        session.SendResponse("Error: Invalid address or count. Usage: disasm [address] [count]");
        return;
    }

    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');

    uint16_t currentAddr = address;
    for (int i = 0; i < count; ++i)
    {
        std::vector<uint8_t> buffer(4);
        for (int j = 0; j < 4; ++j)
            buffer[j] = memory->DirectReadFromZ80Memory(currentAddr + j);

        uint8_t cmdLen = 0;
        DecodedInstruction decoded;
        std::string mnemonic = disassembler->disassembleSingleCommand(buffer, currentAddr, &cmdLen, &decoded);
        if (cmdLen == 0) cmdLen = 1;

        // Format: $XXXX: XX XX XX XX  mnemonic
        ss << "$" << std::setw(4) << currentAddr << ": ";
        for (uint8_t j = 0; j < cmdLen; ++j)
            ss << std::setw(2) << static_cast<int>(buffer[j]) << " ";
        for (int j = cmdLen; j < 4; ++j)
            ss << "   ";
        ss << " " << mnemonic << NEWLINE;

        currentAddr += cmdLen;
    }

    session.SendResponse(ss.str());
}

// HandleDisasmPage - disassemble from physical RAM/ROM page
void CLIProcessor::HandleDisasmPage(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    Memory* memory = emulator->GetMemory();
    std::unique_ptr<Z80Disassembler>& disassembler = emulator->GetDebugManager()->GetDisassembler();

    if (!memory || !disassembler)
    {
        session.SendResponse("Error: Unable to access memory or disassembler.");
        return;
    }

    // Parse arguments: disasm_page <ram|rom> <page> [offset] [count]
    if (args.size() < 2)
    {
        session.SendResponse("Usage: disasm_page <ram|rom> <page> [offset] [count]\n"
                             "Example: disasm_page rom 2 0 20  (TR-DOS ROM start)");
        return;
    }

    std::string type = args[0];
    bool isROM = (type == "rom");
    if (type != "rom" && type != "ram")
    {
        session.SendResponse("Error: First argument must be 'ram' or 'rom'");
        return;
    }

    uint8_t page = 0;
    uint16_t offset = 0;
    int count = 10;

    try {
        page = static_cast<uint8_t>(std::stoul(args[1]));
        if (args.size() >= 3)
        {
            const std::string& offStr = args[2];
            if (offStr.find("0x") == 0 || offStr.find("0X") == 0 || offStr.find("$") == 0)
                offset = static_cast<uint16_t>(std::stoul(offStr.substr(offStr[0] == '$' ? 1 : 2), nullptr, 16));
            else
                offset = static_cast<uint16_t>(std::stoul(offStr));
        }
        if (args.size() >= 4)
        {
            count = std::stoi(args[3]);
            if (count < 1) count = 1;
            if (count > 100) count = 100;
        }
    } catch (...) {
        session.SendResponse("Error: Invalid parameters. Usage: disasm_page <ram|rom> <page> [offset] [count]");
        return;
    }

    if (offset >= PAGE_SIZE) offset = PAGE_SIZE - 1;

    uint8_t* pageBase = isROM ? memory->ROMPageHostAddress(page) : memory->RAMPageAddress(page);
    if (!pageBase)
    {
        session.SendResponse("Error: Invalid page number");
        return;
    }

    std::stringstream ss;
    ss << type << " page " << static_cast<int>(page) << " @ offset $" << std::hex << std::uppercase << offset << ":" << NEWLINE;
    ss << std::hex << std::uppercase << std::setfill('0');

    uint16_t currentOffset = offset;
    for (int i = 0; i < count && currentOffset < PAGE_SIZE; ++i)
    {
        std::vector<uint8_t> buffer(4);
        for (int j = 0; j < 4 && (currentOffset + j) < PAGE_SIZE; ++j)
            buffer[j] = pageBase[currentOffset + j];

        uint8_t cmdLen = 0;
        DecodedInstruction decoded;
        std::string mnemonic = disassembler->disassembleSingleCommand(buffer, currentOffset, &cmdLen, &decoded);
        if (cmdLen == 0) cmdLen = 1;

        // Format: $XXXX: XX XX XX XX  mnemonic
        ss << "$" << std::setw(4) << currentOffset << ": ";
        for (uint8_t j = 0; j < cmdLen; ++j)
            ss << std::setw(2) << static_cast<int>(buffer[j]) << " ";
        for (int j = cmdLen; j < 4; ++j)
            ss << "   ";
        ss << " " << mnemonic << NEWLINE;

        currentOffset += cmdLen;
    }

    session.SendResponse(ss.str());
}

// ============================================================================
// Label/Symbol Management Commands
// ============================================================================

void CLIProcessor::HandleLabel(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        session.SendResponse("Debug manager not available.");
        return;
    }

    LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
    if (!labelMgr)
    {
        session.SendResponse("Label manager not available.");
        return;
    }

    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage:" << NEWLINE;
        ss << "  label <name>                    - Get label by name" << NEWLINE;
        ss << "  label add <name> <addr>         - Add label" << NEWLINE;
        ss << "  label remove <name>             - Remove label" << NEWLINE;
        ss << "  label toggle <name>             - Toggle active state" << NEWLINE;
        ss << "  label resolve <name|addr>       - Resolve name or address to label(s)" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    const std::string& subcmd = args[0];

    if (subcmd == "add" && args.size() >= 3)
    {
        const std::string& name = args[1];
        uint16_t address = 0;
        try
        {
            address = static_cast<uint16_t>(std::stoul(args[2], nullptr, 0));
        }
        catch (...)
        {
            session.SendResponse("Error: Invalid address.");
            return;
        }

        std::string type, module, comment;
        uint16_t bank = UINT16_MAX;

        for (size_t i = 3; i < args.size(); i++)
        {
            if (args[i] == "--type" && i + 1 < args.size())
                type = args[++i];
            else if (args[i] == "--module" && i + 1 < args.size())
                module = args[++i];
            else if (args[i] == "--bank" && i + 1 < args.size())
                bank = static_cast<uint16_t>(std::stoul(args[++i], nullptr, 0));
            else if (args[i] == "--comment" && i + 1 < args.size())
                comment = args[++i];
        }

        if (labelMgr->AddLabel(name, address, bank, UINT16_MAX, type, module, comment))
            session.SendResponse("Label '" + name + "' added at $" +
                (std::stringstream() << std::hex << std::uppercase << address).str() + NEWLINE);
        else
            session.SendResponse("Error: Failed to add label." + std::string(NEWLINE));
    }
    else if (subcmd == "remove" && args.size() >= 2)
    {
        if (labelMgr->RemoveLabel(args[1]))
            session.SendResponse("Label '" + args[1] + "' removed." + std::string(NEWLINE));
        else
            session.SendResponse("Error: Label not found." + std::string(NEWLINE));
    }
    else if (subcmd == "toggle" && args.size() >= 2)
    {
        auto label = labelMgr->GetLabelByName(args[1]);
        if (label)
        {
            label->active = !label->active;
            session.SendResponse("Label '" + args[1] + "' " +
                (label->active ? "activated" : "deactivated") + "." + std::string(NEWLINE));
        }
        else
            session.SendResponse("Error: Label not found." + std::string(NEWLINE));
    }
    else if (subcmd == "resolve" && args.size() >= 2)
    {
        // Name direction: exact label lookup. Address direction (0x/$/decimal):
        // exact label + aliases + nearest labels around the address.
        const std::string& query = args[1];
        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setfill('0');

        bool looksLikeAddress = false;
        uint16_t address = 0;
        if (query.rfind("0x", 0) == 0 || query.rfind("0X", 0) == 0 || (!query.empty() && query[0] == '$'))
        {
            looksLikeAddress = true;
            try
            {
                const std::string digits = query[0] == '$' ? query.substr(1) : query.substr(2);
                address = static_cast<uint16_t>(std::stoul(digits, nullptr, 16));
            }
            catch (...)
            {
                session.SendResponse("Error: Invalid address format.");
                return;
            }
        }
        else
        {
            for (char c : query)
            {
                if (!std::isdigit(static_cast<unsigned char>(c)))
                    break;
                looksLikeAddress = true;
            }
            if (looksLikeAddress)
            {
                try
                {
                    address = static_cast<uint16_t>(std::stoul(query));
                }
                catch (...)
                {
                    looksLikeAddress = false;
                }
            }
        }

        if (!looksLikeAddress)
        {
            auto label = labelMgr->GetLabelByName(query);
            if (!label)
            {
                std::stringstream hint;
                hint << "Error: Label not found: " << query;
                if (labelMgr->GetLabelCount() == 0)
                    hint << " (no labels loaded — 'symbols load <file>' first)";
                session.SendResponse(hint.str() + std::string(NEWLINE));
                return;
            }

            ss << "Label: " << label->name << " = $" << std::setw(4) << label->address << NEWLINE;
            if (!label->type.empty())
                ss << "Type: " << label->type << NEWLINE;
            session.SendResponse(ss.str());
            return;
        }

        ss << "Address: $" << std::setw(4) << address << NEWLINE;

        auto exact = labelMgr->GetLabelByZ80Address(address);
        if (exact)
        {
            ss << "Exact: " << exact->name << NEWLINE;
        }

        auto atAddress = labelMgr->GetAllLabelsAtAddress(address);
        if (atAddress.size() > 1)
        {
            ss << "Aliases:" << NEWLINE;
            for (const auto& l : atAddress)
                ss << "  $" << std::setw(4) << l->address << "  " << l->name << NEWLINE;
        }

        // Nearest labels around the address — context for disassembly annotation
        const Label* bestBelow = nullptr;
        const Label* bestAbove = nullptr;
        for (const auto& l : labelMgr->GetAllLabels())
        {
            if (l->address < address && (!bestBelow || l->address > bestBelow->address))
                bestBelow = l.get();
            else if (l->address > address && (!bestAbove || l->address < bestAbove->address))
                bestAbove = l.get();
        }
        if (bestBelow)
        {
            ss << std::dec;
            ss << "Nearest below: " << bestBelow->name << " (-" << (address - bestBelow->address) << ")" << NEWLINE;
        }
        if (bestAbove)
        {
            ss << std::dec;
            ss << "Nearest above: " << bestAbove->name << " (+" << (bestAbove->address - address) << ")" << NEWLINE;
        }

        if (!exact && atAddress.empty())
            ss << "No label at this address." << NEWLINE;

        session.SendResponse(ss.str());
    }
    else
    {
        auto label = labelMgr->GetLabelByName(args[0]);
        if (label)
        {
            std::stringstream ss;
            ss << std::hex << std::uppercase << std::setfill('0');
            ss << "Name: " << label->name << NEWLINE;
            ss << "Address: $" << std::setw(4) << label->address << NEWLINE;
            if (label->bank != UINT16_MAX)
                ss << "Bank: " << std::dec << label->bank << " (" << (label->isROM() ? "ROM" : "RAM") << ")" << NEWLINE;
            if (!label->type.empty())
                ss << "Type: " << label->type << NEWLINE;
            if (!label->module.empty())
                ss << "Module: " << label->module << NEWLINE;
            if (!label->comment.empty())
                ss << "Comment: " << label->comment << NEWLINE;
            ss << "Active: " << (label->active ? "yes" : "no") << NEWLINE;
            session.SendResponse(ss.str());
        }
        else
            session.SendResponse("Label not found: " + args[0] + NEWLINE);
    }
}

void CLIProcessor::HandleLabels(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        session.SendResponse("Debug manager not available.");
        return;
    }

    LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
    if (!labelMgr)
    {
        session.SendResponse("Label manager not available.");
        return;
    }

    LabelManager::LabelFilter filter;

    for (size_t i = 0; i < args.size(); i++)
    {
        if (args[i] == "--module" && i + 1 < args.size())
            filter.module = args[++i];
        else if (args[i] == "--type" && i + 1 < args.size())
            filter.type = args[++i];
        else if (args[i] == "--bank" && i + 1 < args.size())
            filter.bank = static_cast<uint16_t>(std::stoul(args[++i], nullptr, 0));
        else if (args[i] == "--active")
            filter.activeOnly = true;
        else if (args[i] == "--from" && i + 1 < args.size())
            filter.addressFrom = static_cast<uint16_t>(std::stoul(args[++i], nullptr, 0));
        else if (args[i] == "--to" && i + 1 < args.size())
            filter.addressTo = static_cast<uint16_t>(std::stoul(args[++i], nullptr, 0));
    }

    auto labels = labelMgr->GetLabels(filter);

    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "Labels (" << std::dec << labels.size() << " of " << labelMgr->GetLabelCount() << "):" << NEWLINE;

    for (const auto& label : labels)
    {
        ss << std::hex << "  $" << std::setw(4) << label->address << "  " << label->name;
        if (!label->type.empty())
            ss << " [" << label->type << "]";
        if (!label->module.empty())
            ss << " (" << label->module << ")";
        if (!label->active)
            ss << " (inactive)";
        ss << NEWLINE;
    }

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleSymbols(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        session.SendResponse("Debug manager not available.");
        return;
    }

    LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
    if (!labelMgr)
    {
        session.SendResponse("Label manager not available.");
        return;
    }

    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage:" << NEWLINE;
        ss << "  symbols load <file>             - Load symbol file" << NEWLINE;
        ss << "  symbols save <file>             - Save symbols to file" << NEWLINE;
        ss << "  symbols clear                   - Clear all symbols" << NEWLINE;
        ss << "  symbols info                    - Show symbol count" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    const std::string& subcmd = args[0];

    if (subcmd == "load" && args.size() >= 2)
    {
        if (labelMgr->LoadLabels(args[1]))
            session.SendResponse("Loaded " + std::to_string(labelMgr->GetLabelCount()) +
                " symbols from " + args[1] + NEWLINE);
        else
            session.SendResponse("Error: Failed to load symbols from " + args[1] + NEWLINE);
    }
    else if (subcmd == "save" && args.size() >= 2)
    {
        if (labelMgr->SaveLabels(args[1]))
            session.SendResponse("Saved " + std::to_string(labelMgr->GetLabelCount()) +
                " symbols to " + args[1] + NEWLINE);
        else
            session.SendResponse("Error: Failed to save symbols to " + args[1] + NEWLINE);
    }
    else if (subcmd == "clear")
    {
        labelMgr->ClearAllLabels();
        session.SendResponse("All symbols cleared." + std::string(NEWLINE));
    }
    else if (subcmd == "info")
    {
        session.SendResponse("Symbol count: " + std::to_string(labelMgr->GetLabelCount()) + NEWLINE);
    }
    else
    {
        session.SendResponse("Unknown subcommand: " + subcmd + NEWLINE);
    }
}

// HandleStepOut — run until the current subroutine returns (mirrors the WebAPI
// stepout endpoint). Breakpoints are skipped for the walk; emulation ends paused.
void CLIProcessor::HandleStepOut(const ClientSession& session, const std::vector<std::string>& args)
{
    (void)args;  // No parameters

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    try
    {
        emulator->StepOut();

        Z80State* z80State = emulator->GetZ80State();
        std::stringstream ss;
        ss << "Step out completed" << NEWLINE;
        if (z80State)
        {
            ss << std::hex << std::uppercase << std::setfill('0');
            ss << "PC: $" << std::setw(4) << z80State->pc << "  SP: $" << std::setw(4) << z80State->sp << NEWLINE;
        }
        session.SendResponse(ss.str());
    }
    catch (const std::exception& e)
    {
        session.SendResponse(std::string("Step out failed: ") + e.what());
    }
}

// HandleSkipUntil — fast-forward execution until PC reaches the target address
// (mirrors the WebAPI skip_until endpoint). Breakpoints are skipped for the walk.
void CLIProcessor::HandleSkipUntil(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (args.empty())
    {
        session.SendResponse("Usage: skip_until <pc> [max_tstates]");
        return;
    }

    // Target address: 0x / $ / plain decimal or hex
    const std::string& targetStr = args[0];
    uint32_t target32 = 0;
    try
    {
        if (targetStr.rfind("0x", 0) == 0 || targetStr.rfind("0X", 0) == 0)
            target32 = static_cast<uint32_t>(std::stoul(targetStr.substr(2), nullptr, 16));
        else if (targetStr[0] == '$')
            target32 = static_cast<uint32_t>(std::stoul(targetStr.substr(1), nullptr, 16));
        else
            target32 = static_cast<uint32_t>(std::stoul(targetStr, nullptr, 0));
    }
    catch (...)
    {
        session.SendResponse("Invalid target address (expected hex or decimal).");
        return;
    }

    if (target32 > 0xFFFF)
    {
        session.SendResponse("Target address is out of the 16-bit address range.");
        return;
    }
    const uint16_t target = static_cast<uint16_t>(target32);

    // Safety budget: default 100 frames of emulated time (~2 s), hard cap 200 s
    EmulatorContext* context = emulator->GetContext();
    unsigned maxTStates = 0;
    if (args.size() >= 2)
    {
        try
        {
            maxTStates = std::stoul(args[1], nullptr, 0);
        }
        catch (...)
        {
            session.SendResponse("Invalid max_tstates value.");
            return;
        }
    }
    if (maxTStates == 0 && context)
    {
        maxTStates = context->config.frame * 100;
    }
    if (maxTStates == 0)
    {
        maxTStates = 6988800;  // Fallback if config is unavailable
    }
    if (maxTStates > 700000000u)
    {
        maxTStates = 700000000u;
    }

    emulator->RunUntilCondition([target](const Z80State& state) { return state.pc == target; }, maxTStates);

    Z80State* z80State = emulator->GetZ80State();
    const bool hit = z80State && z80State->pc == target;

    std::stringstream ss;
    ss << (hit ? "Reached target address" : "T-state budget exhausted before reaching target") << NEWLINE;
    ss << "Budget: " << std::dec << maxTStates << " t-states" << NEWLINE;
    if (z80State)
    {
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "PC: $" << std::setw(4) << z80State->pc << "  SP: $" << std::setw(4) << z80State->sp << NEWLINE;
    }
    session.SendResponse(ss.str());
}

// HandleAssemble — assemble Z80 source text (mirrors the WebAPI assemble
// endpoint). Bytes are shown; --write also stores them into emulator RAM.
void CLIProcessor::HandleAssemble(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    // assemble <addr> <code...> [--write]
    if (args.size() < 2)
    {
        session.SendResponse("Usage: assemble <addr> <code...> [--write]");
        return;
    }

    uint16_t address = 0;
    const std::string& addressStr = args[0];
    try
    {
        if (addressStr.rfind("0x", 0) == 0 || addressStr.rfind("0X", 0) == 0)
            address = static_cast<uint16_t>(std::stoul(addressStr.substr(2), nullptr, 16));
        else if (addressStr[0] == '$')
            address = static_cast<uint16_t>(std::stoul(addressStr.substr(1), nullptr, 16));
        else
            address = static_cast<uint16_t>(std::stoul(addressStr, nullptr, 0));
    }
    catch (...)
    {
        session.SendResponse("Invalid address format.");
        return;
    }

    bool write = false;
    std::string code;
    for (size_t i = 1; i < args.size(); i++)
    {
        if (args[i] == "--write")
        {
            write = true;
            continue;
        }
        if (!code.empty())
            code += " ";
        code += args[i];
    }

    if (code.empty())
    {
        session.SendResponse("No code provided.");
        return;
    }

    Z80TextAssembler assembler;
    AsmResult result = assembler.Assemble(code, address);

    if (!result.ok)
    {
        std::stringstream ss;
        ss << "Assembly failed (line " << result.error.line << "): " << result.error.message << NEWLINE;
        ss << "  Source: " << result.error.sourceLine;
        session.SendResponse(ss.str());
        return;
    }

    // Optional: write the emitted bytes into emulator RAM
    if (write)
    {
        Memory* memory = emulator->GetMemory();
        if (!memory)
        {
            session.SendResponse("Memory not available.");
            return;
        }

        uint32_t addr = result.startAddress;
        for (uint8_t b : result.bytes)
            memory->MemoryWriteFast(static_cast<uint16_t>((addr++) & 0xFFFF), b);
    }

    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "Assembled " << std::dec << result.bytes.size() << " bytes at $" << std::hex << std::setw(4)
       << result.startAddress << "-$" << std::setw(4) << result.endAddress << NEWLINE;

    for (const auto& line : result.lines)
    {
        ss << "  $" << std::setw(4) << line.address;
        if (!line.label.empty())
            ss << " " << line.label << ":";
        ss << "  " << line.source;
        if (!line.bytes.empty())
        {
            ss << "   ;";
            for (uint8_t b : line.bytes)
                ss << " " << std::setw(2) << static_cast<int>(b);
        }
        ss << NEWLINE;
    }

    if (!result.symbols.empty())
    {
        ss << "Symbols:" << NEWLINE;
        for (const auto& sym : result.symbols)
            ss << "  " << sym.first << " = $" << std::setw(4) << sym.second << NEWLINE;
    }

    if (write)
        ss << "Bytes written to emulator memory." << NEWLINE;

    session.SendResponse(ss.str());
}

// HandleListing — sjasmplus .lst source-listing navigation (mirrors the WebAPI
// listing/load, listing/source_at, listing/step_line and listing/run_to_line
// endpoints).
void CLIProcessor::HandleListing(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        session.SendResponse("Debug manager not available.");
        return;
    }

    ListingParser* parser = ctx->pDebugManager->GetListingParser();
    if (!parser)
    {
        session.SendResponse("Listing parser not available.");
        return;
    }

    const std::string subcommand = args.empty() ? "info" : args[0];

    if (subcommand == "load")
    {
        if (args.size() < 2)
        {
            session.SendResponse("Usage: listing load <path>");
            return;
        }

        if (parser->LoadListing(args[1]))
        {
            std::stringstream ss;
            ss << std::dec;
            ss << "Loaded " << parser->GetLineCount() << " lines (" << parser->GetCodeLineCount()
               << " code, " << parser->GetTotalBytes() << " bytes)" << NEWLINE;
            ss << std::hex << std::uppercase << std::setfill('0');
            ss << "Address range: $" << std::setw(4) << parser->GetMinAddress() << "-$" << std::setw(4)
               << parser->GetMaxAddress() << NEWLINE;
            session.SendResponse(ss.str());
        }
        else
        {
            session.SendResponse("Failed to load listing: " + args[1]);
        }
        return;
    }

    if (subcommand == "clear")
    {
        parser->Clear();
        session.SendResponse("Listing cleared.");
        return;
    }

    if (subcommand == "info")
    {
        if (!parser->IsLoaded())
        {
            session.SendResponse("No listing loaded. Use 'listing load <path>' first.");
            return;
        }

        std::stringstream ss;
        ss << std::dec;
        ss << "Source: " << parser->GetSourcePath() << NEWLINE;
        ss << "Lines: " << parser->GetLineCount() << " (" << parser->GetCodeLineCount() << " code)" << NEWLINE;
        ss << "Bytes: " << parser->GetTotalBytes() << NEWLINE;
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "Address range: $" << std::setw(4) << parser->GetMinAddress() << "-$" << std::setw(4)
           << parser->GetMaxAddress();
        session.SendResponse(ss.str());
        return;
    }

    if (!parser->IsLoaded())
    {
        session.SendResponse("No listing loaded. Use 'listing load <path>' first.");
        return;
    }

    Z80State* z80State = emulator->GetZ80State();
    if (!z80State)
    {
        session.SendResponse("Z80 state not available.");
        return;
    }

    if (subcommand == "source" || subcommand == "source_at")
    {
        // Address defaults to PC
        uint16_t address = z80State->pc;
        if (args.size() >= 2)
        {
            try
            {
                const std::string& addrStr = args[1];
                if (addrStr.rfind("0x", 0) == 0 || addrStr.rfind("0X", 0) == 0)
                    address = static_cast<uint16_t>(std::stoul(addrStr.substr(2), nullptr, 16));
                else if (addrStr[0] == '$')
                    address = static_cast<uint16_t>(std::stoul(addrStr.substr(1), nullptr, 16));
                else
                    address = static_cast<uint16_t>(std::stoul(addrStr, nullptr, 0));
            }
            catch (...)
            {
                session.SendResponse("Invalid address format.");
                return;
            }
        }

        const ListingLine* line = parser->FindLineByAddress(address);
        if (!line)
        {
            std::stringstream ss;
            ss << std::hex << std::uppercase << std::setfill('0');
            ss << "No source line covers $" << std::setw(4) << address;
            session.SendResponse(ss.str());
            return;
        }

        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "Line " << std::dec << line->lineNumber;
        if (line->hasCode)
            ss << "  $" << std::hex << std::setw(4) << line->addressStart << "-$" << std::setw(4) << line->addressEnd;
        ss << NEWLINE;
        ss << "  " << line->source;
        session.SendResponse(ss.str());
        return;
    }

    if (subcommand == "stepline" || subcommand == "step_line")
    {
        // Stop on the first instruction whose listing line differs from the starting line
        const ListingLine* startLine = parser->FindLineByAddress(z80State->pc);
        const int startLineNumber = startLine ? startLine->lineNumber : -1;

        const unsigned maxTStates = ctx->config.frame * 100;  // ~2 s of emulated time
        emulator->RunUntilCondition(
            [parser, startLineNumber](const Z80State& state) {
                const ListingLine* line = parser->FindLineByAddress(state.pc);
                return line != nullptr && line->lineNumber != startLineNumber;
            },
            maxTStates);

        z80State = emulator->GetZ80State();
        const ListingLine* endLine = z80State ? parser->FindLineByAddress(z80State->pc) : nullptr;
        const bool lineChanged = endLine != nullptr && endLine->lineNumber != startLineNumber;

        std::stringstream ss;
        ss << (lineChanged ? "Stepped to a different source line" : "Stopped without reaching a different source line")
           << NEWLINE;
        if (endLine)
            ss << "Line " << endLine->lineNumber << ": " << endLine->source << NEWLINE;
        if (z80State)
        {
            ss << std::hex << std::uppercase << std::setfill('0');
            ss << "PC: $" << std::setw(4) << z80State->pc;
        }
        session.SendResponse(ss.str());
        return;
    }

    if (subcommand == "runtoline" || subcommand == "run_to_line")
    {
        if (args.size() < 2)
        {
            session.SendResponse("Usage: listing runtoline <line>");
            return;
        }

        int lineNumber = 0;
        try
        {
            lineNumber = std::stoi(args[1]);
        }
        catch (...)
        {
            session.SendResponse("Invalid line number.");
            return;
        }

        const ListingLine* target = parser->FindNextCodeLine(lineNumber);
        if (!target)
        {
            session.SendResponse("No code line at or after line " + std::to_string(lineNumber) +
                                 " in loaded listing");
            return;
        }

        const uint16_t targetAddress = target->addressStart;
        const bool alreadyAt = z80State->pc == targetAddress;

        if (!alreadyAt)
        {
            const unsigned maxTStates = ctx->config.frame * 500;  // ~10 s of emulated time
            emulator->RunUntilCondition(
                [targetAddress](const Z80State& state) { return state.pc == targetAddress; }, maxTStates);
        }

        z80State = emulator->GetZ80State();
        const bool reached = z80State && z80State->pc == targetAddress;

        std::stringstream ss;
        ss << (alreadyAt ? "Already at target line" : (reached ? "Reached target line"
                                                                : "Safety limit reached before target line"))
           << NEWLINE;
        ss << "Target: line " << target->lineNumber << ": " << target->source << NEWLINE;
        if (z80State)
        {
            ss << std::hex << std::uppercase << std::setfill('0');
            ss << "PC: $" << std::setw(4) << z80State->pc;
        }
        session.SendResponse(ss.str());
        return;
    }

    session.SendResponse("Unknown subcommand: " + subcommand +
                         " (expected load|clear|info|source|stepline|runtoline)");
}
