#include "stdafx.h"

#include "evoflash.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <limits>
#include <system_error>

#include "3rdparty/digestpp/digestpp.hpp"
#include "common/filehelper.h"
#include "common/logger.h"

#include "debugger/ttd/ttdserializable.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/atm/evoavrwait.h"
#include "emulator/memory/memory.h"

namespace
{
/// The ZX-Evo's base clock: 3.5 MHz (28 MHz / 8), the unit of EvoAvrWait::BaseNow when the core gives none
constexpr double kBaseClockHz = 3'500'000.0;
}  // namespace

EvoFlash::EvoFlash(EmulatorContext* context, Memory* memory, const char* machine)
    : _context(context)
    , _memory(memory)
    , _machine(machine ? machine : "")
    , _chip(kBaseClockHz, kVendor, memory ? memory->ROMBase() : nullptr)
{
    // Window: the whole address space (on the ATM3 any window can show ROM); which windows reach the chip is
    // checked per access
    windowStart = 0x0000;
    windowEnd = 0x10000;
    observesReads = true;
}

EvoFlash::~EvoFlash() = default;

int64_t EvoFlash::Now() const
{
    return static_cast<int64_t>(EvoAvrWait::BaseNow(_context));
}

void EvoFlash::SetWriteWindows(uint8_t mask)
{
    mask &= 0x0F;
    if (mask == _writeWindows)
        return;
    _writeWindows = mask;
    Sync();
}

void EvoFlash::Sync()
{
    if (_context && _context->emulatorState.base_z80_frequency)
        _chip.setUnitsPerSecond(static_cast<double>(_context->emulatorState.base_z80_frequency));
    _chip.update(Now());

    const bool wanted = _writeWindows != 0 || !_chip.arrayMode();
    Core* core = _context ? _context->pCore : nullptr;
    if (!core || !core->GetZ80())
    {
        _installed = false;
        return;
    }
    _installed = core->IsBusOverlayInstalled(this);
    if (wanted == _installed)
        return;
    if (wanted)
    {
        // Core::AddBusOverlay logs a refusal (all overlay slots taken): ROM writes then do not reach the chip
        _installed = core->AddBusOverlay(this);
    }
    else
    {
        core->RemoveBusOverlay(this);
        _installed = false;
    }
}

/// region <Persistence>

namespace
{
std::atomic<bool> g_persistenceAllowed{true};
}  // namespace

void EvoFlash::SetPersistenceAllowed(bool allowed)
{
    g_persistenceAllowed = allowed;
}

bool EvoFlash::PersistenceAllowed()
{
    return g_persistenceAllowed;
}

std::string EvoFlash::Folder() const
{
    return _persistFolder.empty() ? FileHelper::GetWritablePath() : _persistFolder;
}

bool EvoFlash::ReplayOwnsMachine() const
{
    return _context && _context->ttdReplayActive;
}

void EvoFlash::OnRomImageLoaded(size_t imageBytes)
{
    _path.clear();
    _baseDigest.clear();
    _loadedFromFile = false;
    _unsaved = false;
    _seenChanges = _chip.changeCount();
    _quietFrames = 0;
    _chip.reset();  // a new image: the chip reads its array
    if (!_memory || _machine.empty() || !PersistenceAllowed())
        return;

    const size_t bytes = std::min(imageBytes, static_cast<size_t>(Flash29F040B::SIZE));
    _baseDigest = digestpp::sha256().absorb(_chip.data(), bytes).hexdigest();
    const std::string folder = Folder();
    const std::string prefix = std::string(kFilePrefix) + _machine + "-";
    _path = FileHelper::PathCombine(folder, prefix + _baseDigest + ".rom");

    if (FileHelper::FileExists(_path))
    {
        std::vector<uint8_t> saved(Flash29F040B::SIZE);
        if (FileHelper::GetFileSize(_path) == Flash29F040B::SIZE &&
            FileHelper::ReadFileToBuffer(_path, saved.data(), saved.size()) == saved.size())
        {
            std::copy(saved.begin(), saved.end(), _chip.data());
            _loadedFromFile = true;
            LOGINFO("ZX-Evo flash: the reprogrammed ROM '%s' replaces the ROM image", FileHelper::PrintablePath(_path).c_str());
        }
        else
        {
            LOGWARNING("ZX-Evo flash: '%s' is not a 512 KB flash image - ignored", FileHelper::PrintablePath(_path).c_str());
        }
    }

    for (const std::string& other : Status().otherImageFiles)
        LOGWARNING("ZX-Evo flash: '%s' was flashed over another ROM image than the one loaded now - not used",
                   FileHelper::PrintablePath(other).c_str());
}

void EvoFlash::OnFrameEnd()
{
    if (_installed)
        Sync();

    const uint32_t changes = _chip.changeCount();
    if (changes != _seenChanges)
    {
        _seenChanges = changes;
        _unsaved = true;
        _quietFrames = 0;
        return;
    }
    if (_unsaved && ++_quietFrames >= kSaveQuietFrames)
        Save();
}

bool EvoFlash::Save()
{
    if (_path.empty() || ReplayOwnsMachine())
        return false;
    _chip.update(Now());
    if (!_chip.arrayMode())
        return false;  // mid-operation: the frame end saves once the chip is done

    std::error_code ec;
    std::filesystem::create_directories(FileHelper::ToFsPath(Folder()), ec);
    // Written next to the file and renamed over it: a crash mid-write never leaves half an image
    const std::string temporary = _path + ".tmp";
    if (!FileHelper::SaveBufferToFile(temporary, _chip.data(), Flash29F040B::SIZE))
    {
        LOGWARNING("ZX-Evo flash: cannot write '%s'", FileHelper::PrintablePath(temporary).c_str());
        return false;
    }
    std::filesystem::rename(FileHelper::ToFsPath(temporary), FileHelper::ToFsPath(_path), ec);
    if (ec)
    {
        std::filesystem::remove(FileHelper::ToFsPath(temporary), ec);
        LOGWARNING("ZX-Evo flash: cannot save '%s'", FileHelper::PrintablePath(_path).c_str());
        return false;
    }
    _unsaved = false;
    _quietFrames = 0;
    _seenChanges = _chip.changeCount();
    LOGINFO("ZX-Evo flash: saved to '%s'", FileHelper::PrintablePath(_path).c_str());
    return true;
}

void EvoFlash::SaveIfUnsaved()
{
    if (_chip.changeCount() != _seenChanges)
        _unsaved = true;
    if (_unsaved)
        Save();
}

bool EvoFlash::Discard()
{
    if (_path.empty() || ReplayOwnsMachine())
        return false;
    std::error_code ec;
    std::filesystem::remove(FileHelper::ToFsPath(_path), ec);
    _unsaved = false;
    _quietFrames = 0;
    _seenChanges = _chip.changeCount();
    _loadedFromFile = false;
    LOGINFO("ZX-Evo flash: '%s' discarded", FileHelper::PrintablePath(_path).c_str());
    return !ec;
}

EvoFlash::PersistStatus EvoFlash::Status() const
{
    PersistStatus s;
    s.machine = _machine;
    s.path = _path;
    s.baseDigest = _baseDigest;
    s.fileExists = !_path.empty() && FileHelper::FileExists(_path);
    s.loadedFromFile = _loadedFromFile;
    s.unsaved = _unsaved || _chip.changeCount() != _seenChanges;
    s.changes = _chip.changeCount();
    if (_machine.empty())
        return s;

    const std::string prefix = std::string(kFilePrefix) + _machine + "-";
    std::error_code ec;
    for (std::filesystem::directory_iterator it(FileHelper::ToFsPath(Folder()), ec), end; !ec && it != end;
         it.increment(ec))
    {
        const std::string name = FileHelper::FromFsPath(it->path().filename());
        if (name.size() == prefix.size() + 64 + 4 && name.compare(0, prefix.size(), prefix) == 0 &&
            name.compare(name.size() - 4, 4, ".rom") == 0 && name.compare(prefix.size(), 64, _baseDigest) != 0)
            s.otherImageFiles.push_back(FileHelper::FromFsPath(it->path()));
    }
    return s;
}

/// endregion </Persistence>

void EvoFlash::OnMachineReset()
{
    _chip.update(std::numeric_limits<int64_t>::max());
    Sync();
}

bool EvoFlash::ChipOffset(uint16_t addr, uint32_t& offset) const
{
    const uint8_t bank = static_cast<uint8_t>(addr >> 14);
    if (!_memory || !_memory->IsWindowRom(bank))
        return false;
    const uint16_t page = _memory->GetROMPageForBank(bank);
    if (page >= kPages)
        return false;
    offset = static_cast<uint32_t>(page) * kPageSize + (addr & (kPageSize - 1));
    return true;
}

uint8_t EvoFlash::onRead(uint16_t addr, uint8_t normal, [[maybe_unused]] bool isExecution,
                         [[maybe_unused]] bool romPaged)
{
    // Read-array mode: the ROM page already gave the chip's byte
    if (_chip.arrayMode())
        return normal;
    uint32_t offset = 0;
    if (!ChipOffset(addr, offset))
        return normal;
    return _chip.read(offset, Now());
}

void EvoFlash::onWrite(uint16_t addr, uint8_t value, [[maybe_unused]] bool romPaged)
{
    if (!((_writeWindows >> (addr >> 14)) & 1))
        return;
    uint32_t offset = 0;
    if (!ChipOffset(addr, offset))
        return;
    _chip.write(offset, value, Now());
}

/// region <TTD>

void EvoFlash::SaveState(uint8_t* dst) const
{
    dst[0] = kStateVersion;
    _chip.saveState(dst + 1);
}

void EvoFlash::LoadState(const uint8_t* src)
{
    if (src[0] != kStateVersion)
        return;
    _chip.loadState(src + 1);
    Sync();
}

void EvoFlash::TTDRegions(std::vector<ttd::TTDDeviceRegion>& out)
{
    _tracker.Bind(_chip.data(), Flash29F040B::SIZE);
    ttd::TTDDeviceRegion r;
    r.desc.id = ttd::TTDRegionId::EvoFlash;
    r.desc.name = "evo.flash";
    r.desc.ownerType = static_cast<uint16_t>(ttd::PeripheralId::EvoFlash);
    r.desc.memory = _chip.data();
    r.desc.bytes = static_cast<uint32_t>(Flash29F040B::SIZE);
    r.desc.pieces = _tracker.Pieces();
    r.tracker = &_tracker;
    out.push_back(r);
}

void EvoFlash::TTDArmRegions(bool on)
{
    _chip.setTracker(on ? &_tracker : nullptr);
}

/// endregion </TTD>
