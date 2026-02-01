# TRDOSAnalyzer Control Integration - Implementation Plan

## Goal

Integrate TRDOSAnalyzer into the **unified analyzer framework** with automation control (Phase 1) and UI (Phase 2).

---

## Phase 1: Automation Control

### Design: Unified Analyzer Commands

TRDOSAnalyzer registers as one of many analyzers in [AnalyzerManager](core/src/debugger/analyzers/analyzermanager.h#44-47). Control via unified `analyzer` CLI commands:

```
analyzer list                    - List all registered analyzers
analyzer enable <name>           - Enable analyzer by name
analyzer disable <name>          - Disable analyzer
analyzer status [<name>]         - Show status (all or specific)
analyzer <name> events [--since] - Get analyzer-specific events
analyzer <name> clear            - Clear analyzer buffer
analyzer <name> export <path>    - Export to file
```

### Files to Create/Modify

---

#### [NEW] `cli-processor-analyzer-mgr.cpp`

**Location**: `core/automation/cli/src/commands/cli-processor-analyzer-mgr.cpp`

Unified analyzer commands using [AnalyzerManager](core/src/debugger/analyzers/analyzermanager.h#44-47):

```cpp
void CLIProcessor::HandleAnalyzer(const ClientSession& session, 
                                   const std::vector<std::string>& args);
```

| Subcommand | Implementation |
|:-----------|:---------------|
| `list` | `manager->getRegisteredAnalyzers()` |
| `enable <name>` | `manager->activate(name)` |
| `disable <name>` | `manager->deactivate(name)` |
| `status` | `manager->getActiveAnalyzers()` + per-analyzer stats |
| `<name> events` | Cast to specific type, call [getEvents()](core/src/debugger/analyzers/trdos/trdosanalyzer.cpp#261-265) |
| `<name> clear` | Cast to specific type, call [clear()](core/src/debugger/analyzers/trdos/trdosanalyzer.cpp#286-291) |

---

#### [MODIFY] [debugmanager.cpp](core/src/debugger/debugmanager.cpp)

**Location**: [core/src/debugger/debugmanager.cpp](core/src/debugger/debugmanager.cpp)

Register TRDOSAnalyzer during initialization:

```cpp
#include "debugger/analyzers/trdos/trdosanalyzer.h"

void DebugManager::init() 
{
    // Register analyzers
    _analyzerManager->registerAnalyzer("trdos", 
        std::make_unique<TRDOSAnalyzer>(_context));
    
    // ROMPrintDetector already exists
    _analyzerManager->registerAnalyzer("romprint", 
        std::make_unique<ROMPrintDetector>());
}
```

---

#### [MODIFY] [cli-processor.cpp](core/automation/cli/src/cli-processor.cpp) + [.h](core/src/mods.h)

Add command dispatch for `analyzer`:

```cpp
{"analyzer", &CLIProcessor::HandleAnalyzer},
```

---

### Files Changed Summary

| File | Change |
|:-----|:-------|
| `cli/src/commands/cli-processor-analyzer-mgr.cpp` | [NEW] Unified analyzer commands |
| `cli/include/cli-processor.h` | Add `HandleAnalyzer` |
| [cli/src/cli-processor.cpp](core/automation/cli/src/cli-processor.cpp) | Add dispatch entry |
| [debugger/debugmanager.cpp](core/src/debugger/debugmanager.cpp) | Register TRDOSAnalyzer |

---

## Use-Case Examples

### Example 1: `LOAD "game"` Command

```
> analyzer enable trdos
TRDOSAnalyzer activated.

> ; User types LOAD "game" in TR-DOS

> analyzer trdos events
[0001234] TR-DOS Entry (PC=$3D00)
[0001456] Command: LOAD "game"
[0002100] FDC Restore (T0)
[0002500] FDC Read Sector T0/S1
[0002800] Sector Transfer: 256 bytes, T0/S1
[0003100] FDC Read Sector T0/S2
[0003400] Sector Transfer: 256 bytes, T0/S2
... (catalog sectors 1-8)
[0005500] FDC Seek T1
[0005900] FDC Read Sector T1/S10
[0006200] Sector Transfer: 256 bytes, T1/S10
... (49 sectors for BASIC module)
[0015234] FDC Seek T4
[0015600] FDC Read Sector T4/S1
[0015900] Sector Transfer: 256 bytes, T4/S1
... (96 sectors for CODE module)
[0045780] TR-DOS Exit

> analyzer status trdos
Analyzer: trdos
State: IDLE
Events: 203
Total produced: 203
Evicted: 0
```

### Example 2: `CAT` Command

```
> analyzer enable trdos
> analyzer trdos clear

> ; User types CAT in TR-DOS

> analyzer trdos events
[0000500] TR-DOS Entry (PC=$3D00)
[0000700] Command: CAT
[0001200] FDC Restore (T0)
[0001500] FDC Read Sector T0/S1
[0001800] Sector Transfer: 256 bytes, T0/S1
[0002100] FDC Read Sector T0/S2
[0002400] Sector Transfer: 256 bytes, T0/S2
... (8 catalog sectors)
[0004500] TR-DOS Exit

> analyzer status trdos
Events: 18
```

### Example 3: Custom Loader Detection

```
> analyzer enable trdos

> ; Game loads via TR-DOS, then launches custom loader

> analyzer trdos events
[0001234] TR-DOS Entry (PC=$3D00)
[0001500] Command: LOAD "elite"
[0003500] FDC Read Sector T1/S5
[0003800] Sector Transfer: 256 bytes, T1/S5
... (loader code)
[0010000] TR-DOS Exit
[0050000] FDC Restore (T0)              ; *** After TR-DOS exit = CUSTOM ***
[0050500] FDC Seek T10
[0051000] FDC Read Sector T10/S1
[0051300] Sector Transfer: 256 bytes, T10/S1
[0052000] FDC Read Sector T10/S2
[0052300] Sector Transfer: 256 bytes, T10/S2
... (custom loading continues)

> analyzer status trdos
Analyzer: trdos
State: IN_CUSTOM                        ; Detected custom loader mode
Events: 89
```

### Example 4: FORMAT Detection

```
> analyzer enable trdos
> analyzer trdos clear

> ; User types FORMAT

> analyzer trdos events --limit 20
[0000100] TR-DOS Entry (PC=$3D00)
[0000300] Command: FORMAT
[0001000] FDC Restore (T0)
[0001500] FDC Write Track T0            ; FORMAT = Write Track command
[0005000] Sector Transfer: 6400 bytes, T0
[0005500] FDC Seek T1
[0006000] FDC Write Track T1
[0009500] Sector Transfer: 6400 bytes, T1
... (80 tracks total)

> analyzer status trdos
Events: 323
Total produced: 323
```

---

## Phase 2: UI (Deferred)

Add **Analyzers Panel** to debugger per architecture doc Section 7.1.

*Will create separate plan after Phase 1 verification.*
