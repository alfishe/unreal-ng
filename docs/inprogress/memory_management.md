# Memory Management Analysis & Modernization

## Current Memory Management Issues

```mermaid
graph TD
    A[Memory Management Problems] --> B[Raw Pointer Usage]
    A --> C[Manual new/delete]
    A --> D[Exception Unsafe Code]
    A --> E[Resource Leaks]
    A --> F[Ownership Confusion]

    B --> B1[EmulatorContext members]
    B --> B2[Component pointers]
    B --> B3[Buffer allocations]

    C --> C1[Screen::AllocateFramebuffer]
    C --> C2[ImageHelper thread buffers]
    C --> C3[Core::Release manual cleanup]

    D --> D1[Constructor failures]
    D --> D2[Partial initialization]
    D --> D3[Cleanup on exceptions]

    E --> E1[Thread termination]
    E --> E2[Error handling paths]
    E --> E3[Shutdown scenarios]

    F --> F1[Multiple references]
    F --> F2[Borrowed vs owned]
    F --> F3[Transfer of ownership]
```

## Specific Problem Areas

### Core Class Memory Management

```cpp
// CURRENT: Problematic manual memory management
class Core {
private:
    PortDecoder* _portDecoder = nullptr;    // Raw pointer - manual cleanup needed
    Screen* _screen = nullptr;              // Raw pointer - manual cleanup needed
    SoundManager* _sound = nullptr;         // Raw pointer - manual cleanup needed
    Keyboard* _keyboard = nullptr;          // Raw pointer - manual cleanup needed
    Tape* _tape = nullptr;                  // Raw pointer - manual cleanup needed
    WD1793* _wd1793 = nullptr;              // Raw pointer - manual cleanup needed
    DebugManager* _debugManager = nullptr;  // Raw pointer - manual cleanup needed

public:
    bool Init() {
        // Complex initialization with potential failure points
        _portDecoder = new PortDecoder_Pentagon128();
        _screen = new Screen();
        _sound = new SoundManager();
        // ... more allocations

        // If any allocation fails here, we have partial initialization
        if (!_screen->Init()) {
            return false;  // Leak: _portDecoder, _sound already allocated
        }

        return true;
    }

    void Release() {
        // Manual cleanup - error prone and repetitive
        delete _portDecoder; _portDecoder = nullptr;
        delete _screen; _screen = nullptr;
        delete _sound; _sound = nullptr;
        delete _keyboard; _keyboard = nullptr;
        delete _tape; _tape = nullptr;
        delete _wd1793; _wd1793 = nullptr;
        delete _debugManager; _debugManager = nullptr;
    }
};
```

### Screen Framebuffer Allocation

```cpp
// CURRENT: Manual buffer management
class Screen {
private:
    uint8_t* _frameBuffer = nullptr;  // Raw pointer to buffer

public:
    bool AllocateFramebuffer(size_t size) {
        _frameBuffer = new uint8_t[size];  // Manual allocation
        return _frameBuffer != nullptr;
    }

    ~Screen() {
        delete[] _frameBuffer;  // Manual cleanup - exception unsafe
    }
};
```

### ImageHelper Detached Threads

```cpp
// CURRENT: Dangerous detached thread pattern
class ImageHelper {
public:
    static void SaveScreenshotAsync(uint8_t* buffer, size_t size) {
        // Transfer ownership to detached thread
        std::thread([buffer = buffer, size]() {  // Copy raw pointer
            // Async save operation
            SaveImage(buffer, size);
            delete[] buffer;  // Manual cleanup in thread - dangerous
        }).detach();  // Fire and forget - no error handling
    }
};
```

## Recommended Smart Pointer Solution

### Core Class Modernization

```cpp
// RECOMMENDED: Smart pointer modernization
class Core {
private:
    std::unique_ptr<PortDecoder> _portDecoder;      // Exclusive ownership
    std::unique_ptr<Screen> _screen;                // Exclusive ownership
    std::unique_ptr<SoundManager> _sound;           // Exclusive ownership
    std::unique_ptr<Keyboard> _keyboard;            // Exclusive ownership
    std::unique_ptr<Tape> _tape;                    // Exclusive ownership
    std::unique_ptr<WD1793> _wd1793;                // Exclusive ownership
    std::unique_ptr<DebugManager> _debugManager;    // Exclusive ownership

public:
    bool Init() {
        try {
            // Exception-safe initialization
            _portDecoder = std::make_unique<PortDecoder_Pentagon128>();
            _screen = std::make_unique<Screen>();
            _sound = std::make_unique<SoundManager>();

            // If any initialization fails, smart pointers automatically clean up
            if (!_screen->Init()) {
                return false;  // All previously allocated objects automatically destroyed
            }

            return true;
        } catch (const std::exception&) {
            // All allocations automatically cleaned up on exception
            return false;
        }
    }

    // No manual Release() needed - destructor handles cleanup automatically
};
```

### Screen Framebuffer Modernization

```cpp
// RECOMMENDED: Smart pointer buffer management
class Screen {
private:
    std::unique_ptr<uint8_t[]> _frameBuffer;  // Smart pointer to buffer
    size_t _bufferSize = 0;

public:
    bool AllocateFramebuffer(size_t size) {
        _frameBuffer = std::make_unique<uint8_t[]>(size);
        _bufferSize = size;
        return _frameBuffer != nullptr;
    }

    // No manual destructor needed - unique_ptr handles cleanup automatically
    // Exception-safe, no memory leaks
};
```

### Thread-Safe Resource Management

```cpp
// RECOMMENDED: Proper async resource management
class ImageHelper {
private:
    static std::shared_ptr<ThreadPool> _threadPool;  // Managed thread pool

public:
    static std::future<bool> SaveScreenshotAsync(std::unique_ptr<uint8_t[]> buffer, size_t size) {
        // Return future for proper lifecycle management
        return _threadPool->submit([buffer = std::move(buffer), size]() mutable {
            try {
                SaveImage(buffer.get(), size);
                return true;  // Success
            } catch (const std::exception&) {
                return false;  // Error occurred
            }
            // buffer automatically destroyed when lambda exits
        });
    }
};
```

## Screen Framebuffer Allocation - Detailed Fix

### Current Problematic Implementation

```cpp
// CURRENT: Manual buffer management
class Screen {
private:
    uint8_t* _frameBuffer = nullptr;  // Raw pointer to buffer

public:
    bool AllocateFramebuffer(size_t size) {
        _frameBuffer = new uint8_t[size];  // Manual allocation
        return _frameBuffer != nullptr;
    }

    ~Screen() {
        delete[] _frameBuffer;  // Manual cleanup - exception unsafe
    }
};
```

**Critical Issues:**
1. **Exception Unsafe**: If any code after `new uint8_t[size]` throws an exception, the allocated memory leaks
2. **Manual Memory Management**: Requires explicit `delete[]` in destructor
3. **Resource Ownership Confusion**: Raw pointer doesn't indicate ownership semantics
4. **Double-Free Risk**: If object is copied/moved incorrectly, could lead to double deletion

### Recommended Solution: Smart Pointer Migration

#### Use `std::unique_ptr<uint8_t[]>` for Exclusive Ownership

```cpp
// RECOMMENDED: Modern smart pointer approach
class Screen {
private:
    std::unique_ptr<uint8_t[]> _frameBuffer;  // Smart pointer owns the buffer
    size_t _bufferSize = 0;                   // Track size for bounds checking

public:
    // Exception-safe allocation
    bool AllocateFramebuffer(size_t size) {
        try {
            _frameBuffer = std::make_unique<uint8_t[]>(size);
            _bufferSize = size;
            return true;
        } catch (const std::bad_alloc&) {
            _bufferSize = 0;
            return false;  // Allocation failed, no memory to clean up
        }
    }

    // No manual destructor needed - unique_ptr handles cleanup automatically
    // ~Screen() = default; // Compiler-generated destructor calls unique_ptr destructor

    // Optional: Provide safe access methods
    uint8_t* GetBuffer() const noexcept { return _frameBuffer.get(); }
    size_t GetBufferSize() const noexcept { return _bufferSize; }

    // Optional: Bounds-checked access
    uint8_t& At(size_t index) {
        if (index >= _bufferSize) {
            throw std::out_of_range("Framebuffer index out of bounds");
        }
        return _frameBuffer[index];
    }
};
```

### Key Benefits of This Approach

1. **Exception Safety**: If `std::make_unique<uint8_t[]>` succeeds, ownership is safely transferred. If it fails, no cleanup needed.

2. **Automatic Resource Management**: `std::unique_ptr` automatically calls `delete[]` when the object is destroyed, moved, or goes out of scope.

3. **No Manual Destructor**: The compiler-generated destructor properly cleans up the smart pointer.

4. **Clear Ownership Semantics**: `unique_ptr` indicates exclusive ownership - only one object can own the buffer at a time.

5. **Move Semantics**: The class can be moved efficiently without copying the buffer.

### Alternative: Custom Deleter for Specialized Allocation

If you need custom allocation (e.g., aligned memory, specific allocators):

```cpp
// For custom allocation patterns
class Screen {
private:
    std::unique_ptr<uint8_t, decltype(&std::free)> _frameBuffer{nullptr, &std::free};
    size_t _bufferSize = 0;

public:
    bool AllocateFramebuffer(size_t size) {
        // Use custom allocation if needed
        void* ptr = std::malloc(size);  // or _aligned_malloc(size, alignment)
        if (!ptr) {
            _bufferSize = 0;
            return false;
        }

        _frameBuffer.reset(static_cast<uint8_t*>(ptr));
        _bufferSize = size;
        return true;
    }
};
```

### Implementation Steps for Screen Class

1. **Include Headers**: Add `#include <memory>` to the header file.

2. **Update Member Variables**:
   - Change `uint8_t* _frameBuffer = nullptr;` to `std::unique_ptr<uint8_t[]> _frameBuffer;`
   - Add `size_t _bufferSize = 0;` for size tracking

3. **Update Allocation Method**:
   - Use `std::make_unique<uint8_t[]>(size)` instead of `new uint8_t[size]`
   - Handle allocation failure gracefully
   - Set `_bufferSize` on success

4. **Remove Manual Destructor**:
   - Delete the `~Screen()` method entirely
   - Let the compiler generate it automatically

5. **Update Access Patterns**:
   - Change direct pointer access to `_frameBuffer.get()` when needed
   - Add bounds checking methods if appropriate

6. **Update Move/Copy Semantics**:
   - Ensure the class properly supports move operations
   - Consider deleting copy constructor/assignment if buffer ownership should be exclusive

### Testing Considerations

After implementing this change, add tests to verify:

```cpp
// Example test cases to add
TEST(ScreenTest, AllocationSuccess) {
    Screen screen;
    EXPECT_TRUE(screen.AllocateFramebuffer(1024));
    EXPECT_EQ(screen.GetBufferSize(), 1024);
    EXPECT_NE(screen.GetBuffer(), nullptr);
}

TEST(ScreenTest, AllocationFailure) {
    Screen screen;
    // Simulate allocation failure by requesting huge amount
    EXPECT_FALSE(screen.AllocateFramebuffer(std::numeric_limits<size_t>::max()));
}

TEST(ScreenTest, ExceptionSafety) {
    Screen screen;
    // Verify no leaks if exceptions occur after allocation
    EXPECT_TRUE(screen.AllocateFramebuffer(1024));
    // If any subsequent operations throw, memory should be cleaned up
}
```

### Migration Benefits

- **Zero Memory Leaks**: RAII ensures cleanup in all code paths
- **Exception Safety**: No resource leaks even when exceptions occur
- **Simplified Code**: No manual destructor or cleanup logic
- **Better Performance**: No overhead compared to manual management
- **Thread Safety**: Smart pointers are thread-safe for the ownership operations
- **Modern C++**: Uses current best practices and standard library features

This approach follows the C++ Core Guidelines principle of "no naked `new`" and ensures your framebuffer allocation is robust, safe, and maintainable.

## Migration Strategy

### Phase 1: Core Infrastructure (Week 1-2)

```mermaid
graph TD
    A[Phase 1: Core Infrastructure] --> B[Identify Ownership Patterns]
    A --> C[Create Smart Pointer Aliases]
    A --> D[Migrate Core Classes]

    B --> B1[Exclusive ownership]
    B --> B2[Shared ownership]
    B --> B3[Borrowed references]

    C --> C1[using ComponentPtr = std::unique_ptr<Component>;]
    C --> C2[using SharedComponent = std::shared_ptr<Component>;]

    D --> D1[Emulator class]
    D --> D2[EmulatorContext class]
    D --> D3[Core class]
```

### Phase 2: Component Classes (Week 3-4)

```mermaid
graph TD
    A[Phase 2: Component Classes] --> B[Migrate Screen Classes]
    A --> C[Migrate I/O Classes]
    A --> D[Migrate Memory Classes]
    A --> E[Migrate CPU Classes]

    B --> B1[Screen, VideoController]
    B --> B2[Remove manual new/delete]

    C --> C1[PortDecoder implementations]
    C --> C2[WD1793, Tape, Keyboard]

    D --> D1[Memory, ROM classes]
    D --> D2[Buffer management]

    E --> E1[Z80 CPU core]
    E --> E2[Instruction handlers]
```

### Phase 3: Utility Classes (Week 5-6)

```mermaid
graph TD
    A[Phase 3: Utility Classes] --> B[Migrate Helper Classes]
    A --> C[Migrate Async Operations]
    A --> D[Update Tests]

    B --> B1[ImageHelper, FileHelper]
    B --> B2[StringHelper, AudioHelper]

    C --> C1[Thread pool implementation]
    C --> C2[Future-based APIs]

    D --> D1[Update test fixtures]
    D --> D2[Memory leak testing]
```

## Ownership Pattern Analysis

```mermaid
graph TD
    A[Ownership Patterns] --> B[Exclusive Ownership]
    A --> C[Shared Ownership]
    A --> D[Borrowed References]

    B --> B1[Single owner, no sharing]
    B --> B2[Clear lifetime semantics]
    B --> B3[Use unique_ptr]

    C --> C1[Multiple references needed]
    C --> C2[Complex lifetime]
    C --> C3[Use shared_ptr]

    D --> D1[Non-owning observers]
    D --> D2[Parameter passing]
    D --> D3[Use raw pointers/references]

    E[Examples] --> F[Core owns components]
    E --> G[EmulatorContext shares config]
    E --> H[Callbacks borrow objects]

    F --> B
    G --> C
    H --> D
```

## Exception Safety Analysis

### Current Exception Unsafe Code

```cpp
// PROBLEMATIC: Exception unsafe initialization
bool Emulator::Init() {
    _context = new EmulatorContext();  // Allocation
    _mainloop = new MainLoop(_context);  // Allocation - can throw
    _config = new Config();  // If this throws, _context and _mainloop leak

    if (!_context->Init()) {  // Failure path
        return false;  // Memory leak: objects allocated but not cleaned up
    }

    return true;
}
```

### Recommended Exception Safe Code

```cpp
// SAFE: Exception safe with smart pointers
bool Emulator::Init() {
    try {
        _context = std::make_unique<EmulatorContext>();
        _mainloop = std::make_unique<MainLoop>(_context.get());  // Pass raw pointer for borrowing
        _config = std::make_unique<Config>();

        if (!_context->Init()) {
            return false;  // All allocations automatically cleaned up
        }

        return true;
    } catch (const std::exception&) {
        // All partially constructed objects automatically destroyed
        return false;
    }
}
```

## Memory Leak Detection Strategy

```mermaid
graph TD
    A[Memory Leak Detection] --> B[Static Analysis]
    A --> C[Dynamic Analysis]
    A --> D[CI/CD Integration]

    B --> B1[Clang Static Analyzer]
    B --> B2[Coverity Scan]
    B --> B3[Cppcheck]

    C --> C1[Valgrind Memcheck]
    C --> C2[Address Sanitizer]
    C --> C3[Leak Sanitizer]

    D --> D1[Build-time checks]
    D --> D2[Test-time validation]
    D --> D3[Release validation]

    E[Detection Tools] --> F[Compile-time warnings]
    E --> G[Runtime leak reports]
    E --> H[Test failure on leaks]

    F --> B
    G --> C
    H --> D
```

## Performance Impact Assessment

```mermaid
graph TD
    A[Performance Impact] --> B[Smart Pointer Overhead]
    A --> C[Memory Allocation Patterns]
    A --> D[Cache Locality]

    B --> B1[Reference counting cost]
    B --> B2[Atomic operations]
    B --> B3[Memory indirection]

    C --> C1[Allocation timing]
    C --> C2[Deallocation timing]
    C --> C3[Fragmentation]

    D --> D1[Cache misses]
    D --> D2[Memory layout]
    D --> D3[Access patterns]

    E[Optimization Strategies] --> F[Pool allocators]
    E --> G[Custom deleters]
    E --> H[Arena allocation]
    E --> I[Placement new]

    F --> C
    G --> B
    H --> C
    I --> D
```

## Migration Checklist

- [ ] Analyze current ownership patterns
- [ ] Identify exception unsafe code paths
- [ ] Create smart pointer type aliases
- [ ] Implement custom deleters for platform resources
- [ ] Migrate core infrastructure classes
- [ ] Update component classes
- [ ] Refactor async operations
- [ ] Update test fixtures
- [ ] Add memory leak detection
- [ ] Performance testing and optimization
- [ ] Documentation updates
- [ ] Code review and validation

## Success Metrics

1. **Zero Memory Leaks**: All CI/CD builds pass leak detection
2. **Exception Safety**: No resource leaks in exception paths
3. **Performance**: No degradation in emulator performance
4. **Maintainability**: Reduced manual memory management code
5. **Reliability**: Fewer crashes due to memory corruption

## Risk Mitigation

1. **Incremental Migration**: Migrate components one at a time
2. **Comprehensive Testing**: Memory leak tests for each migrated component
3. **Performance Monitoring**: Benchmark before/after each migration phase
4. **Code Review**: Peer review of all memory management changes
5. **Rollback Plan**: Ability to revert changes if issues arise
