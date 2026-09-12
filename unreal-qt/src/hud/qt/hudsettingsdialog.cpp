#include "hudsettingsdialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStyle>

namespace
{
constexpr const char* kSettingsGroup = "HUD/Notifications";

// Global state: enabled categories and change callback
QSet<QString> g_enabledCategories;
std::function<void()> g_changedCallback;
bool g_initialized = false;

// Category registry
std::vector<HudCategoryDescriptor> g_categories = {
    // Memory / Banking
    {HudNotificationCategory::RamPageSwitch, QObject::tr("RAM Page Switches"),
     QObject::tr("Show when the active RAM bank changes (128K/+2/+3 modes)"),
     QObject::tr("Memory"), true},
    {HudNotificationCategory::RomPageSwitch, QObject::tr("ROM Page Switches"),
     QObject::tr("Show when the active ROM bank changes"),
     QObject::tr("Memory"), true},
    {HudNotificationCategory::ScreenPageSwitch, QObject::tr("Screen Page Switches"),
     QObject::tr("Show when the display switches between normal/shadow screen"),
     QObject::tr("Memory"), true},

    // Audio
    {HudNotificationCategory::AudioBeeper, QObject::tr("Beeper Activity"),
     QObject::tr("Show when the beeper produces sound"),
     QObject::tr("Audio"), true},
    {HudNotificationCategory::AudioCovox, QObject::tr("COVOX / Soundrive Activity"),
     QObject::tr("Show when COVOX or Soundrive DAC output is detected"),
     QObject::tr("Audio"), true},
    {HudNotificationCategory::AudioAY, QObject::tr("AY / TurboSound Activity"),
     QObject::tr("Show when AY or TurboSound becomes active (displays 'AY' or 'TS')"),
     QObject::tr("Audio"), false},  // Default off - AY is almost always active

    // Recording
    {HudNotificationCategory::RecordingVideo, QObject::tr("Video Recording"),
     QObject::tr("Show recording indicator and save notifications for video"),
     QObject::tr("Recording"), true},
    {HudNotificationCategory::RecordingAudio, QObject::tr("Audio Recording"),
     QObject::tr("Show recording indicator and save notifications for audio-only"),
     QObject::tr("Recording"), true},

    // Emulator State
    {HudNotificationCategory::EmulatorState, QObject::tr("Pause / Execute State"),
     QObject::tr("Show pause/execute/stop state changes"),
     QObject::tr("Emulator"), true},
    {HudNotificationCategory::SpeedChange, QObject::tr("Speed Changes"),
     QObject::tr("Show turbo mode and speed multiplier indicator"),
     QObject::tr("Emulator"), true},
    {HudNotificationCategory::SystemReset, QObject::tr("System Reset"),
     QObject::tr("Show notification when the system is reset"),
     QObject::tr("Emulator"), true},

    // Disk / Tape
    {HudNotificationCategory::FloppyDisk, QObject::tr("Floppy Disk Events"),
     QObject::tr("Show disk activity, insertion, ejection, and save events"),
     QObject::tr("Storage"), true},
    {HudNotificationCategory::TapeEvents, QObject::tr("Tape Events"),
     QObject::tr("Show tape loading and status notifications"),
     QObject::tr("Storage"), true},

    // Debug
    {HudNotificationCategory::Breakpoints, QObject::tr("Breakpoint Hits"),
     QObject::tr("Show notification when a breakpoint is triggered"),
     QObject::tr("Debug"), true},

    // File Loading
    {HudNotificationCategory::FileLoading, QObject::tr("File Loading"),
     QObject::tr("Show notifications when snapshots and files are loaded"),
     QObject::tr("Files"), true},
};

void ensureInitialized()
{
    if (!g_initialized)
    {
        HudSettingsDialog::loadSettings();
        g_initialized = true;
    }
}

} // namespace

HudSettingsDialog::HudSettingsDialog(EmulatorContext* context, QWidget* parent)
    : QDialog(parent), _context(context)
{
    ensureInitialized();

    setWindowTitle(tr("HUD Notifications"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumWidth(380);

    createUI();
}

HudSettingsDialog::~HudSettingsDialog() = default;

const std::vector<HudCategoryDescriptor>& HudSettingsDialog::registeredCategories()
{
    return g_categories;
}

bool HudSettingsDialog::isCategoryEnabled(const QString& categoryId)
{
    ensureInitialized();
    return g_enabledCategories.contains(categoryId);
}

QSet<QString> HudSettingsDialog::enabledCategories()
{
    ensureInitialized();
    return g_enabledCategories;
}

void HudSettingsDialog::saveSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    settings.beginGroup(kSettingsGroup);

    // Store disabled (default-on but turned off) and enabled (default-off but turned on)
    QStringList disabled;
    QStringList enabled;
    for (const auto& cat : g_categories)
    {
        bool isEnabled = g_enabledCategories.contains(cat.id);
        if (cat.defaultEnabled && !isEnabled)
        {
            disabled.append(cat.id);
        }
        else if (!cat.defaultEnabled && isEnabled)
        {
            enabled.append(cat.id);
        }
    }
    settings.setValue("disabled", disabled);
    settings.setValue("enabled", enabled);
    settings.endGroup();
    settings.sync();
}

void HudSettingsDialog::loadSettings()
{
    g_enabledCategories.clear();

    // Start with defaults
    for (const auto& cat : g_categories)
    {
        if (cat.defaultEnabled)
        {
            g_enabledCategories.insert(cat.id);
        }
    }

    // Load persisted settings
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    settings.beginGroup(kSettingsGroup);

    // Remove explicitly disabled (default-on turned off)
    if (settings.contains("disabled"))
    {
        QStringList disabled = settings.value("disabled").toStringList();
        for (const QString& id : disabled)
        {
            g_enabledCategories.remove(id);
        }
    }

    // Add explicitly enabled (default-off turned on)
    if (settings.contains("enabled"))
    {
        QStringList enabled = settings.value("enabled").toStringList();
        for (const QString& id : enabled)
        {
            g_enabledCategories.insert(id);
        }
    }

    settings.endGroup();
    g_initialized = true;
}

void HudSettingsDialog::resetToDefaults()
{
    g_enabledCategories.clear();
    for (const auto& cat : g_categories)
    {
        if (cat.defaultEnabled)
        {
            g_enabledCategories.insert(cat.id);
        }
    }
    saveSettings();

    if (g_changedCallback)
    {
        g_changedCallback();
    }
}

void HudSettingsDialog::setChangedCallback(std::function<void()> callback)
{
    g_changedCallback = std::move(callback);
}

void HudSettingsDialog::createUI()
{
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setSpacing(12);

    // Header label
    auto* headerLabel = new QLabel(tr("Select which HUD notifications to display:"), this);
    _mainLayout->addWidget(headerLabel);

    // Scrollable content area
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* scrollContent = new QWidget(scrollArea);
    auto* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setSpacing(4);
    scrollLayout->setContentsMargins(0, 0, 8, 0);

    // Group categories by their group field
    QString currentGroup;
    QGroupBox* currentGroupBox = nullptr;
    QVBoxLayout* currentGroupLayout = nullptr;

    for (const auto& cat : g_categories)
    {
        if (cat.group != currentGroup)
        {
            // Start new group
            currentGroup = cat.group;
            currentGroupBox = new QGroupBox(cat.group, scrollContent);
            currentGroupLayout = new QVBoxLayout(currentGroupBox);
            currentGroupLayout->setSpacing(2);
            currentGroupLayout->setContentsMargins(8, 8, 8, 8);
            scrollLayout->addWidget(currentGroupBox);
        }

        auto* checkbox = new QCheckBox(cat.displayName, currentGroupBox);
        checkbox->setToolTip(cat.description);
        checkbox->setChecked(g_enabledCategories.contains(cat.id));
        checkbox->setProperty("categoryId", cat.id);

        connect(checkbox, &QCheckBox::toggled, this, [this, id = cat.id](bool checked) {
            onCategoryToggled(id, checked);
        });

        _categoryCheckboxes.push_back(checkbox);
        currentGroupLayout->addWidget(checkbox);
    }

    scrollLayout->addStretch();
    scrollArea->setWidget(scrollContent);
    _mainLayout->addWidget(scrollArea, 1);

    // Button bar
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);

    auto* resetButton = new QPushButton(tr("Reset to Defaults"), this);
    resetButton->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
    connect(resetButton, &QPushButton::clicked, this, &HudSettingsDialog::onResetToDefaults);
    buttonLayout->addWidget(resetButton);

    buttonLayout->addStretch();

    auto* closeButton = new QPushButton(tr("Close"), this);
    closeButton->setDefault(true);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeButton);

    _mainLayout->addLayout(buttonLayout);

    // Set reasonable size to fit all categories without scrolling
    resize(400, 620);
}

void HudSettingsDialog::applySettings()
{
    saveSettings();
    emit settingsChanged();

    if (g_changedCallback)
    {
        g_changedCallback();
    }
}

void HudSettingsDialog::onCategoryToggled(const QString& categoryId, bool enabled)
{
    if (enabled)
    {
        g_enabledCategories.insert(categoryId);
    }
    else
    {
        g_enabledCategories.remove(categoryId);
    }

    applySettings();
}

void HudSettingsDialog::onResetToDefaults()
{
    resetToDefaults();

    // Update checkboxes to match
    for (auto* checkbox : _categoryCheckboxes)
    {
        QString id = checkbox->property("categoryId").toString();
        checkbox->blockSignals(true);
        checkbox->setChecked(g_enabledCategories.contains(id));
        checkbox->blockSignals(false);
    }
}
