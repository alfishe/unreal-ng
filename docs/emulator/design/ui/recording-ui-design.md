# Recording UI Design

## Overview

The recording system requires a sophisticated UI to handle:
- Multiple recording modes (single-track, multi-track, channel-split, audio-only)
- Video/audio codec selection
- Multi-track audio configuration
- Format presets
- Real-time recording status
- Quick access for common use cases

This document describes a multi-tiered UI approach that balances simplicity for common tasks with power for advanced scenarios.

## UI Architecture

```
Menu Bar
├─ Tools
│  ├─ Start Recording...              [Ctrl+R]  → Opens Recording Dialog
│  ├─ Quick Record                             → Submenu for presets
│  │  ├─ Gameplay (MP4)               [Ctrl+Shift+R]
│  │  ├─ Music (FLAC)
│  │  ├─ GIF Animation
│  │  └─ WebP Animation
│  ├─ Stop Recording                  [Ctrl+Shift+S]
│  └─ Recording Settings...                    → Advanced configuration
│
└─ View
   └─ Recording Status                         → Show/hide status widget

Status Bar (when recording)
├─ [●] Recording: gameplay.mp4
├─ Duration: 00:01:23
├─ Size: 15.2 MB
└─ FPS: 50.0

Toolbar (optional)
├─ [●] Start/Stop Recording
└─ [⚙] Recording Settings
```

## UI Components

### 1. Recording Dialog (Main Interface)

Primary dialog opened by **Tools → Start Recording...** or **Ctrl+R**.

```
┌─────────────────────────────────────────────────────────────────┐
│ Start Recording                                          [?] [X] │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│ ┌─ Recording Mode ──────────────────────────────────────────┐ │
│ │ ○ Video + Audio (Standard gameplay)                       │ │
│ │ ○ Audio Only (Music/sound effects)                        │ │
│ │ ○ Multi-Track Audio (Advanced - separate device tracks)   │ │
│ │ ○ Animated Image (GIF/WebP for web sharing)              │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│ ┌─ Output ──────────────────────────────────────────────────┐ │
│ │ Filename: [gameplay.mp4                    ] [Browse...]  │ │
│ │ Format:   [MP4 (H.264 + AAC)              ▼]             │ │
│ │                                                            │ │
│ │ Preset:   [YouTube (1080p)                ▼] [Manage...] │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│ ┌─ Video Settings ──────────────────────────────────────────┐ │
│ │ ☑ Enable Video                                            │ │
│ │                                                            │ │
│ │ Codec:      [H.264                        ▼]             │ │
│ │ Resolution: [Native (256x192)            ▼]             │ │
│ │ Bitrate:    [5000         ] kbps  [Auto ☑]             │ │
│ │ Frame Rate: [50.0         ] fps                          │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│ ┌─ Audio Settings ──────────────────────────────────────────┐ │
│ │ Source: [Master Mix (All devices)        ▼]             │ │
│ │ Codec:  [AAC                              ▼]             │ │
│ │ Bitrate:[192          ] kbps  [Auto ☑]                  │ │
│ │                                                            │ │
│ │ [Configure Multi-Track...] (for advanced audio routing)   │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│ Estimated file size: ~625 KB/sec (37.5 MB/min)                │
│                                                                 │
│          [Advanced Settings...]  [ Cancel ]  [ Start ]         │
└─────────────────────────────────────────────────────────────────┘
```

**Key Features:**
- **Recording Mode** - Radio buttons for common scenarios
- **Format dropdown** - Combines container + codecs (e.g., "MP4 (H.264 + AAC)")
- **Preset system** - Quick selection of common configurations
- **Conditional UI** - Video settings greyed out for audio-only mode
- **Real-time estimates** - File size calculation
- **Progressive disclosure** - Advanced settings hidden by default

### 2. Quick Record Menu (Speed Presets)

Submenu under **Tools → Quick Record** for instant recording with presets.

```
Tools
└─ Quick Record
   ├─ Gameplay (MP4)               [Ctrl+Shift+R]
   ├─ Music (Multi-Track FLAC)
   ├─ Audio Only (WAV)
   ├─ GIF Animation
   ├─ WebP Animation
   ├─ High Quality (H.265)
   └─ Custom...                     → Opens Recording Dialog
```

**Behavior:**
- Single click starts recording immediately
- Uses predefined settings for each preset
- Shows recording status in status bar
- No dialog interruption (seamless start)

**Default Presets:**

| Preset | Mode | Format | Video Codec | Audio Codec | Use Case |
|--------|------|--------|-------------|-------------|----------|
| **Gameplay** | Video+Audio | MP4 | H.264 (5 Mbps) | AAC (192 kbps) | Standard recording |
| **Music** | Multi-Track | FLAC | None | FLAC (lossless) | Audio archival |
| **Audio Only** | Audio-Only | WAV | None | PCM 16-bit | Quick audio capture |
| **GIF Animation** | Animated | GIF | GIF (15 fps) | None | Web sharing |
| **WebP Animation** | Animated | WebP | WebP (1 Mbps) | None | Modern web |
| **High Quality** | Video+Audio | MKV | H.265 (10 Mbps) | FLAC | Archival quality |

### 3. Multi-Track Audio Configuration Dialog

Opened by **"Configure Multi-Track..."** button in Recording Dialog.

```
┌─────────────────────────────────────────────────────────────────┐
│ Multi-Track Audio Configuration                         [?] [X] │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│ ┌─ Audio Tracks ────────────────────────────────────────────┐ │
│ │ Track  Name            Source          Codec    Bitrate   │ │
│ │ ───────────────────────────────────────────────────────── │ │
│ │ ☑  1   Master Mix      Master Mix      AAC     192 kbps  │ │
│ │ ☑  2   Beeper          Beeper          FLAC    —         │ │
│ │ ☑  3   AY Chip 1       AY1 (All)       FLAC    —         │ │
│ │ ☐  4   AY Chip 2       AY2 (All)       FLAC    —         │ │
│ │ ☐  5   COVOX           COVOX           FLAC    —         │ │
│ │                                                            │ │
│ │ [Add Track] [Remove Track] [Duplicate] [Clear All]        │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│ ┌─ Track Editor (Track 2: Beeper) ──────────────────────────┐ │
│ │ Enabled:    ☑                                             │ │
│ │ Name:       [Beeper                            ]          │ │
│ │ Source:     [Beeper                            ▼]         │ │
│ │ Codec:      [FLAC (lossless)                   ▼]         │ │
│ │ Bitrate:    N/A (lossless)                                │ │
│ │ Volume:     [||||||||||||||||||||||||    ] 100%           │ │
│ │ Pan:        [        ●                   ] Center         │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│ ┌─ Presets ─────────────────────────────────────────────────┐ │
│ │ [All Devices]  [AY Only]  [Beeper Only]  [Custom...]     │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│                               [ Cancel ]  [ OK ]                │
└─────────────────────────────────────────────────────────────────┘
```

**Features:**
- **Track list** - Shows all configured tracks
- **Checkboxes** - Enable/disable tracks without removing
- **Track editor** - Modify selected track properties
- **Presets** - Quick configuration for common scenarios
- **Drag & drop** - Reorder tracks (for output file track order)

### 4. Recording Status Widget

Shows in status bar when recording is active.

```
Status Bar (Recording Active):
┌────────────────────────────────────────────────────────────┐
│ [●] gameplay.mp4 │ 00:02:15 │ 42.3 MB │ 50.0 fps │ [■]    │
└────────────────────────────────────────────────────────────┘
    ↑                ↑          ↑         ↑           ↑
    Red indicator    Duration   File size  Frame rate Stop
```

**Features:**
- **Red dot indicator** - Visual "recording in progress"
- **Duration** - Real-time elapsed time
- **File size** - Current output file size
- **Frame rate** - Actual capture rate (should match target)
- **Stop button** - Quick stop without menu

**Status Bar (Recording Paused):**
```
┌────────────────────────────────────────────────────────────┐
│ [‖] gameplay.mp4 │ 00:02:15 │ 42.3 MB │ PAUSED │ [►] [■]  │
└────────────────────────────────────────────────────────────┘
```

### 5. Advanced Settings Dialog

Opened by **"Advanced Settings..."** button in Recording Dialog.

```
┌─────────────────────────────────────────────────────────────────┐
│ Advanced Recording Settings                              [?] [X] │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│ ┌─ Video Encoder ───────────────────────────────────────────┐ │
│ │ Codec:         [H.264                          ▼]         │ │
│ │ Preset:        [Medium                         ▼]         │ │
│ │ Profile:       [High                           ▼]         │ │
│ │ CRF/Quality:   [23              ] (lower = better)        │ │
│ │ GOP Size:      [12              ] frames                  │ │
│ │ B-Frames:      [3               ]                         │ │
│ │                                                            │ │
│ │ ☑ Use hardware acceleration (if available)                │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│ ┌─ Audio Encoder ───────────────────────────────────────────┐ │
│ │ Sample Rate:   [44100           ▼] Hz                     │ │
│ │ Channels:      [Stereo          ▼]                        │ │
│ │ Bit Depth:     [16              ▼] bits                   │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│ ┌─ Performance ─────────────────────────────────────────────┐ │
│ │ Encoder threads: [Auto          ▼]                        │ │
│ │ Buffer size:     [2             ] seconds                 │ │
│ │                                                            │ │
│ │ ☑ Enable turbo mode during recording (faster capture)     │ │
│ │ ☐ Drop frames if encoder can't keep up                    │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│ ┌─ Metadata ────────────────────────────────────────────────┐ │
│ │ Title:    [                                    ]          │ │
│ │ Author:   [                                    ]          │ │
│ │ Comment:  [                                    ]          │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│                               [ Cancel ]  [ OK ]                │
└─────────────────────────────────────────────────────────────────┘
```

### 6. Recording Presets Manager

Opened by **"Manage..."** button next to preset dropdown.

```
┌─────────────────────────────────────────────────────────────────┐
│ Recording Presets Manager                                [?] [X] │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│ ┌─ Presets ─────────────────────────────────────────────────┐ │
│ │  ▶ Default Presets (read-only)                            │ │
│ │    ├─ Gameplay (MP4)                                       │ │
│ │    ├─ Music (Multi-Track FLAC)                             │ │
│ │    ├─ Audio Only (WAV)                                     │ │
│ │    ├─ GIF Animation                                        │ │
│ │    ├─ WebP Animation                                       │ │
│ │    └─ High Quality (H.265)                                 │ │
│ │                                                            │ │
│ │  ▶ User Presets                                            │ │
│ │    ├─ My YouTube Setup                            [ACTIVE] │ │
│ │    ├─ Longplay Recording                                   │ │
│ │    └─ AY Music Archive                                     │ │
│ │                                                            │ │
│ │  [New] [Duplicate] [Delete] [Export...] [Import...]       │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│ ┌─ Preset Details: My YouTube Setup ────────────────────────┐ │
│ │ Name:        [My YouTube Setup                 ]          │ │
│ │ Mode:        Video + Audio                                │ │
│ │ Format:      MP4                                          │ │
│ │ Video:       H.264, 1080p upscale, 5000 kbps              │ │
│ │ Audio:       AAC, 192 kbps, Master Mix                    │ │
│ │ Destination: [~/Videos/                    ] [Browse...]  │ │
│ │                                                            │ │
│ │ ☑ Auto-start turbo mode when recording                    │ │
│ │ ☑ Auto-stop after [60          ] minutes                  │ │
│ │                                                            │ │
│ │ [Edit...]                                                  │ │
│ └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│                                        [ Close ]                │
└─────────────────────────────────────────────────────────────────┘
```

**Features:**
- **Hierarchical presets** - Default (read-only) and User (editable)
- **Preset details** - Summary of settings
- **Import/Export** - Share presets with others
- **Auto-actions** - Turbo mode, auto-stop, etc.

## Menu Structure

### Main Menu Bar

```
Tools
├─ Start Recording...                [Ctrl+R]
├─ Stop Recording                    [Ctrl+Shift+S]  (enabled when recording)
├─ Pause Recording                   [Ctrl+Shift+P]  (enabled when recording)
├─ Resume Recording                                  (enabled when paused)
├─ ───────────────────────────────
├─ Quick Record                                      → Submenu
│  ├─ Gameplay (MP4)                 [Ctrl+Shift+R]
│  ├─ Music (Multi-Track FLAC)
│  ├─ Audio Only (WAV)
│  ├─ GIF Animation
│  ├─ WebP Animation
│  ├─ High Quality (H.265)
│  └─ ───────────────────────────────
│     └─ Custom...                                   → Opens Recording Dialog
├─ ───────────────────────────────
├─ Recording Settings...                             → Advanced configuration
└─ Recording Presets...                              → Presets manager

View
├─ Debugger                          [Ctrl+D]
├─ Log Viewer                        [Ctrl+L]
├─ ───────────────────────────────
└─ Recording Status                                  → Show/hide status widget
```

### Context Menu (Right-click on emulator screen)

```
┌────────────────────────────────┐
│ Copy Screen                    │
│ Save Screenshot...             │
│ ─────────────────────────────  │
│ Start Recording...             │
│ Stop Recording                 │  (if recording)
│ ─────────────────────────────  │
│ ...                            │
└────────────────────────────────┘
```

## UI Workflows

### Workflow 1: Quick Gameplay Recording (Beginner)

**User wants:** Record gameplay quickly

```
1. Tools → Quick Record → Gameplay (MP4)
   OR: Press Ctrl+Shift+R

2. [Status bar shows recording indicator]
   [●] gameplay_2026-01-04_143022.mp4 │ 00:00:05 │ 1.2 MB │ 50.0 fps

3. Play game...

4. Tools → Stop Recording
   OR: Click [■] in status bar
   OR: Press Ctrl+Shift+S

5. [Notification] "Recording saved: ~/Videos/gameplay_2026-01-04_143022.mp4"
```

**Time:** < 5 seconds to start recording  
**Complexity:** Minimal (no dialogs)

### Workflow 2: Custom Video Recording (Intermediate)

**User wants:** Record with specific codec and bitrate

```
1. Tools → Start Recording...
   OR: Press Ctrl+R

2. [Recording Dialog opens]
   - Select mode: "Video + Audio"
   - Filename: my_recording.mp4
   - Format: "MP4 (H.264 + AAC)"
   - Video: H.264, 1080p upscale, 8000 kbps
   - Audio: AAC, 256 kbps, Master Mix
   - Click [Start]

3. [Recording starts]

4. [Stop when done]

5. [File saved with custom settings]
```

**Time:** ~20 seconds to configure and start  
**Complexity:** Medium (one dialog)

### Workflow 3: Multi-Track Music Recording (Advanced)

**User wants:** Record AY music with separate channels for remixing

```
1. Tools → Start Recording...

2. [Recording Dialog]
   - Mode: "Multi-Track Audio"
   - Filename: ay_music.flac
   - Click [Configure Multi-Track...]

3. [Multi-Track Dialog]
   - [Add Track] × 3
   - Track 1: AY1 Channel A, FLAC
   - Track 2: AY1 Channel B, FLAC
   - Track 3: AY1 Channel C, FLAC
   - Click [OK]

4. [Recording Dialog] Click [Start]

5. [Record music demo]

6. [Stop recording]

7. [File saved with 3 separate audio tracks]
```

**Time:** ~60 seconds to configure  
**Complexity:** High (multiple dialogs, track configuration)

### Workflow 4: Animated GIF for Web (Beginner)

**User wants:** Create a 10-second GIF for Twitter

```
1. Tools → Quick Record → GIF Animation
   OR: Tools → Start Recording...
      - Mode: "Animated Image"
      - Format: GIF
      - Frame rate: 15 fps (default for GIF)

2. [Recording starts]

3. [Play for 10 seconds]

4. [Stop recording]

5. [notification] "GIF saved: ~/Videos/gameplay.gif (2.1 MB)"
   [Open Folder] [Share...]
```

**Time:** < 10 seconds  
**Complexity:** Minimal

## Implementation Details

### Qt Classes

```cpp
// Main recording dialog
class RecordingDialog : public QDialog
{
    Q_OBJECT
public:
    RecordingDialog(RecordingManager* recording, QWidget* parent = nullptr);
    
signals:
    void recordingStartRequested();
    void settingsChanged();

private slots:
    void onModeChanged(int index);
    void onFormatChanged(int index);
    void onPresetChanged(int index);
    void onBrowseFile();
    void onConfigureMultiTrack();
    void onAdvancedSettings();
    void onStart();
    
private:
    void updateUI();
    void updateEstimates();
    
    RecordingManager* _recordingManager;
    QComboBox* _modeCombo;
    QComboBox* _formatCombo;
    QComboBox* _presetCombo;
    // ... other widgets ...
};

// Multi-track configuration dialog
class MultiTrackDialog : public QDialog
{
    Q_OBJECT
public:
    MultiTrackDialog(RecordingManager* recording, QWidget* parent = nullptr);
    
    void getTrackConfiguration(std::vector<AudioTrackConfig>& tracks);
    void setTrackConfiguration(const std::vector<AudioTrackConfig>& tracks);
    
private slots:
    void onAddTrack();
    void onRemoveTrack();
    void onTrackSelectionChanged();
    void onTrackPropertyChanged();
    
private:
    QTableWidget* _trackList;
    // Track editor widgets...
};

// Recording status widget
class RecordingStatusWidget : public QWidget
{
    Q_OBJECT
public:
    RecordingStatusWidget(RecordingManager* recording, QWidget* parent = nullptr);
    
    void updateStatus();
    
private slots:
    void onStopClicked();
    
private:
    QLabel* _iconLabel;
    QLabel* _filenameLabel;
    QLabel* _durationLabel;
    QLabel* _sizeLabel;
    QLabel* _fpsLabel;
    QPushButton* _stopButton;
    
    QTimer* _updateTimer;
};

// Preset manager
class RecordingPresetManager : public QDialog
{
    Q_OBJECT
public:
    RecordingPresetManager(QWidget* parent = nullptr);
    
    void loadPresets();
    void savePresets();
    
private slots:
    void onNewPreset();
    void onDuplicatePreset();
    void onDeletePreset();
    void onExportPreset();
    void onImportPreset();
    void onEditPreset();
    
private:
    QTreeWidget* _presetTree;
    // ...
};
```

### MainWindow Integration

```cpp
// In mainwindow.h
class MainWindow : public QMainWindow
{
    // ...
private:
    RecordingDialog* _recordingDialog = nullptr;
    RecordingStatusWidget* _recordingStatus = nullptr;
    
private slots:
    void handleStartRecording();
    void handleStopRecording();
    void handlePauseRecording();
    void handleResumeRecording();
    void handleQuickRecordPreset(const QString& presetName);
    void handleRecordingSettings();
    void handleRecordingPresets();
    void updateRecordingStatus();
};

// In mainwindow.cpp
void MainWindow::handleStartRecording()
{
    if (!_recordingDialog)
    {
        _recordingDialog = new RecordingDialog(_emulator->GetRecordingManager(), this);
    }
    
    if (_recordingDialog->exec() == QDialog::Accepted)
    {
        // Recording started, show status widget
        if (_recordingStatus)
        {
            statusBar()->addWidget(_recordingStatus);
            _recordingStatus->show();
        }
    }
}

void MainWindow::handleQuickRecordPreset(const QString& presetName)
{
    // Load preset configuration
    RecordingPreset preset = _presetManager->getPreset(presetName);
    
    // Apply to recording manager
    preset.applyTo(_emulator->GetRecordingManager());
    
    // Generate filename with timestamp
    QString filename = QString("recording_%1.%2")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss"))
        .arg(preset.extension);
    
    // Start recording immediately
    _emulator->GetRecordingManager()->StartRecording(filename.toStdString());
    
    // Show status widget
    _recordingStatus->show();
}
```

## Preset Configuration Format

Store presets as JSON for easy import/export:

```json
{
  "name": "YouTube 1080p",
  "version": "1.0",
  "mode": "video_audio",
  "video": {
    "enabled": true,
    "codec": "h264",
    "resolution": "1080p",
    "bitrate": 5000,
    "framerate": 50.0,
    "preset": "medium",
    "profile": "high"
  },
  "audio": {
    "mode": "single_track",
    "source": "master_mix",
    "codec": "aac",
    "bitrate": 192,
    "samplerate": 44100
  },
  "output": {
    "container": "mp4",
    "destination": "~/Videos/"
  },
  "options": {
    "auto_turbo": true,
    "auto_stop_minutes": 0,
    "hardware_accel": true
  }
}
```

## Keyboard Shortcuts

| Action | Shortcut | Description |
|--------|----------|-------------|
| **Start Recording Dialog** | `Ctrl+R` | Open recording configuration |
| **Quick Record (Gameplay)** | `Ctrl+Shift+R` | Instant gameplay recording |
| **Stop Recording** | `Ctrl+Shift+S` | Stop current recording |
| **Pause Recording** | `Ctrl+Shift+P` | Pause (resume with same key) |
| **Screenshot** | `F12` | Quick screenshot |

## Toolbar Buttons (Optional)

```
┌────────────────────────────────────────────────────────┐
│ [File] [Edit] [View] [Run] [Debug] [Tools] [Help]     │
├────────────────────────────────────────────────────────┤
│ ▶ ‖ ■  │  ●  ⚙  │  ...                                │
│ ↑       │   ↑  ↑                                       │
│ Run     │  Rec Settings                                │
└────────────────────────────────────────────────────────┘
        Recording controls
```

**Buttons:**
- **● Record** - Quick record with last/default preset
- **⚙ Settings** - Recording dialog

## System Tray Integration (Optional)

For long recordings, minimize to system tray:

```
System Tray Icon (when recording):
┌────────────────────────────┐
│ ● Unreal NG                │
│                            │
│ Recording: 00:15:32        │
│ gameplay.mp4               │
│ 234.5 MB                   │
│                            │
│ [■] Stop Recording         │
│ [‖] Pause Recording        │
│ ────────────────────       │
│ [ ] Show Main Window       │
└────────────────────────────┘
```

## Future Enhancements

### Phase 2: Advanced Features

- **Live Preview** - Show recording output in real-time
- **Bookmarks** - Mark important moments during recording
- **Chapter Markers** - Add chapters to video timeline
- **Automatic Scenes** - Detect scene changes and add markers
- **Scheduled Recording** - Start/stop at specific times or events

### Phase 3: Post-Processing

- **Trim Editor** - Cut start/end of recording
- **Video Editor** - Basic editing (cut, splice, overlay)
- **Batch Export** - Convert recordings to different formats
- **Upload to YouTube** - Direct upload from emulator

## References

- [Recording System Architecture](../recording-system.md)
- [Audio Routing](../audio-routing.md)
- [Video & Audio Encoding](../video-audio-encoding.md)
- Qt QDialog documentation
- Qt QTableWidget documentation

