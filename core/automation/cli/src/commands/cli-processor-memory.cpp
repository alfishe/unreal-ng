// CLI Memory Inspection Commands
// Extracted from cli-processor.cpp - 2026-01-08

#include "cli-processor.h"

#include <common/dumphelper.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include <emulator/memory/memoryaccesstracker.h>
#include <emulator/platform.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

// HandleMemory - Unified memory command dispatcher
// Supports two addressing modes:
// 1. Z80 address space: memory read <addr> [len], memory write <addr> <bytes...>
// 2. Physical pages: memory read <type> <page> <offset> [len], etc.
void CLIProcessor::HandleMemory(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (args.empty())
    {
        ShowMemoryHelp(session);
        return;
    }

    Memory* memory = emulator->GetMemory();
    if (!memory)
    {
        session.SendResponse("Memory not available" + std::string(NEWLINE));
        return;
    }

    const std::string& subcommand = args[0];

    if (subcommand == "read")
    {
        HandleMemoryRead(session, memory, args);
    }
    else if (subcommand == "write")
    {
        HandleMemoryWrite(session, memory, args);
    }
    else if (subcommand == "dump")
    {
        HandleMemoryDump(session, memory, args);
    }
    else if (subcommand == "save")
    {
        HandleMemorySave(session, memory, args);
    }
    else if (subcommand == "load")
    {
        HandleMemoryLoad(session, memory, args);
    }
    else if (subcommand == "fill")
    {
        HandleMemoryFill(session, memory, args);
    }
    else if (subcommand == "info")
    {
        HandleMemoryInfo(session, memory);
    }
    else
    {
        // Legacy mode: treat first arg as address for backwards compatibility
        // memory 0x8000 -> memory read 0x8000
        uint16_t address;
        if (ParseAddress(args[0], address))
        {
            // Default length is 128 bytes
            uint16_t length = 128;
            if (args.size() > 1)
            {
                if (!ParseAddress(args[1], length, 0xFFFF))
                {
                    session.SendResponse("Invalid length format" + std::string(NEWLINE));
                    return;
                }
            }
            DumpZ80Memory(session, memory, address, length);
        }
        else
        {
            session.SendResponse("Unknown subcommand: " + subcommand + NEWLINE);
            ShowMemoryHelp(session);
        }
    }
}

// Show memory command help
void CLIProcessor::ShowMemoryHelp(const ClientSession& session)
{
    std::ostringstream oss;
    oss << "Usage: memory <subcommand> [args]" << NEWLINE;
    oss << NEWLINE;
    oss << "Z80 Address Space:" << NEWLINE;
    oss << "  memory read <addr> [len]          - Read from Z80 address space" << NEWLINE;
    oss << "  memory write <addr> <bytes...>    - Write to Z80 address space" << NEWLINE;
    oss << NEWLINE;
    oss << "Physical Pages (ram|rom|cache|misc):" << NEWLINE;
    oss << "  memory read <type> <page> <offset> [len]   - Read from physical page" << NEWLINE;
    oss << "  memory write <type> <page> <offset> <bytes...> [--force]  - Write to page" << NEWLINE;
    oss << "  memory dump <type> <page>                  - Dump entire 16KB page" << NEWLINE;
    oss << "  memory fill <type> <page> <offset> <len> <byte>  - Fill region" << NEWLINE;
    oss << NEWLINE;
    oss << "File Operations:" << NEWLINE;
    oss << "  memory save bank <N> <file>       - Save Z80 bank to file" << NEWLINE;
    oss << "  memory save <type> <page> <file>  - Save physical page to file" << NEWLINE;
    oss << "  memory load bank <N> <file>       - Load file into Z80 bank" << NEWLINE;
    oss << "  memory load <type> <page> <file> [--force] - Load file into page" << NEWLINE;
    oss << NEWLINE;
    oss << "Configuration:" << NEWLINE;
    oss << "  memory info                       - Show memory configuration" << NEWLINE;
    oss << NEWLINE;
    oss << "Page Types: ram (0-255), rom (0-3), cache (0-15), misc (0-15)" << NEWLINE;
    oss << "Address formats: 0x8000, $8000, #8000, 32768" << NEWLINE;
    session.SendResponse(oss.str());
}

// Determine if a string is a page type
static bool IsPageType(const std::string& str)
{
    return str == "ram" || str == "rom" || str == "cache" || str == "misc";
}

// Parse page type string to enum-like value
static int ParsePageType(const std::string& str)
{
    if (str == "ram") return 0;
    if (str == "rom") return 1;
    if (str == "cache") return 2;
    if (str == "misc") return 3;
    return -1;
}

// memory read <addr> [len]  OR  memory read <type> <page> <offset> [len]
void CLIProcessor::HandleMemoryRead(const ClientSession& session, Memory* memory, const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse("Usage: memory read <addr> [len]  OR  memory read <type> <page> <offset> [len]" + std::string(NEWLINE));
        return;
    }

    // Check if second arg is a page type (physical page mode) or address (Z80 mode)
    if (IsPageType(args[1]))
    {
        // Physical page mode: memory read <type> <page> <offset> [len]
        if (args.size() < 4)
        {
            session.SendResponse("Usage: memory read <type> <page> <offset> [len]" + std::string(NEWLINE));
            return;
        }

        int pageType = ParsePageType(args[1]);
        uint16_t page, offset;
        uint16_t length = 128;

        if (!ParseAddress(args[2], page, 0xFF))
        {
            session.SendResponse("Invalid page number (0-255)" + std::string(NEWLINE));
            return;
        }

        if (!ParseAddress(args[3], offset, 0x3FFF))
        {
            session.SendResponse("Invalid offset (0-16383)" + std::string(NEWLINE));
            return;
        }

        if (args.size() > 4)
        {
            if (!ParseAddress(args[4], length, 0x4000))
            {
                session.SendResponse("Invalid length (max 16384)" + std::string(NEWLINE));
                return;
            }
        }

        // Clamp length to not exceed page boundary
        if (offset + length > 0x4000)
        {
            length = 0x4000 - offset;
        }

        DumpPhysicalPage(session, memory, pageType, page, offset, length);
    }
    else
    {
        // Z80 mode: memory read <addr> [len]
        uint16_t address;
        if (!ParseAddress(args[1], address))
        {
            session.SendResponse("Invalid address format" + std::string(NEWLINE));
            return;
        }

        uint16_t length = 128;
        if (args.size() > 2)
        {
            if (!ParseAddress(args[2], length, 0xFFFF))
            {
                session.SendResponse("Invalid length format" + std::string(NEWLINE));
                return;
            }
        }

        DumpZ80Memory(session, memory, address, length);
    }
}

// Dump Z80 address space memory
void CLIProcessor::DumpZ80Memory(const ClientSession& session, Memory* memory, uint16_t address, uint16_t length)
{
    std::ostringstream oss;
    oss << "Memory at 0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << address << ":" << NEWLINE;

    uint16_t remaining = length;
    uint16_t currentAddr = address;

    while (remaining > 0)
    {
        uint16_t rowLen = std::min((uint16_t)16, remaining);
        
        oss << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << currentAddr << ": ";

        // Hex values
        for (uint16_t i = 0; i < rowLen; ++i)
        {
            uint8_t value = memory->DirectReadFromZ80Memory(currentAddr + i);
            oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(value) << " ";
        }

        // Padding for incomplete rows
        for (uint16_t i = rowLen; i < 16; ++i)
        {
            oss << "   ";
        }

        oss << " | ";

        // ASCII representation
        for (uint16_t i = 0; i < rowLen; ++i)
        {
            uint8_t value = memory->DirectReadFromZ80Memory(currentAddr + i);
            oss << (value >= 32 && value <= 126 ? static_cast<char>(value) : '.');
        }

        oss << NEWLINE;

        currentAddr += rowLen;
        remaining -= rowLen;
    }

    session.SendResponse(oss.str());
}

// Dump physical page memory
void CLIProcessor::DumpPhysicalPage(const ClientSession& session, Memory* memory, int pageType, uint16_t page, uint16_t offset, uint16_t length)
{
    uint8_t* pagePtr = nullptr;
    const char* typeName = nullptr;

    switch (pageType)
    {
        case 0:  // RAM
            if (page >= MAX_RAM_PAGES)
            {
                session.SendResponse("Invalid RAM page (max " + std::to_string(MAX_RAM_PAGES - 1) + ")" + std::string(NEWLINE));
                return;
            }
            pagePtr = memory->RAMPageAddress(page);
            typeName = "RAM";
            break;
        case 1:  // ROM
            if (page >= MAX_ROM_PAGES)
            {
                session.SendResponse("Invalid ROM page (max " + std::to_string(MAX_ROM_PAGES - 1) + ")" + std::string(NEWLINE));
                return;
            }
            pagePtr = memory->ROMPageHostAddress(page);
            typeName = "ROM";
            break;
        case 2:  // Cache
            if (page >= MAX_CACHE_PAGES)
            {
                session.SendResponse("Invalid cache page (max " + std::to_string(MAX_CACHE_PAGES - 1) + ")" + std::string(NEWLINE));
                return;
            }
            pagePtr = memory->CacheBase() + (page * PAGE_SIZE);
            typeName = "Cache";
            break;
        case 3:  // Misc
            if (page >= MAX_MISC_PAGES)
            {
                session.SendResponse("Invalid misc page (max " + std::to_string(MAX_MISC_PAGES - 1) + ")" + std::string(NEWLINE));
                return;
            }
            pagePtr = memory->MiscBase() + (page * PAGE_SIZE);
            typeName = "Misc";
            break;
        default:
            session.SendResponse("Unknown page type" + std::string(NEWLINE));
            return;
    }

    if (!pagePtr)
    {
        session.SendResponse("Page not available" + std::string(NEWLINE));
        return;
    }

    std::ostringstream oss;
    oss << typeName << " page " << page << " at offset 0x" << std::hex << std::uppercase 
        << std::setw(4) << std::setfill('0') << offset << ":" << NEWLINE;

    uint16_t remaining = length;
    uint16_t currentOffset = offset;

    while (remaining > 0)
    {
        uint16_t rowLen = std::min((uint16_t)16, remaining);
        
        oss << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << currentOffset << ": ";

        // Hex values
        for (uint16_t i = 0; i < rowLen; ++i)
        {
            uint8_t value = pagePtr[currentOffset + i];
            oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(value) << " ";
        }

        // Padding for incomplete rows
        for (uint16_t i = rowLen; i < 16; ++i)
        {
            oss << "   ";
        }

        oss << " | ";

        // ASCII representation
        for (uint16_t i = 0; i < rowLen; ++i)
        {
            uint8_t value = pagePtr[currentOffset + i];
            oss << (value >= 32 && value <= 126 ? static_cast<char>(value) : '.');
        }

        oss << NEWLINE;

        currentOffset += rowLen;
        remaining -= rowLen;
    }

    session.SendResponse(oss.str());
}

// memory write <addr> <bytes...>  OR  memory write <type> <page> <offset> <bytes...> [--force]
void CLIProcessor::HandleMemoryWrite(const ClientSession& session, Memory* memory, const std::vector<std::string>& args)
{
    if (args.size() < 3)
    {
        session.SendResponse("Usage: memory write <addr> <bytes...>  OR  memory write <type> <page> <offset> <bytes...> [--force]" + std::string(NEWLINE));
        return;
    }

    // Check if second arg is a page type (physical page mode) or address (Z80 mode)
    if (IsPageType(args[1]))
    {
        // Physical page mode
        if (args.size() < 5)
        {
            session.SendResponse("Usage: memory write <type> <page> <offset> <bytes...> [--force]" + std::string(NEWLINE));
            return;
        }

        int pageType = ParsePageType(args[1]);
        uint16_t page, offset;

        if (!ParseAddress(args[2], page, 0xFF))
        {
            session.SendResponse("Invalid page number" + std::string(NEWLINE));
            return;
        }

        if (!ParseAddress(args[3], offset, 0x3FFF))
        {
            session.SendResponse("Invalid offset" + std::string(NEWLINE));
            return;
        }

        // Check for --force flag (required for ROM)
        bool forceFlag = false;
        std::vector<uint8_t> bytes;
        
        for (size_t i = 4; i < args.size(); ++i)
        {
            if (args[i] == "--force")
            {
                forceFlag = true;
                continue;
            }
            
            uint16_t byteVal;
            if (!ParseAddress(args[i], byteVal, 0xFF))
            {
                session.SendResponse("Invalid byte value: " + args[i] + NEWLINE);
                return;
            }
            bytes.push_back(static_cast<uint8_t>(byteVal));
        }

        if (bytes.empty())
        {
            session.SendResponse("No bytes to write" + std::string(NEWLINE));
            return;
        }

        // ROM requires --force
        if (pageType == 1 && !forceFlag)
        {
            session.SendResponse("Error: ROM write requires --force flag" + std::string(NEWLINE));
            return;
        }

        WriteToPhysicalPage(session, memory, pageType, page, offset, bytes);
    }
    else
    {
        // Z80 mode: memory write <addr> <bytes...>
        uint16_t address;
        if (!ParseAddress(args[1], address))
        {
            session.SendResponse("Invalid address format" + std::string(NEWLINE));
            return;
        }

        std::vector<uint8_t> bytes;
        for (size_t i = 2; i < args.size(); ++i)
        {
            uint16_t byteVal;
            if (!ParseAddress(args[i], byteVal, 0xFF))
            {
                session.SendResponse("Invalid byte value: " + args[i] + NEWLINE);
                return;
            }
            bytes.push_back(static_cast<uint8_t>(byteVal));
        }

        if (bytes.empty())
        {
            session.SendResponse("No bytes to write" + std::string(NEWLINE));
            return;
        }

        // Write to Z80 address space
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            uint16_t addr = address + static_cast<uint16_t>(i);
            memory->DirectWriteToZ80Memory(addr, bytes[i]);
        }

        std::ostringstream oss;
        oss << "Wrote " << bytes.size() << " bytes at 0x" << std::hex << std::uppercase 
            << std::setw(4) << std::setfill('0') << address << NEWLINE;
        session.SendResponse(oss.str());
    }
}

// Write to physical page
void CLIProcessor::WriteToPhysicalPage(const ClientSession& session, Memory* memory, int pageType, 
                                        uint16_t page, uint16_t offset, const std::vector<uint8_t>& bytes)
{
    uint8_t* pagePtr = nullptr;
    const char* typeName = nullptr;

    switch (pageType)
    {
        case 0:  // RAM
            if (page >= MAX_RAM_PAGES)
            {
                session.SendResponse("Invalid RAM page" + std::string(NEWLINE));
                return;
            }
            pagePtr = memory->RAMPageAddress(page);
            typeName = "RAM";
            break;
        case 1:  // ROM
            if (page >= MAX_ROM_PAGES)
            {
                session.SendResponse("Invalid ROM page" + std::string(NEWLINE));
                return;
            }
            pagePtr = memory->ROMPageHostAddress(page);
            typeName = "ROM";
            break;
        case 2:  // Cache
            if (page >= MAX_CACHE_PAGES)
            {
                session.SendResponse("Invalid cache page" + std::string(NEWLINE));
                return;
            }
            pagePtr = memory->CacheBase() + (page * PAGE_SIZE);
            typeName = "Cache";
            break;
        case 3:  // Misc
            if (page >= MAX_MISC_PAGES)
            {
                session.SendResponse("Invalid misc page" + std::string(NEWLINE));
                return;
            }
            pagePtr = memory->MiscBase() + (page * PAGE_SIZE);
            typeName = "Misc";
            break;
        default:
            session.SendResponse("Unknown page type" + std::string(NEWLINE));
            return;
    }

    if (!pagePtr)
    {
        session.SendResponse("Page not available" + std::string(NEWLINE));
        return;
    }

    // Check bounds
    if (offset + bytes.size() > PAGE_SIZE)
    {
        session.SendResponse("Write would exceed page boundary" + std::string(NEWLINE));
        return;
    }

    // Write bytes
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        pagePtr[offset + i] = bytes[i];
    }

    std::ostringstream oss;
    oss << "Wrote " << bytes.size() << " bytes to " << typeName << " page " << page 
        << " at offset 0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << offset << NEWLINE;
    session.SendResponse(oss.str());
}

// memory dump <type> <page>
void CLIProcessor::HandleMemoryDump(const ClientSession& session, Memory* memory, const std::vector<std::string>& args)
{
    if (args.size() < 3)
    {
        session.SendResponse("Usage: memory dump <type> <page>" + std::string(NEWLINE));
        return;
    }

    if (!IsPageType(args[1]))
    {
        session.SendResponse("Invalid page type. Use: ram, rom, cache, misc" + std::string(NEWLINE));
        return;
    }

    int pageType = ParsePageType(args[1]);
    uint16_t page;

    if (!ParseAddress(args[2], page, 0xFF))
    {
        session.SendResponse("Invalid page number" + std::string(NEWLINE));
        return;
    }

    // Dump entire page (16KB)
    DumpPhysicalPage(session, memory, pageType, page, 0, PAGE_SIZE);
}

// memory save bank <N> <file>  OR  memory save <type> <page> <file>
void CLIProcessor::HandleMemorySave(const ClientSession& session, Memory* memory, const std::vector<std::string>& args)
{
    if (args.size() < 4)
    {
        session.SendResponse("Usage: memory save bank <N> <file>  OR  memory save <type> <page> <file>" + std::string(NEWLINE));
        return;
    }

    const std::string& target = args[1];
    
    if (target == "bank")
    {
        // Save Z80 bank
        uint16_t bankNum;
        if (!ParseAddress(args[2], bankNum, 3))
        {
            session.SendResponse("Invalid bank number (0-3)" + std::string(NEWLINE));
            return;
        }

        const std::string& filename = args[3];
        
        // Get bank base address
        uint16_t bankAddr = bankNum * 0x4000;
        
        // Read 16KB from Z80 address space
        std::vector<uint8_t> buffer(PAGE_SIZE);
        for (size_t i = 0; i < PAGE_SIZE; ++i)
        {
            buffer[i] = memory->DirectReadFromZ80Memory(bankAddr + static_cast<uint16_t>(i));
        }

        // Write to file
        std::ofstream file(filename, std::ios::binary);
        if (!file)
        {
            session.SendResponse("Failed to create file: " + filename + NEWLINE);
            return;
        }

        file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        file.close();

        std::ostringstream oss;
        oss << "Saved " << PAGE_SIZE << " bytes to " << filename << NEWLINE;
        session.SendResponse(oss.str());
    }
    else if (IsPageType(target))
    {
        // Save physical page
        int pageType = ParsePageType(target);
        uint16_t page;

        if (!ParseAddress(args[2], page, 0xFF))
        {
            session.SendResponse("Invalid page number" + std::string(NEWLINE));
            return;
        }

        const std::string& filename = args[3];
        
        uint8_t* pagePtr = nullptr;
        const char* typeName = nullptr;

        switch (pageType)
        {
            case 0:
                if (page >= MAX_RAM_PAGES) { session.SendResponse("Invalid RAM page" + std::string(NEWLINE)); return; }
                pagePtr = memory->RAMPageAddress(page);
                typeName = "RAM";
                break;
            case 1:
                if (page >= MAX_ROM_PAGES) { session.SendResponse("Invalid ROM page" + std::string(NEWLINE)); return; }
                pagePtr = memory->ROMPageHostAddress(page);
                typeName = "ROM";
                break;
            case 2:
                if (page >= MAX_CACHE_PAGES) { session.SendResponse("Invalid cache page" + std::string(NEWLINE)); return; }
                pagePtr = memory->CacheBase() + (page * PAGE_SIZE);
                typeName = "Cache";
                break;
            case 3:
                if (page >= MAX_MISC_PAGES) { session.SendResponse("Invalid misc page" + std::string(NEWLINE)); return; }
                pagePtr = memory->MiscBase() + (page * PAGE_SIZE);
                typeName = "Misc";
                break;
        }

        if (!pagePtr)
        {
            session.SendResponse("Page not available" + std::string(NEWLINE));
            return;
        }

        std::ofstream file(filename, std::ios::binary);
        if (!file)
        {
            session.SendResponse("Failed to create file: " + filename + NEWLINE);
            return;
        }

        file.write(reinterpret_cast<const char*>(pagePtr), PAGE_SIZE);
        file.close();

        std::ostringstream oss;
        oss << "Saved " << typeName << " page " << page << " (" << PAGE_SIZE << " bytes) to " << filename << NEWLINE;
        session.SendResponse(oss.str());
    }
    else
    {
        session.SendResponse("Invalid target. Use 'bank' or page type (ram, rom, cache, misc)" + std::string(NEWLINE));
    }
}

// memory load bank <N> <file>  OR  memory load <type> <page> <file> [--force]
void CLIProcessor::HandleMemoryLoad(const ClientSession& session, Memory* memory, const std::vector<std::string>& args)
{
    if (args.size() < 4)
    {
        session.SendResponse("Usage: memory load bank <N> <file>  OR  memory load <type> <page> <file> [--force]" + std::string(NEWLINE));
        return;
    }

    const std::string& target = args[1];
    bool forceFlag = false;
    
    // Check for --force flag
    for (const auto& arg : args)
    {
        if (arg == "--force") forceFlag = true;
    }

    if (target == "bank")
    {
        // Load into Z80 bank
        uint16_t bankNum;
        if (!ParseAddress(args[2], bankNum, 3))
        {
            session.SendResponse("Invalid bank number (0-3)" + std::string(NEWLINE));
            return;
        }

        const std::string& filename = args[3];
        
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file)
        {
            session.SendResponse("Failed to open file: " + filename + NEWLINE);
            return;
        }

        std::streamsize fileSize = file.tellg();
        if (fileSize > PAGE_SIZE)
        {
            session.SendResponse("File too large (max 16384 bytes)" + std::string(NEWLINE));
            return;
        }

        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(fileSize);
        file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
        file.close();

        // Write to Z80 address space
        uint16_t bankAddr = bankNum * 0x4000;
        for (size_t i = 0; i < buffer.size(); ++i)
        {
            memory->DirectWriteToZ80Memory(bankAddr + static_cast<uint16_t>(i), buffer[i]);
        }

        std::ostringstream oss;
        oss << "Loaded " << buffer.size() << " bytes into bank " << bankNum << NEWLINE;
        session.SendResponse(oss.str());
    }
    else if (IsPageType(target))
    {
        int pageType = ParsePageType(target);
        uint16_t page;

        if (!ParseAddress(args[2], page, 0xFF))
        {
            session.SendResponse("Invalid page number" + std::string(NEWLINE));
            return;
        }

        // ROM requires --force
        if (pageType == 1 && !forceFlag)
        {
            session.SendResponse("Error: ROM load requires --force flag" + std::string(NEWLINE));
            return;
        }

        const std::string& filename = args[3];
        
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file)
        {
            session.SendResponse("Failed to open file: " + filename + NEWLINE);
            return;
        }

        std::streamsize fileSize = file.tellg();
        if (fileSize > PAGE_SIZE)
        {
            session.SendResponse("File too large (max 16384 bytes)" + std::string(NEWLINE));
            return;
        }

        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(fileSize);
        file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
        file.close();

        uint8_t* pagePtr = nullptr;
        const char* typeName = nullptr;

        switch (pageType)
        {
            case 0:
                if (page >= MAX_RAM_PAGES) { session.SendResponse("Invalid RAM page" + std::string(NEWLINE)); return; }
                pagePtr = memory->RAMPageAddress(page);
                typeName = "RAM";
                break;
            case 1:
                if (page >= MAX_ROM_PAGES) { session.SendResponse("Invalid ROM page" + std::string(NEWLINE)); return; }
                pagePtr = memory->ROMPageHostAddress(page);
                typeName = "ROM";
                break;
            case 2:
                if (page >= MAX_CACHE_PAGES) { session.SendResponse("Invalid cache page" + std::string(NEWLINE)); return; }
                pagePtr = memory->CacheBase() + (page * PAGE_SIZE);
                typeName = "Cache";
                break;
            case 3:
                if (page >= MAX_MISC_PAGES) { session.SendResponse("Invalid misc page" + std::string(NEWLINE)); return; }
                pagePtr = memory->MiscBase() + (page * PAGE_SIZE);
                typeName = "Misc";
                break;
        }

        if (!pagePtr)
        {
            session.SendResponse("Page not available" + std::string(NEWLINE));
            return;
        }

        // Copy data to page
        std::memcpy(pagePtr, buffer.data(), buffer.size());

        std::ostringstream oss;
        oss << "Loaded " << buffer.size() << " bytes into " << typeName << " page " << page << NEWLINE;
        session.SendResponse(oss.str());
    }
    else
    {
        session.SendResponse("Invalid target. Use 'bank' or page type (ram, rom, cache, misc)" + std::string(NEWLINE));
    }
}

// memory fill <type> <page> <offset> <len> <byte>
void CLIProcessor::HandleMemoryFill(const ClientSession& session, Memory* memory, const std::vector<std::string>& args)
{
    if (args.size() < 6)
    {
        session.SendResponse("Usage: memory fill <type> <page> <offset> <len> <byte>" + std::string(NEWLINE));
        return;
    }

    if (!IsPageType(args[1]))
    {
        session.SendResponse("Invalid page type. Use: ram, rom, cache, misc" + std::string(NEWLINE));
        return;
    }

    int pageType = ParsePageType(args[1]);
    uint16_t page, offset, length, byteVal;

    if (!ParseAddress(args[2], page, 0xFF))
    {
        session.SendResponse("Invalid page number" + std::string(NEWLINE));
        return;
    }

    if (!ParseAddress(args[3], offset, 0x3FFF))
    {
        session.SendResponse("Invalid offset" + std::string(NEWLINE));
        return;
    }

    if (!ParseAddress(args[4], length, 0x4000))
    {
        session.SendResponse("Invalid length" + std::string(NEWLINE));
        return;
    }

    if (!ParseAddress(args[5], byteVal, 0xFF))
    {
        session.SendResponse("Invalid byte value" + std::string(NEWLINE));
        return;
    }

    // ROM fill would require --force but we don't support it for fill
    if (pageType == 1)
    {
        session.SendResponse("Error: Cannot fill ROM pages" + std::string(NEWLINE));
        return;
    }

    uint8_t* pagePtr = nullptr;
    const char* typeName = nullptr;

    switch (pageType)
    {
        case 0:
            if (page >= MAX_RAM_PAGES) { session.SendResponse("Invalid RAM page" + std::string(NEWLINE)); return; }
            pagePtr = memory->RAMPageAddress(page);
            typeName = "RAM";
            break;
        case 2:
            if (page >= MAX_CACHE_PAGES) { session.SendResponse("Invalid cache page" + std::string(NEWLINE)); return; }
            pagePtr = memory->CacheBase() + (page * PAGE_SIZE);
            typeName = "Cache";
            break;
        case 3:
            if (page >= MAX_MISC_PAGES) { session.SendResponse("Invalid misc page" + std::string(NEWLINE)); return; }
            pagePtr = memory->MiscBase() + (page * PAGE_SIZE);
            typeName = "Misc";
            break;
    }

    if (!pagePtr)
    {
        session.SendResponse("Page not available" + std::string(NEWLINE));
        return;
    }

    // Clamp length to not exceed page boundary
    if (offset + length > PAGE_SIZE)
    {
        length = PAGE_SIZE - offset;
    }

    // Fill memory
    std::memset(pagePtr + offset, static_cast<uint8_t>(byteVal), length);

    std::ostringstream oss;
    oss << "Filled " << length << " bytes with 0x" << std::hex << std::uppercase 
        << std::setw(2) << std::setfill('0') << byteVal 
        << " in " << typeName << " page " << std::dec << page 
        << " at offset 0x" << std::hex << std::setw(4) << std::setfill('0') << offset << NEWLINE;
    session.SendResponse(oss.str());
}

// memory info
void CLIProcessor::HandleMemoryInfo(const ClientSession& session, Memory* memory)
{
    std::ostringstream oss;
    oss << "Memory Configuration:" << NEWLINE;
    oss << "  RAM:   pages 0-" << (MAX_RAM_PAGES - 1) << " (" << (MAX_RAM_PAGES * 16) << " KB)" << NEWLINE;
    oss << "  ROM:   pages 0-" << (MAX_ROM_PAGES - 1) << " (" << (MAX_ROM_PAGES * 16) << " KB)" << NEWLINE;
    oss << "  Cache: pages 0-" << (MAX_CACHE_PAGES - 1) << " (" << (MAX_CACHE_PAGES * 16) << " KB)" << NEWLINE;
    oss << "  Misc:  pages 0-" << (MAX_MISC_PAGES - 1) << " (" << (MAX_MISC_PAGES * 16) << " KB)" << NEWLINE;
    oss << NEWLINE;
    oss << "Current Z80 Bank Mapping:" << NEWLINE;

    for (int bank = 0; bank < 4; ++bank)
    {
        uint16_t startAddr = bank * 0x4000;
        uint16_t endAddr = startAddr + 0x3FFF;
        
        oss << "  Bank " << bank << " (0x" << std::hex << std::uppercase 
            << std::setw(4) << std::setfill('0') << startAddr << "-0x" 
            << std::setw(4) << std::setfill('0') << endAddr << "): ";
        
        std::string bankName = memory->GetCurrentBankName(bank);
        oss << bankName << NEWLINE;
    }

    session.SendResponse(oss.str());
}

// HandleRegisters - lines 1272-1361
void CLIProcessor::HandleRegisters(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    // Get the Z80 state from the emulator
    Z80State* z80State = emulator->GetZ80State();
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state.");
        return;
    }

    // Format the register values
    std::stringstream ss;
    ss << "Z80 Registers:" << NEWLINE;
    ss << "=============" << NEWLINE << NEWLINE;

    // Main register pairs and alternate registers side by side
    ss << "Main registers:                     Alternate registers:" << NEWLINE;
    ss << std::hex << std::uppercase << std::setfill('0');

    // Access alternate registers from the shadow registers in Z80State
    ss << "  AF: " << std::setw(4) << z80State->af << "  (A: " << std::setw(2) << static_cast<int>(z80State->a)
       << ", F: " << std::setw(2) << static_cast<int>(z80State->f) << ")";
    ss << "           AF': " << std::setw(4) << z80State->alt.af << "  (A': " << std::setw(2)
       << static_cast<int>(z80State->alt.a) << ", F': " << std::setw(2) << static_cast<int>(z80State->alt.f) << ")"
       << NEWLINE;

    ss << "  BC: " << std::setw(4) << z80State->bc << "  (B: " << std::setw(2) << static_cast<int>(z80State->b)
       << ", C: " << std::setw(2) << static_cast<int>(z80State->c) << ")";
    ss << "           BC': " << std::setw(4) << z80State->alt.bc << "  (B': " << std::setw(2)
       << static_cast<int>(z80State->alt.b) << ", C': " << std::setw(2) << static_cast<int>(z80State->alt.c) << ")"
       << NEWLINE;

    ss << "  DE: " << std::setw(4) << z80State->de << "  (D: " << std::setw(2) << static_cast<int>(z80State->d)
       << ", E: " << std::setw(2) << static_cast<int>(z80State->e) << ")";
    ss << "           DE': " << std::setw(4) << z80State->alt.de << "  (D': " << std::setw(2)
       << static_cast<int>(z80State->alt.d) << ", E': " << std::setw(2) << static_cast<int>(z80State->alt.e) << ")"
       << NEWLINE;

    ss << "  HL: " << std::setw(4) << z80State->hl << "  (H: " << std::setw(2) << static_cast<int>(z80State->h)
       << ", L: " << std::setw(2) << static_cast<int>(z80State->l) << ")";
    ss << "           HL': " << std::setw(4) << z80State->alt.hl << "  (H': " << std::setw(2)
       << static_cast<int>(z80State->alt.h) << ", L': " << std::setw(2) << static_cast<int>(z80State->alt.l) << ")"
       << NEWLINE;

    ss << NEWLINE;

    // Index and special registers in two columns
    ss << "Index registers:                    Special registers:" << NEWLINE;
    ss << "  IX: " << std::setw(4) << z80State->ix << "  (IXH: " << std::setw(2) << static_cast<int>(z80State->xh)
       << ", IXL: " << std::setw(2) << static_cast<int>(z80State->xl) << ")";
    ss << "       PC: " << std::setw(4) << z80State->pc << NEWLINE;

    ss << "  IY: " << std::setw(4) << z80State->iy << "  (IYH: " << std::setw(2) << static_cast<int>(z80State->yh)
       << ", IYL: " << std::setw(2) << static_cast<int>(z80State->yl) << ")";
    ss << "       SP: " << std::setw(4) << z80State->sp << NEWLINE;

    // Empty line for IR and first line of flags
    ss << "                                     IR: " << std::setw(4) << z80State->ir_ << "  (I: " << std::setw(2)
       << static_cast<int>(z80State->i) << ", R: " << std::setw(2) << static_cast<int>(z80State->r_low) << ")"
       << NEWLINE;
    ss << NEWLINE;

    // Flags and interrupt state in two columns
    ss << "Flags (" << std::setw(2) << static_cast<int>(z80State->f) << "):                         Interrupt state:\n";
    ss << "  S: " << ((z80State->f & 0x80) ? "1" : "0") << " (Sign)";
    ss << "                        IFF1: " << (z80State->iff1 ? "Enabled" : "Disabled") << NEWLINE;

    ss << "  Z: " << ((z80State->f & 0x40) ? "1" : "0") << " (Zero)";
    ss << "                        IFF2: " << (z80State->iff2 ? "Enabled" : "Disabled") << NEWLINE;

    ss << "  5: " << ((z80State->f & 0x20) ? "1" : "0") << " (Unused bit 5)";
    ss << "                HALT: " << (z80State->halted ? "Yes" : "No") << NEWLINE;

    ss << "  H: " << ((z80State->f & 0x10) ? "1" : "0") << " (Half-carry)" << NEWLINE;
    ss << "  3: " << ((z80State->f & 0x08) ? "1" : "0") << " (Unused bit 3)" << NEWLINE;
    ss << "  P/V: " << ((z80State->f & 0x04) ? "1" : "0") << " (Parity/Overflow)" << NEWLINE;
    ss << "  N: " << ((z80State->f & 0x02) ? "1" : "0") << " (Add/Subtract)" << NEWLINE;
    ss << "  C: " << ((z80State->f & 0x01) ? "1" : "0") << " (Carry)";

    // Send the formatted register dump
    session.SendResponse(ss.str());
}

// HandleMemCounters - lines 2282-2503
void CLIProcessor::HandleMemCounters(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("Error: No emulator selected" + std::string(NEWLINE));
        return;
    }

    // Check for save command first
    if (!args.empty() && args[0] == "save")
    {
        std::string outputPath = "";
        bool singleFile = false;
        std::vector<std::string> filterPages;

        // Parse options
        for (size_t i = 1; i < args.size(); i++)
        {
            if (args[i] == "--single-file" || args[i] == "-s")
            {
                singleFile = true;
            }
            else if (args[i] == "--output" || args[i] == "-o")
            {
                if (i + 1 < args.size())
                {
                    outputPath = args[++i];
                }
                else
                {
                    session.SendResponse("Error: Missing output path" + std::string(NEWLINE));
                    return;
                }
            }
            else if (args[i] == "--page" || args[i] == "-p")
            {
                if (i + 1 < args.size())
                {
                    filterPages.push_back(args[++i]);
                }
                else
                {
                    session.SendResponse("Error: Missing page specification" + std::string(NEWLINE));
                    return;
                }
            }
        }

        // Set the subfolder name
        if (!singleFile)
        {
            outputPath = "memory_logs";
        }

        // Get the memory access tracker
        auto* memory = emulator->GetContext()->pMemory;
        auto& tracker = memory->GetAccessTracker();

        std::string savedPath = tracker.SaveAccessData(outputPath, "yaml", singleFile, filterPages);
        if (!savedPath.empty())
        {
            session.SendResponse("Memory access data saved successfully to " + savedPath + NEWLINE);
        }
        else
        {
            session.SendResponse("Failed to save memory access data" + std::string(NEWLINE));
        }
        return;
    }

    // Parse command line arguments
    bool showAll = false;
    bool resetAfter = false;

    for (const auto& arg : args)
    {
        if (arg == "all")
            showAll = true;
        else if (arg == "reset")
            resetAfter = true;
    }

    // Get the memory access tracker
    auto* memory = emulator->GetContext()->pMemory;
    auto& tracker = memory->GetAccessTracker();

    // Get the current counters by summing up all banks
    uint64_t totalReads = 0;
    uint64_t totalWrites = 0;
    uint64_t totalExecutes = 0;

    // Get per-Z80 bank (4 banks of 16KB each)
    uint64_t bankReads[4] = {0};
    uint64_t bankWrites[4] = {0};
    uint64_t bankExecutes[4] = {0};

    for (int bank = 0; bank < 4; bank++)
    {
        bankReads[bank] = tracker.GetZ80BankReadAccessCount(bank);
        bankWrites[bank] = tracker.GetZ80BankWriteAccessCount(bank);
        bankExecutes[bank] = tracker.GetZ80BankExecuteAccessCount(bank);

        totalReads += bankReads[bank];
        totalWrites += bankWrites[bank];
        totalExecutes += bankExecutes[bank];
    }

    uint64_t totalAccesses = totalReads + totalWrites + totalExecutes;

    // Format the output
    std::stringstream ss;
    ss << "Memory Access Counters" << NEWLINE;
    ss << "=====================" << NEWLINE;
    ss << "Total Reads:    " << StringHelper::Format("%'llu", totalReads) << NEWLINE;
    ss << "Total Writes:   " << StringHelper::Format("%'llu", totalWrites) << NEWLINE;
    ss << "Total Executes: " << StringHelper::Format("%'llu", totalExecutes) << NEWLINE;
    ss << "Total Accesses: " << StringHelper::Format("%'llu", totalAccesses) << NEWLINE << NEWLINE;

    // Always show Z80 memory page (bank) counters with physical page mapping
    ss << "Z80 Memory Banks (16KB each):" << NEWLINE;
    ss << "----------------------------" << NEWLINE;

    const char* bankNames[4] = {"0x0000-0x3FFF", "0x4000-0x7FFF", "0x8000-0xBFFF", "0xC000-0xFFFF"};

    // Process each bank
    for (int bank = 0; bank < 4; bank++)
    {
        uint64_t bankTotal = bankReads[bank] + bankWrites[bank] + bankExecutes[bank];

        // Get bank info using helper methods
        bool isROM = (bank < 2) ? (bank == 0 ? memory->IsBank0ROM() : memory->GetMemoryBankMode(bank) == BANK_ROM)
                                : false;  // Banks 2-3 are always RAM

        uint16_t page = memory->GetPageForBank(bank);
        const char* type = isROM ? "ROM" : "RAM";
        std::string bankName = memory->GetCurrentBankName(bank);

        // Format the output
        ss << StringHelper::Format("Bank %d (%s) -> %s page: %s", bank, bankNames[bank], type, bankName.c_str())
           << NEWLINE;
        ss << StringHelper::Format("  Reads:    %'llu", bankReads[bank]) << NEWLINE;
        ss << StringHelper::Format("  Writes:   %'llu", bankWrites[bank]) << NEWLINE;
        ss << StringHelper::Format("  Executes: %'llu", bankExecutes[bank]) << NEWLINE;
        ss << StringHelper::Format("  Total:    %'llu", bankTotal) << NEWLINE << NEWLINE;
    }

    // Show all physical pages if requested
    if (showAll)
    {
        ss << "Physical Memory Pages with Activity:" << NEWLINE;
        ss << "-----------------------------------" << NEWLINE;

        bool foundActivity = false;

        // Check RAM pages (0-255)
        for (uint16_t page = 0; page < MAX_RAM_PAGES; page++)
        {
            uint32_t reads = tracker.GetPageReadAccessCount(page);
            uint32_t writes = tracker.GetPageWriteAccessCount(page);
            uint32_t executes = tracker.GetPageExecuteAccessCount(page);

            if (reads > 0 || writes > 0 || executes > 0)
            {
                foundActivity = true;
                ss << StringHelper::Format("RAM Page %d:", page) << NEWLINE;
                ss << StringHelper::Format("  Reads:    %'u", reads) << NEWLINE;
                ss << StringHelper::Format("  Writes:   %'u", writes) << NEWLINE;
                ss << StringHelper::Format("  Executes: %'u", executes) << NEWLINE;
                ss << StringHelper::Format("  Total:    %'u", reads + writes + executes) << NEWLINE << NEWLINE;
            }
        }

        // Check ROM pages (start after RAM, cache, and misc pages)
        const uint16_t FIRST_ROM_PAGE = MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES;
        for (uint16_t page = 0; page < MAX_ROM_PAGES; page++)
        {
            uint16_t physicalPage = FIRST_ROM_PAGE + page;
            uint32_t reads = tracker.GetPageReadAccessCount(physicalPage);
            uint32_t writes = tracker.GetPageWriteAccessCount(physicalPage);
            uint32_t executes = tracker.GetPageExecuteAccessCount(physicalPage);

            if (reads > 0 || writes > 0 || executes > 0)
            {
                foundActivity = true;
                ss << StringHelper::Format("ROM Page %d:", page) << NEWLINE;
                ss << StringHelper::Format("  Reads:    %'u", reads) << NEWLINE;
                ss << StringHelper::Format("  Writes:   %'u", writes) << NEWLINE;
                ss << StringHelper::Format("  Executes: %'u", executes) << NEWLINE;
                ss << StringHelper::Format("  Total:    %'u", reads + writes + executes) << NEWLINE << NEWLINE;
            }
        }

        if (!foundActivity)
        {
            ss << "No memory access activity detected in any physical page." << NEWLINE;
        }
    }

    // Show usage if no arguments provided
    if (args.empty())
    {
        ss << "Usage: memcounters [all] [reset] | save [options]" << NEWLINE;
        ss << "  all   - Show all physical pages with activity" << NEWLINE;
        ss << "  reset - Reset counters after displaying" << NEWLINE;
        ss << "  save  - Save memory access data to files" << NEWLINE;
        ss << "    Options:" << NEWLINE;
        ss << "      --single-file, -s     Save to single file" << NEWLINE;
        ss << "      --output <path>, -o   Output path (default: memory_logs)" << NEWLINE;
        ss << "      --page <name>, -p     Filter specific pages (e.g., 'RAM 0', 'ROM 2')" << NEWLINE;
    }

    // Send the response
    session.SendResponse(ss.str());

    // Reset counters if requested
    if (resetAfter)
    {
        tracker.ResetCounters();
        session.SendResponse("Memory counters have been reset." + std::string(NEWLINE));
    }
}

// HandleCallTrace - lines 2505-2727
void CLIProcessor::HandleCallTrace(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("Error: No emulator selected" + std::string(NEWLINE));
        return;
    }
    auto* memory = emulator->GetMemory();
    if (!memory)
    {
        session.SendResponse("Error: Memory not available" + std::string(NEWLINE));
        return;
    }
    auto& tracker = memory->GetAccessTracker();
    auto* callTrace = tracker.GetCallTraceBuffer();
    if (!callTrace)
    {
        session.SendResponse("Error: Call trace buffer not available" + std::string(NEWLINE));
        return;
    }
    if (args.empty() || args[0] == "help")
    {
        std::ostringstream oss;
        oss << "calltrace latest [N]   - Show latest N control flow events (default 10)" << NEWLINE;
        oss << "calltrace save <file> - Save full call trace history to file (binary)" << NEWLINE;
        oss << "calltrace reset       - Reset call trace buffer" << NEWLINE;
        oss << "calltrace help        - Show this help message" << NEWLINE;
        session.SendResponse(oss.str());
        return;
    }
    if (args[0] == "latest")
    {
        size_t count = 10;
        if (args.size() > 1)
        {
            try
            {
                count = std::stoul(args[1]);
            }
            catch (...)
            {
            }
        }
        auto events = callTrace->GetLatestCold(count);
        auto hotEvents = callTrace->GetLatestHot(count);
        std::ostringstream oss;
        if (!events.empty())
        {
            oss << "Latest " << events.size() << " cold control flow events:" << NEWLINE;
            oss << "Idx   m1_pc   type    target    flags   sp      opcodes        bank0    bank1    bank2    bank3    "
                   "stack_top         loop_count"
                << NEWLINE;
            for (size_t i = 0; i < events.size(); ++i)
            {
                const auto& ev = events[i];
                oss << StringHelper::Format("%4d   %04X   ", (int)i, ev.m1_pc);

                // type
                const char* typenames[] = {"JP", "JR", "CALL", "RST", "RET", "RETI", "DJNZ"};
                oss << StringHelper::Format("%-6s   ", typenames[static_cast<int>(ev.type)]);
                oss << StringHelper::Format("%04X     ", ev.target_addr);
                oss << StringHelper::Format("%02X      ", (int)ev.flags);
                oss << StringHelper::Format("%04X    ", ev.sp);
                // opcodes
                for (uint8_t j = 0; j < ev.opcode_len; ++j)
                    oss << StringHelper::Format("%02X ", (int)ev.opcode_bytes[j]);
                oss << std::string(12 - static_cast<size_t>(ev.opcode_len) * 3, ' ');
                oss << "   ";

                // banks
                for (int b = 0; b < 4; ++b)
                {
                    oss << StringHelper::Format("%s%-2d    ", ev.banks[b].is_rom ? "ROM" : "RAM",
                                                (int)ev.banks[b].page_num);
                }

                // stack top
                for (int s = 0; s < 3; ++s)
                {
                    if (ev.stack_top[s])
                        oss << StringHelper::Format("%04X ", ev.stack_top[s]);
                    else
                        oss << "     ";
                }

                oss << std::string(18 - 5 * 3, ' ');
                oss << StringHelper::Format("   %u", ev.loop_count);
                oss << NEWLINE;
            }
            oss << NEWLINE;
        }
        if (!hotEvents.empty())
        {
            oss << "Latest " << hotEvents.size() << " hot control flow events:" << NEWLINE;
            oss << "Idx   m1_pc   type    target    flags   sp      opcodes        bank0    bank1    bank2    bank3    "
                   "stack_top         loop_count   last_seen_frame"
                << NEWLINE;
            for (size_t i = 0; i < hotEvents.size(); ++i)
            {
                const auto& hot = hotEvents[i];
                const auto& ev = hot.event;
                oss << StringHelper::Format("%4d   %04X   ", (int)i, ev.m1_pc);
                // type
                const char* typenames[] = {"JP", "JR", "CALL", "RST", "RET", "RETI", "DJNZ"};
                oss << StringHelper::Format("%-6s ", typenames[static_cast<int>(ev.type)]);
                oss << StringHelper::Format("%04X     ", ev.target_addr);
                oss << StringHelper::Format("%02X     ", (int)ev.flags);
                oss << StringHelper::Format("%04X    ", ev.sp);

                // opcodes
                for (uint8_t j = 0; j < ev.opcode_len; ++j)
                    oss << StringHelper::Format("%02X ", (int)ev.opcode_bytes[j]);
                oss << std::string(12 - static_cast<size_t>(ev.opcode_len) * 3, ' ');
                oss << "   ";
                // banks
                for (int b = 0; b < 4; ++b)
                {
                    oss << StringHelper::Format("%s%-2d    ", ev.banks[b].is_rom ? "ROM" : "RAM",
                                                (int)ev.banks[b].page_num);
                }
                // stack top
                for (int s = 0; s < 3; ++s)
                {
                    if (ev.stack_top[s])
                        oss << StringHelper::Format("%04X ", ev.stack_top[s]);
                    else
                        oss << "     ";
                }
                oss << std::string(18 - 5 * 3, ' ');
                oss << StringHelper::Format("   %u   %llu", hot.loop_count, hot.last_seen_frame);
                oss << NEWLINE;
            }
            oss << NEWLINE;
        }
        session.SendResponse(oss.str());
        return;
    }
    if (args[0] == "save")
    {
        // Generate a unique filename with timestamp if not provided
        std::string filename;
        if (args.size() > 1)
        {
            filename = args[1];
        }
        else
        {
            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            ss << "calltrace_" << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S") << ".yaml";
            filename = ss.str();
        }

        // Use CallTraceBuffer's SaveToFile method
        if (!callTrace->SaveToFile(filename))
        {
            session.SendResponse("Failed to create call trace file: " + filename + NEWLINE);
            return;
        }
        session.SendResponse("Call trace saved to " + filename + NEWLINE);
        return;
    }
    if (args[0] == "reset")
    {
        callTrace->Reset();
        session.SendResponse("Call trace buffer reset." + std::string(NEWLINE));
        return;
    }
    if (args[0] == "stats")
    {
        size_t cold_count = callTrace->ColdSize();
        size_t cold_capacity = callTrace->ColdCapacity();
        size_t hot_count = callTrace->HotSize();
        size_t hot_capacity = callTrace->HotCapacity();
        size_t cold_bytes = cold_count * sizeof(Z80ControlFlowEvent);
        size_t hot_bytes = hot_count * sizeof(HotEvent);
        auto format_bytes = [](size_t bytes) -> std::string {
            char buf[32];
            if (bytes >= 1024 * 1024)
                snprintf(buf, sizeof(buf), "%.2f MB", bytes / 1024.0 / 1024.0);
            else if (bytes >= 1024)
                snprintf(buf, sizeof(buf), "%.2f KB", bytes / 1024.0);
            else
                snprintf(buf, sizeof(buf), "%zu B", bytes);
            return buf;
        };

        std::ostringstream oss;
        oss << "CallTraceBuffer stats:" << NEWLINE;
        oss << "  Cold buffer: " << cold_count << " / " << cold_capacity << "  (" << format_bytes(cold_bytes) << ")"
            << NEWLINE;
        oss << "  Hot buffer:  " << hot_count << " / " << hot_capacity << "  (" << format_bytes(hot_bytes) << ")"
            << NEWLINE;

        // Add was_hot and top 5 loop_count info
        auto allCold = callTrace->GetAll();
        size_t was_hot_count = 0;
        std::vector<uint32_t> loop_counts;
        for (const auto& ev : allCold)
        {
            if (ev.was_hot)
                was_hot_count++;
            loop_counts.push_back(ev.loop_count);
        }

        std::sort(loop_counts.begin(), loop_counts.end(), std::greater<uint32_t>());
        oss << "  Cold buffer: " << was_hot_count << " events were previously hot (was_hot=true)" << NEWLINE;
        oss << "  Top 5 loop_count values in cold buffer: ";
        for (size_t i = 0; i < std::min<size_t>(5, loop_counts.size()); ++i)
        {
            oss << loop_counts[i];
            if (i + 1 < std::min<size_t>(5, loop_counts.size()))
                oss << ", ";
        }
        oss << NEWLINE;

        session.SendResponse(oss.str());
        return;
    }
    session.SendResponse("Unknown calltrace command. Use 'calltrace help' for usage." + std::string(NEWLINE));
}

