#pragma once

/// @file vdac4control.h
/// @brief The VDAC4 board's state for the automation surfaces: one place every interface (WebAPI, MCP, CLI, Lua,
/// Python) reads, so all of them answer the same (docs/inprogress/2026-11-15-tsconf-vdac4/design.md §9).
///
/// Fails with a reason when the machine has no VDAC4 board (not TS-Conf, [MISC] TS_VDAC4=0, or a binary built
/// without ENABLE_VDAC2).

#include <cstdint>
#include <string>
#include <vector>

class EmulatorContext;
class Vdac4Board;
class Vdac4Tang;

namespace Vdac4Control
{

struct BoardState
{
    // Tang
    std::string personality;    ///< "ts", "loading", "ft"
    bool armed = false;         ///< the ESP armed the Tang (TS personality switches on Evo FT traffic)
    bool espHost = false;       ///< FT_CFG ESP_HOST: the ESP drives the FT812
    bool ramG3M = false;        ///< FT_CFG RAM_G 3 MB
    uint32_t ftCfg = 0;

    // ESP
    std::string espPhase;       ///< "off", "booting", "running", "flushing", "restart_wait"
    uint32_t espMs = 0;         ///< ESP time since power-on (millis())
    uint32_t boots = 0;
    uint8_t bootNonce = 0;
    uint8_t lastStep = 0;       ///< the last command the firmware handled (SYS_INFO step)
    std::string lastError;      ///< the firmware's last error text ("" none)
    size_t resources = 0;       ///< resources held in PSRAM
    uint64_t clockRestarts = 0; ///< machine resets the ESP's clock stepped over (it keeps running)
    uint64_t clockOffsetUs = 0; ///< ESP time ahead of the machine's emulated time

    // App
    bool appRunning = false;
    std::string appModule;      ///< module file name
    std::string appName;        ///< zm_app_t name / version
    std::string appVersion;
    uint8_t commandFirst = 1;   ///< the app's command range (first > last: none)
    uint8_t commandLast = 0;
    std::string appError;       ///< the runtime's last error (a fault: what and where; "" none)
    struct AppSection
    {
        uint32_t elfAddress = 0;    ///< the section's address in the app's ELF file
        uint32_t size = 0;
        uint32_t loadedAt = 0;      ///< the ESP D-bus address it was loaded at
    };
    std::vector<AppSection> appSections;  ///< the running app's loaded sections (.text, .bss, .data, ...)

    std::vector<std::string> log;  ///< the ESP's recent log lines, oldest first

    struct Packet
    {
        uint8_t command = 0;
        uint32_t length = 0;
        uint32_t ms = 0;            ///< ESP time it was handled
    };
    // Link and frame transport
    size_t pendingFromZ80 = 0;      ///< bytes received, not yet parsed
    size_t pendingToZ80 = 0;        ///< bytes queued for the Z80
    bool sendBlocked = false;       ///< the firmware waits inside send() for ring room
    uint32_t badChecksums = 0;
    uint32_t resyncs = 0;
    uint32_t droppedFrames = 0;
    uint64_t packets = 0;           ///< packets the Z80 sent since power-on
    std::vector<Packet> recentPackets;  ///< the last ones (up to 64), oldest first

    // Audio (the ESP's I2S player on the mixer row "VDAC4 ESP")
    uint32_t audioInputRate = 0;    ///< the app's PCM rate (0: never opened)
    uint32_t audioOutputRate = 0;
    uint32_t audioQueued = 0;       ///< output frames ahead of the mixer
    uint64_t audioUnderruns = 0;
    uint64_t audioDropped = 0;
    bool audioSound = false;        ///< the last frame was not silent
};

/// The machine has a VDAC4 board; `error` says why not
bool HasBoard(EmulatorContext* context, std::string* error = nullptr);

/// The board's state; `logLines` = how many recent log lines to include (0: none). Any thread: the Tang, ESP and
/// app fields are the board's copy from the last frame end, the log, packets and audio are read under their locks
bool GetState(EmulatorContext* context, BoardState& state, size_t logLines, std::string* error = nullptr);

/// `size` bytes (at most kMaxEspRead) of the ESP's memory from `address`: a D-bus / I-bus / SRAM address, or with
/// `appAddress` an address of the running app's ELF image (the sections of the last frame end map it). Any thread,
/// no pause: the bytes are the ones in memory now, as a Z80 memory read is. False with a reason: no board, no app,
/// unmapped
constexpr uint32_t kMaxEspRead = 4096;
bool ReadEspMemory(EmulatorContext* context, uint32_t address, uint32_t size, bool appAddress,
                   std::vector<uint8_t>& out, std::string* error = nullptr);

/// The Tang, ESP, app and link fields read live (emulation thread only; Vdac4Board publishes them per frame)
void CaptureLive(Vdac4Board& board, const Vdac4Tang& tang, BoardState& state);

} // namespace Vdac4Control
