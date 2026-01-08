// CLI Debug and Stepping Commands
// Extracted from cli-processor.cpp - 2026-01-08

#include "cli-processor.h"

#include <3rdparty/message-center/messagecenter.h>
#include <debugger/debugmanager.h>
#include <debugger/disassembler/z80disasm.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include <emulator/notifications.h>
#include <emulator/platform.h>

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

