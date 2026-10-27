#pragma once

// Test helpers for the ZX-MultiSound in a running machine (multisoundslotcard_test.cpp, multisounddevicestate_test.cpp,
// midicontrol_test.cpp): a shipped config with its [SLOTS] replaced, one bus cycle as the Z80 runs it, a parked CPU and
// a one-preset sound bank. Every scratch file has a per-process unique name.

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "3rdparty/sam2695/tests/sf2builder.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/cards/multisound/multisoundslotcard.h"
#include "emulator/slots/slotmanager.h"

namespace multisoundtest
{

/// A shipped config with its [SLOTS] section replaced and extra sections appended, staged in the per-process scratch
/// folder; the machine is created from it with every sound device as configured
class StagedMachine
{
public:
    StagedMachine(const std::string& folder, const std::string& slotsSection, const std::string& extra = {})
    {
        const std::filesystem::path source = TestPathHelper::FindProjectRoot() / "data" / "configs" / folder / "unreal.ini";
        std::ifstream in(source, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t shipped = text.find("\n[SLOTS]");
        if (shipped != std::string::npos)
        {
            const size_t next = text.find("\n[", shipped + 1);
            text.erase(shipped, next == std::string::npos ? std::string::npos : next - shipped);
        }
        text += "\n[SLOTS]\n" + slotsSection + "\n" + extra + "\n";
        _path = TestPathHelper::GetUniqueTestScratchPath("ms4-" + folder + ".ini");
        std::ofstream out(_path, std::ios::binary);
        out << text;
        out.close();
        _emulator = std::make_unique<Emulator>(LoggerLevel::LogError);
        _emulator->SetCustomConfigPath(_path.string());
        _ok = _emulator->Init();
    }
    ~StagedMachine()
    {
        _emulator->Release();
        std::error_code ignored;
        std::filesystem::remove(_path, ignored);
    }
    bool Ok() const
    {
        return _ok;
    }
    EmulatorContext* Context() const
    {
        return _emulator->GetContext();
    }
    Emulator& Machine()
    {
        return *_emulator;
    }
    /// The slot-built MultiSound in a slot; nullptr when none was built
    MultiSoundSlotCard* Card(const std::string& slot = "zxbus.1") const
    {
        SlotManager* slots = Context()->pSlotManager;
        return slots != nullptr ? dynamic_cast<MultiSoundSlotCard*>(slots->FindCard(slot)) : nullptr;
    }
    const SlotManager::BuiltIn* BuiltIn(const std::string& id) const
    {
        return Context()->pSlotManager->Current().FindBuiltIn(id);
    }

private:
    SoundCardScope _everySound;
    std::filesystem::path _path;
    std::unique_ptr<Emulator> _emulator;
    bool _ok = false;
};

constexpr uint16_t kPc = 0x8000;        // the IN / OUT instruction outside the ROM: no ROM-fetch lock
constexpr uint16_t kRomPc = 0x3D2F;     // TR-DOS ROM: the SAA and SounDrive ports are locked

/// One bus cycle as the Z80 runs it, the IN / OUT instruction's M1 at `m1`
inline void Out(StagedMachine& m, uint16_t port, uint8_t value, uint16_t m1 = kPc)
{
    m.Context()->pCore->GetZ80()->m1_pc = m1;
    m.Context()->pPortDecoder->WriteCycle(port, value, m1);
}
inline uint8_t In(StagedMachine& m, uint16_t port, bool& cardDrove, uint16_t m1 = kPc)
{
    m.Context()->pCore->GetZ80()->m1_pc = m1;
    return m.Context()->pPortDecoder->ReadCycle(port, m1, cardDrove);
}

/// A CPU parked in DI; HALT at #8000, so frames run without the ROM touching the card
inline void ParkCpu(StagedMachine& m)
{
    Z80* z80 = m.Context()->pCore->GetZ80();
    z80->DirectWrite(0x8000, 0xF3);
    z80->DirectWrite(0x8001, 0x76);
    z80->pc = 0x8000;
}


/// A bank with one preset (bank 0, program 0: a looped sine) written to a per-process scratch file
inline std::string WriteTestBank()
{
    using namespace sam2695test;
    Sf2Builder builder;
    const int sine = builder.AddSample(SineSample("sine"));
    builder.presets.push_back(SimplePreset("Sine", 0, 0, sine, { { G(sam2695::Gen::SampleModes), 1 } }));
    const std::vector<uint8_t> bytes = builder.Build();
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("ms4-bank.sf2");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

} // namespace multisoundtest
