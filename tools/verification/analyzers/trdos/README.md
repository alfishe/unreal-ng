# TR-DOS Analyzer Verification Tool

Python script for verifying TR-DOS analyzer functionality via WebAPI.

## Requirements

```bash
pip install requests
```

## Usage

### Basic Usage (Auto-discover emulator)
```bash
python verify_trdos_analyzer.py
```

### Specify Custom Port
```bash
python verify_trdos_analyzer.py --port 8090
```

### Specify Emulator UUID
```bash
python verify_trdos_analyzer.py --uuid 12345678-1234-1234-1234-123456789abc
```

### All Options
```bash
python verify_trdos_analyzer.py --host localhost --port 8090 --uuid <UUID>
```

## Use Case & Architecture

### Purpose

This verification tool validates the TR-DOS analyzer's ability to capture and report high-level disk operations when TR-DOS commands are executed in the emulated ZX Spectrum environment. It ensures the complete integration chain works correctly:

1. **WebAPI Layer** - REST endpoints for analyzer control
2. **Analyzer Manager** - Lifecycle management and event routing
3. **TR-DOS Analyzer** - Breakpoint-based event capture
4. **Z80 CPU** - Breakpoint dispatch mechanism
5. **BASIC Injection** - Command orchestration

### How It Works

The verification script acts as an external test harness that:

1. Connects to a running emulator instance via WebAPI
2. Activates the TR-DOS analyzer (which installs page-specific breakpoints)
3. Injects a TR-DOS command (`LIST`) via BASIC
4. Allows the emulator to execute the command
5. Retrieves captured events from the analyzer
6. Validates that expected events were recorded

### Architecture Overview

```mermaid
graph TB
    subgraph "External Test Harness"
        Script[verify_trdos_analyzer.py]
    end
    
    subgraph "WebAPI Layer"
        API[EmulatorAPI]
        Endpoints[REST Endpoints]
    end
    
    subgraph "Emulator Core"
        Manager[AnalyzerManager]
        TRDOSAnalyzer[TRDOSAnalyzer]
        BPManager[BreakpointManager]
        Z80[Z80 CPU]
        Memory[Memory System]
    end
    
    subgraph "ZX-Spectrum Emulated Environment"
        ROM[TR-DOS ROM]
        BASIC[BASIC Interpreter]
    end
    
    Script -->|HTTP Requests| API
    API --> Endpoints
    Endpoints -->|Activate/Deactivate| Manager
    Endpoints -->|Inject Command| BASIC
    Endpoints -->|Get Events| TRDOSAnalyzer
    
    Manager -->|Register/Unregister| TRDOSAnalyzer
    TRDOSAnalyzer -->|Install Breakpoints| BPManager
    BPManager -->|Dispatch| Z80
    Z80 -->|Execute| ROM
    Z80 -->|onBreakpointHit| TRDOSAnalyzer
    BASIC -->|Call| ROM
    TRDOSAnalyzer -->|Store| Events[(Event Log)]
```

### Verification Flow

```mermaid
sequenceDiagram
    participant Script as verify_trdos_analyzer.py
    participant API as WebAPI
    participant Manager as AnalyzerManager
    participant Analyzer as TRDOSAnalyzer
    participant CPU as Z80 CPU
    participant ROM as TR-DOS ROM
    
    Note over Script: Step 1: Connection Check
    Script->>API: GET /api/v1/emulator
    API-->>Script: 200 OK (emulator list)
    
    Note over Script: Step 2: Discovery
    Script->>API: GET /api/v1/emulator
    API-->>Script: {emulators: [{id: "...", ...}]}
    
    Note over Script: Step 3: Activation
    Script->>API: PUT /api/v1/emulator/{id}/analyzer/trdos<br/>{enabled: true}
    API->>Manager: activateAnalyzer("trdos")
    Manager->>Analyzer: activate()
    Analyzer->>CPU: installBreakpoint(0x3D13, page=1)
    Analyzer->>CPU: installBreakpoint(0x15D4, page=1)
    Analyzer-->>Manager: activated
    Manager-->>API: {success: true}
    API-->>Script: 200 OK
    
    Note over Script: Step 4: Execute Command
    Script->>API: POST /api/v1/emulator/{id}/basic/run<br/>{command: "LIST"}
    API->>ROM: Inject "LIST" + ENTER
    ROM->>CPU: Execute BASIC
    
    Note over CPU,ROM: Emulator runs...
    CPU->>ROM: PC = 0x3D13 (TRDOS entry)
    ROM-->>CPU: Breakpoint hit!
    CPU->>Analyzer: onBreakpointHit(0x3D13)
    Analyzer->>Analyzer: Log TRDOS_ENTRY event
    
    CPU->>ROM: PC = 0x15D4 (Command start)
    ROM-->>CPU: Breakpoint hit!
    CPU->>Analyzer: onBreakpointHit(0x15D4)
    Analyzer->>Analyzer: Log COMMAND_START event
    
    ROM->>ROM: Execute LIST (read catalog)
    ROM-->>API: Command complete
    API-->>Script: {success: true, cycles: 15000}
    
    Note over Script: Step 5: Retrieve Events
    Script->>API: GET /api/v1/emulator/{id}/analyzer/trdos/events
    API->>Analyzer: getEvents()
    Analyzer-->>API: [{type: "TRDOS_ENTRY", ...}, ...]
    API-->>Script: {events: [...]}
    
    Note over Script: Step 6: Verify
    Script->>Script: Check for expected events
    
    Note over Script: Step 7: Deactivation
    Script->>API: PUT /api/v1/emulator/{id}/analyzer/trdos<br/>{enabled: false}
    API->>Manager: deactivateAnalyzer("trdos")
    Manager->>Analyzer: deactivate()
    Analyzer->>CPU: removeAllBreakpoints()
    Analyzer-->>Manager: deactivated
    API-->>Script: 200 OK
```

### State Diagram

```mermaid
stateDiagram-v2
    [*] --> Disconnected
    Disconnected --> Connected: WebAPI accessible
    Connected --> EmulatorSelected: Discover & select emulator
    EmulatorSelected --> AnalyzerActive: Activate analyzer
    AnalyzerActive --> CommandExecuting: Inject BASIC command
    CommandExecuting --> EventsReady: Command completes
    EventsReady --> Verifying: Retrieve events
    Verifying --> AnalyzerActive: Verification complete
    AnalyzerActive --> Cleanup: Deactivate analyzer
    Cleanup --> [*]: Test complete
    
    Connected --> [*]: Connection failed
    EmulatorSelected --> [*]: No emulators
    AnalyzerActive --> [*]: Activation failed
    CommandExecuting --> EventsReady: Execution failed (continue)
    Verifying --> [*]: Verification failed
```

### Event Capture Mechanism

The TR-DOS analyzer uses **page-specific breakpoints** to intercept TR-DOS ROM execution:

1. **Entry Point (0x3D13, Page 1)** - Main TR-DOS entry point
   - Triggered when BASIC calls into TR-DOS
   - Captures: Command type, registers, stack state

2. **Command Dispatcher (0x15D4, Page 1)** - Command execution start
   - Triggered when TR-DOS begins processing a command
   - Captures: Command parameters, drive selection

3. **FDC Operations** - Disk controller interactions
   - Captured via FDC state monitoring
   - Includes: Sector reads, track seeks, data transfers

### Expected Event Sequence for `LIST` Command

```mermaid
sequenceDiagram
    participant BASIC
    participant TRDOS as TR-DOS ROM
    participant FDC as WD1793 FDC
    participant Analyzer
    
    BASIC->>TRDOS: RST 8, DEFB 0x15 (LIST)
    Note over Analyzer: Event: TRDOS_ENTRY
    
    TRDOS->>TRDOS: Parse command
    Note over Analyzer: Event: COMMAND_START<br/>(command: LIST)
    
    TRDOS->>FDC: Read sector (T0, S9)
    Note over Analyzer: Event: FDC_CMD_READ<br/>(track: 0, sector: 9)
    
    FDC-->>TRDOS: System sector data
    Note over Analyzer: Event: SECTOR_TRANSFER
    
    TRDOS->>FDC: Read sector (T0, S1-8)
    Note over Analyzer: Event: FDC_CMD_READ<br/>(catalog sectors)
    
    FDC-->>TRDOS: Catalog data
    TRDOS->>BASIC: Display file list
```

## Verification Steps


The script performs the following verification workflow:

1. **Check WebAPI Connection** - Verifies WebAPI is accessible
2. **Discover Emulators** - Lists running emulator instances
3. **Activate Analyzer** - Activates TR-DOS analyzer
4. **Execute Command** - Injects and executes `LIST` command via BASIC
5. **Retrieve Events** - Gets captured events from analyzer
6. **Verify Events** - Checks for expected event types
7. **Deactivate Analyzer** - Cleans up

## Expected Output

### Success
```
============================================================
TR-DOS Analyzer Verification
============================================================

[Step 1] Checking WebAPI connection...
✓ WebAPI is accessible at http://localhost:8090

[Step 2] Discovering emulator instances...
✓ Found 1 emulator instance(s)
  1. UUID: 12345678... Model: Pentagon, State: running

[Step 3] Activating TR-DOS analyzer...
✓ TR-DOS analyzer activated

[Step 4] Executing BASIC command: LIST
✓ Command 'LIST' executed
ℹ Executed 15000 CPU cycles

[Step 5] Retrieving analyzer events...
✓ Retrieved 5 event(s)

[Step 6] Verifying collected events...

Event Summary:
  TRDOS_ENTRY: 1
  COMMAND_START: 1
  FDC_CMD_READ: 2
  SECTOR_TRANSFER: 1

✓ Found expected event: TRDOS_ENTRY
✓ Found expected event: COMMAND_START

Detailed Events:
  1. [    1000] TRDOS_ENTRY
  2. [    2000] COMMAND_START (Command: LIST)
  3. [    3000] FDC_CMD_READ (Track: 0, Sector: 1)
  4. [    4000] SECTOR_TRANSFER
  5. [    5000] FDC_CMD_READ (Track: 0, Sector: 2)

[Step 7] Deactivating TR-DOS analyzer...
✓ TR-DOS analyzer deactivated

============================================================
✓ VERIFICATION PASSED
TR-DOS analyzer is working correctly!
============================================================
```

### Failure (No Events)
```
[Step 6] Verifying collected events...
✗ No events collected!
ℹ Expected events for LIST command:
ℹ   - TRDOS_ENTRY
ℹ   - COMMAND_START
ℹ   - FDC_CMD_READ (catalog sectors)

============================================================
✗ VERIFICATION FAILED
No events were collected.
============================================================
```

## Exit Codes

- `0` - Verification passed
- `1` - Verification failed
- `130` - Interrupted by user (Ctrl+C)

## Troubleshooting

### Connection Error
```
✗ Cannot connect to WebAPI at http://localhost:8090
ℹ Make sure the emulator is running with WebAPI enabled
```

**Solution**: Start the emulator with WebAPI enabled on port 8090.

### No Emulators Found
```
⚠ No running emulator instances found
```

**Solution**: Start an emulator instance before running the verification.

### Activation Failed
```
✗ HTTP error during activation: 404
ℹ Endpoint not found. Check if analyzer API is implemented.
```

**Solution**: Verify the WebAPI has the analyzer endpoints implemented.

### No Events Collected
```
✗ No events collected!
```

**Possible causes**:
1. Analyzer not properly activated
2. TR-DOS ROM not available in emulator
3. BASIC command execution failed
4. Dispatch chain broken

**Solution**: Check emulator logs and verify TR-DOS ROM is loaded.

## Integration with CI/CD

### GitHub Actions Example
```yaml
- name: Verify TR-DOS Analyzer
  run: |
    python tools/verification/analyzers/trdos/verify_trdos_analyzer.py
```

### Jenkins Example
```groovy
stage('Verify TR-DOS Analyzer') {
    steps {
        sh 'python tools/verification/analyzers/trdos/verify_trdos_analyzer.py'
    }
}
```

## Development

### Running Tests
```bash
# Install dependencies
pip install requests

# Run verification
python verify_trdos_analyzer.py

# Run with verbose output
python verify_trdos_analyzer.py --verbose
```

### Modifying Expected Events

Edit the `verify_events()` method to change expected event types:

```python
expected_events = ['TRDOS_ENTRY', 'COMMAND_START', 'YOUR_EVENT']
```

## See Also

- [Testing Summary](../../docs/inprogress/2026-01-21-trdos-analyzer/testing-summary.md)
- [Quick Reference](../../docs/inprogress/2026-01-21-trdos-analyzer/quick-reference.md)
- [WebAPI Documentation](../../docs/analysis/capture/high-level-disk-operations.md#section-21-webapi-testing)
