# SegmentationWidget — Companion App Design

**Status**: Design — ready for implementation  
**Target**: `unreal-log-viewer/src/SegmentationWidget.h/cpp`  
**Tab position**: Between "Heatmap" and "Traces"

---

## Context

The emulator writes a `SEGMENTATION_MAP` SHM section every frame:
- `SegmentationMapHeader` (12 bytes): `refresh_frame`, `region_count`, `max_regions`, `flags`
- `SegmentationRegionPOD[]` (12 bytes × N, max 512):
  `start_address`, `end_address`, `block_type`, `page_type`, `page_index`, `tags`

Flags: `SEGMAP_BEHAVIOR_CHANGED` (0x0001) — regions in flux, pending re-analysis.

Full definitions: `core/src/emulator/monitoring/manifest.h`

---

## Widget Lifecycle (mirrors HeatmapWidget exactly)

```cpp
class SegmentationWidget : public QWidget {
public slots:
    void attachToShm(void* base, size_t size);  // starts refresh timer
    void detachFromShm();                         // stops timer, clears state
    void onFrameReady();                          // throttled by FRAME_DIVISOR
    void setFifoActive(bool active);              // toggle polling vs FIFO-driven
};
```

No WebAPI calls for data — SHM only.  
Feature control (enable/disable): via `feature memory-region-analyzer on/off` WebAPI call.

---

## Controls

```
[ Analyzing... ]  [ Stop ]   [ Reset ]        frame: 12345
```

- **Start/Stop button** — toggles `feature memory-region-analyzer on/off`
  - Uses `PUT /api/v1/emulator/{id}/feature/memory-region-analyzer {enabled: true/false}`
  - Button text: "Analyzing…" when active, "Analyze" when inactive
  - State synced from SHM: `region_count == 0 && refresh_frame == 0` → inactive
- **Reset button** — calls `POST /api/v1/emulator/{id}/analyzer/memory-region/session {action: "reset"}`
  - Clears accumulated stats, forces full re-analysis (stays active)

---

## Visual Layout

```
┌─────────────────────────────────────────────────────────────┐
│ [ Analyze ] [ Reset ]                     frame: 12345      │  ← toolbar
├─────────────────────────────────────────────────────────────┤
│ ⚠  Behavior changing — regions are being re-analyzed        │  ← conditional
├─────────────────────────────────────────────────────────────┤
│  $0000                                              $FFFF   │
│  ████████████░░░░░░░░░░░░░░░░░░░▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░  │  ← QImage strip
│   CODE       UNKNOWN            DATA  UNKNOWN               │
├─────────────────────────────────────────────────────────────┤
│ ┌──────┬──────┬──────────┬────────┬──────────────────────┐ │
│ │Start │ End  │  Type    │ Page   │ Tags                 │ │  ← QTableWidget
│ ├──────┼──────┼──────────┼────────┼──────────────────────┤ │
│ │$0000 │$3FFF │ CODE     │ ROM 0  │ EntryPoint, KernelROM│ │
│ │$4000 │$57FF │ DATA     │ RAM 5  │ Screen               │ │
│ │$5800 │$5AFF │ DATA     │ RAM 5  │ Screen, Attr         │ │
│ │$5C00 │$5CFF │ DATA     │ RAM 5  │ SysVars              │ │
│ │$8000 │$8FFF │ CODE     │ RAM 2  │ Loader               │ │
│ │$9000 │$BFFF │ UNKNOWN  │ RAM 2  │ (none)               │ │
│ └──────┴──────┴──────────┴────────┴──────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### Map Strip

- `QImage` 65536 × 1 pixel, scaled to widget width
- One pixel per Z80 address byte, colored by `BlockType`:

| BlockType    | Color          |
|-------------|----------------|
| CODE        | `#4caf50` green |
| DATA        | `#2196f3` blue  |
| VARIABLE    | `#00bcd4` cyan  |
| SMC         | `#ff9800` orange |
| UNKNOWN     | `#424242` dark gray |

- Bank dividers: white tick marks at 0x4000, 0x8000, 0xC000

### Region Table

Columns: Start (hex), End (hex), Type (colored text), Page, Tags  
Sorted by `start_address` (ascending) by default.

**Page column**: `page_type` (0=RAM→"RAM", 1=ROM→"ROM", 2=CACHE→"Cache", 3=MISC→"Misc") + `page_index`.  
When `page_index == 0xFFFF`: display "Z80" (logical address only, no physical page info).

**Tags column**: Decompose `tags` uint32_t into `MemoryTag` flag names, join with ", ". Empty → "(none)".

### Behavior Banner

```cpp
// Shown when SEGMAP_BEHAVIOR_CHANGED flag is set
_warningLabel->setText("⚠  Behavior changing — regions are being re-analyzed");
_warningLabel->setStyleSheet("background: #7f4000; color: #ffcc80; padding: 4px 8px;");
_warningLabel->setVisible(behaviorChanging);
```

---

## SHM Read Path

```cpp
void SegmentationWidget::onRefresh()
{
    if (!_shmBase) return;

    auto* manifest = static_cast<ManifestHeader*>(_shmBase);
    for (uint16_t i = 0; i < manifest->section_count; i++)
    {
        auto* desc = getDescriptor(_shmBase, manifest, i);
        if (!desc || desc->type != SectionType::SEGMENTATION_MAP) continue;

        uint64_t before = desc->epoch.load(std::memory_order_acquire);
        if (before == EPOCH_UPDATING) continue;

        auto* data = static_cast<const uint8_t*>(getSectionData(_shmBase, desc));
        if (!data) continue;

        auto* hdr = reinterpret_cast<const SegmentationMapHeader*>(data);
        uint16_t count = std::min(hdr->region_count, hdr->max_regions);

        _regions.resize(count);
        const auto* src = reinterpret_cast<const SegmentationRegionPOD*>(
            data + sizeof(SegmentationMapHeader));
        std::memcpy(_regions.data(), src, count * sizeof(SegmentationRegionPOD));

        std::atomic_thread_fence(std::memory_order_acquire);
        uint64_t after = desc->epoch.load(std::memory_order_relaxed);
        if (before != after) continue;  // torn — skip

        _refreshFrame = hdr->refresh_frame;
        _behaviorChanging = (hdr->flags & SEGMAP_BEHAVIOR_CHANGED) != 0;
        _hasData = true;
        renderMap();
        populateTable();
        _warningLabel->setVisible(_behaviorChanging);
        break;
    }
}
```

---

## MainWindow Integration

### 1. Member + forward decl

```cpp
// MainWindow.h
class SegmentationWidget;
SegmentationWidget* _segmentationView = nullptr;
```

### 2. Tab insertion (between Heatmap and Traces)

```cpp
// MainWindow.cpp setupUI()
_segmentationView = new SegmentationWidget();
_tabWidget->addTab(_logView,          "Logs");
_tabWidget->addTab(_framebufferView,  "Screen");
_tabWidget->addTab(_heatmapView,      "Heatmap");
_tabWidget->addTab(_segmentationView, "Segmentation");  // ← new
_tabWidget->addTab(_traceView,        "Traces");
_tabWidget->addTab(_chipView,         "Chips");
```

### 3. Signal wiring (alongside existing widgets)

```cpp
connect(_logView, &LogViewWidget::shmAttached,
        _segmentationView, &SegmentationWidget::attachToShm);
connect(_logView, &LogViewWidget::shmDetached,
        _segmentationView, &SegmentationWidget::detachFromShm);
connect(_logView, &LogViewWidget::frameReady,
        _segmentationView, &SegmentationWidget::onFrameReady);

// In attached lambda, also set FIFO active for segmentation view:
_segmentationView->setFifoActive(fifo);

// Pass API client for Start/Stop/Reset buttons:
_segmentationView->setApiClient(_webApiClient.get(), emulatorId);
```

---

## Files to Create / Modify

| File | Action |
|------|--------|
| `unreal-log-viewer/src/SegmentationWidget.h` | **Create** |
| `unreal-log-viewer/src/SegmentationWidget.cpp` | **Create** |
| `unreal-log-viewer/src/MainWindow.h` | Add `SegmentationWidget*` member |
| `unreal-log-viewer/src/MainWindow.cpp` | Instantiate, add tab, wire signals |
| `unreal-log-viewer/CMakeLists.txt` | Add source files |

---

## WebAPI Actions Used

| Action | Endpoint |
|--------|----------|
| Start analysis | `PUT /api/v1/emulator/{id}/feature/memory-region-analyzer {"enabled": true}` |
| Stop analysis | `PUT /api/v1/emulator/{id}/feature/memory-region-analyzer {"enabled": false}` |
| Reset analysis | `POST /api/v1/emulator/{id}/analyzer/memory-region/session {"action": "reset"}` |
| Check state | `GET /api/v1/emulator/{id}/analyzer/memory-region` |

> Reset action is now implemented in `analyzers_api.cpp` — calls `forceRefresh()` on the analyzer.
