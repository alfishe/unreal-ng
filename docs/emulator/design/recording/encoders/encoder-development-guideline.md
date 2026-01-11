# Encoder Development Guideline

Guide for implementing new encoders in the recording subsystem.

---

## Required Interface

All encoders must inherit from `EncoderBase` and implement:

```cpp
class MyEncoder : public EncoderBase {
public:
    // Required methods
    bool Start(const std::string& filename, const EncoderConfig& config) override;
    void Stop() override;
    bool IsRecording() const override;
    std::string GetType() const override;
    std::string GetDisplayName() const override;
    bool SupportsVideo() const override;
    bool SupportsAudio() const override;

    // Override if supported
    void OnVideoFrame(const FramebufferDescriptor& fb, double timestampSec) override;
    void OnAudioSamples(const int16_t* samples, size_t count, double timestampSec) override;
};
```

---

## Configuration

Use `EncoderConfig` fields relevant to your encoder:

| Field | Description |
|-------|-------------|
| `videoWidth/Height` | Frame dimensions |
| `frameRate` | Target FPS (50 for ZX Spectrum) |
| `videoBitrate` | Encoding bitrate (video codecs) |
| `videoCodec` | Codec hint string |
| `audioSampleRate` | Sample rate in Hz |
| `audioBitrate` | Audio encoding bitrate |
| `gifDelayMs` | Frame delay (GIF only) |
| `captureRegion` | MainScreen or FullFrame |

---

## Error Handling

### Required Validation

```cpp
bool MyEncoder::Start(const std::string& filename, const EncoderConfig& config)
{
    // 1. Already recording check
    if (_isRecording) {
        _lastError = "Already recording";
        return false;
    }

    // 2. Filename validation
    if (filename.empty()) {
        _lastError = "Empty filename";
        return false;
    }

    // 3. Path validation
    std::filesystem::path parentDir = std::filesystem::path(filename).parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
        _lastError = "Directory does not exist";
        return false;
    }

    // 4. Dimension validation
    if (config.videoWidth == 0 || config.videoHeight == 0) {
        _lastError = "Invalid dimensions";
        return false;
    }

    // 5. Try to open file
    // ... encoder-specific initialization ...

    _isRecording = true;
    return true;
}
```

### Error Reporting

Provide `GetLastError()` method:

```cpp
std::string GetLastError() const { return _lastError; }
```

---

## Resource Management (RAII)

### Guaranteed Cleanup

```cpp
MyEncoder::~MyEncoder()
{
    if (_isRecording)
    {
        Stop();  // Guaranteed cleanup
    }
}
```

### Non-Copyable

Encoders that own file handles must be non-copyable:

```cpp
MyEncoder(const MyEncoder&) = delete;
MyEncoder& operator=(const MyEncoder&) = delete;
MyEncoder(MyEncoder&&) = delete;
MyEncoder& operator=(MyEncoder&&) = delete;
```

---

## Frame Input Protection

### Null Buffer Check

```cpp
void MyEncoder::OnVideoFrame(const FramebufferDescriptor& fb, double ts)
{
    if (!_isRecording) return;
    if (fb.memoryBuffer == nullptr) return;  // Skip invalid frames

    // Process frame...
}
```

### Double-Stop Safety

```cpp
void MyEncoder::Stop()
{
    if (!_isRecording) return;  // Safe to call multiple times

    // Cleanup...
    _isRecording = false;
}
```

---

## Testing Requirements

Create `core/tests/emulator/recording/my_encoder_test.cpp` with:

| Category | Tests |
|----------|-------|
| **Lifecycle** | Start valid, Start invalid path, Double start, Stop without start, Double stop |
| **Validation** | Empty filename, Zero dimensions, Nonexistent directory |
| **Frame Input** | Frame count, Input without start, Null buffer |
| **RAII** | Destructor cleanup |
| **State** | GetType, GetDisplayName, SupportsVideo/Audio |

### Example Test Structure

```cpp
class MyEncoderTest : public ::testing::Test {
protected:
    void SetUp() override {
        _tempDir = std::filesystem::temp_directory_path() / "my_encoder_test";
        std::filesystem::create_directories(_tempDir);
    }
    void TearDown() override {
        std::filesystem::remove_all(_tempDir);
    }
    // ...
};
```

---

## Performance

### Benchmark Requirements

Create `core/benchmarks/emulator/recording/my_encoder_benchmark.cpp`:

- **Static content**: Measure best-case encoding speed
- **Typical content**: Simulate realistic ZX Spectrum usage
- **Worst case**: Full frame changes every frame

Run: `./bin/core-benchmarks --benchmark_filter=MyEncoder`

---

## Files Checklist

| File | Description |
|------|-------------|
| `encoders/my_encoder.h` | Header with class declaration |
| `encoders/my_encoder.cpp` | Implementation |
| `tests/emulator/recording/my_encoder_test.cpp` | Unit tests |
| `benchmarks/emulator/recording/my_encoder_benchmark.cpp` | Performance benchmarks |
| `docs/emulator/design/recording/encoders/my-encoder.md` | Documentation |

---

## See Also

- [Encoder Architecture](./encoder-architecture.md)
- [GIF Encoder](./encoders/gif-encoder.md) - Reference implementation
