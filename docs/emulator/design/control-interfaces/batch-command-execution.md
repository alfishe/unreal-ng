# Batch Command Execution

> **Status**: Design Approved | **Thread Pool Size**: 4 threads

## Goal

Enable parallel execution of multiple commands across emulator instances in a single API transaction, reducing HTTP overhead and achieving sub-20ms execution for 180+ emulator operations in videowall scenarios.

## Executive Summary

The Batch Command API provides a transaction-based mechanism for executing multiple independent commands across emulator instances in parallel. Using a fixed 4-thread pool, 180 snapshot loads complete in **2-3 ms** - well under the 20-40ms target required for seamless 50Hz frame-rate effects.

**Key Metrics**:
| Scenario | Sequential | Batch (4 threads) | Improvement |
|----------|------------|-------------------|-------------|
| 180 snapshots | 19.1 ms | 2.3 ms | **8.3x faster** |
| 48 snapshots | 3.3 ms | 2.3 ms | 1.4x faster |
| HTTP overhead | 36 ms (180 calls) | 0.2 ms (1 call) | **180x reduction** |

---

## High-Level Design

### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Client Application                        │
│                  (VideoWall, Python Script, etc.)                │
└─────────────────────────────────┬───────────────────────────────┘
                                  │
                      POST /api/batch/execute
                      { commands: [...] }
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Batch Command Processor                     │
│                                                                   │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │                    Command Validator                         │ │
│  │  • Check all commands are batchable                          │ │
│  │  • Validate emulator IDs exist                               │ │
│  │  • Verify no state-dependent commands                        │ │
│  └─────────────────────────────┬───────────────────────────────┘ │
│                                │                                  │
│  ┌─────────────────────────────▼───────────────────────────────┐ │
│  │                Fixed Thread Pool (4 threads)                 │ │
│  │                                                               │ │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐         │ │
│  │  │Thread 1 │  │Thread 2 │  │Thread 3 │  │Thread 4 │         │ │
│  │  │Load #0  │  │Load #1  │  │Load #2  │  │Load #3  │         │ │
│  │  │Load #4  │  │Load #5  │  │Load #6  │  │Load #7  │         │ │
│  │  │  ...    │  │  ...    │  │  ...    │  │  ...    │         │ │
│  │  └─────────┘  └─────────┘  └─────────┘  └─────────┘         │ │
│  └─────────────────────────────┬───────────────────────────────┘ │
│                                │                                  │
│  ┌─────────────────────────────▼───────────────────────────────┐ │
│  │                    Result Aggregator                         │ │
│  │  • Collect all command results                               │ │
│  │  • Report partial failures                                   │ │
│  │  • Return unified response                                   │ │
│  └─────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

### API Endpoints

#### Simple Batch Execute (Recommended)
```http
POST /api/batch/execute
Content-Type: application/json

{
    "commands": [
        {"emulator": "emu-001", "command": "load-snapshot", "path": "/data/game1.sna"},
        {"emulator": "emu-002", "command": "load-snapshot", "path": "/data/game2.sna"},
        {"emulator": "emu-003", "command": "reset"},
        {"emulator": "emu-004", "command": "feature", "name": "sound", "value": false}
    ]
}

Response:
{
    "success": true,
    "total": 4,
    "succeeded": 4,
    "failed": 0,
    "duration_ms": 2.3,
    "results": [
        {"emulator": "emu-001", "command": "load-snapshot", "success": true},
        {"emulator": "emu-002", "command": "load-snapshot", "success": true},
        {"emulator": "emu-003", "command": "reset", "success": true},
        {"emulator": "emu-004", "command": "feature", "success": true}
    ]
}
```

#### Transaction-Based (For Complex Workflows)
```http
POST /api/batch/start       → {"batchId": "abc123"}
POST /api/batch/abc123/add  → {"emulator": 0, "command": "load-snapshot", ...}
POST /api/batch/abc123/add  → {"emulator": 1, "command": "load-snapshot", ...}
POST /api/batch/abc123/execute  → Parallel execution, returns results
POST /api/batch/abc123/cancel   → (abort if needed)
```

### CLI Batch Mode

The CLI uses a **batch scope** to collect commands before execution:

```bash
> batch start                    # Enter batch mode
[batch]> load emu-001 game1.sna  # Validated, queued (not executed)
[batch]> load emu-002 game2.sna  # Validated, queued
[batch]> reset emu-003           # Validated, queued
[batch]> step emu-001            # ERROR: 'step' not batchable
[batch]> status                  # Show queued commands (3 pending)
[batch]> execute                 # Execute all, exit batch mode
Executed 3 commands in 2.3ms (3 success, 0 failed)
>
```

**Batch Scope Behavior**:
- Prompt changes to `[batch]>` indicating batch mode
- Commands are **validated immediately** but **queued for later**
- Non-batchable commands rejected with error (batch continues)
- `execute` runs all queued commands, returns to normal mode
- `cancel` discards all queued commands, returns to normal mode
- `status` shows pending command count and list

**Implementation Approach**:
```cpp
class CLIBatchHandler {
    std::vector<BatchCommand> _pendingCommands;
    bool _inBatchMode = false;
    
    void ProcessInput(const std::string& input) {
        if (input == "batch start") {
            _inBatchMode = true;
            _pendingCommands.clear();
            return;
        }
        
        if (_inBatchMode) {
            if (input == "execute") {
                ExecuteBatch(_pendingCommands);
                _inBatchMode = false;
            } else if (input == "cancel") {
                _pendingCommands.clear();
                _inBatchMode = false;
            } else if (input == "status") {
                ShowPendingCommands();
            } else {
                // Parse and validate command
                auto cmd = ParseCommand(input);
                if (!IsBatchable(cmd)) {
                    PrintError("Command not batchable: " + cmd.name);
                } else if (!ValidateCommand(cmd)) {
                    PrintError("Validation failed: " + cmd.error);
                } else {
                    _pendingCommands.push_back(cmd);
                    Print("Queued: " + cmd.ToString());
                }
            }
        } else {
            // Normal command processing
            ExecuteImmediate(input);
        }
    }
};
```

**Key Design Points**:
1. **Reuses existing validators** - same validation as direct commands
2. **Immediate feedback** - errors shown when command is typed, not at execute time
3. **Clear visual indicator** - `[batch]>` prompt shows batch mode active
4. **Safe exit** - `cancel` always available to abort

---

## Command Batchability Matrix

Commands are classified by their suitability for batch execution:

| Category | Command | Batchable | Notes |
|----------|---------|-----------|-------|
| **Lifecycle** | `create` | ✅ | Independent instances |
| | `start` | ✅ | Independent lifecycle |
| | `stop` | ✅ | Independent cleanup |
| | `reset` | ✅ | Independent per emulator |
| | `select` | ❌ | Changes global state |
| | `open` / `load` | ✅ | Independent file ops |
| **Execution** | `pause` | ✅ | Independent |
| | `resume` | ✅ | Independent |
| | `step` | ❌ | State-dependent, needs result |
| | `stepover` | ❌ | State-dependent, needs result |
| | `steps <N>` | ❌ | State-dependent |
| **State Inspection** | `registers` | ⚠️ | Read-only, output-heavy |
| | `memory` | ⚠️ | Read-only, large output |
| | `memcounters` | ⚠️ | Read-only |
| | `calltrace` | ⚠️ | Read-only |
| **Breakpoints** | `bp` / `wp` | ✅ | Independent per emulator |
| | `bpclear` | ✅ | Independent |
| | `bpon` / `bpoff` | ✅ | Independent |
| | `bplist` | ⚠️ | Read-only |
| **Features** | `feature <name> on/off` | ✅ | Independent config |
| | `feature list` | ⚠️ | Read-only |
| **System State** | `state *` | ⚠️ | All read-only |

**Legend**: ✅ Safe to batch | ❌ Not batchable | ⚠️ Read-only (batchable but output-heavy)

---

## When to Use Batch Commands

### ✅ Recommended Use Cases

| Scenario | Commands | Benefit |
|----------|----------|---------|
| **Videowall snapshot waves** | 48-180× `load-snapshot` | 8-10x speedup |
| **Bulk pause/resume** | N× `pause` or `resume` | Single API call |
| **Feature configuration** | N× `feature sound off` | Consistent state |
| **Mass reset** | N× `reset` | Synchronized timing |
| **Initial setup** | N× `create` + `load-snapshot` | Fast startup |

### ❌ When NOT to Use

| Scenario | Reason | Alternative |
|----------|--------|-------------|
| **Single-instance debugging** | No parallelism benefit | Use direct commands |
| **Step-by-step execution** | State-dependent, needs immediate feedback | Use `step` directly |
| **Breakpoint hit analysis** | Requires sequential examination | Use direct commands |
| **Memory inspection** | Large output, need to read results | Use direct commands |
| **Interactive debugging session** | Need real-time response | Use CLI/direct API |

### Decision Flowchart

```
                    ┌─────────────────────────┐
                    │  How many commands?     │
                    └───────────┬─────────────┘
                                │
              ┌─────────────────┼─────────────────┐
              │                 │                 │
           1 command        2-10 commands     10+ commands
              │                 │                 │
              ▼                 ▼                 ▼
        ┌─────────┐       ┌─────────┐       ┌─────────┐
        │ Direct  │       │ Consider│       │  Batch  │
        │  API    │       │  Batch  │       │   API   │
        └─────────┘       └────┬────┘       └─────────┘
                               │
                    ┌──────────┼──────────┐
                    │                     │
              Same command          Different commands
              multiple targets      or debugging
                    │                     │
                    ▼                     ▼
              ┌─────────┐           ┌─────────┐
              │  Batch  │           │ Direct  │
              │   API   │           │  API    │
              └─────────┘           └─────────┘
```

---

## Implementation Details

### Thread Pool Configuration

```cpp
// Fixed 4-thread pool - optimal for M1/M2 efficiency cores
// Benchmarked: 4-8 threads show similar performance
// More threads add context-switch overhead without benefit
constexpr int BATCH_THREAD_POOL_SIZE = 4;

class BatchCommandProcessor {
    ThreadPool _pool{BATCH_THREAD_POOL_SIZE};
    
    BatchResult Execute(const std::vector<BatchCommand>& commands) {
        std::vector<std::future<CommandResult>> futures;
        
        for (const auto& cmd : commands) {
            futures.push_back(_pool.enqueue([&cmd]() {
                return DispatchToEmulator(cmd);
            }));
        }
        
        // Wait for all and aggregate results
        BatchResult result;
        for (auto& f : futures) {
            result.results.push_back(f.get());
        }
        return result;
    }
};
```

### Validation Rules

1. **All commands must be batchable** - reject batch if any command is not batchable
2. **All emulator IDs must exist** - validate before execution
3. **No duplicate emulator+command pairs** - prevent race conditions
4. **Timeout per command** - 5 second default, configurable

### Error Handling

```json
{
    "success": false,
    "total": 4,
    "succeeded": 3,
    "failed": 1,
    "results": [
        {"emulator": "emu-001", "success": true},
        {"emulator": "emu-002", "success": false, "error": "Snapshot file not found"},
        {"emulator": "emu-003", "success": true},
        {"emulator": "emu-004", "success": true}
    ]
}
```

---

## Alternative Approaches Not Selected

| Approach | Description | Why Not Selected |
|----------|-------------|------------------|
| **Async + Fence** | Fire-and-forget with final sync call | Harder to track which commands are pending |
| **WebSocket streaming** | Single connection, stream commands | Added complexity, not needed for sub-20ms |
| **Per-emulator thread** | 180 threads for 180 emulators | Thread creation overhead (14ms vs 2ms) |
| **Dynamic thread pool** | Scale threads based on command count | Added complexity, fixed 4 is sufficient |
| **Skip screen rendering** | Optimize by skipping FillBorder/Render | Causes visual artifacts, black tiles |

---

## Performance Benchmarks

Tests on Apple M1 Ultra, 180 emulator instances:

| Strategy | Wall Time | Speedup |
|----------|-----------|---------|
| Sequential (baseline) | 19.1 ms | 1x |
| ThreadPool (4 threads) | 2.3 ms | **8.3x** |
| ThreadPool (6 threads) | 2.2 ms | 8.7x |
| ThreadPool (8 threads) | 2.0 ms | 9.6x |
| ThreadPool (12 threads) | 2.2 ms | 8.7x |

**Conclusion**: 4 threads provides optimal balance of performance and resource usage.

---

## Conclusion

The Batch Command API with a fixed 4-thread pool delivers:

- **8x speedup** for 180-instance snapshot loading (19ms → 2.3ms)
- **180x HTTP overhead reduction** (single API call vs 180 calls)
- **Simple implementation** using existing command infrastructure
- **Clear batchability rules** preventing misuse with state-dependent commands

This approach is ideal for videowall applications requiring synchronized multi-instance operations while remaining simple enough for straightforward implementation and maintenance.

---

## References

- [Snapshot Loading Optimizations](../../inprogress/2026-01-13-snapshot-loading/snapshot-loading-optimizations.md)
- [Command Interface Documentation](./command-interface.md)
- [Benchmark Source: videowall_benchmark.cpp](../../../../core/benchmarks/loaders/videowall_benchmark.cpp)
