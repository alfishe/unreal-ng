# Architecture Overview Diagrams

## High-Level System Architecture

```mermaid
graph TB
    subgraph "User Interfaces"
        A[Qt GUI Frontend]
        B[Console Test Client]
        C[Web Interface]
    end

    subgraph "Automation Layer"
        D[Lua Scripting]
        E[Python Bindings]
        F[WebAPI REST/WS]
    end

    subgraph "Core Emulation Engine"
        G[Emulator Context]
        H[Z80 CPU Core]
        I[Memory Management]
        J[I/O Controllers]
        K[Video Rendering]
        L[Audio Processing]
        M[File Loaders]
    end

    subgraph "Hardware Abstraction"
        N[Pentagon 128K]
        O[Pentagon 512K]
        P[Spectrum 48K]
        Q[Spectrum 128K]
        R[TS-Conf]
        S[Scorpion 256K]
    end

    subgraph "External Dependencies"
        T[Google Test]
        U[Google Benchmark]
        V[Qt Framework]
        W[Embedded Libraries]
    end

    A --> D
    B --> D
    C --> F

    D --> G
    E --> G
    F --> G

    G --> H
    G --> I
    G --> J
    G --> K
    G --> L
    G --> M

    J --> N
    J --> O
    J --> P
    J --> Q
    J --> R
    J --> S

    H --> T
    I --> T
    J --> T
    K --> T
    L --> T
    M --> T

    A --> V
    K --> V
```

## Component Dependency Diagram

```mermaid
classDiagram
    class Emulator {
        +EmulatorContext* context
        +MainLoop* mainloop
        +Config* config
        +Core* core
        +Z80* z80
        +Memory* memory
        +DebugManager* debugManager
        -volatile bool isRunning
        -volatile bool isPaused
        -std::mutex initializationMutex
        -std::thread* asyncThread
        +Init(): bool
        +Start(): void
        +Stop(): void
        +Reset(): void
        +LoadSnapshot(path): bool
        +LoadTape(path): bool
        +LoadDisk(path): bool
    }

    class EmulatorContext {
        +ModuleLogger* pModuleLogger
        +CONFIG config
        +CoreState coreState
        +EmulatorState emulatorState
        +TEMP temporary
        +HOST host
        +MainLoop* pMainLoop
        +Core* pCore
        +Keyboard* pKeyboard
        +Memory* pMemory
        +PortDecoder* pPortDecoder
        +Tape* pTape
        +WD1793* pBetaDisk
        +Screen* pScreen
        +AudioCallback pAudioCallback
        +SoundManager* pSoundManager
        +DebugManager* pDebugManager
        +Emulator* pEmulator
    }

    class MainLoop {
        +EmulatorContext* context
        +EmulatorState* state
        +Core* cpu
        +Screen* screen
        +SoundManager* soundManager
        -volatile bool isRunning
        -volatile bool stopRequested
        -volatile bool pauseRequested
        +Run(): void
        +Stop(): void
        +Pause(): void
        +Resume(): void
    }

    class Core {
        +Z80* z80
        +Memory* memory
        +PortDecoder* portDecoder
        +Screen* screen
        +SoundManager* soundManager
        +Keyboard* keyboard
        +Tape* tape
        +WD1793* wd1793
        +DebugManager* debugManager
        +Init(): bool
        +Reset(): void
        +Release(): void
    }

    class Z80 {
        +uint16_t PC
        +uint16_t SP
        +uint16_t IX,IY
        +uint8_t A,F,B,C,D,E,H,L
        +uint8_t A_,F_,B_,C_,D_,E_,H_,L_
        +uint8_t I,R,R7
        +bool IFF1,IFF2
        +bool halted
        +Reset(): void
        +ExecuteInstruction(): void
        +GetState(): Z80State
    }

    class Memory {
        +uint8_t* ram[4]
        +uint8_t* rom[4]
        +uint8_t* page[4]
        +uint8_t* cache
        +uint8_t* trash
        +ReadByte(addr): uint8_t
        +WriteByte(addr, val): void
        +ReadWord(addr): uint16_t
        +WriteWord(addr, val): void
        +Init(): void
        +Reset(): void
    }

    class PortDecoder {
        <<interface>>
        +Read(port): uint8_t
        +Write(port, val): void
        +Reset(): void
    }

    class PortDecoder_Pentagon128 {
        +Read(port): uint8_t
        +Write(port, val): void
        +Reset(): void
    }

    class Screen {
        +uint32_t frameBuffer[320*240]
        +uint32_t palette[256]
        +uint32_t borderColor
        +uint32_t screenWidth
        +uint32_t screenHeight
        +uint32_t fps
        +uint32_t tacts
        +UpdateFrame(): void
        +Reset(): void
    }

    class SoundManager {
        +Beeper* beeper
        +AY8910* ay8910
        +TurboSound* turbosound
        +uint16_t* buffer
        +uint32_t bufferSize
        +uint32_t sampleRate
        +uint32_t channels
        +Init(): bool
        +Reset(): void
        +Update(): void
    }

    Emulator --> EmulatorContext : owns
    Emulator --> MainLoop : owns
    EmulatorContext --> MainLoop : references
    EmulatorContext --> Core : references
    MainLoop --> EmulatorContext : uses
    Core --> Z80 : owns
    Core --> Memory : owns
    Core --> PortDecoder : owns
    Core --> Screen : owns
    Core --> SoundManager : owns
    PortDecoder <|-- PortDecoder_Pentagon128 : implements
```

## Memory Management Issues

```mermaid
graph TD
    A[Memory Management Issues] --> B[Raw Pointer Usage]
    A --> C[Manual new/delete]
    A --> D[Exception Unsafe]
    A --> E[Resource Leaks]

    B --> B1[EmulatorContext pointers]
    B --> B2[Component pointers]
    B --> B3[Buffer pointers]

    C --> C1[Screen::AllocateFramebuffer]
    C --> C2[ImageHelper detached threads]
    C --> C3[Core::Release manual cleanup]

    D --> D1[Exception during init]
    D --> D2[Partial construction]
    D --> D3[Cleanup failures]

    E --> E1[Thread termination]
    E --> E2[Error paths]
    E --> E3[Shutdown scenarios]

    F[Smart Pointer Solution] --> G[unique_ptr for ownership]
    F --> H[shared_ptr for sharing]
    F --> I[Automatic cleanup]
    F --> J[Exception safety]
```

## Testing Strategy Pyramid

```mermaid
graph TD
    A[Testing Strategy] --> B[Unit Tests<br/>~60% of tests]
    A --> C[Integration Tests<br/>~20% of tests]
    A --> D[System Tests<br/>~15% of tests]
    A --> E[E2E Tests<br/>~5% of tests]

    B --> B1[Z80 Instructions]
    B --> B2[File Format Loaders]
    B --> B3[Hardware Components]
    B --> B4[Utility Functions]

    C --> C1[Full Emulator Cycles]
    C --> C2[Component Integration]
    C --> C3[Multi-format Testing]
    C --> C4[Hardware Variant Testing]

    D --> D1[GUI Interaction]
    D --> D2[Automation APIs]
    D --> D3[Performance Regression]
    D --> D4[Memory Leak Detection]

    E --> E1[Boot to BASIC]
    E --> E2[Run Commercial Software]
    E --> E3[Cross-platform Validation]
    E --> E4[Long-running Stability]

    F[Test Infrastructure] --> G[Google Test Framework]
    F --> H[Google Benchmark]
    F --> I[CI/CD Pipeline]
    F --> J[Coverage Reporting]

    G --> B
    G --> C
    G --> D
    G --> E

    H --> I

    I --> J
```

## WD1793 Floppy Controller Issues

```mermaid
stateDiagram-v2
    [*] --> Idle: Controller Reset
    Idle --> Command: Command Received
    Command --> ReadSector: READ SECTOR
    Command --> WriteSector: WRITE SECTOR
    Command --> ReadTrack: READ TRACK
    Command --> WriteTrack: WRITE TRACK
    Command --> ReadAddr: READ ADDRESS
    Command --> ReadTrack: FORCE INTERRUPT
    Command --> Seek: SEEK
    Command --> Recalibrate: RECALIBRATE

    ReadSector --> Searching: Find Sector
    Searching --> Reading: Sector Found
    Reading --> Complete: Sector Read
    Reading --> Error: Read Error

    WriteSector --> Searching
    Searching --> Writing: Sector Found
    Writing --> Complete: Sector Written
    Writing --> Error: Write Error

    note right of Searching : MISSING: Disk rotation timing\nTODO: index strobes timing
    note right of ReadSector : MISSING: VERIFY command\nTODO: implement VERIFY
    note right of Writing : MISSING: timeout handling\nTODO: DRQ serve timeout

    Complete --> Idle: Operation Complete
    Error --> Idle: Error Handling

    Seek --> Seeking: Start Seek
    Seeking --> Complete: Track Found
    Seeking --> Error: Seek Error

    Recalibrate --> Seeking

    ReadAddr --> ReadingAddress: Read Address
    ReadingAddress --> Complete: Address Read

    ReadTrack --> ReadingTrack: Read Track
    ReadingTrack --> Complete: Track Read

    WriteTrack --> WritingTrack: Write Track
    WritingTrack --> Complete: Track Written
```

## Implementation Roadmap Timeline

```mermaid
gantt
    title Implementation Roadmap (6 Months)
    dateFormat YYYY-MM-DD
    section Phase 1: Critical (Months 1-2)
    Memory Migration - Core Classes    :done, 2024-01-01, 14d
    Memory Migration - Components     :done, 2024-01-15, 14d
    Memory Migration - Utilities      :done, 2024-01-29, 14d
    WD1793 Timing Logic              :active, 2024-02-12, 10d
    WD1793 VERIFY Command            :2024-02-22, 7d
    WD1793 Timeout Handling          :2024-03-01, 7d
    Error Handling Standardization    :2024-03-08, 14d

    section Phase 2: Architecture (Months 2-4)
    Thread Pool Implementation        :2024-03-22, 14d
    Async Lifecycle Management       :2024-04-05, 14d
    Thread Safety Validation         :2024-04-19, 14d
    Plugin Architecture - HW Variants:2024-05-03, 21d
    Runtime Component Loading        :2024-05-24, 14d
    Cross-platform CI/CD             :2024-06-07, 14d

    section Phase 3: Quality (Months 4-6)
    Integration Testing Framework    :2024-06-21, 21d
    Visual Regression Testing        :2024-07-12, 14d
    Fuzz Testing Implementation      :2024-07-26, 14d
    C++17 Feature Utilization        :2024-08-09, 21d
    Performance Optimizations        :2024-08-30, 21d
    Static Analysis Integration      :2024-09-20, 14d
    Architecture Documentation       :2024-10-04, 14d
    API Stabilization                :2024-10-18, 14d
```
