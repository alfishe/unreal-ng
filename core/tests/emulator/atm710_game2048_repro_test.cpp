// Regression test for the ATM710 1024K + 2048.scl black-screen report (FIXED).
// The game loads from TR-DOS, then on entry switches FF77 video mode 3 -> 0
// (OUT (#BD77),#A8 at $E076). The retired atmMemSwap() misfeature permuted
// A5-A7<->A8-A10 across ALL RAM on every pFF77 bit0 transition, so that OUT
// scrambled the freshly loaded code inside the port handler and the next
// fetch ($E07D CALL $E182) derailed the CPU into a NOP slide: dead ISR,
// black screen. The original Unreal Speccy gates the permutation behind the
// default-OFF "AtmMemSwap" ini option, so the faithful default is to never
// run it (see PortDecoder_ATM710::Port_FF77_Out).
//
// The test boots the machine through the BIOS menu into TR-DOS 5.03 (the
// same verified flow as ATM710TrdosBoot_Test), inserts the original SCL and
// injects RUN "boot" straight into the TR-DOS command buffer
// (BasicEncoder::injectToTRDOS - no keyboard-mode fights), then samples the
// machine state every frame: video mode ports (pFF77/aFF77), memory paging
// (p7FFD/pEFF7), PC (NOP-slide / derail detection), the $FF63 ISR frame
// ticker and per-page VRAM content.
//
// The SCL catalog is deliberately forged (boot: type 'B', Length 518,
// SizeInSectors 0xFF; data: type '0', 61 sectors). The boot BASIC is a single
// line `RANDOMIZE USR 23882: REM <machine code>` whose embedded code writes
// the ATM xx77 port (OUT (C),A with BC = 0xFFF7 via the $5E15 thunk), prints
// "2048 is loading...", loads "data" through TR-DOS $3D13 to $7000, relocates
// it to $C000 and jumps there.

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "debugger/disassembler/z80disasm.h"
#include "emulator/cpu/z80.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"
#include "loaders/disk/loader_scl.h"
#include "pch.h"
#include "stdafx.h"

class ATM710Game2048Repro_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;

protected:
    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }

    void TearDown() override
    {
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }

    /// One menu "down" press: CAPS SHIFT + 6 held together
    static void PressDown(const std::shared_ptr<Emulator>& emulator, int holdFrames = 8)
    {
        Keyboard* keyboard = emulator->GetContext()->pKeyboard;
        keyboard->PressKey(ZXKEY_CAPS_SHIFT);
        keyboard->PressKey(ZXKEY_6);
        emulator->RunNFrames(holdFrames, true);
        keyboard->ReleaseKey(ZXKEY_6);
        keyboard->ReleaseKey(ZXKEY_CAPS_SHIFT);
        emulator->RunNFrames(holdFrames, true);
    }

    /// Decode rows of the standard ZX bitmap screen on a RAM page to ASCII by
    /// matching each 8-byte cell against the classic font (ROM page 0, $3D00).
    /// Unmatched glyphs decode as '?' (TR-DOS ROM draws digits differently).
    static std::string DecodeZXRows(EmulatorContext* context, uint8_t screenPage, uint8_t rowFrom, uint8_t rowTo)
    {
        Memory* memory = context->pMemory;
        const uint8_t* vram = memory->RAMPageAddress(screenPage);
        const uint8_t* font = memory->ROMPageHostAddress(0) + 0x3D00;
        std::string result;
        for (uint8_t row = rowFrom; row < rowTo; row++)
        {
            for (uint8_t col = 0; col < 32; col++)
            {
                uint8_t glyph[8];
                for (int k = 0; k < 8; k++)
                {
                    uint16_t y = row * 8 + k;
                    uint16_t addr = ((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2);
                    glyph[k] = vram[addr + col];
                }
                char best = '?';
                for (int c = 0x20; c < 0x80; c++)
                {
                    if (memcmp(glyph, font + (uint32_t)(c - 0x20) * 8, 8) == 0)
                    {
                        best = (char)c;
                        break;
                    }
                }
                result += best;
            }
            result += '\n';
        }
        return result;
    }

    /// Decode rows of the M_ATMTX 80-column text screen (the ATM BIOS video
    /// mode): chars1 = vp[0x1C0 + 64r + n/2], chars0 = vp[0x2000 + same]
    static std::string DecodeTextRows(EmulatorContext* context, uint8_t rowFrom, uint8_t rowTo)
    {
        EmulatorState& state = context->emulatorState;
        Memory* memory = context->pMemory;
        uint8_t videoPage = (state.p7FFD & 0x08) ? 7 : 5;
        const uint8_t* vp = memory->RAMPageAddress(videoPage);
        std::string result;
        for (uint32_t row = rowFrom; row < rowTo; row++)
        {
            for (uint32_t n = 0; n < 80; n++)
            {
                uint32_t byteIdx = 0x1C0 + 64 * row + n / 2;
                bool fromP0 = (n % 2 == 0);
                uint8_t code = fromP0 ? vp[byteIdx] : vp[0x2000 + byteIdx];
                result += (code >= 0x20 && code < 0x7F) ? (char)code : (code == 0 ? ' ' : '.');
            }
            result += '\n';
        }
        return result;
    }

    /// Non-zero byte count of a RAM page (content fingerprint)
    static int PageNonZero(Memory* memory, uint8_t page)
    {
        const uint8_t* mem = memory->RAMPageAddress(page);
        int count = 0;
        for (uint32_t i = 0; i < 0x4000; i++)
        {
            if (mem[i] != 0)
            {
                count++;
            }
        }
        return count;
    }

    /// Non-zero byte count of a page region
    static int RegionNonZero(Memory* memory, uint8_t page, uint32_t offset, uint32_t length)
    {
        const uint8_t* mem = memory->RAMPageAddress(page) + offset;
        int count = 0;
        for (uint32_t i = 0; i < length; i++)
        {
            if (mem[i] != 0)
            {
                count++;
            }
        }
        return count;
    }

    static std::string Hex(uint32_t value, int width = 2)
    {
        std::ostringstream out;
        out << std::hex << std::uppercase << std::setfill('0') << std::setw(width) << value;
        return out.str();
    }
};

TEST_F(ATM710Game2048Repro_Test, RunBootReachesGameScreen)
{
    // ---- Phase A: cold boot to the ATM BIOS menu -------------------------
    auto emulator = _manager->CreateEmulatorWithModelAndRAM("atm710-2048-repro", "ATM710", 1024, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    emulator->EnableTurboMode();  // diagnostics read VRAM/ports/PC, not rendered pixels
    EmulatorContext* context = emulator->GetContext();
    EmulatorState& state = context->emulatorState;
    Memory* memory = context->pMemory;

    EmulatorTestHelper::RunUntil(
        emulator.get(),
        [&] { return DecodeTextRows(context, 0, 24).find("SPECTRUM128") != std::string::npos; }, 300);

    // ---- Phase B: BIOS menu -> SPECTRUM 128 ------------------------------
    Keyboard* keyboard = context->pKeyboard;
    for (int i = 0; i < 2; i++)
    {
        PressDown(emulator, 4);
    }
    keyboard->PressKey(ZXKEY_ENTER);
    emulator->RunNFrames(12, true);
    keyboard->ReleaseKey(ZXKEY_ENTER);
    EmulatorTestHelper::RunUntil(
        emulator.get(),
        [&] { return DecodeZXRows(context, (state.p7FFD & 0x08) ? 7 : 5, 6, 15).find("TR-DOS") != std::string::npos; }, 250);

    // ---- Phase C: insert the original SCL into drive A: ------------------
    std::string sclPath = TestPathHelper::GetTestDataPath("machines/atm/2048.scl");
    ASSERT_TRUE(FileHelper::FileExists(sclPath)) << "Fixture missing: " << sclPath;
    LoaderSCL sclLoader(context, sclPath);
    ASSERT_TRUE(sclLoader.loadImage()) << "SCL not loaded: " << sclPath;
    WD1793* wd1793 = context->pBetaDisk;
    ASSERT_NE(wd1793, nullptr);
    FDD* fdd = wd1793->getDrive();
    ASSERT_NE(fdd, nullptr);
    fdd->insertDisk(sclLoader.getImage());

    // Verify the TRD image the SCL loader built: catalog entries, volume info
    // and the real bytes at the 'data' extent (locator 0x10 + 255)
    {
        DiskImage* img = sclLoader.getImage();
        DiskImage::Track* t0 = img->getTrackForCylinderAndSide(0, 0);
        const uint8_t* cat = t0->getSector(0)->data;
        for (int e = 0; e < 2; e++)
        {
            const uint8_t* d = cat + e * 16;
            std::cout << "[DISK] entry" << e << ": name='" << std::string((const char*)d, 8)
                      << "' type=" << Hex(d[8])
                      << " start=" << Hex(d[9] | (d[10] << 8), 4)
                      << " len=" << Hex(d[11] | (d[12] << 8), 4)
                      << " siz=" << Hex(d[13])
                      << " startSector=" << (int)d[14] << " startTrack=" << (int)d[15] << "\n";
        }
        int dataLocator = 0x10 + 255;
        auto dumpDataSector = [&](const char* tag, int sectorIdx)
        {
            int loc = dataLocator + sectorIdx;
            const uint8_t* sec = img->getTrack(loc / 16)->getSector(loc % 16)->data;
            std::cout << "[DISK] " << tag << " data sec " << (sectorIdx + 1)
                      << " (locator " << loc << ", img track " << loc / 16
                      << " = cyl " << (loc / 16) / 2 << " side " << (loc / 16) % 2 << "): ";
            for (int i = 0; i < 12; i++)
            {
                std::cout << Hex(sec[i]);
            }
            std::cout << "\n";
        };
        dumpDataSector("pre", 0);
        dumpDataSector("pre", 17);
        dumpDataSector("pre", 39);
        dumpDataSector("pre", 60);
        const uint8_t* vol2 = img->getTrack(0)->getSector(8)->data;
        std::cout << "[DISK] volume: files=" << (int)vol2[0xE4]
                  << " free=" << (vol2[0xE5] | (vol2[0xE6] << 8))
                  << " firstFreeTrack=" << (int)vol2[0xE2]
                  << " firstFreeSector=" << (int)vol2[0xE1] << "\n";
    }

    // ---- Phase D: 128K menu -> TR-DOS 5.03 -------------------------------
    for (int i = 0; i < 4; i++)
    {
        PressDown(emulator);
    }
    keyboard->PressKey(ZXKEY_ENTER);
    emulator->RunNFrames(12, true);
    keyboard->ReleaseKey(ZXKEY_ENTER);
    // "BETA 128" avoids the digits the TR-DOS font draws differently
    // (the "?" cells in the pre-existing banner decode flake)
    EmulatorTestHelper::RunUntil(
        emulator.get(),
        [&] { return DecodeZXRows(context, (state.p7FFD & 0x08) ? 7 : 5, 0, 4).find("BETA") != std::string::npos; }, 900);
    for (int i = 0; i < 50; i++)
    {
        emulator->RunFrame(true);
    }
    std::cout << "[D] TR-DOS up: pc=" << Hex(context->pCore->GetZ80()->pc, 4)
              << " pFF77=" << Hex(state.pFF77) << " p7FFD=" << Hex(state.p7FFD) << "\n";

    // ---- Phase E: port trace (xx77 family, 7FFD, Beta128 #FF) ------------
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    struct PortEvent
    {
        int frame;
        uint16_t port;
        uint16_t pc;
        uint8_t value;
    };
    std::vector<PortEvent> portTrace;
    std::vector<PortEvent> inTrace;   // IN reads after the boot settles (frame > 460)
    int currentFrame = 0;

    // Full-trace window around the game init loop: armed on the boot thunk
    // (OUT #3FF7,7F from the $5E12 BASIC-embedded code), captures every bus
    // event plus the instruction stream of the first loop iterations
    struct FullEvent
    {
        int frame;
        char type;
        uint16_t port;
        uint16_t pc;
        uint8_t value;
    };
    std::vector<FullEvent> fullTrace;   // I/O bus events only (memory R/W excluded)
    std::vector<FullEvent> w1Writes;    // memory writes into the w1 window 0x4000-0x7FFF
    std::vector<FullEvent> depackReads; // reads issued by the depacker loop (pc E0C0-E16F)
    std::vector<FullEvent> w0Reads;     // reads from the w0 window 0x0000-0x3FFF (the source)
    std::vector<FullEvent> w2Writes;    // writes into the w2 window 0x8000-0xBFFF (ROM:63?)
    std::vector<FullEvent> w2Reads;     // reads from the w2 window by code outside w2
    std::vector<FullEvent> w3Writes;    // writes into the w3 window 0xC000-0xFFFF (the code page!)
    // Snapshot of the bank-table state right after every paging OUT: compares
    // the port-decoder view (pFFF7) against the physical bank tables
    struct PageProbe
    {
        int frame;
        uint16_t port;
        uint8_t value;
        uint8_t w[4];
        uint8_t expected[4];
    };
    std::vector<PageProbe> pageProbes;
    int readProbeCount = 0;   // data-read verification probes (depacker source)
    int fetchProbeCount = 0;  // opcode-fetch verification probes (w3 code page)
    int depFetchProbe = 0;    // depacker-block fetch verification probes
    std::vector<std::pair<int, uint16_t>> m1Seq;  // (frame, pc)
    bool windowArmed = true;
    bool windowActive = false;
    int windowStartFrame = -1;

    // Loader-phase forensics (dbg-3b): everything before the boot thunk's
    // final OUT #3FF7,7F - the TR-DOS load of "boot" and "data". Page 0 ends
    // up with only 0x0000-0x1001 delivered, so the two questions are: (1) did
    // the FDC read the data file's sectors at all (command timeline + payload
    // byte counts), and (2) which code wrote page 0 and where it stopped
    struct FdcEvent
    {
        int frame;
        char type;
        uint16_t port;
        uint16_t pc;
        uint8_t value;
    };
    struct FdcCmd
    {
        int frame;
        uint16_t pc;
        uint8_t cmd;
        uint8_t track;
        uint8_t sector;
        uint8_t sys;
        long dataIns;
    };
    std::vector<FdcEvent> fdcRaw;           // register I/O (no #7F reads / #1F polls)
    std::vector<FdcCmd> fdcCmds;            // every command OUT #1F + live register state
    std::map<int, int> fdcDataPerFrame;     // #7F payload reads per frame
    std::map<uint16_t, int> fdcStatusPolls; // #1F status reads aggregated by pc
    uint8_t fdcTrackReg = 0;
    uint8_t fdcSectorReg = 0;
    uint8_t fdcSysPort = 0;
    long fdcDataIns = 0;
    struct Page0Write
    {
        int frame;
        uint16_t addr;
        uint16_t pc;
        uint8_t value;
    };
    std::vector<Page0Write> page0Writes;               // writes landing in physical RAM:0
    std::map<int, std::map<uint16_t, int>> page0Writers;  // frame -> pc -> count
    std::map<uint16_t, std::tuple<int, int, long long>> loaderM1;  // pc -> first, last, count
    bool loaderDone = false;
    std::vector<std::pair<int, uint8_t>> fdcDataSample;  // (frame, #7F value) during the data phase
    struct MemEvent
    {
        int frame;
        uint16_t addr;
        uint16_t pc;
        uint8_t value;
    };
    std::vector<MemEvent> bufferWrites;   // writes into 0x7000-0xB000 during the data phase
    std::vector<MemEvent> expanderReads;  // reads from 0x7000-0x77FF by the expander at 5E8D-5F20
    std::vector<MemEvent> lateWrites;     // every memory write f415+ (store-loop death forensics)
    std::map<uint16_t, int> lateDataInPcs;  // pcs of IN #7F reads, frames 416+
    std::vector<uint8_t> loaderDonePage0;   // page-0 content at the loader-done OUT 3FF7=7F
    std::vector<uint8_t> loaderDonePage12;
    std::vector<uint8_t> loaderDonePage15;
    struct PortGateEvent
    {
        int frame;
        uint16_t port;
        uint8_t value;
        uint16_t pc;
        uint16_t aFF77;
        uint8_t pFF77;
        uint8_t flags;
    };
    std::vector<PortGateEvent> gateWrites;  // xx77/xF7 OUTs, loader tail + game (post-decode state)
    std::vector<PortGateEvent> ffReads;     // INs on #xxFF ports (ATM status/palette group)
    std::map<uint16_t, long long> runtimePcPages;  // pc>>8 execution histogram, frames 455+
    long long runtimeSamples = 0;
    std::vector<MemEvent> ffVarWrites;       // writes into the $FF00-$FFFF variable block, frames 453+
    std::map<uint16_t, long long> addrHist;            // per-address M1 histogram, frames 466-472
    std::vector<std::pair<int, uint16_t>> gameSeq;     // M1 pcs in game/ISR regions, frames 462-468
    std::map<uint16_t, int> firstSeen;                 // first frame >=460 a key pc was fetched
    bool callArmed = false;                            // arm at the $E07D CALL fetch
    std::vector<MemEvent> p15Writes;                   // writes landing in physical p15 outside $FF00+, f452+
    std::vector<MemEvent> p15CodeWrites;               // writes into p15 $E000-$E300 (entry code), all detail
    std::vector<uint8_t> p15CodeAtCall;                // p15 $E000-$E300 bytes at the $E07D fetch
    DiskImage* dataPhaseImageProbe = sclLoader.getImage();   // re-read these bytes after the run
    cpu->busTraceHook = [&portTrace, &inTrace, &currentFrame, cpu, memory, &state,
                         &fullTrace, &w1Writes, &depackReads, &w0Reads, &w2Writes, &w2Reads, &w3Writes, &pageProbes,
                         &readProbeCount, &fetchProbeCount, &depFetchProbe,
                         &windowArmed, &windowActive, &windowStartFrame, &loaderDone,
                         &fdcRaw, &fdcCmds, &fdcDataPerFrame, &fdcStatusPolls,
                         &fdcTrackReg, &fdcSectorReg, &fdcSysPort, &fdcDataIns,
                         &page0Writes, &page0Writers, &fdcDataSample, &bufferWrites, &expanderReads,
                         &lateWrites, &lateDataInPcs, &loaderDonePage0, &loaderDonePage12, &loaderDonePage15,
                         &gateWrites, &ffReads, &runtimePcPages, &runtimeSamples, &ffVarWrites,
                         &addrHist, &gameSeq, &firstSeen, &callArmed, &p15Writes, &p15CodeAtCall, &p15CodeWrites](char type, uint16_t port, uint8_t value)
    {
        uint16_t low = port & 0x00FF;
        // Loader-phase capture: Beta128 FDC ports are #1F (status/command),
        // #3F (track), #5F (sector), #7F (data), #FF (system)
        if (!loaderDone)
        {
            if ((type == 'I' || type == 'O') &&
                (low == 0x1F || low == 0x3F || low == 0x5F || low == 0x7F || low == 0xFF))
            {
                if (low == 0x7F && type == 'I')
                {
                    fdcDataIns++;
                    fdcDataPerFrame[currentFrame]++;
                    if (currentFrame >= 416)
                    {
                        lateDataInPcs[cpu->m1_pc]++;
                    }
                    if (currentFrame >= 385 && fdcDataSample.size() < 8000)
                    {
                        fdcDataSample.emplace_back(currentFrame, value);
                    }
                }
                else if (low == 0x1F && type == 'I')
                {
                    fdcStatusPolls[cpu->m1_pc]++;  // dense polling - aggregate only
                }
                else if (fdcRaw.size() < 24000)
                {
                    fdcRaw.push_back({currentFrame, type, port, cpu->m1_pc, value});
                }
                if (type == 'O' && low == 0x1F && fdcCmds.size() < 4000)
                {
                    fdcCmds.push_back({currentFrame, cpu->m1_pc, value, fdcTrackReg, fdcSectorReg, fdcSysPort, fdcDataIns});
                }
                if (type == 'O' && low == 0x3F)
                {
                    fdcTrackReg = value;
                }
                if (type == 'O' && low == 0x5F)
                {
                    fdcSectorReg = value;
                }
                if (type == 'O' && low == 0xFF)
                {
                    fdcSysPort = value;
                }
            }
            // Data-phase buffer traffic: where the ROM's read loop stores the
            // payload, and what the expander actually consumes as its input
            if (type == 'W' && port >= 0x7000 && port < 0xB000 && currentFrame >= 380 && bufferWrites.size() < 6000)
            {
                bufferWrites.push_back({currentFrame, port, cpu->m1_pc, value});
            }
            if (type == 'R' && port >= 0x7000 && port < 0x7800 &&
                cpu->m1_pc >= 0x5E80 && cpu->m1_pc < 0x5F20 && expanderReads.size() < 512)
            {
                expanderReads.push_back({currentFrame, port, cpu->m1_pc, value});
            }
            // Store-loop death forensics: f415-f418 exclude the C000+ expansion
            // flood (the expander writes 16K there); f419+ capture everything
            if (type == 'W' && currentFrame >= 415 && lateWrites.size() < 16000 &&
                (port < 0xC000 || currentFrame >= 419))
            {
                lateWrites.push_back({currentFrame, port, cpu->m1_pc, value});
            }
            // Every write that lands in physical page 0, whatever window
            // currently maps it - the packed-stream delivery under scrutiny
            if (type == 'W' && page0Writes.size() < 200000)
            {
                uint8_t physPage = memory->GetPhysPageForZ80Address((uint16_t)(port & 0xC000));
                if (physPage == 0)
                {
                    page0Writes.push_back({currentFrame, port, cpu->m1_pc, value});
                    page0Writers[currentFrame][cpu->m1_pc]++;
                }
            }
        }
        if (windowArmed && type == 'O' && port == 0x3FF7 && value == 0x7F)
        {
            windowArmed = false;
            windowActive = true;
            loaderDone = true;
            windowStartFrame = currentFrame;
            const uint8_t* p0 = memory->RAMPageAddress(0);
            loaderDonePage0.assign(p0, p0 + 0x4000);
            const uint8_t* p12 = memory->RAMPageAddress(12);
            loaderDonePage12.assign(p12, p12 + 0x4000);
            const uint8_t* p15 = memory->RAMPageAddress(15);
            loaderDonePage15.assign(p15, p15 + 0x4000);
            std::cout << "[WINDOW] full bus + m1 trace armed at frame " << currentFrame << "\n";
        }
        // Runtime port-gate forensics: the hook fires AFTER DecodePortOut, so
        // aFF77/pFF77 already reflect whether the DOSEN||SYSEN gate accepted
        // the write (blocked writes leave the latches stale)
        if (type == 'O' && ((port & 0x00FF) == 0x77 || (port & 0x00FF) == 0xF7) &&
            currentFrame >= 438 && gateWrites.size() < 800)
        {
            gateWrites.push_back({currentFrame, port, value, cpu->m1_pc,
                                  static_cast<uint16_t>(state.aFF77), state.pFF77,
                                  static_cast<uint8_t>(state.flags & 0x3F)});
        }
        // Runtime execution-region histogram: which code page hosts the
        // instructions running after the loader (ISR / p0 kernel / p12 module)
        if (currentFrame >= 455 && currentFrame <= 525)
        {
            runtimePcPages[cpu->m1_pc >> 8]++;
            runtimeSamples++;
        }
        // Derailment forensics: $E07D (CALL $E182) is fetched at f465 but its
        // target $E182 never runs, and no interrupt is accepted ever again.
        // Record the complete M1 sequence from that CALL to the derailment,
        // plus the exact code bytes executing at that moment.
        if (currentFrame >= 460 && cpu->m1_pc == 0xE07D)
        {
            if (!callArmed)
            {
                const uint8_t* p15 = memory->RAMPageAddress(15);
                p15CodeAtCall.assign(p15 + 0x2000, p15 + 0x2300);
            }
            callArmed = true;
        }
        if (callArmed && gameSeq.size() < 8000)
            gameSeq.push_back({currentFrame, cpu->m1_pc});
        // p15 code-corruption forensics: any write landing in physical page 15
        // outside the $FF00+ variable block, from loader-done on
        if (type == 'W' && currentFrame >= 452 && p15Writes.size() < 4000 && port < 0xFF00)
        {
            if (memory->GetPhysPageForZ80Address((uint16_t)(port & 0xC000)) == 15)
            {
                p15Writes.push_back({currentFrame, port, cpu->m1_pc, value});
                if (port >= 0xE000 && port < 0xE300 && p15CodeWrites.size() < 2000)
                    p15CodeWrites.push_back({currentFrame, port, cpu->m1_pc, value});
            }
        }
        if (currentFrame >= 460)
        {
            switch (cpu->m1_pc)
            {
                case 0xE07D: case 0xE182: case 0xE196: case 0xE080:
                case 0xE08B: case 0xE08C: case 0xFDFD: case 0xF373:
                    firstSeen.emplace(cpu->m1_pc, currentFrame);
                    break;
                default: break;
            }
        }
        if (currentFrame >= 466 && currentFrame <= 472)
            addrHist[cpu->m1_pc]++;
        if (type == 'W' && port >= 0xFF00 && currentFrame >= 453 && ffVarWrites.size() < 2000)
        {
            ffVarWrites.push_back({currentFrame, port, cpu->m1_pc, value});
        }
        if (type == 'I' && (port & 0x00FF) == 0xFF && currentFrame >= 445 && ffReads.size() < 400)
        {
            ffReads.push_back({currentFrame, port, value, cpu->m1_pc,
                               static_cast<uint16_t>(state.aFF77), state.pFF77,
                               static_cast<uint8_t>(state.flags & 0x3F)});
        }
        if (windowActive)
        {
            if ((type == 'I' || type == 'O') && currentFrame - windowStartFrame <= 20 && fullTrace.size() < 200000)
            {
                fullTrace.push_back({currentFrame, type, port, cpu->m1_pc, value});
            }
            // Any write into the w1 window: pages 1/3/5/7 can only be filled
            // through this address range while the pager maps them at 0x4000
            if (type == 'W' && port >= 0x4000 && port < 0x8000 && w1Writes.size() < 100000)
            {
                w1Writes.push_back({currentFrame, type, port, cpu->m1_pc, value});
            }
            // Depacker source reads: every load the depacker/copy loop issues
            // (ld a,(de) and friends), whatever window it points into
            if (type == 'R' && cpu->m1_pc >= 0xE0C0 && cpu->m1_pc < 0xE170 && depackReads.size() < 100000)
            {
                depackReads.push_back({currentFrame, type, port, cpu->m1_pc, value});
            }
            // Live verification of depacker source reads: CPU-visible value vs
            // the byte that the bank table's page actually contains
            if (type == 'R' && port >= 0x4000 && port < 0x8000 && readProbeCount < 20 &&
                cpu->m1_pc >= 0xE0C0 && cpu->m1_pc < 0xE170)
            {
                uint8_t cachePage = memory->GetPhysPageForZ80Address(0x4000);
                uint16_t off = port & 0x3FFF;
                uint8_t expect = (cachePage != 0xFF) ? memory->RAMPageAddress(cachePage)[off] : 0xEE;
                std::cout << "[RPROBE] f" << currentFrame << " cpu read #" << Hex(port, 4) << "=" << Hex(value)
                          << " @pc=" << Hex(cpu->m1_pc, 4)
                          << " | cacheW1=" << (int)cachePage
                          << " pageByte=" << Hex(expect)
                          << (expect != value ? "  <<< DIVERGENT" : "") << "\n";
                readProbeCount++;
            }
            // Live verification of w3 opcode fetches vs the page-15 content
            if (type == 'R' && port >= 0xE000 && port < 0xE200 && port == cpu->m1_pc && fetchProbeCount < 12)
            {
                uint8_t cachePage3 = memory->GetPhysPageForZ80Address(0xC000);
                uint16_t off3 = port & 0x3FFF;
                uint8_t expect3 = (cachePage3 != 0xFF) ? memory->RAMPageAddress(cachePage3)[off3] : 0xEE;
                std::cout << "[FPROBE] f" << currentFrame << " fetch #" << Hex(port, 4) << "=" << Hex(value)
                          << " | cacheW3=" << (int)cachePage3
                          << " pageByte=" << Hex(expect3)
                          << (expect3 != value ? "  <<< DIVERGENT" : "") << "\n";
                fetchProbeCount++;
            }
            // Targeted: every opcode fetch in the depacker block E0C0-E16F vs
            // the live page-15 content (settle whether the executed code and
            // the page diverge mid-game)
            if (type == 'R' && port >= 0xE0C0 && port < 0xE170 && port == cpu->m1_pc && depFetchProbe < 48)
            {
                uint8_t cp = memory->GetPhysPageForZ80Address(0xC000);
                uint8_t exp = (cp != 0xFF) ? memory->RAMPageAddress(cp)[port & 0x3FFF] : 0xEE;
                std::cout << "[DFPROBE] f" << currentFrame << " fetch #" << Hex(port, 4) << "=" << Hex(value)
                          << " page" << (int)cp << "Byte=" << Hex(exp)
                          << (exp != value ? "  <<< DIVERGENT" : "") << "\n";
                depFetchProbe++;
                if (depFetchProbe == 1)
                {
                    // One-time live snapshot of the depacker block in page 15
                    std::string path = TestPathHelper::GetUniqueTestScratchPath("game2048x_p15_at_depack.bin");
                    std::ofstream out(path, std::ios::binary);
                    out.write((const char*)memory->RAMPageAddress(15), 0x4000);
                    std::cout << "[SNAP] page 15 at depack start -> " << path << "\n";
                }
            }
            // w0 window traffic: the packed-stream source (never executes)
            if (type == 'R' && port < 0x4000 && w0Reads.size() < 100000)
            {
                w0Reads.push_back({currentFrame, type, port, cpu->m1_pc, value});
            }
            // w2 window traffic (mapped ROM:63 by the game): writes are the
            // interesting half - on real hardware this ROM page may be RAM-backed
            if (type == 'W' && port >= 0x8000 && port < 0xC000 && w2Writes.size() < 100000)
            {
                w2Writes.push_back({currentFrame, type, port, cpu->m1_pc, value});
            }
            if (type == 'R' && port >= 0x8000 && port < 0xC000 &&
                !(cpu->m1_pc >= 0x8000 && cpu->m1_pc < 0xC000) && w2Reads.size() < 100000)
            {
                w2Reads.push_back({currentFrame, type, port, cpu->m1_pc, value});
            }
            // w3 writes: who rewrites the code page (page 15) mid-game?
            if (type == 'W' && port >= 0xC000 && w3Writes.size() < 100000)
            {
                w3Writes.push_back({currentFrame, type, port, cpu->m1_pc, value});
            }
            // Bank-table snapshot after every paging write (xxF7 family): the
            // physical page per window per the tables vs per pFFF7
            if (type == 'O' && (port & 0x00FF) == 0xF7 && (port & 0xC000) != 0 && pageProbes.size() < 2000)
            {
                int regSetPr = (state.p7FFD & 0x10) ? 4 : 0;
                PageProbe p{currentFrame, port, value, {}, {}};
                for (int b = 0; b < 4; b++)
                {
                    p.w[b] = memory->GetPhysPageForZ80Address((uint16_t)(b * 0x4000));
                    unsigned fff7 = state.pFFF7[regSetPr + b];
                    p.expected[b] = (fff7 & 0x100) ? 0xFF : (uint8_t)(fff7 & 0xFF);  // ROM -> 0xFF
                }
                pageProbes.push_back(p);
            }
        }
        // xx77 family (A7/A6 don't participate), 7FFD, Beta128 system port
        if (type == 'O' &&
            (low == 0x77 || low == 0x37 || low == 0xB7 || low == 0xF7 || low == 0xFD || low == 0xFF))
        {
            if (portTrace.size() < 20000)
            {
                portTrace.push_back({currentFrame, port, cpu->m1_pc, value});
            }
        }
        // What the game polls after the loader finished: every IN from the
        // game phase (pc outside the TR-DOS ROM range is implied - the boot
        // settled long before 460)
        if (type == 'I' && currentFrame > 460 && inTrace.size() < 60000)
        {
            inTrace.push_back({currentFrame, port, cpu->m1_pc, value});
        }
    };
    cpu->m1TraceHook = [&m1Seq, &windowActive, &currentFrame, &windowStartFrame, &loaderDone, &loaderM1](uint16_t pc)
    {
        if (!loaderDone)
        {
            auto& t = loaderM1[pc];
            if (std::get<2>(t) == 0)
            {
                std::get<0>(t) = currentFrame;
            }
            std::get<1>(t) = currentFrame;
            std::get<2>(t)++;
        }
        if (windowActive && currentFrame - windowStartFrame <= 16 && m1Seq.size() < 1200000)
        {
            m1Seq.push_back({currentFrame, pc});
        }
    };

    // ---- Phase F: RUN "boot" + per-frame state trace ---------------------
    auto injection = BasicEncoder::injectToTRDOS(memory, "RUN \"boot\"");
    EXPECT_TRUE(injection.success) << injection.message;
    BasicEncoder::injectEnter(memory);

    uint8_t lastFF77 = state.pFF77;
    uint8_t last7FFD = state.p7FFD;
    uint8_t lastEFF7 = state.pEFF7;
    bool sawLoadingText = false;
    bool nopSlideReported = false;
    uint16_t prevPc = cpu->pc;
    int nopSlideRun = 0;
    bool derailReported = false;
    std::map<uint16_t, int> pcHistogram;
    // ISR liveness probe: the game's IM2 handler increments a 32-bit frame
    // ticker at $FF63 (p15 offset 0x3F63, ISR tail INC block at $F4FF)
    uint32_t prevTicker = 0;
    int tickerIncrements = 0;
    // Game entry lands around frame 466 (loader -> $E051 OUT #AB -> $E076 OUT
    // #A8); ~90 more frames give the board time to render. Booting a real ROM
    // through the BIOS menu into TR-DOS is the documented slow-test exception.
    const int maxFrames = 560;

    auto dumpState = [&](const char* why)
    {
        uint8_t videoPage = (state.p7FFD & 0x08) ? 7 : 5;
        std::cout << "[" << why << "] frame=" << currentFrame
                  << " pc=" << Hex(cpu->pc, 4)
                  << " pFF77=" << Hex(state.pFF77) << " aFF77=" << Hex(state.aFF77 & 0xFFFF, 4)
                  << " p7FFD=" << Hex(state.p7FFD) << " pEFF7=" << Hex(state.pEFF7)
                  << " mode=" << (int)(state.pFF77 & 7)
                  << " vp=" << (int)videoPage
                  << " vpNZ=" << PageNonZero(memory, videoPage)
                  << " altNZ=" << PageNonZero(memory, videoPage - 4)
                  << " zxBitmapNZ=" << RegionNonZero(memory, videoPage, 0, 0x1800)
                  << "\n";
    };

    for (int i = 0; i < maxFrames; i++)
    {
        emulator->RunFrame(true);
        currentFrame++;

        pcHistogram[cpu->pc]++;

        // Sample the $FF63 frame ticker once the game phase starts; a frozen
        // ticker means the interrupt path died (the pre-fix derail left
        // iff1=0 and the counter stuck at its last value)
        if (currentFrame >= 470)
        {
            const uint8_t* p15 = memory->RAMPageAddress(15);
            uint32_t ticker = static_cast<uint32_t>(p15[0x3F63])
                            | (static_cast<uint32_t>(p15[0x3F64]) << 8)
                            | (static_cast<uint32_t>(p15[0x3F65]) << 16)
                            | (static_cast<uint32_t>(p15[0x3F66]) << 24);
            if (ticker != prevTicker)
            {
                tickerIncrements++;
                prevTicker = ticker;
            }
        }
        if (cpu->pc == (uint16_t)(prevPc + 1))
        {
            nopSlideRun++;
            if (nopSlideRun >= 100 && !nopSlideReported)
            {
                nopSlideReported = true;
                std::cout << "[NOP-SLIDE] frame=" << currentFrame
                          << " pc walks +1/frame since ~" << Hex((uint16_t)(cpu->pc - nopSlideRun), 4)
                          << " (sampled per frame)\n";
            }
        }
        else
        {
            nopSlideRun = 0;
        }
        prevPc = cpu->pc;

        // The game main code lives in window 3 (0xC000+); landing in the
        // 0x4000-0x7FFF window means it jumped into the w1 scratch pages.
        // Dump memory + disassemble IMMEDIATELY: after the derail the
        // wandering code overwrites pages and would contaminate the dump
        if (!derailReported && windowStartFrame >= 0 && cpu->pc >= 0x4000 && cpu->pc < 0x8000)
        {
            derailReported = true;
            dumpState("DERAIL");
            // Live window mapping at the derail: pFFF7[regSet..regSet+3] with
            // type/page decode plus the ROM/RAM mask context
            {
                int regSet = (state.p7FFD & 0x10) ? 4 : 0;
                uint8_t romBanks = (context->pCore && context->pCore->GetROM()) ? context->pCore->GetROM()->GetROMBanksLoaded() : 0;
                std::cout << "[DERAIL] p7FFD=" << Hex(state.p7FFD) << " regSet=" << regSet
                          << " romBanks=" << (int)romBanks << " aFF77=" << Hex(state.aFF77 & 0xFFFF, 4) << "\n";
                for (int bank = 0; bank < 4; bank++)
                {
                    unsigned fff7 = state.pFFF7[regSet + bank];
                    unsigned t = fff7 & 0x300;
                    unsigned page = fff7 & 0xFF;
                    const char* tname = (t == 0x000) ? "RAM7FFD" : (t == 0x100) ? "ROM7FFD" : (t == 0x200) ? "RAMfix" : "ROMfix";
                    uint8_t mask = (t & 0x100) ? (uint8_t)(romBanks ? romBanks - 1 : 0) : 0x3F;
                    std::cout << "  w" << bank << " (cpu " << Hex(bank * 0x4000, 4) << "-" << Hex(bank * 0x4000 + 0x3FFF, 4) << ")"
                              << " pFFF7=" << Hex(fff7, 3) << " " << tname << ":" << page
                              << " -> phys " << (t & 0x100 ? "ROM" : "RAM") << ":" << (page & mask) << "\n";
                }
            }
            windowActive = false;  // stop the m1/bus capture at the derail
            for (uint8_t page : {0, 1, 2, 3, 4, 5, 7, 12, 15, 16, 17, 19, 24, 25, 31, 32})
            {
                std::string path = TestPathHelper::GetUniqueTestScratchPath("game2048x_p" + std::to_string((int)page) + ".bin");
                std::ofstream out(path, std::ios::binary);
                out.write((const char*)memory->RAMPageAddress(page), 0x4000);
                std::cout << "[DUMP] page " << (int)page << " -> " << path << "\n";
            }
            {
                Z80Disassembler disasm(context);
                auto disasmRange = [&](const char* tag, uint8_t page, uint16_t cpuFrom, uint16_t cpuTo, uint16_t cpuBase)
                {
                    const uint8_t* mem = memory->RAMPageAddress(page);
                    std::cout << "[DISASM " << tag << "] page " << (int)page << " cpu " << Hex(cpuFrom, 4) << ".." << Hex(cpuTo, 4) << ":\n";
                    uint32_t off = cpuFrom - cpuBase;
                    uint32_t end = cpuTo - cpuBase;
                    while (off < end)
                    {
                        std::vector<uint8_t> buf(mem + off, mem + off + 4);
                        uint8_t len = 0;
                        DecodedInstruction dec;
                        std::string m = disasm.disassembleSingleCommand(buf, (uint16_t)(cpuBase + off), &len, &dec);
                        if (len == 0)
                        {
                            len = 1;
                        }
                        std::cout << "  " << Hex(cpuBase + off, 4) << ": " << m << "\n";
                        off += len;
                    }
                };
                disasmRange("loop-head", 15, 0xF45C, 0xF4C0, 0xC000);
                disasmRange("page-walk", 15, 0xF820, 0xF870, 0xC000);
                disasmRange("copy-loop", 15, 0xE0C0, 0xE160, 0xC000);
                disasmRange("page0-module", 0, 0x4000, 0x4040, 0x4000);
            }
        }

        if (state.pFF77 != lastFF77 || state.p7FFD != last7FFD || state.pEFF7 != lastEFF7)
        {
            dumpState("CHANGE");
            lastFF77 = state.pFF77;
            last7FFD = state.p7FFD;
            lastEFF7 = state.pEFF7;
        }

        // Per-frame map + source-page probe across the game phase: the bank
        // tables (what the CPU actually reads/writes) vs pFFF7 (what the pager
        // was told), and page 0's first bytes (the packed-stream source)
        if (currentFrame >= 450 && currentFrame <= 470)
        {
            int regSet = (state.p7FFD & 0x10) ? 4 : 0;
            const uint8_t* p0 = memory->RAMPageAddress(0);
            std::cout << "[MAP] f" << currentFrame
                      << " tbl w0=" << (int)memory->GetPhysPageForZ80Address(0x0000)
                      << " w1=" << (int)memory->GetPhysPageForZ80Address(0x4000)
                      << " w2=" << (int)memory->GetPhysPageForZ80Address(0x8000)
                      << " w3=" << (int)memory->GetPhysPageForZ80Address(0xC000)
                      << " | pFFF7:";
            for (int b = 0; b < 4; b++)
            {
                unsigned fff7 = state.pFFF7[regSet + b];
                std::cout << " w" << b << "=" << ((fff7 & 0x100) ? "ROM" : "RAM") << (fff7 & 0xFF);
            }
            std::cout << " | p0[0..7]=";
            for (int k = 0; k < 8; k++) std::cout << Hex(p0[k]);
            std::cout << " p0nz=" << PageNonZero(memory, 0) << "\n";
        }

        if (!sawLoadingText && (state.pFF77 & 7) == 3 && currentFrame % 10 == 0)
        {
            std::string screen = DecodeZXRows(context, (state.p7FFD & 0x08) ? 7 : 5, 0, 24);
            if (screen.find("2048") != std::string::npos)
            {
                sawLoadingText = true;
                std::cout << "[LOADER] \"2048\" on screen at frame " << currentFrame << ":\n" << screen;
            }
        }
    }
    cpu->busTraceHook = nullptr;
    cpu->m1TraceHook = nullptr;
    windowActive = false;

    // ---- Phase F2: the game init loop under the microscope ---------------
    {
        // Every bus event of the window, minus the AY register writes of the
        // music ISR (pcs E330-E350) that repeat every frame
        std::cout << "[F2] full bus trace of the window (" << fullTrace.size() << " events, ISR AY writes hidden):\n";
        int shown = 0;
        int hidden = 0;
        for (const auto& e : fullTrace)
        {
            if ((e.port == 0xFFFD || e.port == 0xBFFD) && e.pc >= 0xE330 && e.pc <= 0xE350)
            {
                hidden++;
                continue;
            }
            std::cout << "  f" << e.frame << " " << e.type << " #" << Hex(e.port, 4)
                      << " val=" << Hex(e.value) << " pc=" << Hex(e.pc, 4) << "\n";
            if (++shown >= 3000)
            {
                std::cout << "  ... (" << fullTrace.size() << " total, " << hidden << " ISR writes hidden)\n";
                break;
            }
        }

        // Writes into the w1 window: the only gateway to pages 1/3/5/7 during
        // the game phase. Aggregated per frame: count, address span, nonzero
        // values and dominant writer pcs
        std::cout << "[F2] writes into w1 window 0x4000-0x7FFF during the window: " << w1Writes.size() << "\n";
        {
            std::map<int, std::tuple<int, uint16_t, uint16_t, int>> perFrame;  // frame -> (count, min, max, nonzeroVals)
            std::map<int, std::map<uint16_t, int>> pcPerFrame;
            for (const auto& e : w1Writes)
            {
                auto& agg = perFrame[e.frame];
                std::get<0>(agg)++;
                std::get<1>(agg) = std::get<1>(agg) == 0 ? e.port : std::min(std::get<1>(agg), e.port);
                std::get<2>(agg) = std::max(std::get<2>(agg), e.port);
                if (e.value != 0)
                {
                    std::get<3>(agg)++;
                }
                pcPerFrame[e.frame][e.pc]++;
            }
            for (const auto& kv : perFrame)
            {
                std::cout << "  f" << kv.first << ": " << std::get<0>(kv.second) << " writes, addr "
                          << Hex(std::get<1>(kv.second), 4) << ".." << Hex(std::get<2>(kv.second), 4)
                          << ", nonzero values: " << std::get<3>(kv.second) << "\n";
                // top writer pcs of this frame
                std::vector<std::pair<int, uint16_t>> top;
                for (const auto& p : pcPerFrame[kv.first])
                {
                    top.emplace_back(p.second, p.first);
                }
                std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                std::cout << "    top pcs:";
                for (size_t i = 0; i < top.size() && i < 6; i++)
                {
                    std::cout << " " << Hex(top[i].second, 4) << "x" << top[i].first;
                }
                std::cout << "\n";
            }
        }

        // Depacker source reads: where does `ld a,(de)` actually read from,
        // and what does it get back? Compare against the page dumps: page 0
        // holds the packed stream at 0x0000-0x1001
        auto summarizeEvents = [](const char* tag, const std::vector<FullEvent>& events)
        {
            std::cout << "[F2] " << tag << ": " << events.size() << " events\n";
            if (events.empty())
            {
                return;
            }
            int nonzero = 0;
            uint16_t minAddr = 0xFFFF, maxAddr = 0;
            std::map<int, int> perFrame;
            for (const auto& e : events)
            {
                if (e.value != 0)
                {
                    nonzero++;
                }
                minAddr = std::min(minAddr, e.port);
                maxAddr = std::max(maxAddr, e.port);
                perFrame[e.frame]++;
            }
            std::cout << "  addr " << Hex(minAddr, 4) << ".." << Hex(maxAddr, 4)
                      << ", nonzero values: " << nonzero << "\n";
            std::cout << "  per-frame:";
            for (const auto& kv : perFrame)
            {
                std::cout << " f" << kv.first << "=" << kv.second;
            }
            std::cout << "\n";
            std::cout << "  first 24: ";
            for (size_t i = 0; i < events.size() && i < 24; i++)
            {
                std::cout << "f" << events[i].frame << " " << events[i].type << Hex(events[i].port, 4)
                          << "=" << Hex(events[i].value) << "@" << Hex(events[i].pc, 4) << " ";
            }
            std::cout << "\n  last 8: ";
            for (size_t i = events.size() > 8 ? events.size() - 8 : 0; i < events.size(); i++)
            {
                std::cout << "f" << events[i].frame << " " << events[i].type << Hex(events[i].port, 4)
                          << "=" << Hex(events[i].value) << "@" << Hex(events[i].pc, 4) << " ";
            }
            std::cout << "\n";
        };
        summarizeEvents("depacker reads (pc E0C0-E16F)", depackReads);
        summarizeEvents("w0 window reads 0x0000-0x3FFF", w0Reads);
        summarizeEvents("w2 window writes 0x8000-0xBFFF", w2Writes);
        summarizeEvents("w2 window reads 0x8000-0xBFFF (code outside w2)", w2Reads);
        summarizeEvents("w3 window writes 0xC000-0xFFFF (code page!)", w3Writes);

        // w3 writes: per-frame detail with value samples (who rewrites page 15?)
        if (!w3Writes.empty())
        {
            std::cout << "[F2] w3 write detail per frame:\n";
            std::map<int, std::vector<const FullEvent*>> byFrame;
            for (const auto& e : w3Writes) byFrame[e.frame].push_back(&e);
            for (const auto& kv : byFrame)
            {
                uint16_t minA = 0xFFFF, maxA = 0;
                std::map<uint16_t, int> pcs;
                std::string sample;
                for (size_t i = 0; i < kv.second.size(); i++)
                {
                    const FullEvent* e = kv.second[i];
                    minA = std::min(minA, e->port);
                    maxA = std::max(maxA, e->port);
                    pcs[e->pc]++;
                    if (i < 12) sample += Hex(e->value);
                }
                std::cout << "  f" << kv.first << ": " << kv.second.size() << " writes "
                          << Hex(minA, 4) << ".." << Hex(maxA, 4) << " values~" << sample << " pcs:";
                int shown = 0;
                for (auto it = pcs.rbegin(); it != pcs.rend() && shown < 4; ++it, ++shown)
                {
                    std::cout << " " << Hex(it->first, 4) << "x" << it->second;
                }
                std::cout << "\n";
            }
        }

        // Bank-table snapshots after paging writes: divergence table vs pFFF7
        std::cout << "[F2] bank-table snapshots after paging OUTs (" << pageProbes.size() << "):\n";
        for (const auto& p : pageProbes)
        {
            bool mismatch = false;
            for (int b = 0; b < 4; b++)
            {
                if (p.w[b] != p.expected[b]) mismatch = true;
            }
            std::cout << "  f" << p.frame << " OUT #" << Hex(p.port, 4) << "=" << Hex(p.value)
                      << " tbl[" << (int)p.w[0] << "," << (int)p.w[1] << "," << (int)p.w[2] << "," << (int)p.w[3] << "]"
                      << " exp[" << (int)p.expected[0] << "," << (int)p.expected[1] << "," << (int)p.expected[2] << "," << (int)p.expected[3] << "]"
                      << (mismatch ? "  <<< MISMATCH" : "") << "\n";
        }

        std::cout << "[F2] instruction stream of the first loop iterations (" << m1Seq.size()
                  << " instructions, ISR range E330-E350 skipped):\n";
        {
            int shown = 0;
            int skipped = 0;
            for (const auto& e : m1Seq)
            {
                if (e.second >= 0xE330 && e.second <= 0xE350)
                {
                    skipped++;
                    continue;
                }
                std::cout << "  f" << e.first << " pc=" << Hex(e.second, 4) << "\n";
                if (++shown >= 1200)
                {
                    std::cout << "  ... (ISR instructions skipped so far: " << skipped << ")\n";
                    break;
                }
            }
            // Tail: the derail itself (last 1000 instructions of the window)
            std::cout << "[F2] tail of the instruction stream (last 1000, ISR skipped):\n";
            int tailShown = 0;
            for (auto it = m1Seq.rbegin(); it != m1Seq.rend() && tailShown < 1000; ++it)
            {
                if (it->second >= 0xE330 && it->second <= 0xE350)
                {
                    continue;
                }
                std::cout << "  f" << it->first << " pc=" << Hex(it->second, 4) << "\n";
                tailShown++;
            }
        }

        // What executes inside ROM:63 (mapped at 0x8000-0xBFFF by w2)
        std::map<uint16_t, int> rom63;
        for (const auto& e : m1Seq)
        {
            if (e.second >= 0x8000 && e.second < 0xC000)
            {
                rom63[e.second]++;
            }
        }
        std::cout << "[F2] ROM:63 execution: " << rom63.size() << " distinct pcs\n";
        {
            std::vector<std::pair<int, uint16_t>> top;
            for (const auto& kv : rom63)
            {
                top.emplace_back(kv.second, kv.first);
            }
            std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            for (size_t i = 0; i < top.size() && i < 25; i++)
            {
                std::cout << "  pc=" << Hex(top[i].second, 4) << " x" << top[i].first << "\n";
            }
        }
    }

    // ---- Phase F3: loader-phase forensics (dbg-3b) -----------------------
    // Probe the same image bytes after the run - did anything overwrite them?
    if (dataPhaseImageProbe)
    {
        DiskImage::Track* t0p = dataPhaseImageProbe->getTrackForCylinderAndSide(0, 0);
        (void)t0p;
        struct ImgProbe
        {
            int locator;
            const char* tag;
        };
        for (const auto& pr : {ImgProbe{271 + 0, "sec1"}, ImgProbe{271 + 17, "sec18"}, ImgProbe{271 + 39, "sec40"}, ImgProbe{271 + 60, "sec61"}})
        {
            DiskImage::Track* tr = dataPhaseImageProbe->getTrack(pr.locator / 16);
            DiskImage::Sector* se = tr ? tr->getSector(pr.locator % 16) : nullptr;
            std::cout << "[DISK-END] " << pr.tag << " (locator " << pr.locator << "): ";
            if (se)
            {
                for (int i = 0; i < 12; i++) std::cout << Hex(se->data[i]);
            }
            else
            {
                std::cout << "MISSING";
            }
            std::cout << "\n";
        }
    }
    {
        std::cout << "[F3] #7F payload sample, frames 385+ (first 400):\n";
        for (size_t i = 0; i < fdcDataSample.size() && i < 400; i++)
        {
            std::cout << Hex(fdcDataSample[i].second);
            if (i % 32 == 31) std::cout << "\n";
        }
        std::cout << "\n";
        // Where the ROM's read loop stored the payload during the data phase
        std::cout << "[F3] writes into 0x7000-0xB000, frames 380+ (" << bufferWrites.size() << "):\n";
        {
            std::map<int, std::tuple<int, uint16_t, uint16_t>> agg;  // frame -> count, min, max
            for (const auto& w : bufferWrites)
            {
                auto& a = agg[w.frame];
                if (std::get<0>(a) == 0)
                {
                    std::get<1>(a) = w.addr;
                    std::get<2>(a) = w.addr;
                }
                else
                {
                    std::get<1>(a) = std::min<uint16_t>(std::get<1>(a), w.addr);
                    std::get<2>(a) = std::max<uint16_t>(std::get<2>(a), w.addr);
                }
                std::get<0>(a)++;
            }
            for (const auto& kv : agg)
            {
                std::cout << "  f" << kv.first << ": " << std::get<0>(kv.second) << " writes "
                          << Hex(std::get<1>(kv.second), 4) << ".." << Hex(std::get<2>(kv.second), 4) << "\n";
            }
        }
        // The expander's actual input stream vs the SCL bytes at locator 288+
        std::cout << "[F3] expander input reads (first 160):\n  ";
        for (size_t i = 0; i < expanderReads.size() && i < 160; i++)
        {
            std::cout << Hex(expanderReads[i].value);
            if (i % 32 == 31) std::cout << "\n  ";
        }
        std::cout << "\n  addrs: " << Hex(expanderReads.front().addr, 4) << ".." << Hex(expanderReads.back().addr, 4)
                  << " pcs " << Hex(expanderReads.front().pc, 4) << "/" << Hex(expanderReads.back().pc, 4) << "\n";
        // Buffer-write tail with pcs: the exact instruction that stopped storing
        std::cout << "[F3] late buffer writes, tail 48 (of " << bufferWrites.size() << "):\n";
        for (size_t i = bufferWrites.size() > 48 ? bufferWrites.size() - 48 : 0; i < bufferWrites.size(); i++)
        {
            std::cout << "  f" << bufferWrites[i].frame << " " << Hex(bufferWrites[i].addr, 4)
                      << " pc=" << Hex(bufferWrites[i].pc, 4) << " = " << Hex(bufferWrites[i].value) << "\n";
        }
        // All memory writes f415+: did the stores move elsewhere or truly stop?
        std::cout << "[F3] ALL memory writes f415+ (" << lateWrites.size() << "), per frame/pc:\n";
        {
            std::map<int, std::map<uint16_t, std::tuple<int, uint16_t, uint16_t>>> agg;
            for (const auto& w : lateWrites)
            {
                auto& a = agg[w.frame][w.pc];
                if (std::get<0>(a) == 0)
                {
                    std::get<1>(a) = w.addr;
                    std::get<2>(a) = w.addr;
                }
                else
                {
                    std::get<1>(a) = std::min<uint16_t>(std::get<1>(a), w.addr);
                    std::get<2>(a) = std::max<uint16_t>(std::get<2>(a), w.addr);
                }
                std::get<0>(a)++;
            }
            for (const auto& fkv : agg)
            {
                std::cout << "  f" << fkv.first << ":";
                for (const auto& pkv : fkv.second)
                {
                    std::cout << " [" << Hex(pkv.first, 4) << " x" << std::get<0>(pkv.second)
                              << " " << Hex(std::get<1>(pkv.second), 4) << ".." << Hex(std::get<2>(pkv.second), 4) << "]";
                }
                std::cout << "\n";
            }
        }
        std::cout << "[F3] IN #7F pcs, frames 416+:\n";
        for (const auto& kv : lateDataInPcs)
        {
            std::cout << "  pc " << Hex(kv.first, 4) << " x" << kv.second << "\n";
        }
        // ---- Phase F4: runtime port-gate forensics (dbg-3g) -----------------
        std::cout << "[F4] xx77/xF7 OUTs, frames 438+ (raw first 40 of " << gateWrites.size() << "):\n";
        for (size_t i = 0; i < gateWrites.size() && i < 40; i++)
        {
            const auto& e = gateWrites[i];
            std::cout << "  f" << e.frame << " OUT #" << Hex(e.port, 4) << "=" << Hex(e.value)
                      << " @pc=" << Hex(e.pc, 4)
                      << " -> aFF77=" << Hex(e.aFF77, 4) << " pFF77=" << Hex(e.pFF77)
                      << " fl=" << Hex(e.flags) << "\n";
        }
        {
            std::map<std::tuple<uint16_t, uint8_t, uint16_t>, std::pair<int, int>> agg;
            for (const auto& e : gateWrites)
            {
                auto k = std::make_tuple(e.port, e.value, e.pc);
                auto& a = agg[k];
                if (a.second == 0)
                {
                    a.first = e.frame;
                }
                a.second = e.frame;
            }
            std::cout << "[F4] xx77/xF7 OUTs aggregated (port,value,pc -> frames):\n";
            for (const auto& kv : agg)
            {
                std::cout << "  #" << Hex(std::get<0>(kv.first), 4) << "=" << Hex(std::get<1>(kv.first))
                          << " @pc=" << Hex(std::get<2>(kv.first), 4)
                          << " -> f" << kv.second.first << ".." << kv.second.second << "\n";
            }
        }
        std::cout << "[F4] #xxFF reads (raw first 30 of " << ffReads.size() << "):\n";
        for (size_t i = 0; i < ffReads.size() && i < 30; i++)
        {
            std::cout << "  f" << ffReads[i].frame << " IN #" << Hex(ffReads[i].port, 4)
                      << "=" << Hex(ffReads[i].value) << " @pc=" << Hex(ffReads[i].pc, 4) << "\n";
        }
        {
            std::map<std::pair<uint16_t, uint8_t>, int> ragg;
            for (const auto& e : ffReads)
            {
                ragg[{e.pc, e.value}]++;
            }
            std::cout << "[F4] #xxFF reads aggregated (pc,value -> count):\n";
            for (const auto& kv : ragg)
            {
                std::cout << "  @pc=" << Hex(kv.first.first, 4) << " val=" << Hex(kv.first.second)
                          << " x" << kv.second << "\n";
            }
        }
        std::cout << "[F4] final: aFF77=" << Hex(state.aFF77, 4) << " pFF77=" << Hex(state.pFF77)
                  << " flags=" << Hex((int)state.flags, 2)
                  << " (pFF77&7=mode " << (state.pFF77 & 7) << ")\n";
        std::cout << "[F4] runtime pc>>8 histogram, frames 455-525 (" << runtimeSamples << " samples):\n";
        for (const auto& kv : runtimePcPages)
        {
            std::cout << "  pc$" << Hex(kv.first << 8, 4) << "+ x" << kv.second << "\n";
        }
        std::cout << "[F4] $FF00-$FFFF variable writes, frames 453+ (" << ffVarWrites.size() << "):\n";
        for (size_t i = 0; i < ffVarWrites.size() && i < 60; i++)
        {
            std::cout << "  f" << ffVarWrites[i].frame << " #" << Hex(ffVarWrites[i].addr, 4) << "=" << Hex(ffVarWrites[i].value)
                      << " @pc=" << Hex(ffVarWrites[i].pc, 4) << "\n";
        }
        if (ffVarWrites.size() > 60)
        {
            std::cout << "  ... tail:\n";
            for (size_t i = ffVarWrites.size() > 12 ? ffVarWrites.size() - 12 : 0; i < ffVarWrites.size(); i++)
            {
                std::cout << "  f" << ffVarWrites[i].frame << " #" << Hex(ffVarWrites[i].addr, 4) << "=" << Hex(ffVarWrites[i].value)
                          << " @pc=" << Hex(ffVarWrites[i].pc, 4) << "\n";
            }
        }
        std::cout << "[F5] per-address M1 top-60, frames 466-472:\n";
        {
            std::vector<std::pair<long long, uint16_t>> top;
            for (const auto& kv : addrHist) top.push_back({kv.second, kv.first});
            std::sort(top.begin(), top.end(), std::greater<>());
            for (size_t k = 0; k < top.size() && k < 60; k++)
                std::cout << "  $" << Hex(top[k].second, 4) << " x" << top[k].first << "\n";
        }
        std::cout << "[F5] CPU end state: pc=$" << Hex(cpu->pc, 4) << " sp=$" << Hex(cpu->sp, 4)
                  << " i=$" << Hex(cpu->i, 2) << " im=" << (int)cpu->im
                  << " iff1=" << (int)cpu->iff1 << " iff2=" << (int)cpu->iff2
                  << " halted=" << (int)cpu->halted << " int_pending=" << (int)cpu->int_pending << "\n";
        std::cout << "[F5] p15 writes landing outside $FF00+, f452+ (" << p15Writes.size() << "):\n";
        {
            std::map<int, long long> perFrame;
            for (const auto& w : p15Writes) perFrame[w.frame]++;
            std::cout << "  per-frame totals:";
            for (const auto& kv : perFrame) std::cout << " f" << kv.first << ":" << kv.second;
            std::cout << "\n";
        }
        std::cout << "[F5] p15 ENTRY-CODE writes $E000-$E300 (" << p15CodeWrites.size() << "), full detail:\n";
        for (size_t i = 0; i < p15CodeWrites.size() && i < 90; i++)
        {
            std::cout << "  f" << p15CodeWrites[i].frame << " #$" << Hex(p15CodeWrites[i].addr, 4)
                      << "=" << Hex(p15CodeWrites[i].value) << " @pc=" << Hex(p15CodeWrites[i].pc, 4) << "\n";
        }
        if (p15CodeWrites.size() > 90)
        {
            std::map<int, std::map<uint16_t, int>> agg;
            for (const auto& w : p15CodeWrites) agg[w.frame][w.pc]++;
            std::cout << "  ... aggregate by frame/pc:\n";
            for (const auto& fkv : agg)
            {
                std::cout << "    f" << fkv.first << ":";
                for (const auto& pkv : fkv.second)
                    std::cout << " @$" << Hex(pkv.first, 4) << " x" << pkv.second;
                std::cout << "\n";
            }
        }
        std::cout << "[F5] p15 code $E040-$E0B0 at the $E07D fetch (loader-done ref in second half):\n";
        for (int row = 0; row < 7; row++)
        {
            int off = 0x2040 + row * 16;
            std::cout << "  $" << Hex(0xE040 + row * 16, 4) << ": ";
            for (int i = 0; i < 16; i++)
            {
                uint8_t cur = (off - 0x2000 < (int)p15CodeAtCall.size())
                                  ? p15CodeAtCall[off - 0x2000] : 0;
                std::cout << Hex(cur);
            }
            std::cout << "  | ";
            for (int i = 0; i < 16; i++) std::cout << Hex(loaderDonePage15[off + i]);
            std::cout << "\n";
        }
        std::cout << "[F5] first-seen frames (>=460) of key pcs:\n";
        for (uint16_t key : {0xE07D, 0xE182, 0xE196, 0xE080, 0xE08B, 0xE08C, 0xFDFD, 0xF373})
        {
            auto it = firstSeen.find(key);
            std::cout << "  $" << Hex(key, 4) << ": " << (it != firstSeen.end() ? std::to_string(it->second) : "never") << "\n";
        }
        std::cout << "[F5] M1 sequence from the $E07D CALL (" << gameSeq.size() << " events, RLE):\n";
        {
            size_t i = 0;
            int runs = 0;
            while (i < gameSeq.size() && runs < 200)
            {
                size_t j = i;
                while (j < gameSeq.size() && gameSeq[j].second == gameSeq[i].second) j++;
                std::cout << "  f" << gameSeq[i].first;
                if (gameSeq[j - 1].first != gameSeq[i].first)
                    std::cout << "-" << gameSeq[j - 1].first;
                std::cout << " $" << Hex(gameSeq[i].second, 4) << " x" << (j - i) << "\n";
                i = j;
                runs++;
            }
        }
        // Snapshot the expander outputs for offline comparison against a
        // reference expansion of the same SCL input bytes
        {
            auto dump = [](const char* name, const std::vector<uint8_t>& bytes) {
                std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
                std::ofstream out(path, std::ios::binary);
                out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
                std::cout << "[F3] dumped " << name << " -> " << path << "\n";
            };
            dump("game2048x_p0_loaderdone.bin", loaderDonePage0);
            dump("game2048x_p12_loaderdone.bin", loaderDonePage12);
            dump("game2048x_p15_loaderdone.bin", loaderDonePage15);
        }
    }
    // only 0x1002 bytes reached page 0. A READ SECTOR command (0x80) returns
    // 256 payload bytes (#7F reads) before the next command; deltas < 256 or
    // a stop in the command stream localize the failure to the FDC, while a
    // full command stream + early page-0 write stop localizes it to the
    // loader's relocation code
    {
        std::cout << "[F3] FDC commands: " << fdcCmds.size() << ", payload (#7F) reads: " << fdcDataIns
                  << " (all-sectors expectation ~" << ((9 + 255 + 61) * 256) << ")\n";
        long prevData = 0;
        int shown = 0;
        for (const auto& c : fdcCmds)
        {
            long delta = c.dataIns - prevData;
            prevData = c.dataIns;
            std::cout << "  f" << c.frame << " cmd=" << Hex(c.cmd)
                      << " tr=" << Hex(c.track) << " sec=" << Hex(c.sector)
                      << " sys=" << Hex(c.sys) << " pc=" << Hex(c.pc, 4)
                      << " data=+" << delta << "\n";
            if (++shown >= 1500)
            {
                std::cout << "  ... (" << fdcCmds.size() << " commands total)\n";
                break;
            }
        }
        std::cout << "[F3] FDC payload reads per frame:";
        for (const auto& kv : fdcDataPerFrame)
        {
            std::cout << " f" << kv.first << "=" << kv.second;
        }
        std::cout << "\n";
        {
            long polls = 0;
            for (const auto& kv : fdcStatusPolls)
            {
                polls += kv.second;
            }
            std::cout << "[F3] FDC status (#1F) polls by pc (total " << polls << "):\n";
            for (const auto& kv : fdcStatusPolls)
            {
                std::cout << "  pc=" << Hex(kv.first, 4) << " x" << kv.second << "\n";
            }
        }

        // FDC register I/O around the last payload-bearing frame
        int lastDataFrame = -1;
        for (const auto& kv : fdcDataPerFrame)
        {
            if (kv.second)
            {
                lastDataFrame = kv.first;
            }
        }
        if (lastDataFrame >= 0)
        {
            std::cout << "[F3] FDC register I/O from frame " << (lastDataFrame - 1) << " on (cap 250):\n";
            int shownRaw = 0;
            for (const auto& e : fdcRaw)
            {
                if (e.frame + 1 >= lastDataFrame)
                {
                    std::cout << "  f" << e.frame << " " << e.type << " #" << Hex(e.port, 4)
                              << "=" << Hex(e.value) << " @" << Hex(e.pc, 4) << "\n";
                    if (++shownRaw >= 250)
                    {
                        break;
                    }
                }
            }
        }

        // Page-0 delivery: per-frame span, writer pcs and FDC correlation
        std::map<int, std::tuple<int, uint16_t, uint16_t, int>> page0PerFrame;  // frame -> count, min, max, nonzero
        for (const auto& w : page0Writes)
        {
            auto& a = page0PerFrame[w.frame];
            if (std::get<0>(a) == 0)
            {
                std::get<1>(a) = w.addr;
                std::get<2>(a) = w.addr;
            }
            else
            {
                std::get<1>(a) = std::min<uint16_t>(std::get<1>(a), w.addr);
                std::get<2>(a) = std::max<uint16_t>(std::get<2>(a), w.addr);
            }
            std::get<0>(a)++;
            if (w.value != 0)
            {
                std::get<3>(a)++;
            }
        }
        std::cout << "[F3] page-0 writes per frame (" << page0Writes.size() << " writes):\n";
        for (const auto& kv : page0PerFrame)
        {
            std::cout << "  f" << kv.first << ": " << std::get<0>(kv.second) << " writes "
                      << Hex(std::get<1>(kv.second), 4) << ".." << Hex(std::get<2>(kv.second), 4)
                      << ", nonzero " << std::get<3>(kv.second)
                      << ", fdc payload this frame: " << fdcDataPerFrame[kv.first] << "\n";
            auto it = page0Writers.find(kv.first);
            if (it != page0Writers.end())
            {
                std::vector<std::pair<int, uint16_t>> top;
                for (const auto& p : it->second)
                {
                    top.emplace_back(p.second, p.first);
                }
                std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                std::cout << "    writers:";
                for (size_t i = 0; i < top.size() && i < 6; i++)
                {
                    std::cout << " " << Hex(top[i].second, 4) << "x" << top[i].first;
                }
                std::cout << "\n";
            }
        }
        if (!page0Writes.empty())
        {
            int lastFrame = page0Writes.back().frame;
            std::cout << "[F3] last page-0 write at frame " << lastFrame
                      << " (addr " << Hex(page0Writes.back().addr, 4)
                      << ", pc " << Hex(page0Writes.back().pc, 4) << ")\n";
            std::cout << "[F3] tail of the page-0 delivery (last 30 writes):\n";
            for (size_t i = page0Writes.size() > 30 ? page0Writes.size() - 30 : 0; i < page0Writes.size(); i++)
            {
                const auto& w = page0Writes[i];
                std::cout << "  f" << w.frame << " #" << Hex(w.addr, 4) << "=" << Hex(w.value)
                          << " @" << Hex(w.pc, 4) << "\n";
            }
            // What still executed in the frames right after the stop: the
            // loader code's last-known activity tells whether it errored out,
            // retried or moved on to the next stage
            std::cout << "[F3] pcs alive at/after frame " << lastFrame << " (top 40 by last frame seen):\n";
            std::vector<std::tuple<int, uint16_t, int, long long>> alive;  // last, pc, first, count
            for (const auto& kv : loaderM1)
            {
                if (std::get<1>(kv.second) >= lastFrame)
                {
                    alive.emplace_back(std::get<1>(kv.second), kv.first, std::get<0>(kv.second), std::get<2>(kv.second));
                }
            }
            std::sort(alive.begin(), alive.end(), [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });
            for (size_t i = 0; i < alive.size() && i < 40; i++)
            {
                std::cout << "  pc=" << Hex(std::get<1>(alive[i]), 4)
                          << " frames " << std::get<2>(alive[i]) << ".." << std::get<0>(alive[i])
                          << " x" << std::get<3>(alive[i]) << "\n";
            }
        }

        // Loader code map: contiguous clusters of executed pcs
        std::cout << "[F3] loader m1 clusters (gap > 32):\n";
        {
            uint16_t prev = 0;
            int start = -1;
            int startF = 0;
            int lastF = 0;
            long long cnt = 0;
            auto flush = [&]()
            {
                if (start >= 0)
                {
                    std::cout << "  [" << Hex((uint16_t)start, 4) << "-" << Hex(prev, 4) << "]"
                              << " frames " << startF << ".." << lastF << " x" << cnt << "\n";
                }
            };
            for (const auto& kv : loaderM1)
            {
                uint16_t pc = kv.first;
                if (start < 0 || pc > (uint16_t)(prev + 32))
                {
                    flush();
                    start = pc;
                    startF = std::get<0>(kv.second);
                    cnt = 0;
                }
                lastF = std::get<1>(kv.second);
                cnt += std::get<2>(kv.second);
                prev = pc;
            }
            flush();
        }
    }

    // ---- Phase G0: exact instruction stream of the stuck state ----------
    // 30 frames of per-instruction PC histogram - shows the real loop(s)
    {
        std::map<uint16_t, int> m1Counts;
        cpu->m1TraceHook = [&m1Counts](uint16_t pc) { m1Counts[pc]++; };
        for (int i = 0; i < 30; i++)
        {
            emulator->RunFrame(true);
        }
        cpu->m1TraceHook = nullptr;
        long long total = 0;
        for (const auto& kv : m1Counts)
        {
            total += kv.second;
        }
        std::cout << "[G0] " << total << " instructions / 30 frames, " << m1Counts.size() << " distinct pcs. Top 30:\n";
        std::vector<std::pair<int, uint16_t>> top1;
        for (const auto& kv : m1Counts)
        {
            top1.emplace_back(kv.second, kv.first);
        }
        std::sort(top1.begin(), top1.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < top1.size() && i < 30; i++)
        {
            std::cout << "  pc=" << Hex(top1[i].second, 4) << " x" << top1[i].first << "\n";
        }
        // Region map of the executed pcs: contiguous clusters
        std::cout << "[G0] executed pc clusters:";
        uint16_t prev = 0;
        int clusterStart = -1;
        for (const auto& kv : m1Counts)
        {
            uint16_t pc = kv.first;
            if (clusterStart < 0 || pc > (uint16_t)(prev + 32))
            {
                if (clusterStart >= 0)
                {
                    std::cout << " [" << Hex((uint16_t)clusterStart, 4) << "-" << Hex(prev, 4) << "]";
                }
                clusterStart = pc;
            }
            prev = pc;
        }
        if (clusterStart >= 0)
        {
            std::cout << " [" << Hex((uint16_t)clusterStart, 4) << "-" << Hex(prev, 4) << "]";
        }
        std::cout << "\n";
    }

    // Non-zero regions per content page (where code/data actually sits)
    for (uint8_t page : {0, 2, 7, 12, 15})
    {
        const uint8_t* mem = memory->RAMPageAddress(page);
        int first = -1, last = -1;
        for (uint32_t i = 0; i < 0x4000; i++)
        {
            if (mem[i] != 0)
            {
                if (first < 0)
                {
                    first = i;
                }
                last = i;
            }
        }
        std::cout << "[G0] page " << (int)page << " non-zero: " << (first < 0 ? std::string("none") : Hex(first, 4) + ".." + Hex(last, 4)) << "\n";
    }

    // ---- Phase G1: content dumps + disassembly of the init-loop code -----
    for (uint8_t page : {0, 1, 2, 3, 4, 5, 7, 12, 15, 16, 17, 19, 24, 25, 31, 32})
    {
        std::string path = TestPathHelper::GetUniqueTestScratchPath("game2048_p" + std::to_string((int)page) + ".bin");
        std::ofstream out(path, std::ios::binary);
        out.write((const char*)memory->RAMPageAddress(page), 0x4000);
        std::cout << "[DUMP] page " << (int)page << " -> " << path << "\n";
    }
    {
        Z80Disassembler disasm(context);
        auto disasmRange = [&](const char* tag, uint8_t page, uint16_t cpuFrom, uint16_t cpuTo, uint16_t cpuBase)
        {
            const uint8_t* mem = memory->RAMPageAddress(page);
            std::cout << "[DISASM " << tag << "] page " << (int)page << " cpu " << Hex(cpuFrom, 4) << ".." << Hex(cpuTo, 4) << ":\n";
            uint32_t off = cpuFrom - cpuBase;
            uint32_t end = cpuTo - cpuBase;
            while (off < end)
            {
                std::vector<uint8_t> buf(mem + off, mem + off + 4);
                uint8_t len = 0;
                DecodedInstruction dec;
                std::string m = disasm.disassembleSingleCommand(buf, (uint16_t)(cpuBase + off), &len, &dec);
                if (len == 0)
                {
                    len = 1;
                }
                std::cout << "  " << Hex(cpuBase + off, 4) << ": " << m << "\n";
                off += len;
            }
        };
        // page 15 is mapped at window 3 (0xC000): game init + loop code.
        // F45C/F481/F842 are known instruction boundaries from the m1 trace
        disasmRange("game-entry", 15, 0xE000, 0xE057, 0xC000);
        disasmRange("loop-head", 15, 0xF45C, 0xF4C0, 0xC000);
        disasmRange("page-walk", 15, 0xF842, 0xF8A0, 0xC000);
        disasmRange("post-loop", 15, 0xE070, 0xE0D0, 0xC000);
        // page 0 module entry: mapped at w1 (0x4000) when the game calls it
        disasmRange("page0-module", 0, 0x4000, 0x4040, 0x4000);
        disasmRange("page0-488c", 0, 0x488C, 0x48C0, 0x4000);
    }

    // ---- Phase G: final analysis -----------------------------------------
    dumpState("FINAL");
    std::cout << "[G] per-page non-zero counts:";
    for (uint8_t page = 0; page < 16; page++)
    {
        std::cout << " p" << (int)page << "=" << PageNonZero(memory, page);
    }
    std::cout << "\n";

    std::cout << "[G] top PC samples:\n";
    std::vector<std::pair<int, uint16_t>> top;
    for (const auto& kv : pcHistogram)
    {
        top.emplace_back(kv.second, kv.first);
    }
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < top.size() && i < 12; i++)
    {
        std::cout << "  pc=" << Hex(top[i].second, 4) << " x" << top[i].first << "\n";
    }

    // xx77 writes are rare and decisive for video/memory config
    std::cout << "[G] xx77/7FFD OUT trace (first 600):\n";
    int shown = 0;
    for (const auto& e : portTrace)
    {
        if ((e.port & 0x00FF) == 0xFF)
        {
            continue;  // Beta128 activity is dense; see FDC stats below
        }
        std::cout << "  frame=" << e.frame << " OUT #" << Hex(e.port, 4) << " val=" << Hex(e.value)
                  << " pc=" << Hex(e.pc, 4) << "\n";
        if (++shown >= 600)
        {
            break;
        }
    }
    int ffOuts = 0;
    for (const auto& e : portTrace)
    {
        if ((e.port & 0x00FF) == 0xFF)
        {
            ffOuts++;
        }
    }
    std::cout << "[G] Beta128 #FF OUTs: " << ffOuts << " (total trace " << portTrace.size() << ")\n";

    // What the game phase READS (poll loops / raster sync would show up as a
    // dense (pc, port) aggregate)
    std::cout << "[G] IN aggregates after frame 460 (" << inTrace.size() << " captured):\n";
    {
        std::map<std::string, int> inAgg;
        for (const auto& e : inTrace)
        {
            char key[64];
            snprintf(key, sizeof(key), "pc=%04X port=%04X val=%02X", e.pc, e.port, e.value);
            inAgg[key]++;
        }
        std::vector<std::pair<int, std::string>> top;
        for (const auto& kv : inAgg)
        {
            top.emplace_back(kv.second, kv.first);
        }
        std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < top.size() && i < 15; i++)
        {
            std::cout << "  " << top[i].second << "  x" << top[i].first << "\n";
        }
    }

    // CPU state + the four window mappings (pFFF7: [9:8] = type, [7:0] = page)
    {
        unsigned regSet = (state.p7FFD & 0x10) ? 4 : 0;
        std::cout << "[G] cpu: pc=" << Hex(cpu->pc, 4) << " sp=" << Hex(cpu->sp, 4)
                  << " af=" << Hex(cpu->af, 4) << " bc=" << Hex(cpu->bc, 4)
                  << " de=" << Hex(cpu->de, 4) << " hl=" << Hex(cpu->hl, 4)
                  << " im=" << (int)cpu->im << " iff1=" << (int)cpu->iff1
                  << " | windows (7FFD.4=" << (int)((state.p7FFD & 0x10) ? 1 : 0) << "):";
        for (unsigned w = 0; w < 4; w++)
        {
            unsigned fff7 = state.pFFF7[regSet + w];
            std::cout << " w" << w << "=" << ((fff7 & 0x300) == 0x200 ? "RAM" : (fff7 & 0x300) == 0x100 ? "ROM7FFD" : (fff7 & 0x300) == 0x000 ? "RAM7FFD" : "ROM?") << ":" << (fff7 & 0xFF);
        }
        std::cout << "\n";
    }

    // ---- Assertions -------------------------------------------------------
    // Black-screen regression (ATM710 1024K, 2048.scl): on entry the game
    // switches FF77 video mode 3 -> 0 (OUT (#BD77),#A8 at $E076). The retired
    // atmMemSwap() physically permuted A5-A7<->A8-A10 across ALL RAM on every
    // pFF77 bit0 transition, so that OUT scrambled the freshly loaded code
    // one instruction later - the $E07D CALL $E182 fetched garbage, the CPU
    // derailed into a NOP slide, the ISR died and the screen stayed black.
    // Every assertion below fails on that build.
    EXPECT_FALSE(derailReported) << "CPU derailed into the w1 window";
    EXPECT_FALSE(nopSlideReported) << "CPU walked a NOP slide";

    // Entry code intact: $E07D..$E07F = CALL $E182, $E080 = LD HL,$00C0
    // (p15 file offsets 0x207D..0x2080 - exactly the bytes atmMemSwap
    // scrambled inside the port handler, before the next instruction fetch)
    const uint8_t* p15Code = memory->RAMPageAddress(15);
    EXPECT_EQ(p15Code[0x207D], 0xCD) << "$E07D CALL opcode overwritten";
    EXPECT_EQ(p15Code[0x207E], 0x82) << "$E07D CALL lo operand overwritten";
    EXPECT_EQ(p15Code[0x207F], 0xE1) << "$E07D CALL hi operand overwritten";
    EXPECT_EQ(p15Code[0x2080], 0x21) << "$E080 main-loop LD HL opcode overwritten";

    // The game reached its runtime state: EGA mode selected and the module
    // windows w0/w3 = RAM:12/15 (reprogrammed every frame by the $E080 main
    // loop after the $E08B HALT wakes)
    int regSet = (state.p7FFD & 0x10) ? 4 : 0;
    EXPECT_EQ(state.pFF77, 0xA8) << "game never switched to its EGA video mode";
    EXPECT_EQ(state.pFFF7[regSet], 0x0200 | 12) << "w0 is not the RAM:12 module page";
    EXPECT_EQ(state.pFFF7[regSet + 3], 0x0200 | 15) << "w3 is not the RAM:15 kernel page";

    // ISR alive: interrupts enabled and the $FF63 frame ticker advanced
    // through the tail frames
    EXPECT_EQ(cpu->iff1, 1) << "interrupts disabled at the end";
    EXPECT_GE(tickerIncrements, 10) << "ISR frame ticker stopped after game entry";

    // And the reported symptom: the game screen must carry content in the
    // active mode's video pages (the board renders into the EGA planes)
    uint8_t videoPage = (state.p7FFD & 0x08) ? 7 : 5;
    int content = PageNonZero(memory, videoPage) + PageNonZero(memory, videoPage - 4);
    EXPECT_GT(content, 1000) << "Screen is black: active video pages carry no content";
}
