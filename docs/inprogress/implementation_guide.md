# Implementation Guide: Critical Fixes

## Priority 1: Memory Management Migration

### Before/After Code Examples

#### Problematic Current Code
```cpp
// core/src/emulator/core.cpp - CURRENT (PROBLEMATIC)
Core::Core() : _initialized(false) {
    // Default constructor - no initialization
}

bool Core::Init() {
    _portDecoder = new PortDecoder_Pentagon128();  // Raw pointer allocation
    _screen = new Screen();                        // Raw pointer allocation
    _sound = new SoundManager();                   // Raw pointer allocation
    _keyboard = new Keyboard();                    // Raw pointer allocation
    _tape = new Tape();                           // Raw pointer allocation
    _wd1793 = new WD1793();                       // Raw pointer allocation
    _debugManager = new DebugManager();           // Raw pointer allocation

    // Complex initialization that can fail
    if (!_screen->Init()) {
        return false;  // MEMORY LEAK: Allocated objects above are leaked
    }

    if (!_sound->Init()) {
        return false;  // MEMORY LEAK: Even more objects leaked
    }

    _initialized = true;
    return true;
}

void Core::Release() {
    // Manual cleanup - error prone and repetitive
    delete _portDecoder; _portDecoder = nullptr;
    delete _screen; _screen = nullptr;
    delete _sound; _sound = nullptr;
    delete _keyboard; _keyboard = nullptr;
    delete _tape; _tape = nullptr;
    delete _wd1793; _wd1793 = nullptr;
    delete _debugManager; _debugManager = nullptr;

    _initialized = false;
}

Core::~Core() {
    if (_initialized) {
        Release();  // Manual cleanup call required
    }
}
```

#### Modernized Solution
```cpp
// core/src/emulator/core.cpp - RECOMMENDED (SAFE)
Core::Core() : _initialized(false) {
    // Default constructor - RAII ready
}

bool Core::Init() {
    try {
        // Exception-safe initialization with smart pointers
        _portDecoder = std::make_unique<PortDecoder_Pentagon128>();
        _screen = std::make_unique<Screen>();
        _sound = std::make_unique<SoundManager>();
        _keyboard = std::make_unique<Keyboard>();
        _tape = std::make_unique<Tape>();
        _wd1793 = std::make_unique<WD1793>();
        _debugManager = std::make_unique<DebugManager>();

        // If any step fails, all previously allocated objects are automatically cleaned up
        if (!_screen->Init()) {
            return false;  // No memory leaks - smart pointers handle cleanup
        }

        if (!_sound->Init()) {
            return false;  // No memory leaks - smart pointers handle cleanup
        }

        _initialized = true;
        return true;

    } catch (const std::exception&) {
        // All allocations automatically cleaned up on exception
        _initialized = false;
        return false;
    }
}

// No manual Release() method needed - destructor handles everything automatically
Core::~Core() = default;  // Smart pointers handle cleanup automatically
```

#### Header File Changes
```cpp
// core/src/emulator/core.h

// BEFORE
class Core {
private:
    PortDecoder* _portDecoder = nullptr;
    Screen* _screen = nullptr;
    SoundManager* _sound = nullptr;
    Keyboard* _keyboard = nullptr;
    Tape* _tape = nullptr;
    WD1793* _wd1793 = nullptr;
    DebugManager* _debugManager = nullptr;
    // ...
};

// AFTER
class Core {
private:
    std::unique_ptr<PortDecoder> _portDecoder;
    std::unique_ptr<Screen> _screen;
    std::unique_ptr<SoundManager> _sound;
    std::unique_ptr<Keyboard> _keyboard;
    std::unique_ptr<Tape> _tape;
    std::unique_ptr<WD1793> _wd1793;
    std::unique_ptr<DebugManager> _debugManager;
    // ...
};
```

## Priority 2: WD1793 Floppy Controller Completion

### Current Missing Implementation
```cpp
// core/src/emulator/io/fdc/wd1793.cpp - CURRENT (INCOMPLETE)

// TODO: implement VERIFY
void WD1793::ExecuteCommand_Verify() {
    // NOT IMPLEMENTED - returns immediately
    SetStatus(StatusBits::Busy, false);
}

// TODO: apply the delay related to disk rotation so searching for ID Address Mark may take up to a full disk revolution
void WD1793::ExecuteCommand_ReadSector() {
    // MISSING: Disk rotation timing simulation
    // Current implementation doesn't account for physical disk rotation delays
}

// TODO: implement DRQ serve time timeout
void WD1793::HandleDataRequest() {
    // MISSING: Timeout handling for DRQ operations
    // No timeout implemented for data request operations
}
```

### Required Implementation
```cpp
// core/src/emulator/io/fdc/wd1793.cpp - RECOMMENDED (COMPLETE)

void WD1793::ExecuteCommand_Verify() {
    // Implement sector verification after read/write operations
    if (_currentTrack == nullptr) {
        SetError(StatusBits::NotReady);
        return;
    }

    // Find the target sector
    auto sector = FindSector(_targetSector);
    if (!sector) {
        SetError(StatusBits::RecordNotFound);
        return;
    }

    // Verify sector data integrity
    bool verificationPassed = VerifySectorData(sector);
    if (!verificationPassed) {
        SetError(StatusBits::CRCError);
        return;
    }

    SetStatus(StatusBits::Busy, false);
}

void WD1793::ExecuteCommand_ReadSector() {
    if (_currentTrack == nullptr) {
        SetError(StatusBits::NotReady);
        return;
    }

    // Simulate disk rotation timing for sector search
    uint32_t rotationDelay = CalculateSectorSearchTime(_targetSector);
    ScheduleDelayedOperation(rotationDelay, [this]() {
        PerformSectorRead();
    });
}

void WD1793::HandleDataRequest() {
    // Implement DRQ timeout handling
    if (_drqActive) {
        // Start timeout timer if not already active
        if (!_drqTimeoutActive) {
            _drqTimeoutActive = true;
            ScheduleTimeout(_drqTimeoutMs, [this]() {
                if (_drqActive) {
                    // Timeout occurred - abort operation
                    SetError(StatusBits::LostData);
                    _drqActive = false;
                }
                _drqTimeoutActive = false;
            });
        }
    } else {
        // DRQ satisfied - cancel timeout
        _drqTimeoutActive = false;
    }
}
```

## Priority 3: Error Handling Standardization

### Current Inconsistent Error Handling
```cpp
// Various files - CURRENT (INCONSISTENT)

// Pattern 1: Boolean returns
bool Emulator::Init() {
    if (/* some failure condition */) {
        return false;  // No context about what failed
    }
    return true;
}

// Pattern 2: Exceptions
void FileLoader::LoadFile(const std::string& path) {
    if (!file.exists()) {
        throw std::runtime_error("File not found");  // Inconsistent with boolean pattern
    }
}

// Pattern 3: Return codes
int PortDecoder::Read(uint16_t port) {
    if (port > MAX_PORT) {
        return -1;  // Magic number, unclear meaning
    }
    return ReadPort(port);
}
```

### Standardized Error Handling System
```cpp
// common/error.h - NEW FILE
#pragma once

#include <string>
#include <optional>
#include <source_location>

enum class EmulatorError {
    Success = 0,
    FileNotFound,
    InvalidFormat,
    HardwareNotSupported,
    MemoryAllocationFailed,
    InitializationFailed,
    InvalidState,
    Timeout,
    DeviceNotReady,
    CRCError,
    RecordNotFound
};

class Error {
public:
    Error(EmulatorError code, std::string message = "",
          std::source_location location = std::source_location::current())
        : code_(code), message_(std::move(message)), location_(location) {}

    EmulatorError code() const { return code_; }
    const std::string& message() const { return message_; }
    std::source_location location() const { return location_; }

    std::string to_string() const;

private:
    EmulatorError code_;
    std::string message_;
    std::source_location location_;
};

template<typename T>
class Result {
public:
    // Success constructor
    static Result success(T value) {
        return Result(std::move(value));
    }

    // Error constructor
    static Result error(EmulatorError code, std::string message = "") {
        return Result(Error(code, std::move(message)));
    }

    // Accessors
    bool is_success() const { return data_.index() == 0; }
    bool is_error() const { return data_.index() == 1; }

    const T& value() const { return std::get<0>(data_); }
    const Error& error() const { return std::get<1>(data_); }

    T& value() { return std::get<0>(data_); }
    Error& error() { return std::get<1>(data_); }

private:
    std::variant<T, Error> data_;

    Result(T value) : data_(std::move(value)) {}
    Result(Error error) : data_(std::move(error)) {}
};
```

### Updated Code with Standardized Errors
```cpp
// emulator/emulator.cpp - RECOMMENDED
Result<bool> Emulator::Init() {
    try {
        _context = std::make_unique<EmulatorContext>();
        if (!_context->Init()) {
            return Result<bool>::error(EmulatorError::InitializationFailed,
                                     "Failed to initialize emulator context");
        }

        _mainloop = std::make_unique<MainLoop>(_context.get());
        _config = std::make_unique<Config>();

        return Result<bool>::success(true);

    } catch (const std::bad_alloc&) {
        return Result<bool>::error(EmulatorError::MemoryAllocationFailed,
                                 "Failed to allocate emulator components");
    } catch (const std::exception& e) {
        return Result<bool>::error(EmulatorError::InitializationFailed,
                                 std::string("Initialization error: ") + e.what());
    }
}

// loaders/sna_loader.cpp - RECOMMENDED
Result<std::unique_ptr<Snapshot>> SNALoader::LoadSnapshot(const std::string& path) {
    auto fileResult = FileHelper::OpenFile(path);
    if (!fileResult.is_success()) {
        return Result<std::unique_ptr<Snapshot>>::error(
            EmulatorError::FileNotFound,
            "Failed to open snapshot file: " + path);
    }

    auto& file = fileResult.value();

    // Validate SNA format
    if (!ValidateSNAHeader(file)) {
        return Result<std::unique_ptr<Snapshot>>::error(
            EmulatorError::InvalidFormat,
            "Invalid SNA file format or unsupported version");
    }

    auto snapshot = std::make_unique<Snapshot>();
    if (!LoadSNAData(file, *snapshot)) {
        return Result<std::unique_ptr<Snapshot>>::error(
            EmulatorError::InvalidFormat,
            "Failed to parse SNA file data");
    }

    return Result<std::unique_ptr<Snapshot>>::success(std::move(snapshot));
}
```

## Priority 4: Thread Safety Improvements

### Current Threading Issues
```cpp
// common/imagehelper.cpp - CURRENT (DANGEROUS)
void ImageHelper::SaveScreenshotAsync(uint8_t* buffer, size_t size, const std::string& path) {
    // Fire and forget - no error handling, no lifecycle management
    std::thread([buffer, size, path]() {
        SaveImage(buffer, size, path);
        delete[] buffer;  // Manual cleanup in detached thread
    }).detach();
}

// common/messagecenter.cpp - CURRENT (POTENTIAL RACE CONDITIONS)
void MessageCenter::SendMessage(const Message& message) {
    std::lock_guard<std::mutex> lock(_mutex);
    _messageQueue.push(message);
    // No timeout, no error handling for full queue
}
```

### Thread-Safe Modern Implementation
```cpp
// common/threadpool.h - NEW FILE
#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    ~ThreadPool();

    // Submit work and get future for result
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;

    // Shutdown the pool
    void shutdown();

private:
    void worker();

    std::vector<std::thread> _workers;
    std::queue<std::function<void()>> _tasks;

    std::mutex _queueMutex;
    std::condition_variable _condition;
    bool _stop = false;
};
```

```cpp
// common/imagehelper.cpp - RECOMMENDED (SAFE)
class ImageHelper {
private:
    static std::unique_ptr<ThreadPool> _threadPool;

public:
    static std::future<bool> SaveScreenshotAsync(
        std::unique_ptr<uint8_t[]> buffer,
        size_t size,
        const std::string& path) {

        return _threadPool->submit([buffer = std::move(buffer), size, path]() -> bool {
            try {
                SaveImage(buffer.get(), size, path);
                return true;
            } catch (const std::exception& e) {
                // Log error, but don't crash
                Logger::Error("Failed to save screenshot: " + std::string(e.what()));
                return false;
            }
            // buffer automatically destroyed here
        });
    }
};

// Initialize thread pool
std::unique_ptr<ThreadPool> ImageHelper::_threadPool = std::make_unique<ThreadPool>(2);
```

```cpp
// common/messagecenter.cpp - RECOMMENDED (ROBUST)
class MessageCenter {
public:
    bool SendMessage(const Message& message, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(_mutex);

        // Wait for space in queue with timeout
        bool success = _conditionNotFull.wait_for(lock, timeout, [this]() {
            return _messageQueue.size() < _maxQueueSize;
        });

        if (!success) {
            return false;  // Queue full, message not sent
        }

        _messageQueue.push(message);
        lock.unlock();

        _conditionNotEmpty.notify_one();
        return true;
    }

private:
    static constexpr size_t _maxQueueSize = 1000;
    std::queue<Message> _messageQueue;
    std::mutex _mutex;
    std::condition_variable _conditionNotEmpty;
    std::condition_variable _conditionNotFull;
};
```

## Priority 5: Testing Infrastructure Expansion

### Integration Test Framework
```cpp
// core/tests/integration/emulator_integration_test.cpp - NEW FILE
class EmulatorIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        emulator = std::make_unique<Emulator>(LoggerLevel::LogError);
        ASSERT_TRUE(emulator->Init());

        // Load test ROM
        testRomPath = GetTestDataPath("testroms/diagnostic.rom");
        ASSERT_TRUE(emulator->LoadROM(testRomPath));
    }

    void TearDown() override {
        if (emulator) {
            emulator->Stop();
            emulator->Release();
        }
    }

    std::unique_ptr<Emulator> emulator;
    std::string testRomPath;
};

TEST_F(EmulatorIntegrationTest, BootToBasic) {
    // Start emulator
    emulator->Start();

    // Wait for boot (simulated time)
    RunEmulatorFor(5);  // 5 seconds of emulated time

    // Capture screen and verify BASIC prompt is visible
    auto framebuffer = emulator->GetFramebuffer();
    ASSERT_TRUE(ContainsText(framebuffer, "128 BASIC"));

    emulator->Stop();
}

TEST_F(EmulatorIntegrationTest, LoadAndRunProgram) {
    // Load a test program
    std::string programPath = GetTestDataPath("programs/hello.tap");
    ASSERT_TRUE(emulator->LoadTape(programPath));

    // Start and run
    emulator->Start();
    emulator->StartAsync();

    // Simulate typing LOAD "" and ENTER
    SimulateKeyPress(KeyCode::LOAD_QUOTE_QUOTE);
    SimulateKeyPress(KeyCode::ENTER);

    RunEmulatorFor(10);  // Wait for program to load

    // Verify program loaded and ran
    auto framebuffer = emulator->GetFramebuffer();
    ASSERT_TRUE(ContainsText(framebuffer, "HELLO WORLD"));

    emulator->Stop();
}
```

### Visual Regression Testing
```cpp
// core/tests/visual/visual_regression_test.cpp - NEW FILE
class VisualRegressionTest : public EmulatorIntegrationTest {
protected:
    void CaptureAndCompareScreen(const std::string& testName) {
        auto framebuffer = emulator->GetFramebuffer();

        // Save current frame as PNG
        std::string actualPath = GetTestOutputPath(testName + "_actual.png");
        SaveFramebufferAsPNG(framebuffer, actualPath);

        // Compare with reference image
        std::string referencePath = GetTestDataPath("references/" + testName + ".png");
        ASSERT_TRUE(CompareImages(referencePath, actualPath))
            << "Visual regression detected. Check " << actualPath << " vs " << referencePath;
    }
};

TEST_F(VisualRegressionTest, BasicScreenOutput) {
    // Run emulator with known program
    LoadAndRunTestProgram("basic_output.tap");

    // Wait for output
    RunEmulatorFor(2);

    // Compare screen output
    CaptureAndCompareScreen("basic_screen_output");
}

TEST_F(VisualRegressionTest, ColorAttributeTest) {
    LoadAndRunTestProgram("color_test.tap");
    RunEmulatorFor(1);

    // Verify color reproduction
    CaptureAndCompareScreen("color_attributes");
}
```

## Implementation Checklist

### Phase 1: Critical Foundation (Weeks 1-2)
- [ ] Create Result<T> error handling system
- [ ] Migrate Core class to smart pointers
- [ ] Migrate Emulator class to smart pointers
- [ ] Migrate EmulatorContext to smart pointers
- [ ] Add memory leak detection to CI/CD

### Phase 2: Component Migration (Weeks 3-4)
- [ ] Migrate Screen classes to smart pointers
- [ ] Migrate I/O component classes (WD1793, Tape, etc.)
- [ ] Implement ThreadPool class
- [ ] Refactor ImageHelper to use ThreadPool
- [ ] Update MessageCenter with timeouts

### Phase 3: WD1793 Completion (Weeks 5-6)
- [ ] Implement VERIFY command
- [ ] Add disk rotation timing simulation
- [ ] Implement DRQ timeout handling
- [ ] Add comprehensive FDC testing
- [ ] Test with real disk images

### Phase 4: Testing Infrastructure (Weeks 7-8)
- [ ] Create integration test framework
- [ ] Implement visual regression testing
- [ ] Add performance regression tests
- [ ] Expand CI/CD to all platforms
- [ ] Add fuzz testing for file loaders

### Phase 5: Quality Assurance (Weeks 9-10)
- [ ] Static analysis integration
- [ ] Code coverage > 80%
- [ ] Performance benchmarking
- [ ] Documentation updates
- [ ] Final validation and testing

## Success Validation

### Automated Checks
```bash
# CI/CD pipeline validation
./build/test/core-tests --gtest_filter="*Integration*"  # All integration tests pass
valgrind --leak-check=full ./build/bin/core-tests       # No memory leaks
./build/bin/core-benchmarks                              # Performance within baselines
cppcheck --enable=all --std=c++17 core/src/             # No static analysis warnings
```

### Manual Validation
- [ ] Emulator boots to BASIC prompt without crashes
- [ ] Commercial games load and run correctly
- [ ] Disk operations work with TRD images
- [ ] Audio output is clean and synchronized
- [ ] GUI responds correctly to all inputs
- [ ] Cross-platform builds work on Windows/macOS/Linux

### Performance Baselines
- [ ] Z80 instruction execution: > 100 MIPS
- [ ] Frame rendering: < 16ms per frame (60 FPS)
- [ ] Memory usage: < 50MB for basic operation
- [ ] Startup time: < 2 seconds
- [ ] No performance regressions from smart pointer usage
