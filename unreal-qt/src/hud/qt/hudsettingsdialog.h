#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QSet>
#include <QString>
#include <functional>
#include <vector>

class EmulatorContext;

/// Notification category identifiers (stable string keys for persistence and filtering)
namespace HudNotificationCategory
{
    // Memory / banking
    inline constexpr const char* RamPageSwitch = "ram-page";
    inline constexpr const char* RomPageSwitch = "rom-page";
    inline constexpr const char* ScreenPageSwitch = "screen-page";

    // Audio devices
    inline constexpr const char* AudioBeeper = "audio-beeper";
    inline constexpr const char* AudioCovox = "audio-covox";
    inline constexpr const char* AudioAY = "audio-ay";
    inline constexpr const char* AudioTurboSound = "audio-turbosound";

    // Recording
    inline constexpr const char* RecordingVideo = "recording-video";
    inline constexpr const char* RecordingAudio = "recording-audio";

    // Emulator state
    inline constexpr const char* EmulatorState = "emulator-state";
    inline constexpr const char* SpeedChange = "speed-change";
    inline constexpr const char* SystemReset = "system-reset";

    // Disk / tape
    inline constexpr const char* FloppyDisk = "floppy-disk";
    inline constexpr const char* TapeEvents = "tape-events";

    // Debug
    inline constexpr const char* Breakpoints = "breakpoints";

    // File loading
    inline constexpr const char* FileLoading = "file-loading";
}

/// Description of a single HUD notification category
struct HudCategoryDescriptor
{
    QString id;           // Stable key (matches HudNotificationCategory::*)
    QString displayName;  // Human-readable name
    QString description;  // Tooltip / detailed description
    QString group;        // Grouping header (empty = ungrouped)
    bool defaultEnabled;  // Default state
};

/// HUD Settings dialog - configures which notification categories to display
class HudSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HudSettingsDialog(EmulatorContext* context, QWidget* parent = nullptr);
    ~HudSettingsDialog() override;

    /// Get all registered categories
    static const std::vector<HudCategoryDescriptor>& registeredCategories();

    /// Check if a category is enabled (class-level for use by HudModel)
    static bool isCategoryEnabled(const QString& categoryId);

    /// Get all enabled category IDs
    static QSet<QString> enabledCategories();

    /// Save current settings to persistent storage
    static void saveSettings();

    /// Load settings from persistent storage (called on startup)
    static void loadSettings();

    /// Reset to defaults
    static void resetToDefaults();

    /// Register callback for settings changes
    static void setChangedCallback(std::function<void()> callback);

signals:
    void settingsChanged();

private:
    void createUI();
    void applySettings();
    void onCategoryToggled(const QString& categoryId, bool enabled);
    void onResetToDefaults();

private:
    EmulatorContext* _context = nullptr;
    QVBoxLayout* _mainLayout = nullptr;
    std::vector<QCheckBox*> _categoryCheckboxes;
};
