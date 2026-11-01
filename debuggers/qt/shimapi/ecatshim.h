// unreal-debugger: the part of eCat3's core API its debugger views call (src/emulator/core.h, emulator.h, disasm.h of
// ecat3 dev 27a98108), declared over nothing. The adapter (adapter/unrealmachine.cpp) implements it over
// unreal-ng's core. Everything lives in namespace ecatshim, so these Emulator / CPU / Memory classes never meet
// unreal-ng's own in one program: the ecat sources see them through the using-directive of emulator/core.h,
// the adapter names them with the namespace.

#pragma once

#include <cstdint>
#include <functional>
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
    virtual bool has_temp_break() const { return false; }

    // Not in ecat: the windows of the address space in order, empty when it is not paged
    virtual std::vector<AddressWindow> get_windows() { return {}; }
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
    virtual unsigned int debug_serial() const = 0;
    virtual CPU * debug_stop_cpu() const = 0;
    virtual int debug_stop_reason() const = 0;
    virtual void set_debug_listener(const std::function<void()> &fn) = 0;
    // A remote machine's connection: false while its emulator is gone and it waits for a suitable one (it reattaches
    // by itself, the listener tells both). The last state stays readable; nothing runs, steps or changes
    virtual bool debug_online() const { return true; }
    // The status text while offline ("No emulator: waiting for one at 127.0.0.1:8090")
    virtual std::string debug_offline_text() const { return std::string(); }

    virtual std::string read_setup(const std::string &section, const std::string &key, const std::string &def) = 0;
    virtual void write_setup(const std::string &section, const std::string &key, const std::string &value) = 0;
    virtual void record_command(const std::string &, const std::string &, const std::string &) {}
    virtual std::string get_last_path() = 0;
    virtual void set_last_path(const std::string &path) = 0;
    // The machine's character set: a ZX Spectrum shows ASCII, 7F the copyright sign
    virtual std::string translate_char(unsigned int code) = 0;
    virtual int char_code(const std::string &text) = 0;
};

} // namespace ecatshim
