# Modular logging system for ZX-Spectrum emulator

**Created:** 1/31/2026 12:44:20  
**Updated:** 1/31/2026 12:45:48  
**Exported:** 1/31/2026 12:54:57  
**Link:** [https://claude.ai/chat/d7b3890d-d815-49f6-a3f8-990244027235](https://claude.ai/chat/d7b3890d-d815-49f6-a3f8-990244027235)  

## Prompt:
1/31/2026, 12:44:21 PM

Let's design modular logging system (C++, cross-platform) for zx-spectrum emulator. I want to use it mostly as realtime monitoring/diagnostics with ability to change settings in realtime - for example - turn off all loggers, but enabe just one responsible for FDC sector reads and see all events from there. then disable it.

Ideally - to have fast IPC so I can see everything in realtime without polluting filesystem



## Response:
1/31/2026, 12:45:48 PM

Great project! Let me think through the design for a real-time, modular logging system that fits your emulator debugging workflow.

## Design Goals

1. **Zero-cost when disabled** - critical for emulator performance
2. **Runtime reconfiguration** - toggle individual loggers without restart
3. **Fast IPC** - real-time viewing without file I/O
4. **Hierarchical categories** - e.g., `fdc.read`, `fdc.write`, `z80.interrupt`
5. **Cross-platform** - Windows, Linux, macOS

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      Emulator Process                       │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐         │
│  │Z80 Core │  │  FDC    │  │  AY     │  │  ULA    │  ...    │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘         │
│       │            │            │            │              │
│       ▼            ▼            ▼            ▼              │
│  ┌──────────────────────────────────────────────────┐       │
│  │              Logger Registry                     │       │
│  │  ┌────────────────────────────────────────────┐  │       │
│  │  │ Category Tree + Atomic Enable Flags        │  │       │
│  │  └────────────────────────────────────────────┘  │       │
│  └──────────────────────────────────────────────────┘       │
│                          │                                  │
│                          ▼                                  │
│  ┌──────────────────────────────────────────────────┐       │
│  │         Lock-free Ring Buffer (SPSC)             │       │
│  └──────────────────────────────────────────────────┘       │
│                          │                                  │
│                          ▼                                  │
│  ┌──────────────────────────────────────────────────┐       │
│  │    IPC Transport (Shared Memory / Unix Socket)   │       │
│  └──────────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                    Viewer Process                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │  Filter UI  │  │  Category   │  │  Real-time Display  │  │
│  │             │  │  Tree View  │  │  (scrolling log)    │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
│                          │                                  │
│                          ▼                                  │
│  ┌──────────────────────────────────────────────────┐       │
│  │         Control Channel (commands back)          │       │
│  └──────────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. Logger Category with Compile-time + Runtime Check

```cpp
// logger_category.h
#pragma once
#include <atomic>
#include <string_view>
#include <cstdint>

namespace emu::log {

// Compile-time category ID generation
constexpr uint32_t hash_category(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (char c : name) {
        hash ^= static_cast<uint32_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

enum class Level : uint8_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Off   = 255
};

// Per-category state - fits in cache line considerations
struct CategoryState {
    std::atomic<Level> level{Level::Off};
    std::atomic<bool> enabled{false};
    const char* name;
    uint32_t id;
    CategoryState* parent{nullptr};
    
    bool is_enabled(Level msg_level) const noexcept {
        // Fast path: atomic load with relaxed ordering
        return enabled.load(std::memory_order_relaxed) && 
               msg_level >= level.load(std::memory_order_relaxed);
    }
};

// Static category registration via constructor
class Category {
public:
    constexpr Category(const char* name, CategoryState& state)
        : state_(state), name_(name) {}
    
    bool is_enabled(Level l = Level::Trace) const noexcept {
        return state_.is_enabled(l);
    }
    
    CategoryState& state() const noexcept { return state_; }
    const char* name() const noexcept { return name_; }
    
private:
    CategoryState& state_;
    const char* name_;
};

} // namespace emu::log

// Macro for declaring categories - creates static storage
#define EMU_LOG_CATEGORY(var_name, category_path) \
    static ::emu::log::CategoryState var_name##_state{ \
        ::emu::log::Level::Off, false, category_path, \
        ::emu::log::hash_category(category_path), nullptr \
    }; \
    static ::emu::log::Category var_name{category_path, var_name##_state}
```

### 2. Lock-free Ring Buffer for Log Messages

```cpp
// ring_buffer.h
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>

namespace emu::log {

// Message header in ring buffer
struct alignas(8) MessageHeader {
    uint32_t category_id;
    uint32_t length;        // payload length
    uint64_t timestamp;     // TSC or steady_clock
    Level level;
    uint8_t reserved[7];
    // Followed by: char payload[length], padded to 8 bytes
};

static_assert(sizeof(MessageHeader) == 24);

// Single-Producer Single-Consumer lock-free ring buffer
// Designed to sit in shared memory
class SPSCRingBuffer {
public:
    static constexpr size_t DEFAULT_SIZE = 4 * 1024 * 1024; // 4MB
    
    struct Header {
        std::atomic<uint64_t> write_pos{0};
        std::atomic<uint64_t> read_pos{0};
        uint64_t capacity;
        uint64_t mask;
        // Padding to separate atomics to different cache lines
        char pad1[64 - 32];
    };
    
    // For creating in shared memory
    static size_t required_size(size_t capacity) {
        return sizeof(Header) + capacity;
    }
    
    // Initialize in existing memory region
    static SPSCRingBuffer* create(void* memory, size_t capacity) {
        auto* self = new (memory) SPSCRingBuffer();
        self->header_.capacity = capacity;
        self->header_.mask = capacity - 1;  // capacity must be power of 2
        self->data_ = reinterpret_cast<char*>(memory) + sizeof(Header);
        return self;
    }
    
    // Attach to existing (shared memory)
    static SPSCRingBuffer* attach(void* memory) {
        auto* self = reinterpret_cast<SPSCRingBuffer*>(memory);
        self->data_ = reinterpret_cast<char*>(memory) + sizeof(Header);
        return self;
    }
    
    // Producer: try to write a message, returns false if buffer full
    template<typename Formatter>
    bool try_write(uint32_t category_id, Level level, Formatter&& fmt) {
        // Format to temporary buffer first to know size
        thread_local char temp[4096];
        size_t len = fmt(temp, sizeof(temp));
        
        size_t total = sizeof(MessageHeader) + ((len + 7) & ~7ULL); // align to 8
        
        uint64_t write = header_.write_pos.load(std::memory_order_relaxed);
        uint64_t read = header_.read_pos.load(std::memory_order_acquire);
        
        if (write - read + total > header_.capacity) {
            return false; // Would overflow, drop message
        }
        
        // Write header
        MessageHeader hdr{};
        hdr.category_id = category_id;
        hdr.length = static_cast<uint32_t>(len);
        hdr.timestamp = __rdtsc(); // Or use std::chrono on non-x86
        hdr.level = level;
        
        write_bytes(write, &hdr, sizeof(hdr));
        write_bytes(write + sizeof(hdr), temp, len);
        
        header_.write_pos.store(write + total, std::memory_order_release);
        return true;
    }
    
    // Consumer: read next message, returns false if empty
    bool try_read(MessageHeader& hdr, char* payload, size_t payload_capacity) {
        uint64_t read = header_.read_pos.load(std::memory_order_relaxed);
        uint64_t write = header_.write_pos.load(std::memory_order_acquire);
        
        if (read >= write) {
            return false; // Empty
        }
        
        read_bytes(read, &hdr, sizeof(hdr));
        
        size_t to_read = std::min(static_cast<size_t>(hdr.length), payload_capacity - 1);
        read_bytes(read + sizeof(hdr), payload, to_read);
        payload[to_read] = '\0';
        
        size_t total = sizeof(MessageHeader) + ((hdr.length + 7) & ~7ULL);
        header_.read_pos.store(read + total, std::memory_order_release);
        return true;
    }
    
private:
    void write_bytes(uint64_t pos, const void* src, size_t len) {
        const char* s = static_cast<const char*>(src);
        for (size_t i = 0; i < len; ++i) {
            data_[(pos + i) & header_.mask] = s[i];
        }
    }
    
    void read_bytes(uint64_t pos, void* dst, size_t len) {
        char* d = static_cast<char*>(dst);
        for (size_t i = 0; i < len; ++i) {
            d[i] = data_[(pos + i) & header_.mask];
        }
    }
    
    Header header_;
    char* data_{nullptr};
};

} // namespace emu::log
```

### 3. Cross-platform Shared Memory IPC

```cpp
// ipc_transport.h
#pragma once
#include <string>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace emu::log {

class SharedMemoryRegion {
public:
    static constexpr const char* DEFAULT_NAME = "/emu_log_buffer";
    
    ~SharedMemoryRegion() { close(); }
    
    // Create new shared memory (producer side)
    bool create(const std::string& name, size_t size) {
        name_ = name;
        size_ = size;
        
#ifdef _WIN32
        handle_ = CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            static_cast<DWORD>(size >> 32),
            static_cast<DWORD>(size & 0xFFFFFFFF),
            name.c_str()
        );
        if (!handle_) return false;
        
        ptr_ = MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (!ptr_) {
            CloseHandle(handle_);
            handle_ = nullptr;
            return false;
        }
#else
        // Remove any stale shm
        shm_unlink(name.c_str());
        
        fd_ = shm_open(name.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd_ < 0) return false;
        
        if (ftruncate(fd_, size) < 0) {
            ::close(fd_);
            shm_unlink(name.c_str());
            return false;
        }
        
        ptr_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (ptr_ == MAP_FAILED) {
            ::close(fd_);
            shm_unlink(name.c_str());
            ptr_ = nullptr;
            return false;
        }
#endif
        is_owner_ = true;
        return true;
    }
    
    // Attach to existing shared memory (consumer side)
    bool attach(const std::string& name) {
        name_ = name;
        
#ifdef _WIN32
        handle_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (!handle_) return false;
        
        // Get size by mapping and reading header
        ptr_ = MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!ptr_) {
            CloseHandle(handle_);
            handle_ = nullptr;
            return false;
        }
#else
        fd_ = shm_open(name.c_str(), O_RDWR, 0666);
        if (fd_ < 0) return false;
        
        struct stat st;
        if (fstat(fd_, &st) < 0) {
            ::close(fd_);
            return false;
        }
        size_ = st.st_size;
        
        ptr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (ptr_ == MAP_FAILED) {
            ::close(fd_);
            ptr_ = nullptr;
            return false;
        }
#endif
        is_owner_ = false;
        return true;
    }
    
    void close() {
        if (!ptr_) return;
        
#ifdef _WIN32
        UnmapViewOfFile(ptr_);
        CloseHandle(handle_);
        handle_ = nullptr;
#else
        munmap(ptr_, size_);
        ::close(fd_);
        if (is_owner_) {
            shm_unlink(name_.c_str());
        }
#endif
        ptr_ = nullptr;
    }
    
    void* data() const { return ptr_; }
    size_t size() const { return size_; }
    
private:
    std::string name_;
    size_t size_{0};
    void* ptr_{nullptr};
    bool is_owner_{false};
    
#ifdef _WIN32
    HANDLE handle_{nullptr};
#else
    int fd_{-1};
#endif
};

} // namespace emu::log
```

### 4. Logger Registry and Control

```cpp
// logger_registry.h
#pragma once
#include "logger_category.h"
#include "ring_buffer.h"
#include "ipc_transport.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <string>

namespace emu::log {

class LoggerRegistry {
public:
    static LoggerRegistry& instance() {
        static LoggerRegistry reg;
        return reg;
    }
    
    // Called at startup
    bool initialize(const std::string& shm_name = SharedMemoryRegion::DEFAULT_NAME,
                    size_t buffer_size = SPSCRingBuffer::DEFAULT_SIZE) {
        size_t total = SPSCRingBuffer::required_size(buffer_size);
        if (!shm_.create(shm_name, total)) {
            return false;
        }
        buffer_ = SPSCRingBuffer::create(shm_.data(), buffer_size);
        return true;
    }
    
    // Register a category - called from static initializers
    void register_category(CategoryState& state) {
        std::lock_guard lock(mutex_);
        categories_by_id_[state.id] = &state;
        categories_by_name_[state.name] = &state;
        build_hierarchy(state);
    }
    
    // Runtime control - enable/disable by pattern
    void set_enabled(const std::string& pattern, bool enabled, Level level = Level::Trace) {
        std::lock_guard lock(mutex_);
        for (auto& [name, state] : categories_by_name_) {
            if (matches_pattern(name, pattern)) {
                state->enabled.store(enabled, std::memory_order_relaxed);
                state->level.store(level, std::memory_order_relaxed);
            }
        }
    }
    
    // Disable all logging
    void disable_all() {
        std::lock_guard lock(mutex_);
        for (auto& [_, state] : categories_by_id_) {
            state->enabled.store(false, std::memory_order_relaxed);
        }
    }
    
    // Get list of all categories for UI
    std::vector<std::pair<std::string, bool>> list_categories() const {
        std::lock_guard lock(mutex_);
        std::vector<std::pair<std::string, bool>> result;
        for (const auto& [name, state] : categories_by_name_) {
            result.emplace_back(name, state->enabled.load());
        }
        return result;
    }
    
    SPSCRingBuffer* buffer() { return buffer_; }
    
private:
    void build_hierarchy(CategoryState& state) {
        std::string_view name{state.name};
        auto dot = name.rfind('.');
        if (dot != std::string_view::npos) {
            std::string parent_name{name.substr(0, dot)};
            auto it = categories_by_name_.find(parent_name);
            if (it != categories_by_name_.end()) {
                state.parent = it->second;
            }
        }
    }
    
    bool matches_pattern(const std::string& name, const std::string& pattern) {
        if (pattern == "*") return true;
        if (pattern.back() == '*') {
            return name.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
        }
        return name == pattern;
    }
    
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, CategoryState*> categories_by_id_;
    std::unordered_map<std::string, CategoryState*> categories_by_name_;
    SharedMemoryRegion shm_;
    SPSCRingBuffer* buffer_{nullptr};
};

// Auto-registration helper
struct CategoryRegistrar {
    CategoryRegistrar(CategoryState& state) {
        LoggerRegistry::instance().register_category(state);
    }
};

} // namespace emu::log

// Enhanced macro with auto-registration
#define EMU_LOG_DEFINE_CATEGORY(var_name, category_path) \
    static ::emu::log::CategoryState var_name##_state{ \
        ::emu::log::Level::Off, false, category_path, \
        ::emu::log::hash_category(category_path), nullptr \
    }; \
    static ::emu::log::CategoryRegistrar var_name##_reg{var_name##_state}; \
    static ::emu::log::Category var_name{category_path, var_name##_state}
```

### 5. Logging Macros (Zero-cost when disabled)

```cpp
// logger.h
#pragma once
#include "logger_registry.h"
#include <cstdio>

namespace emu::log {

template<typename... Args>
inline void log_impl(const Category& cat, Level level, const char* fmt, Args... args) {
    auto* buffer = LoggerRegistry::instance().buffer();
    if (!buffer) return;
    
    buffer->try_write(cat.state().id, level, [&](char* buf, size_t cap) {
        return std::snprintf(buf, cap, fmt, args...);
    });
}

} // namespace emu::log

// Main logging macros - check is inlined, rest is out-of-line
#define EMU_LOG(category, level, fmt, ...) \
    do { \
        if ((category).is_enabled(::emu::log::Level::level)) { \
            ::emu::log::log_impl(category, ::emu::log::Level::level, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define EMU_LOG_TRACE(cat, fmt, ...) EMU_LOG(cat, Trace, fmt, ##__VA_ARGS__)
#define EMU_LOG_DEBUG(cat, fmt, ...) EMU_LOG(cat, Debug, fmt, ##__VA_ARGS__)
#define EMU_LOG_INFO(cat, fmt, ...)  EMU_LOG(cat, Info, fmt, ##__VA_ARGS__)
#define EMU_LOG_WARN(cat, fmt, ...)  EMU_LOG(cat, Warn, fmt, ##__VA_ARGS__)
#define EMU_LOG_ERROR(cat, fmt, ...) EMU_LOG(cat, Error, fmt, ##__VA_ARGS__)
```

### 6. Usage in FDC Code

```cpp
// fdc.cpp
#include "logger.h"

// Define categories for FDC subsystem
EMU_LOG_DEFINE_CATEGORY(log_fdc, "fdc");
EMU_LOG_DEFINE_CATEGORY(log_fdc_cmd, "fdc.command");
EMU_LOG_DEFINE_CATEGORY(log_fdc_read, "fdc.read");
EMU_LOG_DEFINE_CATEGORY(log_fdc_write, "fdc.write");
EMU_LOG_DEFINE_CATEGORY(log_fdc_seek, "fdc.seek");

void FDC::execute_read_sector() {
    EMU_LOG_INFO(log_fdc_cmd, "READ_SECTOR track=%d side=%d sector=%d", 
                 current_track_, current_side_, current_sector_);
    
    // ... do the read ...
    
    EMU_LOG_DEBUG(log_fdc_read, "Read %zu bytes from T%d/S%d/Sec%d, CRC=%04X",
                  bytes_read, current_track_, current_side_, current_sector_, crc);
}
```

### 7. Simple Viewer Tool (CLI)

```cpp
// log_viewer.cpp
#include "ring_buffer.h"
#include "ipc_transport.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <unordered_set>

using namespace emu::log;

static volatile bool running = true;

void signal_handler(int) { running = false; }

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);
    
    std::unordered_set<uint32_t> filter_ids;
    std::string shm_name = SharedMemoryRegion::DEFAULT_NAME;
    
    // Parse args for filters like: --filter fdc.read --filter fdc.write
    for (int i = 1; i < argc; i += 2) {
        if (std::string(argv[i]) == "--filter" && i + 1 < argc) {
            filter_ids.insert(hash_category(argv[i + 1]));
        }
    }
    
    SharedMemoryRegion shm;
    if (!shm.attach(shm_name)) {
        std::cerr << "Failed to attach to shared memory. Is emulator running?\n";
        return 1;
    }
    
    auto* buffer = SPSCRingBuffer::attach(shm.data());
    
    MessageHeader hdr;
    char payload[4096];
    
    std::cout << "Attached to emulator log. Press Ctrl+C to exit.\n";
    
    while (running) {
        if (buffer->try_read(hdr, payload, sizeof(payload))) {
            // Apply filter if set
            if (!filter_ids.empty() && filter_ids.find(hdr.category_id) == filter_ids.end()) {
                continue;
            }
            
            // Format timestamp (TSC cycles for now, could convert)
            const char* level_str = "?";
            switch (hdr.level) {
                case Level::Trace: level_str = "TRC"; break;
                case Level::Debug: level_str = "DBG"; break;
                case Level::Info:  level_str = "INF"; break;
                case Level::Warn:  level_str = "WRN"; break;
                case Level::Error: level_str = "ERR"; break;
                default: break;
            }
            
            std::cout << "[" << hdr.timestamp << "] " 
                      << level_str << " "
                      << "[" << std::hex << hdr.category_id << std::dec << "] "
                      << payload << "\n";
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    return 0;
}
```

### 8. Control Protocol (for runtime changes)

```cpp
// control_channel.h
// Second shared memory region for bidirectional control

#pragma once
#include <atomic>
#include <cstring>

namespace emu::log {

struct ControlCommand {
    enum class Type : uint8_t {
        None = 0,
        EnableCategory,
        DisableCategory,
        DisableAll,
        SetLevel,
        ListCategories,
    };
    
    std::atomic<Type> type{Type::None};
    char pattern[64];
    Level level;
    std::atomic<bool> processed{true};
};

struct ControlResponse {
    std::atomic<bool> ready{false};
    char data[4096];  // JSON or simple text list of categories
};

struct ControlBlock {
    ControlCommand cmd;
    ControlResponse resp;
};

} // namespace emu::log
```

## IPC Options Comparison

| Method | Latency | Throughput | Cross-platform | Complexity |
|--------|---------|------------|----------------|------------|
| **Shared Memory + Ring Buffer** | ~100ns | Excellent | Yes (with abstraction) | Medium |
| Unix Domain Sockets | ~1-10µs | Good | Linux/macOS only | Low |
| Named Pipes | ~1-10µs | Good | Yes | Low |
| TCP localhost | ~50-100µs | Good | Yes | Low |
| Memory-mapped file | ~100ns | Excellent | Yes | Medium |

**Recommendation**: Shared memory with SPSC ring buffer gives you the best latency for your real-time monitoring use case.

## Next Steps

1. **Add format string safety** - use `{fmt}` or compile-time format checking
2. **Structured logging** - binary format with typed fields for the ring buffer
3. **GUI viewer** - ImGui-based tool with category tree, filtering, search
4. **Breakpoints** - pause emulator when specific log pattern matches
5. **TSC-to-time conversion** - calibrate TSC frequency for human-readable timestamps

Want me to flesh out any particular component - maybe the GUI viewer, or a more sophisticated binary message format?





---
Powered by [Claude Exporter](https://www.ai-chat-exporter.net)