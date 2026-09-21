#include "trdostesthelper.h"

#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "emulator/io/fdc/diskautostart.h"
#include "emulator/mainloop.h"

namespace
{
constexpr uint16_t FORMAT_TYPE_PROMPT = 0x1EDD;  // TR-DOS ROM: asks for the format type
constexpr uint16_t FORMAT_AFTER_PROMPT = 0x1EE0;
}  // namespace

TRDOSTestHelper::TRDOSTestHelper(Emulator* emulator, bool muteSound)
    : _emulator(emulator)
{
    if (_emulator)
    {
        _context = _emulator->GetContext();
        _memory = _emulator->GetMemory();
        _z80 = _context ? _context->pCore->GetZ80() : nullptr;

        if (muteSound && _context && _context->pFeatureManager)
            _context->pFeatureManager->setFeature(Features::kSoundGeneration, false);
    }
}

TRDOSTestHelper::~TRDOSTestHelper()
{
    removePromptHook();
}

bool TRDOSTestHelper::usable() const
{
    return _emulator && _memory && _z80 && _context && _context->pMainLoop && _context->pDiskAutostart;
}

void TRDOSTestHelper::removePromptHook()
{
    // The hook captures this helper: it must never outlive it
    if (_promptHookInstalled && _z80)
        _z80->m1TraceHook = nullptr;
    _promptHookInstalled = false;
}

/// region <Running TR-DOS commands>

void TRDOSTestHelper::startCommand(const std::string& command)
{
    if (!usable())
        return;

    // Breakpoint (analyzer) dispatch is only active in debug mode
    _emulator->DebugOn();

    removePromptHook();
    _formatPromptSkipped = false;

    _context->pCore->Reset(RM_DOS);
    _context->pDiskAutostart->ArmCommand(BasicEncoder::tokenizeImmediate(command), command);
}

void TRDOSTestHelper::skipFormatTypePrompt()
{
    if (!usable())
        return;

    removePromptHook();
    _formatPromptSkipped = false;

    // The M1 hook fires as the ROM fetches the opcode at $1EDD; the state is patched right there
    _z80->m1TraceHook = [this](uint16_t pc) {
        if (pc != FORMAT_TYPE_PROMPT || _formatPromptSkipped || !(_context->emulatorState.flags & CF_TRDOS))
            return;

        _memory->DirectWriteToZ80Memory(0x5CE6, 0xB9);  // Format sector table = $1FB9
        _memory->DirectWriteToZ80Memory(0x5CE7, 0x1F);
        _memory->DirectWriteToZ80Memory(0x5CE8, 0xBA);  // Verify sector table = $1FBA
        _memory->DirectWriteToZ80Memory(0x5CE9, 0x1F);
        _z80->a = 0x80;                                 // 80-track drive type
        _z80->pc = FORMAT_AFTER_PROMPT;
        _formatPromptSkipped = true;
    };
    _promptHookInstalled = true;
}

uint64_t TRDOSTestHelper::executeCommand(const std::string& command, uint64_t maxCycles)
{
    if (!usable())
        return 0;

    startCommand(command);

    const uint64_t frameTStates = _context->config.frame;
    return runFrames(static_cast<unsigned>((maxCycles + frameTStates - 1) / frameTStates));
}

bool TRDOSTestHelper::formatDisk(uint64_t maxCycles, uint64_t* cyclesUsed)
{
    if (cyclesUsed)
        *cyclesUsed = 0;
    if (!usable())
        return false;

    startCommand("FORMAT \"testdisk\"");
    skipFormatTypePrompt();

    return runUntil([this] { return _formatPromptSkipped && formatFinishedOnScreen(); }, maxCycles,
                    2 * DEFAULT_POLL_FRAMES, cyclesUsed);
}

/// endregion </Running TR-DOS commands>

/// region <Execution control>

uint64_t TRDOSTestHelper::runFrames(unsigned frames)
{
    if (!usable())
        return 0;

    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop);
    for (unsigned i = 0; i < frames; i++)
        mainLoop->RunFrame();

    return static_cast<uint64_t>(frames) * _context->config.frame;
}

bool TRDOSTestHelper::runUntil(const std::function<bool()>& done, uint64_t maxCycles, unsigned pollFrames,
                               uint64_t* cyclesUsed)
{
    uint64_t cycles = 0;
    bool reached = false;

    while (usable() && cycles < maxCycles)
    {
        cycles += runFrames(pollFrames);
        if (done())
        {
            reached = true;
            break;
        }
    }

    if (cyclesUsed)
        *cyclesUsed = cycles;
    return reached;
}

/// endregion </Execution control>

/// region <State>

std::string TRDOSTestHelper::screenText() const
{
    return _emulator ? ScreenOCR::ocrScreen(_emulator->GetId()) : std::string();
}

bool TRDOSTestHelper::formatFinishedOnScreen() const
{
    const std::string screen = screenText();
    return screen.find("repeat FORMAT") != std::string::npos ||
           (screen.find("A>") != std::string::npos && screen.find("FORMAT") == std::string::npos);
}

bool TRDOSTestHelper::isTRDOSActive() const
{
    if (!_memory)
        return false;

    return _memory->isCurrentROMDOS();
}

bool TRDOSTestHelper::verifyTRDOSVariables() const
{
    if (!_memory)
        return false;

    // Check PROG and VARS are reasonable
    uint8_t progL = _memory->DirectReadFromZ80Memory(SystemVariables48k::PROG);
    uint8_t progH = _memory->DirectReadFromZ80Memory(SystemVariables48k::PROG + 1);
    uint16_t progAddr = progL | (progH << 8);

    uint8_t varsL = _memory->DirectReadFromZ80Memory(SystemVariables48k::VARS);
    uint8_t varsH = _memory->DirectReadFromZ80Memory(SystemVariables48k::VARS + 1);
    uint16_t varsAddr = varsL | (varsH << 8);

    // VARS should be >= PROG
    if (varsAddr < progAddr)
        return false;

    // Both should be in reasonable range
    if (progAddr < 0x5C00 || progAddr > 0xFF00)
        return false;

    return true;
}

uint8_t TRDOSTestHelper::getTRDOSError() const
{
    if (!_memory)
        return 0xFF;

    return _memory->DirectReadFromZ80Memory(SystemVariables48k::ERR_NR);
}

bool TRDOSTestHelper::isExecutionStopped() const
{
    if (!_z80)
        return true;

    // HALT instruction was executed
    return _z80->halted;
}

/// endregion </State>
