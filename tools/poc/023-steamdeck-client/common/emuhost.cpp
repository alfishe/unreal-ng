#include "emuhost.h"

#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace deckpoc
{

EmuHost::~EmuHost()
{
    Stop();
}

bool EmuHost::Create(const std::string& model, std::string* error)
{
    _id = "deck-poc";
    _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel(_id, model, LoggerLevel::LogWarning, error);
    return _emulator != nullptr;
}

bool EmuHost::LoadFile(const std::string& path, std::string* error)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto has = [&](const std::vector<std::string>& list) { return std::find(list.begin(), list.end(), ext) != list.end(); };
    if (has(Emulator::SupportedSnapshotExtensions()))
        return _emulator->LoadSnapshot(path);
    if (has(Emulator::SupportedTapeExtensions()))
        return _emulator->LoadTape(path, error);
    if (has(Emulator::SupportedDiskExtensions()))
        return _emulator->LoadDisk(path, 0, error);

    if (error)
        *error = "unsupported extension " + ext;
    return false;
}

void EmuHost::InstallProgram(uint16_t address, const std::vector<uint8_t>& code, uint16_t sp)
{
    Memory* memory = _emulator->GetMemory();
    for (size_t i = 0; i < code.size(); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(address + i), code[i]);

    Z80State* z80 = _emulator->GetZ80State();
    z80->pc = address;
    z80->sp = sp;
}

std::vector<uint8_t> EmuHost::BorderFlipProgram()
{
    return {
        0xF3,             // 8000 DI
        0xDB, 0x1F,       // 8001 loop: IN A,(#1F)   Kempston
        0xE6, 0x10,       // 8003 AND #10             fire
        0x28, 0x06,       // 8005 JR Z,black
        0x3E, 0x07,       // 8007 LD A,7              white border
        0xD3, 0xFE,       // 8009 OUT (#FE),A
        0x18, 0xF4,       // 800B JR loop
        0xAF,             // 800D black: XOR A
        0xD3, 0xFE,       // 800E OUT (#FE),A
        0x18, 0xEF,       // 8010 JR loop
    };
}

bool EmuHost::HasKempston() const
{
    PortDecoder* decoder = _emulator->GetContext()->pPortDecoder;
    return decoder && decoder->HasKempstonJoystick();
}

void EmuHost::PrintScreen(const std::vector<std::string>& rows)
{
    // Character set of the 48K ROM: 96 glyphs x 8 bytes at #3D00
    std::vector<uint8_t> font(96 * 8, 0);
    const char* base = SDL_GetBasePath();
    std::ifstream rom(std::filesystem::path(base ? base : "") / "rom" / "48.rom", std::ios::binary);
    if (rom)
    {
        rom.seekg(0x3D00);
        rom.read(reinterpret_cast<char*>(font.data()), static_cast<std::streamsize>(font.size()));
    }

    Memory* memory = _emulator->GetMemory();
    for (uint16_t a = 0x4000; a < 0x5800; a++)
        memory->DirectWriteToZ80Memory(a, 0x00);
    for (uint16_t a = 0x5800; a < 0x5B00; a++)
        memory->DirectWriteToZ80Memory(a, 0x38);   // white paper, black ink

    for (size_t row = 0; row < rows.size() && row < 24; row++)
    {
        const std::string& text = rows[row];
        for (size_t col = 0; col < text.size() && col < 32; col++)
        {
            const unsigned char ch = static_cast<unsigned char>(text[col]);
            if (ch < 0x20 || ch > 0x7F)
                continue;
            const size_t glyph = static_cast<size_t>(ch - 0x20) * 8;
            // Pixel line l of character row r: 010r rlll rrrc cccc (r split into thirds)
            const uint16_t addr = static_cast<uint16_t>(0x4000 + ((row & 0x18) << 8) + ((row & 0x07) << 5) + col);
            for (uint16_t line = 0; line < 8; line++)
                memory->DirectWriteToZ80Memory(static_cast<uint16_t>(addr + (line << 8)), font[glyph + line]);
        }
    }
}

void EmuHost::AudioCallback(void* obj, int16_t* samples, size_t numSamples)
{
    auto* self = static_cast<EmuHost*>(obj);
    if (!self->_stream)
        return;

    const int queuedBefore = SDL_GetAudioStreamQueued(self->_stream);
    if (queuedBefore == 0)
        self->_audioUnderruns.fetch_add(1, std::memory_order_relaxed);

    SDL_PutAudioStreamData(self->_stream, samples, static_cast<int>(numSamples * sizeof(int16_t)));
    const int queued = SDL_GetAudioStreamQueued(self->_stream);
    self->_audioOccupancy.store(static_cast<uint32_t>(std::max(queued, 0) / 4), std::memory_order_relaxed);
}

bool EmuHost::OpenAudio(std::string* error)
{
    SDL_AudioSpec device{};
    int deviceFrames = 0;
    if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device, &deviceFrames))
        device.freq = 48000;

    // The core resamples to the device rate itself (DRC), so the stream does no rate conversion
    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, device.freq};
    _stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!_stream)
    {
        if (error)
            *error = SDL_GetError();
        return false;
    }
    _audioRate = static_cast<uint32_t>(device.freq);

    _emulator->SetAudioDeviceSampleRate(_audioRate);
    _emulator->SetAudioCallback(this, &EmuHost::AudioCallback, &_audioOccupancy);
    SDL_ResumeAudioStreamDevice(_stream);
    return true;
}

void EmuHost::SetPresentDelayFrames(uint8_t frames)
{
    _emulator->GetContext()->pScreen->SetPresentDelayFrames(frames);
}

void EmuHost::Start()
{
    _emulator->StartAsync();
}

void EmuHost::Stop()
{
    if (_emulator)
    {
        _emulator->ClearAudioCallback();
        EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetId());
        _emulator.reset();
    }
    if (_stream)
    {
        SDL_DestroyAudioStream(_stream);
        _stream = nullptr;
    }
}

bool EmuHost::FrameSize(FrameInfo& info) const
{
    const FramebufferDescriptor& fb = _emulator->GetContext()->pScreen->GetFramebufferDescriptor();
    if (fb.width == 0 || fb.height == 0)
        return false;
    info.width = fb.width;
    info.height = fb.height;
    info.frameCounter = FrameCounter();
    return true;
}

bool EmuHost::CopyFrameTo(uint8_t* dst, size_t size)
{
    return _emulator->GetContext()->pScreen->CopyPresentedFramebuffer(dst, size);
}

void EmuHost::JoystickPress(uint8_t mask)
{
    if (Joystick* joystick = _emulator->GetContext()->pJoystick)
        joystick->Press(mask);
}

void EmuHost::JoystickRelease(uint8_t mask)
{
    if (Joystick* joystick = _emulator->GetContext()->pJoystick)
        joystick->Release(mask);
}

uint64_t EmuHost::FrameCounter() const
{
    return _emulator ? _emulator->GetContext()->emulatorState.frame_counter : 0;
}

} // namespace deckpoc
