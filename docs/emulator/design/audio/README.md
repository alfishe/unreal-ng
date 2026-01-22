# Audio Subsystem

## Overview

The Unreal-NG audio subsystem uses [miniaudio](https://miniaud.io/) as a cross-platform audio library. On macOS, miniaudio utilizes Core Audio for audio output.

## macOS Debug Build: Core Audio HAL Overload Warnings

### Problem

When running debug builds on macOS, you may see repeated console messages like:

```
HALC_ProxyIOContext.cpp:1622 HALC_ProxyIOContext::IOWorkLoop: skipping cycle due to overload
```

This is caused by macOS Core Audio's Hardware Abstraction Layer (HAL) detecting that the audio I/O thread cannot complete work within its real-time deadline. Debug builds run significantly slower due to:
- Disabled compiler optimizations (`-O0`)
- Debug symbols and sanitizers
- Slower memory access patterns

### Solution

Set the `OS_ACTIVITY_MODE=disable` environment variable to suppress Apple's os_log/Activity Tracing subsystem output.

#### CLion Configuration

1. **Run → Edit Configurations**
2. Select your `unreal-qt` configuration
3. **Environment variables**: Add `OS_ACTIVITY_MODE=disable`
4. Apply


#### Command Line

```bash
OS_ACTIVITY_MODE=disable ./build/bin/unreal-qt
```

#### Xcode

1. **Product → Scheme → Edit Scheme**
2. Select **Run** → **Arguments**
3. Under **Environment Variables**, add:
   - Name: `OS_ACTIVITY_MODE`
   - Value: `disable`

### Technical Details

The `HALC_ProxyIOContext::IOWorkLoop` messages are logged via Apple System Log (ASL) / `os_log`, not stderr. Setting `OS_ACTIVITY_MODE=disable` suppresses these system-level debug messages while maintaining full audio functionality.

> [!NOTE]
> This does not affect audio quality or functionality - it only suppresses the diagnostic messages that spam the console in debug builds.

### Alternatives Considered

| Approach | Result |
|----------|--------|
| Increase `periodSizeInMilliseconds` | ❌ Didn't help - HAL timeout is at the system level |
| `HALC_DebugFlags=0` | ❌ Not reliably inherited by child processes |
| Use null audio backend | ❌ No sound output |
| **`OS_ACTIVITY_MODE=disable`** | ✅ Works - suppresses os_log output |

## Audio Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Emulator Core                        │
│  ┌─────────────────────────────────────────────────┐    │
│  │              SoundManager (core)                │    │
│  │  - AY-8910 / TurboSound chip emulation          │    │
│  │  - Beeper emulation                             │    │
│  │  - Sample mixing                                │    │
│  └────────────────────┬────────────────────────────┘    │
└───────────────────────┼─────────────────────────────────┘
                        │ audioCallback()
                        ▼
┌─────────────────────────────────────────────────────────┐
│                 AppSoundManager (Qt)                    │
│  ┌─────────────────────────────────────────────────┐    │
│  │             AudioRingBuffer                     │    │
│  │  - Thread-safe sample buffer                    │    │
│  │  - Decouples emulator from audio thread         │    │
│  └────────────────────┬────────────────────────────┘    │
│                       │                                 │
│  ┌────────────────────▼────────────────────────────┐    │
│  │              miniaudio device                   │    │
│  │  - audioDataCallback() pulls from ring buffer   │    │
│  └────────────────────┬────────────────────────────┘    │
└───────────────────────┼─────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│                  macOS Core Audio                       │
│  - Real-time audio thread                               │
│  - HAL (Hardware Abstraction Layer)                     │
└─────────────────────────────────────────────────────────┘
```

## Design Documents

- **[Audio Sync Relay Thread](audio-sync-relay-thread.md)** - Design for fixing Core Audio HAL overload by moving MessageCenter calls out of real-time audio thread

## Related Files

- [`unreal-qt/src/emulator/soundmanager.cpp`](unreal-qt/src/emulator/soundmanager.cpp) - Qt audio integration
- [`core/src/emulator/sound/soundmanager.cpp`](core/src/emulator/sound/soundmanager.cpp) - Core sound mixing
- [`core/src/3rdparty/miniaudio/miniaudio.h`](core/src/3rdparty/miniaudio/miniaudio.h) - miniaudio library
