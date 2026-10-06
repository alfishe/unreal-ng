#include "stdafx.h"

#include "common/modulelogger.h"

#include "loader_sna.h"

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "loaders/snapshot/snapshotcapture.h"

/// region <Constructors / destructors>

LoaderSNA::LoaderSNA(EmulatorContext* context, const std::string& path)
{
    _context = context;
    _logger = context->pModuleLogger;

    _path = FileHelper::AbsolutePath(path, false);  // Expand tilde, don't resolve symlinks for non-existent files

    // Initialize all staging buffers to zero for deterministic state
    // Prevents uninitialized POD members from causing inconsistent snapshot loading
    memset(&_header, 0, sizeof(_header));
    memset(&_ext128Header, 0, sizeof(_ext128Header));

    // Initialize memory pages with proper size
    for (int i = 0; i < 8; i++)
    {
        memset(_memoryPages[i], 0x00, PAGE_SIZE);
        _memoryPagesUsed[i] = false;
    }
}

LoaderSNA::LoaderSNA(EmulatorContext* context, std::vector<uint8_t> data, const std::string& name)
    : LoaderSNA(context, name)
{
    _path = name;
    _data = std::move(data);
    _fromMemory = true;
}

LoaderSNA::~LoaderSNA() = default;

/// endregion </Constructors / destructors>

/// region <Public methods>

/// Multi-stage snapshot loading
/// Guarantees that if SNA file is invalid / corrupted - current emulator session and memory content preserved
/// \return result for snapshot load operation
bool LoaderSNA::load()
{
    bool result = false;

    if (validate())
    {
        if (loadToStaging())
        {
            if (planSnapshot())
            {
                result = _decision.action == snapshot::Decision::Action::Take
                             ? _decision.Commit(_image, *_context, _report)
                             : applySnapshotFromStaging();
            }
        }
    }

    return result;
}

/// The staging as a format-neutral image (logical banks; a 48K file's PC is taken off its stack, as the commit does)
snapshot::Image LoaderSNA::BuildImage() const
{
    snapshot::Image image;
    image.format = "sna";
    image.sourcePath = _path;
    image.timingHint = _snapshotMode == SNA_48 ? "48k" : "128k";
    image.machineHint = _snapshotMode == SNA_48 ? "48k" : "128k-family";
    image.memoryModel = _snapshotMode == SNA_48 ? snapshot::MemoryModel::Mem48k : snapshot::MemoryModel::Mem128k;
    image.formatVersion = _snapshotMode == SNA_48 ? "48" : "128";

    for (uint16_t bank = 0; bank < 8; ++bank)
    {
        if (_memoryPagesUsed[bank])
            image.banks[bank] = std::vector<uint8_t>(_memoryPages[bank], _memoryPages[bank] + PAGE_SIZE);
    }

    snapshot::Cpu& cpu = image.cpu;
    const snaHeader& h = _header;
    cpu.af = static_cast<uint16_t>(h.a << 8 | h.f);
    cpu.bc = static_cast<uint16_t>(h.b << 8 | h.c);
    cpu.de = static_cast<uint16_t>(h.d << 8 | h.e);
    cpu.hl = static_cast<uint16_t>(h.h << 8 | h.l);
    cpu.ix = static_cast<uint16_t>(h.hx << 8 | h.lx);
    cpu.iy = static_cast<uint16_t>(h.hy << 8 | h.ly);
    cpu.af2 = static_cast<uint16_t>(h._a << 8 | h._f);
    cpu.bc2 = static_cast<uint16_t>(h._b << 8 | h._c);
    cpu.de2 = static_cast<uint16_t>(h._d << 8 | h._e);
    cpu.hl2 = static_cast<uint16_t>(h._h << 8 | h._l);
    cpu.sp = static_cast<uint16_t>(h.hsp << 8 | h.lsp);
    cpu.i = h.i;
    cpu.r = h.r;
    // Byte 19 bit 2 is IFF2; the commit sets IFF1 from the same bit (the format was born as an NMI-taken image)
    cpu.iff2 = (h.flag19 & 0b100u) != 0;
    cpu.iff1 = cpu.iff2;
    cpu.im = h.imod & 0x03u;
    image.border = static_cast<uint8_t>(h.border & 0b111u);

    if (_snapshotMode == SNA_128)
    {
        cpu.pc = _ext128Header.reg_PC;
        image.paging.p7FFD = _ext128Header.port_7FFD;
        image.trdosPaged = _ext128Header.is_TRDOS != 0;
    }
    else
    {
        // The PC is on the stack: the low byte at SP, the high at SP + 1 (banks 5, 2, 0 at #4000, #8000, #C000)
        auto byteAt = [&](uint16_t address, uint8_t& out) {
            const uint16_t bank = address >= 0xC000 ? 0 : address >= 0x8000 ? 2 : address >= 0x4000 ? 5 : 0xFFFF;
            const auto it = image.banks.find(bank);
            if (it == image.banks.end())
                return false;
            out = it->second[address & 0x3FFF];
            return true;
        };
        uint8_t lo = 0, hi = 0;
        if (byteAt(cpu.sp, lo) && byteAt(static_cast<uint16_t>(cpu.sp + 1), hi))
        {
            cpu.pc = static_cast<uint16_t>(hi << 8 | lo);
            cpu.sp = static_cast<uint16_t>(cpu.sp + 2);
        }
        else
        {
            // SP in the ROM or at the top of memory: the machine's memory at SP decides (the commit pops it)
            cpu.pcOnMachineStack = true;
            image.warnings.push_back("the stack pointer is in the ROM: the PC cannot be read off the stack here");
        }
    }
    return image;
}

bool LoaderSNA::Stage()
{
    if (!validate() || !loadToStaging())
        return false;
    _image = BuildImage();
    return true;
}

bool LoaderSNA::planSnapshot()
{
    _image = BuildImage();
    _report = snapshot::Report();
    _decision = snapshot::Pipeline::Plan(_image, _context, _options, _report);
    return _decision.Proceeds();
}

/// endregion </Public methods>

/// region <Helper methods>
bool LoaderSNA::validate()
{
    bool result = false;

    // The snapshot bytes: given in memory, or the whole file (read once; no
    // handle stays open)
    bool haveData = _fromMemory;
    if (!_fromMemory && FileHelper::FileExists(_path))
    {
        const size_t size = FileHelper::GetFileSize(_path);
        _data.assign(size, 0);
        haveData = size == 0 || FileHelper::ReadFileToBuffer(_path, _data.data(), size) == size;
    }
    if (haveData)
    {
        {
            _fileSize = _data.size();
            _readPos = 0;

            if (is48kSnapshot())
            {
                _snapshotMode = SNA_48;
                
                // 48K SNA must be exactly 49179 bytes (27 header + 49152 RAM)
                const size_t expected48kSize = _snaHeaderSize + 3 * PAGE_SIZE;
                if (_fileSize != expected48kSize)
                {
                    MLOGWARNING("Invalid 48K SNA file size: %zu (expected %zu)", _fileSize, expected48kSize);
                    _snapshotMode = SNA_UNKNOWN;
                }
            }
            else if (is128kSnapshot())
            {
                _snapshotMode = SNA_128;
                
                // 128K SNA format is flexible - minimum is 49183 bytes (base structure),
                // plus 0-8 additional 16KB RAM banks depending on which banks were saved
                // Common sizes: 131103 bytes (5 banks), 147487 bytes (8 banks)
                const size_t min128kSize = _snaHeaderSize + 3 * PAGE_SIZE + sizeof(sna128Header);
                size_t remainingBytes = _fileSize - min128kSize;
                size_t additionalBanks = remainingBytes / PAGE_SIZE;
                
                // Validate that we have whole banks (no partial pages)
                if (remainingBytes % PAGE_SIZE != 0)
                {
                    MLOGWARNING("Invalid 128K SNA file size: %zu (has partial page: %zu bytes)", _fileSize, remainingBytes % PAGE_SIZE);
                    _snapshotMode = SNA_UNKNOWN;
                }
                // Validate we have at least 1 additional bank (128K SNA must have more than just base structure)
                else if (additionalBanks < 1)
                {
                    MLOGWARNING("Invalid 128K SNA: no additional banks (size %zu)", _fileSize);
                    _snapshotMode = SNA_UNKNOWN;
                }
                // Validate we don't have more than 8 additional banks
                else if (additionalBanks > 8)
                {
                    MLOGWARNING("Invalid 128K SNA: too many banks (%zu banks, max 8)", additionalBanks);
                    _snapshotMode = SNA_UNKNOWN;
                }
            }
        }
    }

    if (_snapshotMode != SNA_UNKNOWN)
    {
        result = true;
    }

    // Persist validation state in the field
    _fileValidated = result;

    /// region <Info logging>
    if (result)
    {
        std::string version;
        if (_snapshotMode == SNA_48)
        {
            version = "SNA48";
        }
        if (_snapshotMode == SNA_128)
        {
            version = "SNA128";
        }

        MLOGINFO("Valid SNA file, type: %s, size: %d path: '%s'", version.c_str(), _fileSize, _path.c_str());
    }
    else
    {
        std::string version = "UNKNOWN";
        if (_snapshotMode == SNA_48)
        {
            version = "SNA48";
        }
        if (_snapshotMode == SNA_128)
        {
            version = "SNA128";
        }

        MLOGWARNING("File is not valid SNA, type: %s, size: %d '%s'", version.c_str(), _fileSize, _path.c_str());
    }
    /// endregion </Info logging>

    return result;
}

bool LoaderSNA::Read(void* target, size_t size)
{
    if (size > _data.size() || _readPos > _data.size() - size)
        return false;
    memcpy(target, _data.data() + _readPos, size);
    _readPos += size;
    return true;
}

bool LoaderSNA::is48kSnapshot() const
{
    bool result = false;

    size_t fileSize = _data.size();
    
    // Minimum size check: must have at least header (27 bytes)
    if (fileSize < _snaHeaderSize)
    {
        return false;
    }
    
    size_t headerSize = fileSize % PAGE_SIZE;

    if (headerSize == _snaHeaderSize)
    {
        result = true;
    }

    return result;
}

bool LoaderSNA::is128kSnapshot() const
{
    bool result = false;

    size_t fileSize = _data.size();
    
    // Minimum size for 128K SNA: header (27) + 3 banks (48KB) + extended header (4)
    const size_t min128kSize = _snaHeaderSize + 3 * PAGE_SIZE + sizeof(sna128Header);
    
    if (fileSize < min128kSize)
    {
        return false;
    }
    
    // 128K SNA format is flexible - after the base structure (49183 bytes),
    // it can contain anywhere from 1 to 8 additional 16KB RAM banks
    // Check if remaining data after base structure is a multiple of PAGE_SIZE and non-zero
    size_t remainingBytes = fileSize - min128kSize;
    
    if (remainingBytes > 0 && remainingBytes % PAGE_SIZE == 0)
    {
        result = true;
    }

    return result;
}

bool LoaderSNA::loadToStaging()
{
    bool result = false;
    _image = snapshot::Image();   // staged afresh: an image built from an earlier staging is stale

    switch (_snapshotMode)
    {
        case SNA_48:
            result = load48kToStaging();
            break;
        case SNA_128:
            result = load128kToStaging();
            break;
        default:
            break;
    }

    return result;
}

bool LoaderSNA::load48kToStaging()
{
    bool result = false;

    if (_snapshotMode == SNA_48 && !_data.empty())
    {
        // Ensure we're reading from file start
        _readPos = 0;

        // Read SNA common header
        if (!Read(&_header, sizeof(_header)))
        {
            return false;
        }

        // Read 48K RAM (3 x 16KB pages)
        // Bank 5 [4000:7FFF]
        if (!Read(&_memoryPages[5], PAGE_SIZE))
        {
            return false;
        }
        _memoryPagesUsed[5] = true;

        // Bank 2 [8000:BFFF]
        if (!Read(&_memoryPages[2], PAGE_SIZE))
        {
            return false;
        }
        _memoryPagesUsed[2] = true;

        // Bank 0 [C000:FFFF]
        if (!Read(&_memoryPages[0], PAGE_SIZE))
        {
            return false;
        }
        _memoryPagesUsed[0] = true;

        // Only set the staging flag after all reads succeed
        _stagingLoaded = true;
        result = true;
    }

    return result;
}

bool LoaderSNA::load128kToStaging()
{
    bool result = true;

    if (_snapshotMode == SNA_128 && !_data.empty())
    {
        int memoryPagesToLoad = (_fileSize - sizeof(snaHeader) - 3 * PAGE_SIZE - sizeof (sna128Header)) / PAGE_SIZE;

        // Ensure we're reading from file start
        _readPos = 0;

        // Read SNA common header
        if (!Read(&_header, sizeof(_header)))
        {
            result = false;
        }

        // Read Bank 5 [4000:7FFF]
        if (result)
        {
            if (!Read(&_memoryPages[5], PAGE_SIZE))
            {
                result = false;
            }
            else
            {
                _memoryPagesUsed[5] = true;
            }
        }

        // Read Bank 2 [8000:BFFF]
        if (result)
        {
            if (!Read(&_memoryPages[2], PAGE_SIZE))
            {
                result = false;
            }
            else
            {
                _memoryPagesUsed[2] = true;
            }
        }

        // Read Bank N [C000:FFFF]
        // It will go to the page mapped by port #7FFD value
        if (result)
        {
            if (!Read(&_memoryPages[0], PAGE_SIZE))
            {
                result = false;
            }
            else
            {
                _memoryPagesUsed[0] = true;
            }
        }

        // Read extended SNA header
        if (result)
        {
            if (!Read(&_ext128Header, sizeof(_ext128Header)))
            {
                result = false;
            }
        }

        // Memory page mapped to [C000:FFFF]
        if (result)
        {
            uint8_t currentTopPage = _ext128Header.port_7FFD & 0x07u;

            // Move Page 0 content loaded previously to mapped RAM page
            // The third bank in the SNA file was loaded into _memoryPages[0],
            // but it actually belongs to the page selected by port_7FFD bits 0-2
            if (currentTopPage != 0 && currentTopPage != 5 && currentTopPage != 2)
            {
                // Normal case: move page 0 data to the actual target page
                _memoryPagesUsed[0] = false;
                memcpy(&_memoryPages[currentTopPage], &_memoryPages[0], PAGE_SIZE);
                memset(&_memoryPages[0], 0x00, PAGE_SIZE);
                _memoryPagesUsed[currentTopPage] = true;
            }
            else if (currentTopPage == 5 || currentTopPage == 2)
            {
                // Edge case: currentTopPage is the same as a fixed bank page (5 or 2)
                // The third bank's data duplicates page 5 or 2 (already loaded)
                // Discard the duplicate and mark page 0 as unused
                _memoryPagesUsed[0] = false;
                memset(&_memoryPages[0], 0x00, PAGE_SIZE);
            }
            // If currentTopPage == 0, data is already in the right place

            // Load all the rest RAM pages from 128k extended section
            if (memoryPagesToLoad > 0)
            {
                int pagesRead = 0;
                for (int pageNum = 0; pageNum < 8; pageNum++)
                {
                    if (pagesRead == memoryPagesToLoad)
                        break;

                    // All those pages were already loaded
                    if (_memoryPagesUsed[pageNum])
                        continue;

                    // Load next page
                    if (!Read(&_memoryPages[pageNum], PAGE_SIZE))
                    {
                        result = false;
                        break;
                    }
                    pagesRead++;
                    _memoryPagesUsed[pageNum] = true;
                }
            }
        }

        // Only set the staging flag after all reads succeed
        // Prevents applySnapshotFromStaging() from using partial/garbage data
        if (result)
        {
            _stagingLoaded = true;
        }
    }

    return result;
}

bool LoaderSNA::applySnapshotFromStaging()
{
    bool result = false;

    Memory& memory = *_context->pMemory;
    Screen& screen = *_context->pScreen;
    Core& core = *_context->pCore;
    Z80& z80 = *_context->pCore->GetZ80();
    int ramPagesLoaded = 0;

    if (_stagingLoaded)
    {
        // The commit reads the image (snapshot pipeline P9), not the staging buffers. load() has built it for the plan;
        // a caller that stages and applies by hand has not
        if (_image.format.empty())
            _image = BuildImage();
        const snapshot::Image& image = _image;
        const snapshot::Cpu& cpu = image.cpu;
        const bool is48 = image.memoryModel == snapshot::MemoryModel::Mem48k;

        // Reset Z80 and all peripherals
        core.Reset();
        // A model whose reset leaves an extended paging on (the Pentagon 1024) goes back to the plain 128K form
        _context->pPortDecoder->EnterSpectrum128Paging(z80.pc);

        // Transfer RAM data to emulator (only the banks the snapshot has)
        for (const auto& bank : image.banks)
        {
            memory.LoadRAMPageData(bank.first, const_cast<uint8_t*>(bank.second.data()), PAGE_SIZE);
            ramPagesLoaded++;
        }

        // Transfer registers
        z80.af = cpu.af;
        z80.bc = cpu.bc;
        z80.de = cpu.de;
        z80.hl = cpu.hl;
        z80.alt.af = cpu.af2;
        z80.alt.bc = cpu.bc2;
        z80.alt.de = cpu.de2;
        z80.alt.hl = cpu.hl2;
        z80.ix = cpu.ix;
        z80.iy = cpu.iy;
        z80.sp = cpu.sp;

        z80.i = cpu.i;
        z80.r_low = cpu.r;
        z80.r_hi = cpu.r & 0x80u;
        z80.im = cpu.im & 0x03u;
        // Byte 19 bit 2 is IFF2. The format was born as an NMI-taken image
        // resumed by RETN, which copies IFF2 into IFF1, so both flip-flops
        // come from the one bit (libspectrum/FUSE do the same). Forcing IFF2
        // to 1 left a DI snapshot in the "inside an NMI handler" state
        // (IFF1=0, IFF2=1): its first RETN/RETI would enable interrupts
        z80.iff2 = cpu.iff2 ? 1 : 0;
        z80.iff1 = z80.iff2;

        // Initialize undocumented registers (not stored in SNA format)
        z80.memptr = 0;
        z80.q = 0;

        // Set up ports
        if (is48)
        {
            // A 48K snapshot carries no TR-DOS state: end any TR-DOS session the
            // machine was in (e.g. the Pentagon boot menu), exactly as the Z80
            // paging trap does when PC leaves the ROM. Otherwise the session
            // outlives the load: bank 0 keeps the DOS ROM instead of BASIC and
            // the Beta128 ports stay on the bus (every #FF-family read wakes
            // the FDC, which then runs its FSM per instruction).
            _context->emulatorState.flags &= ~CF_TRDOS;
            memory.UpdateZ80Banks();

            // Set default 48k mode RAM pages
            memory.SetRAMPageToBank1(5);
            memory.SetRAMPageToBank2(2);
            memory.SetRAMPageToBank3(0);

            // The 48K BASIC, latches included (what the reset does for RM_SOS): with the shipped RESET=128 the latch still
            // says BASIC-128, and the first bank recompute (any #7FFD write, a TR-DOS page-in) would swap the ROM under
            // the program. Written through the decoder so models that keep the ROM bit elsewhere (TS-Conf) follow
            memory.SetROMMode(RM_SOS);
            const uint8_t rom48Latch = _context->emulatorState.p7FFD;
            _context->pPortDecoder->UnlockPaging();
            _context->pPortDecoder->DecodePortOut(0x7FFD, rom48Latch, z80.pc);

            // The 48K BASIC ROM, wherever the model keeps it (Memory::base_sos_rom: page 3 on the Pentagon,
            // page 1 on the 128K, the only ROM on the 48K - a fixed page 3 was empty there)
            memory.SetROM48k();

            // 48k SNA files store Z80 PC on stack. The image has read it off the stack in the file's RAM; when the stack
            // is elsewhere (the ROM, the top of memory) the machine's memory at SP says, as it always has.
            // Z80 is little-endian: low byte at SP, high byte at SP+1
            if (cpu.pcOnMachineStack)
            {
                uint8_t pc_low = memory.DirectReadFromZ80Memory(z80.sp++);
                uint8_t pc_high = memory.DirectReadFromZ80Memory(z80.sp++);
                z80.pc = (pc_high << 8) | pc_low;
            }
            else
            {
                z80.pc = cpu.pc;
            }
        }
        else
        {
            // Memory page mapped to [C000:FFFF]
            const uint8_t port7FFD = image.paging.p7FFD.value_or(0);
            uint8_t currentTopPage = port7FFD & 0x07u;

            // Step 1: Unlock paging for state-independent loading
            // Ensures snapshot loads correctly even if port 7FFD was previously locked
            _context->pPortDecoder->UnlockPaging();

            // Step 1b: TR-DOS session state comes from the snapshot, not from
            // whatever the machine was doing before the load. Clear it here so
            // the 7FFD write below maps the ROM by bit 4 (bank 0 stays on the
            // DOS/SYS ROM while a session is active) and the Beta128 ports
            // leave the bus; step 5 re-activates the session for TR-DOS snapshots.
            if (!image.trdosPaged)
            {
                _context->emulatorState.flags &= ~CF_TRDOS;
                memory.UpdateZ80Banks();
            }

            // Step 2: Configure 128K memory banks
            memory.SetRAMPageToBank1(5);
            memory.SetRAMPageToBank2(2);
            memory.SetRAMPageToBank3(currentTopPage);

            z80.pc = cpu.pc;

            // Step 3: Set port values via decoder
            _context->pPortDecoder->DecodePortOut(0x7FFD, port7FFD, z80.pc);

            // Step 4: Explicit state assignment (including lock bit if present)
            _context->emulatorState.p7FFD = port7FFD;

            // Step 5: Activate TR-DOS ROM if needed
            if (image.trdosPaged)
            {
                // Set CF_TRDOS flag to indicate TR-DOS is active
                _context->emulatorState.flags |= CF_TRDOS;

                // Activate TR-DOS ROM
                _context->pMemory->SetROMDOS();
            }
        }

        // Detect if CPU was halted when snapshot was taken
        // If PC points to HALT instruction (0x76), set halted state
        // This ensures proper INT timing on first frame after load
        if (memory.DirectReadFromZ80Memory(z80.pc) == 0x76)
        {
            z80.halted = 1;
            z80.halt_cycle = 0;
            // haltpos is unknown from SNA, set to 0 for consistent behavior
            z80.haltpos = 0;
        }

        // Pre-fill border with color
        const uint8_t borderColor = image.border & 0b0000'0111;
        screen.FillBorderWithColor(borderColor);

        // Keep the machine state in step with the picture. FillBorderWithColor
        // only paints; pFE is the port latch every consumer reads back, and
        // border_attr is what a TTD checkpoint captures and the machine-state
        // hash folds in. Leaving them at their reset value made a checkpoint
        // record a border the machine never had - a snapshot with a black
        // border restored as white on seek.
        EmulatorState& borderState = _context->emulatorState;
        borderState.pFE = static_cast<uint8_t>((borderState.pFE & 0b1111'1000) | borderColor);
        borderState.border_attr = borderColor;


        // Trigger screen redraw to show snapshot screen immediately
        screen.RenderOnlyMainScreen();

        result = true;
    }

    /// region <Info logging>
    std::string version = "UNKNOWN";
    if (_snapshotMode == SNA_48)
    {
        version = "SNA48";
    }
    if (_snapshotMode == SNA_128)
    {
        version = "SNA128";
    }

    if (result)
    {
        std::string pcAddress = StringHelper::ToHexWithPrefix((uint16_t)z80.pc, "$");
        MLOGINFO("%s, %d RAM pages loaded, PC=%s", version.c_str(), ramPagesLoaded, pcAddress.c_str());
    }
    else
    {
        MLOGWARNING("Unable to apply loaded SNA data, type: %s, size: %d '%s'", version.c_str(), _fileSize, _path.c_str());
    }
    /// endregion </Info logging>

    return result;
}

/// region <Save methods>

/// Save through the pipeline's save side: the machine's 128K view, the formats the machine can be saved in, the file
/// written from the captured image (snapshot::SaveSnapshotFile). A refusal is logged and writes nothing
bool LoaderSNA::save()
{
    if (!_context)
    {
        MLOGERROR("save: Invalid emulator context");
        return false;
    }
    const snapshot::SaveResult result = snapshot::SaveSnapshotFile(*_context, snapshot::SaveFormat::Sna, _path);
    if (!result.ok)
        MLOGERROR("%s", result.text.c_str());
    return result.ok;
}

bool LoaderSNA::WriteImage(const snapshot::Image& image, std::string& error)
{
    const bool layout48 = snapshot::SavesAs48K(image);
    auto bankOf = [&](uint16_t bank) -> const std::vector<uint8_t>* {
        const auto it = image.banks.find(bank);
        return (it != image.banks.end() && it->second.size() == PAGE_SIZE) ? &it->second : nullptr;
    };
    for (uint16_t bank : {5, 2, 0})
    {
        if (!bankOf(bank))
        {
            error = "the machine state has no RAM bank " + std::to_string(bank);
            return false;
        }
    }

    const snapshot::Cpu& cpu = image.cpu;
    snaHeader header;
    memset(&header, 0, sizeof(header));
    header._h = cpu.hl2 >> 8;
    header._l = cpu.hl2 & 0xFF;
    header._d = cpu.de2 >> 8;
    header._e = cpu.de2 & 0xFF;
    header._b = cpu.bc2 >> 8;
    header._c = cpu.bc2 & 0xFF;
    header._a = cpu.af2 >> 8;
    header._f = cpu.af2 & 0xFF;
    header.h = cpu.hl >> 8;
    header.l = cpu.hl & 0xFF;
    header.d = cpu.de >> 8;
    header.e = cpu.de & 0xFF;
    header.b = cpu.bc >> 8;
    header.c = cpu.bc & 0xFF;
    header.a = cpu.af >> 8;
    header.f = cpu.af & 0xFF;
    header.hx = cpu.ix >> 8;
    header.lx = cpu.ix & 0xFF;
    header.hy = cpu.iy >> 8;
    header.ly = cpu.iy & 0xFF;
    header.i = cpu.i;
    header.r = cpu.r;
    header.imod = cpu.im & 0x03;
    header.flag19 = static_cast<uint8_t>((cpu.iff2 ? 1 : 0) << 2);
    header.border = image.border & 0x07;

    std::vector<uint8_t> file;
    auto append = [&](const void* bytes, size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(bytes);
        file.insert(file.end(), p, p + size);
    };

    if (layout48)
    {
        // The three pages as the CPU sees 0x4000-0xFFFF; the PC goes on the stack in this FILE's copy, never in the machine's RAM
        std::vector<uint8_t> ram;
        for (uint16_t bank : {5, 2, 0})
            ram.insert(ram.end(), bankOf(bank)->begin(), bankOf(bank)->end());

        uint16_t sp = cpu.sp;
        if (sp >= 1 && sp < 0x4002)
        {
            error = "a 48K .sna keeps the PC on the stack, and SP is " + StringHelper::Format("#%04X", sp) +
                    ": the stack is in ROM (or wraps through it); save as .z80 or .szx";
            return false;
        }
        sp = static_cast<uint16_t>(sp - 2);
        ram[static_cast<size_t>(static_cast<uint16_t>(sp - 0x4000))] = cpu.pc & 0xFF;
        ram[static_cast<size_t>(static_cast<uint16_t>(sp + 1 - 0x4000))] = cpu.pc >> 8;
        header.lsp = sp & 0xFF;
        header.hsp = sp >> 8;
        append(&header, sizeof(header));
        file.insert(file.end(), ram.begin(), ram.end());
    }
    else
    {
        for (uint16_t bank = 0; bank < 8; bank++)
        {
            if (!bankOf(bank))
            {
                error = "the machine state has no RAM bank " + std::to_string(bank);
                return false;
            }
        }
        // 128K: SP unchanged, the PC in the extended header; banks 5, 2, the one at 0xC000, then the others ascending
        // (the one at 0xC000 is not repeated unless it is 5 or 2, whose second copy the format keeps)
        header.lsp = cpu.sp & 0xFF;
        header.hsp = cpu.sp >> 8;
        const uint8_t p7ffd = image.paging.p7FFD.value_or(0);
        const uint16_t current = p7ffd & 0x07;
        append(&header, sizeof(header));
        for (uint16_t bank : {static_cast<uint16_t>(5), static_cast<uint16_t>(2), current})
            file.insert(file.end(), bankOf(bank)->begin(), bankOf(bank)->end());
        sna128Header ext;
        ext.reg_PC = cpu.pc;
        ext.port_7FFD = p7ffd;
        ext.is_TRDOS = image.trdosPaged ? 1 : 0;
        append(&ext, sizeof(ext));
        for (uint16_t bank = 0; bank < 8; bank++)
        {
            if (bank == 5 || bank == 2 || bank == current)
                continue;
            file.insert(file.end(), bankOf(bank)->begin(), bankOf(bank)->end());
        }
    }

    if (!FileHelper::SaveBufferToFile(_path, file.data(), file.size()))
    {
        error = "cannot write '" + _path + "'";
        return false;
    }
    MLOGINFO("Saved %s SNA: %s", layout48 ? "48K" : "128K", _path.c_str());
    return true;
}

/// endregion </Save methods>

/// endregion </Helper methods>