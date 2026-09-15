#include "tsfmplayerharness.h"

#include <algorithm>
#include <cstring>

#include "emulatortesthelper.h"
#include "testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

namespace
{
/// TAP block header (17 bytes after the 0x00 flag): type, name[10], length, start, param2
struct TapHeader
{
    uint8_t type = 0;
    char name[11] = {};
    uint16_t length = 0;
    uint16_t start = 0;
    uint16_t param2 = 0;
};

/// Minimal .TAP walker: [len:u16][flag][body...][xor]. Returns header/data pairs.
/// Any malformed length aborts the walk (matches how the tape subsystem stops).
bool ParseTap(const std::vector<uint8_t>& tap, std::vector<std::pair<TapHeader, std::vector<uint8_t>>>& blocks)
{
    size_t pos = 0;
    TapHeader pending;
    bool hasPending = false;

    while (pos + 2 <= tap.size())
    {
        uint16_t blockLength = static_cast<uint16_t>(tap[pos] | (tap[pos + 1] << 8));
        if (blockLength == 0 || pos + 2 + blockLength > tap.size())
            break;

        const uint8_t* block = tap.data() + pos + 2;
        uint8_t flag = block[0];

        if (flag == 0x00 && blockLength == 19)  // flag + 17 header bytes + checksum
        {
            const uint8_t* body = block + 1;
            pending.type = body[0];
            std::memcpy(pending.name, body + 1, 10);
            pending.name[10] = '\0';
            pending.length = static_cast<uint16_t>(body[11] | (body[12] << 8));
            pending.start = static_cast<uint16_t>(body[13] | (body[14] << 8));
            pending.param2 = static_cast<uint16_t>(body[15] | (body[16] << 8));
            hasPending = true;
        }
        else if (flag == 0xFF && hasPending)
        {
            // Data block: body without the trailing checksum byte
            blocks.emplace_back(pending, std::vector<uint8_t>(block + 1, block + blockLength - 1));
            hasPending = false;
        }

        pos += 2 + blockLength;
    }

    return !blocks.empty();
}
}  // namespace

bool TsfmPlayerHarness::Setup(Emulator* emulator, size_t tuneIndex, std::string* errorMessage)
{
    auto fail = [errorMessage](const char* message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    _emulator = emulator;
    _z80 = emulator ? emulator->GetContext()->pCore->GetZ80() : nullptr;
    if (!_z80)
        return fail("emulator or Z80 unavailable");

    // Load and parse the collection TAP
    std::vector<uint8_t> tap;
    {
        FILE* file = fopen(TestPathHelper::GetTestDataPath("sound/tsfm/TSFM-EL.TAP").c_str(), "rb");
        if (!file)
            return fail("TSFM-EL.TAP not found under testdata/sound/tsfm/");
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        tap.resize(static_cast<size_t>(size));
        size_t read = fread(tap.data(), 1, tap.size(), file);
        fclose(file);
        if (read != tap.size())
            return fail("failed to read TSFM-EL.TAP");
    }

    std::vector<std::pair<TapHeader, std::vector<uint8_t>>> blocks;
    if (!ParseTap(tap, blocks))
        return fail("TSFM-EL.TAP contains no loadable blocks");

    // Pick the player, the loader block and the tune by header name / load address
    const std::vector<uint8_t>* player = nullptr;
    const std::vector<uint8_t>* lnxdata = nullptr;
    const std::vector<uint8_t>* tune = nullptr;
    size_t tuneSeen = 0;

    for (const auto& [header, data] : blocks)
    {
        if (header.type == 3 && header.start == PLAYER_BASE && data.size() >= 1883)
        {
            player = &data;
        }
        else if (header.type == 3 && header.start == LNXDATA_BASE)
        {
            lnxdata = &data;
        }
        else if (header.type == 3 && header.start == TUNE_BASE)
        {
            // Tunes carry the "TFMcom1.12" signature; the final "MMM" block is a
            // collection terminator, not a tune
            if (data.size() >= 10 && std::memcmp(data.data(), "TFMcom1.12", 10) == 0)
            {
                if (tuneSeen == tuneIndex)
                {
                    tune = &data;
                    // Block names are space padded
                    _tuneName = header.name;
                    _tuneName.erase(_tuneName.find_last_not_of(' ') + 1);
                    break;
                }
                tuneSeen++;
            }
        }
    }

    if (!player)
        return fail("'_tsfmplaye' player block not found at 25000");
    if (!lnxdata)
        return fail("'lnxdata' block not found at 31000");
    if (!tune)
        return fail("no TFMcom1.12 tune block found at 32768");

    // Hardware load order: player, lnxdata, tune. The tune (0x8000) overwrites
    // the lnxdata tail, leaving memory exactly as the MB03+ loader does
    PokeBlock(PLAYER_BASE, *player);
    PokeBlock(LNXDATA_BASE, *lnxdata);
    PokeBlock(TUNE_BASE, *tune);

    // IM1 interrupt stub (EI; RET) at 0x0038. The harness machine carries no
    // system ROM ISR - on hardware the player's `ei; halt` main loop is
    // serviced by the standard ROM interrupt handler. Without the stub the
    // first interrupt after init jumps into empty RAM and the CPU is lost
    // (observed: pc wandering in low memory, no further TS writes)
    _z80->DirectWrite(0x0038, 0xFB);
    _z80->DirectWrite(0x0039, 0xC9);

    // Enter the player's own main loop: init(HL = tune) once, then one frame
    // routine per interrupt. IM1 + disabled interrupts: the player's first
    // instruction sequence ends in `ei` before the `halt` loop
    _z80->pc = PLAYER_BASE;
    _z80->hl = TUNE_BASE;
    _z80->sp = 0xFF00;
    _z80->im = 1;
    _z80->iff1 = 0;
    _z80->iff2 = 0;
    _z80->halted = 0;

    // Track TS port writes via the bus trace hook ('O' = port write)
    _previousHook = _z80->busTraceHook;
    _hookInstalled = true;
    TsfmPlayerHarness* self = this;
    _z80->busTraceHook = [self](char type, uint16_t port, uint8_t value)
    {
        if (type == 'O')
            self->OnPortWrite(port, value);
    };

    _writes.clear();
    _perFrameCounts.clear();
    _chip0Writes = _chip1Writes = 0;
    std::memset(_controlWordCounts, 0, sizeof(_controlWordCounts));
    _framesSinceLastWrite = 0;
    _frameStartWriteCount = 0;

    return true;
}

void TsfmPlayerHarness::RunFrames(int frameCount)
{
    for (int i = 0; i < frameCount; i++)
    {
        EmulatorTestHelper::RunFramesFast(_emulator, 1);

        uint32_t frameWrites = static_cast<uint32_t>(_writes.size() - _frameStartWriteCount);
        _frameStartWriteCount = _writes.size();
        _perFrameCounts.push_back(frameWrites);

        // Frame 0 hosts init; give the player one frame of grace before the
        // "no output" counter starts growing
        if (frameWrites > 0 || _perFrameCounts.size() == 1)
            _framesSinceLastWrite = 0;
        else
            _framesSinceLastWrite++;
    }
}

void TsfmPlayerHarness::Detach()
{
    if (_hookInstalled && _z80)
        _z80->busTraceHook = _previousHook;
    _hookInstalled = false;
    _z80 = nullptr;
    _emulator = nullptr;
}

uint64_t TsfmPlayerHarness::GetControlWordCount(uint8_t controlWord) const
{
    if (controlWord < 0xF8)
        return 0;
    return _controlWordCounts[controlWord & 7];
}

uint64_t TsfmPlayerHarness::GetTrafficHash() const
{
    // FNV-1a over (port, value) pairs
    uint64_t hash = 1469598103934665603ULL;
    for (const PortWrite& write : _writes)
    {
        hash ^= write.port & 0xFF;
        hash *= 1099511628211ULL;
        hash ^= write.port >> 8;
        hash *= 1099511628211ULL;
        hash ^= write.value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void TsfmPlayerHarness::PokeBlock(uint16_t address, const std::vector<uint8_t>& bytes)
{
    for (size_t i = 0; i < bytes.size(); i++)
        _z80->DirectWrite(static_cast<uint16_t>(address + i), bytes[i]);
}

void TsfmPlayerHarness::OnPortWrite(uint16_t port, uint8_t value)
{
    // TurboSound ports: #FFFD (chip 0 select / register address) and #BFFD
    // (chip 1 select / register data). The legacy decoder reacts to the exact
    // addresses; the TSFM CPLD decodes any A0=1 port, but the player only ever
    // drives these two
    if (port == 0xFFFD)
    {
        _chip0Writes++;
        if (value >= 0xF8)
            _controlWordCounts[value & 7]++;
    }
    else if (port == 0xBFFD)
    {
        _chip1Writes++;
    }
    else
    {
        return;
    }

    _writes.push_back(PortWrite{port, value});
}
