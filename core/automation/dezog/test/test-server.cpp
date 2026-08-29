// Standalone DZRP test server with mock debug interface
// Usage: dezog-test-server [port]

#include "dzrpserver.h"
#include <iostream>
#include <csignal>
#include <cstring>
#include <atomic>
#include <chrono>
#include <functional>
#include <set>
#include <thread>

std::atomic<bool> g_running{true};

void signalHandler(int)
{
    g_running.store(false);
}

// Mock debug interface for testing
class MockDebugInterface : public dzrp::IDebugInterface
{
public:
    MockDebugInterface()
    {
        // Initialize with some test values
        m_regs.pc = 0x0000;
        m_regs.sp = 0xFFFF;
        m_regs.af = 0x0044;
        m_regs.bc = 0x0000;
        m_regs.de = 0x0000;
        m_regs.hl = 0x0000;
        m_regs.ix = 0x0000;
        m_regs.iy = 0x0000;
        m_regs.af2 = 0x0000;
        m_regs.bc2 = 0x0000;
        m_regs.de2 = 0x0000;
        m_regs.hl2 = 0x0000;
        m_regs.r = 0;
        m_regs.i = 0;
        m_regs.im = 1;

        // Initialize memory with ZX Spectrum ROM-like pattern
        std::memset(m_memory, 0, sizeof(m_memory));

        // Put some test pattern in screen area
        for (int i = 0; i < 256; ++i)
        {
            m_memory[0x4000 + i] = static_cast<uint8_t>(i);
        }

        // Initialize slots for 48K (single slot, bank 0)
        m_slots = {0, 0, 5, 2};  // 128K style: ROM, RAM5, RAM2, RAM0
    }

    void pause() override
    {
        m_paused = true;
        std::cout << "[Mock] Paused\n";
    }

    void resume() override
    {
        m_paused = false;
        std::cout << "[Mock] Resumed\n";

        // Simulate a breakpoint hit shortly after continue: fire NTF_PAUSE
        // asynchronously for the first temporary breakpoint. This is the only
        // way to exercise Server::notifyPause end-to-end with the mock.
        if (m_pauseNotifier && !m_tempBreakpoints.empty())
        {
            uint16_t hitAddr = m_breakpoints[*m_tempBreakpoints.begin()];
            std::thread([this, hitAddr]()
            {
                for (int i = 0; i < 15 && g_running.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (!g_running.load())
                    return;
                m_paused = true;
                std::cout << "[Mock] Simulated breakpoint hit at 0x"
                          << std::hex << hitAddr << std::dec << "\n";
                m_pauseNotifier(dzrp::BreakReason::BREAKPOINT, hitAddr, 0);
            }).detach();
        }
    }

    bool isPaused() const override
    {
        return m_paused;
    }

    Registers getRegisters() const override
    {
        return m_regs;
    }

    void setRegister(dzrp::RegisterId regId, uint16_t value) override
    {
        std::cout << "[Mock] Set register " << static_cast<int>(regId)
                  << " = 0x" << std::hex << value << std::dec << "\n";

        switch (regId)
        {
            case dzrp::RegisterId::PC: m_regs.pc = value; break;
            case dzrp::RegisterId::SP: m_regs.sp = value; break;
            case dzrp::RegisterId::AF: m_regs.af = value; break;
            case dzrp::RegisterId::BC: m_regs.bc = value; break;
            case dzrp::RegisterId::DE: m_regs.de = value; break;
            case dzrp::RegisterId::HL: m_regs.hl = value; break;
            case dzrp::RegisterId::IX: m_regs.ix = value; break;
            case dzrp::RegisterId::IY: m_regs.iy = value; break;
            case dzrp::RegisterId::AF2: m_regs.af2 = value; break;
            case dzrp::RegisterId::BC2: m_regs.bc2 = value; break;
            case dzrp::RegisterId::DE2: m_regs.de2 = value; break;
            case dzrp::RegisterId::HL2: m_regs.hl2 = value; break;
            case dzrp::RegisterId::R: m_regs.r = static_cast<uint8_t>(value); break;
            case dzrp::RegisterId::I: m_regs.i = static_cast<uint8_t>(value); break;
            case dzrp::RegisterId::IM: m_regs.im = static_cast<uint8_t>(value); break;
            case dzrp::RegisterId::A: m_regs.af = (m_regs.af & 0x00FF) | (value << 8); break;
            case dzrp::RegisterId::F: m_regs.af = (m_regs.af & 0xFF00) | (value & 0xFF); break;
            case dzrp::RegisterId::B: m_regs.bc = (m_regs.bc & 0x00FF) | (value << 8); break;
            case dzrp::RegisterId::C: m_regs.bc = (m_regs.bc & 0xFF00) | (value & 0xFF); break;
            case dzrp::RegisterId::D: m_regs.de = (m_regs.de & 0x00FF) | (value << 8); break;
            case dzrp::RegisterId::E: m_regs.de = (m_regs.de & 0xFF00) | (value & 0xFF); break;
            case dzrp::RegisterId::H: m_regs.hl = (m_regs.hl & 0x00FF) | (value << 8); break;
            case dzrp::RegisterId::L: m_regs.hl = (m_regs.hl & 0xFF00) | (value & 0xFF); break;
            default: break;
        }
    }

    std::vector<uint8_t> readMemory(uint16_t addr, uint16_t len) const override
    {
        std::vector<uint8_t> result(len);
        for (uint16_t i = 0; i < len; ++i)
        {
            result[i] = m_memory[(addr + i) & 0xFFFF];
        }
        return result;
    }

    void writeMemory(uint16_t addr, const std::vector<uint8_t>& data) override
    {
        std::cout << "[Mock] Write " << data.size() << " bytes at 0x"
                  << std::hex << addr << std::dec << "\n";
        for (size_t i = 0; i < data.size(); ++i)
        {
            m_memory[(addr + i) & 0xFFFF] = data[i];
        }
    }

    std::vector<uint8_t> getSlots() const override
    {
        return m_slots;
    }

    uint8_t readPort(uint16_t port) const override
    {
        // Mock port bus: echo the low byte of the port number so the
        // Python verifier can assert an exact round-trip value.
        std::cout << "[Mock] Read port 0x" << std::hex << port << std::dec << "\n";
        return static_cast<uint8_t>(port & 0xFF);
    }

    void writePort(uint16_t port, uint8_t value) override
    {
        std::cout << "[Mock] Write port 0x" << std::hex << port << std::dec
                  << " = " << static_cast<int>(value) << "\n";
    }

    void setSlot(uint8_t slot, uint8_t bank) override
    {
        std::cout << "[Mock] Set slot " << static_cast<int>(slot)
                  << " = bank " << static_cast<int>(bank) << "\n";
        if (slot < m_slots.size())
        {
            m_slots[slot] = bank;
        }
    }

    void writeBank(uint8_t bank, const std::vector<uint8_t>& data) override
    {
        std::cout << "[Mock] Write bank " << static_cast<int>(bank)
                  << " (" << data.size() << " bytes)\n";
        // In a real implementation, this would write to the specific bank
    }

    uint16_t addBreakpoint(uint16_t addr, uint8_t bank,
                           const std::string& condition,
                           bool temporary) override
    {
        uint16_t id = m_nextBpId++;
        m_breakpoints[id] = addr;
        if (temporary)
            m_tempBreakpoints.insert(id);

        std::cout << "[Mock] Add " << (temporary ? "TEMP " : "")
                  << "breakpoint #" << id << " at 0x"
                  << std::hex << addr << std::dec
                  << " bank " << static_cast<int>(bank);
        if (!condition.empty())
            std::cout << " condition: " << condition;
        std::cout << "\n";
        return id;
    }

    void removeBreakpoint(uint16_t id) override
    {
        std::cout << "[Mock] Remove breakpoint #" << id << "\n";
        m_breakpoints.erase(id);
        m_tempBreakpoints.erase(id);
    }

    void clearTemporaryBreakpoints() override
    {
        if (!m_tempBreakpoints.empty())
        {
            std::cout << "[Mock] Clearing " << m_tempBreakpoints.size()
                      << " temporary breakpoints\n";
            for (uint16_t id : m_tempBreakpoints)
            {
                m_breakpoints.erase(id);
            }
            m_tempBreakpoints.clear();
        }
    }

    bool addWatchpoint(uint16_t addr, uint8_t bank, uint16_t size,
                       dzrp::WatchAccess access) override
    {
        std::cout << "[Mock] Add watchpoint at 0x" << std::hex << addr
                  << ", bank " << std::dec << static_cast<int>(bank)
                  << ", size " << size
                  << ", access " << static_cast<int>(access) << "\n";
        return true;
    }

    void removeWatchpoint(uint16_t addr, uint8_t bank, uint16_t size,
                          dzrp::WatchAccess access) override
    {
        std::cout << "[Mock] Remove watchpoint at 0x" << std::hex << addr
                  << ", bank " << std::dec << static_cast<int>(bank)
                  << ", size " << size
                  << ", access " << static_cast<int>(access) << "\n";
    }

    std::vector<uint8_t> captureState() const override
    {
        // Return a simple state snapshot
        std::vector<uint8_t> state;
        state.reserve(sizeof(m_regs) + sizeof(m_memory));

        const uint8_t* regPtr = reinterpret_cast<const uint8_t*>(&m_regs);
        state.insert(state.end(), regPtr, regPtr + sizeof(m_regs));

        state.insert(state.end(), m_memory, m_memory + sizeof(m_memory));

        std::cout << "[Mock] Captured state (" << state.size() << " bytes)\n";
        return state;
    }

    void restoreState(const std::vector<uint8_t>& state) override
    {
        if (state.size() >= sizeof(m_regs) + sizeof(m_memory))
        {
            std::memcpy(&m_regs, state.data(), sizeof(m_regs));
            std::memcpy(m_memory, state.data() + sizeof(m_regs), sizeof(m_memory));
            std::cout << "[Mock] Restored state\n";
        }
    }

    dzrp::MachineType getMachineType() const override
    {
        return dzrp::MachineType::ZX128K;
    }

    void setBorder(uint8_t color) override
    {
        std::cout << "[Mock] Set border color " << static_cast<int>(color) << "\n";
        m_borderColor = color;
    }

    // Wires the async pause notification: called by main() once the server exists
    void setPauseNotifier(std::function<void(dzrp::BreakReason, uint16_t, uint8_t)> notifier)
    {
        m_pauseNotifier = std::move(notifier);
    }

private:
    Registers m_regs{};
    uint8_t m_memory[65536]{};
    std::vector<uint8_t> m_slots;
    std::atomic<bool> m_paused{true};
    uint8_t m_borderColor = 0;
    std::function<void(dzrp::BreakReason, uint16_t, uint8_t)> m_pauseNotifier;

    std::unordered_map<uint16_t, uint16_t> m_breakpoints;
    std::set<uint16_t> m_tempBreakpoints;
    uint16_t m_nextBpId = 1;
};

int main(int argc, char* argv[])
{
    uint16_t port = dzrp::DEFAULT_PORT;

    if (argc > 1)
    {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    MockDebugInterface mockDebug;

    dzrp::ServerConfig config;
    config.port = port;
    config.serverName = "Unreal-NG-Test";
    config.serverVersion = "0.1.0";

    dzrp::Server server(&mockDebug, config);

    if (!server.start())
    {
        std::cerr << "Failed to start DZRP server\n";
        return 1;
    }

    // Route simulated breakpoint hits to the server's notification path
    mockDebug.setPauseNotifier([&server](dzrp::BreakReason reason, uint16_t addr, uint8_t bank)
    {
        server.notifyPause(reason, addr, bank);
    });

    std::cout << "DZRP test server running. Press Ctrl+C to stop.\n";
    std::cout << "Connect DeZog with launch.json:\n";
    std::cout << "  \"remoteType\": \"cspect\",\n";
    std::cout << "  \"cspect\": { \"hostname\": \"localhost\", \"port\": "
              << server.getPort() << " }\n";

    while (g_running.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    server.stop();
    return 0;
}
