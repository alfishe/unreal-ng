#include "bindingtable.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTabBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cctype>
#include <cmath>

#include "controllers/actionregistry.h"
#include "controllers/hostcontrollers.h"

#ifdef ENABLE_CONTROLLERS
#include "devicedatabase.h"
#include "hostcontrols.h"
#include "profiles/yaml.h"

using namespace controllers;

namespace
{
constexpr int kCaptureTicks = 150;  // 5 s of the dialog's 33 ms live ticks
constexpr int kTicksPerSecond = 30;
constexpr float kEdge = 0.5f;
// The direction bits of a group, in this order: up 1, down 2, left 4, right 8
const char* const kDirections[] = {"up", "down", "left", "right"};
constexpr unsigned kAllDirections = 15;
constexpr int kLineHeight = 30;  // a line of the table: a chip and its margins
constexpr int kChipHeight = 24;

const char* const kChipStyle =
    "QPushButton { border: 1px solid palette(mid); border-radius: 10px; padding: 2px 10px; }";
const char* const kLitStyle =
    "QPushButton { border: 1px solid palette(highlight); border-radius: 10px; padding: 2px "
    "10px; background: palette(highlight); color: palette(highlighted-text); }";
const char* const kCaptureStyle =
    "QPushButton { border: 2px solid #d80; border-radius: 10px; padding: 2px 10px; "
    "background: #fe8; color: black; }";

// The ZX keyboard of the key picker, in its rows
const char* const kZxRows[][10] = {
    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
    {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"},
    {"A", "S", "D", "F", "G", "H", "J", "K", "L", "ENTER"},
    {"CAPS", "Z", "X", "C", "V", "B", "N", "M", "SYM", "SPACE"},
};

std::string Text(const JsonValue& spec, const char* field)
{
    const JsonValue* value = spec.Find(field);
    return value != nullptr && value->IsString() ? value->AsString() : std::string{};
}

double Number(const JsonValue& spec, const char* field)
{
    const JsonValue* value = spec.Find(field);
    return value != nullptr && value->IsNumber() ? value->AsNumber() : 0;
}

std::string Upper(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::toupper(c); });
    return text;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

JsonValue TargetSpec(const char* field, const std::string& value)
{
    return JsonValue::Object({{field, JsonValue::String(value)}});
}

JsonValue None()
{
    return JsonValue::Object({{"none", JsonValue::Bool(true)}});
}

bool IsNone(const JsonValue& spec)
{
    return spec.Find("none") != nullptr;
}

std::vector<std::string> Split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t at; (at = text.find(separator, start)) != std::string::npos; start = at + 1)
        parts.push_back(text.substr(start, at - start));
    parts.push_back(text.substr(start));
    return parts;
}

bool IsChord(const std::string& control)
{
    return control.size() > 1 && control.find('+') != std::string::npos;
}

/// The row a binding shows in: what the machine gets
std::string RowKeyOf(const std::string& control, const JsonValue& spec)
{
    if (IsNone(spec))
        return {};
    if (spec.Find("retro") != nullptr)
    {
        const std::string retro = Text(spec, "retro");
        const size_t colon = retro.rfind(':');
        return "retro:" + Lower(colon == std::string::npos ? retro : retro.substr(colon + 1));
    }
    if (spec.Find("key") != nullptr)
        return "key:" + Upper(Text(spec, "key"));
    if (const JsonValue* keys = spec.Find("keys"); keys != nullptr && keys->IsArray())
    {
        std::string joined;
        for (const JsonValue& key : keys->Items())
            joined += (joined.empty() ? "" : "+") + Upper(key.AsString());
        return "key:" + joined;
    }
    if (spec.Find("macro") != nullptr)
        return "macro:" + control;
    if (spec.Find("mouse") != nullptr)
    {
        const std::string mouse = Text(spec, "mouse");
        return mouse == "dx" || mouse == "dy" ? "mouse:move" : "mouse:" + mouse;
    }
    if (spec.Find("action") != nullptr)
        return "action:" + Text(spec, "action");
    if (spec.Find("layer") != nullptr)
        return "layer:" + Text(spec, "layer");
    return "other:" + control;
}

/// What part of a D-pad or a stick a control covers: the group and its direction bits (none for the rest)
struct Cover
{
    std::string group;
    unsigned bits = 0;
};

unsigned DirectionBit(const std::string& direction)
{
    for (unsigned i = 0; i < 4; ++i)
        if (direction == kDirections[i])
            return 1u << i;
    return 0;
}

Cover CoverOf(const std::string& control)
{
    if (IsChord(control))
        return {};
    if (control == "dpad.*")
        return {"dpad", kAllDirections};
    if (control.rfind("dpad.", 0) == 0)
        return {"dpad", DirectionBit(control.substr(5))};
    for (const std::string stick : {"lstick", "rstick"})
    {
        if (control == stick)
            return {stick, kAllDirections};
        if (control.rfind(stick + ".", 0) != 0)
            continue;
        const std::string part = control.substr(stick.size() + 1);
        if (part == "x")
            return {stick, 4 | 8};
        if (part == "y")
            return {stick, 1 | 2};
        return {stick, DirectionBit(part)};
    }
    return {};
}

/// Both fire from the same press (dpad.* and dpad.left, lstick.x and lstick.left)
bool Overlaps(const std::string& a, const std::string& b)
{
    if (a == b)
        return true;
    const Cover x = CoverOf(a);
    const Cover y = CoverOf(b);
    return x.bits != 0 && y.bits != 0 && x.group == y.group && (x.bits & y.bits) != 0;
}

std::string DirectionControl(const std::string& group, const char* direction)
{
    return group + "." + direction;
}

/// The picture's hotspots a control is drawn on: a stick is its x axis, a trigger click the trigger
std::vector<std::string> HotspotsOf(const std::string& control)
{
    if (IsChord(control))
        return {};
    const Cover cover = CoverOf(control);
    if (cover.group == "dpad")
    {
        std::vector<std::string> spots;
        for (unsigned i = 0; i < 4; ++i)
            if (cover.bits & (1u << i))
                spots.push_back(DirectionControl("dpad", kDirections[i]));
        return spots;
    }
    if (!cover.group.empty())
        return {cover.group + ".x"};
    if (control == "lt.click" || control == "rt.click")
        return {control.substr(0, 2)};
    if (control.rfind("key.", 0) == 0)
        return {"key." + control.substr(control.rfind('.') + 1)};  // key.ctrl.f10 is the f10 key
    return {control};
}

bool Active(const std::string& control, const std::map<std::string, float>& live)
{
    const auto value = [&live](const std::string& name) {
        auto it = live.find(name);
        return it != live.end() ? it->second : 0.0f;
    };
    if (IsChord(control))
    {
        for (const std::string& part : Split(control, '+'))
            if (!Active(part, live))
                return false;
        return true;
    }
    const Cover cover = CoverOf(control);
    if (cover.bits != 0)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            if (!(cover.bits & (1u << i)))
                continue;
            if (cover.group == "dpad")
            {
                if (value(DirectionControl("dpad", kDirections[i])) >= kEdge)
                    return true;
                continue;
            }
            const float axis = value(cover.group + (i < 2 ? ".y" : ".x"));
            if ((i % 2 == 0 && axis <= -kEdge) || (i % 2 == 1 && axis >= kEdge))
                return true;
        }
        return false;
    }
    if (control == "lt.click" || control == "rt.click")
        return value(control.substr(0, 2)) >= kEdge;
    return std::fabs(value(control)) >= kEdge;
}

/// A capture ignores what moves by itself or is no press: motion, the touchpad's finger, the mouse, an encoder's
/// turn and the hand on it (a Surface Dial's `knob` and `touch`)
bool Ignored(const std::string& name)
{
    for (const char* prefix : {"gyro.", "accel.", "mouse.", "pointer."})
        if (name.rfind(prefix, 0) == 0)
            return true;
    ControlInfo info;
    if (name == "touch" || (ParseControl(name, info) && info.type == ControlType::Encoder))
        return true;
    return name.rfind("touchpad.", 0) == 0 && name != "touchpad.click";
}

QString KeyTitle(const std::string& key)
{
    QStringList parts;
    for (const std::string& part : Split(Upper(key), '+'))
    {
        if (part == "CAPS" || part == "CS" || part == "SHIFT" || part == "CAPSSHIFT" || part == "CAPS_SHIFT")
            parts << BindingTable::tr("Caps Shift");
        else if (part == "SYM" || part == "SS" || part == "SYMBOL" || part == "SYMSHIFT" || part == "SYM_SHIFT")
            parts << BindingTable::tr("Symbol Shift");
        else if (part == "SPACE")
            parts << BindingTable::tr("Space");
        else if (part == "ENTER" || part == "RETURN")
            parts << BindingTable::tr("Enter");
        else if (part.size() > 1)
            parts << QString::fromStdString(part.substr(0, 1) + Lower(part.substr(1)));
        else
            parts << QString::fromStdString(part);
    }
    return parts.join(" + ");
}

QString RetroTitle(const std::string& name)
{
    static const std::map<std::string, const char*> titles = {
        {"dpad", QT_TRANSLATE_NOOP("BindingTable", "Move")},    {"up", QT_TRANSLATE_NOOP("BindingTable", "Up")},
        {"down", QT_TRANSLATE_NOOP("BindingTable", "Down")},    {"left", QT_TRANSLATE_NOOP("BindingTable", "Left")},
        {"right", QT_TRANSLATE_NOOP("BindingTable", "Right")},  {"fire", QT_TRANSLATE_NOOP("BindingTable", "Fire")},
        {"fire2", QT_TRANSLATE_NOOP("BindingTable", "Fire 2")}, {"fire3", QT_TRANSLATE_NOOP("BindingTable", "Fire 3")},
        {"fire4", QT_TRANSLATE_NOOP("BindingTable", "Fire 4")}, {"start", QT_TRANSLATE_NOOP("BindingTable", "Start")},
        {"mode", QT_TRANSLATE_NOOP("BindingTable", "Mode")},
    };
    auto it = titles.find(name);
    if (it != titles.end())
        return BindingTable::tr(it->second);
    return BindingTable::tr("Button %1").arg(QString::fromStdString(Upper(name)));
}
}  // namespace

BindingTable::BindingTable(HostControllers* host, QWidget* parent) : QWidget(parent), _host(host)
{
    setFocusPolicy(Qt::StrongFocus);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto* top = new QHBoxLayout();
    _layers = new QTabBar(this);
    _layers->setDrawBase(false);
    _layers->addTab(tr("Playing"));
    _layers->setTabData(0, "play");
    _layers->addTab(tr("Debugging"));
    _layers->setTabData(1, "debug");
    _layers->setTabToolTip(1, tr("What the pad does while its debugger key is held (View + Menu on most pads)"));
    _layers->setTabVisible(1, false);
    top->addWidget(_layers);
    top->addStretch(1);
    top->addWidget(new QLabel(tr("Works as"), this));
    _scheme = new QComboBox(this);
    _scheme->setToolTip(tr("How the pad drives the machine: its joystick, or the keys a game reads"));
    top->addWidget(_scheme);
    top->addSpacing(12);
    top->addWidget(new QLabel(tr("Save for"), this));
    _scope = new QComboBox(this);
    _scope->setToolTip(tr("Whose bindings a change goes to"));
    top->addWidget(_scope);
    root->addLayout(top);

    auto* bar = new QFrame(this);
    bar->setObjectName("bindingPrompt");
    bar->setStyleSheet(
        "#bindingPrompt { background: #fe8; border: 1px solid #d80; border-radius: 4px; } "
        "#bindingPrompt QLabel { color: black; }");
    auto* barLayout = new QHBoxLayout(bar);
    _prompt = new QLabel(bar);
    _prompt->setWordWrap(true);
    _prompt->setTextFormat(Qt::RichText);
    barLayout->addWidget(_prompt, 1);
    _promptYes = new QPushButton(tr("Use it here"), bar);
    _promptBack = new QPushButton(tr("Back"), bar);
    _promptSkip = new QPushButton(tr("Skip"), bar);
    _promptCancel = new QPushButton(tr("Cancel"), bar);
    for (QPushButton* button : {_promptYes, _promptBack, _promptSkip, _promptCancel})
        barLayout->addWidget(button);
    bar->hide();
    root->addWidget(bar);

    _grid = new QTableWidget(0, 2, this);
    _grid->setHorizontalHeaderLabels({tr("Machine input"), tr("Pad controls")});
    QHeaderView* columns = _grid->horizontalHeader();
    columns->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    columns->setSectionResizeMode(1, QHeaderView::Stretch);
    columns->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    columns->setHighlightSections(false);
    _grid->verticalHeader()->hide();
    _grid->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    _grid->verticalHeader()->setDefaultSectionSize(kLineHeight);
    _grid->setShowGrid(false);
    _grid->setFrameShape(QFrame::NoFrame);
    _grid->setSelectionMode(QAbstractItemView::NoSelection);
    _grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _grid->setFocusPolicy(Qt::NoFocus);
    _grid->setWordWrap(false);
    _grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    _grid->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    root->addWidget(_grid, 1);

    auto* bottom = new QHBoxLayout();
    _setAll = new QPushButton(tr("Set all…"), this);
    _setAll->setToolTip(tr("Press the pad's controls for Up, Down, Left, Right, Fire and Fire 2 in turn"));
    _restore = new QPushButton(tr("Restore defaults"), this);
    _restore->setToolTip(tr("Remove your changes in this tab for the Save for choice"));
    _status = new QLabel(this);
    _status->setWordWrap(true);
    bottom->addWidget(_setAll);
    bottom->addWidget(_restore);
    bottom->addWidget(_status, 1);
    root->addLayout(bottom);

    connect(_layers, &QTabBar::currentChanged, this, [this] {
        cancelCapture();
        refresh();
    });
    connect(_scope, &QComboBox::currentIndexChanged, this, [this] {
        if (!_filling)
            refreshScope();
    });
    connect(_scheme, &QComboBox::currentIndexChanged, this, [this] {
        if (!_filling)
            schemeChosen();
    });
    connect(_setAll, &QPushButton::clicked, this, &BindingTable::startWizard);
    connect(_restore, &QPushButton::clicked, this, &BindingTable::restoreDefaults);
    connect(_promptYes, &QPushButton::clicked, this, [this] {
        const Binds binds = std::move(_pendingBinds);
        const std::string replace = _pendingReplace;
        _pendingBinds.clear();
        hidePrompt();
        apply(binds, replace);
    });
    connect(_promptBack, &QPushButton::clicked, this, [this] {
        if (_wizardStep > 0)
            --_wizardStep;
        wizardStep();
    });
    connect(_promptSkip, &QPushButton::clicked, this, [this] {
        ++_wizardStep;
        wizardStep();
    });
    connect(_promptCancel, &QPushButton::clicked, this, [this] { cancelCapture(tr("Cancelled: nothing changed")); });
    populate();
}

QString BindingTable::ControlName(const std::string& control, const std::string& glyphs)
{
    if (IsChord(control))
    {
        QStringList parts;
        for (const std::string& part : Split(control, '+'))
            parts << ControlName(part, glyphs);
        return parts.join(" + ");
    }
    if (control.rfind("key.", 0) == 0)
    {
        // A keyboard shortcut: `key.ctrl.f10` is Ctrl + F10 (Command + F10 on macOS)
        QStringList parts;
        for (const std::string& part : Split(control.substr(4), '.'))
        {
#if defined(Q_OS_MACOS)
            const char* modifier = part == "ctrl" ? "Cmd" : part == "alt" ? "Option" : part == "meta" ? "Ctrl" : nullptr;
#else
            const char* modifier = part == "ctrl" ? "Ctrl" : part == "alt" ? "Alt" : part == "meta" ? "Meta" : nullptr;
#endif
            if (modifier == nullptr && part == "shift")
                modifier = "Shift";
            parts << (modifier != nullptr ? QString(modifier) : QString::fromStdString(Upper(part)));
        }
        return parts.join(" + ");
    }
    const DeviceDatabase& database = DeviceDatabase::Builtin();
    const auto glyph = [&](const std::string& name) {
        if (glyphs.empty())
            return QString();
        const std::string text = database.Glyph(glyphs, name);
        return text == name ? QString() : QString::fromStdString(text);
    };
    static const std::map<std::string, const char*> arrows = {{"up", "↑"},    {"down", "↓"}, {"left", "←"},
                                                              {"right", "→"}, {"x", "↔"},    {"y", "↕"}};
    if (control == "dpad.*")
        return tr("D-pad");
    if (control.rfind("dpad.", 0) == 0)
        if (auto arrow = arrows.find(control.substr(5)); arrow != arrows.end())
            return tr("D-pad %1").arg(QString::fromUtf8(arrow->second));
    for (const std::string stick : {"lstick", "rstick"})
    {
        const QString name = stick == "lstick" ? tr("Left stick") : tr("Right stick");
        if (control == stick)
            return name;
        if (control.rfind(stick + ".", 0) != 0)
            continue;
        const std::string part = control.substr(stick.size() + 1);
        if (part == "click")
            return tr("%1 press").arg(name);
        if (auto arrow = arrows.find(part); arrow != arrows.end())
            return name + " " + QString::fromUtf8(arrow->second);
    }
    if (control == "lt.click" || control == "rt.click")
    {
        const QString text = glyph(control.substr(0, 2));
        return !text.isEmpty() ? text : control[0] == 'l' ? tr("Left trigger") : tr("Right trigger");
    }
    if (const QString text = glyph(control); !text.isEmpty())
        return text;
    static const std::map<std::string, const char*> names = {
        {"south", QT_TRANSLATE_NOOP("BindingTable", "Bottom button")},
        {"east", QT_TRANSLATE_NOOP("BindingTable", "Right button")},
        {"west", QT_TRANSLATE_NOOP("BindingTable", "Left button")},
        {"north", QT_TRANSLATE_NOOP("BindingTable", "Top button")},
        {"lb", QT_TRANSLATE_NOOP("BindingTable", "Left shoulder")},
        {"rb", QT_TRANSLATE_NOOP("BindingTable", "Right shoulder")},
        {"lt", QT_TRANSLATE_NOOP("BindingTable", "Left trigger")},
        {"rt", QT_TRANSLATE_NOOP("BindingTable", "Right trigger")},
        {"view", QT_TRANSLATE_NOOP("BindingTable", "View")},
        {"menu", QT_TRANSLATE_NOOP("BindingTable", "Menu")},
        {"guide", QT_TRANSLATE_NOOP("BindingTable", "Guide")},
        {"share", QT_TRANSLATE_NOOP("BindingTable", "Share")},
        {"touchpad.click", QT_TRANSLATE_NOOP("BindingTable", "Touchpad")},
    };
    if (auto it = names.find(control); it != names.end())
        return tr(it->second);
    if (control.rfind("paddle.p", 0) == 0)
        return tr("Paddle %1").arg(QString::fromStdString(control.substr(8)));
    if (control.rfind("misc.", 0) == 0)
        return tr("Extra button %1").arg(QString::fromStdString(control.substr(5)));
    return QString::fromStdString(control);
}

void BindingTable::setDevice(const Device& device)
{
    if (device.id != _device.id || device.family != _device.family)
    {
        cancelCapture();
        _status->clear();
    }
    _device = device;
    setDebugTab(_debugTab);
    refresh();
}

void BindingTable::setDebugTab(bool on)
{
    _debugTab = on;
    // The keyboard's debug bindings are the debugger's shortcuts, a control surface's (the Surface Dial) its time
    // travel remote: there without the debug layer
    const bool keyboard = _device.family == "keyboard";
    const bool surface = !keyboard && !_device.family.empty() && _device.family != "mouse" &&
                         !DeviceDatabase::Builtin().ForFamily(_device.family).player;
    const bool shown = on || keyboard || surface;
    _layers->setTabVisible(1, shown);
    _layers->setTabToolTip(1, keyboard  ? tr("The debugger's shortcuts: in the debugger window, and in the main window "
                                             "with global shortcuts on")
                              : surface ? tr("What the control does in the debugger: it is a debugger remote")
                                        : tr("What the pad does while its debugger key is held (View + Menu on most "
                                             "pads)"));
    if (!shown && _layers->currentIndex() == 1)
        _layers->setCurrentIndex(0);
}

std::string BindingTable::layer() const
{
    const QString data = _layers->tabData(_layers->currentIndex()).toString();
    return data.isEmpty() ? "play" : data.toStdString();
}

std::map<std::string, std::string> BindingTable::scopeOptions() const
{
    const std::string scope = _scope->currentData().toString().toStdString();
    if (scope.rfind("game:", 0) == 0)
        return {{"profile", scope.substr(5)}};
    if (scope == "family")
        return {{"family", _device.family}};
    if (scope == "device")
        return {{"device", _device.id}};
    return {};
}

std::string BindingTable::scopeProfile() const
{
    const std::string scope = _scope->currentData().toString().toStdString();
    if (scope.rfind("game:", 0) == 0)
        return scope.substr(5);
    return HostInputControl::UserProfileFor(scope == "device" ? _device.id : std::string{},
                                            scope == "family" ? _device.family : std::string{});
}

void BindingTable::say(const QString& text, bool error)
{
    _status->setStyleSheet(error ? QString("color: #c33") : QString());
    _status->setText(text);
}

void BindingTable::refreshScope()
{
    _filling = true;
    // Save for: the game running now (when a game profile applies), the pad's family, this pad, every pad
    const QString keep = _scope->currentData().toString();
    _scope->clear();
    const ResolvedProfiles& resolved = _host->Resolved();
    for (auto id = resolved.applied.rbegin(); id != resolved.applied.rend(); ++id)
        if (const Profile* profile = _host->Profiles().Find(*id); profile != nullptr && profile->IsGame())
        {
            const std::string& name = !profile->label.empty()   ? profile->label
                                      : !profile->title.empty() ? profile->title
                                                                : profile->id;
            _scope->addItem(tr("This game (%1)").arg(QString::fromStdString(name)),
                            QString::fromStdString("game:" + profile->id));
            break;
        }
    const bool keyboard = _device.family == "keyboard";
    if (keyboard)
        _scope->addItem(tr("The keyboard"), "family");
    else if (!_device.family.empty())
        _scope->addItem(tr("Every %1 pad").arg(QString::fromStdString(_device.family)), "family");
    if (!_device.id.empty() && !keyboard)
        _scope->addItem(tr("Only this pad"), "device");
    _scope->addItem(tr("Every pad"), "global");
    int index = _scope->findData(keep);
    if (index < 0)
        index = _scope->findData("family");
    _scope->setCurrentIndex(std::max(0, index));
    _scope->setToolTip(tr("Changes go to %1").arg(QString::fromStdString(scopeProfile())));

    // Works as: the scheme the scope's profile extends
    _scheme->clear();
    _scheme->addItem(tr("Machine joystick (default)"), QString());
    _scheme->setItemData(0, tr("The pad is the machine's joystick (Kempston on most machines)"), Qt::ToolTipRole);
    std::vector<const Profile*> schemes;
    for (const Profile* profile : _host->Profiles().Profiles())
        if (profile->scheme)
            schemes.push_back(profile);
    std::sort(schemes.begin(), schemes.end(), [](const Profile* a, const Profile* b) { return a->label < b->label; });
    for (const Profile* profile : schemes)
        _scheme->addItem(QString::fromStdString(profile->label.empty() ? profile->id : profile->label),
                         QString::fromStdString(profile->id));
    const Profile* own = _host->Profiles().Find(scopeProfile());
    const QString current =
        own != nullptr && !own->extends.empty() ? QString::fromStdString(own->extends.front()) : QString();
    index = _scheme->findData(current);
    if (index < 0 && !current.isEmpty())
    {
        _scheme->addItem(current, current);
        index = _scheme->count() - 1;
    }
    _scheme->setCurrentIndex(std::max(0, index));
    _scheme->setEnabled(!_device.id.empty() && layer() == "play");
    _filling = false;
}

BindingTable::Row& BindingTable::addRow(const std::string& key, const QString& section, const QString& title,
                                        const QString& tooltip, JsonValue target, Capture capture)
{
    Row row;
    row.key = key;
    row.section = section;
    row.title = title;
    row.tooltip = tooltip;
    row.target = std::move(target);
    row.capture = capture;
    _rows.push_back(std::move(row));
    return _rows.back();
}

BindingTable::Row* BindingTable::findRow(const std::string& key)
{
    auto it = std::find_if(_rows.begin(), _rows.end(), [&key](const Row& row) { return row.key == key; });
    return it != _rows.end() ? &*it : nullptr;
}

const BindingTable::Bound* BindingTable::findBound(const std::string& control) const
{
    auto it = std::find_if(_effective.begin(), _effective.end(),
                           [&control](const Bound& bound) { return bound.control == control; });
    return it != _effective.end() ? &*it : nullptr;
}

QString BindingTable::fullTitle(const std::string& rowKey)
{
    const Row* row = findRow(rowKey);
    if (row == nullptr)
        return QString::fromStdString(rowKey);
    if (row->section == tr("Joystick") || row->section == tr("Mouse"))
        return row->section + " " + row->title.toLower();
    if (row->section == tr("ZX keys"))
        return tr("ZX key %1").arg(row->title);
    return row->title;
}

void BindingTable::buildRows()
{
    _rows.clear();
    if (_device.id.empty())
        return;
    const auto actionTitle = [this](const std::string& id) {
        const ActionRegistry::Action* action = _actions != nullptr ? _actions->Find(id) : nullptr;
        return action != nullptr ? action->label : QString::fromStdString(id);
    };
    if (layer() != "play")
    {
        if (_actions != nullptr)
            for (const ActionRegistry::Action* action : _actions->Actions())
                addRow("action:" + action->id, action->group, action->label, QString::fromStdString(action->id),
                       TargetSpec("action", action->id));
        return;
    }
    const QString joystick = tr("Joystick");
    addRow("retro:dpad", joystick, tr("Move"), tr("All four directions at once: the D-pad or a stick"),
           TargetSpec("retro", "auto:dpad"), Capture::Directions);
    for (const char* direction : kDirections)
        addRow(std::string("retro:") + direction, joystick, RetroTitle(direction),
               tr("One direction alone (the rest of a D-pad or stick bound to Move keeps moving)"),
               TargetSpec("retro", std::string("auto:") + direction));
    addRow("retro:fire", joystick, tr("Fire"), {}, TargetSpec("retro", "auto:fire"));
    addRow("retro:fire2", joystick, tr("Fire 2"), tr("The second button (Sega pads, Kempston 2-button)"),
           TargetSpec("retro", "auto:fire2"));
    if (!_pendingKey.empty())
        addRow("key:" + Upper(_pendingKey), tr("ZX keys"), KeyTitle(_pendingKey), {}, TargetSpec("key", _pendingKey));
    const QString mouse = tr("Mouse");
    addRow("mouse:move", mouse, tr("Move"), tr("A stick moves the Kempston mouse"), TargetSpec("mouse", "dx"),
           Capture::MouseMove);
    addRow("mouse:left", mouse, tr("Left button"), {}, TargetSpec("mouse", "left"));
    addRow("mouse:right", mouse, tr("Right button"), {}, TargetSpec("mouse", "right"));
    const QString emulator = tr("Emulator");
    for (const char* id : {"run.pause_toggle", "osd.keyboard", "osd.quick_menu", "mach.turbo_toggle", "run.turbo_hold",
                           "cap.screenshot", "mach.reset"})
        if (_actions != nullptr && _actions->Find(id) != nullptr)
            addRow(std::string("action:") + id, emulator, actionTitle(id), QString::fromUtf8(id),
                   TargetSpec("action", id));
}

void BindingTable::refresh()
{
    _effective.clear();
    if (!_device.id.empty())
    {
        const HostReply reply = _host->Host({"bindings", {{"device", _device.id}, {"layer", layer()}}});
        if (const JsonValue* bindings = reply.body.Find("bindings"))
            for (const JsonValue& binding : bindings->Items())
                if (const JsonValue* spec = binding.Find("spec"))
                    _effective.push_back({Text(binding, "control"), *spec, Text(binding, "profile")});
    }
    refreshScope();
    buildRows();

    const bool play = layer() == "play";
    for (const Bound& bound : _effective)
    {
        const std::string key = RowKeyOf(bound.control, bound.spec);
        if (key.empty())
            continue;
        if (findRow(key) == nullptr)
        {
            // A row for what the profiles bind beyond the fixed ones: a key, an MD pad button, a macro, an action
            const std::string what = key.substr(key.find(':') + 1);
            const QString yaml = QString::fromStdString(DumpYaml(bound.spec, 0)).trimmed();
            if (key.rfind("retro:", 0) == 0)
                addRow(key, tr("Joystick"), RetroTitle(what), {}, TargetSpec("retro", "auto:" + what));
            else if (key.rfind("key:", 0) == 0)
                addRow(key, tr("ZX keys"), KeyTitle(what), {}, TargetSpec("key", what));
            else if (key.rfind("macro:", 0) == 0)
                addRow(key, tr("ZX keys"), tr("Typed keys"), yaml, bound.spec);
            else if (key.rfind("mouse:", 0) == 0)
                addRow(key, tr("Mouse"), what == "middle" ? tr("Middle button") : tr("Wheel"), {},
                       TargetSpec("mouse", what));
            else if (key.rfind("action:", 0) == 0)
            {
                const ActionRegistry::Action* action = _actions != nullptr ? _actions->Find(what) : nullptr;
                addRow(key, play || action == nullptr ? tr("Emulator") : action->group,
                       action != nullptr ? action->label : QString::fromStdString(what), QString::fromStdString(what),
                       TargetSpec("action", what));
            }
            else if (key.rfind("layer:", 0) == 0)
                addRow(key, tr("Emulator"), tr("Layer %1").arg(QString::fromStdString(what)), yaml, bound.spec);
            else
                addRow(key, tr("Other"), tr("Something else"), yaml, bound.spec);
        }
        Row& row = *findRow(key);
        const Profile* from = _host->Profiles().Find(bound.profile);
        const bool user = from != nullptr && from->scope == ProfileScope::User;
        if (key == "mouse:move")
        {
            // A stick's two axes are one chip
            const Cover cover = CoverOf(bound.control);
            const std::string stick = cover.group.empty() || cover.group == "dpad" ? bound.control : cover.group;
            auto chip = std::find_if(row.chips.begin(), row.chips.end(),
                                     [&stick](const Chip& c) { return c.control == stick; });
            if (chip != row.chips.end())
            {
                chip->parts.push_back(bound.control);
                chip->user = chip->user || user;
                continue;
            }
            row.chips.push_back({stick, {bound.control}, bound.spec, bound.profile, user});
            continue;
        }
        row.chips.push_back({bound.control, {bound.control}, bound.spec, bound.profile, user});
    }
    updateCaptions();
    populate();
}

void BindingTable::updateCaptions()
{
    _captions.clear();
    for (const Row& row : _rows)
    {
        const QString caption = row.section == tr("Mouse") ? fullTitle(row.key) : row.title;
        for (const Chip& chip : row.chips)
            for (const std::string& part : chip.parts)
                for (const std::string& spot : HotspotsOf(part))
                {
                    // A key with modifiers names its combo: "Ctrl + F10: Step out"
                    const QString entry =
                        spot != part && part.rfind("key.", 0) == 0 ? ControlName(part, _device.glyphs) + ": " + caption
                                                                    : caption;
                    QString& text = _captions[spot];
                    if (!text.split(" / ").contains(entry))
                        text += (text.isEmpty() ? "" : " / ") + entry;
                }
    }
    emit captionsChanged();
}

QWidget* BindingTable::chipButton(const Row& row, const Chip& chip)
{
    QString text = ControlName(chip.control, _device.glyphs);
    const std::string turbo = Text(chip.spec, "turbo");
    if (!turbo.empty() && turbo != "off")
        text += tr(" · auto");
    if (Text(chip.spec, "press") == "tap")
        text += tr(" · tap");
    auto* button = new QPushButton(text);
    button->setFixedHeight(kChipHeight);
    QFont font = button->font();
    font.setBold(chip.user);
    button->setFont(font);
    button->setStyleSheet(kChipStyle);
    button->setToolTip(tr("%1 → %2\nFrom %3%4\nClick to change it, right-click for more")
                           .arg(ControlName(chip.control, _device.glyphs), fullTitle(row.key),
                                QString::fromStdString(chip.profile), chip.user ? tr(" (yours)") : QString()));
    const std::vector<std::string> spots = HotspotsOf(chip.control);
    button->setProperty("hotspot", QString::fromStdString(spots.empty() ? std::string{} : spots.front()));
    button->setProperty("row", QString::fromStdString(row.key));
    button->installEventFilter(this);
    button->setContextMenuPolicy(Qt::CustomContextMenu);
    const std::string key = row.key;
    const Chip copy = chip;
    connect(button, &QPushButton::clicked, this, [this, key, copy] { startCapture(key, copy.control); });
    connect(button, &QWidget::customContextMenuRequested, this,
            [this, button, key, copy](const QPoint& at) { chipMenu(key, copy, button->mapToGlobal(at)); });
    if (!_captureRow.empty() && _captureRow == row.key && _captureReplace == chip.control)
        _captureButton = button;
    _chipButtons.emplace_back(button, chip);
    return button;
}

void BindingTable::populate()
{
    _filling = true;
    _chipButtons.clear();
    _lit.clear();
    _captureButton = nullptr;
    // Removing the rows deletes their widgets later: a button of them may be the one whose click got here
    _grid->setRowCount(0);
    const auto addLine = [this] {
        const int line = _grid->rowCount();
        _grid->insertRow(line);
        return line;
    };
    const auto spanned = [this, &addLine](const QString& text, bool header) {
        const int line = addLine();
        auto* item = new QTableWidgetItem(text);
        item->setFlags(Qt::ItemIsEnabled);
        if (header)
        {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
            item->setBackground(palette().alternateBase());
        }
        _grid->setItem(line, 0, item);
        _grid->setSpan(line, 0, 1, 2);
    };
    const auto cellOf = [](QWidget* parent) {
        auto* layout = new QHBoxLayout(parent);
        layout->setContentsMargins(4, 0, 4, 0);
        layout->setSpacing(4);
        return layout;
    };
    if (_device.id.empty())
        spanned(tr("Connect a game controller to bind it."), false);
    else
    {
        std::vector<QString> sections;
        if (layer() == "play")
            sections = {tr("Joystick"), tr("ZX keys"), tr("Mouse"), tr("Emulator")};
        for (const Row& row : _rows)
            if (std::find(sections.begin(), sections.end(), row.section) == sections.end())
                sections.push_back(row.section);
        for (const QString& section : sections)
        {
            spanned(section, true);
            for (const Row& row : _rows)
            {
                if (row.section != section)
                    continue;
                const int line = addLine();
                auto* title = new QTableWidgetItem(row.title);
                title->setFlags(Qt::ItemIsEnabled);
                title->setToolTip(row.tooltip);
                _grid->setItem(line, 0, title);
                auto* cell = new QWidget();
                QHBoxLayout* chips = cellOf(cell);
                for (const Chip& chip : row.chips)
                {
                    QWidget* button = chipButton(row, chip);
                    button->setProperty("line", line);
                    chips->addWidget(button);
                }
                auto* add = new QPushButton(row.chips.empty() ? tr("+ Bind") : QString("+"));
                add->setFlat(true);
                add->setFixedHeight(kChipHeight);
                add->setProperty("line", line);
                add->setToolTip(row.chips.empty() ? tr("Click, then press the control on the pad")
                                                  : tr("Add another control: click, then press it on the pad"));
                const std::string key = row.key;
                connect(add, &QPushButton::clicked, this, [this, key] { startCapture(key); });
                if (!_captureRow.empty() && _captureRow == row.key && _captureReplace.empty())
                    _captureButton = add;
                chips->addWidget(add);
                chips->addStretch(1);
                _grid->setCellWidget(line, 1, cell);
            }
            if (section == tr("ZX keys"))
            {
                const int line = addLine();
                auto* cell = new QWidget();
                auto* more = new QPushButton(tr("+ Add a ZX key…"));
                more->setFlat(true);
                more->setFixedHeight(kChipHeight);
                more->setToolTip(tr("Pick a key of the Spectrum's keyboard, then press the control for it"));
                connect(more, &QPushButton::clicked, this, &BindingTable::addKey);
                QHBoxLayout* layout = cellOf(cell);
                layout->addWidget(more);
                layout->addStretch(1);
                _grid->setCellWidget(line, 1, cell);
            }
        }
    }
    if (_captureButton != nullptr)
    {
        _captureButton->setStyleSheet(kCaptureStyle);
        _captureButton->setText(
            _wizard.empty()
                ? tr("Press… %1 s").arg((kCaptureTicks - _captureTicks + kTicksPerSecond - 1) / kTicksPerSecond)
                : tr("Press…"));
        reveal(_captureButton);
    }
    _setAll->setVisible(layer() == "play");
    _setAll->setEnabled(!_device.id.empty());
    _restore->setEnabled(!_device.id.empty());
    _filling = false;
    light();
}

void BindingTable::reveal(QWidget* widget)
{
    const QVariant line = widget->property("line");
    if (line.isValid())
        _grid->scrollTo(_grid->model()->index(line.toInt(), 1));
}

void BindingTable::light()
{
    for (const auto& [button, chip] : _chipButtons)
    {
        if (button == _captureButton)
            continue;
        const bool on = Active(chip.control, _live);
        if (on == (_lit.count(button) != 0))
            continue;
        if (on)
        {
            _lit.insert(button);
            button->setStyleSheet(kLitStyle);
            // Spectaculator's press-to-find: a pressed control shows where it is bound
            if (_captureRow.empty())
                reveal(button);
        }
        else
        {
            _lit.erase(button);
            button->setStyleSheet(kChipStyle);
        }
    }
}

void BindingTable::setLive(const std::map<std::string, float>& live)
{
    _live = live;
    if (!_captureRow.empty() && _pendingBinds.empty())
    {
        ++_captureTicks;
        if (_wizard.empty() && _captureTicks >= kCaptureTicks)
        {
            // PCSX2 / RetroArch: a timeout cancels, it never clears (the Mesen2 bug)
            cancelCapture(tr("Nothing was pressed in 5 s: nothing changed"));
            return;
        }
        if (_wizard.empty() && _captureButton != nullptr && _captureTicks % kTicksPerSecond == 0)
            _captureButton->setText(
                tr("Press… %1 s").arg((kCaptureTicks - _captureTicks + kTicksPerSecond - 1) / kTicksPerSecond));
        for (const auto& [name, value] : live)
        {
            if (Ignored(name))
                continue;
            // The rest a control is compared with: what it held at the start, or its value since it let go
            float& rest = _baseline[name];
            if (std::fabs(value) < kEdge)
            {
                rest = value;
                continue;
            }
            if (std::fabs(value - rest) >= kEdge)
            {
                captured(name, value);
                break;
            }
        }
    }
    light();
}

void BindingTable::startCapture(const std::string& rowKey, const std::string& replace)
{
    if (_device.id.empty())
        return;
    _pendingBinds.clear();
    _captureRow = rowKey;
    _captureReplace = replace;
    _captureTicks = 0;
    // The baseline: a resting trigger, a drifting stick, a button still held from the last press bind nothing
    _baseline = _host->Service().Live(_device.id);
    if (_wizard.empty())
    {
        const Row* row = findRow(rowKey);
        const Capture capture = row != nullptr ? row->capture : Capture::Button;
        const QString what = fullTitle(rowKey).toHtmlEscaped();
        showPrompt(
            capture == Capture::Directions  ? tr("Press the D-pad or push a stick for <b>%1</b>… Esc cancels").arg(what)
            : capture == Capture::MouseMove ? tr("Push a stick for <b>%1</b>… Esc cancels").arg(what)
            : _device.family == "keyboard"  ? tr("Press the keys for <b>%1</b>… Esc cancels").arg(what)
                                            : tr("Press the control for <b>%1</b> on the pad… Esc cancels").arg(what),
            false, false);
    }
    populate();
    setFocus();
}

void BindingTable::captured(const std::string& name, float value)
{
    const Row* row = findRow(_captureRow);
    if (row == nullptr)
    {
        cancelCapture();
        return;
    }
    const size_t dot = name.rfind('.');
    const std::string stick = dot != std::string::npos ? StickOf(name.substr(0, dot)) : std::string{};
    const bool stickAxis = !stick.empty() && (name.substr(dot + 1) == "x" || name.substr(dot + 1) == "y");
    // Changing a chip keeps its options (autofire, tap, label)
    JsonValue spec = row->target;
    if (!_captureReplace.empty() && row->capture != Capture::MouseMove)
        for (const Chip& chip : row->chips)
            if (chip.control == _captureReplace)
                spec = chip.spec;
    Binds binds;
    switch (row->capture)
    {
        case Capture::Button: {
            std::string control = name;
            if (stickAxis)
                control =
                    stick + "." + (name.back() == 'x' ? (value > 0 ? "right" : "left") : (value > 0 ? "down" : "up"));
            else if (name == "lt" || name == "rt")
                control = name + ".click";
            binds = {{control, spec}};
            break;
        }
        case Capture::Directions:
            if (name.rfind("dpad.", 0) == 0)
                binds = {{"dpad.*", spec}};
            else if (stickAxis)
                binds = {{stick, spec}};
            break;
        case Capture::MouseMove:
            if (stickAxis)
                binds = {{stick + ".x", TargetSpec("mouse", "dx")}, {stick + ".y", TargetSpec("mouse", "dy")}};
            break;
    }
    if (binds.empty())
    {
        // Not one that can do it: said, ignored until let go, and the wait goes on
        _baseline[name] = value;
        say(row->capture == Capture::MouseMove
                ? tr("%1 cannot move the mouse: push a stick").arg(ControlName(name, _device.glyphs))
                : tr("%1 cannot move the joystick: press the D-pad or push a stick")
                      .arg(ControlName(name, _device.glyphs)),
            true);
        return;
    }
    commit(binds, _captureReplace);
}

void BindingTable::commit(const Binds& binds, const std::string& replace)
{
    const std::string rowKey = _captureRow;
    const QString title = fullTitle(rowKey);
    const QString name =
        ControlName(binds.size() == 1 ? binds.front().first : CoverOf(binds.front().first).group, _device.glyphs);
    // Already so
    const bool same = std::all_of(binds.begin(), binds.end(), [this, &rowKey](const auto& entry) {
        const Bound* bound = findBound(entry.first);
        return bound != nullptr && RowKeyOf(bound->control, bound->spec) == rowKey;
    });
    if (same && (replace.empty() || replace == binds.front().first || replace == CoverOf(binds.front().first).group))
    {
        _captureRow.clear();
        _captureReplace.clear();
        _pendingKey.clear();
        say(tr("%1 is %2 already").arg(name, title));
        if (!_wizard.empty())
        {
            ++_wizardStep;
            wizardStep();
            return;
        }
        hidePrompt();
        refresh();
        return;
    }
    // Another row has it: one control does one thing, so it moves here or stays (the wizard moves it)
    QStringList taken;
    for (const Bound& bound : _effective)
    {
        const std::string key = RowKeyOf(bound.control, bound.spec);
        if (key.empty() || key == rowKey)
            continue;
        for (const auto& entry : binds)
            if (Overlaps(bound.control, entry.first))
            {
                const QString was = fullTitle(key);
                const QString text =
                    CoverOf(bound.control).bits == kAllDirections && CoverOf(entry.first).bits != kAllDirections
                        ? tr("%1 is part of %2").arg(ControlName(entry.first, _device.glyphs), was)
                        : tr("%1 is %2").arg(ControlName(bound.control, _device.glyphs), was);
                if (!taken.contains(text))
                    taken << text;
                break;
            }
    }
    if (!taken.isEmpty() && _wizard.empty())
    {
        _pendingBinds = binds;
        _pendingReplace = replace;
        showPrompt(
            tr("%1 now. Use it for <b>%2</b> instead?").arg(taken.join(", ").toHtmlEscaped(), title.toHtmlEscaped()),
            true, false);
        return;
    }
    apply(binds, replace);
}

void BindingTable::apply(const Binds& binds, const std::string& replace)
{
    const std::string rowKey = _captureRow;
    const QString title = fullTitle(rowKey);
    const QString name =
        ControlName(binds.size() == 1 ? binds.front().first : CoverOf(binds.front().first).group, _device.glyphs);
    // Copies: every bind re-applies the profiles, and the table reads them again
    const std::vector<Bound> effective = _effective;
    std::vector<std::string> replaced;
    if (const Row* row = findRow(rowKey))
        for (const Chip& chip : row->chips)
            if (chip.control == replace)
                replaced = chip.parts;
    std::set<std::string> controls;
    for (const auto& entry : binds)
        controls.insert(entry.first);

    bool ok = true;
    // What the new controls overlap elsewhere is hidden: within a layer dpad.* and dpad.left would both fire
    for (const Bound& bound : effective)
    {
        if (IsNone(bound.spec) || controls.count(bound.control) != 0)
            continue;
        if (std::none_of(controls.begin(), controls.end(),
                         [&bound](const std::string& control) { return Overlaps(bound.control, control); }))
            continue;
        ok = ok && bind(bound.control, None());
        // A direction taken from a whole D-pad or stick: the other directions keep moving (auto:dpad -> auto:up …)
        const Cover cover = CoverOf(bound.control);
        const std::string retro = Text(bound.spec, "retro");
        if (cover.bits != kAllDirections || retro.size() < 5 || retro.compare(retro.size() - 5, 5, ":dpad") != 0)
            continue;
        unsigned takenBits = 0;
        for (const std::string& control : controls)
            if (const Cover taken = CoverOf(control); taken.group == cover.group)
                takenBits |= taken.bits;
        for (unsigned i = 0; i < 4; ++i)
        {
            const std::string direction = DirectionControl(cover.group, kDirections[i]);
            const auto own = std::find_if(effective.begin(), effective.end(),
                                          [&direction](const Bound& b) { return b.control == direction; });
            if ((takenBits & (1u << i)) || (own != effective.end() && !IsNone(own->spec)))
                continue;
            ok = ok && bind(direction, TargetSpec("retro", retro.substr(0, retro.size() - 4) + kDirections[i]));
        }
    }
    for (const auto& [control, spec] : binds)
        ok = ok && bind(control, spec);
    for (const std::string& part : replaced)
        if (controls.count(part) == 0)
            ok = ok && bind(part, None());

    _captureRow.clear();
    _captureReplace.clear();
    _pendingKey.clear();
    _pendingBinds.clear();
    if (_wizard.empty())
        hidePrompt();
    refresh();
    if (!ok)
        return;
    // A profile above the chosen one may bind the control over it (a game's profile over the pad's)
    const Bound* now = findBound(binds.front().first);
    if (now != nullptr && now->profile != scopeProfile())
        say(tr("%1 → %2 is saved for %3, but %4 binds it above that: choose it in Save for")
                .arg(name, title, _scope->currentText(), QString::fromStdString(now->profile)),
            true);
    else
        say(tr("%1 → %2").arg(name, title));
    if (!_wizard.empty())
    {
        ++_wizardStep;
        wizardStep();
    }
}

void BindingTable::cancelCapture(const QString& why)
{
    const bool busy = !_captureRow.empty() || !_wizard.empty() || !_pendingKey.empty();
    _captureRow.clear();
    _captureReplace.clear();
    _pendingBinds.clear();
    _pendingKey.clear();
    _wizard.clear();
    _wizardStep = 0;
    hidePrompt();
    if (!why.isEmpty())
        say(why);
    if (busy)
        refresh();
}

void BindingTable::startWizard()
{
    _wizard = {"retro:up", "retro:down", "retro:left", "retro:right", "retro:fire", "retro:fire2"};
    _wizardStep = 0;
    wizardStep();
}

void BindingTable::wizardStep()
{
    if (_wizardStep >= _wizard.size())
    {
        endWizard();
        say(tr("All set"));
        return;
    }
    const std::string key = _wizard[_wizardStep];
    startCapture(key);
    showPrompt(tr("Step %1 of %2: press the control for <b>%3</b> on the pad")
                   .arg(_wizardStep + 1)
                   .arg(_wizard.size())
                   .arg(fullTitle(key).toHtmlEscaped()),
               false, true);
}

void BindingTable::endWizard()
{
    _wizard.clear();
    _wizardStep = 0;
    _captureRow.clear();
    _captureReplace.clear();
    hidePrompt();
    refresh();
}

void BindingTable::showPrompt(const QString& text, bool yes, bool wizard)
{
    _prompt->setText(text);
    _promptYes->setVisible(yes);
    _promptBack->setVisible(wizard);
    _promptBack->setEnabled(wizard && _wizardStep > 0);
    _promptSkip->setVisible(wizard);
    _prompt->parentWidget()->show();
}

void BindingTable::hidePrompt()
{
    _prompt->parentWidget()->hide();
}

void BindingTable::chipMenu(const std::string& rowKey, const Chip& chip, const QPoint& where)
{
    QMenu menu(this);
    const Chip copy = chip;
    const QString name = ControlName(copy.control, _device.glyphs);
    menu.addAction(tr("Change…"), this, [this, rowKey, copy] { startCapture(rowKey, copy.control); });
    const bool single = copy.parts.size() == 1;
    const bool pressable =
        copy.spec.Find("retro") != nullptr || copy.spec.Find("key") != nullptr || copy.spec.Find("keys") != nullptr;
    if (single && pressable)
    {
        QMenu* autofire = menu.addMenu(tr("Autofire"));
        const std::string turbo = Text(copy.spec, "turbo");
        const struct
        {
            QString name;
            int frames;
        } speeds[] = {{tr("Off"), 0}, {tr("Slow"), 4}, {tr("Normal"), 2}, {tr("Fast"), 1}};
        for (const auto& speed : speeds)
        {
            QAction* action = autofire->addAction(speed.name);
            action->setCheckable(true);
            action->setChecked(speed.frames == 0 ? turbo.empty() || turbo == "off"
                                                 : turbo == "classic" && Number(copy.spec, "on") == speed.frames);
            const int frames = speed.frames;
            connect(action, &QAction::triggered, this, [this, copy, frames, name] {
                JsonValue spec = copy.spec;
                for (const char* field : {"turbo", "on", "off"})
                    spec.Remove(field);
                if (frames > 0)
                {
                    spec.Set("turbo", JsonValue::String("classic"));
                    spec.Set("on", JsonValue::Number(frames));
                    spec.Set("off", JsonValue::Number(frames));
                }
                if (bind(copy.control, spec))
                {
                    refresh();
                    say(frames > 0 ? tr("%1 fires by itself while held").arg(name) : tr("%1: autofire off").arg(name));
                }
            });
        }
        QAction* tap = menu.addAction(tr("Short tap per press"));
        tap->setToolTip(tr("One short press however long it is held (for menus)"));
        tap->setCheckable(true);
        tap->setChecked(Text(copy.spec, "press") == "tap");
        connect(tap, &QAction::triggered, this, [this, copy](bool on) {
            JsonValue spec = copy.spec;
            spec.Remove("press");
            if (on)
                spec.Set("press", JsonValue::String("tap"));
            if (bind(copy.control, spec))
                refresh();
        });
    }
    menu.addSeparator();
    menu.addAction(tr("Remove"), this, [this, copy, name, rowKey] {
        bool ok = true;
        for (const std::string& part : copy.parts)
            ok = ok && bind(part, None());
        const QString title = fullTitle(rowKey);
        refresh();
        if (ok)
            say(tr("%1 does nothing now (was %2)").arg(name, title));
    });
    QAction* restore = menu.addAction(tr("Restore default"), this, [this, copy] {
        bool ok = true;
        for (const std::string& part : copy.parts)
            ok = ok && unbind(part);
        refresh();
        if (ok)
            say(tr("%1 is back to its default").arg(ControlName(copy.control, _device.glyphs)));
    });
    restore->setEnabled(copy.profile == scopeProfile());
    if (single)
        menu.addAction(tr("Edit as text…"), this, [this, copy, name] {
            bool ok = false;
            const QString text =
                QInputDialog::getText(this, tr("Edit the binding"), tr("%1, as the profile file holds it:").arg(name),
                                      QLineEdit::Normal, QString::fromStdString(DumpYaml(copy.spec, 0)).trimmed(), &ok);
            if (!ok)
                return;
            JsonValue spec;
            std::string error;
            if (!ParseYaml(text.toStdString(), spec, error) || !spec.IsObject())
            {
                say(error.empty() ? tr("Not a binding: { key: Q }, { retro: \"auto:fire\" }")
                                  : QString::fromStdString(error),
                    true);
                return;
            }
            if (bind(copy.control, spec))
                refresh();
        });
    menu.exec(where);
}

bool BindingTable::bind(const std::string& control, const JsonValue& spec)
{
    HostRequest request{"bind", scopeOptions()};
    request.options["control"] = control;
    request.options["layer"] = layer();
    request.options["spec"] = DumpYaml(spec, 0);
    const HostReply reply = _host->Host(request);
    if (!reply.Ok())
        say(QString::fromStdString(reply.message), true);
    return reply.Ok();
}

bool BindingTable::unbind(const std::string& control)
{
    HostRequest request{"unbind", scopeOptions()};
    request.options["control"] = control;
    request.options["layer"] = layer();
    const HostReply reply = _host->Host(request);
    if (!reply.Ok())
        say(QString::fromStdString(reply.message), true);
    return reply.Ok();
}

void BindingTable::restoreDefaults()
{
    const Profile* own = _host->Profiles().Find(scopeProfile());
    std::vector<std::string> controls;
    bool scheme = false;
    if (own != nullptr && own->scope == ProfileScope::User)
    {
        for (const auto& [name, members] : own->layers)
            if (name == layer())
                for (const auto& [control, spec] : members)
                    controls.push_back(control);
        scheme = layer() == "play" && !own->extends.empty();
    }
    if (controls.empty() && !scheme)
    {
        say(tr("Nothing of yours to restore for %1").arg(_scope->currentText()));
        return;
    }
    const int count = static_cast<int>(controls.size()) + (scheme ? 1 : 0);
    if (QMessageBox::question(
            this, tr("Restore defaults"),
            tr("Remove your %n change(s) in this tab for %1?", nullptr, count).arg(_scope->currentText())) !=
        QMessageBox::Yes)
        return;
    cancelCapture();
    bool ok = true;
    for (const std::string& control : controls)
        ok = ok && unbind(control);
    if (ok && scheme)
    {
        HostRequest request{"scheme", scopeOptions()};
        request.options["id"] = "none";
        const HostReply reply = _host->Host(request);
        if (!reply.Ok())
            say(QString::fromStdString(reply.message), true);
        ok = reply.Ok();
    }
    refresh();
    if (ok)
        say(tr("Defaults restored for %1").arg(_scope->currentText()));
}

void BindingTable::schemeChosen()
{
    const QString id = _scheme->currentData().toString();
    HostRequest request{"scheme", scopeOptions()};
    request.options["id"] = id.isEmpty() ? "none" : id.toStdString();
    const HostReply reply = _host->Host(request);
    const QString text = _scheme->currentText();
    refresh();
    if (reply.Ok())
        say(tr("Works as %1 for %2").arg(text, _scope->currentText()));
    else
        say(QString::fromStdString(reply.message), true);
}

void BindingTable::addKey()
{
    QDialog picker(this);
    picker.setWindowTitle(tr("Add a ZX key"));
    auto* layout = new QVBoxLayout(&picker);
    layout->addWidget(new QLabel(tr("The key the pad presses:"), &picker));
    auto* caps = new QCheckBox(tr("with Caps Shift"), &picker);
    auto* sym = new QCheckBox(tr("with Symbol Shift"), &picker);
    std::string chosen;
    for (const auto& keys : kZxRows)
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(3);
        for (const char* key : keys)
        {
            auto* button = new QPushButton(KeyTitle(key), &picker);
            button->setMinimumWidth(std::string(key).size() > 1 ? 72 : 34);
            connect(button, &QPushButton::clicked, &picker, [&chosen, &picker, caps, sym, key] {
                const std::string name = key;
                chosen = (caps->isChecked() && name != "CAPS" ? "CAPS+" : "") +
                         std::string(sym->isChecked() && name != "SYM" ? "SYM+" : "") + name;
                picker.accept();
            });
            row->addWidget(button);
        }
        layout->addLayout(row);
    }
    auto* modifiers = new QHBoxLayout();
    modifiers->addWidget(caps);
    modifiers->addWidget(sym);
    modifiers->addStretch(1);
    layout->addLayout(modifiers);
    if (picker.exec() != QDialog::Accepted || chosen.empty())
        return;
    _pendingKey = chosen;
    refresh();
    startCapture("key:" + Upper(chosen));
}

void BindingTable::showControl(const std::string& control)
{
    for (const auto& [button, chip] : _chipButtons)
        for (const std::string& part : chip.parts)
        {
            const std::vector<std::string> spots = HotspotsOf(part);
            if (!Overlaps(part, control) && std::find(spots.begin(), spots.end(), control) == spots.end())
                continue;
            reveal(button);
            button->setFocus();
            say(tr("%1 is %2")
                    .arg(ControlName(part, _device.glyphs),
                         fullTitle(button->property("row").toString().toStdString())));
            return;
        }
    say(tr("%1 does nothing here: click + on a row, then press it").arg(ControlName(control, _device.glyphs)));
}

void BindingTable::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && (!_captureRow.empty() || !_wizard.empty()))
    {
        cancelCapture(tr("Cancelled: nothing changed"));
        return;
    }
    // The keyboard binds the key pressed here, with its modifiers (`key.ctrl.f10`)
    if (!_captureRow.empty() && _pendingBinds.empty() && _device.family == "keyboard" && !event->isAutoRepeat())
    {
        const std::string control = HostControllers::KeyControlOf(event);
        if (!control.empty())
        {
            captured(control, 1);
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

bool BindingTable::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Enter)
        emit controlHovered(watched->property("hotspot").toString().toStdString());
    else if (event->type() == QEvent::Leave)
        emit controlHovered({});
    return QWidget::eventFilter(watched, event);
}

#else

BindingTable::BindingTable(HostControllers* host, QWidget* parent) : QWidget(parent), _host(host) {}

QString BindingTable::ControlName(const std::string& control, const std::string&)
{
    return QString::fromStdString(control);
}

void BindingTable::setDevice(const Device& device)
{
    _device = device;
}

void BindingTable::refresh() {}

void BindingTable::setLive(const std::map<std::string, float>& live)
{
    _live = live;
}

void BindingTable::showControl(const std::string&) {}

void BindingTable::setDebugTab(bool) {}

void BindingTable::keyPressEvent(QKeyEvent* event)
{
    QWidget::keyPressEvent(event);
}

bool BindingTable::eventFilter(QObject* watched, QEvent* event)
{
    return QWidget::eventFilter(watched, event);
}

#endif
