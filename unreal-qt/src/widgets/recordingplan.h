#pragma once

/// @file recordingplan.h
/// @brief What a set of shared recording options means, in one place: the recording panel and the full Video Recording
/// dialog both start from this plan, so the same options give the same file whichever one is used (they once mapped
/// the quality, the audio codec and the profile each their own way).

#include <QString>
#include <QStringList>

#include <algorithm>

#include "recordingsettings.h"

namespace recordingplan
{
/// The audio codecs a container takes (the dialog's list), as shown: the first is the default. The native encoders
/// (the Mac's AVAssetWriter and the like) write AAC only
inline QStringList AudioCodecsFor(const QString& container, bool nativeOnly)
{
    const QString c = container.toUpper();
    if (c == "GIF" || c == "APNG" || c == "WEBP")
        return {};
    if (nativeOnly)
        return {"AAC"};
    if (c == "MP4")
        return {"AAC", "MP3"};
    if (c == "MOV")
        return {"AAC", "PCM"};
    if (c == "MKV")
        return {"AAC", "MP3", "FLAC", "PCM", "Opus", "Vorbis"};
    if (c == "WEBM")
        return {"Opus", "Vorbis"};
    if (c == "AVI")
        return {"MP3", "PCM"};
    return {"AAC"};
}

inline bool IsAudioOnly(const QString& container)
{
    const QString c = container.toUpper();
    return c == "WAV" || c == "MP3" || c == "FLAC" || c == "OGG" || c == "DSD";
}

/// The window as source (CRT shaders, temporal processing) is written by the window recorder: MP4 / MKV / MOV with
/// H.264 or H.265, and ProRes 4444 in a MOV
inline bool WindowSourceTakes(const QString& container, const QString& codec)
{
    const QString c = container.toUpper();
    const QString k = codec.toLower();
    if (k == "prores")
        return c == "MOV";  // the file is a .mov
    return (c == "MP4" || c == "MKV" || c == "MOV") && (k == "h264" || k == "h265");
}

/// A fixed frame profile (1080p / 1440p / 4K) is for the formats of the window recorder and the H.264 / H.265 raw ones
inline bool ProfileTakes(const QString& codec)
{
    const QString k = codec.toLower();
    return k == "h264" || k == "h265" || k == "prores";
}


/// What the encoders on this machine can write: filled by the caller (the dialog from its detection, the panel from
/// the same probes), used by both through the same catalog, so they offer the same formats
struct Environment
{
    int engine = 0;                ///< 0 = Auto, 1 = Native, 2 = FFmpeg
    bool nativeAvailable = false;  ///< a platform encoder (AVAssetWriter, Media Foundation...)
    QStringList nativeCodecs;      ///< what it writes: h264 / h265 / av1 (empty = h264 only)
    bool ffmpegAvailable = false;
    bool ffmpegHasWebP = false;
    bool mac = false;              ///< ProRes 4444 is the Mac's
};

/// The containers of the engine, in the dialog's order
inline QStringList Containers(const Environment& env)
{
    const QStringList native = {"MP4", "MOV", "GIF"};
    QStringList ffmpeg = {"MP4", "MKV", "MOV", "WebM", "AVI", "GIF", "APNG"};
    if (env.ffmpegHasWebP)
        ffmpeg << "WebP";
    if (env.engine == 1)
        return native;
    if (env.engine == 2)
        return ffmpeg;
    if (env.ffmpegAvailable)  // Auto: whatever the available engines can write
        return ffmpeg;
    if (env.nativeAvailable)
        return native;
    return {"GIF"};
}

/// The video codec ids of a container (h264, h265, av1, vp9, vp8, gif, apng, webp, prores), the first is the default
inline QStringList VideoCodecIds(const Environment& env, const QString& container)
{
    const QString c = container.toUpper();
    if (c == "GIF")
        return {"gif"};
    if (c == "APNG")
        return {"apng"};
    if (c == "WEBP")
        return {"webp"};
    QStringList codecs;
    if (env.engine == 1)
    {
        codecs = env.nativeCodecs.isEmpty() ? QStringList{"h264"} : env.nativeCodecs;
    }
    else if (c == "MP4")
    {
        codecs = {"h264", "h265"};
        if (env.ffmpegAvailable)
            codecs << "av1";
    }
    else if (c == "MOV")
        codecs = {"h264", "h265"};
    else if (c == "MKV")
        codecs = {"h264", "h265", "vp9", "av1"};
    else if (c == "WEBM")
        codecs = {"vp9", "vp8", "av1"};
    else if (c == "AVI")
        codecs = {"h264"};
    // True 4:4:4 on the Mac's ProRes engine, written by the window recorder through ffmpeg whatever the engine of the
    // raw recording is
    if (env.mac && env.ffmpegAvailable && c == "MOV")
        codecs << "prores";
    return codecs;
}

inline QString CodecLabel(const QString& id)
{
    if (id == "h264") return "H.264";
    if (id == "h265") return "H.265 (HEVC)";
    if (id == "av1") return "AV1";
    if (id == "vp9") return "VP9";
    if (id == "vp8") return "VP8";
    if (id == "gif") return "GIF";
    if (id == "apng") return "APNG";
    if (id == "webp") return "WebP";
    if (id == "prores") return "ProRes 4444";
    return id;
}

/// The codec id of a label of CodecLabel() ("H.265 (HEVC)" -> h265)
inline QString CodecIdOf(const QString& label)
{
    for (const char* id : {"h264", "h265", "av1", "vp9", "vp8", "gif", "apng", "webp", "prores"})
        if (CodecLabel(QString::fromLatin1(id)) == label)
            return QString::fromLatin1(id);
    return label.split(' ').value(0).toLower();
}

/// Which controls the options leave free, for both UIs
struct Controls
{
    bool source = true;    ///< the Raw / Window choice is open (the format takes it, and is not ProRes)
    bool windowSource = false;  ///< the picture is the window's (chosen, or ProRes)
    bool capture = true;   ///< the raw capture region
    bool size = true;      ///< the size factor
    bool profile = true;   ///< the fixed frame profile
};

/// @p windowRecorder: the window recorder is there to take a window source
inline Controls ControlsFor(const QString& container, const QString& codec, bool windowSourceChosen,
                            const QString& profile, bool windowRecorder)
{
    Controls controls;
    const bool takes = windowRecorder && WindowSourceTakes(container, codec);
    controls.source = takes && codec.toLower() != "prores";
    controls.windowSource = takes && (windowSourceChosen || codec.toLower() == "prores");
    controls.profile = ProfileTakes(codec);
    controls.capture = !controls.windowSource;
    controls.size = !controls.windowSource && (!controls.profile || profile == "native");
    return controls;
}

struct Plan
{
    bool audioOnly = false;
    QString container;       ///< upper case
    QString videoCodec;      ///< h264 / h265 / vp9 / ... / prores; "" for audio only
    QString audioCodec;      ///< ffmpeg's name (aac, pcm_s16le, ...); "" = no sound
    int qualityPreset = 5;   ///< the recording manager's 0..10
    bool mainScreenOnly = false;
    int scale = 2;
    QString profile = QStringLiteral("native");  ///< what the format takes: native for the others
    bool windowSource = false;
    int audioBitrate = 0;    ///< kbps, MP3 only
    QString problem;         ///< not empty: the options cannot be recorded, and why
};

/// @p nativeOnly: the engine is Native (one audio codec, the platform's formats)
inline Plan MakePlan(const RecordingSettings& settings, bool nativeOnly)
{
    Plan plan;
    plan.container = settings.container.toUpper();
    plan.audioOnly = IsAudioOnly(plan.container);
    // Fastest .. Best on the recording manager's 0..10; each step lands in its own bucket of the encoders' thresholds
    static const int kPresets[] = {1, 3, 5, 7, 9};
    plan.qualityPreset = kPresets[std::clamp(settings.quality, 0, 4)];
    plan.scale = std::clamp(settings.scaleFactor, 1, 8);
    plan.mainScreenOnly = settings.captureRegion == 1;
    if (plan.audioOnly)
    {
        plan.audioBitrate = plan.container == "MP3" ? settings.audioBitrate : 0;
        plan.audioCodec = plan.container == "WAV"    ? "pcm_s16le"
                          : plan.container == "MP3"  ? "mp3"
                          : plan.container == "FLAC" ? "flac"
                                                     : QString();  // OGG and DSD: the dialog's own encoders
        if (plan.audioCodec.isEmpty())
            plan.problem = QStringLiteral("%1 is recorded from the Audio Only tab of the full Video Recording dialog")
                               .arg(plan.container);
        return plan;
    }

    const QString kind = plan.container;
    plan.videoCodec = (kind == "GIF" || kind == "APNG" || kind == "WEBP") ? kind.toLower() : settings.videoCodec.toLower();
    if (plan.videoCodec.isEmpty())
        plan.videoCodec = QStringLiteral("h265");
    const bool windowTakes = WindowSourceTakes(plan.container, plan.videoCodec);
    plan.windowSource = windowTakes && (settings.windowSource || plan.videoCodec == "prores");
    if (plan.videoCodec == "prores" && !windowTakes)
        plan.problem = QStringLiteral("ProRes 4444 is written in a MOV by the window recorder only");
    plan.profile = ProfileTakes(plan.videoCodec) ? settings.profile : QStringLiteral("native");

    if (settings.includeAudio)
    {
        const QStringList takes = AudioCodecsFor(plan.container, nativeOnly);
        QString wanted = settings.audioCodec;
        if (wanted.isEmpty() || !takes.contains(wanted, Qt::CaseInsensitive))
            wanted = takes.value(0);
        wanted = wanted.toLower();
        plan.audioCodec = wanted == "pcm" ? QStringLiteral("pcm_s16le") : wanted;
    }
    return plan;
}
}  // namespace recordingplan
