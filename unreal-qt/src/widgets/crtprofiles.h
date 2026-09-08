#pragma once

#include <QString>
#include <vector>

/// @brief CRT effect profile identifiers
enum class CRTProfile
{
    None,           // No effects
    Basic,          // Simple scanlines + slight curvature
    Aperture,       // Aperture grille (vertical RGB stripes, like Sony Trinitron)
    ShadowMask,     // Shadow mask (traditional CRT dot pattern)
    SlotMask,       // Slot mask (like arcade monitors)
    Megatron        // Sony Megatron-style adaptive phosphor simulation
};

/// @brief CRT profile parameters
struct CRTProfileParams
{
    CRTProfile profile = CRTProfile::None;

    // Geometry
    float curvature = 0.0f;         // Screen curvature (0 = flat, 0.1 = subtle curve)
    float cornerRadius = 0.0f;       // Rounded corners
    float overscan = 0.0f;           // Border overscan simulation

    // Scanlines
    float scanlineWeight = 0.0f;     // Scanline darkness (0 = none, 1 = full black)
    float scanlineGap = 1.0f;        // Gap between scanlines (in source pixels)
    bool scanlineAdaptive = true;    // Scale scanlines with resolution

    // Phosphor mask
    float maskStrength = 0.0f;       // Phosphor mask visibility (0 = none, 1 = full)
    float maskDotPitch = 0.0f;       // Dot pitch in output pixels (0 = auto from resolution)

    // Bloom/glow
    float bloomStrength = 0.0f;      // Phosphor bloom/glow
    float bloomRadius = 0.0f;        // Bloom spread

    // Color
    float saturation = 1.0f;         // Color saturation boost
    float brightness = 1.0f;         // Brightness adjustment
    float contrast = 1.0f;           // Contrast adjustment
    float gamma = 2.2f;              // Display gamma

    // Phosphor persistence (for motion blur effect)
    float persistence = 0.0f;        // 0 = none, 1 = full frame persistence

    static CRTProfileParams None();
    static CRTProfileParams Basic();
    static CRTProfileParams Aperture();
    static CRTProfileParams ShadowMask();
    static CRTProfileParams SlotMask();
    static CRTProfileParams Megatron();

    static CRTProfileParams FromProfile(CRTProfile profile);
    static QString ProfileName(CRTProfile profile);
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

    /// @brief Scan for available shader files in data/shaders directory
    void scanShaderDirectory();

    /// @brief Get list of available external shader profiles
    std::vector<QString> availableShaders() const { return _shaderFiles; }

    /// @brief Load shader source from file
    /// @param name Shader name (without .glsl extension)
    /// @return Shader source code, or empty string if not found
    QString loadShader(const QString& name) const;

    /// @brief Get shader directory path
    QString shaderDirectory() const;

private:
    CRTShaderManager() = default;
    std::vector<QString> _shaderFiles;
};
