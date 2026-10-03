#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/video/screen.h"

/// @file screenshotter.h
/// @brief One screenshot for every surface (WebAPI, MCP, CLI, Lua, Python, Qt).
///
/// A screenshot is a pure function of one frame snapshot: the pixels and the geometry of the same
/// presented frame (Screen::SnapshotPresented). The area (whole frame, or the working picture the
/// frame's own geometry names) is cut from it and encoded. Nothing here knows a machine: the
/// raster table, FT812 and TS-Conf windows are all behind PictureGeometry.
/// Design: docs/inprogress/2026-10-03-screenshotter/design.md

/// What part of the frame to return
enum class ScreenshotArea
{
    Full,    ///< The whole frame, border included (the default)
    Screen,  ///< The working picture: the frame geometry's screen window
};

/// Which frame the pixels come from
enum class ScreenshotSource
{
    Presented,  ///< The finished, tear-free frame the window shows, a couple of frames behind (the default)
    Live,       ///< The frame as drawn right now: no present delay, no post-processing. Taken by the emulation thread
};

enum class ScreenshotFormat
{
    Png,  ///< Lossless, full colour (the default)
    Gif,  ///< 256 colors: lossy for true-colour pictures, only on explicit request
};

/// Why a screenshot failed; surfaces map these to their own statuses (HTTP 404 / 400 / 409 / 500)
enum class ScreenshotError
{
    None,
    NotFound,       ///< no such emulator
    NoFrame,        ///< the emulator has no presented frame yet
    BadParameter,   ///< an unknown area or format
    BadGeometry,    ///< the screen window does not fit inside the frame
    EncodeFailed,   ///< the encoder produced nothing
    IoFailed,       ///< the file could not be written
};

struct ScreenshotOptions
{
    ScreenshotArea area = ScreenshotArea::Full;
    ScreenshotFormat format = ScreenshotFormat::Png;
    ScreenshotSource source = ScreenshotSource::Presented;
    uint32_t liveTimeoutMs = 1000;  ///< Live, emulation running: how long to wait for the frame end
    std::string saveTo;  ///< UTF-8 path: write the image here instead of returning the bytes
};

struct ScreenshotResult
{
    bool ok = false;
    ScreenshotError error = ScreenshotError::None;
    std::string errorMessage;  ///< what went wrong and what the values were

    ScreenshotFormat format = ScreenshotFormat::Png;
    std::vector<uint8_t> bytes;  ///< the encoded image (empty when saved to a file)
    std::string savedFile;       ///< where it was written, when saveTo was given
    size_t encodedSize = 0;      ///< bytes of the encoded image, saved or not

    uint16_t width = 0;   ///< size of the returned image
    uint16_t height = 0;
    PictureRect crop;       ///< the returned image's rectangle inside the frame
    PictureGeometry frame;  ///< the frame the image was taken from
};

class Screenshotter
{
public:
    /// Cut the area from the snapshot and encode it. No emulator needed
    static ScreenshotResult Render(const FrameSnapshot& snapshot, const ScreenshotOptions& options);

    /// The frame of a screen. `emulationParked`: no emulation thread is drawing (Emulator::IsEmulationParked), which
    /// lets the live source read the buffer directly; otherwise the live source asks the emulation thread
    static ScreenshotResult TakeFrom(Screen& screen, const ScreenshotOptions& options, bool emulationParked = false);

    /// The frame of the emulator with this id
    static ScreenshotResult Take(const std::string& emulatorId, const ScreenshotOptions& options);

    /// Strict parsing for the surfaces: an unknown word is an error, never a silent default
    static bool ParseArea(const std::string& text, ScreenshotArea& area);
    static bool ParseFormat(const std::string& text, ScreenshotFormat& format);
    static bool ParseSource(const std::string& text, ScreenshotSource& source);

    /// The words of one request (a WebAPI query, which MCP calls): `area`, its deprecated alias `mode`, `format`
    /// and `source`. An empty word keeps the default (the whole frame, PNG, presented). A word that is not
    /// allowed, or an area and a mode that disagree, is false with `message` saying what and which words are allowed
    static bool ParseRequestWords(const std::string& area, const std::string& mode, const std::string& format,
                                  const std::string& source, ScreenshotOptions& options, std::string& message);

    static const char* AreaName(ScreenshotArea area);
    static const char* FormatName(ScreenshotFormat format);
    static const char* RequestSourceName(ScreenshotSource source);
    static const char* ErrorName(ScreenshotError error);
    /// native / external / composed: how the surfaces name a frame's source
    static const char* SourceName(FrameSource source);

    static std::string Base64Encode(const std::vector<uint8_t>& data);
};
