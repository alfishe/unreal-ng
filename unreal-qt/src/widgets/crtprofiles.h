#pragma once

#include <QString>
#include <map>
#include <vector>

/// @brief CRT effect profile identifiers
enum class CRTProfile
{
    None,           // No effects
    Basic           // Simple scanlines + slight curvature
};

/// @brief CRT profile parameters
struct CRTProfileParams
{
    CRTProfile profile = CRTProfile::None;

    // Geometry
    float curvature = 0.0f;         // Screen curvature (0 = flat, 0.1 = subtle curve)
    float cornerRadius = 0.0f;       // Rounded corners

    // Scanlines
    float scanlineWeight = 0.0f;     // Scanline darkness (0 = none, 1 = full black)

    // Bloom/glow
    float bloomStrength = 0.0f;      // Phosphor bloom/glow
    float bloomRadius = 0.0f;        // Bloom spread

    // Color
    float saturation = 1.0f;         // Color saturation boost
    float brightness = 1.0f;         // Brightness adjustment
    float contrast = 1.0f;           // Contrast adjustment
    float gamma = 2.2f;              // Display gamma

    static CRTProfileParams None();
    static CRTProfileParams Basic();

    static CRTProfileParams FromProfile(CRTProfile profile);
    static QString ProfileName(CRTProfile profile);
    /// The automation word: "none", "basic"
    static QString ProfileKey(CRTProfile profile);
    static QString ProfileDescription(CRTProfile profile);
    static std::vector<CRTProfile> AllProfiles();

    // External shader support
    QString customShaderPath;  // Path to custom .glsl fragment shader
    bool hasCustomShader() const { return !customShaderPath.isEmpty(); }
};

/// @brief Manages CRT shader profiles and external shader files
class CRTShaderManager
{
public:
    /// @brief Get singleton instance
    static CRTShaderManager& instance();

    /// @brief Scan for the shaders shipped as resources (:/shaders) and the user's (shaderDirectory())
    void scanShaderDirectory();

    /// @brief Get list of available external shader profiles
    std::vector<QString> availableShaders() const { return _shaderFiles; }

    /// @brief Load shader source from file (a name the last scan found)
    /// @param name Shader name (without .glsl extension)
    /// @return Shader source code, or empty string if not found
    QString loadShader(const QString& name) const;

    /// @brief The directory of the user's own shaders
    QString shaderDirectory() const;

private:
    CRTShaderManager() = default;
    std::vector<QString> _shaderFiles;
    std::map<QString, QString> _shaderPaths;  ///< name -> file
};
