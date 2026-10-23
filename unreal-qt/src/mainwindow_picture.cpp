/// @file mainwindow_picture.cpp
/// @brief The main window's picture for the automation (WindowDisplay, Stream Deck use case C9): the display
/// settings read and changed through the View menu's handlers, the active CRT profile's parameters, the published copy
/// the core's readouts and toggles read, the View/* settings that keep the picture across sessions, and the screen
/// following the machine's overscan viewport.

#include "mainwindow.h"

#include <QSettings>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/video/viewportcontrol.h"
#include "emulator/video/windowdisplay.h"
#include "hud/core/hudmodel.h"
#include "hud/qt/hudoverlaywrapper.h"
#include "hud/qt/hudsettingsdialog.h"
#include "nextlayers/nextlayerswindow.h"
#include "osdkeyboard/osdkeyboardwindow.h"
#include "ui/temporaleffectsdialog.h"
#include "widgets/crtprofiles.h"
#include "widgets/slangpresetrunner.h"

namespace
{
/// The automation word of a custom CRT shader: "shader:<name>"
const QString kShaderPrefix = QStringLiteral("shader:");

constexpr const char* kViewGroup = "View";

/// A float of the screen as a report shows it: 6 decimals, not 0.30000001192092896
double Shown(float value)
{
    return std::round(static_cast<double>(value) * 1e6) / 1e6;
}

/// The built-in profile's parameters (basic; the uniforms of a custom shader): field, range, a dial's step
struct ProfileParameter
{
    const char* name;
    const char* description;
    float CRTProfileParams::*field;
    float minimum;
    float maximum;
    float step;
};

const std::vector<ProfileParameter>& ProfileParameters()
{
    static const std::vector<ProfileParameter> list = {
        {"curvature", "Screen curvature (0 flat)", &CRTProfileParams::curvature, 0.0f, 0.5f, 0.01f},
        {"corner_radius", "Rounded corners", &CRTProfileParams::cornerRadius, 0.0f, 0.2f, 0.005f},
        {"scanline_weight", "Scanline darkness (0 none, 1 black)", &CRTProfileParams::scanlineWeight, 0.0f, 1.0f, 0.05f},
        {"bloom_strength", "Phosphor bloom", &CRTProfileParams::bloomStrength, 0.0f, 1.0f, 0.05f},
        {"bloom_radius", "Bloom spread (pixels)", &CRTProfileParams::bloomRadius, 0.0f, 8.0f, 0.5f},
        {"saturation", "Colour saturation", &CRTProfileParams::saturation, 0.0f, 2.0f, 0.05f},
        {"brightness", "Brightness", &CRTProfileParams::brightness, 0.5f, 2.0f, 0.05f},
        {"contrast", "Contrast", &CRTProfileParams::contrast, 0.5f, 2.0f, 0.05f},
        {"gamma", "Display gamma", &CRTProfileParams::gamma, 1.0f, 3.0f, 0.1f},
    };
    return list;
}

/// The viewport menu's index of a VideoViewport name (0 full, 1 symmetric, 2 standard, 3 screen only)
int ViewportIndex(const std::string& name)
{
    const std::vector<std::string>& names = VideoViewport::Names();
    const auto at = std::find(names.begin(), names.end(), name);
    return at == names.end() ? 1 : static_cast<int>(at - names.begin());
}
}  // namespace

/// region <Display settings>

WindowDisplaySettings MainWindow::windowDisplaySettings() const
{
    WindowDisplaySettings s;
    s.backend = _screenWrapper->isGPUAccelerated() ? "gpu" : "software";
    s.gpuAvailable = DeviceScreenWrapper::isGPUAvailable();
    s.gpu = _screenWrapper->isGPUAccelerated();
    s.crt = _screenWrapper->crtEffectsEnabled();
    // A custom shader is "shader:<name>", offered when the host has a GPU (it needs the GPU on: WindowDisplay::Set)
    const QString shader = _screenWrapper->customShader();
    s.crtProfile = shader.isEmpty() ? CRTProfileParams::ProfileKey(_screenWrapper->crtProfile()).toStdString()
                                    : (kShaderPrefix + shader).toStdString();
    for (CRTProfile profile : CRTProfileParams::AllProfiles())
        s.crtProfiles.push_back(CRTProfileParams::ProfileKey(profile).toStdString());
    if (s.gpuAvailable)
    {
        for (const QString& name : DeviceScreenWrapper::availableShaders())
            s.crtProfiles.push_back((kShaderPrefix + name).toStdString());
    }
    s.blending = _screenWrapper->temporalBlendingEnabled();
    s.blendingFrames = _screenWrapper->temporalHistorySize();
    s.blendingWeights = _screenWrapper->temporalWeightMode() == 1 ? "exponential" : "equal";
    s.blendingDecay = Shown(_screenWrapper->temporalDecay());
    s.emulatorId = _emulator ? _emulator->GetId() : std::string();

    s.fullscreen = isFullScreen();
    // A whole zoom while the window is fitted to the picture; resized by hand, maximized or full screen: none
    const bool fitted = !_userResizedWindow && !isFullScreen() && !isMaximized();
    const double zoom = std::round(_zoomFactor);
    s.scale = fitted && zoom == _zoomFactor && zoom >= WindowDisplay::kMinScale && zoom <= WindowDisplay::kMaxScale
                  ? static_cast<int>(zoom)
                  : 0;
    s.toolbar = _toolBarManager ? _toolBarManager->isVisibleByUser() : true;
    s.statusBar = _statusBarManager ? _statusBarManager->isVisibleByUser() : true;

    s.hud = _hudOverlayVisible;
    s.hudTheme = HudTheme::ThemeKey(_hudTheme);
    for (HudThemeId theme : HudTheme::AvailableThemes())
        s.hudThemes.push_back(HudTheme::ThemeKey(theme));
    for (const HudCategoryDescriptor& category : HudSettingsDialog::registeredCategories())
        s.hudNotices[category.id.toStdString()] = HudSettingsDialog::isCategoryEnabled(category.id);

    if (_osdKeyboardWindow)
    {
        _osdKeyboardWindow->followModel();  // its pictures are the shown machine's even while it is hidden
        s.osdKeyboard = _osdKeyboardWindow->isVisible();
        s.osdKeyboardPicture = _osdKeyboardWindow->picture().id;
        s.osdKeyboardPictures = _osdKeyboardWindow->choices();
        s.osdKeyboardStyle = _osdKeyboardWindow->style();
        s.osdKeyboardStyles = _osdKeyboardWindow->styles();
    }
    if (_nextLayersWindow)
    {
        s.nextLayers = _nextLayersWindow->isVisible();
        const std::vector<std::string>& modes = WindowDisplay::NextLayersModes();
        const int mode = _nextLayersWindow->mode();
        s.nextLayersMode = mode >= 0 && mode < static_cast<int>(modes.size()) ? modes[static_cast<size_t>(mode)] : "";
        const std::vector<std::string>& layers = WindowDisplay::NextLayerNames();
        for (size_t layer = 0; layer < layers.size(); ++layer)
            s.nextLayersShown[layers[layer]] = _nextLayersWindow->layerShown(static_cast<int>(layer));
    }
    return s;
}

void MainWindow::applyWindowDisplay(const WindowDisplayChange& change)
{
    // The order of the menu: the backend first (it re-creates the screen and carries the rest over), then the profile
    // (it switches CRT on or off by itself), then the switches that name it
    if (change.gpu)
    {
        _menuManager->setGpuAccelerationChecked(*change.gpu);
        handleGpuAccelerationToggled(*change.gpu);
    }
    if (change.crtProfile && QString::fromStdString(*change.crtProfile).startsWith(kShaderPrefix))
    {
        const QString name = QString::fromStdString(*change.crtProfile).mid(static_cast<int>(kShaderPrefix.size()));
        _menuManager->setCrtShader(name);
        handleCrtShaderChanged(name);
    }
    else if (change.crtProfile)
    {
        const std::vector<CRTProfile> profiles = CRTProfileParams::AllProfiles();
        for (int index = 0; index < static_cast<int>(profiles.size()); ++index)
        {
            if (CRTProfileParams::ProfileKey(profiles[index]).toStdString() != *change.crtProfile)
                continue;
            _menuManager->setCrtProfile(index);
            handleCrtProfileChanged(index);
        }
    }
    if (change.crt)
    {
        _menuManager->setCrtEffectsChecked(*change.crt);
        handleCrtEffectsToggled(*change.crt);
    }
    if (change.blendingFrames)
        _screenWrapper->setTemporalHistorySize(*change.blendingFrames);
    if (change.blendingDecay)
        _screenWrapper->setTemporalDecay(static_cast<float>(*change.blendingDecay));
    if (change.blendingWeights)
        _screenWrapper->setTemporalWeightMode(*change.blendingWeights == "exponential" ? 1 : 0);
    if (change.blending)
    {
        _menuManager->setTemporalBlendingChecked(*change.blending);
        handleTemporalBlendingToggled(*change.blending);
    }
    // An open Temporal Effects dialog shows what automation set
    if (change.blending || change.blendingFrames || change.blendingWeights || change.blendingDecay)
    {
        for (auto* dialog : findChildren<TemporalEffectsDialog*>())
            dialog->updateFromScreen();
    }

    if (change.toolbar)
    {
        _menuManager->setToolBarChecked(*change.toolbar);
        handleToolBarToggled(*change.toolbar);
    }
    if (change.statusBar)
    {
        _menuManager->setStatusBarChecked(*change.statusBar);
        handleStatusBarToggled(*change.statusBar);
    }
    if (change.hud)
    {
        _menuManager->setHudOverlayChecked(*change.hud);
        handleHudOverlayToggled(*change.hud);
    }
    HudThemeId theme = _hudTheme;
    if (change.hudTheme && HudTheme::ThemeFromKey(*change.hudTheme, theme))
        applyHudTheme(theme);
    for (const auto& [id, on] : change.hudNotices)
        HudSettingsDialog::setCategoryEnabled(QString::fromStdString(id), on);

    if (_osdKeyboardWindow && change.osdKeyboard)
    {
        _menuManager->setOsdKeyboardChecked(*change.osdKeyboard);
        handleOsdKeyboardToggled(*change.osdKeyboard);
    }
    if (_osdKeyboardWindow && change.osdKeyboardPicture)
    {
        _osdKeyboardWindow->followModel();
        _osdKeyboardWindow->choose(*change.osdKeyboardPicture);
    }
    if (_osdKeyboardWindow && change.osdKeyboardStyle)
    {
        _osdKeyboardWindow->followModel();
        _osdKeyboardWindow->chooseStyle(*change.osdKeyboardStyle);
    }
    if (_nextLayersWindow && change.nextLayers)
    {
        _menuManager->setNextLayersChecked(*change.nextLayers);
        handleNextLayersToggled(*change.nextLayers);
    }
    if (_nextLayersWindow && change.nextLayersMode)
    {
        const std::vector<std::string>& modes = WindowDisplay::NextLayersModes();
        const auto at = std::find(modes.begin(), modes.end(), *change.nextLayersMode);
        if (at != modes.end())
            _nextLayersWindow->setMode(static_cast<int>(at - modes.begin()));
    }
    if (_nextLayersWindow)
    {
        const std::vector<std::string>& layers = WindowDisplay::NextLayerNames();
        for (const auto& [name, shown] : change.nextLayersShown)
        {
            const auto at = std::find(layers.begin(), layers.end(), name);
            if (at != layers.end())
                _nextLayersWindow->setLayerShown(static_cast<int>(at - layers.begin()), shown);
        }
    }

    // The window's frame last: off full screen, then the scale (it leaves full screen itself), then on full screen
    if (change.fullscreen && !*change.fullscreen)
        setFullScreen(false);
    if (change.scale)
        handleScaleRequested(*change.scale);
    if (change.fullscreen && *change.fullscreen)
        setFullScreen(true);
}

void MainWindow::setFullScreen(bool on)
{
    if (isFullScreen() != on)
        handleFullScreenShortcut();
}

void MainWindow::applyHudTheme(HudThemeId theme)
{
    _hudTheme = theme;
    if (_hudModel)
        _hudModel->setTheme(theme);
    if (_hudWrapper)
        _hudWrapper->setTheme(theme);
    schedulePublishWindowDisplay();
}

/// endregion </Display settings>

/// region <CRT parameters>

CrtParameters MainWindow::crtParameters() const
{
    CrtParameters p;
    const QString shader = _screenWrapper->customShader();
    p.profile = shader.isEmpty() ? CRTProfileParams::ProfileKey(_screenWrapper->crtProfile()).toStdString()
                                 : (kShaderPrefix + shader).toStdString();
    if (!shader.isEmpty() && SlangPresetRunner::isPreset(shader))
    {
        p.kind = "preset";
        for (const SlangPresetRunner::Parameter& parameter : _screenWrapper->presetParameters())
        {
            CrtParameter item;
            item.name = parameter.name.toStdString();
            item.description = parameter.description.toStdString();
            item.value = Shown(parameter.value);
            item.initial = Shown(parameter.initial);
            item.minimum = Shown(parameter.minimum);
            item.maximum = Shown(parameter.maximum);
            item.step = Shown(parameter.step);
            p.parameters.push_back(std::move(item));
        }
        return p;
    }
    // Basic and the custom shaders (their uniforms) take the built-in profile's parameters; the beam models none
    if (shader.isEmpty() && _screenWrapper->crtProfile() != CRTProfile::Basic)
        return p;
    p.kind = "profile";
    const CRTProfileParams now = _screenWrapper->crtParams();
    const CRTProfileParams initial = CRTProfileParams::Basic();
    for (const ProfileParameter& parameter : ProfileParameters())
    {
        CrtParameter item;
        item.name = parameter.name;
        item.description = parameter.description;
        item.value = Shown(now.*parameter.field);
        item.initial = Shown(initial.*parameter.field);
        item.minimum = Shown(parameter.minimum);
        item.maximum = Shown(parameter.maximum);
        item.step = Shown(parameter.step);
        p.parameters.push_back(std::move(item));
    }
    return p;
}

void MainWindow::applyCrtParameters(const CrtParameterChange& change)
{
    const CrtParameters now = crtParameters();
    auto find = [&](const std::string& name) -> const CrtParameter* {
        for (const CrtParameter& parameter : now.parameters)
            if (parameter.name == name)
                return &parameter;
        return nullptr;
    };
    // The value each named parameter ends at (reset, then values, then steps), from the values now
    std::map<std::string, double> target;
    auto valueOf = [&](const CrtParameter& parameter) {
        const auto set = target.find(parameter.name);
        return set != target.end() ? set->second : (change.resetAll ? parameter.initial : parameter.value);
    };
    for (const std::string& name : change.reset)
        if (const CrtParameter* parameter = find(name))
            target[name] = parameter->initial;
    for (const auto& [name, value] : change.values)
        if (find(name) != nullptr)
            target[name] = value;
    for (const auto& [name, steps] : change.steps)
        if (const CrtParameter* parameter = find(name))
        {
            const double step = parameter->step > 0 ? parameter->step : (parameter->maximum - parameter->minimum) / 100.0;
            target[name] = valueOf(*parameter) + steps * step;
        }

    if (now.kind == "preset")
    {
        if (change.resetAll)
            _screenWrapper->resetPresetParameters();
        for (const auto& [name, value] : target)
            _screenWrapper->setPresetParameter(QString::fromStdString(name), static_cast<float>(value));
    }
    else if (now.kind == "profile")
    {
        CRTProfileParams params = _screenWrapper->crtParams();
        const CRTProfileParams initial = CRTProfileParams::Basic();
        for (const ProfileParameter& parameter : ProfileParameters())
        {
            if (change.resetAll)
                params.*parameter.field = initial.*parameter.field;
            const auto set = target.find(parameter.name);
            if (set != target.end())
                params.*parameter.field =
                    std::clamp(static_cast<float>(set->second), parameter.minimum, parameter.maximum);
        }
        _screenWrapper->setCRTParams(params);
    }
    schedulePublishWindowDisplay();
}

/// endregion </CRT parameters>

/// region <Published copy and settings>

void MainWindow::schedulePublishWindowDisplay()
{
    if (!_publishTimer)
    {
        _publishTimer = new QTimer(this);
        _publishTimer->setSingleShot(true);
        _publishTimer->setInterval(50);
        connect(_publishTimer, &QTimer::timeout, this, &MainWindow::publishWindowDisplay);
    }
    if (!_publishTimer->isActive())
        _publishTimer->start();
}

void MainWindow::publishWindowDisplay()
{
    if (!_screenWrapper)
        return;
    WindowView::Publish(windowDisplaySettings(), crtParameters());
    saveViewSettings();
}

void MainWindow::connectWindowDisplayPublishing()
{
    auto publish = [this]() { schedulePublishWindowDisplay(); };
    for (auto signal : {&MenuManager::gpuAccelerationToggled, &MenuManager::crtEffectsToggled,
                        &MenuManager::temporalBlendingToggled, &MenuManager::toolBarToggled,
                        &MenuManager::statusBarToggled, &MenuManager::hudOverlayToggled,
                        &MenuManager::overscanModeToggled})
        connect(_menuManager, signal, this, publish);
    connect(_menuManager, &MenuManager::crtProfileChanged, this, publish);
    connect(_menuManager, &MenuManager::viewportChanged, this, publish);
    connect(_menuManager, &MenuManager::scaleRequested, this, publish);
    connect(_menuManager, &MenuManager::crtShaderChanged, this, publish);
    connect(_menuManager, &MenuManager::fullScreenToggled, this, publish);
    if (_osdKeyboardWindow)
    {
        connect(_osdKeyboardWindow, &OsdKeyboardWindow::visibilityChanged, this, publish);
        connect(_osdKeyboardWindow, &OsdKeyboardWindow::pictureChanged, this, publish);
    }
    if (_nextLayersWindow)
    {
        connect(_nextLayersWindow, &NextLayersWindow::visibilityChanged, this, publish);
        connect(_nextLayersWindow, &NextLayersWindow::viewChanged, this, publish);
    }
    // The HUD Notifications dialog's check boxes (and automation's hud_notice)
    HudSettingsDialog::setChangedCallback([guard = QPointer<MainWindow>(this)]() {
        if (guard)
            guard->schedulePublishWindowDisplay();
    });
    schedulePublishWindowDisplay();
}

void MainWindow::saveViewSettings() const
{
    if (!_viewSettingsEnabled || !_screenWrapper)
        return;
    const WindowDisplaySettings s = windowDisplaySettings();
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    settings.beginGroup(kViewGroup);
    settings.setValue("Gpu", s.gpu);
    settings.setValue("Crt", s.crt);
    settings.setValue("CrtProfile", QString::fromStdString(s.crtProfile));
    // The values set on the preset shown, and the built-in profile's parameters
    const QString shader = _screenWrapper->customShader();
    if (!shader.isEmpty() && SlangPresetRunner::isPreset(shader))
    {
        QVariantMap values;
        for (const auto& [name, value] : _screenWrapper->presetOverrides())
            values.insert(name, value);
        settings.setValue(QStringLiteral("CrtPresetParams/") + shader, values);
    }
    if (_screenWrapper->crtProfile() == CRTProfile::Basic || !shader.isEmpty())
    {
        QVariantMap values;
        const CRTProfileParams params = _screenWrapper->crtParams();
        for (const ProfileParameter& parameter : ProfileParameters())
            values.insert(QString::fromLatin1(parameter.name), params.*parameter.field);
        settings.setValue("CrtProfileParams", values);
    }
    settings.setValue("Blending", s.blending);
    settings.setValue("BlendingFrames", s.blendingFrames);
    settings.setValue("BlendingWeights", QString::fromStdString(s.blendingWeights));
    settings.setValue("BlendingDecay", s.blendingDecay);
    if (s.scale > 0)
        settings.setValue("Scale", s.scale);  // a hand-sized window keeps the last whole scale
    settings.setValue("HudVisible", s.hud);
    settings.setValue("HudTheme", QString::fromStdString(s.hudTheme));
    // Not the overscan frame: it is the machine's, off on every new Pentagon (set it explicitly when wanted)
    settings.endGroup();
}

void MainWindow::restoreViewSettings()
{
    _viewSettingsEnabled = qEnvironmentVariable("UNREAL_QT_VIEW_SETTINGS").compare("off", Qt::CaseInsensitive) != 0;
    if (!_viewSettingsEnabled || !_screenWrapper)
        return;
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    settings.beginGroup(kViewGroup);

    // The backend first: it re-creates the screen
    if (settings.contains("Gpu"))
    {
        const bool gpu = settings.value("Gpu").toBool() && DeviceScreenWrapper::isGPUAvailable();
        if (gpu != _screenWrapper->isGPUAccelerated())
        {
            _menuManager->setGpuAccelerationChecked(gpu);
            handleGpuAccelerationToggled(gpu);
        }
    }

    const WindowDisplaySettings now = windowDisplaySettings();
    WindowDisplayChange change;
    std::string error;
    if (settings.contains("CrtProfile"))
    {
        const std::string profile = settings.value("CrtProfile").toString().toStdString();
        const bool offered = std::find(now.crtProfiles.begin(), now.crtProfiles.end(), profile) != now.crtProfiles.end();
        if (offered && (now.gpu || profile.rfind("shader:", 0) != 0))
            change.crtProfile = profile;
    }
    if (settings.contains("Crt"))
        change.crt = settings.value("Crt").toBool();
    if (settings.contains("BlendingFrames"))
        WindowDisplay::ApplyWord(change, "blending_frames", settings.value("BlendingFrames").toString().toStdString(), error);
    if (settings.contains("BlendingWeights"))
        WindowDisplay::ApplyWord(change, "blending_weights", settings.value("BlendingWeights").toString().toStdString(),
                                 error);
    if (settings.contains("BlendingDecay"))
    {
        const double decay = settings.value("BlendingDecay").toDouble();
        if (decay >= WindowDisplay::kMinBlendingDecay && decay <= WindowDisplay::kMaxBlendingDecay)
            change.blendingDecay = decay;
    }
    if (settings.contains("Blending"))
        change.blending = settings.value("Blending").toBool();
    if (settings.contains("HudVisible"))
        change.hud = settings.value("HudVisible").toBool();
    if (settings.contains("HudTheme"))
        change.hudTheme = settings.value("HudTheme").toString().toStdString();
    applyWindowDisplay(change);

    // The values set on the profile: after it is loaded
    const QString shader = _screenWrapper->customShader();
    if (!shader.isEmpty() && SlangPresetRunner::isPreset(shader))
    {
        const QVariantMap values = settings.value(QStringLiteral("CrtPresetParams/") + shader).toMap();
        std::map<QString, float> overrides;
        for (auto it = values.begin(); it != values.end(); ++it)
            overrides[it.key()] = it.value().toFloat();
        _screenWrapper->setPresetOverrides(overrides);
    }
    if (settings.contains("CrtProfileParams") && (_screenWrapper->crtProfile() == CRTProfile::Basic || !shader.isEmpty()))
    {
        const QVariantMap values = settings.value("CrtProfileParams").toMap();
        CRTProfileParams params = _screenWrapper->crtParams();
        for (const ProfileParameter& parameter : ProfileParameters())
        {
            const QVariant value = values.value(QString::fromLatin1(parameter.name));
            if (value.isValid())
                params.*parameter.field = std::clamp(value.toFloat(), parameter.minimum, parameter.maximum);
        }
        _screenWrapper->setCRTParams(params);
    }
    _startScale = std::clamp(settings.value("Scale", 2).toInt(), WindowDisplay::kMinScale, WindowDisplay::kMaxScale);
    settings.endGroup();
}

void MainWindow::handleVideoViewportChanged(int id, Message* message)
{
    // Arrives on the MessageCenter's thread: the screen follows on the GUI thread
    (void)id;
    if (!message || !message->obj)
        return;
    auto* payload = dynamic_cast<EmulatorFramePayload*>(message->obj);
    if (!payload)
        return;
    const auto emulatorId = payload->_emulatorId;
    QMetaObject::invokeMethod(
        this,
        [this, emulatorId]() {
            if (!_screenWrapper || !_emulator || emulatorId != _emulator->GetUUID())
                return;
            if (_emulator->IsOverscanMode())
            {
                const DisplayViewport viewport = _emulator->GetDisplayViewport();
                _screenWrapper->setDisplayViewport(viewport);
                const std::string name = VideoViewport::NameOf(viewport);
                if (name != "custom")
                    _menuManager->setViewportSelection(ViewportIndex(name));
            }
            else
            {
                _screenWrapper->clearDisplayViewport();
            }
            updateMenuStates();
            schedulePublishWindowDisplay();
        },
        Qt::QueuedConnection);
}

/// endregion </Published copy and settings>
