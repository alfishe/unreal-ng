#include "spectrumbasictesthelper.h"

#include <iostream>

#include "common/image/imagehelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

SpectrumBasicTestHelper::SpectrumBasicTestHelper(Emulator* emulator) : _emulator(emulator) {}

bool SpectrumBasicTestHelper::injectAndRun(const std::string& basicProgram)
{
    if (!_emulator)
    {
        std::cout << "[BasicTestHelper] ERROR: No emulator instance" << std::endl;
        return false;
    }

    Memory* memory = _emulator->GetMemory();
    if (!memory)
    {
        std::cout << "[BasicTestHelper] ERROR: No memory instance" << std::endl;
        return false;
    }

    EmulatorContext* context = _emulator->GetContext();
    if (!context || !context->pKeyboard)
    {
        std::cout << "[BasicTestHelper] ERROR: No keyboard available" << std::endl;
        return false;
    }

    Keyboard* keyboard = context->pKeyboard;

    // Helper to save screenshot
    auto saveScreen = [this](const std::string& name) {
        FramebufferDescriptor fb = _emulator->GetFramebuffer();
        if (fb.memoryBuffer && fb.memoryBufferSize > 0)
        {
            std::string path = name + ".png";
            std::cout << "[BasicTestHelper] Saving screenshot: " << path << std::endl;
            ImageHelper::SavePNG(path, fb.memoryBuffer, fb.memoryBufferSize, fb.width, fb.height);
        }
    };

    std::cout << "[BasicTestHelper] Switching to 48K ROM for BASIC..." << std::endl;
    memory->SetROM48k();

    // Start emulator
    std::cout << "[BasicTestHelper] Starting emulator..." << std::endl;
    _emulator->Resume();

    // Step 1: Run ROM to show copyright screen
    std::cout << "[BasicTestHelper] Running ROM initialization (~1 second)..." << std::endl;
    _emulator->RunNCPUCycles(3500000, false);
    saveScreen("step1_copyright");

    // Step 2: Press key to pass copyright screen
    std::cout << "[BasicTestHelper] Pressing SPACE to pass copyright screen..." << std::endl;
    keyboard->PressKey(ZXKEY_SPACE);
    _emulator->RunNCPUCycles(70000, false);
    keyboard->ReleaseKey(ZXKEY_SPACE);
    _emulator->RunNCPUCycles(3500000, false);  // Wait for prompt
    saveScreen("step2_after_keypress");

    // Step 3: Inject BASIC program
    std::cout << "[BasicTestHelper] Injecting BASIC program into memory..." << std::endl;
    bool injected = _encoder.loadProgram(memory, basicProgram);
    if (!injected)
    {
        std::cout << "[BasicTestHelper] ERROR: Failed to inject BASIC program" << std::endl;
        return false;
    }
    std::cout << "[BasicTestHelper] BASIC program injected" << std::endl;
    saveScreen("step3_after_inject");

    // Step 4: Type RUN command
    std::cout << "[BasicTestHelper] Typing RUN command..." << std::endl;
    typeRunCommand();
    saveScreen("step4_after_run");

    return true;
}

bool SpectrumBasicTestHelper::injectAndRunViaKeyboard(const std::string& basicProgram, unsigned cyclesToRun)
{
    std::cout << "[BasicTestHelper] === Starting BASIC program injection and execution ===" << std::endl;

    if (!injectAndRun(basicProgram))
    {
        std::cout << "[BasicTestHelper] ERROR: injectAndRun failed" << std::endl;
        return false;
    }

    // Execute emulator to run the program
    std::cout << "[BasicTestHelper] Executing " << cyclesToRun << " cycles for BASIC execution..." << std::endl;
    runCycles(cyclesToRun);
    std::cout << "[BasicTestHelper] Execution complete" << std::endl;
    std::cout << "[BasicTestHelper] === BASIC execution finished ===" << std::endl;

    return true;
}

void SpectrumBasicTestHelper::typeRunCommand()
{
    EmulatorContext* context = _emulator->GetContext();
    if (!context || !context->pKeyboard)
    {
        std::cout << "[BasicTestHelper] ERROR: No keyboard available" << std::endl;
        return;
    }

    Keyboard* keyboard = context->pKeyboard;

    std::cout << "[BasicTestHelper] Typing 'R'..." << std::endl;
    // Type "RUN" command
    // Each key press/release with delay
    keyboard->PressKey(ZXKEY_R);
    _emulator->RunNCPUCycles(3500, false);
    keyboard->ReleaseKey(ZXKEY_R);
    _emulator->RunNCPUCycles(3500, false);

    std::cout << "[BasicTestHelper] Typing 'U'..." << std::endl;
    keyboard->PressKey(ZXKEY_U);
    _emulator->RunNCPUCycles(3500, false);
    keyboard->ReleaseKey(ZXKEY_U);
    _emulator->RunNCPUCycles(3500, false);

    std::cout << "[BasicTestHelper] Typing 'N'..." << std::endl;
    keyboard->PressKey(ZXKEY_N);
    _emulator->RunNCPUCycles(3500, false);
    keyboard->ReleaseKey(ZXKEY_N);
    _emulator->RunNCPUCycles(3500, false);

    std::cout << "[BasicTestHelper] Pressing ENTER..." << std::endl;
    // Press ENTER to execute
    keyboard->PressKey(ZXKEY_ENTER);
    _emulator->RunNCPUCycles(3500, false);
    keyboard->ReleaseKey(ZXKEY_ENTER);
    _emulator->RunNCPUCycles(3500, false);

    std::cout << "[BasicTestHelper] RUN command completed" << std::endl;
}

void SpectrumBasicTestHelper::runCycles(unsigned cycles)
{
    if (!_emulator)
        return;
    _emulator->RunNCPUCycles(cycles, false);  // Don't skip breakpoints
}
