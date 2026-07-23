#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QList>
#include <QSettings>
#include <QDateTime>
#include <cstdint>

#include "emulator/recording/recordingmanager.h"

/// @brief A recording preset that captures all encoding parameters
///
/// Presets allow users to quickly switch between common recording
/// configurations (e.g., "Gameplay MP4", "GIF Animation", "High Quality MKV").
struct RecordingPreset
{
    QString name;                           ///< Display name
    RecordingMode mode = RecordingMode::SingleTrack;
    QString container = "mp4";              ///< Output container (mp4, mkv, avi, webm, gif)
    QString videoCodec = "h264";            ///< Video codec (h264, h265, vp9, gif)
    QString audioCodec = "aac";             ///< Audio codec (aac, mp3, flac, pcm)
    uint32_t videoBitrate = 0;              ///< Video bitrate in kbps (0 = default/CRF)
    uint32_t audioBitrate = 192;            ///< Audio bitrate in kbps
    float frameRate = 50.0f;                ///< Target frame rate
    bool hardwareAccel = true;              ///< Prefer hardware acceleration
    bool includeAudio = true;               ///< Record audio alongside video
    int qualityPreset = 5;                  ///< Quality preset (0-10)
    int crf = 23;                           ///< Constant Rate Factor (0-51, x264/x265)
    QString outputDir;                      ///< Default output directory
    QString description;                    ///< Human-readable description

    /// Serialize to JSON
    QJsonObject toJson() const;

    /// Deserialize from JSON
    static RecordingPreset fromJson(const QJsonObject& json);
};

/// @brief Manages recording presets (built-in defaults + user presets)
///
/// Presets are persisted via QSettings (or a JSON file). Built-in presets
/// are always available. User presets can be added/removed.
class RecordingPresetManager
{
public:
    RecordingPresetManager();
    ~RecordingPresetManager();

    /// Load presets from persistent storage
    void loadPresets();

    /// Save user presets to persistent storage
    void savePresets();

    /// Get all built-in (default) presets
    const QList<RecordingPreset>& getDefaultPresets() const { return _defaultPresets; }

    /// Get all user-defined presets
    const QList<RecordingPreset>& getUserPresets() const { return _userPresets; }

    /// Add a user preset
    void addUserPreset(const RecordingPreset& preset);

    /// Remove a user preset by name
    void removeUserPreset(const QString& name);

    /// Find a preset by name (searches defaults first, then user presets)
    /// Returns nullptr if not found
    const RecordingPreset* findPreset(const QString& name) const;

private:
    void createDefaultPresets();

    QList<RecordingPreset> _defaultPresets;
    QList<RecordingPreset> _userPresets;
};
