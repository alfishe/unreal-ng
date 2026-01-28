#pragma once

#include "stdafx.h"

#include <atomic>
#include "common/modulelogger.h"
#include "emulator/platform.h"
#include "corestate.h"
#include "emulator/io/tape/tape.h"

class Core;
class Emulator;
class Keyboard;
class MainLoop;
class Memory;
class WD1793;
class PortDecoder;
class Screen;
class SoundManager;
class RecordingManager;
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

    // Audio callback (will be triggered after each video frame render and provide audio samples for host system)
    // Using std::atomic to ensure proper memory ordering between UI thread (setting) and emulator thread (reading)
    std::atomic<void*> pAudioManagerObj;
    std::atomic<AudioCallback> pAudioCallback;

    // Sound manager
    SoundManager* pSoundManager = nullptr;

    // Recording manager (video/audio capture for recordings)
    RecordingManager* pRecordingManager = nullptr;

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
