// unreal-debugger: the part of eCat3's core API its debugger views call (src/emulator/core.h, emulator.h, disasm.h of
// ecat3 dev 27a98108), declared over nothing. The adapter (adapter/unrealmachine.cpp) implements it over
// unreal-ng's core. Everything lives in namespace ecatshim, so these Emulator / CPU / Memory classes never meet
// unreal-ng's own in one program: the ecat sources see them through the using-directive of emulator/core.h,
// the adapter names them with the namespace.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace emulator {
// ecat's Result: only the "ok" the views construct
struct Result {
    static Result ok() { return Result(); }
};
}

namespace ecatshim {

// CPU::m_debug of ecat: what the processor's debugger hooks do
enum { DEBUG_OFF = 0, DEBUG_BRAKES, DEBUG_STEP, DEBUG_STOPPED };

enum DisasmFlow {
    FLOW_NONE = 0,
    FLOW_JUMP,
    FLOW_CALL,
    FLOW_RET
};

struct DisasmLine {
    unsigned int length = 0;
    std::string text;
    bool valid = false;
    DisasmFlow flow = FLOW_NONE;
    bool conditional = false;
    bool has_target = false;
    unsigned int target = 0;
    std::string label;          // the symbol at this address, if any
};

typedef uint8_t (*CommandBytes)[15];

class DisAsm {
public:
    unsigned int max_command_length = 4;
    virtual ~DisAsm() {}
    virtual unsigned int decode(CommandBytes bytes, unsigned int PC, unsigned int max_len, DisasmLine * out) = 0;
    virtual unsigned int unit_bytes() const { return 1; }
    //The labels' column: as wide as the longest name (0: no labels, no column); the serial moves when they change
    virtual unsigned int label_chars() { return 0; }
    virtual unsigned int labels_serial() { return 0; }
};

struct CpuRegister {
    std::string name;       // empty: a separator row
    unsigned int value = 0;
    unsigned int bits = 0;
};

struct DeviceFieldInfo {
    std::string name;
    bool ranged = false;
};

struct DeviceFieldValue {
    bool numeric = false;
    unsigned int width = 0;
    std::vector<unsigned int> values;
    std::string text;
};

// A device's state as the Devices tab shows it: a named value with the parts under it (a report's objects and lists)
struct DeviceNode {
    std::string name;
    std::string value;
    std::vector<DeviceNode> children;
};

class ComputerDevice {
public:
    std::string name;
    std::string type;
    virtual ~ComputerDevice() {}
    virtual unsigned int radix() const { return 16; }
    virtual std::vector<DeviceFieldInfo> get_device_fields() { return {}; }
    virtual bool get_field(const std::string &, unsigned int, unsigned int, DeviceFieldValue &) { return false; }
    // The Devices tab's entry: the title in name, the one-line summary in value; the parts only when @p deep (an open
    // entry). False: not a Devices tab entry (a CPU, a memory: tabs of their own)
    virtual bool get_tree(bool deep, DeviceNode &out) { (void)deep; (void)out; return false; }
};

class AddressableDevice : public ComputerDevice {
public:
    virtual unsigned int get_size() = 0;
    // A side-effect-free read
    virtual unsigned int get_direct(unsigned int address) = 0;
    virtual void set_value(unsigned int address, unsigned int value, bool force = false) = 0;
    virtual bool debug_writable(unsigned int) { return true; }
};

// Not in ecat: one device's history as the core records it (a controller's commands, a chip's register writes, a
// card's events), the History tab's source; not a Devices tab entry. A processor's executed instructions come
// through its CPU (get_history), these never name one
class History : public ComputerDevice {
public:
    struct Summary {
        std::string what;       // "FDC commands"
        bool recording = false;
        uint64_t kept = 0;
        uint64_t written = 0;
    };
    // The registry's name of the device ("beta128", "zxbus.1.ports")
    virtual std::string device() const = 0;
    virtual Summary summary() = 0;
    // The newest @p count records, oldest first, a line each
    virtual std::vector<std::string> records(unsigned int count) = 0;
    // A ring on or off; what the debugger turned on it turns off when it goes
    virtual void set_recording(bool on) = 0;
};

// Not in ecat: a memory made of pages a processor maps into its windows (RAM, ROM, a card's RAM). The debugger
// groups them under one tab, a page at a time
class Memory : public AddressableDevice {
public:
    // 0: not paged
    virtual unsigned int page_size() { return 0; }
    // What the page holds now: a ROM by its signature ("128k with TR-DOS in menu"), a RAM page's use ("screen 0");
    // empty when nothing particular
    virtual std::string page_title(unsigned int) { return std::string(); }
    // Its slot in the model's layout ("48K BASIC ROM"), empty when the model has none
    virtual std::string page_role(unsigned int) { return std::string(); }
};

// Not in ecat: an event breakpoint of a processor (the core's BRK_EVENT): a sound card's event or protocol step, a
// machine's event, by the core's name of the kind (gsdebughooks.h GSCardEventName: "card_command", "host_command",
// "int_accept"). The card's side is the card CPU's, the host's side and the machine's are the main CPU's
struct EventBreakpoint {
    unsigned int id = 0;        // the breakpoint's id among its CPU's breakpoints
    std::string kind;
    unsigned int value = 0;     // the event's byte, compared where mask has bits
    unsigned int mask = 0;      // 0: any value
    bool enabled = true;
};

// Not in ecat: one window of a processor's address space and the page mapped there now
struct AddressWindow {
    unsigned int start = 0;
    unsigned int size = 0;
    std::string kind;           // "ROM", "RAM", ...; empty: nothing answers there
    unsigned int page = 0;
    std::string name;           // what the page holds: a ROM's title found by its signature, "screen 0"
    std::string role;           // the page's role in the model's ROM layout
    bool switchable = false;    // the machine's ports can map another page there
    bool writable = true;
    AddressableDevice * device = nullptr;   // the paged memory the page is part of, and where
    unsigned int offset = 0;
};

class MemoryMapper : public AddressableDevice {
public:
    // The device that answers the address and the offset in it, null when none
    virtual AddressableDevice * peek_owner(unsigned int address, unsigned int &offset) = 0;
    virtual void poke(unsigned int address, unsigned int value) { set_value(address, value, true); }
    virtual bool writable(unsigned int address) { return debug_writable(address); }
};

class CPU : public ComputerDevice {
public:
    int m_debug = DEBUG_OFF;
    MemoryMapper * mm = nullptr;

    virtual unsigned int get_pc() = 0;
    virtual unsigned int peek_mem(unsigned int address) = 0;
    virtual bool poke_mem(unsigned int address, unsigned int value) = 0;
    virtual bool mem_writable(unsigned int) { return true; }
    virtual DisAsm * create_disasm(const std::string &data_path, emulator::Result &res) = 0;

    virtual std::vector<CpuRegister> get_register_values() = 0;
    virtual std::vector<std::pair<std::string, std::string>> get_flags() = 0;
    virtual void set_context_value(const std::string &name, unsigned int value) = 0;
    virtual bool set_flag(const std::string &, bool) { return false; }

    virtual bool stack_address(int, unsigned int &) { return false; }
    virtual unsigned int stack_entry_bytes() const { return 2; }
    virtual std::vector<unsigned int> get_history() { return {}; }

    virtual bool has_breakpoint(unsigned int address) = 0;
    virtual void add_breakpoint(unsigned int address) = 0;
    virtual void remove_breakpoint(unsigned int address) = 0;
    virtual void clear_breakpoints() = 0;
    // A copy: ecat returns a reference to its own vector, the views only read it
    virtual std::vector<unsigned int> get_breakpoints() = 0;
    // Not in ecat: the condition of the execution breakpoint at the address (the Unreal condition language), as
    // reverse-continue takes it; empty: it stops there whatever holds (no condition, or one of them has none)
    virtual std::string breakpoint_condition(unsigned int) { return {}; }
    virtual bool has_temp_break() const { return false; }

    // Not in ecat: event breakpoints (EventBreakpoint). The kinds this CPU's breakpoints take on this machine, in the
    // core's names (none: it has no event breakpoints)
    virtual std::vector<std::string> event_kinds() { return {}; }
    virtual std::vector<EventBreakpoint> get_event_breakpoints() { return {}; }
    // False with the reason in @p error (a kind of another CPU, a value out of range)
    virtual bool add_event_breakpoint(const std::string &kind, unsigned int value, unsigned int mask, std::string &error)
    {
        (void)kind;
        (void)value;
        (void)mask;
        error = "this processor has no event breakpoints";
        return false;
    }
    virtual bool remove_event_breakpoint(unsigned int id) { (void)id; return false; }
    virtual bool enable_event_breakpoint(unsigned int id, bool on) { (void)id; (void)on; return false; }

    // Not in ecat: the windows of the address space in order, empty when it is not paged
    virtual std::vector<AddressWindow> get_windows() { return {}; }

    // Not in ecat: a sound card CPU's own clock (gs-debugger A4, P5.2; the `gs` report's cpu.frame_cycle): its cycle
    // in the ZX frame `frame`, and its cycles since the card started when known (-1: not known, the change is then
    // taken within one frame). False: no such clock (the main CPU, a card without a CPU clock)
    struct CardCycle {
        int64_t frame_cycle = -1;
        int64_t total = -1;
        uint64_t frame = 0;
        unsigned int clock_hz = 0;
    };
    virtual bool card_cycle(CardCycle &out) { (void)out; return false; }
};

struct DeviceEntry {
    std::shared_ptr<ComputerDevice> device;
};

class DeviceManager {
public:
    unsigned int device_count = 0;
    std::vector<DeviceEntry> entries;

    void add(const std::shared_ptr<ComputerDevice> &d)
    {
        entries.push_back(DeviceEntry{d});
        device_count = (unsigned int)entries.size();
    }
    DeviceEntry * get_device(unsigned int i) { return &entries[i]; }
    std::vector<CPU *> get_cpus()
    {
        std::vector<CPU *> r;
        for (DeviceEntry &e : entries)
            if (CPU * c = dynamic_cast<CPU *>(e.device.get())) r.push_back(c);
        return r;
    }
};

class Emulator {
public:
    enum { DSTOP_NONE = 0, DSTOP_PAUSE, DSTOP_STEP, DSTOP_BREAKPOINT, DSTOP_TEMP, DSTOP_ERROR, DSTOP_NO_PROGRESS };

    bool loaded = false;
    DeviceManager * dm = nullptr;
    std::string data_path;

    virtual ~Emulator() {}

    // ecat runs fn on the emulation thread between two slices. Here every operation the views call inside it
    // carries its own synchronisation (tool edits, breakpoint manager, direct steps), so fn runs where it is
    virtual void invoke(const std::function<void()> &fn) { fn(); }

    virtual void debug_attach(bool on) = 0;
    virtual void debug_detach() = 0;
    virtual bool debug_paused() const = 0;
    virtual bool debug_stepping() const = 0;
    virtual void debug_pause() = 0;
    virtual void debug_run(bool breakpoints) = 0;
    virtual void debug_step(CPU * c, unsigned int count = 1) = 0;
    virtual void debug_step_over(CPU * c) = 0;
    virtual void debug_step_out(CPU * c) = 0;
    virtual void debug_run_to(CPU * c, unsigned int address) = 0;
    // A card CPU's run to a moment (not in ecat; gs-debugger P5.4, RunControl::CardRunTo), the machine paused: `until`
    // "cycle" (to card cycle `value` of the frame), "int" (the card's next interrupt), "command" (the host's next
    // command to the card) or "main_t" (the main CPU to T `value` of the frame, the card following it lazily). It
    // returns when the run has ended: the core's answer
    struct CardRunAnswer {
        bool ok = false;          // it ran (got there or not); false: refused, nothing ran
        bool hit = false;         // it got there
        std::string message;      // the core's sentence (where it stopped, or why it did not run)
        std::string stop_reason;  // step, breakpoint, timeout, pause
        int64_t card_cycle = -1;  // where the card stands now: its cycle of the frame (-1: not known)
        int64_t main_t = -1;      // the main CPU's T of the frame
        int64_t frame = -1;
    };
    virtual CardRunAnswer debug_card_run_to(CPU * c, const std::string &until, uint64_t value)
    {
        (void)c;
        (void)until;
        (void)value;
        CardRunAnswer a;
        a.message = "This machine has no card run";
        return a;
    }
    // The CPU of the tab shown: a card's follows the main CPU's steps of the paused machine (its state at the main
    // CPU's moment); the main CPU's ends that
    virtual void debug_select(CPU * c) { (void)c; }
    virtual unsigned int debug_serial() const = 0;
    virtual CPU * debug_stop_cpu() const = 0;
    virtual int debug_stop_reason() const = 0;
    // The last stop's number, one more at every stop: an event that finds it unchanged (a move in time, an edit, the
    // stop's own event delivered late) is no new stop, and brings no processor's tab to the front
    virtual uint64_t debug_stop_serial() const = 0;
    // A card CPU's stop: where the main CPU stood at the card's moment, its instruction's progress
    // ("main #8123: T 9 of 11, port write; 2 T past the card"); empty for any other stop
    virtual std::string debug_stop_main_text() const { return std::string(); }
    virtual void set_debug_listener(const std::function<void()> &fn) = 0;
    // A remote machine's connection: false while its emulator is gone and it waits for a suitable one (it reattaches
    // by itself, the listener tells both). The last state stays readable; nothing runs, steps or changes
    virtual bool debug_online() const { return true; }
    // The status text while offline ("Waiting for PENTAGON at 127.0.0.1:8090")
    virtual std::string debug_offline_text() const { return std::string(); }

    virtual std::string read_setup(const std::string &section, const std::string &key, const std::string &def) = 0;
    virtual void write_setup(const std::string &section, const std::string &key, const std::string &value) = 0;
    virtual void record_command(const std::string &, const std::string &, const std::string &) {}
    virtual std::string get_last_path() = 0;
    virtual void set_last_path(const std::string &path) = 0;
    // The machine's character set: a ZX Spectrum shows ASCII, 7F the copyright sign
    virtual std::string translate_char(unsigned int code) = 0;
    virtual int char_code(const std::string &text) = 0;

    // The machine's time travel (not in ecat; TTD branches debuggers.md §4): a TTDControl verb with its options as
    // text, the answer as JSON text (the verb's body, or {error, message}); ok false on a refusal
    struct TtdAnswer {
        bool ok = false;
        std::string json;
    };
    // A device board (not in ecat; gs-debugger P5.5, debugger-model widget catalog §4): the core's DebugBoards::Read
    // (over the WebAPI GET /debug/boards/{id}), its columns' groups in order. A control's value as text: numbers
    // decimal, true / false; its type (hex8, hex16, hex21, dec, counter, rate, bool, led, text, bits, enum, bar) says
    // how it reads; bits name their bits (bit 0 first), a bar has its max. ok false: no such board here (message why)
    struct BoardControl {
        std::string id, label, type, value, note;
        std::vector<std::string> names;
        long long max = 0;
    };
    struct BoardGroup {
        std::string title, raw;
        std::vector<BoardControl> controls;
    };
    struct Board {
        bool ok = false;
        bool available = false;
        std::string message, id, title;
        std::vector<BoardGroup> groups;
        std::vector<std::pair<std::string, std::string>> actions;  // id, title
        const BoardControl * control(const std::string &control_id) const
        {
            for (const BoardGroup &g : groups)
                for (const BoardControl &c : g.controls)
                    if (c.id == control_id) return &c;
            return nullptr;
        }
    };
    virtual Board read_board(const std::string &id)
    {
        Board b;
        b.id = id;
        b.message = "This machine has no device boards";
        return b;
    }
    // A board's action (DebugBoards::Action; over the WebAPI POST /debug/boards/{board}/actions/{action} {arg}):
    // neogs_sd insert <path: an image or a host folder> / eject (the media manager's slot sd.ngs), neogs_flash save.
    // ok false on a refusal, the core's words in message
    struct BoardAnswer {
        bool ok = false;
        std::string message;
    };
    virtual BoardAnswer board_action(const std::string &board, const std::string &action, const std::string &arg)
    {
        (void)board;
        (void)action;
        (void)arg;
        return {false, "This machine has no device boards"};
    }

    // The NeoGS ZX-DMA trace (not in ecat; gui-card-debugger B3.2, gs-debugger N6): the bytes the host moved through
    // the ZX-DMA window, a row a byte, oldest first: the core's DeviceState::GsZxDmaTrace (over the WebAPI GET
    // state/audio/gs/zxdma?last=&since=). Recorded while the GS port trace captures (zxdma_trace_capture). A poller
    // passes the last row's seq (or last_seq) as `since`; restarted: that seq was past the newest (a new session), the
    // rows are from the start. ok false: not read (message why); available false: no NeoGS (message the core's words)
    struct ZxDmaRow {
        uint64_t seq = 0;
        int64_t frame = 0;
        int64_t host_t = 0;         // the host's T-state of the frame at the access end (after the wait)
        int64_t card_time = 0;      // the card CPU's cycles
        bool write = false;         // the host wrote card RAM (false: read it)
        unsigned int card_address = 0;
        unsigned int page = 0;      // card_address >> 14
        unsigned int value = 0;
        unsigned int wait = 0;      // the /WAIT T-states
        unsigned int host_pc = 0;
        bool dropped = false;
    };
    struct ZxDmaTrace {
        bool ok = false;
        bool available = false;
        std::string message;
        std::string card, device;
        bool capturing = false;
        bool armed = false;
        uint64_t total_bytes_kept = 0;
        uint64_t evicted = 0;
        uint64_t last_seq = 0;
        bool restarted = false;
        uint64_t bytes_read = 0, bytes_written = 0, bytes_dropped = 0, wait_t_states = 0;
        std::vector<ZxDmaRow> rows;
        std::string text;
        std::string how_to_start;   // while not capturing: how to start it
    };
    virtual ZxDmaTrace read_zxdma_trace(size_t last, uint64_t since)
    {
        (void)last;
        (void)since;
        ZxDmaTrace t;
        t.message = "This machine has no ZX-DMA trace";
        return t;
    }
    // The GS port trace's capture, which records the ZX-DMA bytes: "start", "stop" or "clear" (in process the card's
    // start / stop / clearPortTrace; over the WebAPI POST control/audio/gs/porttrace {action})
    virtual BoardAnswer zxdma_trace_capture(const std::string &action)
    {
        (void)action;
        return {false, "This machine has no ZX-DMA trace"};
    }

    // The NeoGS ZX-DMA banner (not in ecat; gui-card-debugger S4, neogs-zxdma-design §6.2) for the main CPU's
    // listing and dump: the core's DeviceState::ZxDmaBanner (over the WebAPI the `gs` report's neogs.dma.zx.banner),
    // empty while the module is off or without a NeoGS. card_bytes: the module diverts with the ROM paged, the main
    // CPU's reads of window_start..window_end return card RAM at run time
    struct ZxDmaBanner {
        std::string text;
        bool card_bytes = false;
        unsigned int window_start = 0x0000;
        unsigned int window_end = 0x3FFF;
    };
    virtual ZxDmaBanner zxdma_banner() { return ZxDmaBanner(); }
    // How often the views read the card's line (and the banner) while the machine runs (not in ecat): a read of the
    // card where the machine stands still in this process, a plain GET over the WebAPI; never a pause
    virtual unsigned int live_card_ms() const { return 250; }

    // The GS protocol monitor (not in ecat; gui-card-debugger A5.3, gs-debugger P5.6): one read of a card's conversation
    // with the host for the GS monitor tab. `card` "gs" (the General Sound or NeoGS) or "ms" (the MultiSound's GS).
    // In process where the machine stands still (the `gs` report or the `multisound` report's gs part, the command log
    // history `<slot>.commands` read with `last`, the firmware's unknown commands); over the WebAPI plain GETs
    // (state/audio/gs or state/audio/multisound, history/<device>?last=N, state/audio/gs/firmware?object=
    // unknown_commands). The history read has no since cursor: the newest `last` records, oldest first
    struct GsMonitorRow {
        int64_t frame = 0;
        int64_t t = 0;
        int64_t card_cycle = 0;
        int command = -1;
        std::string kind, name, text;
        std::string anomaly;      // kind "anomaly": its name ("data_overwritten")
    };
    struct GsMonitor {
        bool ok = false;          // read (false: message why)
        std::string message;
        // The card's protocol (the gs report): the decoder's line, the status register and its bits, the mailbox
        bool protocol_on = false;            // the command log decodes (protocol.on)
        std::string protocol_text;           // protocol.text ("#2C Set current module: not taken yet")
        int status = -1;                     // the status register (-1: not read)
        bool command_pending = false, data_pending = false;
        int command_from_host = -1, data_from_host = -1, data_to_host = -1;
        bool has_last_send = false;          // the debugger's last send (last_send)
        int last_send_command = -1;
        std::string last_send_name, last_send_state;
        std::vector<int> last_send_replies;
        // The command log (the history)
        std::string log_device;              // "zxbus.1.commands"; empty: the card has none
        std::string log_mode;                // off, ring, capture
        uint64_t written = 0, kept = 0, dropped = 0;
        std::vector<GsMonitorRow> rows;      // the newest records, oldest first
        // The firmware's unknown commands (only with a firmware profile and the log on)
        bool unknown_present = false;
        uint64_t unknown_total = 0;          // every count summed
        std::vector<std::pair<int, uint64_t>> unknown;  // command, count
    };
    virtual GsMonitor read_gs_monitor(const std::string &card, size_t last)
    {
        (void)card;
        (void)last;
        GsMonitor m;
        m.message = "This machine has no GS monitor";
        return m;
    }
    // The command log of `card` on (history <device> config mode ring): the tab's Turn on (an explicit user action)
    virtual BoardAnswer gs_command_log(const std::string &card, bool on)
    {
        (void)card;
        (void)on;
        return {false, "This machine has no GS command log"};
    }

    // The GS analysis tabs (not in ecat; gui-card-debugger A5.2a Dispatch, Unknown, Objects; requirements M3, M4, M5,
    // AU6): one read of a card's firmware objects through its profile. In process GSFirmwareObjectsRead where the
    // machine stands still, with the `gs` report's firmware_profile and protocol (the profile on top, the #14 uploads);
    // over the WebAPI plain GETs of state/audio/gs/firmware[?profile=][&cpu=ms] and state/audio/gs (or
    // state/audio/multisound's gs part). The handlers' labels are the card CPU's own symbols (in process its label
    // manager; over the WebAPI GET labels?cpu=, kept a while)
    struct GsDispatchEntry {
        int command = 0;
        std::string name;              // the command's name in the profile ("" none)
        bool readable = true;          // the table's bytes could be read
        unsigned int handler = 0;
        std::string label;             // the card's symbol at the handler ("" none)
        bool has_baseline = false;     // the ROM's handler (the stock split COMTAB only)
        unsigned int baseline = 0;
        bool patched = false;          // differs from the ROM's
        std::string patched_by;        // the extension profile that names the command ("" none)
        bool unknown = false;          // the unknown-command stub, or a code the profile names no command for
    };
    struct GsUpload {                  // a #14 block the host sent (M5)
        unsigned int address = 0;
        uint64_t length = 0, matched_length = 0;
        std::string sha256;
        std::string profile;           // the profile whose upload match it is ("" unknown code)
        int64_t frame = 0;
        bool started = false;          // #13 jumped into it
    };
    struct GsStructTable {             // a struct instance of the profile (M3): columns from the schema
        std::string name, struct_name, at;
        int64_t count = 0, size = 0;
        std::vector<std::string> columns;            // the struct's fields
        std::vector<std::string> row_at;             // each row's place ("ram 2:#0100")
        std::vector<std::vector<std::string>> rows;  // the fields' values as text ("" unreadable)
    };
    struct GsVariable {
        std::string name, kind, at, meaning, reason;
        bool valid = false;
        int64_t value = 0;
    };
    struct GsFirmware {
        bool ok = false;
        std::string message;
        std::string profile, title, source;          // the profile the read used
        std::string rom_profile, rom_title;          // the ROM's (the gs report's firmware_profile; "" none)
        bool log_on = false;                         // the command log records (protocol.on)
        std::string top_profile;                     // the profile the decoder uses now (protocol.profile)
        std::vector<GsUpload> uploads;               // the #14 blocks up to now, oldest first
        bool has_dispatch = false;
        std::string table, table_high;               // the dispatch table's place(s)
        int64_t patched = 0, unknown = 0;
        std::vector<GsDispatchEntry> entries;
        bool unknown_present = false;                // the command log's counts are there (the log records)
        std::vector<std::pair<int, uint64_t>> unknown_commands;  // command, count (since the log started)
        std::vector<GsStructTable> structs;
        std::vector<GsVariable> variables;           // the profile's memory / register / operand variables
    };
    // `profile`: "" the card's active one, else the id to read the card's memory as
    virtual GsFirmware read_gs_firmware(const std::string &card, const std::string &profile)
    {
        (void)card;
        (void)profile;
        GsFirmware f;
        f.message = "This machine has no GS firmware objects";
        return f;
    }
    struct GsProfileInfo {
        std::string id, title, source;
        int64_t commands = 0, structs = 0, instances = 0, variables = 0;
    };
    // The firmware profiles known (built-in and files): GSFirmwareProfilesState, GET state/audio/gs/firmware/profiles
    virtual std::vector<GsProfileInfo> gs_firmware_profiles() { return {}; }

    // The Writers tab (gui-card-debugger A5.2a, requirements X7): the writes to the card's ports, oldest first. The
    // host's #B3 / #BB from the command log (its records carry the main CPU's PC; #33 bit 7 a reset marker), the
    // card's own OUTs and the host's #33 from the GS port trace (its events carry the card CPU's PC: for the host
    // side not the writer's). In process the trace's host and card sides read apart (getPortTraceSide); over the
    // WebAPI the trace's newest events (GET state/audio/gs/porttrace?events=N, the GS slot's card only)
    struct GsPortWrite {
        bool host = false;             // a host port (#B3 / #BB / #33); else the card's own port
        unsigned int port = 0;
        unsigned int value = 0;
        int64_t frame = 0;
        int64_t t = -1;                // the main CPU's T in the frame (command log), -1 unknown
        int64_t card_cycle = -1;       // the card's clock (port trace), -1 unknown
        bool has_pc = false;           // the writer's PC is known
        unsigned int pc = 0;
        std::string label;             // the writer CPU's symbol at the PC, "NAME" or "NAME+4"
        std::string source;            // "command log", "port trace"
        std::string kind;              // the command log's kind (command, param, data, marker); "" from the trace
    };
    struct GsWrites {
        bool ok = false;
        std::string message;
        std::string log_device, log_mode;            // the command log ("" none)
        bool trace_present = false;                  // the port trace could be read
        std::string trace_note;                      // why not, or how it was read
        bool trace_capturing = false;
        uint64_t trace_kept = 0, trace_evicted = 0;
        bool card_cpu = true;                        // the card has a CPU (the lightweight player has none)
        std::vector<GsPortWrite> writes;
    };
    virtual GsWrites read_gs_writes(const std::string &card)
    {
        (void)card;
        GsWrites w;
        w.message = "This machine has no GS port writes";
        return w;
    }
    // The GS port trace of `card` started / stopped (the Writers tab's Start; the trace records the card's ports)
    virtual BoardAnswer gs_port_trace(const std::string &card, const std::string &action)
    {
        (void)card;
        (void)action;
        return {false, "This machine has no GS port trace"};
    }

    virtual bool ttd_supported() const { return false; }
    virtual TtdAnswer ttd_call(const std::string &verb, const std::map<std::string, std::string> &options)
    {
        (void)verb;
        (void)options;
        return {false, R"({"error":"Not Implemented","message":"This machine has no time travel"})"};
    }
};

} // namespace ecatshim
