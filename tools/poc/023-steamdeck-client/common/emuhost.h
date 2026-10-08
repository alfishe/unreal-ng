#pragma once

// EmuHost: the minimum a Deck front-end needs from the core, in one place, so each POC stays small.
// - one emulator instance (EmulatorManager::CreateEmulatorWithModel)
// - audio: core callback -> SDL audio stream (push), stream fill reported to the core's DRC
// - frame: CopyPresentedFramebuffer into a caller buffer (any thread)
// - input: Kempston joystick bits (atomic), ZX keys through the MessageCenter
// - a test program poked into RAM and started at its address (no ROM involvement)

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Emulator;

namespace deckpoc
{

struct FrameInfo
{
    uint16_t width = 0;
    uint16_t height = 0;
    uint64_t frameCounter = 0;
};

class EmuHost
{
public:
    EmuHost() = default;
    ~EmuHost();
    EmuHost(const EmuHost&) = delete;
    EmuHost& operator=(const EmuHost&) = delete;

    /// model: short name ("48K", "PENTAGON", "TSL", ...)
    bool Create(const std::string& model, std::string* error = nullptr);

    /// Load a tape / disk / snapshot by extension (before or after Start)
    bool LoadFile(const std::string& path, std::string* error = nullptr);

    /// Write code at address, set SP / PC; call before Start
    void InstallProgram(uint16_t address, const std::vector<uint8_t>& code, uint16_t sp = 0xFF00);

    /// Open the default playback device and register the audio callback
    bool OpenAudio(std::string* error = nullptr);
    void SetPresentDelayFrames(uint8_t frames);

    void Start();   // StartAsync: the core's MainLoop paces itself
    void Stop();

    /// Current framebuffer size (changes on NC_VIDEO_MODE_CHANGED); false if none yet
    bool FrameSize(FrameInfo& info) const;
    /// Copy the newest presented frame straight into dst (e.g. a mapped GPU transfer buffer)
    bool CopyFrameTo(uint8_t* dst, size_t size);

    void JoystickPress(uint8_t mask);
    void JoystickRelease(uint8_t mask);

    uint64_t FrameCounter() const;
    uint32_t AudioQueuedFrames() const { return _audioOccupancy.load(std::memory_order_relaxed); }
    uint32_t AudioUnderruns() const { return _audioUnderruns.load(std::memory_order_relaxed); }
    uint32_t AudioRate() const { return _audioRate; }

    Emulator* Get() const { return _emulator.get(); }

    /// Border flip test program: DI; loop: IN A,(#1F); AND #10; border 7 if fire else 0
    static std::vector<uint8_t> BorderFlipProgram();

private:
    static void AudioCallback(void* obj, int16_t* samples, size_t numSamples);

    std::shared_ptr<Emulator> _emulator;
    std::string _id;
    SDL_AudioStream* _stream = nullptr;
    uint32_t _audioRate = 0;
    std::atomic<uint32_t> _audioOccupancy{0};
    std::atomic<uint32_t> _audioUnderruns{0};
};

} // namespace deckpoc
