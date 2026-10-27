#include "crtprofiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

CRTProfileParams CRTProfileParams::None()
{
    CRTProfileParams p;
    p.profile = CRTProfile::None;
    // Explicitly neutral values - no processing
    p.gamma = 1.0f;
    p.brightness = 1.0f;
    p.contrast = 1.0f;
    p.saturation = 1.0f;
    p.scanlineWeight = 0.0f;
    return p;
}

CRTProfileParams CRTProfileParams::Basic()
{
    CRTProfileParams p;
    p.profile = CRTProfile::Basic;
    p.curvature = 0.02f;
    p.scanlineWeight = 0.3f;  // On the source's lines
    p.bloomStrength = 0.1f;
    p.bloomRadius = 1.5f;
    p.gamma = 1.0f;  // Neutral gamma for CPU/GPU parity
    return p;
}

CRTProfileParams CRTProfileParams::FromProfile(CRTProfile profile)
{
    switch (profile)
    {
        case CRTProfile::Basic:      return Basic();
        case CRTProfile::None:       return None();
        default:
        {
            // A model (CRTBeamModel): its own tables, the grading neutral
            CRTProfileParams p = None();
            p.profile = profile;
            return p;
        }
    }
}

QString CRTProfileParams::ProfileName(CRTProfile profile)
{
    switch (profile)
    {
        case CRTProfile::None:       return "None";
        case CRTProfile::Basic:      return "Basic";
        case CRTProfile::Lottes:      return "CRT Lottes";
        case CRTProfile::Geom:        return "CRT Geom";
        case CRTProfile::Easymode:    return "CRT Easymode";
        case CRTProfile::GdvMini:     return "CRT GDV Mini";
        case CRTProfile::Consumer:    return "CRT Consumer";
        case CRTProfile::Pi:          return "CRT Pi";
        case CRTProfile::Zfast:       return "zfast CRT";
        default:                     return "Unknown";
    }
}

QString CRTProfileParams::ProfileKey(CRTProfile profile)
{
    switch (profile)
    {
        case CRTProfile::None:       return "none";
        case CRTProfile::Basic:      return "basic";
        case CRTProfile::Lottes:      return "crt-lottes";
        case CRTProfile::Geom:        return "crt-geom";
        case CRTProfile::Easymode:    return "crt-easymode";
        case CRTProfile::GdvMini:     return "crt-gdv-mini";
        case CRTProfile::Consumer:    return "crt-consumer";
        case CRTProfile::Pi:          return "crt-pi";
        case CRTProfile::Zfast:       return "zfast-crt";
        default:                     return "unknown";
    }
}

QString CRTProfileParams::ProfileDescription(CRTProfile profile)
{
    switch (profile)
    {
        case CRTProfile::None:
            return "No CRT effects";
        case CRTProfile::Basic:
            return "Simple scanlines with slight curvature";
        case CRTProfile::Lottes:
            return "crt-lottes model: gaussian beams, shadow mask, curvature, bloom";
        case CRTProfile::Geom:
            return "crt-geom model: lanczos, beams wider when bright, curved tube, dot mask";
        case CRTProfile::Easymode:
            return "crt-easymode model: sharp lanczos, scanlines by brightness, flat";
        case CRTProfile::GdvMini:
            return "crt-gdv-mini model: beams by brightness, aperture mask";
        case CRTProfile::Consumer:
            return "crt-consumer model: a soft consumer TV, glow, curvature";
        case CRTProfile::Pi:
            return "crt-pi model: light scanlines, RGB mask, curvature";
        case CRTProfile::Zfast:
            return "zfast-crt model: fast scanlines and a fine mask";
        default:
            return "";
    }
}

std::vector<CRTProfile> CRTProfileParams::AllProfiles()
{
    return {CRTProfile::None,    CRTProfile::Basic,    CRTProfile::Lottes, CRTProfile::Geom, CRTProfile::Easymode,
            CRTProfile::GdvMini, CRTProfile::Consumer, CRTProfile::Pi,     CRTProfile::Zfast};
}

// CRTShaderManager implementation

CRTShaderManager& CRTShaderManager::instance()
{
    static CRTShaderManager manager;
    return manager;
}

QString CRTShaderManager::shaderDirectory() const
{
    // A user's own shaders, first existing of:
    // 1. Application data directory: data/shaders/
    // 2. User config directory: ~/.config/unreal-ng/shaders/ (Linux)
    //                          ~/Library/Application Support/unreal-ng/shaders/ (macOS)
    //                          %APPDATA%/unreal-ng/shaders/ (Windows)

    QString appDir = QCoreApplication::applicationDirPath();

    // Check relative to app
    QDir dataDir(appDir + "/../data/shaders");
    if (dataDir.exists())
        return dataDir.absolutePath();

    dataDir.setPath(appDir + "/data/shaders");
    if (dataDir.exists())
        return dataDir.absolutePath();

    // Check user config location
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    dataDir.setPath(configPath + "/shaders");
    if (dataDir.exists())
        return dataDir.absolutePath();

    // Return default location (may not exist yet)
    return appDir + "/data/shaders";
}

void CRTShaderManager::scanShaderDirectory()
{
    _shaderFiles.clear();
    _shaderPaths.clear();

    // The shipped shaders (:/shaders resources), then the user's: a user file of the same name replaces a shipped one
    const QStringList filters = {"*.glsl", "*.frag", "*.fs"};
    for (const QString& path : {QStringLiteral(":/shaders"), shaderDirectory()})
    {
        const QDir dir(path);
        if (!dir.exists())
            continue;
        for (const QFileInfo& fi : dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name))
        {
            if (_shaderPaths.find(fi.baseName()) == _shaderPaths.end())
                _shaderFiles.push_back(fi.baseName());
            _shaderPaths[fi.baseName()] = fi.filePath();
        }
    }
}

QString CRTShaderManager::loadShader(const QString& name) const
{
    const auto it = _shaderPaths.find(name);
    if (it == _shaderPaths.end())
        return QString();
    QFile file(it->second);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QTextStream in(&file);
    return in.readAll();
}
