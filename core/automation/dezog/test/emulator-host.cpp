// Headless DeZog host: a real emulator instance exposed through the production
// AutomationDezog module - no GUI, no CLI/WebAPI ports, deterministic startup.
//
// Used by tools/verification/dezog/verify_dzrp_emulator.py --launch and handy
// for attaching the DeZog VS Code extension to a scripted, headless emulator.
//
// Usage: dezog-emulator-host [port] [model]
//   port   DZRP listen port (default: UNREAL_DEZOG_PORT env or 12000)
//   model  emulator model short name (default: PENTAGON)

#include "automation-dezog.h"

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/platform.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace
{
std::atomic<bool> g_running{true};

void signalHandler(int)
{
    g_running.store(false);
}
}  // namespace

int main(int argc, char* argv[])
{
    uint16_t port = 0;  // 0 → AutomationDezog::resolvePort (env / default)
    std::string model = "PENTAGON";

    if (argc > 1)
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    if (argc > 2)
        model = argv[2];

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    EmulatorManager* manager = EmulatorManager::GetInstance();
    if (!manager)
    {
        std::cerr << "EmulatorManager unavailable\n";
        return 1;
    }

    auto emulator = manager->CreateEmulatorWithModel("dezog-host", model, LoggerLevel::LogWarning);
    if (!emulator)
    {
        std::cerr << "Failed to create emulator for model '" << model << "'\n";
        return 1;
    }

    // Deterministic boot into 48K BASIC ROM regardless of the staged unreal.ini
    emulator->GetContext()->config.reset_rom = RM_SOS;
    emulator->Reset();
    emulator->StartAsync();

    for (int i = 0; i < 100 && emulator->GetState() != StateRun; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (emulator->GetState() != StateRun)
    {
        std::cerr << "Emulator did not reach StateRun\n";
        return 1;
    }

    AutomationDezog dezog;
    if (!dezog.start(port))
    {
        std::cerr << "Failed to start DeZog server\n";
        return 1;
    }

    std::cout << "dezog-emulator-host: model " << model << ", emulator " << emulator->GetId()
              << ", DZRP port " << dezog.getPort() << " (Ctrl+C to stop)\n"
              << std::flush;

    while (g_running.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    dezog.stop();

    std::string id = emulator->GetId();
    emulator.reset();
    manager->RemoveEmulator(id);

    std::cout << "dezog-emulator-host: stopped\n";
    return 0;
}
