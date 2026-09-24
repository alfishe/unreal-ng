#pragma once

#include <QSettings>
#include <QString>

struct RecordingSettings
{
    // Backend: 0 = Auto, 1 = Native, 2 = FFmpeg
    int backend = 0;

    // Container / Format (e.g. "MP4", "MKV", "WebM", "GIF", "WAV", "MP3", "FLAC", "OGG", "DSD")
    QString container = QStringLiteral("MP4");

    // Quality: 0 = Fastest, 1 = Fast, 2 = Medium, 3 = High, 4 = Best
    int quality = 2;

    // Capture region: 0 = Full frame (with border), 1 = Screen only (256x192)
    int captureRegion = 0;

    // Scale factor: 1, 2, 3, 4 (integer nearest-neighbor)
    int scaleFactor = 2;

    // Include audio in video recording
    bool includeAudio = true;

    // Video codec override (empty = default for container)
    QString videoCodec;

    // Audio codec override (empty = default for container)
    QString audioCodec;

    // Audio bitrate for lossy formats like MP3 (kbps, default 192)
    int audioBitrate = 192;

    void load()
    {
        QSettings settings(QStringLiteral("unreal-ng"), QStringLiteral("recording"));
        backend = settings.value(QStringLiteral("backend"), 0).toInt();
        container = settings.value(QStringLiteral("container"), QStringLiteral("MP4")).toString();
        quality = settings.value(QStringLiteral("quality"), 2).toInt();
        captureRegion = settings.value(QStringLiteral("captureRegion"), 0).toInt();
        scaleFactor = settings.value(QStringLiteral("scaleFactor"), 2).toInt();
        includeAudio = settings.value(QStringLiteral("includeAudio"), true).toBool();
        videoCodec = settings.value(QStringLiteral("videoCodec"), QString()).toString();
        audioCodec = settings.value(QStringLiteral("audioCodec"), QString()).toString();
        audioBitrate = settings.value(QStringLiteral("audioBitrate"), 192).toInt();
    }

    void save() const
    {
        QSettings settings(QStringLiteral("unreal-ng"), QStringLiteral("recording"));
        settings.setValue(QStringLiteral("backend"), backend);
        settings.setValue(QStringLiteral("container"), container);
        settings.setValue(QStringLiteral("quality"), quality);
        settings.setValue(QStringLiteral("captureRegion"), captureRegion);
        settings.setValue(QStringLiteral("scaleFactor"), scaleFactor);
        settings.setValue(QStringLiteral("includeAudio"), includeAudio);
        settings.setValue(QStringLiteral("videoCodec"), videoCodec);
        settings.setValue(QStringLiteral("audioCodec"), audioCodec);
        settings.setValue(QStringLiteral("audioBitrate"), audioBitrate);
    }
};
