#include "crtprofiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

CRTProfileParams CRTProfileParams::None()
{
    return CRTProfileParams{};
}

CRTProfileParams CRTProfileParams::Basic()
{
    CRTProfileParams p;
    p.profile = CRTProfile::Basic;
    p.curvature = 0.02f;
    p.scanlineWeight = 0.3f;
    p.scanlineAdaptive = false;  // Fixed scanlines on source resolution
    p.bloomStrength = 0.1f;
    p.bloomRadius = 1.5f;
    return p;
}

CRTProfileParams CRTProfileParams::Aperture()
{
    CRTProfileParams p;
    p.profile = CRTProfile::Aperture;
    p.curvature = 0.015f;
    p.cornerRadius = 0.02f;
    p.scanlineWeight = 0.25f;
    p.scanlineAdaptive = true;
    p.maskStrength = 0.5f;
    p.maskDotPitch = 0.0f;  // Auto-calculate from resolution
    p.bloomStrength = 0.15f;
    p.bloomRadius = 2.0f;
    p.saturation = 1.1f;
    return p;
}

CRTProfileParams CRTProfileParams::ShadowMask()
{
    CRTProfileParams p;
    p.profile = CRTProfile::ShadowMask;
    p.curvature = 0.025f;
    p.cornerRadius = 0.03f;
    p.scanlineWeight = 0.2f;
    p.scanlineAdaptive = true;
    p.maskStrength = 0.6f;
    p.maskDotPitch = 0.0f;
    p.bloomStrength = 0.12f;
    p.bloomRadius = 1.8f;
    p.saturation = 1.05f;
    p.gamma = 2.4f;
    return p;
}

CRTProfileParams CRTProfileParams::SlotMask()
{
    CRTProfileParams p;
    p.profile = CRTProfile::SlotMask;
    p.curvature = 0.03f;
    p.cornerRadius = 0.025f;
    p.scanlineWeight = 0.35f;
    p.scanlineAdaptive = true;
    p.maskStrength = 0.55f;
    p.maskDotPitch = 0.0f;
    p.bloomStrength = 0.18f;
    p.bloomRadius = 2.2f;
    p.saturation = 1.15f;
    p.brightness = 1.1f;
    return p;
}

CRTProfileParams CRTProfileParams::Megatron()
{
    // Sony Megatron-style: designed to scale with output resolution
    // Simulates a real CRT with proper phosphor triads
    CRTProfileParams p;
    p.profile = CRTProfile::Megatron;
    p.curvature = 0.02f;
    p.cornerRadius = 0.015f;
    p.overscan = 0.01f;

    // No source-based scanlines - uses resolution-adaptive phosphor mask instead
    p.scanlineWeight = 0.0f;
    p.scanlineAdaptive = true;

    // Phosphor mask simulates real CRT at target resolution
    p.maskStrength = 0.7f;
    p.maskDotPitch = 0.0f;  // Auto: calculates from output resolution

    // Phosphor bloom is key to Megatron look
    p.bloomStrength = 0.25f;
    p.bloomRadius = 3.0f;

    // Color adjustments for authentic CRT feel
    p.saturation = 1.2f;
    p.brightness = 1.05f;
    p.contrast = 1.1f;
    p.gamma = 2.4f;

    // Slight persistence for motion
    p.persistence = 0.1f;

    return p;
}

CRTProfileParams CRTProfileParams::FromProfile(CRTProfile profile)
{
    switch (profile)
    {
        case CRTProfile::Basic:      return Basic();
        case CRTProfile::Aperture:   return Aperture();
        case CRTProfile::ShadowMask: return ShadowMask();
        case CRTProfile::SlotMask:   return SlotMask();
        case CRTProfile::Megatron:   return Megatron();
        default:                     return None();
    }
}

QString CRTProfileParams::ProfileName(CRTProfile profile)
{
    switch (profile)
    {
        case CRTProfile::None:       return "None";
        case CRTProfile::Basic:      return "Basic";
        case CRTProfile::Aperture:   return "Aperture Grille";
        case CRTProfile::ShadowMask: return "Shadow Mask";
        case CRTProfile::SlotMask:   return "Slot Mask";
        case CRTProfile::Megatron:   return "Megatron";
        default:                     return "Unknown";
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
        case CRTProfile::Aperture:
            return "Aperture grille (vertical RGB stripes like Sony Trinitron)";
        case CRTProfile::ShadowMask:
            return "Traditional shadow mask CRT dot pattern";
        case CRTProfile::SlotMask:
            return "Slot mask pattern (arcade monitor style)";
        case CRTProfile::Megatron:
            return "Sony Megatron-style adaptive phosphor simulation";
        default:
            return "";
    }
}

std::vector<CRTProfile> CRTProfileParams::AllProfiles()
{
    return {
        CRTProfile::None,
        CRTProfile::Basic,
        CRTProfile::Aperture,
        CRTProfile::ShadowMask,
        CRTProfile::SlotMask,
        CRTProfile::Megatron
    };
}

// CRTShaderManager implementation

CRTShaderManager& CRTShaderManager::instance()
{
    static CRTShaderManager manager;
    return manager;
}

QString CRTShaderManager::shaderDirectory() const
{
    // Look for shaders in multiple locations:
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

    QDir dir(shaderDirectory());
    if (!dir.exists())
        return;

    QStringList filters;
    filters << "*.glsl" << "*.frag" << "*.fs";

    QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::Readable);
    for (const QFileInfo& fi : files)
    {
        _shaderFiles.push_back(fi.baseName());
    }
}

QString CRTShaderManager::loadShader(const QString& name) const
{
    QString dir = shaderDirectory();
    QStringList extensions = {"glsl", "frag", "fs"};

    for (const QString& ext : extensions)
    {
        QFile file(dir + "/" + name + "." + ext);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&file);
            return in.readAll();
        }
    }

    return QString();
}
