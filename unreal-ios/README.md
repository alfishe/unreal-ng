# UnrealNG iOS

iOS/iPadOS native app for the UnrealNG ZX Spectrum emulator.

## Prerequisites

- macOS with Xcode 15+ installed
- CMake 3.20+
- iOS SDK (included with Xcode)
- Apple Developer account (for device deployment)

## Building the XCFramework

The iOS app requires a pre-built xcframework containing the emulator core. Run:

```bash
./unreal-ios/scripts/build-ios-xcframework.sh
```

This builds:
- Device slice (arm64)
- Simulator slice (arm64)
- Bundles them into `unreal-ios/Frameworks/UnrealNGCore.xcframework`

The build takes 5-10 minutes on first run. Subsequent builds are incremental.

## Building the iOS App

1. Open `unreal-ios/UnrealNG.xcodeproj` in Xcode
2. Select your development team in Signing & Capabilities
3. Choose target device (simulator or physical device)
4. Build and run (Cmd+R)

## Architecture

```
unreal-ios/
├── Frameworks/           # Pre-built xcframework
│   └── UnrealNGCore.xcframework/
├── UnrealNG/
│   ├── App/              # AppDelegate, SceneDelegate
│   ├── Audio/            # CoreAudio AudioUnit integration
│   ├── Bridge/           # Obj-C++ bridge to C API
│   └── Screen/           # Metal rendering, keyboard handling
└── scripts/              # Build scripts
```

### Key Components

- **UNGBridge**: Obj-C++ wrapper around the C embed API (`unrealng_embed.h`)
- **ScreenViewController**: Metal-based display with CAMetalLayer
- **AudioSession**: Pull-based audio via AudioUnit (RemoteIO)

## Potential Issues and Solutions

### 1. XCFramework Build Fails: Lua `system()` unavailable

**Error:**
```
loslib.c: error: 'system' has been explicitly marked unavailable here
```

**Cause:** iOS prohibits `system()` calls for security reasons.

**Solution:** The CMake config defines `LUA_USE_IOS` for iOS builds, which stubs out `system()`. If this error appears, ensure you're building with the latest CMake files:

```bash
rm -rf cmake-build-ios-*
./unreal-ios/scripts/build-ios-xcframework.sh
```

### 2. Muddy/Blurry Display on Retina Devices

**Cause:** CAMetalLayer not scaled for device pixel density.

**Solution:** Ensure `ScreenViewController` sets:
```swift
metalLayer.contentsScale = UIScreen.main.scale
```

### 3. Network Requests Fail on Device

**Error:** `NSURLErrorDomain` errors when accessing WebAPI.

**Cause:** iOS App Transport Security (ATS) blocks non-HTTPS local connections.

**Solution:** `Info.plist` must include:
```xml
<key>NSAppTransportSecurity</key>
<dict>
    <key>NSAllowsLocalNetworking</key>
    <true/>
</dict>
```

### 4. Audio Doesn't Play or Crackles

**Cause:** Sample rate mismatch or AudioSession not configured.

**Solution:**
1. Check that `AudioSession.swift` queries the hardware sample rate:
   ```swift
   let sampleRate = AVAudioSession.sharedInstance().sampleRate
   ```
2. Ensure the emulator core is initialized with matching rate
3. For crackling, increase buffer size in AudioUnit configuration

### 5. Keyboard Input Not Working

**Cause:** Bluetooth keyboard events not forwarded to emulator.

**Solution:** Ensure `ScreenViewController`:
- Returns `true` from `canBecomeFirstResponder`
- Calls `becomeFirstResponder()` in `viewDidAppear`
- Implements `pressesBegan`/`pressesEnded` to forward to `UNGBridge.keyPress`/`keyRelease`

### 6. App Hangs After Multiple HTTP Requests

**Cause:** Drogon thread exhaustion or GetMachineIdentity crash (fixed in master).

**Solution:** Ensure you have the latest core with these commits:
- `fix(emulator): prevent GetMachineIdentity crash from dangling pointer`
- `fix(emulator): reduce WaitForPauseConfirmation timeout to 100ms`

Rebuild xcframework after pulling latest code.

### 7. Upload/Snapshot Load Fails on Device

**Error:** "Failed to create staging file" or similar.

**Cause:** Attempting to write to read-only app bundle.

**Solution:** The upload helper must use writable paths. Ensure `UploadHelper` uses:
```cpp
FileHelper::GetWritablePath()  // Returns Documents or Library/Application Support
```
Not:
```cpp
FileHelper::GetExecutablePath()  // Read-only on iOS
```

### 8. Simulator Works But Device Doesn't

**Cause:** Missing arm64 device slice or code signing issues.

**Solution:**
1. Verify xcframework has device slice:
   ```bash
   lipo -info unreal-ios/Frameworks/UnrealNGCore.xcframework/ios-arm64/libUnrealNGCore.a
   ```
2. Check Xcode signing configuration
3. Ensure provisioning profile includes the device

## Remote Control

The app exposes a WebAPI on port 8090 for remote control. Use the Python test tool:

```bash
cd tools/poc/015-ios-remote-control
python3 main.py
```

Features:
- Load snapshots, disks, tapes
- Send keyboard input
- Monitor emulator state

## WebAPI Access from Mac

Find the device IP in Settings > Wi-Fi, then:

```bash
# Check status
curl http://<device-ip>:8090/api/v1/emulator/status

# List emulators
curl http://<device-ip>:8090/api/v1/emulator

# Load snapshot (raw binary)
curl -X POST "http://<device-ip>:8090/api/v1/emulator/<id>/snapshot/load" \
  -H "Content-Type: application/octet-stream" \
  -H "X-Filename: game.sna" \
  --data-binary @game.sna
```

## Tips

1. **First launch is slow**: The emulator initializes ROMs and configs on first run
2. **Use Release builds**: Debug builds are significantly slower
3. **Metal debugging**: Enable GPU Frame Capture in Xcode for graphics issues
4. **Console logs**: Use Xcode console or `os_log` for debugging on device
