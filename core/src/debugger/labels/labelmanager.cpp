#include "labelmanager.h"
#include "symbolfiles.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
#include "unrealasm/symbols/store.h"

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
    : _store(std::make_unique<unrealasm::symbols::SymbolStore>()), _view(std::make_shared<View>())
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

namespace
{
using unrealasm::symbols::Symbol;
using unrealasm::symbols::SymbolSet;

// The set and the place of the symbol a name shows (the highest-priority enabled set, its last record of the name)
bool FindOwner(const std::vector<SymbolSet>& sets, const std::string& name, size_t& set, size_t& index)
{
    bool found = false;
    int best = 0;
    for (size_t i = 0; i < sets.size(); i++)
    {
        if (!sets[i].enabled || (found && sets[i].priority < best))
            continue;
        for (size_t j = sets[i].symbols.size(); j-- > 0;)
        {
            const Symbol& s = sets[i].symbols[j];
            if ((s.name == name || std::find(s.aliases.begin(), s.aliases.end(), name) != s.aliases.end()) && LabelManager::ToLabel(s))
            {
                // the same priority: the later set wins, as in Rebuild
                found = true;
                best = sets[i].priority;
                set = i;
                index = j;
                break;
            }
        }
    }
    return found;
}

SymbolSet UserSet(const unrealasm::symbols::SymbolStore& store)
{
    if (auto set = store.GetSet(LabelManager::USER_SET))
        return std::move(*set);
    SymbolSet set;
    set.id = LabelManager::USER_SET;
    set.title = "User labels";
    set.origin.kind = "user";
    set.priority = LabelManager::USER_SET_PRIORITY;
    return set;
}

// Removes the name's records (and the name from other records' aliases) from a set; true when there were any
bool EraseName(SymbolSet& set, const std::string& name)
{
    const auto it = std::remove_if(set.symbols.begin(), set.symbols.end(), [&](const Symbol& s) { return s.name == name; });
    bool erased = it != set.symbols.end();
    set.symbols.erase(it, set.symbols.end());
    for (Symbol& s : set.symbols)
    {
        const auto alias = std::remove(s.aliases.begin(), s.aliases.end(), name);
        erased = erased || alias != s.aliases.end();
        s.aliases.erase(alias, s.aliases.end());
    }
    return erased;
}

bool HasLabelTraits(const Symbol& s)
{
    return std::any_of(s.traits.begin(), s.traits.end(), [](const std::string& t) { return t.compare(0, 6, "label.") == 0; });
}
}  // namespace

bool LabelManager::AddLabel(const std::string& name, uint16_t z80Address, uint16_t bank, uint16_t bankOffset,
                            const std::string& type, const std::string& module, const std::string& comment, bool active)
{
    if (name.empty())
    {
        return false;
    }

    Label label;
    label.name = name;
    label.address = z80Address;
    label.bank = bank;
    label.bankOffset = bankOffset;
    label.type = type;
    label.module = module;
    label.comment = comment;
    label.active = active;

    // If address is in ROM area (below 0x4000)
    if (z80Address < 0x4000)
    {
        label.setBankTypeROM();
    }

    Symbol base;
    base.provenance.importer = "user";
    Symbol symbol = FromLabel(label, base);
    if (_userOnTop && !_view->byName.count(name))
    {
        // A new name at the end of the top set: it shows by its name and is the last label placed at its address
        auto shown = std::make_shared<Label>(std::move(label));
        _view->byName.emplace(name, shown);
        _view->byAddress[z80Address] = std::move(shown);
        _pendingUser.push_back(std::move(symbol));
    }
    else
    {
        // A name added again moves to the end: the last label placed at an address is the one its address shows
        Flush();
        SymbolSet user = UserSet(*_store);
        EraseName(user, name);
        user.symbols.push_back(std::move(symbol));
        _store->PutSets({std::move(user)});
        Rebuild();
    }

    // Notify about the new label
    Notify();

    return true;
}

// @brief Remove a label by its name
// @param name Name of the label to remove
// @return true if the label was found and removed
// @return false if no label with the given name exists
bool LabelManager::RemoveLabel(const std::string& name)
{
    if (!_view->byName.count(name))
    {
        return false;
    }

    // The name goes from every set, as a label removed is gone whatever file it came from
    Flush();
    std::vector<SymbolSet> changed;
    for (SymbolSet& set : _store->Sets())
        if (EraseName(set, name))
            changed.push_back(std::move(set));
    _store->PutSets(std::move(changed));
    Rebuild();

    // Notify about the removed label
    Notify();

    return true;
}

// @brief Remove all labels from the manager
//
// Clears all internal data structures and frees all allocated resources.
void LabelManager::ClearAllLabels()
{
    // Check if we have any labels before clearing
    bool hadLabels = !_view->byName.empty();

    _pendingUser.clear();
    _store->Clear();
    _nextFilePriority = 100;
    Rebuild();

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
    auto it = _view->byAddress.find(address);
    return it != _view->byAddress.end() ? it->second : nullptr;
}

// @brief Find a label by its name
// @param name Name of the label to find
// @return std::shared_ptr<Label> Pointer to the label if found, nullptr otherwise
std::shared_ptr<Label> LabelManager::GetLabelByName(const std::string& name) const
{
    auto it = _view->byName.find(name);
    return it != _view->byName.end() ? it->second : nullptr;
}

// @brief Get all labels in the manager
// @return std::vector<std::shared_ptr<Label>> Vector containing all labels
std::vector<std::shared_ptr<Label>> LabelManager::GetAllLabels() const
{
    std::vector<std::shared_ptr<Label>> result;
    for (const auto& pair : _view->byName)
    {
        result.push_back(pair.second);
    }
    return result;
}

// @brief Get the total number of labels
// @return size_t Number of labels currently managed
size_t LabelManager::GetLabelCount() const
{
    return _view->byName.size();
}

std::vector<std::shared_ptr<Label>> LabelManager::GetAllLabelsAtAddress(uint16_t address) const
{
    std::vector<std::shared_ptr<Label>> result;
    for (const auto& pair : _view->byName)
    {
        if (pair.second->address == address)
            result.push_back(pair.second);
    }
    return result;
}

std::vector<std::shared_ptr<Label>> LabelManager::GetLabelsByModule(const std::string& module) const
{
    std::vector<std::shared_ptr<Label>> result;
    for (const auto& pair : _view->byName)
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
    for (const auto& pair : _view->byName)
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
    for (const auto& pair : _view->byName)
    {
        if (pair.second->type == type)
            result.push_back(pair.second);
    }
    return result;
}

std::vector<std::shared_ptr<Label>> LabelManager::GetLabelsInRange(uint16_t fromAddr, uint16_t toAddr) const
{
    std::vector<std::shared_ptr<Label>> result;
    for (const auto& pair : _view->byName)
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
    for (const auto& pair : _view->byName)
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

// @brief Changes a label in place; the edited label goes to the user set (a file's own record stays under it, so a
// reload of the file keeps the edit)
bool LabelManager::UpdateLabel(const Label& updatedLabel)
{
    Flush();
    const std::vector<SymbolSet> sets = _store->Sets();
    size_t owner = 0;
    size_t index = 0;
    if (!FindOwner(sets, updatedLabel.name, owner, index))
    {
        // Label with this name does not exist, cannot update
        _logger->Warning(_MODULE, _SUBMODULE, "UpdateLabel failed: Label '%s' not found.", updatedLabel.name.c_str());
        return false;
    }

    const Symbol& existing = sets[owner].symbols[index];
    const std::optional<Label> existingLabel = ToLabel(existing);

    // Update label properties (name is the key, so it's not changed here; the bank type stays)
    Label label = updatedLabel;
    label.bankType = existingLabel->bankType;

    // The edit goes to the user set (in the file's own set while the user set is off); a label at a new address moves
    // to the end: the last one placed at an address shows there
    const auto userSet = _store->GetSet(USER_SET);
    const bool inPlace = sets[owner].id == USER_SET || (userSet && !userSet->enabled);
    SymbolSet target = inPlace ? sets[owner] : UserSet(*_store);
    Symbol symbol = FromLabel(label, existing);
    if (inPlace && label.address == existingLabel->address)
        target.symbols[index] = std::move(symbol);
    else
    {
        EraseName(target, label.name);
        target.symbols.push_back(std::move(symbol));
    }
    _store->PutSets({std::move(target)});
    Rebuild();

    _logger->Debug(_MODULE, _SUBMODULE, "Label '%s' updated successfully.", label.name.c_str());

    // Notify about the updated label
    Notify();

    return true;
}

/// endregion </Label management>

/// region <Symbol sets>

std::vector<unrealasm::symbols::SymbolSet> LabelManager::GetSymbolSets() const
{
    Flush();
    return _store->Sets();
}

std::shared_ptr<const unrealasm::symbols::SymbolIndex> LabelManager::GetSymbolIndex() const
{
    Flush();
    return _store->Index();
}

bool LabelManager::SetSymbolSetEnabled(const std::string& id, bool enabled)
{
    Flush();
    if (!_store->SetEnabled(id, enabled))
        return false;
    Rebuild();
    Notify();
    return true;
}

bool LabelManager::SetSymbolSetPriority(const std::string& id, int priority)
{
    Flush();
    if (!_store->SetPriority(id, priority))
        return false;
    Rebuild();
    Notify();
    return true;
}

bool LabelManager::DropSymbolSet(const std::string& id)
{
    Flush();
    if (!_store->Drop(id))
        return false;
    Rebuild();
    Notify();
    return true;
}

// @brief Sets by priority (a later set first among equals), each set's records in order, a record's aliases before its
// name; a name shows its last record. Records without a main CPU address show nowhere
std::vector<std::pair<const unrealasm::symbols::Symbol*, std::string>> LabelManager::Resolve(const std::vector<SymbolSet>& sets,
                                                                                            const std::vector<std::string>* only)
{
    std::vector<const SymbolSet*> order;
    for (const SymbolSet& set : sets)
        if (only ? std::find(only->begin(), only->end(), set.id) != only->end() : set.enabled)
            order.push_back(&set);
    std::stable_sort(order.begin(), order.end(), [](const SymbolSet* a, const SymbolSet* b) { return a->priority < b->priority; });

    std::vector<std::pair<const Symbol*, std::string>> placed;
    std::unordered_map<std::string, size_t> last;
    for (const SymbolSet* set : order)
        for (const Symbol& symbol : set->symbols)
        {
            if (!unrealasm::symbols::CpuAddress(symbol))
                continue;
            for (const std::string& alias : symbol.aliases)
            {
                last[alias] = placed.size();
                placed.emplace_back(&symbol, alias);
            }
            last[symbol.name] = placed.size();
            placed.emplace_back(&symbol, symbol.name);
        }
    std::vector<std::pair<const Symbol*, std::string>> shown;
    shown.reserve(last.size());
    for (size_t i = 0; i < placed.size(); i++)
        if (last[placed[i].second] == i)
            shown.push_back(std::move(placed[i]));
    return shown;
}

// @brief The labels of the enabled sets (Resolve): a name shows its last record, an address the last label placed at it
void LabelManager::Rebuild()
{
    Flush();
    const auto index = _store->Index();

    // The user set (or the one AddLabel makes) comes last: a new name there needs no rebuild
    const SymbolSet* user = nullptr;
    int above = std::numeric_limits<int>::min();
    bool userOff = false;
    for (const SymbolSet& set : index->Sets())
    {
        if (set.id == USER_SET)
        {
            if (set.enabled)
                user = &set;
            else
                userOff = true;
        }
        else if (set.enabled)
            above = std::max(above, set.priority);
    }
    _userOnTop = !userOff && (user ? user->priority > above : USER_SET_PRIORITY > above);

    auto view = std::make_shared<View>();
    for (const auto& [symbol, name] : Resolve(index->Sets()))
    {
        auto label = std::make_shared<Label>(*ToLabel(*symbol));
        label->name = name;
        view->byName.emplace(name, label);
        view->byAddress[label->address] = std::move(label);
    }
    _view = std::move(view);
}

// @brief The labels AddLabel only put in the view join the user set
void LabelManager::Flush() const
{
    if (_pendingUser.empty())
        return;
    SymbolSet user = UserSet(*_store);
    std::move(_pendingUser.begin(), _pendingUser.end(), std::back_inserter(user.symbols));
    _pendingUser.clear();
    _store->PutSets({std::move(user)});
}

void LabelManager::Notify()
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_LABEL_CHANGED, nullptr, true);
}

/// endregion </Symbol sets>

/// region <File operations>

// @brief Load labels from a file, auto-detecting the file format
// @param path Path to the file containing the labels
// @return true if the file was loaded successfully
// @return false if the file could not be opened or parsed
bool LabelManager::LoadLabels(const std::string& path)
{
    // The extension decides as it always did (.map, .sym, .vice, .s / .asm, .z88); other files by their content
    return ImportSymbols(path, {}).ok;
}

// @brief Load labels from a map file
// @param path Path to the map file
// @return true if the file was loaded successfully
// @return false if the file could not be opened or parsed
bool LabelManager::LoadMapFile(const std::string& path)
{
    SymbolImportRequest request;
    request.format = "unreal-map";
    return ImportSymbols(path, request).ok;
}

// @brief Load labels from a symbol file
// @param path Path to the symbol file
// @return true if the file was loaded successfully
// @return false if the file could not be opened or parsed
bool LabelManager::LoadSymFile(const std::string& path)
{
    SymbolImportRequest request;
    request.format = "simple-sym";
    return ImportSymbols(path, request).ok;
}

// @brief Reads a symbol file into the store. Without a set, space, base or policy the file is a set of its own above
// the files loaded before (a file loaded again replaces its set; inside a file a later name or address wins, as
// LoadLabels always did); with one of them its records are normalized and merged into the set (DT-1, DT-2)
SymbolImportResult LabelManager::ImportSymbols(const std::string& path, const SymbolImportRequest& request)
{
    using namespace unrealasm::symbols;
    SymbolImportResult result;
    std::vector<uint8_t> bytes;
    if (path.empty() || !ReadLabelFile(path, bytes))
    {
        result.message = "Cannot read the symbol file: " + path;
        return result;
    }
    std::string reason;
    const ISymbolCodec* codec = CodecForFile(path, bytes, request.format, result.score, reason);
    if (!codec)
    {
        LOGERROR("%s", reason.c_str());
        result.message = reason;
        return result;
    }
    result.format = codec->Info().id;
    SymbolDecodeResult decoded = codec->Decode(bytes);
    for (const auto& d : decoded.diagnostics)
        LOGDEBUG("%s line %u: %s", path.c_str(), d.line, d.message.c_str());
    if (!decoded.ok)
    {
        LOGERROR("Failed to read label file %s as %s", path.c_str(), result.format.c_str());
        result.message = "Failed to read " + path + " as " + result.format;
        result.report.diagnostics = std::move(decoded.diagnostics);
        return result;
    }
    for (const SymbolSet& set : decoded.file.sets)
        result.records += set.symbols.size();

    Flush();
    const std::string title = FileHelper::ToFsPath(path).filename().string();
    if (request.set.empty() && !request.space && request.base == 0 && !request.policy)
    {
        const bool several = decoded.file.sets.size() > 1;
        std::vector<SymbolSet> sets;
        for (SymbolSet& set : decoded.file.sets)
        {
            SymbolSet file = std::move(set);
            if (file.title.empty())
                file.title = title;
            file.id = "file:" + path + (several ? "#" + file.id : std::string());
            file.origin.kind = "file";
            file.origin.where = path;
            file.priority = _nextFilePriority++;
            file.enabled = true;
            sets.push_back(std::move(file));
        }
        result.report.set = sets.empty() ? "file:" + path : sets.front().id;
        result.report.added = result.records;
        result.report.diagnostics = std::move(decoded.diagnostics);
        result.report.ok = true;
        _store->PutSets(std::move(sets));
    }
    else
    {
        ImportOptions options;
        options.set = request.set.empty() ? "file:" + path : request.set;
        options.title = title;
        options.origin.kind = "file";
        options.origin.where = path;
        options.space = request.space;
        options.base = request.base;
        options.policy = request.policy.value_or(MergePolicy::Both);
        const bool existed = _store->GetSet(options.set).has_value();
        std::vector<Symbol> records;
        records.reserve(result.records);
        for (SymbolSet& set : decoded.file.sets)
            std::move(set.symbols.begin(), set.symbols.end(), std::back_inserter(records));
        result.report = _store->Import(std::move(records), options);
        result.report.diagnostics.insert(result.report.diagnostics.begin(), decoded.diagnostics.begin(), decoded.diagnostics.end());
        if (!result.report.ok)
            result.message = "A conflict stopped the merge (policy fail): nothing changed";
        else if (!existed)
            _store->SetPriority(options.set, _nextFilePriority++);
    }
    Rebuild();
    Notify();
    result.ok = result.report.ok;
    return result;
}

// @brief Writes symbols in a format: the labels as they show (the given sets' when `sets` names some) as one set, or
// for the native format the sets themselves (every set when none is named)
SymbolExportResult LabelManager::ExportSymbols(const std::string& path, const SymbolExportRequest& request) const
{
    using namespace unrealasm::symbols;
    SymbolExportResult result;
    const auto& registry = SymbolCodecRegistry::Builtin();
    const ISymbolCodec* codec = nullptr;
    if (!request.format.empty())
        codec = registry.Find(request.format);
    else
    {
        std::string extension = FileHelper::ToFsPath(path).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        if (!extension.empty() && extension[0] == '.')
            extension.erase(0, 1);
        codec = registry.Find(CodecForExtension(extension));
        for (size_t i = 0; !codec && !extension.empty() && i < registry.All().size(); i++)
        {
            const auto& extensions = registry.All()[i]->Info().extensions;
            if (std::find(extensions.begin(), extensions.end(), extension) != extensions.end())
                codec = registry.All()[i].get();
        }
    }
    if (!codec)
    {
        result.message = request.format.empty() ? "No symbol format for the extension of " + path + ": name one"
                                                : "Unknown symbol format: " + request.format;
        return result;
    }
    result.format = codec->Info().id;

    Flush();
    const std::vector<SymbolSet> sets = _store->Sets();
    for (const std::string& id : request.sets)
        if (std::none_of(sets.begin(), sets.end(), [&](const SymbolSet& set) { return set.id == id; }))
        {
            result.message = "No symbol set: " + id;
            return result;
        }
    SymbolFile file;
    if (codec->Info().family == Family::Native)
    {
        for (const SymbolSet& set : sets)
            if (request.sets.empty() || std::find(request.sets.begin(), request.sets.end(), set.id) != request.sets.end())
                file.sets.push_back(set);
    }
    else
    {
        file.sets.emplace_back();
        std::vector<Symbol>& symbols = file.sets[0].symbols;
        for (const auto& [symbol, name] : Resolve(sets, request.sets.empty() ? nullptr : &request.sets))
        {
            // A label set by hand as the label shows it; a file's record as the file had it
            Symbol s = HasLabelTraits(*symbol) ? ToSymbol(*ToLabel(*symbol)) : *symbol;
            s.name = name;
            s.aliases.clear();
            symbols.push_back(std::move(s));
        }
        std::stable_sort(symbols.begin(), symbols.end(),
                         [](const Symbol& a, const Symbol& b) { return CpuAddress(a).value_or(0) < CpuAddress(b).value_or(0); });
    }
    SymbolEncodeOptions options;
    options.unrepresentable = request.pages;
    SymbolEncodeResult encoded = codec->Encode(file, options);
    result.diagnostics = std::move(encoded.diagnostics);
    result.written = encoded.written;
    if (!encoded.ok)
    {
        result.message = "Cannot write the symbols as " + result.format;
        return result;
    }
    std::ofstream out(FileHelper::ToFsPath(path), std::ios::binary);
    out.write(reinterpret_cast<const char*>(encoded.bytes.data()), static_cast<std::streamsize>(encoded.bytes.size()));
    if (!out.good())
    {
        result.message = "Cannot write " + path;
        return result;
    }
    result.ok = true;
    return result;
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
    // Every label by address (names sharing an address each get their line)
    std::vector<std::shared_ptr<Label>> labels = GetAllLabels();
    std::stable_sort(labels.begin(), labels.end(), [](const auto& a, const auto& b) { return a->address < b->address; });
    for (const auto& label : labels)
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

// @brief The codec for a file: the format named; else the codec LabelManager has always picked for the extension (a
// .map whose content is z80asm's goes to z88dk-map); else the one detection chooses
const unrealasm::symbols::ISymbolCodec* LabelManager::CodecForFile(const std::string& path, const std::vector<uint8_t>& bytes,
                                                                   const std::string& format, int& score, std::string& reason)
{
    using namespace unrealasm::symbols;
    const auto& registry = SymbolCodecRegistry::Builtin();
    if (!format.empty())
    {
        const ISymbolCodec* codec = registry.Find(format);
        if (!codec)
        {
            reason = "Unknown symbol format: " + format + " (formats:";
            for (const auto& c : registry.All())
                reason += " " + c->Info().id;
            reason += ")";
            return nullptr;
        }
        score = 100;
        return codec;
    }
    std::string extension = FileHelper::ToFsPath(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    if (!extension.empty() && extension[0] == '.')
        extension.erase(0, 1);
    const ISymbolCodec* codec = registry.Find(CodecForExtension(extension));
    // z80asm writes its map (-m) as .map too: its content says so
    if (extension == "map")
    {
        const auto detected = registry.Detect(bytes, extension);
        if (detected.chosen && detected.chosen->Info().id == "z88dk-map")
            codec = detected.chosen;
    }
    if (codec)
    {
        Probe probe;
        probe.bytes = std::span<const uint8_t>(bytes.data(), std::min(bytes.size(), SymbolCodecRegistry::kProbeBytes));
        probe.extension = extension;
        score = codec->Detect(probe);
        return codec;
    }
    const auto detected = registry.Detect(bytes, extension);
    if (!detected.chosen)
    {
        reason = "Unsupported label file format: " + path + " (" + detected.reason + ")";
        return nullptr;
    }
    score = detected.candidates.empty() ? 0 : detected.candidates.front().score;
    return detected.chosen;
}

// @brief A symbol as a label: the CPU address (a page symbol at its window), the page as bank + bank offset, the kind
// (or the file's own type word) as the type, "code" when the file gave none; the bank type by the address (ROM below
// #4000); the "label.*" traits FromLabel left override each field
std::optional<Label> LabelManager::ToLabel(const unrealasm::symbols::Symbol& symbol)
{
    using unrealasm::symbols::SpaceKind;
    const auto address = unrealasm::symbols::CpuAddress(symbol);
    if (!address)
        return std::nullopt;
    Label label;
    label.name = symbol.name;
    label.address = *address;
    const SpaceKind kind = symbol.location.space.kind;
    if (kind == SpaceKind::Rom || kind == SpaceKind::Ram || kind == SpaceKind::Cache)
    {
        label.bank = symbol.location.space.page;
        label.bankOffset = static_cast<uint16_t>(symbol.location.offset & (PAGE_SIZE - 1));
    }
    label.type = symbol.kind != unrealasm::symbols::SymbolKind::Unknown ? std::string(unrealasm::symbols::KindName(symbol.kind))
                                                                         : symbol.provenance.type;
    if (label.type.empty())
        label.type = "code";
    label.module = symbol.module;
    label.comment = symbol.comment;
    label.active = symbol.enabled;
    label.bankType = label.address < 0x4000 ? BANK_ROM : BANK_RAM;
    for (const std::string& trait : symbol.traits)
    {
        if (trait.compare(0, 6, "label.") != 0)
            continue;
        const size_t eq = trait.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = trait.substr(6, eq - 6);
        const std::string value = trait.substr(eq + 1);
        const auto number = [&]() { return static_cast<uint16_t>(std::strtoul(value.c_str(), nullptr, 16)); };
        if (key == "address")
            label.address = number();
        else if (key == "bank")
            label.bank = number();
        else if (key == "bankOffset")
            label.bankOffset = number();
        else if (key == "bankType")
            label.bankType = value == "rom" ? BANK_ROM : BANK_RAM;
        else if (key == "type")
            label.type = value;
    }
    return label;
}

// @brief A label as a stored symbol: the page (ROM / RAM by its bank type) when it has a bank, else the CPU view; the
// type as a kind when it is one's exact name, else the type word; what ToLabel would not give back is kept as
// "label.<field>=<value>" traits (numbers in hex)
unrealasm::symbols::Symbol LabelManager::FromLabel(const Label& label, const unrealasm::symbols::Symbol& base)
{
    using namespace unrealasm::symbols;
    Symbol s = base;
    s.name = label.name;
    s.aliases.clear();
    s.location = {};
    s.window = -1;
    if (label.bank != UINT16_MAX)
    {
        const bool cache = base.location.space.kind == SpaceKind::Cache;
        s.location.space.kind = cache ? SpaceKind::Cache : label.isROM() ? SpaceKind::Rom : SpaceKind::Ram;
        s.location.space.page = label.bank;
        s.location.offset = (label.bankOffset != UINT16_MAX ? label.bankOffset : label.address) & (PAGE_SIZE - 1);
        s.window = label.address >> 14;
    }
    else
        s.location.offset = label.address;
    if (!ParseKind(label.type, s.kind) || KindName(s.kind) != label.type)
    {
        s.kind = SymbolKind::Unknown;
        s.provenance.type = label.type;
    }
    else
        s.provenance.type.clear();
    s.module = label.module;
    s.comment = label.comment;
    s.enabled = label.active;
    s.traits.erase(std::remove_if(s.traits.begin(), s.traits.end(), [](const std::string& t) { return t.compare(0, 6, "label.") == 0; }),
                   s.traits.end());

    const std::optional<Label> shown = ToLabel(s);
    const auto hex = [](unsigned value) {
        std::ostringstream out;
        out << std::hex << std::uppercase << value;
        return out.str();
    };
    if (!shown || shown->address != label.address)
        s.traits.push_back("label.address=" + hex(label.address));
    if (shown && shown->bank != label.bank)
        s.traits.push_back("label.bank=" + hex(label.bank));
    if (shown && shown->bankOffset != label.bankOffset)
        s.traits.push_back("label.bankOffset=" + hex(label.bankOffset));
    if (!shown || (shown->bankType == BANK_ROM) != label.isROM())
        s.traits.push_back(std::string("label.bankType=") + (label.isROM() ? "rom" : "ram"));
    if (shown && shown->type != label.type)
        s.traits.push_back("label.type=" + label.type);
    return s;
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

