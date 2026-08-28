# DZRP Automation Module Design

## Directory Structure (Actual)

```
core/automation/dezog/
├── CMakeLists.txt             # automation_dezog static lib (+ dezog-test-server mock binary)
├── include/
│   ├── dzrptypes.h            # Protocol constants, enums
│   ├── dzrpprotocol.h         # Message framing, serialization
│   ├── dzrpserver.h           # dzrp::Server + IDebugInterface (transport layer, emulator-agnostic)
│   ├── dezogdebugadapter.h    # DezogDebugAdapter: IDebugInterface over the real Emulator
│   └── automation-dezog.h     # AutomationDezog: module lifecycle (start/stop), like AutomationCLI
├── src/
│   ├── dzrpprotocol.cpp
│   ├── dzrpserver.cpp         # Server + command handlers
│   ├── dezogdebugadapter.cpp
│   └── automation-dezog.cpp
└── test/
    ├── test-server.cpp        # Standalone mock server (no emulator dependency)
    └── emulator-host.cpp      # Headless real-emulator host (dezog-emulator-host [port] [model])

core/tests/automation/dezog/
├── dezogtestfixture.h         # Live-emulator fixture + RAM test program
├── dzrpprotocol_test.cpp      # Protocol layer (framing, LE helpers, bitfield)
├── dezogdebugadapter_test.cpp # Adapter over a live emulator (no sockets)
└── dzrpserver_test.cpp        # Real TCP session → server → adapter → emulator; AutomationDezog lifecycle
```

Layering: `dzrpserver.cpp` never touches the emulator - everything goes through
`IDebugInterface`. `DezogDebugAdapter` is the only file that includes emulator
headers, which keeps the protocol/server layer buildable standalone (mock
test server) and lets `core-tests` compile the whole stack in directly.

## Class Design

### dzrp::Server

```cpp
// dzrpserver.h
#pragma once

#include "dzrpprotocol.h"
#include "dzrptypes.h"
#include <atomic>
#include <thread>
#include <functional>

class IDebugInterface;  // forward

class DZRPServer
{
public:
    struct Config
    {
        uint16_t port = 12000;
        std::string bindAddress = "127.0.0.1";
    };

    explicit DZRPServer(IDebugInterface* debug, const Config& config = {});
    ~DZRPServer();

    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const;

    // Called by emulator when execution pauses
    void notifyPause(DZRPBreakReason reason, uint16_t addr, uint8_t bank = 0,
                     const std::string& message = "");

private:
    void acceptLoop();
    void sessionLoop(int clientSocket);
    DZRPResponse handleCommand(const DZRPCommand& cmd);

    IDebugInterface* m_debug;
    Config m_config;
    int m_listenSocket = -1;
    std::atomic<bool> m_running{false};
    std::thread m_acceptThread;
    
    // Active session
    std::mutex m_sessionMutex;
    int m_clientSocket = -1;
    uint8_t m_seqNo = 0;
};
```

### DZRPProtocol

```cpp
// dzrp-protocol.h
#pragma once

#include "dzrp-types.h"
#include <vector>
#include <cstdint>
#include <string>
#include <optional>

// Message structures
struct DZRPCommand
{
    uint8_t seqNo;
    uint8_t cmdId;
    std::vector<uint8_t> payload;
};

struct DZRPResponse
{
    uint8_t seqNo;
    bool nak = false;
    std::vector<uint8_t> payload;
};

struct DZRPNotification
{
    uint8_t notifyId;
    std::vector<uint8_t> payload;
};

class DZRPProtocol
{
public:
    // Framing
    static std::optional<DZRPCommand> parseCommand(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> serializeResponse(const DZRPResponse& resp);
    static std::vector<uint8_t> serializeNotification(const DZRPNotification& notif);

    // Read from socket with framing
    static std::optional<DZRPCommand> readCommand(int socket);
    static bool writeResponse(int socket, const DZRPResponse& resp);
    static bool writeNotification(int socket, const DZRPNotification& notif);

    // Helpers
    static uint16_t readU16LE(const uint8_t* data);
    static uint32_t readU32LE(const uint8_t* data);
    static void writeU16LE(uint8_t* data, uint16_t value);
    static void writeU32LE(uint8_t* data, uint32_t value);
    static std::string readNulString(const uint8_t* data, size_t maxLen);
};
```

### DZRPCommands

```cpp
// dzrp-commands.h
#pragma once

#include "dzrp-types.h"
#include "dzrp-protocol.h"

class IDebugInterface;

class DZRPCommands
{
public:
    explicit DZRPCommands(IDebugInterface* debug);

    // Command handlers - return response payload
    DZRPResponse handleInit(const DZRPCommand& cmd);
    DZRPResponse handleClose(const DZRPCommand& cmd);
    DZRPResponse handleGetRegisters(const DZRPCommand& cmd);
    DZRPResponse handleSetRegister(const DZRPCommand& cmd);
    DZRPResponse handleContinue(const DZRPCommand& cmd);
    DZRPResponse handlePause(const DZRPCommand& cmd);
    DZRPResponse handleReadMem(const DZRPCommand& cmd);
    DZRPResponse handleWriteMem(const DZRPCommand& cmd);
    DZRPResponse handleAddBreakpoint(const DZRPCommand& cmd);
    DZRPResponse handleRemoveBreakpoint(const DZRPCommand& cmd);
    DZRPResponse handleAddWatchpoint(const DZRPCommand& cmd);
    DZRPResponse handleRemoveWatchpoint(const DZRPCommand& cmd);
    DZRPResponse handleGetSupportedCommands(const DZRPCommand& cmd);
    DZRPResponse handleSetSlot(const DZRPCommand& cmd);
    DZRPResponse handleWriteBank(const DZRPCommand& cmd);
    DZRPResponse handleReadState(const DZRPCommand& cmd);
    DZRPResponse handleWriteState(const DZRPCommand& cmd);

    // Build pause notification
    DZRPNotification buildPauseNotification(DZRPBreakReason reason, 
                                             uint16_t addr, uint8_t bank,
                                             const std::string& message);

    // Dispatch by command ID
    DZRPResponse dispatch(const DZRPCommand& cmd);

private:
    IDebugInterface* m_debug;
    
    // Breakpoint tracking
    std::unordered_map<uint16_t, uint16_t> m_breakpoints;  // id -> addr
    uint16_t m_nextBpId = 1;
};
```

### DZRPTypes

```cpp
// dzrp-types.h
#pragma once

#include <cstdint>

// DZRP version we implement
constexpr uint8_t DZRP_VERSION_MAJOR = 2;
constexpr uint8_t DZRP_VERSION_MINOR = 2;
constexpr uint8_t DZRP_VERSION_PATCH = 0;

// Command IDs
enum class DZRPCommandId : uint8_t
{
    CMD_INIT = 1,
    CMD_CLOSE = 2,
    CMD_GET_REGISTERS = 3,
    CMD_SET_REGISTER = 4,
    CMD_WRITE_BANK = 5,
    CMD_CONTINUE = 6,
    CMD_PAUSE = 7,
    CMD_READ_MEM = 8,
    CMD_WRITE_MEM = 9,
    CMD_SET_SLOT = 10,
    CMD_GET_TBBLUE_REG = 11,
    CMD_SET_BORDER = 12,
    CMD_SET_BREAKPOINTS = 13,
    CMD_RESTORE_MEM = 14,
    CMD_LOOPBACK = 15,
    CMD_GET_SPRITES_PALETTE = 16,
    CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL = 17,
    CMD_GET_SPRITES = 18,
    CMD_GET_SPRITE_PATTERNS = 19,
    CMD_READ_PORT = 20,
    CMD_WRITE_PORT = 21,
    CMD_EXEC_ASM = 22,
    CMD_INTERRUPT_ON_OFF = 23,
    CMD_GET_SUPPORTED_COMMANDS = 24,
    CMD_ADD_BREAKPOINT = 40,
    CMD_REMOVE_BREAKPOINT = 41,
    CMD_ADD_WATCHPOINT = 42,
    CMD_REMOVE_WATCHPOINT = 43,
    CMD_READ_STATE = 50,
    CMD_WRITE_STATE = 51,
};

// Notification IDs
enum class DZRPNotificationId : uint8_t
{
    NTF_PAUSE = 1,
};

// Break reasons
enum class DZRPBreakReason : uint8_t
{
    NONE = 0,
    MANUAL = 1,
    BREAKPOINT = 2,
    WATCHPOINT_READ = 3,
    WATCHPOINT_WRITE = 4,
    OTHER = 255,
};

// Machine types
enum class DZRPMachineType : uint8_t
{
    UNKNOWN = 0,
    ZX16K = 1,
    ZX48K = 2,
    ZX128K = 3,
    ZXNEXT = 4,
};

// Register IDs
enum class DZRPRegister : uint8_t
{
    PC = 0, SP = 1, AF = 2, BC = 3, DE = 4, HL = 5, IX = 6, IY = 7,
    AF2 = 8, BC2 = 9, DE2 = 10, HL2 = 11,
    IM = 13,
    F = 14, A = 15, C = 16, B = 17, E = 18, D = 19, L = 20, H = 21,
    IXL = 22, IXH = 23, IYL = 24, IYH = 25,
    F2 = 26, A2 = 27, C2 = 28, B2 = 29, E2 = 30, D2 = 31, L2 = 32, H2 = 33,
    R = 34, I = 35,
};

// Watchpoint access types
enum class DZRPWatchAccess : uint8_t
{
    READ = 0x01,
    WRITE = 0x02,
    READ_WRITE = 0x03,
};
```

### IDebugInterface (Shared)

```cpp
// core/automation/include/debug-interface.h
#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <string>

struct Z80Registers
{
    uint16_t pc, sp;
    uint16_t af, bc, de, hl;
    uint16_t ix, iy;
    uint16_t af2, bc2, de2, hl2;
    uint8_t r, i, im;
};

struct MemorySlot
{
    uint8_t bank;
};

enum class BreakReason
{
    None,
    Manual,
    Breakpoint,
    WatchpointRead,
    WatchpointWrite,
    Other,
};

struct BreakEvent
{
    BreakReason reason;
    uint16_t address;
    uint8_t bank;
    std::string message;
};

class IDebugInterface
{
public:
    virtual ~IDebugInterface() = default;

    // Execution control
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual bool isPaused() const = 0;

    // Registers
    virtual Z80Registers getRegisters() const = 0;
    virtual void setRegister(uint8_t regId, uint16_t value) = 0;

    // Memory
    virtual std::vector<uint8_t> readMemory(uint16_t addr, uint16_t len) const = 0;
    virtual void writeMemory(uint16_t addr, const std::vector<uint8_t>& data) = 0;

    // Memory banking (ZX 128K+)
    virtual std::vector<MemorySlot> getSlots() const = 0;
    virtual void setSlot(uint8_t slot, uint8_t bank) = 0;
    virtual void writeBank(uint8_t bank, const std::vector<uint8_t>& data) = 0;

    // Breakpoints
    virtual uint16_t addBreakpoint(uint16_t addr, uint8_t bank = 0,
                                    const std::string& condition = "") = 0;
    virtual void removeBreakpoint(uint16_t id) = 0;

    // Watchpoints
    virtual bool addWatchpoint(uint16_t addr, uint8_t bank, uint16_t size,
                               uint8_t access) = 0;
    virtual void removeWatchpoint(uint16_t addr, uint8_t bank, uint16_t size,
                                  uint8_t access) = 0;

    // State (reverse debugging)
    virtual std::vector<uint8_t> captureState() const = 0;
    virtual void restoreState(const std::vector<uint8_t>& state) = 0;

    // Machine info
    virtual uint8_t getMachineType() const = 0;  // DZRPMachineType

    // Event callback
    using BreakHandler = std::function<void(const BreakEvent&)>;
    virtual void setBreakHandler(BreakHandler handler) = 0;
};
```

## Sequence Diagrams

### Connection Handshake

```
DeZog                           Unreal-NG
  |                                  |
  |-------- TCP Connect ------------>|
  |                                  |
  |-------- CMD_INIT --------------->|
  |         (version, "DeZog v3.5")  |
  |                                  |
  |<------- Response ----------------|
  |         (version, machine,       |
  |          "Unreal-NG v1.0")       |
  |                                  |
  |-------- CMD_GET_SUPPORTED ------>|
  |                                  |
  |<------- Response (bitfield) -----|
  |                                  |
  |-------- CMD_GET_REGISTERS ------>|
  |                                  |
  |<------- Response (regs+slots) ---|
```

### Breakpoint Hit

```
DeZog                           Unreal-NG
  |                                  |
  |-------- CMD_ADD_BREAKPOINT ----->|
  |         (addr=0x8000)            |
  |                                  |
  |<------- Response (bpId=1) -------|
  |                                  |
  |-------- CMD_CONTINUE ----------->|
  |                                  |
  |<------- Response ----------------|
  |                                  |
  |         ... Z80 runs ...         |
  |         ... hits 0x8000 ...      |
  |                                  |
  |<------- NTF_PAUSE ---------------|
  |         (reason=BP, addr=0x8000) |
  |                                  |
  |-------- CMD_GET_REGISTERS ------>|
  |                                  |
  |<------- Response ----------------|
```

## Build Integration (Actual)

`ENABLE_DEZOG_AUTOMATION` (default `ON`) is declared at every level, exactly
like the other automation modules, and gated by the master `ENABLE_AUTOMATION`:

| File | What it does |
|------|--------------|
| `CMakeLists.txt` (root) | `option(ENABLE_DEZOG_AUTOMATION ... ON)` + status line |
| `core/automation/CMakeLists.txt` | option, forced OFF when `ENABLE_AUTOMATION=OFF`, `add_subdirectory(dezog)`, links `automation_dezog` (exe + lib branches), include dir, `ENABLE_DEZOG_AUTOMATION` compile definition |
| `core/automation/dezog/CMakeLists.txt` | `automation_dezog` static lib (`unrealng::automation_dezog`), links `unrealng::core`; `dezog-test-server` mock binary built from the protocol sources only; `dezog-emulator-host` headless real-emulator host |
| `unreal-qt/CMakeLists.txt` | option + compile definition (must match automation's) |
| `unreal-videowall/CMakeLists.txt` | option + compile definition |
| `core/tests/CMakeLists.txt` | compiles the four dezog sources directly into `core-tests` |

```cmake
# core/automation/dezog/CMakeLists.txt (excerpt)
add_library(automation_dezog STATIC
    src/dzrpprotocol.cpp src/dzrpserver.cpp          # transport layer
    src/dezogdebugadapter.cpp src/automation-dezog.cpp  # emulator-bound layer
)
target_include_directories(automation_dezog PUBLIC include ../cli/include)  # platform-sockets.h
target_link_libraries(automation_dezog PUBLIC unrealng::core)
```

Configure with `-DENABLE_DEZOG_AUTOMATION=OFF` to drop the module entirely
(no server thread, no port, no symbols).

## Integration Points (Actual)

### Automation singleton

`core/automation/automation.{h,cpp}` owns one `AutomationDezog*` next to the
Lua/Python/WebAPI/CLI modules:

```cpp
bool Automation::start()   { ... #if ENABLE_DEZOG_AUTOMATION  result &= startDezog();  #endif ... }
void Automation::stop()    { ... #if ENABLE_DEZOG_AUTOMATION  stopDezog();  #endif ... }
AutomationDezog* Automation::getDezog();
```

`AutomationDezog::start(port)`:
- `port == 0` → `UNREAL_DEZOG_PORT` environment variable → `dzrp::DEFAULT_PORT` (12000)
- creates a `DezogDebugAdapter` (unbound → resolves `EmulatorManager::GetMostRecentEmulator()` per call)
- creates a `dzrp::Server` bound to `127.0.0.1:<port>` and wires
  `adapter.setPauseNotifier(server.notifyPause)`
- returns `false` (and tears down) if the port is busy — the rest of automation keeps running

### DezogDebugAdapter ↔ Emulator

| IDebugInterface | Emulator API |
|-----------------|--------------|
| `pause()` | `Emulator::Pause()` (blocks until the Z80 thread parked) then emits `MANUAL` with PC |
| `resume()` | `Emulator::Resume()` |
| `getRegisters()/setRegister()` | `Z80State` fields (`alt.*` for shadow set, `r_low` for R) |
| `readMemory()/writeMemory()` | `Memory::DirectRead/WriteToZ80Memory` (no breakpoint side effects) |
| `getSlots()/setSlot()` | `GetROMPage/GetRAMPageForBankN`, `SetROMPage/SetRAMPageToBankN`; ROM banks = 8/9 (DeZog ZX128 model), `0xFE/0xFF` aliases accepted |
| `writeBank()` | `memcpy` into `Memory::RAMPageAddress(bank)` |
| `addBreakpoint()` | `BreakpointManager::AddExecutionBreakpoint[InPage]` (owner `"dezog"`); temporaries tracked in a set |
| `addWatchpoint()` | one `AddCombinedMemoryBreakpoint[InPage]` per byte of the range (idempotent per key) |
| `captureState()/restoreState()` | `Emulator::SaveSnapshot/LoadSnapshot` via a temp `.sna` file |
| `getMachineType()` | `config.mem_model` (`MM_SPECTRUM48` → ZX48K, everything else → ZX128K) |
| `setBorder()` | `Screen::SetBorderColor` |

Adding a breakpoint/watchpoint calls `Emulator::DebugOn()` and enables the
`debugmode` + `breakpoints` features so the instrumented memory interface is
active.

### Stop notifications

The adapter subscribes (Observer-method, so it can unsubscribe precisely) to
`NC_EXECUTION_BREAKPOINT`, filters by the emulator UUID in
`BreakpointTriggeredPayload` (nil id = legacy sender, accepted), classifies the
hit via the `BreakpointDescriptor::memoryType` (execute → `BREAKPOINT`,
write-only → `WATCHPOINT_WRITE`, otherwise `WATCHPOINT_READ`) and calls the
notifier. Exactly one NTF_PAUSE is produced per stop:

- `CMD_PAUSE` → the server defers `pause()` until after the ACK is written, so
  the wire order is always ACK → NTF_PAUSE.
- GUI-initiated pauses are **not** forwarded (DeZog only expects a notification
  in reply to its own CMD_PAUSE / CMD_CONTINUE).

### Session close

When the client goes away (CMD_CLOSE or a dropped socket) the server calls
`IDebugInterface::onSessionClosed()`. The adapter removes every dezog-owned
breakpoint and watchpoint (temporaries included), keeps foreign breakpoints
(CLI / GUI / analyzers) untouched, and resumes the emulator if it was paused.
DeZog re-sends all breakpoints on the next connect, so nothing is lost and a
closed VS Code window never leaves the target stuck at a stale breakpoint.

### GDB Server Refactor

The existing GDB server should be refactored to use IDebugInterface:

```cpp
// Before: direct Z80/memory access
// After: via IDebugInterface

class GDBServer
{
    IDebugInterface* m_debug;  // shared with DZRP
    // ...
};
```
