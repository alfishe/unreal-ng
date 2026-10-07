#include "labelmanager.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>  // Required for UINT32_MAX
#include <sstream>

#include "3rdparty/message-center/messagecenter.h"
#include "common/modulelogger.h"
#include "common/filehelper.h"
#include "emulator/emulatorcontext.h"
#include "stdafx.h"
#include "unrealasm/symbols/codec.h"

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
LabelManager::LabelManager(EmulatorContext* context)
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

    // If address is in ROM area (below 0x4000)
    if (z80Address < 0x4000)
    {
        label->setBankTypeROM();
    }

    // Add to all lookup maps
    _labelsByZ80Address[z80Address] = label;
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

std::vector<std::shared_ptr<Label>> LabelManager::GetAllLabelsAtAddress(uint16_t address) const
{
    std::vector<std::shared_ptr<Label>> result;
    for (const auto& pair : _labelsByName)
    {
        if (pair.second->address == address)
            result.push_back(pair.second);
    }
    return result;
}

std::vector<std::shared_ptr<Label>> LabelManager::GetLabelsByModule(const std::string& module) const
{
    std::vector<std::shared_ptr<Label>> result;
    for (const auto& pair : _labelsByName)
    {
        if (pair.second->module == module)
            result.push_back(pair.second);
    }
    return result;
}

std::vector<std::shared_ptr<Label>> LabelManager::GetLabelsByBank(uint16_t bank,
    std::optional<MemoryBankModeEnum> bankType) const
{
    std::vector<std::shared_ptr<Label>> result;
    for (const auto& pair : _labelsByName)
    {
        if (pair.second->bank == bank)
        {
            if (!bankType.has_value() || pair.second->bankType == bankType.value())
                result.push_back(pair.second);
        }
    }
    return result;
}

std::vector<std::shared_ptr<Label>> LabelManager::GetLabelsByType(const std::string& type) const
{
    std::vector<std::shared_ptr<Label>> result;
    for (const auto& pair : _labelsByName)
    {
        if (pair.second->type == type)
            result.push_back(pair.second);
    }
    return result;
}

std::vector<std::shared_ptr<Label>> LabelManager::GetLabelsInRange(uint16_t fromAddr, uint16_t toAddr) const
{
    std::vector<std::shared_ptr<Label>> result;
    for (const auto& pair : _labelsByName)
    {
        uint16_t addr = pair.second->address;
        if (addr >= fromAddr && addr <= toAddr)
            result.push_back(pair.second);
    }
    return result;
}

/// @brief Get labels matching filter criteria
/// @param filter Combined filter with optional constraints
/// @return Vector of matching labels
///
/// Filter criteria (all optional, unset = match any):
/// - activeOnly: if true, skip inactive labels
/// - module: exact match on module name
/// - bank: exact match on bank number
/// - bankType: exact match on RAM/ROM type
/// - type: exact match on label type (code/data/const)
/// - addressFrom/addressTo: inclusive address range
///
/// Performance: O(n) linear scan. Acceptable for typical label counts (100-2000).
/// For 10,000+ labels, consider adding secondary indices (_labelsByModule, etc.)
std::vector<std::shared_ptr<Label>> LabelManager::GetLabels(const LabelFilter& filter) const
{
    std::vector<std::shared_ptr<Label>> result;
    for (const auto& pair : _labelsByName)
    {
        const auto& label = pair.second;

        // Early-continue on first mismatch for efficiency
        if (filter.activeOnly && !label->active)
            continue;

        if (filter.module.has_value() && label->module != filter.module.value())
            continue;

        if (filter.bank.has_value() && label->bank != filter.bank.value())
            continue;

        if (filter.bankType.has_value() && label->bankType != filter.bankType.value())
            continue;

        if (filter.type.has_value() && label->type != filter.type.value())
            continue;

        if (filter.addressFrom.has_value() && label->address < filter.addressFrom.value())
            continue;

        if (filter.addressTo.has_value() && label->address > filter.addressTo.value())
            continue;

        result.push_back(label);
    }
    return result;
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

    // Update label properties (name is the key, so it's not changed here)
    existingLabel->address = updatedLabel.address;
    existingLabel->bank = updatedLabel.bank;
    existingLabel->bankOffset = updatedLabel.bankOffset;
    existingLabel->type = updatedLabel.type;
    existingLabel->module = updatedLabel.module;
    existingLabel->comment = updatedLabel.comment;
    existingLabel->active = updatedLabel.active;

    // Update Z80 address map if address changed
    if (existingLabel->address != oldZ80Address)
    {
        _labelsByZ80Address.erase(oldZ80Address);
        _labelsByZ80Address[existingLabel->address] = existingLabel;
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
        return false;
    std::vector<uint8_t> bytes;
    if (!ReadLabelFile(path, bytes))
        return false;
    // The extension decides as it always did (.map, .sym, .vice, .s / .asm, .z88); other files by their content
    std::string extension = FileHelper::ToFsPath(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    if (!extension.empty() && extension[0] == '.')
        extension.erase(0, 1);
    const auto& registry = unrealasm::symbols::SymbolCodecRegistry::Builtin();
    const unrealasm::symbols::ISymbolCodec* codec = registry.Find(CodecForExtension(extension));
    // z80asm writes its map (-m) as .map too: its content says so
    if (extension == "map")
    {
        const auto detected = registry.Detect(bytes, extension);
        if (detected.chosen && detected.chosen->Info().id == "z88dk-map")
            codec = detected.chosen;
    }
    if (!codec)
    {
        const auto detected = registry.Detect(bytes, extension);
        codec = detected.chosen;
        if (!codec)
        {
            LOGERROR("Unsupported label file format: %s (%s)", path.c_str(), detected.reason.c_str());
            return false;
        }
    }
    return ImportWith(*codec, bytes, path);
}

// @brief Load labels from a map file
// @param path Path to the map file
// @return true if the file was loaded successfully
// @return false if the file could not be opened or parsed
bool LabelManager::LoadMapFile(const std::string& path)
{
    std::vector<uint8_t> bytes;
    return ReadLabelFile(path, bytes) && ImportWith(*unrealasm::symbols::SymbolCodecRegistry::Builtin().Find("unreal-map"), bytes, path);
}

// @brief Load labels from a symbol file
// @param path Path to the symbol file
// @return true if the file was loaded successfully
// @return false if the file could not be opened or parsed
bool LabelManager::LoadSymFile(const std::string& path)
{
    std::vector<uint8_t> bytes;
    return ReadLabelFile(path, bytes) && ImportWith(*unrealasm::symbols::SymbolCodecRegistry::Builtin().Find("simple-sym"), bytes, path);
}

// @brief Save all labels to a file in the specified format
// @param path Path where to save the labels
// @param format File format to use for saving
// @return true if the file was saved successfully
// @return false if the file could not be written
bool LabelManager::SaveLabels(const std::string& path, FileFormat format) const
{
    const char* id = "simple-sym";
    switch (format)
    {
        case FileFormat::MAP: id = "unreal-map"; break;
        case FileFormat::VICE: id = "vice"; break;
        case FileFormat::SJASM: id = "sjasm-equ"; break;
        case FileFormat::Z88DK: id = "z88dk-defc"; break;
        case FileFormat::SYM:
        case FileFormat::UNKNOWN: break;
    }
    const auto* codec = unrealasm::symbols::SymbolCodecRegistry::Builtin().Find(id);
    unrealasm::symbols::SymbolFile file;
    file.sets.emplace_back();
    for (const auto& [address, label] : _labelsByZ80Address)
        file.sets[0].symbols.push_back(ToSymbol(*label));
    const auto encoded = codec->Encode(file, {});
    for (const auto& d : encoded.diagnostics)
        LOGWARNING("SaveLabels %s: %s", path.c_str(), d.message.c_str());
    std::ofstream out(FileHelper::ToFsPath(path), std::ios::binary);
    if (!out.is_open())
        return false;
    out.write(reinterpret_cast<const char*>(encoded.bytes.data()), static_cast<std::streamsize>(encoded.bytes.size()));
    return out.good();
}

/// endregion </File operations>

/// region <Symbol codecs>

// @brief The codec LabelManager has always picked for a file extension ("" when the extension says nothing)
std::string LabelManager::CodecForExtension(const std::string& extension)
{
    if (extension == "map")
        return "unreal-map";
    if (extension == "sym")
        return "simple-sym";
    if (extension == "vice")
        return "vice";
    if (extension == "s" || extension == "asm")
        return "sjasm-equ";
    if (extension == "z88")
        return "z88dk-defc";
    return {};
}

bool LabelManager::ReadLabelFile(const std::string& path, std::vector<uint8_t>& bytes) const
{
    std::ifstream file(FileHelper::ToFsPath(path), std::ios::binary);
    if (!file.is_open())
    {
        LOGERROR("Failed to open label file: %s", path.c_str());
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

// @brief Decodes a label file with a codec and adds its symbols in file order (a later name or address wins, as before)
bool LabelManager::ImportWith(const unrealasm::symbols::ISymbolCodec& codec, const std::vector<uint8_t>& bytes, const std::string& path)
{
    const auto decoded = codec.Decode(bytes);
    for (const auto& d : decoded.diagnostics)
        LOGDEBUG("%s line %u: %s", path.c_str(), d.line, d.message.c_str());
    if (!decoded.ok)
    {
        LOGERROR("Failed to read label file %s as %s", path.c_str(), codec.Info().id.c_str());
        return false;
    }
    for (const auto& set : decoded.file.sets)
        for (const auto& symbol : set.symbols)
            AddSymbol(symbol);
    return true;
}

// @brief A symbol as a label: the CPU address (a page symbol at its window), the page as bank + bank offset, the kind
// (or the file's own type word) as the type, "code" when the file gave none
bool LabelManager::AddSymbol(const unrealasm::symbols::Symbol& symbol)
{
    using unrealasm::symbols::SpaceKind;
    const auto address = unrealasm::symbols::CpuAddress(symbol);
    if (!address)
        return false;
    uint16_t bank = UINT16_MAX;
    uint16_t bankOffset = UINT16_MAX;
    const SpaceKind kind = symbol.location.space.kind;
    if (kind == SpaceKind::Rom || kind == SpaceKind::Ram || kind == SpaceKind::Cache)
    {
        bank = symbol.location.space.page;
        bankOffset = static_cast<uint16_t>(symbol.location.offset & (PAGE_SIZE - 1));
    }
    std::string type = symbol.kind != unrealasm::symbols::SymbolKind::Unknown ? std::string(unrealasm::symbols::KindName(symbol.kind))
                                                                                 : symbol.provenance.type;
    if (type.empty())
        type = "code";
    return AddLabel(symbol.name, *address, bank, bankOffset, type, symbol.module, symbol.comment, symbol.enabled);
}

// @brief A label as a symbol: a label with a bank is a page symbol (ROM / RAM by its bank type), else the CPU view
unrealasm::symbols::Symbol LabelManager::ToSymbol(const Label& label)
{
    using namespace unrealasm::symbols;
    Symbol s;
    s.name = label.name;
    if (label.bank != UINT16_MAX && label.bank != UINT8_MAX)
    {
        s.location.space.kind = label.isROM() ? SpaceKind::Rom : SpaceKind::Ram;
        s.location.space.page = label.bank;
        s.location.offset = label.bankOffset != UINT16_MAX ? (label.bankOffset & (PAGE_SIZE - 1)) : (label.address & (PAGE_SIZE - 1));
        s.window = label.address >> 14;
    }
    else
        s.location.offset = label.address;
    if (!ParseKind(label.type, s.kind) || s.kind == SymbolKind::Unknown)
    {
        s.kind = SymbolKind::Unknown;
        s.provenance.type = label.type;
    }
    s.module = label.module;
    s.comment = label.comment;
    s.enabled = label.active;
    return s;
}

/// endregion </Symbol codecs>

