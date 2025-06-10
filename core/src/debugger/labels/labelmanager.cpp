#include "stdafx.h"
#include "labelmanager.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <limits> // Required for UINT32_MAX

#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"
#include "3rdparty/message-center/messagecenter.h"

// @file labelmanager.cpp
// @brief Implementation of the LabelManager class for managing debug symbols and labels
// 
// This file contains the implementation of the LabelManager class which provides
// functionality to manage debug symbols, labels, and their associated metadata.
// It supports loading and saving labels in various formats and provides lookup
// capabilities by address or name.

/// region <Constructors / destructors>

// @brief Construct a new LabelManager instance
// @param context Pointer to the emulator context
LabelManager::LabelManager(EmulatorContext *context)
{
    _context = context;
    _logger = _context->pModuleLogger;
}

// @brief Destroy the LabelManager instance
// 
// Cleans up all allocated resources and removes all labels.
LabelManager::~LabelManager()
{
    ClearAllLabels();
}

/// endregion </Constructors / destructors>

/// region <Label management>

// @brief Add a new label to the manager
// @param name Unique name of the label
// @param z80Address Z80 address of the label
// @param physicalAddress Physical memory address (optional, 0 if not used)
// @param type Type of the label (e.g., "code", "data", "bss")
// @param module Module name this label belongs to (optional)
// @param comment Optional comment for the label
// @return true if the label was added successfully
// @return false if the label name is empty or a label with this name already exists
// Static helper function to calculate physical address
// This is a simplified calculation. A real implementation might need more context from EmulatorContext/MemoryMapper.
static uint32_t CalculateLabelPhysicalAddress(uint16_t /*z80Addr*/, uint8_t bank, uint16_t bankAddrInPage, EmulatorContext* /*context*/)
{
    if (bank == 0xFF) // Label is Z80-address-specific, bank-agnostic.
    {
        // This type of label does not map to a single, fixed physical address.
        // Its "physicality" is resolved at runtime based on the currently paged bank.
        // Return a sentinel value.
        return UINT32_MAX; 
    }

    // Label is specific to a bank and an offset within that bank.
    // TODO: Ideally, use EmulatorContext to get page size and perform mapping if a memory mapper exists.
    // For now, using a common 16KB page size (0x4000).
    const uint32_t BANK_PAGE_SIZE = 0x4000; // Assuming 16KB pages
    return (static_cast<uint32_t>(bank) * BANK_PAGE_SIZE) + bankAddrInPage;
}

bool LabelManager::AddLabel(const std::string& name, uint16_t z80Address, uint16_t bank, uint16_t bankOffset,
                           const std::string& type, const std::string& module, const std::string& comment, bool active)
{
    if (name.empty())
    {
        return false;
    }

    // Create a new label
    auto label = std::make_shared<Label>();
    label->name = name;
    label->address = z80Address;
    label->bank = bank;
    label->bankOffset = bankOffset;
    label->type = type;
    label->module = module;
    label->comment = comment;
    label->active = active;
    
    // If address is in ROM area (below 0x4000) and bank is not specified, set bank type to ROM
    if (z80Address < 0x4000 && bank == UINT16_MAX)
    {
        label->setBankTypeROM();
    }
    
    label->physicalAddress = CalculateLabelPhysicalAddress(label->address, label->bank, label->bankOffset, _context);

    // Add to all lookup maps
    _labelsByZ80Address[z80Address] = label;
    if (label->physicalAddress != UINT32_MAX) 
    {
        _labelsByPhysicalAddress[label->physicalAddress] = label;
    }
    _labelsByName[name] = label;

    // Notify about the new label
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_LABEL_CHANGED, nullptr, true);

    return true;
}

// @brief Remove a label by its name
// @param name Name of the label to remove
// @return true if the label was found and removed
// @return false if no label with the given name exists
bool LabelManager::RemoveLabel(const std::string& name)
{
    auto it = _labelsByName.find(name);
    if (it == _labelsByName.end())
    {
        return false;
    }

    std::shared_ptr<Label> label = it->second;
    
    // Remove from all maps
    _labelsByZ80Address.erase(label->address);
    if (label->physicalAddress != UINT32_MAX)
    {
        _labelsByPhysicalAddress.erase(label->physicalAddress);
    }
    _labelsByName.erase(it);

    // Notify about the removed label
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_LABEL_CHANGED, nullptr, true);

    return true;
}

// @brief Remove all labels from the manager
// 
// Clears all internal data structures and frees all allocated resources.
void LabelManager::ClearAllLabels()
{
    // Check if we have any labels before clearing
    bool hadLabels = !_labelsByName.empty();
    
    _labelsByZ80Address.clear();
    _labelsByPhysicalAddress.clear();
    _labelsByName.clear();
    
    // Notify about clearing all labels if there were any
    if (hadLabels)
    {
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        messageCenter.Post(NC_LABEL_CHANGED);
    }
}

// @brief Find a label by its Z80 address
// @param address Z80 address to search for
// @return std::shared_ptr<Label> Pointer to the label if found, nullptr otherwise
std::shared_ptr<Label> LabelManager::GetLabelByZ80Address(uint16_t address) const
{
    auto it = _labelsByZ80Address.find(address);
    return it != _labelsByZ80Address.end() ? it->second : nullptr;
}

// @brief Find a label by its physical memory address
// @param address Physical memory address to search for
// @return std::shared_ptr<Label> Pointer to the label if found, nullptr otherwise
std::shared_ptr<Label> LabelManager::GetLabelByPhysicalAddress(uint32_t address) const
{
    auto it = _labelsByPhysicalAddress.find(address);
    return it != _labelsByPhysicalAddress.end() ? it->second : nullptr;
}

// @brief Find a label by its name
// @param name Name of the label to find
// @return std::shared_ptr<Label> Pointer to the label if found, nullptr otherwise
std::shared_ptr<Label> LabelManager::GetLabelByName(const std::string& name) const
{
    auto it = _labelsByName.find(name);
    return it != _labelsByName.end() ? it->second : nullptr;
}

// @brief Get all labels in the manager
// @return std::vector<std::shared_ptr<Label>> Vector containing all labels
std::vector<std::shared_ptr<Label>> LabelManager::GetAllLabels() const
{
    std::vector<std::shared_ptr<Label>> result;
    for (const auto& pair : _labelsByName)
    {
        result.push_back(pair.second);
    }
    return result;
}

// @brief Get the total number of labels
// @return size_t Number of labels currently managed
size_t LabelManager::GetLabelCount() const
{
    return _labelsByName.size();
}

bool LabelManager::UpdateLabel(const Label& updatedLabel)
{
    auto it = _labelsByName.find(updatedLabel.name);
    if (it == _labelsByName.end())
    {
        // Label with this name does not exist, cannot update
        _logger->Warning(_MODULE, _SUBMODULE, "UpdateLabel failed: Label '%s' not found.", updatedLabel.name.c_str());
        return false;
    }

    std::shared_ptr<Label> existingLabel = it->second;

    // Store old addresses for map updates
    uint16_t oldZ80Address = existingLabel->address;
    uint32_t oldPhysicalAddress = existingLabel->physicalAddress;

    // Update label properties (name is the key, so it's not changed here)
    existingLabel->address = updatedLabel.address;
    existingLabel->bank = updatedLabel.bank;
    existingLabel->bankOffset = updatedLabel.bankOffset;
    existingLabel->type = updatedLabel.type;
    existingLabel->module = updatedLabel.module;
    existingLabel->comment = updatedLabel.comment;
    existingLabel->active = updatedLabel.active;

    // Recalculate physical address
    existingLabel->physicalAddress = CalculateLabelPhysicalAddress(existingLabel->address, existingLabel->bank, existingLabel->bankOffset, _context);

    // Update Z80 address map if address changed
    if (existingLabel->address != oldZ80Address)
    {
        _labelsByZ80Address.erase(oldZ80Address);
        _labelsByZ80Address[existingLabel->address] = existingLabel;
    }

    // Update physical address map if physical address changed or its validity changed
    if (existingLabel->physicalAddress != oldPhysicalAddress)
    {
        if (oldPhysicalAddress != UINT32_MAX) // If old physical address was valid, remove it
        {
            _labelsByPhysicalAddress.erase(oldPhysicalAddress);
        }
        if (existingLabel->physicalAddress != UINT32_MAX) // If new physical address is valid, add it
        {
            _labelsByPhysicalAddress[existingLabel->physicalAddress] = existingLabel;
        }
    }

    _logger->Debug(_MODULE, _SUBMODULE, "Label '%s' updated successfully.", existingLabel->name.c_str());

    // Notify about the updated label
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_LABEL_CHANGED, nullptr, true);

    return true;
}

/// endregion </Label management>


/// region <File operations>

// @brief Load labels from a file, auto-detecting the file format
// @param path Path to the file containing the labels
// @return true if the file was loaded successfully
// @return false if the file could not be opened or parsed
bool LabelManager::LoadLabels(const std::string& path)
{
    if (path.empty())
    {
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        LOGERROR("Failed to open label file: %s", path.c_str());
        return false;
    }

    FileFormat format = DetectFileFormat(path);
    bool result = false;

    switch (format)
    {
        case FileFormat::MAP:
            result = ParseMapFile(file);
            break;
        case FileFormat::SYM:
            result = ParseSymFile(file);
            break;
        case FileFormat::VICE:
            result = ParseViceSymFile(file);
            break;
        case FileFormat::SJASM:
            result = ParseSJASMSymFile(file);
            break;
        case FileFormat::Z88DK:
            result = ParseZ88DKSymFile(file);
            break;
        default:
            LOGERROR("Unsupported label file format: %s", path.c_str());
            break;
    }

    file.close();
    return result;
}

// @brief Load labels from a map file
// @param path Path to the map file
// @return true if the file was loaded successfully
// @return false if the file could not be opened or parsed
bool LabelManager::LoadMapFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    return ParseMapFile(file);
}

// @brief Load labels from a symbol file
// @param path Path to the symbol file
// @return true if the file was loaded successfully
// @return false if the file could not be opened or parsed
bool LabelManager::LoadSymFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    return ParseSymFile(file);
}

// @brief Save all labels to a file in the specified format
// @param path Path where to save the labels
// @param format File format to use for saving
// @return true if the file was saved successfully
// @return false if the file could not be written
bool LabelManager::SaveLabels(const std::string& path, FileFormat format) const
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    // Common header for all formats
    file << "; Labels exported by UnrealNG Emulator" << std::endl;
    file << "; Format: " << (format == FileFormat::SYM ? "Simple Symbol" : "Map") << std::endl << std::endl;

    // Export all labels
    for (const auto& pair : _labelsByName)
    {
        const auto& label = pair.second;
        
        switch (format)
        {
            case FileFormat::SYM:
                file << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << label->address 
                     << " " << label->name << " " << label->type;
                if (!label->comment.empty())
                {
                    file << " ; " << label->comment;
                }
                file << std::endl;
                break;
                
            case FileFormat::MAP:
                file << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << label->address 
                     << " " << label->type << " " << label->name;
                if (!label->comment.empty())
                {
                    file << " ; " << label->comment;
                }
                file << std::endl;
                break;
                
            default:
                // Default to simple format
                file << label->name << " = 0x" << std::hex << std::uppercase << label->address << "\n";
                break;
        }
    }

    file.close();
    return true;
}

/// endregion </File operations>


/// region <File format detection and parsing>

// @brief Detect the format of a label file based on its extension and content
// @param path Path to the file to analyze
// @return FileFormat Detected file format or FileFormat::UNKNOWN if format cannot be determined
LabelManager::FileFormat LabelManager::DetectFileFormat(const std::string& path) const
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".map")
        return FileFormat::MAP;
    else if (ext == ".sym")
        return FileFormat::SYM;
    else if (ext == ".vice")
        return FileFormat::VICE;
    else if (ext == ".s" || ext == ".asm")
        return FileFormat::SJASM;
    else if (ext == ".z88")
        return FileFormat::Z88DK;

    // Try to detect by content
    std::ifstream file(path);
    if (file.is_open())
    {
        std::string line;
        if (std::getline(file, line))
        {
            // Check for common map file patterns
            if (line.find("Linker script and memory map") != std::string::npos ||
                line.find("Memory map") != std::string::npos)
            {
                return FileFormat::MAP;
            }
            // Check for VICE symbol format
            else if (line.find("al") == 0 || line.find("add_label") == 0)
            {
                return FileFormat::VICE;
            }
        }
        file.close();
    }

    return FileFormat::UNKNOWN;
}

// @brief Parse a map file from an input stream
// @param input Input stream containing the map file data
// @return true if the file was parsed successfully
// @return false if a parse error occurred
// @note Map file format: ADDR TYPE NAME [; COMMENT]
// Example: 1234 code main ; Entry point
bool LabelManager::ParseMapFile(std::istream& input)
{
    std::string line;
    while (std::getline(input, line))
    {
        line = TrimWhitespace(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        // Create a string stream to parse the line
        std::istringstream iss(line);
        
        // Variables to hold parsed components
        std::string addressStr;  // First column: memory address in hex
        std::string name;        // Second column: label name
        std::string typeStr;     // Third column: type in parentheses (e.g., (CODE))
        std::string type = "code"; // Default type is "code"
        
        // Read address, name, and type
        if (iss >> addressStr >> name)
        {
            // Try to read type in format (TYPE)
            std::string token;
            if (iss >> token)
            {
                if (token.size() >= 3 && token[0] == '(' && token[token.size()-1] == ')')
                {
                    // Extract type from (TYPE) and convert to lowercase
                    type = token.substr(1, token.size() - 2);
                    std::transform(type.begin(), type.end(), type.begin(), 
                                 [](unsigned char c) { return std::tolower(c); });
                }
            }
            
            // Extract comment if present (after semicolon)
            std::string comment;
            size_t commentPos = line.find(';');
            if (commentPos != std::string::npos)
            {
                comment = line.substr(commentPos + 1);
                comment = TrimWhitespace(comment);
            }

            uint16_t address = ParseHex16(addressStr);
            if (address != 0xFFFF)  // 0xFFFF indicates parse error
            {
                // Use UINT16_MAX for bank and bankOffset to indicate they're not specified
                AddLabel(name, address, UINT16_MAX, UINT16_MAX, type, "", comment);
            }
        }
    }
    
    return true;
}

// @brief Parse a simple symbol file from an input stream
// @param input Input stream containing the symbol file data
// @return true if the file was parsed successfully
// @return false if a parse error occurred
// @note Simple symbol format: ADDR NAME [TYPE] [; COMMENT]
// Example: 1234 main code ; Entry point
bool LabelManager::ParseSymFile(std::istream& input)
{
    const std::string DEFAULT_LABEL_TYPE = "code";
    
    std::string line;
    while (std::getline(input, line))
    {
        // Skip empty lines and comments
        line = TrimWhitespace(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
        {
            continue;
        }

        // Parse the line into components
        std::istringstream lineStream(line);
        std::string addressStr, name, type;
        
        // Read the address (required)
        if (!(lineStream >> addressStr))
        {
            continue;  // Skip malformed lines
        }
        
        // Read the name (required)
        if (!(lineStream >> name))
        {
            continue;  // Skip lines without a name
        }
        
        // Read type if present in format (TYPE)
        type = DEFAULT_LABEL_TYPE;
        std::string token;
        if (lineStream >> token)
        {
            // Check if the token is in (TYPE) format
            if (token.size() >= 3 && token[0] == '(' && token[token.size()-1] == ')')
            {
                // Extract type from (TYPE) and convert to lowercase
                type = token.substr(1, token.size() - 2);
                std::transform(type.begin(), type.end(), type.begin(), 
                             [](unsigned char c) { return std::tolower(c); });
            }
        }
        
        // Extract comment if present (after semicolon)
        std::string comment;
        size_t commentPos = line.find(';');
        if (commentPos != std::string::npos)
        {
            comment = line.substr(commentPos + 1);
            comment = TrimWhitespace(comment);
        }
        
        // Parse address and add the label if valid
        uint16_t address = ParseHex16(addressStr);
        if (address != 0xFFFF)
        {
            // Use UINT8_MAX for bank and UINT16_MAX for bankOffset to indicate they're not specified
            AddLabel(name, address, UINT8_MAX, UINT16_MAX, type, "", comment);
        }
    }
    
    return true;
}

// @brief Parse a VICE emulator symbol file
// @param input Input stream containing the VICE symbol file data
// @return true if the file was parsed successfully
// @return false if a parse error occurred
// @note VICE symbol file format: .al ADDR "NAME"
// Example: .al 0x1234 "main"
bool LabelManager::ParseViceSymFile(std::istream& input)
{
    std::string line;
    while (std::getline(input, line))
    {
        line = TrimWhitespace(line);
        if (line.empty() || line[0] == '#')
            continue;

        // VICE format: al C:address name (TYPE)
        if (line.find("al ") == 0)
        {
            std::vector<std::string> parts = SplitString(line, ' ');
            if (parts.size() >= 3)
            {
                std::string addrStr = parts[1].substr(2);  // Skip "C:" prefix
                std::string name = parts[2];
                std::string type = "code";  // Default type

                // Check for type in format (TYPE)
                if (parts.size() >= 4)
                {
                    std::string token = parts[3];
                    if (token.size() >= 3 && token[0] == '(' && token[token.size() - 1] == ')')
                    {
                        // Extract type from (TYPE) and convert to lowercase
                        type = token.substr(1, token.size() - 2);
                        std::transform(type.begin(), type.end(), type.begin(),
                                       [](unsigned char c) { return std::tolower(c); });
                    }
                }

                uint16_t address = ParseHex16(addrStr);

                if (address != 0xFFFF)
                {
                    // Use UINT8_MAX for bank and UINT16_MAX for bankOffset to indicate they're not specified
                    AddLabel(name, address, UINT8_MAX, UINT16_MAX, type, "", "", true);
                }
            }
        }
    }
    
    return true;
}

// @brief Parse an SJASM symbol file
// @param input Input stream containing the SJASM symbol file data
// @return true if the file was parsed successfully
// @return false if a parse error occurred
// @note SJASM symbol file format: NAME = VALUE ; TYPE
// Example: main = 0x1234 ; code
bool LabelManager::ParseSJASMSymFile(std::istream& input)
{
    std::string line;
    while (std::getline(input, line))
    {
        line = TrimWhitespace(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        // SJASM format: LABEL EQU $ADDR ; (TYPE)
        size_t equPos = line.find(" EQU ");
        if (equPos != std::string::npos)
        {
            std::string name = line.substr(0, equPos);
            std::string rest = line.substr(equPos + 5);
            
            // Extract address and type
            std::string addrStr = rest;
            std::string type = "code"; // Default type
            
            // Check for comment with type
            size_t commentPos = rest.find(';');
            if (commentPos != std::string::npos)
            {
                addrStr = TrimWhitespace(rest.substr(0, commentPos));
                std::string comment = TrimWhitespace(rest.substr(commentPos + 1));
                
                // Check if comment contains type in (TYPE) format
                if (comment.size() >= 3 && comment[0] == '(' && comment[comment.size()-1] == ')')
                {
                    // Extract type from (TYPE) and convert to lowercase
                    type = comment.substr(1, comment.size() - 2);
                    std::transform(type.begin(), type.end(), type.begin(), 
                                 [](unsigned char c) { return std::tolower(c); });
                }
            }
            
            // Remove $ prefix if present
            if (!addrStr.empty() && addrStr[0] == '$')
                addrStr = addrStr.substr(1);
                
            uint16_t address = ParseHex16(addrStr);
            if (address != 0xFFFF)
            {
                // Use UINT8_MAX for bank and UINT16_MAX for bankOffset to indicate they're not specified
                AddLabel(name, address, UINT8_MAX, UINT16_MAX, type, "", "", true);
            }
        }
    }
    
    return true;
}

// @brief Parse a Z88DK symbol file
// @param input Input stream containing the Z88DK symbol file data
// @return true if the file was parsed successfully
// @return false if a parse error occurred
// @note Z88DK symbol file format: DEFC NAME = VALUE ; TYPE
// Example: DEFC main = 0x1234 ; code
bool LabelManager::ParseZ88DKSymFile(std::istream& input)
{
    std::string line;
    while (std::getline(input, line))
    {
        line = TrimWhitespace(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        // Z88DK format: DEFC name = $ADDR
        if (line.find("DEFC ") == 0)
        {
            size_t nameStart = 5; // Length of "DEFC "
            size_t eqPos = line.find('=');
            
            if (eqPos != std::string::npos)
            {
                std::string name = line.substr(nameStart, eqPos - nameStart);
                name = TrimWhitespace(name);
                
                std::string addrStr = line.substr(eqPos + 1);
                addrStr = TrimWhitespace(addrStr);
                
                // Extract type from comment if present
                std::string type = "code"; // Default type
                size_t commentPos = addrStr.find(';');
                if (commentPos != std::string::npos)
                {
                    // Extract the address part before the comment
                    std::string addrPart = TrimWhitespace(addrStr.substr(0, commentPos));
                    std::string comment = TrimWhitespace(addrStr.substr(commentPos + 1));
                    
                    // Check if comment contains type in (TYPE) format
                    if (comment.size() >= 3 && comment[0] == '(' && comment[comment.size()-1] == ')')
                    {
                        // Extract type from (TYPE) and convert to lowercase
                        type = comment.substr(1, comment.size() - 2);
                        std::transform(type.begin(), type.end(), type.begin(), 
                                     [](unsigned char c) { return std::tolower(c); });
                    }
                    
                    addrStr = addrPart;
                }
                
                // Remove $ prefix if present
                if (!addrStr.empty() && addrStr[0] == '$')
                    addrStr = addrStr.substr(1);
                    
                uint16_t address = ParseHex16(addrStr);
                if (address != 0xFFFF)
                {
                    // Use UINT8_MAX for bank and UINT16_MAX for bankOffset to indicate they're not specified
                    AddLabel(name, address, UINT8_MAX, UINT16_MAX, type, "", "", true);
                }
            }
        }
    }
    
    return true;
}

/// endregion </File format detection and parsing>


/// region <Helper methods>

// @brief Remove leading and trailing whitespace from a string
// @param str Input string to trim
// @return std::string Trimmed string
std::string LabelManager::TrimWhitespace(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t");
    if (std::string::npos == first)
    {
        return "";
    }
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

// @brief Split a string into tokens using the specified delimiter
// @param str String to split
// @param delimiter Character to use as delimiter
// @return std::vector<std::string> Vector of tokens
std::vector<std::string> LabelManager::SplitString(const std::string& str, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    
    while (std::getline(tokenStream, token, delimiter))
    {
        token = TrimWhitespace(token);
        if (!token.empty())
        {
            tokens.push_back(token);
        }
    }
    
    return tokens;
}

// @brief Check if a character is a valid hexadecimal digit
// @param c Character to check
// @return true if the character is 0-9, a-f, or A-F
// @return false otherwise
bool LabelManager::IsHexDigit(char c)
{
    return (c >= '0' && c <= '9') || 
           (c >= 'a' && c <= 'f') || 
           (c >= 'A' && c <= 'F');
}

// @brief Parse a 16-bit hexadecimal string to an integer
// @param str String containing hexadecimal number (with or without 0x prefix)
// @return uint16_t Parsed value, or 0xFFFF if parsing fails
uint16_t LabelManager::ParseHex16(const std::string& str)
{
    if (str.empty())
        return 0xFFFF;
        
    std::string s = str;
    // Remove 0x or $ prefix if present
    if (s.size() > 1 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')))
        s = s.substr(2);
    else if (s[0] == '$')
        s = s.substr(1);
        
    // Check if all characters are valid hex digits
    for (char c : s)
    {
        if (!IsHexDigit(c))
            return 0xFFFF;
    }
    
    try
    {
        return static_cast<uint16_t>(std::stoul(s, nullptr, 16));
    }
    catch (...)
    {
        return 0xFFFF;
    }
}

// @brief Parse a 32-bit hexadecimal string to an integer
// @param str String containing hexadecimal number (with or without 0x prefix)
// @return uint32_t Parsed value, or 0xFFFFFFFF if parsing fails
uint32_t LabelManager::ParseHex32(const std::string& str)
{
    if (str.empty())
        return 0xFFFFFFFF;
        
    std::string s = str;
    // Remove 0x or $ prefix if present
    if (s.size() > 1 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')))
        s = s.substr(2);
    else if (s[0] == '$')
        s = s.substr(1);
        
    // Check if all characters are valid hex digits
    for (char c : s)
    {
        if (!IsHexDigit(c))
            return 0xFFFFFFFF;
    }
    
    try
    {
        return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
    }
    catch (...)
    {
        return 0xFFFFFFFF;
    }
}

/// endregion </Helper methods>
