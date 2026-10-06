#include "loader_z80.h"
#include "loaders/snapshot/snapshotcapture.h"

#include <algorithm>
#include <iterator>

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/snapshot/szx/loaderszx.h"
#include "stdafx.h"

LoaderZ80::LoaderZ80(EmulatorContext* context, const std::string& path)
{
    _context = context;
    _logger = context->pModuleLogger;

    _path = path;
}

LoaderZ80::LoaderZ80(EmulatorContext* context, std::vector<uint8_t> data, const std::string& name)
{
    _context = context;
    _logger = context->pModuleLogger;

    _path = name;
    _data = std::move(data);
    _fromMemory = true;
}

LoaderZ80::~LoaderZ80()
{
    freeStagingMemory();
}

bool LoaderZ80::load()
{
    bool result = false;

    if (validate())
    {
        if (stageLoad())
        {
            if (planSnapshot())
            {
                if (_decision.action == snapshot::Decision::Action::Take)
                {
                    result = _decision.Commit(_image, *_context, _report);
                    freeStagingMemory();
                }
                else
                {
                    commitFromStage();

                    result = true;
                }
            }
            else
            {
                freeStagingMemory();
            }
        }
    }

    return result;
}

namespace
{
/// The machine a file says it was made on, from its model byte (the v2 and v3 numberings differ below 7)
const char* Z80MachineHint(Z80SnapshotVersion version, uint8_t model)
{
    if (version == Z80v1)
        return "48k";
    const bool v3 = version == Z80v3;
    switch (model)
    {
        case 0:
        case 1: return "48k";
        case 2: return "samram";
        case 3: return v3 ? "48k" : "128k";
        case 4:
        case 5:
        case 6: return v3 ? "128k" : "unknown";
        case 7:
        case 8: return "plus3";
        case 9: return "pentagon128";
        case 10: return "scorpion256";
        case 11: return "didaktik";
        case 12: return "plus2";
        case 13: return "plus2a";
        case 14:
        case 15:
        case 128: return "timex";
        default: return "unknown";
    }
}
}  // namespace

/// The staging as a format-neutral image (logical banks: the staging pages are indexed by the 128K bank number)
snapshot::Image LoaderZ80::BuildImage() const
{
    snapshot::Image image;
    image.format = "z80";
    image.sourcePath = _path;
    image.formatVersion = _snapshotVersion == Z80v1 ? "v1" : _snapshotVersion == Z80v2 ? "v2" : "v3";
    // The model byte is read from the header here: the v2 staging does not keep it (_modelCode is v3's)
    uint8_t model = 0;
    if (_snapshotVersion != Z80v1 && _data.size() >= sizeof(Z80Header_v2))
        model = static_cast<uint8_t>(reinterpret_cast<const Z80Header_v2*>(_data.data())->model);
    image.machineHint = Z80MachineHint(_snapshotVersion, model);
    if (_snapshotVersion != Z80v1)
        image.rawMachineId = "z80 " + image.formatVersion + " hardware " + std::to_string(model);

    switch (_memoryMode)
    {
        case Z80_48K: image.memoryModel = snapshot::MemoryModel::Mem48k; break;
        case Z80_128K: image.memoryModel = snapshot::MemoryModel::Mem128k; break;
        case Z80_256K: image.memoryModel = snapshot::MemoryModel::Extended; break;
        default:
            image.memoryModel = snapshot::MemoryModel::Extended;
            image.unsupported = "a SamRam / SAM Coupe snapshot: this emulator has no such machine";
            image.warnings.push_back(image.unsupported);
            break;
    }
    image.timingHint = image.machineHint == "pentagon128" ? "pentagon" : _memoryMode == Z80_48K ? "48k" : "128k";

    for (size_t idx = 0; idx < MAX_RAM_PAGES; ++idx)
    {
        if (_stagingRAMPages[idx])
            image.banks[static_cast<uint16_t>(idx)] =
                std::vector<uint8_t>(_stagingRAMPages[idx], _stagingRAMPages[idx] + PAGE_SIZE);
    }
    for (size_t idx = 0; idx < MAX_ROM_PAGES; ++idx)
    {
        if (_stagingROMPages[idx])
        {
            image.extensions.push_back({"z80:rom-block", "rom", PAGE_SIZE, "ROM page " + std::to_string(idx), {}});
            image.warnings.push_back("the file carries a ROM block: Z80 snapshots with ROM blocks are not supported");
            if (image.unsupported.empty())
                image.unsupported = "the file carries a ROM block (a custom ROM): Z80 snapshots with ROM blocks are not supported";
        }
    }

    snapshot::Cpu& cpu = image.cpu;
    const Z80Registers& r = _z80Registers;
    cpu.af = r.af;
    cpu.bc = r.bc;
    cpu.de = r.de;
    cpu.hl = r.hl;
    cpu.ix = r.ix;
    cpu.iy = r.iy;
    cpu.sp = r.sp;
    cpu.pc = r.pc;
    cpu.af2 = r.alt.af;
    cpu.bc2 = r.alt.bc;
    cpu.de2 = r.alt.de;
    cpu.hl2 = r.alt.hl;
    cpu.i = r.i;
    cpu.r = static_cast<uint8_t>((r.r_hi & 0x80u) | (r.r_low & 0x7Fu));
    cpu.iff1 = r.iff1 != 0;
    cpu.iff2 = r.iff2 != 0;
    cpu.im = r.im;

    if (_memoryMode == Z80_128K || _memoryMode == Z80_256K)
    {
        image.paging.p7FFD = _port7FFD;
        if (_hasPort1FFD)
            image.paging.p1FFD = _port1FFD;
    }
    if (_hasTStates)
        image.framePosition = _tstatesFromInt;
    image.ayAddressLatch = _portFFFD;
    image.border = static_cast<uint8_t>(_borderColor & 7u);
    if (_hasAyRegisters)
    {
        snapshot::Ay ay;
        std::copy(std::begin(_ayRegisters), std::end(_ayRegisters), ay.registers.begin());
        ay.selected = _portFFFD;
        image.ay.push_back(ay);
    }
    return image;
}

bool LoaderZ80::Stage()
{
    if (!validate() || !stageLoad())
        return false;
    _image = BuildImage();
    return true;
}

bool LoaderZ80::planSnapshot()
{
    _image = BuildImage();
    _report = snapshot::Report();
    _decision = snapshot::Pipeline::Plan(_image, _context, _options, _report);
    return _decision.Proceeds();
}

bool LoaderZ80::save()
{
    // Through the pipeline's save side: the machine's 128K view, the formats the machine can be saved in, the file written
    // from the captured image (snapshot::SaveSnapshotFile). A refusal is logged and writes nothing
    if (!_context)
    {
        MLOGERROR("save: Invalid emulator context");
        return false;
    }
    const snapshot::SaveResult result = snapshot::SaveSnapshotFile(*_context, snapshot::SaveFormat::Z80, _path);
    if (!result.ok)
        MLOGERROR("%s", result.text.c_str());
    return result.ok;
}

bool LoaderZ80::WriteImage(const snapshot::Image& image, std::string& error, std::vector<std::string>& warnings)
{
    // The layout: a 48K machine or a locked 128K with bank 0 on top is saved as a 48K program (three pages); a Scorpion's
    // 16 pages are pages 3-18; the others have 8
    const bool layout48 = snapshot::SavesAs48K(image);
    const std::string& machine = image.machineHint;
    uint8_t modelCode = Z80_MODEL3_128K;
    if (layout48)
        modelCode = Z80_MODEL3_48K;
    else if (machine == "128k")
        modelCode = Z80_MODEL3_128K;
    else if (machine == "plus2")
        modelCode = Z80_MODEL3_128K_2;
    else if (machine == "plus2a")
        modelCode = Z80_MODEL3_128K_2A;
    else if (machine == "plus3")
        modelCode = Z80_MODEL3_128K_3;
    else if (machine == "pentagon128")
        modelCode = Z80_MODEL3_P128K;
    else if (machine == "scorpion256")
        modelCode = Z80_MODEL3_ZS256K;
    else
    {
        error = "the .z80 format has no model for '" + machine + "'";
        return false;
    }
    std::vector<uint16_t> banks;
    if (layout48)
        banks = {5, 2, 0};
    else
    {
        const uint16_t count = machine == "scorpion256" ? 16 : 8;
        for (uint16_t bank = 0; bank < count; bank++)
            banks.push_back(bank);
    }
    for (uint16_t bank : banks)
    {
        const auto it = image.banks.find(bank);
        if (it == image.banks.end() || it->second.size() != PAGE_SIZE)
        {
            error = "the machine state has no RAM bank " + std::to_string(bank);
            return false;
        }
    }

    const snapshot::Cpu& cpu = image.cpu;
    Z80Header_v3 header = {};
    header.reg_A = cpu.af >> 8;
    header.reg_F = cpu.af & 0xFF;
    header.reg_BC = cpu.bc;
    header.reg_DE = cpu.de;
    header.reg_HL = cpu.hl;
    header.reg_SP = cpu.sp;
    header.reg_I = cpu.i;
    header.reg_R = cpu.r & 0x7F;   // bits 0-6; R bit 7 goes into flags bit 0 (the .z80 layout)
    header.flags = static_cast<uint8_t>(((cpu.r & 0x80) >> 7) | ((image.border & 0x07) << 1) | 0x20);   // 0x20 = compressed
    header.reg_DE1 = cpu.de2;
    header.reg_BC1 = cpu.bc2;
    header.reg_HL1 = cpu.hl2;
    header.reg_A1 = cpu.af2 >> 8;
    header.reg_F1 = cpu.af2 & 0xFF;
    header.reg_IY = cpu.iy;
    header.reg_IX = cpu.ix;
    header.IFF1 = cpu.iff1 ? 1 : 0;
    header.IFF2 = cpu.iff2 ? 1 : 0;
    header.im = cpu.im & 0x03;
    header.reg_PC = 0;   // V1: PC = 0 says v2 / v3

    header.extendedHeaderLen = 54;   // V3 standard; 55 with the #1FFD byte
    header.newPC = cpu.pc;
    header.model = static_cast<Z80_Models_v2>(modelCode);   // a v3 code in the v2-typed field
    header.p7FFD = image.paging.p7FFD.value_or(0);   // kept for a locked 128K that is saved as a 48K program, as it always was
    if (modelCode == Z80_MODEL3_128K_2A || modelCode == Z80_MODEL3_128K_3 || modelCode == Z80_MODEL3_ZS256K)
    {
        header.extendedHeaderLen = 55;
        header.p1FFD = image.paging.p1FFD.value_or(0);
    }

    // A 48K program on a machine that has an AY carries the AY state; byte 37 bit 2 says so, otherwise readers (this
    // loader included) ignore the AY bytes of a 48K snapshot
    if (!image.ay.empty())
    {
        if (layout48)
            header.r2 |= 0b0000'0100;
        for (int i = 0; i < 16; i++)
            header.ay[i] = image.ay[0].registers[i];
        header.pFFFD = image.ay[0].selected;
    }

    // The frame position, as libspectrum / Fuse encode it: a count-down within the current quarter of the frame and the
    // quarter, both from the INT
    const uint32_t quarter = std::max(1u, _context->config.frame / 4);
    const uint32_t tstates = image.framePosition.value_or(0);
    header.lowTCounter = static_cast<uint16_t>(quarter - (tstates % quarter) - 1);
    header.highTCounter = static_cast<uint8_t>(((tstates / quarter) + 3) % 4);

    std::vector<uint8_t> file;
    auto append = [&](const void* bytes, size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(bytes);
        file.insert(file.end(), p, p + size);
    };
    append(&header, sizeof(Z80Header_v1));
    const uint16_t extLen = header.extendedHeaderLen;
    append(&extLen, sizeof(extLen));
    append(&header.newPC, extLen);

    uint8_t compressBuffer[PAGE_SIZE + 1024];   // extra space for the worst case
    for (uint16_t bank : banks)
    {
        std::vector<uint8_t> copy = image.banks.at(bank);
        const size_t compressedSize = compressPage(copy.data(), PAGE_SIZE, compressBuffer, sizeof(compressBuffer));
        MemoryBlockDescriptor desc;
        desc.compressedSize = static_cast<uint16_t>(compressedSize);
        // 48K: banks 5, 2, 0 are Z80 pages 8, 4, 5; 128K: Z80 pages 3-10 are banks 0-7 (Scorpion 3-18)
        desc.memoryPage = layout48 ? (bank == 5 ? 8 : (bank == 2 ? 4 : 5)) : static_cast<uint8_t>(bank + 3);
        append(&desc, sizeof(desc));
        append(compressBuffer, compressedSize);
    }

    if (!FileHelper::SaveBufferToFile(_path, file.data(), file.size()))
    {
        error = "cannot write '" + _path + "'";
        return false;
    }
    (void)warnings;
    MLOGINFO("Saved Z80 v3 snapshot to '%s' (%s mode)", _path.c_str(), layout48 ? "48K" : (machine == "scorpion256" ? "256K" : "128K"));
    return true;
}

bool LoaderZ80::validate()
{
    bool result = false;

    // 1. The snapshot bytes: given in memory, or the whole file (read once;
    // no handle stays open)
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
            // 2. Check file has appropriate size (header + data bytes)
            _fileSize = _data.size();
            if (_fileSize > 0)
            {
                size_t dataSize = _fileSize - sizeof(Z80Header_v1);
                if (dataSize > 0)
                {
                    // 3. Detect snapshot version
                    Z80SnapshotVersion ver = getSnapshotFileVersion();
                    if (ver != Unknown)
                    {
                        // 4. Validate header sanity for detected version
                        if (!validateHeaderSanity(ver))
                        {
                            MLOGWARNING("Z80 snapshot file '%s' failed header sanity checks", _path.c_str());
                            return false;
                        }

                        // 5. Validate minimum file size for detected version
                        bool sizeValid = true;
                        switch (ver)
                        {
                            case Z80v1:
                                // v1: 30-byte header + at least some data
                                if (_fileSize < sizeof(Z80Header_v1) + 1)
                                {
                                    MLOGWARNING("Z80 v1 snapshot file '%s' too small (size=%zu)", _path.c_str(), _fileSize);
                                    sizeValid = false;
                                }
                                break;
                            case Z80v2:
                                // v2: 30-byte header + 2-byte length + 23-byte extended header = 55 bytes minimum
                                if (_fileSize < 55)
                                {
                                    MLOGWARNING("Z80 v2 snapshot file '%s' too small (size=%zu, need at least 55)", _path.c_str(), _fileSize);
                                    sizeValid = false;
                                }
                                break;
                            case Z80v3:
                                // v3: 30-byte header + 2-byte length + 54-byte extended header = 86 bytes minimum
                                if (_fileSize < 86)
                                {
                                    MLOGWARNING("Z80 v3 snapshot file '%s' too small (size=%zu, need at least 86)", _path.c_str(), _fileSize);
                                    sizeValid = false;
                                }
                                break;
                            default:
                                sizeValid = false;
                                break;
                        }

                        if (sizeValid)
                        {
                            _snapshotVersion = ver;
                            result = true;
                        }
                    }
                }
                else
                {
                    MLOGWARNING("Z80 snapshot file '%s' has incorrect size %zu", _path.c_str(), _fileSize);
                }
            }
        }
    }
    else
    {
        MLOGWARNING("Z80 snapshot file '%s' not found", _path.c_str());
    }

    // Persist validation state in the field
    _fileValidated = result;

    return result;
}

bool LoaderZ80::stageLoad()
{
    bool result = false;
    _image = snapshot::Image();   // staged afresh: an image built from an earlier staging is stale

    if (_fileValidated && _snapshotVersion != Unknown)
    {
        switch (_snapshotVersion)
        {
            case Z80v1:
                result = loadZ80v1();
                break;
            case Z80v2:
                result = loadZ80v2();
                break;
            case Z80v3:
                result = loadZ80v3();
                break;
            default:
                break;
        }
    }

    if (result)
    {
        _stagingLoaded = true;

        /// region <Info logging>
        std::string message = dumpSnapshotMemoryInfo();
        MLOGINFO(message);
        /// endregion </Info logging>
    }

    return result;
}

void LoaderZ80::commitFromStage()
{
    if (_stagingLoaded)
    {
        Memory& memory = *_context->pMemory;
        Screen& screen = *_context->pScreen;
        PortDecoder& ports = *_context->pPortDecoder;
        Core& core = *_context->pCore;

        // The commit reads the image (snapshot pipeline P9), not the staging buffers. load() has built it for the plan; a
        // caller that stages and commits by hand has not
        if (_image.format.empty())
            _image = BuildImage();
        const snapshot::Image& image = _image;
        const snapshot::Cpu& cpu = image.cpu;
        // load() refuses these in the plan, before anything is touched; a caller that commits by hand gets what it always got
        if (!image.unsupported.empty())
            throw std::logic_error("Not supported");

        const Z80MemoryMode memoryMode = image.memoryModel == snapshot::MemoryModel::Mem48k    ? Z80_48K
                                         : image.memoryModel == snapshot::MemoryModel::Mem128k ? Z80_128K
                                                                                                : Z80_256K;
        const uint8_t port7FFD128 = image.paging.p7FFD.value_or(0);
        const uint8_t portFFFD = image.ayAddressLatch.value_or(0);

        // Reset Z80 and all peripherals for a clean state-independent load
        // Ensures AY registers, beeper, FDC, tape, screen mode etc. are clean
        core.Reset();
        // A model whose reset leaves an extended paging on (the Pentagon 1024) goes back to the plain 128K form
        ports.EnterSpectrum128Paging(cpu.pc);

        /// region <Apply port configuration>
        switch (memoryMode)
        {
            case Z80_48K: {
                // The 48K BASIC, latches included (what the reset does for RM_SOS): the +2A / +3 take the ROM's high bit
                // from #1FFD, which the shipped RESET=128 leaves at ROM 0 / 1
                memory.SetROMMode(RM_SOS);

                // Step 1: Unlock paging for state-independent loading
                ports.UnlockPaging();

                // Step 2: Configure 48K memory banks
                memory.SetRAMPageToBank1(5);
                memory.SetRAMPageToBank2(2);
                memory.SetRAMPageToBank3(0);
                memory.SetROM48k();

                // Step 3: Set port values via decoder (goes through hardware logic)
                uint8_t port7FFD =
                    PORT_7FFD_RAM_BANK_0 | PORT_7FFD_SCREEN_NORMAL | PORT_7FFD_ROM_BANK_1 | PORT_7FFD_LOCK;
                ports.DecodePortOut(0x7FFD, port7FFD, cpu.pc);
                ports.DecodePortOut(0xFFFD, portFFFD, cpu.pc);

                // Step 4: Explicit state assignment
                _context->emulatorState.p7FFD = port7FFD;
            }
            break;
            case Z80_128K:
            case Z80_256K:  // Scorpion ZS-256: the 128K path plus #1FFD (RAM page bit 3, ROM, RAM at #0000)
            {
                // Initialize 128K memory configuration
                // CRITICAL: Must fully unlock emulator state before applying snapshot
                
                // Extract RAM page for bank 3 from port 7FFD (bits 0-2)
                uint8_t bank3Page = port7FFD128 & 0x07;
                
                // Step 1: Unlock paging via PortDecoder interface
                // This allows subsequent port writes to succeed even if previously locked
                ports.UnlockPaging();
                
                // Step 2: Set up standard 128K memory mapping
                // Bank 0 (0x0000-0x3FFF): ROM (set by UpdateZ80Banks based on port 7FFD bit 4)
                // Bank 1 (0x4000-0x7FFF): RAM page 5 (fixed)
                // Bank 2 (0x8000-0xBFFF): RAM page 2 (fixed)  
                // Bank 3 (0xC000-0xFFFF): RAM page from port 7FFD bits 0-2
                memory.SetRAMPageToBank1(5);
                memory.SetRAMPageToBank2(2);
                memory.SetRAMPageToBank3(bank3Page);

                // Step 3: Set port values via decoder (goes through hardware logic for ROM/screen).
                // #1FFD first (+2A / +3 special paging and ROM high bit, Scorpion page bit 3):
                // #7FFD's lock bit would block it afterwards
                const MEM_MODEL model = _context->config.mem_model;
                const bool has1FFD = model == MM_PLUS2A || model == MM_PLUS3 || model == MM_SCORP || model == MM_PROFSCORP;
                if (image.paging.p1FFD && has1FFD)
                {
                    ports.DecodePortOut(0x1FFD, *image.paging.p1FFD, cpu.pc);
                    _context->emulatorState.p1FFD = *image.paging.p1FFD;
                }
                ports.DecodePortOut(0x7FFD, port7FFD128, cpu.pc);
                ports.DecodePortOut(0xFFFD, portFFFD, cpu.pc);
                
                // Step 4: Ensure emulatorState reflects snapshot's port value (including lock bit)
                _context->emulatorState.p7FFD = port7FFD128;
                
                // Step 5: Trigger ROM selection based on port 7FFD bit 4
                memory.UpdateZ80Banks();
                break;
            }
            default:
                throw std::logic_error("Not supported");
                break;
        }

        // Pre-fill whole border with color (visual only, no timing side effects)
        // Don't call Default_Port_FE_Out here - it triggers UpdateScreen() with stale t-state
        // Just set the visual state directly like SNA loader does
        screen.FillBorderWithColor(image.border);

        // Keep the machine state in step with the picture. FillBorderWithColor
        // only paints; pFE is the port latch every consumer reads back, and
        // border_attr is what a TTD checkpoint captures and the machine-state
        // hash folds in. Leaving them at their reset value made a checkpoint
        // record a border the machine never had - a snapshot with a black
        // border restored as white on seek.
        EmulatorState& borderState = _context->emulatorState;
        borderState.pFE = static_cast<uint8_t>((borderState.pFE & 0b1111'1000) |
                                               (image.border & 0b0000'0111));
        borderState.border_attr = static_cast<uint8_t>(image.border & 0b0000'0111);

        // AY registers: core.Reset() above cleared the chip, so the snapshot's
        // registers go in after it
        commitPeripheralState(image);

        /// endregion </Apply port configuration>

        /// region <Transfer memory content>

        for (const auto& bank : image.banks)
            memcpy(memory.RAMPageAddress(bank.first), bank.second.data(), PAGE_SIZE);

        // Free used staging memory
        freeStagingMemory();

        /// endregion </Transfer memory content>

        /// region <Transfer Z80 registers>
        Z80* z80 = _context->pCore->GetZ80();

        // Copy registers but preserve timing state (t) from Reset
        // memcpy would overwrite t to 0, but Reset set it to 3
        // SNA loader uses individual assignments which preserve t
        Z80Registers regs = {};
        regs.af = cpu.af;
        regs.bc = cpu.bc;
        regs.de = cpu.de;
        regs.hl = cpu.hl;
        regs.alt.af = cpu.af2;
        regs.alt.bc = cpu.bc2;
        regs.alt.de = cpu.de2;
        regs.alt.hl = cpu.hl2;
        regs.ix = cpu.ix;
        regs.iy = cpu.iy;
        regs.sp = cpu.sp;
        regs.pc = cpu.pc;
        regs.i = cpu.i;
        regs.r_low = cpu.r & 0x7Fu;
        regs.r_hi = cpu.r & 0x80u;
        regs.iff1 = cpu.iff1 ? 1 : 0;
        regs.iff2 = cpu.iff2 ? 1 : 0;
        regs.im = cpu.im;
        regs.memptr = 0;
        regs.q = 0;

        uint32_t preservedT = z80->tt;
        Z80Registers* actualRegisters = static_cast<Z80Registers*>(z80);
        memcpy(actualRegisters, &regs, sizeof(Z80Registers));
        z80->tt = preservedT;  // Restore timing state
        // A v3 file stores the frame position (from the INT): resume there
        if (image.framePosition)
            z80->t = LoaderSZX::FramePositionFromIntCount(_context, *image.framePosition);

        // Detect if CPU was halted when snapshot was taken
        // If PC points to HALT instruction (0x76), set halted state
        // This ensures proper INT timing on first frame after load
        if (memory.DirectReadFromZ80Memory(z80->pc) == 0x76)
        {
            z80->halted = 1;
            z80->halt_cycle = 0;
            z80->haltpos = 0;
        }
        /// endregion </Transfer Z80 registers>

        // Trigger screen redraw to show snapshot screen immediately
        screen.RenderOnlyMainScreen();
    }
}

/// region <Helper methods>
Z80SnapshotVersion LoaderZ80::getSnapshotFileVersion()
{
    Z80SnapshotVersion result = Unknown;

    if (!_data.empty())
    {
        Z80Header_v1 header;

        // Read Z80 common header
        if (_data.size() >= sizeof(header))
        {
            memcpy(&header, _data.data(), sizeof(header));
            if (header.reg_PC == 0x0000)
            {
                // PC register is zero, indicating Z80 v2 or newer format
                Z80Header_v2 headerV2;
                if (_data.size() >= sizeof(headerV2))
                {
                    memcpy(&headerV2, _data.data(), sizeof(headerV2));
                    uint16_t extendedHeaderSize = headerV2.extendedHeaderLen;

                    switch (extendedHeaderSize)
                    {
                        case 23:
                            result = Z80v2;
                            break;
                        case 54:
                            result = Z80v3;
                            break;
                        case 55:
                            result = Z80v3;
                            break;
                        default:
                            break;
                    }
                }
            }
            else
            {
                // PC register is not zero, it's Z80 v1 format
                result = Z80v1;
            }
        }
    }

    return result;
}

bool LoaderZ80::loadZ80v1()
{
    bool result = false;

    if (_fileValidated && _snapshotVersion == Z80v1)
    {
        _memoryMode = Z80_48K;

        // The whole snapshot is in _data (validate())
        uint8_t* pBuffer = _data.data();

        // Provide access to header structure
        Z80Header_v1& headerV1 = *(Z80Header_v1*)pBuffer;

        // Extract Z80 registers information
        _z80Registers = getZ80Registers(headerV1, headerV1.reg_PC);

        // Handle flags byte: if 255, treat as 1 (per specification)
        uint8_t flags = headerV1.flags;
        if (flags == 255)
        {
            flags = 1;
        }

        // Remember border color (bits 1-3)
        _borderColor = (flags & 0b0000'1110) >> 1;

        // Check if data is compressed (bit 5)
        bool isCompressed = (flags & 0b0010'0000) != 0;

        // Allocate buffer for 48K of memory (3 pages: 0x4000-0xFFFF)
        constexpr size_t MEMORY_48K_SIZE = 3 * PAGE_SIZE;
        auto unpackedMemory = std::unique_ptr<uint8_t[]>(new uint8_t[MEMORY_48K_SIZE]);
        uint8_t* pUnpacked = unpackedMemory.get();

        // Data starts after the 30-byte header
        uint8_t* dataStart = pBuffer + sizeof(Z80Header_v1);
        size_t dataSize = _fileSize - sizeof(Z80Header_v1);

        if (isCompressed)
        {
            decompressV1Data(dataStart, dataSize, pUnpacked, MEMORY_48K_SIZE);
        }
        else
        {
            // Uncompressed: copy directly (capped at 48K)
            size_t copySize = std::min(dataSize, MEMORY_48K_SIZE);
            memcpy(pUnpacked, dataStart, copySize);

            // Zero remaining if source is smaller (shouldn't happen for valid files)
            if (copySize < MEMORY_48K_SIZE)
            {
                memset(pUnpacked + copySize, 0, MEMORY_48K_SIZE - copySize);
            }
        }

        // Map 48K memory to RAM pages:
        // 0x4000-0x7FFF (offset 0x0000 in unpacked) -> RAM Page 5
        // 0x8000-0xBFFF (offset 0x4000 in unpacked) -> RAM Page 2
        // 0xC000-0xFFFF (offset 0x8000 in unpacked) -> RAM Page 0
        uint8_t* page5 = new uint8_t[PAGE_SIZE];
        uint8_t* page2 = new uint8_t[PAGE_SIZE];
        uint8_t* page0 = new uint8_t[PAGE_SIZE];

        memcpy(page5, pUnpacked, PAGE_SIZE);                  // 0x4000-0x7FFF
        memcpy(page2, pUnpacked + PAGE_SIZE, PAGE_SIZE);      // 0x8000-0xBFFF
        memcpy(page0, pUnpacked + 2 * PAGE_SIZE, PAGE_SIZE);  // 0xC000-0xFFFF

        _stagingRAMPages[5] = page5;
        _stagingRAMPages[2] = page2;
        _stagingRAMPages[0] = page0;

        result = true;
    }

    return result;
}

bool LoaderZ80::loadZ80v2()
{
    bool result = false;

    if (_fileValidated && _snapshotVersion == Z80v2 && _fileSize > 0)
    {
        // The whole snapshot is in _data (validate())
        uint8_t* pBuffer = _data.data();

        // Provide access to header structure
        Z80Header_v1& headerV1 = *(Z80Header_v1*)pBuffer;
        Z80Header_v2& headerV2 = *(Z80Header_v2*)pBuffer;

        // Extract Z80 registers information
        _z80Registers = getZ80Registers(headerV1, headerV2.newPC);

        // Determine snapshot memory model based on model
        _memoryMode = getMemoryModeV2(static_cast<uint8_t>(headerV2.model));

        // Retrieve ports configuration
        _port7FFD = headerV2.p7FFD;
        _portFFFD = headerV2.pFFFD;
        stagePeripheralState(headerV2);

        // Remember border color
        _borderColor = (headerV1.flags & 0b0000'1110) >> 1;

        // Start memory blocks processing after all headers
        uint8_t* memBlock = pBuffer + sizeof(Z80Header_v1) + headerV2.extendedHeaderLen + 2;
        uint8_t* pBufferEnd = pBuffer + _fileSize;

        while (memBlock)
        {
            // Bounds check: ensure memory block descriptor is within file
            if (memBlock + sizeof(MemoryBlockDescriptor) > pBufferEnd)
            {
                MLOGWARNING("Z80 v2 snapshot truncated: memory block descriptor at offset %zu exceeds file size %zu",
                            static_cast<size_t>(memBlock - pBuffer), _fileSize);
                break;
            }

            MemoryBlockDescriptor* memoryBlockDescriptor = (MemoryBlockDescriptor*)memBlock;
            uint8_t* pageBlock = memBlock + sizeof(MemoryBlockDescriptor);
            size_t compressedBlockSize = memoryBlockDescriptor->compressedSize;
            uint8_t targetPage = memoryBlockDescriptor->memoryPage;

            // Determine emulator target page
            MemoryPageDescriptor targetPageDescriptor = resolveSnapshotPage(targetPage, _memoryMode);

            // Skip invalid/unknown pages (don't allocate or crash)
            if (targetPageDescriptor.mode == BANK_INVALID)
            {
                MLOGWARNING("Z80 v2 snapshot: unknown page %d in %s mode, skipping",
                            targetPage, (_memoryMode == Z80_48K) ? "48K" : "128K");
                // Advance to next block
                size_t skipSize = (compressedBlockSize == 0xFFFF) ? PAGE_SIZE : compressedBlockSize;
                memBlock += skipSize + sizeof(MemoryBlockDescriptor);
                if (memBlock >= pBufferEnd)
                {
                    memBlock = nullptr;
                }
                continue;
            }

            // Bounds check: ensure compressed data is within file
            size_t actualBlockSize = (compressedBlockSize == 0xFFFF) ? PAGE_SIZE : compressedBlockSize;
            if (pageBlock + actualBlockSize > pBufferEnd)
            {
                MLOGWARNING("Z80 v2 snapshot truncated: block data at offset %zu (size %zu) exceeds file size %zu",
                            static_cast<size_t>(pageBlock - pBuffer), actualBlockSize, _fileSize);
                break;
            }

            // Allocate memory page and register it in one of staging collections (ROM or RAM)
            // De-allocation will be performed after staging changes applied to main emulator memory
            // or in loader destructor
            uint8_t* pageBuffer = new uint8_t[PAGE_SIZE];
            switch (targetPageDescriptor.mode)
            {
                case BANK_ROM:
                    _stagingROMPages[targetPageDescriptor.page] = pageBuffer;
                    break;
                case BANK_RAM:
                    _stagingRAMPages[targetPageDescriptor.page] = pageBuffer;
                    break;
                default:
                    // Should not reach here - already handled above
                    delete[] pageBuffer;
                    break;
            }

            // Unpack memory block to target staging page
            if (compressedBlockSize == 0xFFFF)
            {
                // Block is not compressed and has fixed length 0x4000 (16384)
                compressedBlockSize = PAGE_SIZE;
                memcpy(pageBuffer, pageBlock, PAGE_SIZE);
            }
            else
            {
                // Block is compressed so we need to decompress it
                decompressPage(pageBlock, compressedBlockSize, pageBuffer, PAGE_SIZE);
            }

            uint8_t* nextMemBlock = memBlock + compressedBlockSize + sizeof(MemoryBlockDescriptor);
            if (nextMemBlock >= pBufferEnd || nextMemBlock == memBlock)
            {
                memBlock = nullptr;
            }
            else
            {
                memBlock = nextMemBlock;
            }
        }

        result = true;
    }

    return result;
}

bool LoaderZ80::loadZ80v3()
{
    bool result = false;

    if (_fileValidated && _snapshotVersion == Z80v3 && _fileSize > 0)
    {
        // The whole snapshot is in _data (validate())
        uint8_t* pBuffer = _data.data();

        // Provide access to header structure
        Z80Header_v1& headerV1 = *(Z80Header_v1*)pBuffer;
        Z80Header_v3& headerV3 = *(Z80Header_v3*)pBuffer;

        // Extract Z80 registers information
        _z80Registers = getZ80Registers(headerV1, headerV3.newPC);

        // Determine snapshot memory model based on model (v3 has different model interpretation)
        _memoryMode = getMemoryModeV3(static_cast<uint8_t>(headerV3.model));

        // Retrieve ports configuration
        _port7FFD = headerV3.p7FFD;
        _portFFFD = headerV3.pFFFD;
        _modelCode = static_cast<uint8_t>(headerV3.model);
        // Byte 86 exists only in a 55-byte extended header (+2A / +3 / Scorpion writers)
        _hasPort1FFD = headerV3.extendedHeaderLen >= 55;
        _port1FFD = _hasPort1FFD ? headerV3.p1FFD : 0;
        // Bytes 55-57: the frame position (libspectrum's decoding); out of range means "not stored"
        {
            const uint32_t frame = _context->config.frame;
            const uint32_t quarter = std::max(1u, frame / 4);
            const int64_t tstates = static_cast<int64_t>(((headerV3.highTCounter + 1) % 4) + 1) * quarter -
                                    (static_cast<int64_t>(headerV3.lowTCounter) + 1);
            _hasTStates = tstates >= 0 && tstates < static_cast<int64_t>(frame);
            _tstatesFromInt = _hasTStates ? static_cast<uint32_t>(tstates) : 0;
        }
        stagePeripheralState(headerV3);

        // Remember border color
        _borderColor = (headerV1.flags & 0b0000'1110) >> 1;

        // Start memory blocks processing after all headers
        uint8_t* memBlock = pBuffer + sizeof(Z80Header_v1) + headerV3.extendedHeaderLen + 2;
        uint8_t* pBufferEnd = pBuffer + _fileSize;

        while (memBlock)
        {
            // Bounds check: ensure memory block descriptor is within file
            if (memBlock + sizeof(MemoryBlockDescriptor) > pBufferEnd)
            {
                MLOGWARNING("Z80 v3 snapshot truncated: memory block descriptor at offset %zu exceeds file size %zu",
                            static_cast<size_t>(memBlock - pBuffer), _fileSize);
                break;
            }

            MemoryBlockDescriptor* memoryBlockDescriptor = (MemoryBlockDescriptor*)memBlock;
            uint8_t* pageBlock = memBlock + sizeof(MemoryBlockDescriptor);
            size_t compressedBlockSize = memoryBlockDescriptor->compressedSize;
            uint8_t targetPage = memoryBlockDescriptor->memoryPage;

            // Determine emulator target page
            MemoryPageDescriptor targetPageDescriptor = resolveSnapshotPage(targetPage, _memoryMode);

            // Skip invalid/unknown pages (don't allocate or crash)
            if (targetPageDescriptor.mode == BANK_INVALID)
            {
                MLOGWARNING("Z80 v3 snapshot: unknown page %d in %s mode, skipping",
                            targetPage, (_memoryMode == Z80_48K) ? "48K" : "128K");
                // Advance to next block
                size_t skipSize = (compressedBlockSize == 0xFFFF) ? PAGE_SIZE : compressedBlockSize;
                memBlock += skipSize + sizeof(MemoryBlockDescriptor);
                if (memBlock >= pBufferEnd)
                {
                    memBlock = nullptr;
                }
                continue;
            }

            // Bounds check: ensure compressed data is within file
            size_t actualBlockSize = (compressedBlockSize == 0xFFFF) ? PAGE_SIZE : compressedBlockSize;
            if (pageBlock + actualBlockSize > pBufferEnd)
            {
                MLOGWARNING("Z80 v3 snapshot truncated: block data at offset %zu (size %zu) exceeds file size %zu",
                            static_cast<size_t>(pageBlock - pBuffer), actualBlockSize, _fileSize);
                break;
            }

            // Allocate memory page and register it in one of staging collections (ROM or RAM)
            // De-allocation will be performed after staging changes applied to main emulator memory
            // or in loader destructor
            uint8_t* pageBuffer = new uint8_t[PAGE_SIZE];
            switch (targetPageDescriptor.mode)
            {
                case BANK_ROM:
                    _stagingROMPages[targetPageDescriptor.page] = pageBuffer;
                    break;
                case BANK_RAM:
                    _stagingRAMPages[targetPageDescriptor.page] = pageBuffer;
                    break;
                default:
                    // Should not reach here - already handled above
                    delete[] pageBuffer;
                    break;
            }

            // Unpack memory block to target staging page
            if (compressedBlockSize == 0xFFFF)
            {
                // Block is not compressed and has fixed length 0x4000 (16384)
                compressedBlockSize = PAGE_SIZE;
                memcpy(pageBuffer, pageBlock, PAGE_SIZE);
            }
            else
            {
                // Block is compressed so we need to decompress it
                decompressPage(pageBlock, compressedBlockSize, pageBuffer, PAGE_SIZE);
            }

            uint8_t* nextMemBlock = memBlock + compressedBlockSize + sizeof(MemoryBlockDescriptor);
            if (nextMemBlock >= pBufferEnd || nextMemBlock == memBlock)
            {
                memBlock = nullptr;
            }
            else
            {
                memBlock = nextMemBlock;
            }
        }

        result = true;
    }

    return result;
}

bool LoaderZ80::validateHeaderSanity(Z80SnapshotVersion version)
{
    // Validate Z80-specific header constraints based on detected version
    // NOTE: Generic file type detection (ASCII, etc.) will be handled by a shared component
    Z80Header_v1 header;
    if (_data.size() < sizeof(header))
        return false;
    memcpy(&header, _data.data(), sizeof(header));

    // Note: IFF flags are sanitized during register extraction rather than rejected
    // This allows loading of files with corrupted IFF values but still valid otherwise

    // For v2/v3, validate extended header constraints
    if (version == Z80v2 || version == Z80v3)
    {
        Z80Header_v2 headerV2;
        if (_data.size() < sizeof(headerV2))
            return false;
        memcpy(&headerV2, _data.data(), sizeof(headerV2));

        // Validate extended header length based on expected version
        uint16_t extLen = headerV2.extendedHeaderLen;
        if (version == Z80v2 && extLen != 23)
        {
            MLOGWARNING("Z80 file '%s' v2 has invalid extended header length %d (expected 23)", 
                        _path.c_str(), extLen);
            return false;
        }
        if (version == Z80v3 && extLen != 54 && extLen != 55)
        {
            MLOGWARNING("Z80 file '%s' v3 has invalid extended header length %d (expected 54 or 55)", 
                        _path.c_str(), extLen);
            return false;
        }

        // Validate model number is in valid range (0-13 per Z80 format spec)
        if (headerV2.model > 13)
        {
            MLOGWARNING("Z80 file '%s' has invalid model number %d (max 13)", 
                        _path.c_str(), headerV2.model);
            return false;
        }
    }

    return true;
}

Z80MemoryMode LoaderZ80::getMemoryModeV2(uint8_t model)
{
    Z80MemoryMode result = Z80_48K;

    switch (model)
    {
        case 0:  // 48K
        case 1:  // 48K + IF1
            result = Z80_48K;
            break;
        case 2:  // SamRam
            result = Z80_SAMCOUPE;
            break;
        case 3:   // 128K (v2 only)
        case 4:   // 128K + IF1 (v2 only)
        case 7:   // +3
        case 8:   // +3 (alternate)
        case 9:   // Pentagon 128K
        case 12:  // +2
        case 13:  // +2A
            result = Z80_128K;
            break;
        case 10:  // Scorpion 256K
            result = Z80_256K;
            break;
        default:
            result = Z80_48K;
            break;
    }

    return result;
}

Z80MemoryMode LoaderZ80::getMemoryModeV3(uint8_t model)
{
    Z80MemoryMode result = Z80_48K;

    switch (model)
    {
        case 0:  // 48K
        case 1:  // 48K + IF1
        case 3:  // 48K + MGT (v3: model 3 is 48K, not 128K!)
            result = Z80_48K;
            break;
        case 2:  // SamRam
            result = Z80_SAMCOUPE;
            break;
        case 4:   // 128K (v3: model 4 is 128K base)
        case 5:   // 128K + IF1
        case 6:   // 128K + MGT
        case 7:   // +3
        case 8:   // +3 (alternate)
        case 9:   // Pentagon 128K
        case 12:  // +2
        case 13:  // +2A
            result = Z80_128K;
            break;
        case 10:  // Scorpion 256K
            result = Z80_256K;
            break;
        default:
            result = Z80_48K;
            break;
    }

    return result;
}

Z80Registers LoaderZ80::getZ80Registers(const Z80Header_v1& header, uint16_t pc)
{
    Z80Registers result = {};
    result.a = header.reg_A;
    result.f = header.reg_F;
    result.bc = header.reg_BC;
    result.de = header.reg_DE;
    result.hl = header.reg_HL;
    result.alt.a = header.reg_A1;
    result.alt.f = header.reg_F1;
    result.alt.bc = header.reg_BC1;
    result.alt.de = header.reg_DE1;
    result.alt.hl = header.reg_HL1;
    result.ix = header.reg_IX;
    result.iy = header.reg_IY;

    result.sp = header.reg_SP;

    result.iff1 = header.IFF1 ? 1 : 0;
    result.iff2 = header.IFF2 ? 1 : 0;
    result.i = header.reg_I;

    // Per spec: reg_R contains lower 7 bits; flags bit 0 contains bit 7 of R
    // Handle flags=255 compatibility case
    uint8_t flags = (header.flags == 255) ? 1 : header.flags;
    result.r_low = header.reg_R & 0x7F;
    result.r_hi = (flags & 0x01) << 7;

    // Interrupt mode: Z80 only supports modes 0, 1, 2
    // Mask to lower 2 bits and validate
    uint8_t im = header.im & 0x03;
    if (im > 2)
    {
        MLOGWARNING("Invalid interrupt mode %d in Z80 snapshot, using mode 0", header.im);
        im = 0;
    }
    result.im = im;

    result.pc = pc;

    result.memptr = 0;
    result.q = 0;

    return result;
}

/// @brief Stage the AY registers of a v2 / v3 snapshot
/// @details Bytes 38-54 hold the selected AY register and the 16 AY registers. They describe the machine's AY
///          on 128K-class models, and on 48K only when byte 37 bit 2 ("AY sound in use, even on 48K
///          machines") is set; otherwise they are not state and stay unused
void LoaderZ80::stagePeripheralState(const Z80Header_v2& header)
{
    constexpr uint8_t Z80_FLAGS2_AY_IN_USE = 0b0000'0100;

    const bool ayState = (_memoryMode != Z80_48K) || (header.r2 & Z80_FLAGS2_AY_IN_USE) != 0;
    _hasAyRegisters = ayState;
    if (ayState)
    {
        memcpy(_ayRegisters, header.ay, sizeof(_ayRegisters));
    }
}

/// @brief Write the staged AY registers into the machine's first AY chip
/// @details Called after core.Reset(). The registers go through the chip's logic-level interface (the same one
///          the saver reads back from), then the selected register is restored. On a TurboSound machine this is
///          chip 0, the chip the reset leaves selected
void LoaderZ80::commitPeripheralState(const snapshot::Image& image)
{
    if (image.ay.empty() || _context->pSoundManager == nullptr)
        return;

    SoundChip_AY8910* psg = _context->pSoundManager->getAYChip(0);
    if (psg == nullptr)
        return;

    for (uint8_t reg = 0; reg < 16; reg++)
    {
        psg->writeRegister(reg, image.ay[0].registers[reg]);
    }

    psg->setRegister(image.ay[0].selected & 0x0F);
}

/// @brief Compress memory page using Z80 RLE compression
/// @details RLE format: ED ED nn bb = repeat byte 'bb' nn times
///          - Only sequences of ≥5 identical bytes are compressed
///          - ED bytes are special: even 2 consecutive EDs → ED ED 02 ED
///          - Single ED followed by non-ED is written as-is (ED xx)
/// @param src Source uncompressed data
/// @param srcLen Source data length
/// @param dst Destination buffer for compressed data
/// @param dstLen Destination buffer size
/// @return Number of bytes written to dst
size_t LoaderZ80::compressPage(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
    if (src == nullptr || dst == nullptr || srcLen == 0 || dstLen == 0)
    {
        return 0;
    }

    size_t srcPos = 0;
    size_t dstPos = 0;

    while (srcPos < srcLen && dstPos < dstLen)
    {
        uint8_t byte = src[srcPos];

        // Count consecutive identical bytes
        size_t runLen = 1;
        while (srcPos + runLen < srcLen && src[srcPos + runLen] == byte && runLen < 255)
        {
            runLen++;
        }

        // Special case: ED bytes must always be encoded if there are 2+ of them
        if (byte == 0xED)
        {
            if (runLen >= 2)
            {
                // Encode ED sequence: ED ED count ED
                if (dstPos + 4 > dstLen) break;
                dst[dstPos++] = 0xED;
                dst[dstPos++] = 0xED;
                dst[dstPos++] = static_cast<uint8_t>(runLen);
                dst[dstPos++] = 0xED;
                srcPos += runLen;
            }
            else
            {
                // Single ED: write as-is
                if (dstPos + 1 > dstLen) break;
                dst[dstPos++] = byte;
                srcPos++;
            }
        }
        else if (runLen >= 5)
        {
            // Encode RLE sequence: ED ED count value
            if (dstPos + 4 > dstLen) break;
            dst[dstPos++] = 0xED;
            dst[dstPos++] = 0xED;
            dst[dstPos++] = static_cast<uint8_t>(runLen);
            dst[dstPos++] = byte;
            srcPos += runLen;
        }
        else
        {
            // Write literal bytes (run too short for compression)
            for (size_t i = 0; i < runLen && dstPos < dstLen; i++)
            {
                dst[dstPos++] = byte;
            }
            srcPos += runLen;
        }
    }

    return dstPos;
}

void LoaderZ80::decompressPage(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
    decompressPage_Optimized(src, srcLen, dst, dstLen);
}

/// @brief Original implementation - byte-by-byte RLE decompression (for benchmarking)
void LoaderZ80::decompressPage_Original(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
    /// region <Sanity check>
    if (src == nullptr || dst == nullptr || srcLen == 0 || dstLen == 0)
    {
        return;
    }
    /// endregion </Sanity check>

    memset(dst, 0, dstLen);
    while (srcLen > 0 && dstLen > 0)
    {
        if (srcLen >= 4 && src[0] == 0xED && src[1] == 0xED)
        {
            for (uint8_t i = src[2]; i; i--)
            {
                *dst++ = src[3], dstLen--;
            }
            srcLen -= 4;
            src += 4;
        }
        else
        {
            *dst++ = *src++;
            --dstLen;
            --srcLen;
        }
    }
}

/// @brief Optimized implementation - uses memset for RLE sequences (3-7x faster)
void LoaderZ80::decompressPage_Optimized(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
    /// region <Sanity check>
    if (src == nullptr || dst == nullptr || srcLen == 0 || dstLen == 0)
    {
        return;
    }
    /// endregion </Sanity check>

    uint8_t* dstEnd = dst + dstLen;

    while (srcLen > 0 && dst < dstEnd)
    {
        if (srcLen >= 4 && src[0] == 0xED && src[1] == 0xED)
        {
            // RLE sequence: ED ED nn bb = repeat 'bb' nn times
            uint8_t count = src[2];
            uint8_t value = src[3];

            size_t remaining = dstEnd - dst;
            size_t fillLen = std::min<size_t>(count, remaining);
            
            // Warn if compressed data would overflow buffer
            if (count > remaining)
            {
                MLOGWARNING("Z80 decompression overflow: RLE sequence requests %d bytes but only %zu available, truncating",
                            count, remaining);
            }

            // Use memset for bulk fill (SIMD-accelerated)
            memset(dst, value, fillLen);
            dst += fillLen;

            srcLen -= 4;
            src += 4;
        }
        else
        {
            // Literal byte copy
            *dst++ = *src++;
            --srcLen;
        }
    }

    // Zero-fill any remaining destination (if source exhausted early)
    if (dst < dstEnd)
    {
        memset(dst, 0, dstEnd - dst);
    }
}

size_t LoaderZ80::decompressV1Data(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
    if (src == nullptr || dst == nullptr || srcLen == 0 || dstLen == 0)
    {
        return 0;
    }

    uint8_t* srcStart = src;
    uint8_t* dstEnd = dst + dstLen;

    while (srcLen > 0 && dst < dstEnd)
    {
        // Check for end marker: 00 ED ED 00
        if (srcLen >= 4 && src[0] == 0x00 && src[1] == 0xED && src[2] == 0xED && src[3] == 0x00)
        {
            break;
        }

        // Check for RLE sequence: ED ED nn bb
        if (srcLen >= 4 && src[0] == 0xED && src[1] == 0xED)
        {
            uint8_t count = src[2];
            uint8_t value = src[3];

            size_t remaining = dstEnd - dst;
            size_t fillLen = std::min<size_t>(count, remaining);
            
            // Warn if compressed data would overflow buffer
            if (count > remaining)
            {
                MLOGWARNING("Z80 decompression overflow: RLE sequence requests %d bytes but only %zu available, truncating",
                            count, remaining);
            }
            
            memset(dst, value, fillLen);
            dst += fillLen;

            srcLen -= 4;
            src += 4;
        }
        else
        {
            *dst++ = *src++;
            --srcLen;
        }
    }

    // Zero-fill remaining destination
    if (dst < dstEnd)
    {
        memset(dst, 0, dstEnd - dst);
    }

    return static_cast<size_t>(src - srcStart);
}

MemoryPageDescriptor LoaderZ80::resolveSnapshotPage(uint8_t page, Z80MemoryMode mode)
{
    MemoryPageDescriptor result;
    result.mode = MemoryBankModeEnum::BANK_INVALID;  // Default to invalid - must be explicitly set
    result.page = 0;
    result.addressInPage = 0x0000;

    switch (mode)
    {
        case Z80_48K:
            switch (page)
            {
                case 0:
                    result.mode = MemoryBankModeEnum::BANK_ROM;
                    result.page = 0;
                    break;
                case 1:
                    result.mode = MemoryBankModeEnum::BANK_ROM;
                    result.page = 0;
                    break;
                case 4:
                    // 0x8000 - 0xBFFF -> RAM Page 2
                    result.mode = MemoryBankModeEnum::BANK_RAM;
                    result.page = 2;
                    break;
                case 5:
                    // 0xC000 - 0xFFFF -> RAM Page 0
                    result.mode = MemoryBankModeEnum::BANK_RAM;
                    result.page = 0;
                    break;
                case 8:
                    // 0x4000 - 0x7FFF -> RAM Page 5
                    result.mode = MemoryBankModeEnum::BANK_RAM;
                    result.page = 5;
                    break;
                default:
                    // Unknown page for 48K mode - leave as BANK_INVALID
                    break;
            }
            break;
        case Z80_128K:
            if (page < 3)
            {
                result.mode = MemoryBankModeEnum::BANK_ROM;
                result.page = page;
            }
            else if (page < 11)
            {
                // 3 -> RAM Page 0
                // 4 -> RAM Page 1
                // 10 -> RAM Page 7
                result.mode = MemoryBankModeEnum::BANK_RAM;
                result.page = page - 3;
            }
            // else: page >= 11, leave as BANK_INVALID
            break;
        case Z80_256K:
            // Scorpion ZS-256: 3 -> RAM page 0 ... 18 -> RAM page 15
            if (page >= 3 && page < 19)
            {
                result.mode = MemoryBankModeEnum::BANK_RAM;
                result.page = page - 3;
            }
            break;
        case Z80_SAMCOUPE:
            // Not implemented - leave as BANK_INVALID
            MLOGWARNING("Z80 SamCoupe mode not implemented, skipping page %d", page);
            break;
        default:
            break;
    }

    return result;
}

/// @brief Free all memory allocated for snapshot staging
void LoaderZ80::freeStagingMemory()
{
    // Clean-up staging ROM pages
    for (size_t idx = 0; idx < MAX_ROM_PAGES; idx++)
    {
        uint8_t* ptr = _stagingROMPages[idx];
        if (ptr != nullptr)
        {
            delete ptr;
            _stagingROMPages[idx] = nullptr;
        }
    }

    // Clean-up staging RAM pages
    for (size_t idx = 0; idx < MAX_RAM_PAGES; idx++)
    {
        uint8_t* ptr = _stagingRAMPages[idx];
        if (ptr != nullptr)
        {
            delete ptr;
            _stagingRAMPages[idx] = nullptr;
        }
    }
}

/// endregion </Helper methods>

/// region <Debug methods>

std::string LoaderZ80::dumpSnapshotInfo()
{
    std::string result;
    std::stringstream ss;

    result = ss.str();
    return result;
}

std::string LoaderZ80::dumpSnapshotMemoryInfo()
{
    std::string result;
    std::stringstream ss;

    if (_stagingLoaded)
    {
        ss << "Z80 snapshot memory pages usage: " << std::endl;

        for (size_t idx = 0; idx < MAX_ROM_PAGES; idx++)
        {
            if (_stagingROMPages[idx] != nullptr)
            {
                ss << StringHelper::Format("ROM %d", idx) << std::endl;
            }
        }

        for (size_t idx = 0; idx < MAX_RAM_PAGES; idx++)
        {
            if (_stagingRAMPages[idx] != nullptr)
            {
                ss << StringHelper::Format("RAM %d", idx) << std::endl;
            }
        }
    }

    result = ss.str();
    return result;
}

/// endregion </Debug methods>