#pragma once

#include "stdafx.h"

#include <atomic>
#include "common/modulelogger.h"
#include "common/sound/audiodevicedescriptor.h"
#include "emulator/platform.h"
#include "corestate.h"
#include "emulator/io/tape/tape.h"
#include "emulator/notifications.h"

class Core;
class Emulator;
class Keyboard;
class MainLoop;
class Memory;
class WD1793;
class PortDecoder;
class Screen;
class UlaContention;
class SoundManager;
#ifdef ENABLE_RECORDING
class RecordingManager;
#endif
class DebugManager;
class Z80Disassembler;
class FeatureManager;

// Create callback type for audio
// User in emulator/sound/soundmanager and client/GUI
// void audioCallback(int16_t* samples, size_t numSamples);
typedef void (*AudioCallback)(void* obj, int16_t*, size_t);

class EmulatorContext
{
    /// region <Child object references>
public:
    // Unique identifier for this emulator instance
    unreal::UUID emulatorId;

    // Advanced logger instance
    ModuleLogger* pModuleLogger = nullptr;

	// Global emulator configuration (MemoryRead from ini file)
	CONFIG config;

    // Runtime state
    CoreState coreState;

	// Emulated system state (ports, flags including peripheral devices)
	EmulatorState emulatorState;

	// Temporary state for all extended platform features
	// TODO: rework and put into appropriate platform / state classes
	TEMP temporary;

	// Host system properties / context
	HOST host;

    // Main emulation loop
    MainLoop* pMainLoop = nullptr;

	// Computer system instance
	Core* pCore = nullptr;

	// Keyboard controller instance
	Keyboard* pKeyboard = nullptr;

	// Memory controller instance
	Memory* pMemory = nullptr;

	// Model-specific port decoder
	PortDecoder* pPortDecoder = nullptr;

    // Tape input instance
    Tape* pTape = nullptr;

    // BDI - Beta Disk Interface controller instance
    WD1793* pBetaDisk = nullptr;

	// Video controller parameters and logic
	Screen* pScreen = nullptr;

	// Standalone ULA contention component (memory/IO contention + floating bus)
	UlaContention* pUlaContention = nullptr;

    // Audio callback (will be triggered after each video frame render and provide audio samples for host system)
    // Using std::atomic to ensure proper memory ordering between UI thread (setting) and emulator thread (reading)
    std::atomic<void*> pAudioManagerObj;
    std::atomic<AudioCallback> pAudioCallback;

    /// Audio ring occupancy cell in STEREO FRAMES, owned by the app-side
    /// sound manager (outlives the emulator) and registered together with the
    /// audio callback. The DRC controller (SoundManager::updateDrcControl)
    /// reads it once per frame as its process variable; MainLoop reads it for
    /// the emergency refill path. nullptr = no audio device attached -> DRC
    /// disengaged (unity bypass).
    std::atomic<const std::atomic<uint32_t>*> pAudioRingOccupancy{nullptr};

    /// Native sample rate of the attached audio device (audio-sync design
    /// Fix 3). 0 = same as CORE_SAMPLING_RATE. The DRC resampler uses
    /// device/core as its base ratio; ring occupancy is in DEVICE-rate frames.
    std::atomic<uint32_t> pAudioDeviceSampleRate{0};

    /// Full realtime-observable device/ring state (audiodevicedescriptor.h),
    /// owned by the frontend's sound manager. Superset of the two cells
    /// above (occupancy cell points INTO it); monitoring consumers (WebAPI,
    /// diagnostics) read it lock-free. nullptr = no device attached.
    std::atomic<const AudioDeviceDescriptor*> pAudioDeviceDescriptor{nullptr};

    // Sound manager
    SoundManager* pSoundManager = nullptr;

#ifdef ENABLE_RECORDING
    // Recording manager (video/audio capture for recordings)
    RecordingManager* pRecordingManager = nullptr;
#endif

	// Debug manager (includes Breakpoints, Labels and Disassembler)
	DebugManager* pDebugManager = nullptr;

    // Feature toggle manager
    FeatureManager* pFeatureManager = nullptr;
    /// endregion </Child object references>

    /// region <Parent object references>
public:
    Emulator* pEmulator;
    /// endregion </Parent object references>

    /// region <Constructors / destructors>
public:
    EmulatorContext();                      // Default constructor with LogTrace default logging level
    EmulatorContext(LoggerLevel level);     // Constructor allowing to specify default logging level
    EmulatorContext(Emulator* emulator, LoggerLevel level = LoggerLevel::LogTrace);    // Constructor registering reference to parent Emulator object
    virtual ~EmulatorContext();
    /// endregion </Constructors / destructors>
};
