#include "hudtheme.h"

#include <algorithm>

const HudTileStyle& HudTheme::resolveStyle(const std::string& styleId) const
{
    if (!styleId.empty())
    {
        auto it = customStyles.find(styleId);
        if (it != customStyles.end())
        {
            return it->second;
        }
        if (styleId == "toast") return toast;
        if (styleId == "indicator") return indicator;
        if (styleId == "alert" || styleId == "danger") return alert;
    }
    return defaultTile;
}

HudTheme HudTheme::CreateDarkGlass()
{
    HudTheme theme;
    theme.id = HudThemeId::DarkGlass;
    theme.name = "Dark Glass";
    theme.description = "Modern translucent keynote dark glass";

    // Default tile
    theme.defaultTile.frame.backgroundColor = 0xDD10141C;
    theme.defaultTile.frame.backgroundGradientEnd = 0xEE1A202C;
    theme.defaultTile.frame.borderColor = 0x33FFFFFF;
    theme.defaultTile.frame.borderWidth = 1.0f;
    theme.defaultTile.frame.borderRadius = 14.0f;
    theme.defaultTile.frame.shadowColor = 0x88000000;
    theme.defaultTile.frame.shadowBlur = 16.0f;
    theme.defaultTile.frame.glassEffect = true;
    theme.defaultTile.frame.padding = HudMargins{14, 12, 14, 12};

    theme.defaultTile.content.fontFamily = "Inter";
    theme.defaultTile.content.titleFontSize = 11.0f;
    theme.defaultTile.content.bodyFontSize = 9.5f;
    theme.defaultTile.content.titleColor = 0xFFFFFFFF;
    theme.defaultTile.content.bodyColor = 0xFF94A3B8;
    theme.defaultTile.content.accentColor = 0xFF38BDF8;
    theme.defaultTile.content.iconColor = 0xFFE2E8F0;
    theme.defaultTile.content.badgeBgColor = 0xB0475569;
    theme.defaultTile.content.badgeTextColor = 0xFFFFFFFF;

    // Toast
    theme.toast = theme.defaultTile;
    theme.toast.frame.backgroundColor = 0xE6101218;
    theme.toast.frame.backgroundGradientEnd = 0xF0181C26;
    theme.toast.frame.borderColor = 0x2EFFFFFF;
    theme.toast.frame.borderRadius = 16.0f;
    theme.toast.content.titleColor = 0xFFFFFFFF;
    theme.toast.content.bodyColor = 0xFFCBD5E1;

    // Indicator rounded box
    theme.indicator = theme.defaultTile;
    theme.indicator.frame.backgroundColor = 0xD912161E;
    theme.indicator.frame.backgroundGradientEnd = 0xE61A202B;
    theme.indicator.frame.borderColor = 0x2BFFFFFF;
    theme.indicator.frame.borderRadius = 8.0f;
    theme.indicator.frame.padding = HudMargins{10, 6, 12, 6};
    theme.indicator.content.titleColor = 0xFFF1F5F9;
    theme.indicator.content.accentColor = 0xFF38BDF8;

    // Alert
    theme.alert = theme.defaultTile;
    theme.alert.frame.backgroundColor = 0xF03D1418;
    theme.alert.frame.backgroundGradientEnd = 0xF5240B0E;
    theme.alert.frame.borderColor = 0xCCFF4D4D;
    theme.alert.frame.borderRadius = 16.0f;
    theme.alert.frame.shadowColor = 0x77FF4D4D;
    theme.alert.content.titleColor = 0xFFFF6B6B;
    theme.alert.content.accentColor = 0xFFFF4D4D;
    theme.alert.content.iconColor = 0xFFFF5F5A;

    return theme;
}

HudTheme HudTheme::CreateRetroZX()
{
    HudTheme theme;
    theme.id = HudThemeId::RetroZX;
    theme.name = "Retro ZX Spectrum";
    theme.description = "Sinclair ZX Spectrum authentic 8-color pixel framing";

    // Default tile: Sinclair black background, bright cyan pixel border, 0 radius
    theme.defaultTile.frame.backgroundColor = 0xF2000000;
    theme.defaultTile.frame.backgroundGradientEnd = 0x00000000;
    theme.defaultTile.frame.borderColor = 0xFF00D7D7; // Bright cyan
    theme.defaultTile.frame.borderWidth = 2.0f;
    theme.defaultTile.frame.borderRadius = 0.0f;     // Crisp pixel block
    theme.defaultTile.frame.shadowColor = 0xFF000000;
    theme.defaultTile.frame.shadowBlur = 0.0f;
    theme.defaultTile.frame.glassEffect = false;
    theme.defaultTile.frame.padding = HudMargins{12, 10, 12, 10};

    theme.defaultTile.content.fontFamily = "Consolas";
    theme.defaultTile.content.titleFontSize = 11.0f;
    theme.defaultTile.content.bodyFontSize = 9.5f;
    theme.defaultTile.content.titleColor = 0xFFFFD700; // Bright yellow
    theme.defaultTile.content.bodyColor = 0xFFFFFFFF;  // Bright white
    theme.defaultTile.content.accentColor = 0xFF00D7D7; // Bright cyan
    theme.defaultTile.content.iconColor = 0xFF00D700;  // Bright green
    theme.defaultTile.content.badgeBgColor = 0xFFD700D7; // Bright magenta
    theme.defaultTile.content.badgeTextColor = 0xFFFFFFFF;

    // Toast
    theme.toast = theme.defaultTile;
    theme.toast.frame.borderColor = 0xFF00D7D7;
    theme.toast.content.titleColor = 0xFFFFD700;
    theme.toast.content.bodyColor = 0xFFFFFFFF;

    // Indicator
    theme.indicator = theme.defaultTile;
    theme.indicator.frame.borderColor = 0xFF00D700; // Bright green border
    theme.indicator.content.titleColor = 0xFFFFFFFF;
    theme.indicator.content.accentColor = 0xFF00D700;

    // Alert
    theme.alert = theme.defaultTile;
    theme.alert.frame.borderColor = 0xFFD70000; // Bright red
    theme.alert.content.titleColor = 0xFFFF3333;
    theme.alert.content.accentColor = 0xFFFF3333;
    theme.alert.content.iconColor = 0xFFFF3333;

    return theme;
}

HudTheme HudTheme::CreateCyberpunk()
{
    HudTheme theme;
    theme.id = HudThemeId::Cyberpunk;
    theme.name = "Cyberpunk";
    theme.description = "High-contrast neon cyan & hot magenta on deep indigo";

    theme.defaultTile.frame.backgroundColor = 0xF00D091A;
    theme.defaultTile.frame.backgroundGradientEnd = 0xF8190F2E;
    theme.defaultTile.frame.borderColor = 0xCCFF007F; // Hot magenta
    theme.defaultTile.frame.borderWidth = 1.5f;
    theme.defaultTile.frame.borderRadius = 4.0f;     // Angular cyber corners
    theme.defaultTile.frame.shadowColor = 0x88FF007F; // Pink neon glow
    theme.defaultTile.frame.shadowBlur = 18.0f;
    theme.defaultTile.frame.glassEffect = true;
    theme.defaultTile.frame.padding = HudMargins{14, 12, 14, 12};

    theme.defaultTile.content.fontFamily = "Inter";
    theme.defaultTile.content.titleFontSize = 11.0f;
    theme.defaultTile.content.bodyFontSize = 9.5f;
    theme.defaultTile.content.titleColor = 0xFFFCEE0A; // Cyber yellow
    theme.defaultTile.content.bodyColor = 0xFF00F0FF;  // Neon cyan
    theme.defaultTile.content.accentColor = 0xFFFF007F; // Hot magenta
    theme.defaultTile.content.iconColor = 0xFF00F0FF;
    theme.defaultTile.content.badgeBgColor = 0xCCFF007F;
    theme.defaultTile.content.badgeTextColor = 0xFFFFFFFF;

    theme.toast = theme.defaultTile;
    theme.toast.frame.borderColor = 0xCC00F0FF;       // Cyan border for toasts
    theme.toast.frame.shadowColor = 0x8800F0FF;

    theme.indicator = theme.defaultTile;
    theme.indicator.frame.borderRadius = 4.0f;
    theme.indicator.frame.borderColor = 0xAA00F0FF;
    theme.indicator.content.titleColor = 0xFFFCEE0A;

    theme.alert = theme.defaultTile;
    theme.alert.frame.backgroundColor = 0xF5330018;
    theme.alert.frame.borderColor = 0xFFFF0055;
    theme.alert.frame.shadowColor = 0xAAFF0055;
    theme.alert.content.titleColor = 0xFFFF0055;
    theme.alert.content.accentColor = 0xFFFF0055;
    theme.alert.content.iconColor = 0xFFFF0055;

    return theme;
}

HudTheme HudTheme::CreateAmberCRT()
{
    HudTheme theme;
    theme.id = HudThemeId::AmberCRT;
    theme.name = "Amber CRT";
    theme.description = "Phosphor amber monochrome CRT";

    theme.defaultTile.frame.backgroundColor = 0xF21C1204;
    theme.defaultTile.frame.backgroundGradientEnd = 0xF8291A06;
    theme.defaultTile.frame.borderColor = 0xAAFFB000; // Amber #FFB000
    theme.defaultTile.frame.borderWidth = 1.2f;
    theme.defaultTile.frame.borderRadius = 8.0f;
    theme.defaultTile.frame.shadowColor = 0x66FFB000; // Amber phosphor glow
    theme.defaultTile.frame.shadowBlur = 14.0f;
    theme.defaultTile.frame.glassEffect = false;
    theme.defaultTile.frame.padding = HudMargins{14, 12, 14, 12};

    theme.defaultTile.content.fontFamily = "Consolas";
    theme.defaultTile.content.titleFontSize = 11.0f;
    theme.defaultTile.content.bodyFontSize = 9.5f;
    theme.defaultTile.content.titleColor = 0xFFFFB000;
    theme.defaultTile.content.bodyColor = 0xCCFFA000;
    theme.defaultTile.content.accentColor = 0xFFFFCC33;
    theme.defaultTile.content.iconColor = 0xFFFFB000;
    theme.defaultTile.content.badgeBgColor = 0xAA5C3A00;
    theme.defaultTile.content.badgeTextColor = 0xFFFFD566;

    theme.toast = theme.defaultTile;
    theme.indicator = theme.defaultTile;
    theme.alert = theme.defaultTile;
    theme.alert.frame.borderColor = 0xFFFF6600;
    theme.alert.content.titleColor = 0xFFFF6600;
    theme.alert.content.accentColor = 0xFFFF6600;

    return theme;
}

HudTheme HudTheme::CreateEmeraldCRT()
{
    HudTheme theme;
    theme.id = HudThemeId::EmeraldCRT;
    theme.name = "Emerald CRT";
    theme.description = "P1 phosphor green monochrome terminal";

    theme.defaultTile.frame.backgroundColor = 0xF2041808;
    theme.defaultTile.frame.backgroundGradientEnd = 0xF808240D;
    theme.defaultTile.frame.borderColor = 0xAA33FF33; // Phosphor green #33FF33
    theme.defaultTile.frame.borderWidth = 1.2f;
    theme.defaultTile.frame.borderRadius = 8.0f;
    theme.defaultTile.frame.shadowColor = 0x6633FF33; // Green phosphor glow
    theme.defaultTile.frame.shadowBlur = 14.0f;
    theme.defaultTile.frame.glassEffect = false;
    theme.defaultTile.frame.padding = HudMargins{14, 12, 14, 12};

    theme.defaultTile.content.fontFamily = "Consolas";
    theme.defaultTile.content.titleFontSize = 11.0f;
    theme.defaultTile.content.bodyFontSize = 9.5f;
    theme.defaultTile.content.titleColor = 0xFF33FF33;
    theme.defaultTile.content.bodyColor = 0xCC29CC29;
    theme.defaultTile.content.accentColor = 0xFF66FF66;
    theme.defaultTile.content.iconColor = 0xFF33FF33;
    theme.defaultTile.content.badgeBgColor = 0xAA0E4D1A;
    theme.defaultTile.content.badgeTextColor = 0xFF85FF85;

    theme.toast = theme.defaultTile;
    theme.indicator = theme.defaultTile;
    theme.alert = theme.defaultTile;
    theme.alert.frame.borderColor = 0xFF88FF00;
    theme.alert.content.titleColor = 0xFF88FF00;
    theme.alert.content.accentColor = 0xFF88FF00;

    return theme;
}

HudTheme HudTheme::CreateLightModern()
{
    HudTheme theme;
    theme.id = HudThemeId::LightModern;
    theme.name = "Light Modern";
    theme.description = "Clean frosted light modern theme";

    theme.defaultTile.frame.backgroundColor = 0xEAFFFFFF;
    theme.defaultTile.frame.backgroundGradientEnd = 0xF5F1F5F9;
    theme.defaultTile.frame.borderColor = 0x33000000;
    theme.defaultTile.frame.borderWidth = 1.0f;
    theme.defaultTile.frame.borderRadius = 14.0f;
    theme.defaultTile.frame.shadowColor = 0x22000000;
    theme.defaultTile.frame.shadowBlur = 12.0f;
    theme.defaultTile.frame.glassEffect = true;
    theme.defaultTile.frame.padding = HudMargins{14, 12, 14, 12};

    theme.defaultTile.content.fontFamily = "Inter";
    theme.defaultTile.content.titleFontSize = 11.0f;
    theme.defaultTile.content.bodyFontSize = 9.5f;
    theme.defaultTile.content.titleColor = 0xFF0F172A;
    theme.defaultTile.content.bodyColor = 0xFF475569;
    theme.defaultTile.content.accentColor = 0xFF2563EB;
    theme.defaultTile.content.iconColor = 0xFF334155;
    theme.defaultTile.content.badgeBgColor = 0x260F172A;
    theme.defaultTile.content.badgeTextColor = 0xFF0F172A;

    theme.toast = theme.defaultTile;
    theme.indicator = theme.defaultTile;
    theme.indicator.frame.borderRadius = 8.0f;

    theme.alert = theme.defaultTile;
    theme.alert.frame.backgroundColor = 0xEEFEF2F2;
    theme.alert.frame.borderColor = 0xCCEF4444;
    theme.alert.content.titleColor = 0xFFB91C1C;
    theme.alert.content.accentColor = 0xFFDC2626;
    theme.alert.content.iconColor = 0xFFDC2626;

    return theme;
}

const HudTheme& HudTheme::FromId(HudThemeId id)
{
    static const HudTheme darkGlass = CreateDarkGlass();
    static const HudTheme retroZX = CreateRetroZX();
    static const HudTheme cyberpunk = CreateCyberpunk();
    static const HudTheme amberCRT = CreateAmberCRT();
    static const HudTheme emeraldCRT = CreateEmeraldCRT();
    static const HudTheme lightModern = CreateLightModern();

    switch (id)
    {
        case HudThemeId::DarkGlass: return darkGlass;
        case HudThemeId::RetroZX: return retroZX;
        case HudThemeId::Cyberpunk: return cyberpunk;
        case HudThemeId::AmberCRT: return amberCRT;
        case HudThemeId::EmeraldCRT: return emeraldCRT;
        case HudThemeId::LightModern: return lightModern;
    }
    return darkGlass;
}

const HudTheme& HudTheme::FromName(const std::string& name)
{
    std::string norm;
    norm.reserve(name.size());
    for (char c : name)
    {
        if (c != ' ' && c != '_' && c != '-')
        {
            norm.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }

    if (norm == "retrozx" || norm == "retro" || norm == "zx" || norm == "retrozxspectrum")
        return FromId(HudThemeId::RetroZX);
    if (norm == "cyberpunk" || norm == "cyber" || norm == "neon")
        return FromId(HudThemeId::Cyberpunk);
    if (norm == "ambercrt" || norm == "amber")
        return FromId(HudThemeId::AmberCRT);
    if (norm == "emeraldcrt" || norm == "emerald" || norm == "green")
        return FromId(HudThemeId::EmeraldCRT);
    if (norm == "lightmodern" || norm == "light")
        return FromId(HudThemeId::LightModern);
    return FromId(HudThemeId::DarkGlass);
}

std::vector<HudThemeId> HudTheme::AvailableThemes()
{
    return {
        HudThemeId::DarkGlass,
        HudThemeId::RetroZX,
        HudThemeId::Cyberpunk,
        HudThemeId::AmberCRT,
        HudThemeId::EmeraldCRT,
        HudThemeId::LightModern
    };
}

std::string HudTheme::ThemeName(HudThemeId id)
{
    switch (id)
    {
        case HudThemeId::DarkGlass: return "Dark Glass";
        case HudThemeId::RetroZX: return "Retro ZX Spectrum";
        case HudThemeId::Cyberpunk: return "Cyberpunk";
        case HudThemeId::AmberCRT: return "Amber CRT";
        case HudThemeId::EmeraldCRT: return "Emerald CRT";
        case HudThemeId::LightModern: return "Light Modern";
    }
    return "Dark Glass";
}
