#pragma once
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sdb/image.h"
#include "sdb/input.h"
#include "sdb/models.h"
#include "sdb/transport.h"

namespace sdb
{

struct WriteStats
{
    std::size_t reports = 0, bytes = 0;
    std::chrono::nanoseconds elapsed{0};
};

class Device
{
public:
    Device(const Model& m, std::unique_ptr<Transport> t, std::string serial);

    [[nodiscard]] const Model& GetModel() const
    {
        return _emu ? *_emu->virt : _m;
    }
    [[nodiscard]] const Model& PhysicalModel() const
    {
        return _m;
    }

    // Emulation: present a smaller model on this device (a Stream Deck + XL can stand in for every other model).
    // The virtual keys take a block from the top-left, the virtual window sits centred in the strip, only the first
    // virtual-dial-count dials stay active, Neo sensors become the keys under the first and last column. Images go
    // through the VIRTUAL model's real pipeline (size, rotation, JPEG/BMP) and are decoded back for display, so the
    // encoding of every model is exercised on hardware. Input is translated to virtual indices; the rest is dropped.
    bool Emulate(const Model& virt, std::string* error = nullptr);
    [[nodiscard]] bool Emulating() const
    {
        return _emu.has_value();
    }
    [[nodiscard]] const std::string& Serial() const
    {
        return _serial;
    }
    [[nodiscard]] std::optional<std::string> Firmware();
    [[nodiscard]] std::optional<std::string> SerialFromDevice();

    bool SetBrightness(int percent);
    bool Reset();
    bool SetTouchLed(int point, std::uint8_t r, std::uint8_t g, std::uint8_t b);

    // Key images. `Rgb` overload does the full device pipeline; `Raw` takes wire-ready JPEG/BMP bytes.
    WriteStats SetKeyImage(int key, const Rgb& img, int quality = 90);
    WriteStats SetKeyRaw(int key, std::span<const std::uint8_t> encoded);
    WriteStats ClearKey(int key);
    WriteStats ClearAllKeys();

    // LCD in LOGICAL landscape coordinates (Plus 800x100, Plus XL 1200x100, Neo 248x58).
    // Rotation into device space is applied here; lcdRotationOverride() lets hardware tests try other orientations.
    WriteStats SetLcdFull(const Rgb& logical, int quality = 90);
    WriteStats SetLcdRegion(int logicalX, int logicalY, const Rgb& logical, int quality = 90);
    WriteStats SetLcdTile(int dial, const Rgb& tile, int quality = 90);  // tile = dialTileW x lcdLogicalH
    void SetLcdRotationOverride(std::optional<Rotation> r)
    {
        _lcdRot = r;
    }
    void SetLcdMirrorOverride(std::optional<Mirror> m)
    {
        _lcdMir = m;
    }  // full-fill only; region mapping ignores it
    [[nodiscard]] Rotation LcdRotation() const
    {
        return _lcdRot.value_or(_m.lcd.rotation);
    }

    // Elgato docs extras (V2 models)
    WriteStats SetFullScreen(const Rgb& logical, int quality = 90);  // whole panel in one image (output 0x08)
    // Whole panel from parts: per-key images (any size, fitted to the key) + optional strip, placed by panelLayout().
    WriteStats SetPanel(const std::vector<std::pair<int, Rgb>>& keys, const Rgb* strip = nullptr, int quality = 90);
    WriteStats SetWindowImage(const Rgb& logical, int quality = 90);  // full strip with "Update Window Image" (0x0B)
    bool FillKeyColor(int key, std::uint8_t r, std::uint8_t g, std::uint8_t b);  // feature 03 06: no image needed
    bool FillScreenColor(std::uint8_t r, std::uint8_t g, std::uint8_t b);        // feature 03 05
    bool SetSleepAfter(std::int32_t seconds);                                    // 0 = never
    [[nodiscard]] std::optional<std::int32_t> SleepAfter();
    struct UnitInfo
    {
        int rows = 0, cols = 0, keyW = 0, keyH = 0, lcdW = 0, lcdH = 0, bpp = 0, scheme = 0;
        std::vector<std::uint8_t> raw;
    };
    [[nodiscard]] std::optional<UnitInfo> ReadUnitInfo();  // V2 "Get Unit Information" (0x08)

    // Partial window (0x0C) coordinates. Logical (default) = landscape coordinates as the Elgato docs say - verified on
    // the Plus XL. Device = rotated into the panel's axes (what the crate does): every tile lands at x = 0.
    enum class LcdCoords
    {
        Device,
        Logical
    };
    void SetLcdCoords(LcdCoords c)
    {
        _lcdCoords = c;
    }
    void SetUseWindowCmd(bool on)
    {
        _useWindowCmd = on;
    }  // setLcdFull via 0x0B instead of a full 0x0C region

    // Blocks up to timeoutMs for ONE input report; returns the edge events it produced (timestamps filled).
    std::vector<Event> PollEvents(int timeoutMs);
    // One input report as the device sent it (bytes, 0 timeout, -1 error): for listeners that log / forward raw
    // reports.
    int ReadReport(std::span<std::uint8_t> buf, int timeoutMs)
    {
        return _t->Read(buf, timeoutMs);
    }
    // Shows a line in Stream Deck Studio's protocol log (when it is the target or a mirror); no-op on USB alone.
    void Note(std::string_view text)
    {
        _t->Note(text);
    }
    void PublishPage(std::string_view json)
    {
        _t->PublishPage(json);
    }

    [[nodiscard]] Transport& GetTransport()
    {
        return *_t;
    }

    // Mirror everything this device is sent to a Stream Deck Studio too (it switches to this model); input comes
    // from both. addr = "host:port" (default 127.0.0.1:37050).
    bool AttachMirror(const std::string& addr, std::string* error = nullptr);

private:
    WriteStats Send(const std::vector<std::vector<std::uint8_t>>& reports);

    struct Emulation
    {
        const Model* virt = nullptr;
        int stripX = 0, stripY = 0;  // virtual window origin inside the physical strip (logical px)
        std::vector<int> keyMap;     // virtual button (keys + sensors) -> physical key
        std::vector<int> reverse;    // physical key -> virtual button, -1 = unused
    };
    std::optional<Emulation> _emu;
    WriteStats EmuKeyRaw(int vkey, std::span<const std::uint8_t> encoded);
    WriteStats EmuLcd(int vx, int vy, const Rgb& img, int q);
    static void Add(WriteStats& a, const WriteStats& b)
    {
        a.reports += b.reports;
        a.bytes += b.bytes;
        a.elapsed += b.elapsed;
    }

    const Model& _m;
    std::unique_ptr<Transport> _t;
    std::string _serial;
    InputParser _parser, _emuParser;  // one per source: each reports its own full button state
    std::optional<Rotation> _lcdRot;
    std::optional<Mirror> _lcdMir;
    LcdCoords _lcdCoords = LcdCoords::Logical;
    Rgb _stripShadow;           // what the window strip shows (logical): partial writes must span the full height [hw]
    bool _useWindowCmd = true;  // full strip via 0x0B (verified on the Plus XL)
};

struct Found
{
    const Model* model;  // what the caller works with (the virtual model when emulating)
    HidInfo info;
    const Model* physical = nullptr;      // set when `model` is emulated on this physical device
    std::optional<std::string> mirror{};  // also drive a Stream Deck Studio at this address ("" = default)
    bool mirrorOptional = false;          // the default mirror: no emulator running = the real deck alone, not an error
};
[[nodiscard]] std::vector<Found> ListDevices();
[[nodiscard]] std::unique_ptr<Device> OpenDevice(const Found& f, std::string* error = nullptr);
// serialSubstring empty => first device.
[[nodiscard]] std::unique_ptr<Device> OpenFirst(const std::string& serialSubstring, std::string* error = nullptr);
// A Stream Deck Studio instead of USB: "host:port", default 127.0.0.1:37050. The model is the emulator's.
[[nodiscard]] std::unique_ptr<Device> OpenEmulator(const std::string& addr, std::string* error = nullptr);
// Asks a Stream Deck Studio to save a PNG of its window (for tests and docs).
bool EmulatorSnapshot(const std::string& addr, const std::string& path, std::string* error = nullptr);
// Starts (path set) or stops (path empty) a video recording of the emulator's deck; the format follows the extension.
// `result` gets the emulator's summary (file, frames, seconds) on stop.
bool EmulatorRecord(const std::string& addr, const std::string& path, int fps = 0, std::string* result = nullptr,
                    std::string* error = nullptr);
// Switches a Stream Deck Studio to another model.
bool EmulatorSetModel(const std::string& addr, std::uint16_t pid, std::string* error = nullptr);

[[nodiscard]] std::int64_t NowUs();

}  // namespace sdb
