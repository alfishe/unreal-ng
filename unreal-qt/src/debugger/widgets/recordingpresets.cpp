#include "recordingpresets.h"

#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDebug>

// ============================================================================
// RecordingPreset serialization
// ============================================================================

QJsonObject RecordingPreset::toJson() const
{
    QJsonObject json;
    json["name"] = name;
    json["mode"] = static_cast<int>(mode);
    json["container"] = container;
    json["videoCodec"] = videoCodec;
    json["audioCodec"] = audioCodec;
    json["videoBitrate"] = static_cast<int>(videoBitrate);
    json["audioBitrate"] = static_cast<int>(audioBitrate);
    json["frameRate"] = static_cast<double>(frameRate);
    json["hardwareAccel"] = hardwareAccel;
    json["includeAudio"] = includeAudio;
    json["qualityPreset"] = qualityPreset;
    json["crf"] = crf;
    json["outputDir"] = outputDir;
    json["description"] = description;
    return json;
}

RecordingPreset RecordingPreset::fromJson(const QJsonObject& json)
{
    RecordingPreset p;
    p.name = json.value("name").toString(p.name);
    p.mode = static_cast<RecordingMode>(json.value("mode").toInt(static_cast<int>(RecordingMode::SingleTrack)));
    p.container = json.value("container").toString(p.container);
    p.videoCodec = json.value("videoCodec").toString(p.videoCodec);
    p.audioCodec = json.value("audioCodec").toString(p.audioCodec);
    p.videoBitrate = static_cast<uint32_t>(json.value("videoBitrate").toInt(p.videoBitrate));
    p.audioBitrate = static_cast<uint32_t>(json.value("audioBitrate").toInt(p.audioBitrate));
    p.frameRate = static_cast<float>(json.value("frameRate").toDouble(p.frameRate));
    p.hardwareAccel = json.value("hardwareAccel").toBool(p.hardwareAccel);
    p.includeAudio = json.value("includeAudio").toBool(p.includeAudio);
    p.qualityPreset = json.value("qualityPreset").toInt(p.qualityPreset);
    p.crf = json.value("crf").toInt(p.crf);
    p.outputDir = json.value("outputDir").toString(p.outputDir);
    p.description = json.value("description").toString(p.description);
    return p;
}

// ============================================================================
// RecordingPresetManager
// ============================================================================

RecordingPresetManager::RecordingPresetManager()
{
    createDefaultPresets();
    loadPresets();
}

RecordingPresetManager::~RecordingPresetManager()
{
    savePresets();
}

void RecordingPresetManager::createDefaultPresets()
{
    // Gameplay - standard MP4 with H.264 + AAC
    RecordingPreset gameplay;
    gameplay.name = "Gameplay (MP4)";
    gameplay.description = "Standard gameplay recording with H.264 video and AAC audio.";
    gameplay.mode = RecordingMode::SingleTrack;
    gameplay.container = "mp4";
    gameplay.videoCodec = "h264";
    gameplay.audioCodec = "aac";
    gameplay.videoBitrate = 2500;
    gameplay.audioBitrate = 192;
    gameplay.frameRate = 50.0f;
    gameplay.hardwareAccel = true;
    gameplay.includeAudio = true;
    gameplay.qualityPreset = 5;
    gameplay.crf = 23;
    _defaultPresets.append(gameplay);

    // Music - FLAC audio-only
    RecordingPreset music;
    music.name = "Music (FLAC)";
    music.description = "Lossless audio-only recording for music capture.";
    music.mode = RecordingMode::AudioOnly;
    music.container = "wav";
    music.videoCodec = "";
    music.audioCodec = "flac";
    music.videoBitrate = 0;
    music.audioBitrate = 0;
    music.frameRate = 50.0f;
    music.hardwareAccel = false;
    music.includeAudio = true;
    music.qualityPreset = 10;
    _defaultPresets.append(music);

    // Audio Only - WAV
    RecordingPreset audioOnly;
    audioOnly.name = "Audio Only (WAV)";
    audioOnly.description = "Uncompressed PCM audio-only recording.";
    audioOnly.mode = RecordingMode::AudioOnly;
    audioOnly.container = "wav";
    audioOnly.videoCodec = "";
    audioOnly.audioCodec = "pcm_s16le";
    audioOnly.videoBitrate = 0;
    audioOnly.audioBitrate = 0;
    audioOnly.frameRate = 50.0f;
    audioOnly.hardwareAccel = false;
    audioOnly.includeAudio = true;
    audioOnly.qualityPreset = 10;
    _defaultPresets.append(audioOnly);

    // GIF Animation
    RecordingPreset gif;
    gif.name = "GIF Animation";
    gif.description = "Animated GIF for web sharing. No audio.";
    gif.mode = RecordingMode::SingleTrack;  // Video-only; GIF simply has no audio track
    gif.container = "gif";
    gif.videoCodec = "gif";
    gif.audioCodec = "";
    gif.videoBitrate = 0;
    gif.audioBitrate = 0;
    gif.frameRate = 50.0f;
    gif.hardwareAccel = false;
    gif.includeAudio = false;
    gif.qualityPreset = 5;
    _defaultPresets.append(gif);

    // High Quality - MKV with H.265 + FLAC
    RecordingPreset highQuality;
    highQuality.name = "High Quality (MKV)";
    highQuality.description = "Premium quality H.265 video with lossless FLAC audio.";
    highQuality.mode = RecordingMode::SingleTrack;
    highQuality.container = "mkv";
    highQuality.videoCodec = "h265";
    highQuality.audioCodec = "flac";
    highQuality.videoBitrate = 0;  // CRF mode
    highQuality.audioBitrate = 0;
    highQuality.frameRate = 50.0f;
    highQuality.hardwareAccel = true;
    highQuality.includeAudio = true;
    highQuality.qualityPreset = 8;
    highQuality.crf = 18;  // High quality
    _defaultPresets.append(highQuality);

    // Multi-Track - MKV with multiple audio sources
    RecordingPreset multiTrack;
    multiTrack.name = "Multi-Track (MKV)";
    multiTrack.description = "H.264 video with multiple audio tracks (Master + AY channels).";
    multiTrack.mode = RecordingMode::MultiTrack;
    multiTrack.container = "mkv";
    multiTrack.videoCodec = "h264";
    multiTrack.audioCodec = "flac";
    multiTrack.videoBitrate = 2500;
    multiTrack.audioBitrate = 0;
    multiTrack.frameRate = 50.0f;
    multiTrack.hardwareAccel = true;
    multiTrack.includeAudio = true;
    multiTrack.qualityPreset = 6;
    multiTrack.crf = 20;
    _defaultPresets.append(multiTrack);
}

static QString getPresetFilePath()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty())
        configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (configDir.isEmpty())
        configDir = QDir::homePath() + "/.unreal-ng";

    QDir().mkpath(configDir);
    return configDir + "/recording_presets.json";
}

void RecordingPresetManager::loadPresets()
{
    QString path = getPresetFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError)
    {
        qWarning() << "RecordingPresetManager: Failed to parse presets file:" << parseError.errorString();
        return;
    }

    QJsonArray array = doc.array();
    _userPresets.clear();
    for (const auto& val : array)
    {
        if (val.isObject())
        {
            _userPresets.append(RecordingPreset::fromJson(val.toObject()));
        }
    }

    qDebug() << "RecordingPresetManager: Loaded" << _userPresets.size() << "user presets from" << path;
}

void RecordingPresetManager::savePresets()
{
    QString path = getPresetFilePath();

    QJsonArray array;
    for (const auto& preset : _userPresets)
    {
        array.append(preset.toJson());
    }

    QJsonDocument doc(array);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        qWarning() << "RecordingPresetManager: Failed to save presets to" << path;
        return;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    qDebug() << "RecordingPresetManager: Saved" << _userPresets.size() << "user presets to" << path;
}

void RecordingPresetManager::addUserPreset(const RecordingPreset& preset)
{
    // Check if a preset with this name already exists; if so, replace it
    for (int i = 0; i < _userPresets.size(); ++i)
    {
        if (_userPresets[i].name == preset.name)
        {
            _userPresets[i] = preset;
            return;
        }
    }
    _userPresets.append(preset);
}

void RecordingPresetManager::removeUserPreset(const QString& name)
{
    for (int i = 0; i < _userPresets.size(); ++i)
    {
        if (_userPresets[i].name == name)
        {
            _userPresets.removeAt(i);
            return;
        }
    }
}

const RecordingPreset* RecordingPresetManager::findPreset(const QString& name) const
{
    // Search defaults first
    for (const auto& preset : _defaultPresets)
    {
        if (preset.name == name)
            return &preset;
    }
    // Then user presets
    for (const auto& preset : _userPresets)
    {
        if (preset.name == name)
            return &preset;
    }
    return nullptr;
}
