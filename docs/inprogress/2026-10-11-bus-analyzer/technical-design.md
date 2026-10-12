# Bus analyzer: technical design

**Date:** 2026-10-11 · part of [README.md](README.md) · requirements in [requirements.md](requirements.md)

## 1. Shape

```
                       BusManager (one per emulator, context->pBusManager)
                       ├── registry: id -> BusTap*          (mutex; taps register / unregister)
                       └── epoch (machine resets)
  bus master ──tap──▶  BusTap  "zc.spi"  protocol SPI, devices {0: sd, 1: ft812}, byte time 607 ns
  (ZControllerSpi,     ├── Active()  ← the only hot-path test
   Next SPI, DivMMC,   ├── BusTrace ring (BusTraceEvent, lazily allocated)
   NeoGSSpi)           ├── findings ring (BusFinding)
  device ──tap──▶      └── SpiChecks state (per slot: byte in command, selected)
  (SdCardSpi)
                       DeviceState::Bus* (StateNode) ──▶ WebAPI · MCP · CLI · Lua · Python · export
```

The model is the media manager's: the manager knows every bus, the bus owners keep their own code and call
the tap where they already do the work. Nothing moves out of the masters; the masters gain one or two tap
calls at their existing exchange and select points.

## 2. Event model (`core/src/emulator/bus/bustraceevent.h`)

Transaction level, not wire level: the emulator does not run wires on any of these buses, and a
transaction is what a driver writer reasons about. Wires are synthesized only for export (§8).

```cpp
enum class BusProtocol : uint8_t { Spi, I2c, Uart, Ps2, Ata, Fdc, Parallel };
enum class BusSource   : uint8_t { Cpu, Dma, CardCpu, Host };

enum class BusEventKind : uint8_t
{
    // SPI transaction level (the master)
    SpiSelect, SpiDeselect, SpiByte, SpiEarly, SpiTruncated,
    // SD protocol level (the card)
    SdCommand, SdReadBlock, SdWriteBlock,
};

struct BusTraceEvent          // 40 bytes, trivially copyable
{
    uint64_t seq; uint32_t epoch; uint32_t frame; uint32_t t; uint16_t pc;
    BusEventKind kind; BusSource source; uint8_t slot;
    uint8_t a;           // SpiByte / SpiEarly: MOSI; SdCommand: index
    uint8_t b;           // SpiByte: MISO; SdCommand: R1 (0xFF none)
    uint8_t flags;       // read (the byte was clocked by a read), ACMD, CRC valid, write ok
    uint32_t arg;        // SdCommand: argument
    uint64_t block;      // SdReadBlock / SdWriteBlock
};
```

`BusFinding` carries the rule (`BusRule`), severity (`error`, `warning`, `info`), the seq of the event it
belongs to (the event is recorded even when tracing is off: the finding then names the last seq),
frame / T / PC / slot and a short static detail. Names (`BusEventKindName`, `BusRuleName`,
`SdCommandName`) live beside the enums so every plane prints the same words.

## 3. Components

### 3.1 `BusTrace` (`bustrace.h/.cpp`)

A ring of `BusTraceEvent` plus a ring of `BusFinding`, each with capacity, size, evicted and last seq. The ring
is allocated on enable and freed on disable (coding-guidelines: lazy diagnostic buffers). Query structs
`BusEventQuery {kinds, slot, since, frameFrom, frameTo, limit}` and `BusFindingQuery {rules, since,
limit}`, and `...FromStrings` parsers shared by every plane (the `NextSdJournalQueryFromStrings` pattern).

Time order (the `MachineEventJournal` rule): an event earlier than the newest one in the same epoch means a
TTD seek followed by live running; the later events are dropped first and counted as `rewound`. The owner
does not record while `ttdReplayActive`.

### 3.2 `BusTap` (`bustap.h/.cpp`)

One per bus, owned by the bus master's owner (port decoder, card), registered with the manager on
`Attach(context)` and unregistered in its destructor.

```cpp
class BusTap
{
public:
    BusTap(std::string id, BusProtocol protocol, std::string label);
    ~BusTap();                                       // unregisters
    void Attach(EmulatorContext* context);           // registers with context->pBusManager
    bool Active() const { return _active; }          // trace or checks on: the one hot-path test

    void SetStamp(std::function<void(BusStamp&)>);   // frame / T / PC / source; default: the main Z80
    void SetDevice(uint8_t slot, std::string name);
    void SetByteTimeNs(double ns);                   // for export and the report
    void SetSource(BusSource s);                     // a DMA brackets its exchanges

    // SPI, called by masters only when Active()
    void SpiSelect(uint8_t slot, bool selected, bool byteInFlight);
    void SpiByte(uint8_t slot, uint8_t mosi, uint8_t miso, bool read);
    void SpiEarly(uint8_t slot, bool write, uint8_t value);
    void SpiTruncated(uint8_t slot);
    void SpiDmaCollision(bool write, uint8_t value);
    // SD protocol and card-side checks, called by SdCardSpi
    void Sd(uint8_t slot, const SdTraceEvent& event);
    void Finding(BusRule rule, uint8_t slot);

    // control and reads (any thread)
    void Control(int enable, int checks, bool clear, size_t capacity);
    BusTapStatus Status() const;
    std::vector<BusTraceEvent> Events(const BusEventQuery&) const;
    std::vector<BusFinding> Findings(const BusFindingQuery&) const;
};
```

The tap holds its own mutex for the rings; the hot path never takes it when inactive.

### 3.3 `BusManager` (`busmanager.h/.cpp`)

Owned by the `Emulator`, created beside the `MediaManager` (`emulator.cpp:348`), pointer in
`EmulatorContext::pBusManager`. Registry `std::vector<BusTap*>` under a mutex; `With(id, fn)` runs `fn`
under the registry lock so a tap cannot unregister (model switch, card removed) while a reader uses it.
`NextEpoch()` is called from `Emulator::ResetNow` after `_core->Reset()`. `ForEach` serves `bus = all`.

## 4. Taps in the four masters

| Master | Select | Byte | Early / truncated | Source / stamp |
|:--|:--|:--|:--|:--|
| `ZControllerSpi` (`zcontrollerspi.cpp`) | `WriteConfig`, per slot whose line changed | `WriteData`, `ReadData` (after the exchange) | never (instant; physically impossible on the Evo FPGAs) | owner (TS-Conf / ATM3): main Z80; the TS-Conf DMA callback brackets with `SetSource(Dma)` |
| Next (`portdecoder_next.cpp` `SpiSelect`, `SpiWrite`, `SpiRead`) | `SpiSelect` (byte in flight known) | at the exchange | the `_spiTooFast` branches | main Z80 |
| `DivMmcPaging` (`divmmcpaging.cpp`) | `SpiSelect` | `#EB` in / out | never (instant) | main Z80 |
| `NeoGSSpi` (`neogsspi.cpp`), three taps `neogs.sd`, `neogs.mp3.sci`, `neogs.mp3.sdi` | `applySelects` | `deliver` | `start` with a byte pending: `SpiEarly` + `SpiTruncated` | the card: host frame, GS CPU PC; the NeoGS DMA brackets with `SetSource(Dma)` |

`SpiTruncated` is the event; the finding is `early_access` (the access that cut the byte off).

`dma_collision` (TS-Conf only): the port handler for `#57` / `#77` already catches the engine up
(`CatchUpEngine`, `portdecoder_tsconf.cpp:565,639,644`); if after that an SPI DMA is still active
(`TsConfDma::Busy()` and device `SpiIn` / `SpiOut`), the CPU access collides with it on the real board.

## 4a. ATA (B6a, pulled forward for DSS)

`AtaChannel` (every IDE board: Nemo, ATM, TS-Conf, Sprinter) gets a tap from `IdeController` (`ide0`, `ide1`, devices
`master` / `slave`). Events: `reg_read` / `reg_write` (register, value), `ata_command` (the byte written to register 7,
named `READ SECTORS (#20)`), `data` (one word, read or write); the slot is the unit the device register selects. Checks
against the selected unit's status, read without side effects (`AtaDevice::State().status`): `ata_write_while_busy` (a
task-file write other than device control while BSY), `ata_command_drops_data` (a command while DRQ),
`ata_data_without_drq` (the data register while DRQ is clear). The emulated drive is BSY only during a software reset,
so the first rule fires for a driver that does not wait after SRST; a timing profile for drives (BSY during commands)
would widen it (later).

## 5. Card-side protocol events and checks (`SdCardSpi`)

`SdCardSpi::SetBusTap(BusTap*, uint8_t slot)`. The card already builds `SdTraceEvent` for its single
listener (`setTraceListener`, used by the Next journal); the tap is a second consumer at the same points,
so the Next journal is untouched (FR-10). Card-side rules:

| Rule | Where | Condition |
|:--|:--|:--|
| `deselect_mid_command` | `select(false)` | `_cmdLength` in 1..5 |
| `command_while_busy` | first byte of a command (`0x40..0x7F` with `_cmdLength == 0`) | write busy still pending (§6 delay in progress with value `#00`) |
| `command_drops_answer` | `onCommandBody` before `_out.clear()` | the queue still holds a data token or data block (a read not fully clocked out); CMD12 in a stream and CMD0 excluded |
| `crc_error` | `respondR1` | R1 has the CRC error bit |

None of them changes what the card does (NFR-3): the ideal card keeps forgiving; the finding says that a
real card would not.

## 6. SD card timing profiles

Today every delay is bytes pushed into `_out`. The change keeps `_out` for answer bytes and adds one
pending delay applied after `_out` drains:

```cpp
struct Delay { uint64_t untilUs; uint32_t bytesLeft; uint8_t value; uint8_t then; uint64_t block; };
// value #FF (wait for a token) or #00 (busy); then: nothing, or queue `block` (the data token + block)
```

While a delay is pending each exchange returns `value`. With a clock (`SetClock(std::function<uint64_t()>
nowUs)`, emulated time from the host: `PortDecoder::EmulatedMicroseconds()` on the main machine, card
ticks / 120 on NeoGS) the delay ends at `untilUs`, so a driver that deselects and waits sees a real card's
behaviour; without a clock (unit tests) it counts `bytesLeft`. The `ideal` profile uses byte counts that
reproduce today's stream exactly (1 NCR byte, 8 bytes to the token, 64 busy bytes, 4 ACMD41 polls), so
every existing test stays green.

| Parameter | ideal | realistic (seeded) | slow (specification worst case) |
|:--|:--|:--|:--|
| NCR (bytes before R1) | 1 | 1..8 | 8 |
| Read: to the data token | 8 bytes | 100 us .. 2 ms | 100 ms |
| CMD18: between blocks | 8 bytes | 8..64 bytes | 64 bytes |
| Write: busy after the data response | 64 bytes | 250 us .. 20 ms | 250 ms |
| CMD12 / stop-tran busy | 4 / 64 bytes | 100 us .. 2 ms | 100 ms / 250 ms |
| Leaves idle (ACMD41) | after 4 polls | 50 .. 500 ms after the first ACMD41 | 1 s |

Without a clock the time values become bytes at 1 us per byte. The random generator is a 64-bit
xorshift seeded by the profile's seed; its state is saved with the card (state layout version 2, the spare
bytes after the `out` area; version 1 loads as `ideal`). Setting a profile is a machine change for
automation; the profile survives card insert / eject (it belongs to the slot's card, not to the medium).

## 7. Automation (one implementation, five planes)

`DeviceState` (`core/src/emulator/state/devicestatebus.cpp`, declared in `devicestate.h`):

```cpp
StateNode BusList(EmulatorContext*);
StateNode BusEvents(EmulatorContext*, const std::string& bus, const BusEventQuery&);
StateNode BusFindings(EmulatorContext*, const std::string& bus, const BusFindingQuery&);
StateNode BusControl(EmulatorContext*, const std::string& bus, int enable, int checks, bool clear, size_t capacity);
StateNode BusSdTiming(EmulatorContext*, const std::string& bus, int slot, const std::string& profile, int64_t seed);
std::string BusExport(EmulatorContext*, const std::string& bus, const std::string& format, const BusEventQuery&, std::string& error);
```

JSON of `BusList`: `{buses:[{id, protocol, label, byte_time_ns, devices:[{slot, name, selected}],
trace:{enabled, size, capacity, evicted, rewound, last_seq}, checks:{enabled, findings, by_rule:{...}}}]}`.
`BusEvents`: the bus header plus `events:[{seq, frame, t, pc, source, slot, kind, ...}]` where an SPI byte
has `mosi`, `miso`, `read`; an SD command `command` (`CMD17`, `ACMD41`), `argument`, `r1`, `crc_valid`; a
block `block`, `ok`. `BusFindings`: `findings:[{seq, event_seq, frame, t, pc, slot, rule, severity,
detail}]`.

| Plane | Where |
|:--|:--|
| WebAPI | `core/automation/webapi/src/api/bus_api.cpp`, routes in `emulator_api.h` (§3 of requirements); OpenAPI `openapi/openapi_bus.inc` |
| MCP | tool `bus_analyzer` (`core/automation/mcp/src/mcp-bus.cpp`, registered in `BuildFullRegistry`): actions `list`, `enable`, `disable`, `clear`, `events`, `findings`, `sd_timing`, `export`; text one line per bus / event / finding with every field |
| CLI | `bus` command (`core/automation/cli/src/commands/cli-processor-bus.cpp`) |
| Lua / Python | `bus_list`, `bus_events`, `bus_findings`, `bus_control`, `bus_sd_timing`, `bus_export` (`lua_emulator.h`, `python_emulator.h`) |
| Qt | none in this work; the logic-analyzer widget `W.logic` of the debugger roadmap reads the same `DeviceState` later |

Docs touched with the code: `docs/emulator/design/control-interfaces/command-interface.md` (one matrix row),
`webapi-interface.md`, `cli-interface.md`, `lua-interface.md`, `python-interface.md`,
`docs/features/mcp/README.md`, the recipes ([agent-recipes.md](agent-recipes.md)).

## 8. Export

- **JSON lines:** one `BusTraceEvent` per line with every field, the bus header first.
- **VCD:** a 1 ns timescale; signals `cs<slot>`, `sck`, `mosi`, `miso` per bus. Each `SpiByte` becomes 8 SCK
  periods at the bus's byte time starting at the event's time (frame x frame length + T, converted with the
  machine's CPU clock); select / deselect become CS edges. Opens in GTKWave and PulseView; PulseView's SPI
  and SD-card-SPI decoders run on it.
- sigrok `.sr` sessions and triggers / pre-trigger capture are B5b (later).

## 9. TTD, snapshots, threading, cost

- **TTD:** taps do not record while `ttdReplayActive`; a seek followed by live running rewinds the ring
  (§3.1). The trace itself is not part of any snapshot or TTD state (it observes). The card's timing
  profile and pending delay are part of the card's state (`SdCardSpi::saveState`, version 2).
- **Threading:** the emulator thread appends under the tap's mutex only when active; WebAPI / MCP threads
  read copies through `BusManager::With`. Registration and unregistration take the manager's mutex.
- **Cost off:** `if (_tap.Active()) [[unlikely]]` per byte / select. SPI is not on the CPU's hot path
  (only when a program talks to a card); the NeoGS DMA streams through `deliver`, so B1 measures the NeoGS
  MP3 playback benchmark A/B (performance-guidelines).

## 10. Changes per file

| File | Change |
|:--|:--|
| `core/src/emulator/bus/*` (new) | `bustraceevent.h/.cpp`, `bustrace.h/.cpp`, `bustap.h/.cpp`, `busmanager.h/.cpp`, `busexport.cpp` |
| `core/src/emulator/emulatorcontext.h`, `emulator.cpp` | `pBusManager`; create / delete beside the media manager; `NextEpoch` in `ResetNow` |
| `core/src/emulator/io/spi/zcontrollerspi.h/.cpp` | a `BusTap*` (set by the owner); taps in `WriteConfig`, `WriteData`, `ReadData` |
| `core/src/emulator/ports/models/portdecoder_tsconf.h/.cpp`, `portdecoder_atm3.h/.cpp` | own `BusTap _spiTap{"zc.spi"}`; attach; DMA source bracket and `dma_collision` (TS-Conf) |
| `core/src/emulator/ports/models/portdecoder_next.h/.cpp` | `BusTap _spiTap{"next.spi"}`; taps in `SpiSelect` / `SpiWrite` / `SpiRead` |
| `core/src/emulator/io/divmmc/divmmcpaging.h/.cpp` | `BusTap _spiTap{"divmmc.spi"}` |
| `core/src/emulator/sound/chips/neogs/neogsspi.h/.cpp`, `soundchip_neogs.*` | three taps, GS stamp, DMA bracket |
| `core/src/emulator/io/sdcard/sdcardspi.h/.cpp` | `SetBusTap`, `SetClock`, timing profile, delay, card-side checks, state v2 |
| `core/src/emulator/state/devicestate.h`, `devicestatebus.cpp` (new) | `DeviceState::Bus*` |
| WebAPI, OpenAPI, MCP, CLI, Lua, Python | §7 |
| `core/tests/emulator/bus/*` (new), `core/tests/automation/*` | unit and machine tests (§11) |
| `tools/verification/mcp/test_bus.py` (new) | live MCP block |

## 11. Tests (written first)

| Test | What |
|:--|:--|
| `BusTrace_Test` | ring, capacity, evicted, query filters, rewind on time going back, epoch |
| `BusTap_Test` | inactive costs nothing (no allocation), enable / disable / clear, findings, manager register / unregister / `With` |
| `ZControllerSpiTap_Test` | select / byte events, slots, the DMA source |
| `SdCardSpiChecks_Test` | each card-side rule fires on the bad sequence and stays quiet on the good one |
| `SdCardSpiTiming_Test` | ideal stream byte-identical to before; realistic deterministic by seed; slow meets ACC-6; clocked delays end on time across a deselect; state v2 round trip, v1 load |
| `TsConfBus_Test` | a Z80 program on TS-Conf: SD boot sequence in the trace (ACC-1); DMA collision (ACC-2); busy violation (ACC-3) |
| `NextBus_Test`, `NeoGSBus_Test`, `DivMmcBus_Test` | ACC-4, ACC-5, the DivMMC trace |
| `BusExport_Test` | VCD header, edges of one byte, JSON lines |
| `McpBus_Test` | every action's text and structured content with `FakeApiCaller` |
| `OpenApiBus_Test` | every route in the manifest |
| `tools/verification/mcp/test_bus.py` | live: TS-Conf, enable, run, events and findings text |
