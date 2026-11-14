#include "handheldmenu.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>

#include "emulator/actions/actionregistry.h"
#include "emulator/actions/readouts.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/tape/tape.h"
#include "emulator/media/mediamanager.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/windowview.h"
#include "widgets/crtprofiles.h"

namespace
{
// Visual tokens (design §6), sampled from the SteamOS settings page
const QColor kBackground(0x1a, 0x1c, 0x21);
const QColor kSidebar(0x2b, 0x2d, 0x33);
const QColor kRow(0x23, 0x26, 0x2e);
const QColor kControl(0x44, 0x46, 0x4d);
const QColor kAccent(0x1a, 0x9f, 0xff);
const QColor kSelectedFrom(0x24, 0x58, 0x80);
const QColor kSelectedTo(0x29, 0x34, 0x41);
const QColor kFooter(0x0e, 0x0f, 0x12);
const QColor kDivider(0x41, 0x42, 0x48);
const QColor kText(0xdc, 0xde, 0xdf);
const QColor kDim(0x8b, 0x92, 0x9a);

constexpr qreal kDesignWidth = 1280;
constexpr qreal kDesignHeight = 800;
constexpr qreal kRowHeight = 64;
constexpr qreal kHeaderHeight = 44;
constexpr qreal kSliderHeight = 92;
constexpr int kSlots = 9;
constexpr int kSlotColumns = 3;
constexpr int kBrowserRows = 10;
const int kSpeeds[] = {1, 2, 4, 8, 16};

QString SlotTime(qint64 time)
{
    if (time <= 0)
        return {};
    return QDateTime::fromSecsSinceEpoch(time).toString(QStringLiteral("dd MMM, HH:mm"));
}

// region <Readout fields>

const StateNode* Field(const StateNode& node, const char* key)
{
    return node.isObject() ? node.find(key) : nullptr;
}

QString Text(const StateNode& node, const char* key)
{
    const StateNode* field = Field(node, key);
    return field != nullptr && field->kind == StateNode::Kind::String ? QString::fromStdString(field->s) : QString();
}

bool Flag(const StateNode& node, const char* key)
{
    const StateNode* field = Field(node, key);
    return field != nullptr && field->kind == StateNode::Kind::Bool && field->b;
}

double Number(const StateNode& node, const char* key)
{
    const StateNode* field = Field(node, key);
    if (field == nullptr)
        return 0;
    if (field->kind == StateNode::Kind::Double)
        return field->d;
    if (field->kind == StateNode::Kind::Int)
        return static_cast<double>(field->i);
    return 0;
}

const std::vector<StateNode>& Items(const StateNode& node, const char* key)
{
    static const std::vector<StateNode> none;
    const StateNode* field = Field(node, key);
    return field != nullptr && field->isArray() ? field->items : none;
}

// endregion </Readout fields>

QString OnOff(bool on)
{
    return on ? QCoreApplication::translate("HandheldMenu", "On") : QCoreApplication::translate("HandheldMenu", "Off");
}

QString Percent(double value)
{
    return QStringLiteral("%1%").arg(static_cast<int>(std::lround(value * 100)));
}

/// A CRT profile as people call it: the built-in models by their names ("CRT Lottes"), a shader by its file name
/// in words ("shader:guest-pal" -> "Guest PAL (shader)")
QString CrtProfileTitle(const std::string& key)
{
    const bool shader = key.rfind("shader:", 0) == 0;
    if (!shader)
        for (CRTProfile profile : CRTProfileParams::AllProfiles())
            if (CRTProfileParams::ProfileKey(profile).toStdString() == key)
                return CRTProfileParams::ProfileName(profile);
    static const QStringList upper = {QStringLiteral("crt"), QStringLiteral("pal"), QStringLiteral("pvm"),
                                      QStringLiteral("rgb"), QStringLiteral("sdr"), QStringLiteral("gdv")};
    QStringList words = QString::fromStdString(shader ? key.substr(7) : key).split(QLatin1Char('-'), Qt::SkipEmptyParts);
    for (QString& word : words)
        word = upper.contains(word) ? word.toUpper() : word.left(1).toUpper() + word.mid(1);
    const QString title = words.join(QLatin1Char(' '));
    return shader ? QCoreApplication::translate("HandheldMenu", "%1 (shader)").arg(title) : title;
}

/// The overscan crops (video.viewport) in words
QString ViewportTitle(const QString& viewport)
{
    if (viewport == QStringLiteral("full"))
        return QCoreApplication::translate("HandheldMenu", "Full overscan");
    if (viewport == QStringLiteral("symmetric"))
        return QCoreApplication::translate("HandheldMenu", "Centred");
    if (viewport == QStringLiteral("standard"))
        return QCoreApplication::translate("HandheldMenu", "Standard");
    if (viewport == QStringLiteral("screen_only"))
        return QCoreApplication::translate("HandheldMenu", "No border");
    return viewport;
}

/// "brightness_boost" -> "Brightness boost"
QString Readable(const std::string& name)
{
    QString text = QString::fromStdString(name).replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!text.isEmpty())
        text[0] = text[0].toUpper();
    return text;
}

QString MediumIcon(MediaKind kind)
{
    switch (kind)
    {
        case MediaKind::Tape: return QStringLiteral("tape");
        case MediaKind::Floppy: return QStringLiteral("disk");
        case MediaKind::Block: return QStringLiteral("memory");
        case MediaKind::Optical: return QStringLiteral("media");
    }
    return QStringLiteral("media");
}

QString BrowserIcon(HandheldBrowser::Kind kind, const QString& name)
{
    switch (kind)
    {
        case HandheldBrowser::Kind::Root:
            return name == QCoreApplication::translate("HandheldBrowser", "Recent") ? QStringLiteral("recent")
                                                                                     : QStringLiteral("folder");
        case HandheldBrowser::Kind::Folder: return QStringLiteral("folder");
        case HandheldBrowser::Kind::Up: return QStringLiteral("folder-up");
        case HandheldBrowser::Kind::Snapshot: return QStringLiteral("snapshot");
        case HandheldBrowser::Kind::Tape: return QStringLiteral("tape");
        case HandheldBrowser::Kind::Disk: return QStringLiteral("disk");
        case HandheldBrowser::Kind::File: return QStringLiteral("file");
    }
    return QStringLiteral("file");
}
}  // namespace

HandheldMenu::HandheldMenu(Hooks hooks, QWidget* parent) : QWidget(parent), _hooks(std::move(hooks))
{
    _sections = {
        {Page::Slots, QStringLiteral("slots"), tr("Save & Load")},
        {Page::Media, QStringLiteral("media"), tr("Media")},
        {Page::Machine, QStringLiteral("machine"), tr("Machine")},
        {Page::Cards, QStringLiteral("card"), tr("Expansion")},
        {Page::Display, QStringLiteral("display"), tr("Display")},
        {Page::Sound, QStringLiteral("sound"), tr("Sound")},
        {Page::Network, QStringLiteral("network"), tr("Network")},
        {Page::Controls, QStringLiteral("controls"), tr("Controls")},
        {Page::Quit, QStringLiteral("quit"), tr("Quit")},
    };
    setFocusPolicy(Qt::StrongFocus);
    // The touchscreen taps what it touches (no pointer: the cursor is hidden over the menu)
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setCursor(Qt::BlankCursor);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    if (parent != nullptr)
        parent->installEventFilter(this);  // the menu covers its parent at any size
    // The live values (the tape's block, the network counters) follow while the menu is open
    _live = new QTimer(this);
    _live->setInterval(500);
    connect(_live, &QTimer::timeout, this, [this] {
        const Page shown = page();
        if (shown == Page::Media || shown == Page::Network || shown == Page::Sound)
            update();
    });
    hide();
}

bool HandheldMenu::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && _open)
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(watched, event);
}

void HandheldMenu::Open()
{
    if (_open)
        return;
    _open = true;
    _inPage = false;
    _row = 0;
    _scroll = 0;
    _hasPicker = false;
    _hasBrowser = false;
    _status.clear();
    _background = _hooks.grabPicture ? _hooks.grabPicture() : QImage();
    // C-6: the machine waits while the menu is open; a machine paused before stays paused after
    Emulator* emulator = _hooks.emulator ? _hooks.emulator() : nullptr;
    _pausedByMenu = emulator != nullptr && !emulator->IsPaused();
    if (_pausedByMenu)
        submit("run.pause", StateNode::Object());
    if (_hooks.showScreen)
        _hooks.showScreen(false);
    refreshSlots();
    if (parentWidget() != nullptr)
        setGeometry(parentWidget()->rect());
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    _live->start();
    emitState();
}

void HandheldMenu::Close()
{
    if (!_open)
        return;
    _open = false;
    _hasPicker = false;
    _hasBrowser = false;
    _live->stop();
    hide();
    if (_hooks.showScreen)
        _hooks.showScreen(true);
    if (_pausedByMenu)
    {
        // The machine the menu paused runs on: a refused resume (another client holding the run control) is retried
        // once, and said, so the game never stays frozen behind a closed menu
        const QPointer<HandheldMenu> self(this);
        submit("run.resume", StateNode::Object(), [self](bool ok, const QString&, const StateNode&) {
            if (!ok && self)
                QTimer::singleShot(300, self, [self] {
                    if (self && !self->_open)
                        self->submit("run.resume", StateNode::Object());
                });
        });
    }
    _pausedByMenu = false;
    _background = QImage();
    emitState();
}

// region <Navigation>

void HandheldMenu::moveRow(int step)
{
    const std::vector<Row> list = rows();
    const int count = static_cast<int>(list.size());
    if (count == 0)
        return;
    int row = _row;
    for (int n = 0; n < count; ++n)
    {
        const int next = row + step;
        if (next < 0 || next >= count)
            break;
        row = next;
        if (!list[static_cast<size_t>(row)].header)
        {
            _row = row;
            return;
        }
    }
}

void HandheldMenu::Nav(int dx, int dy)
{
    if (!_open)
        return;
    if (_hasBrowser)
    {
        const int count = static_cast<int>(_browser.entries().size());
        if (count > 0)
            _browserRow = std::clamp(_browserRow + dy, 0, count - 1);
        emitState();
        return;
    }
    if (_hasPicker)
    {
        const int count = static_cast<int>(_picker.items.size());
        if (count > 0)
            _picker.row = std::clamp(_picker.row + dy, 0, count - 1);
        emitState();
        return;
    }
    if (!_inPage)
    {
        if (dy != 0)
        {
            _section = std::clamp(_section + dy, 0, static_cast<int>(_sections.size()) - 1);
            _row = 0;
            _scroll = 0;
            if (page() == Page::Slots)
                refreshSlots();
        }
        if (dx > 0)
            _inPage = page() != Page::Network;  // the Network page is a monitor, nothing to focus
        clampFocus();
        emitState();
        return;
    }
    if (page() == Page::Slots)
    {
        const int column = _row % kSlotColumns;
        if (dx < 0 && column == 0)
            _inPage = false;
        else
            _row = std::clamp(_row + dx + dy * kSlotColumns, 0, kSlots - 1);
        emitState();
        return;
    }
    if (dx != 0)
    {
        const std::vector<Row> list = rows();
        const bool adjustable = _row >= 0 && _row < static_cast<int>(list.size()) && list[static_cast<size_t>(_row)].adjust;
        if (adjustable)
            list[static_cast<size_t>(_row)].adjust(dx);
        else if (dx < 0)
            _inPage = false;
    }
    if (dy != 0)
        moveRow(dy);
    clampFocus();
    emitState();
}

void HandheldMenu::Accept()
{
    if (!_open)
        return;
    if (_hasBrowser)
    {
        const QString path = _browser.Activate(_browserRow);
        if (path.isEmpty())
            _browserRow = _browser.focusAfterMove();
        else
        {
            _hasBrowser = false;
            if (_browserChosen)
                _browserChosen(path);
        }
        emitState();
        return;
    }
    if (_hasPicker)
    {
        Picker picker = std::move(_picker);
        _hasPicker = false;
        if (picker.chosen && picker.row >= 0 && picker.row < static_cast<int>(picker.items.size()))
            picker.chosen(picker.row);
        emitState();
        return;
    }
    if (!_inPage)
    {
        _inPage = page() != Page::Network;
        _row = 0;
        clampFocus();
        emitState();
        return;
    }
    if (page() == Page::Slots)
    {
        quickSlot("load", _row + 1);
        return;
    }
    const std::vector<Row> list = rows();
    if (_row >= 0 && _row < static_cast<int>(list.size()))
    {
        const Row& row = list[static_cast<size_t>(_row)];
        if (row.activate)
            row.activate();
        else if (row.adjust)
            row.adjust(1);  // A on a value row steps it on (a switch flips, a choice cycles)
    }
    emitState();
}

void HandheldMenu::Back()
{
    if (!_open)
        return;
    if (_hasBrowser)
    {
        if (_browser.Up())
            _browserRow = _browser.focusAfterMove();
        else
            _hasBrowser = false;
    }
    else if (_hasPicker)
        _hasPicker = false;
    else if (_inPage)
        _inPage = false;
    else
    {
        Close();
        return;
    }
    emitState();
}

void HandheldMenu::Option(char button)
{
    if (!_open || _hasPicker || _hasBrowser || !_inPage)
        return;
    if (page() != Page::Slots)
        return;
    if (button == 'x')
        quickSlot("save", _row + 1);
    else if (button == 'y')
        quickSlot("delete", _row + 1);
}

void HandheldMenu::Section(int step)
{
    if (!_open)
        return;
    if (_hasBrowser)
    {
        const int count = static_cast<int>(_browser.entries().size());
        if (count > 0)
            _browserRow = std::clamp(_browserRow + step * (kBrowserRows - 2), 0, count - 1);
        emitState();
        return;
    }
    if (_hasPicker)
    {
        // A page up / down in a picker
        const int count = static_cast<int>(_picker.items.size());
        if (count > 0)
            _picker.row = std::clamp(_picker.row + step * 6, 0, count - 1);
        emitState();
        return;
    }
    const int count = static_cast<int>(_sections.size());
    _section = (_section + step + count) % count;
    _row = 0;
    _scroll = 0;
    if (page() == Page::Slots)
        refreshSlots();
    clampFocus();
    emitState();
}

void HandheldMenu::clampFocus()
{
    if (page() == Page::Slots)
    {
        _row = std::clamp(_row, 0, kSlots - 1);
        return;
    }
    const std::vector<Row> list = rows();
    const int count = static_cast<int>(list.size());
    _row = count > 0 ? std::clamp(_row, 0, count - 1) : 0;
    // Never on a caption: the next row down, else the one up
    if (count > 0 && list[static_cast<size_t>(_row)].header)
    {
        const int from = _row;
        moveRow(1);
        if (_row == from)
            moveRow(-1);
    }
}

// endregion </Navigation>

// region <Pages>

std::vector<HandheldMenu::Row> HandheldMenu::rows() const
{
    std::vector<Row> list;
    switch (page())
    {
        case Page::Slots: slotRows(list); break;
        case Page::Media: mediaRows(list); break;
        case Page::Machine: machineRows(list); break;
        case Page::Cards: cardRows(list); break;
        case Page::Display: displayRows(list); break;
        case Page::Sound: soundRows(list); break;
        case Page::Network: networkRows(list); break;
        case Page::Controls: controlRows(list); break;
        case Page::Quit: quitRows(list); break;
    }
    return list;
}

void HandheldMenu::slotRows(std::vector<Row>&) const
{
    // The slots page is a grid of pictures (paintSlots), not rows
}

void HandheldMenu::mediaRows(std::vector<Row>& list) const
{
    auto* self = const_cast<HandheldMenu*>(this);
    list.push_back({QStringLiteral("file"), tr("Open file…"), tr("snapshot, tape or disk"), true, [self] {
                        self->openBrowser(HandheldBrowser::Filter::Any, tr("Open"),
                                          [self](const QString& path) { self->openFile(path); });
                    }});
    list.push_back({QStringLiteral("snapshot"), tr("Save snapshot to a file"), tr("Saves folder"), false,
                    [self] { self->saveSnapshot(); }});
    // The media manager: every medium slot the machine has, with what is in it
    MediaManager* media = machineContext() != nullptr ? machineContext()->pMediaManager : nullptr;
    if (media != nullptr)
    {
        list.push_back({{}, tr("Drives"), {}, false, {}, {}, true});
        const StateNode tape = readout("media.tape");
        for (const SlotInfo& slot : media->List())
        {
            if (slot.detached)
                continue;
            const SlotDescriptor& d = slot.descriptor;
            QString value = slot.present ? QFileInfo(QString::fromStdString(slot.source)).fileName() : tr("Empty");
            if (slot.present && d.kind == MediaKind::Tape)
            {
                const int blocks = static_cast<int>(Number(tape, "blocks"));
                value = QStringLiteral("%1 %2  %3/%4")
                            .arg(Flag(tape, "playing") ? QStringLiteral("▶") : QStringLiteral("■"), value)
                            .arg(static_cast<int>(Number(tape, "block")) + (blocks > 0 ? 1 : 0))
                            .arg(blocks);
            }
            if (slot.dirty)
                value += QStringLiteral(" *");
            if (slot.writeProtect && slot.present)
                value += tr(" (protected)");
            const QString label = d.label.empty() ? QString::fromStdString(d.id) : QString::fromStdString(d.label);
            const std::string id = d.id;
            const int kind = static_cast<int>(d.kind);
            list.push_back({MediumIcon(d.kind), label, value, true, [self, id, kind] { self->openMediumActions(id, kind); }});
        }
    }
    list.push_back({{}, tr("Loading"), {}, false, {}, {}, true});
    for (const auto& [name, label] : {std::pair{"fast_tape", "Fast tape loading"}, std::pair{"turbo_tape", "Turbo tape loading"},
                                      std::pair{"fast_disk", "Fast disk loading"}})
    {
        StateNode params = StateNode::Object();
        params["name"] = StateNode(std::string(name));
        const std::optional<bool> on = toggleOn("set.toggle", params);
        const std::string setting = name;
        auto flip = [self, setting](int) {
            StateNode p = StateNode::Object();
            p["name"] = StateNode(setting);
            self->submit("set.toggle", std::move(p));
        };
        list.push_back({QStringLiteral("turbo"), tr(label), on ? OnOff(*on) : QString(), false, {}, flip});
    }
}

void HandheldMenu::machineRows(std::vector<Row>& list) const
{
    auto* self = const_cast<HandheldMenu*>(this);
    Emulator* emulator = _hooks.emulator ? _hooks.emulator() : nullptr;
    QString model;
    QString speed;
    bool turbo = false;
    if (emulator != nullptr)
    {
        const CONFIG& config = machineContext()->config;
        for (const TMemModel& m : EmulatorManager::GetInstance()->GetAvailableModels())
            if (m.Model == config.mem_model)
                model = QStringLiteral("%1 %2K").arg(QString::fromUtf8(m.FullName)).arg(config.ramsize);
        speed = QStringLiteral("%1×").arg(static_cast<int>(emulator->GetSpeedMultiplier()));
        turbo = emulator->IsTurboMode();
    }
    // Pause / continue: the machine waits while the menu is open (C-6); this says what it does once the menu closes.
    // Either way back to the machine at once: a paused one stays on its frozen picture until Continue here
    if (emulator != nullptr)
    {
        if (_pausedByMenu)
            list.push_back({QStringLiteral("pause"), tr("Pause"), {}, false, [self] {
                                self->_pausedByMenu = false;  // the menu's own pause stays after it closes
                                self->Close();
                            }});
        else
            list.push_back({QStringLiteral("play"), tr("Continue"), tr("paused"), false, [self] {
                                self->_pausedByMenu = true;  // closing resumes it (retried once if refused)
                                self->Close();
                            }});
    }
    list.push_back({QStringLiteral("machine"), tr("Model"), model, true, [self] { self->openModelPicker(); }});
    list.push_back({QStringLiteral("speed"), tr("Speed"), speed, true, [self] { self->openSpeedPicker(); }});
    list.push_back({QStringLiteral("turbo"), tr("Turbo"), OnOff(turbo), false, {},
                    [self](int) { self->submit("mach.turbo_toggle", StateNode::Object()); }});
    list.push_back({QStringLiteral("reset"), tr("Reset"), {}, false, [self] {
                        StateNode params = StateNode::Object();
                        params["now"] = StateNode(true);
                        self->submit("mach.reset", std::move(params));
                        // A reset is for the machine: back to it at once, not to the menu
                        self->Close();
                    }});
    list.push_back({QStringLiteral("power"), tr("Hard reset"), {}, false, [self] {
                        self->submit("mach.cold_reset", StateNode::Object());
                        self->Close();
                    }});
    list.push_back({QStringLiteral("nmi"), tr("NMI"), {}, false, [self] { self->submit("mach.nmi", StateNode::Object()); }});
    // The front-panel switches the machine has (the Profi's TURBO and CP/M)
    const StateNode switches = readout("machine.switches");
    for (const StateNode& sw : Items(switches, "switches"))
    {
        const std::string name = Text(sw, "name").toStdString();
        if (name.empty())
            continue;
        list.push_back({QStringLiteral("power"), tr("%1 switch").arg(QString::fromStdString(name).toUpper()),
                        OnOff(Flag(sw, "on")), false, {}, [self, name](int) {
                            StateNode params = StateNode::Object();
                            params["name"] = StateNode(name);
                            self->submit("mach.switch", std::move(params));
                        }});
    }
}

void HandheldMenu::cardRows(std::vector<Row>& list) const
{
    auto* self = const_cast<HandheldMenu*>(this);
    Emulator* emulator = _hooks.emulator ? _hooks.emulator() : nullptr;
    if (emulator == nullptr)
        return;
    const StateNode report = DeviceState::Slots(machineContext());
    if (!Text(report, "board").isEmpty())
        list.push_back({{}, Text(report, "board"), {}, false, {}, {}, true});
    // The slots with their cards; a slot the machine declares ([ISA] SlotN) shows its card too
    std::vector<std::string> shown;
    for (const StateNode& slot : Items(report, "slots"))
    {
        const std::string id = Text(slot, "slot").toStdString();
        shown.push_back(id);
        QString value = Text(slot, "name");
        if (!Text(slot, "options").isEmpty())
            value += QStringLiteral(" (%1)").arg(Text(slot, "options"));
        list.push_back({QStringLiteral("card"), QString::fromStdString(id), value, true,
                        [self, id] { self->openCardPicker(id); }});
    }
    for (const StateNode& slot : Items(report, "machineSlots"))
    {
        const std::string id = Text(slot, "slot").toStdString();
        if (std::find(shown.begin(), shown.end(), id) != shown.end())
            continue;
        QString value = Text(slot, "name");
        if (!Text(slot, "details").isEmpty())
            value += QStringLiteral(" (%1)").arg(Text(slot, "details"));
        list.push_back({QStringLiteral("card"), QString::fromStdString(id), value, false, {}});
    }
    list.push_back({QStringLiteral("card"), tr("Add a card…"), {}, true, [self] { self->openCardPicker({}); }});
    const std::vector<StateNode>& builtIns = Items(report, "builtIns");
    if (!builtIns.empty())
    {
        list.push_back({{}, tr("Built in"), {}, false, {}, {}, true});
        for (const StateNode& item : builtIns)
            list.push_back({QStringLiteral("memory"), Text(item, "name"), Text(item, "state"), false, {}});
    }
}

void HandheldMenu::displayRows(std::vector<Row>& list) const
{
    auto* self = const_cast<HandheldMenu*>(this);
    const std::optional<WindowDisplaySettings> settings = WindowView::PublishedSettings();
    if (!settings)
    {
        list.push_back({QStringLiteral("info"), tr("No window"), {}, false, {}});
        return;
    }
    auto press = [self](const char* id) { return [self, id](int) { self->submit(id, StateNode::Object()); }; };

    // A choice among several opens a popup (the CRT look, the temporal effect, the border); on / off stays a switch
    const bool crtOn = settings->crt && settings->crtProfile != "none";
    list.push_back({QStringLiteral("crt"), tr("CRT effect"), crtOn ? CrtProfileTitle(settings->crtProfile) : OnOff(false),
                    true, [self] { self->openCrtPicker(); }});

    const StateNode temporal = readout("video.temporal");
    const bool temporalOn = Flag(temporal, "on");
    const bool dlss = Text(temporal, "effect") == QStringLiteral("dlss");
    list.push_back({QStringLiteral("display"), tr("Temporal effect"),
                    !temporalOn ? OnOff(false) : dlss ? tr("ZX DLSS de-flicker") : tr("Frame blending"), true,
                    [self] { self->openTemporalPicker(); }});
    if (temporalOn && !dlss)
    {
        const int frames = settings->blendingFrames;
        list.push_back({QStringLiteral("settings"), tr("Frames blended"), QString::number(frames), false, {},
                        [self, frames](int step) {
                            StateNode params = StateNode::Object();
                            params["frames"] = StateNode(static_cast<int64_t>(std::clamp(frames + step, 2, 5)));
                            self->submit("video.blending", std::move(params));
                        }});
        const bool exponential = settings->blendingWeights == "exponential";
        list.push_back({QStringLiteral("settings"), tr("Weights"), exponential ? tr("Newest frames stronger") : tr("Equal"),
                        true, [self, exponential] {
                            auto weights = [self](const char* word) {
                                return [self, word] {
                                    StateNode params = StateNode::Object();
                                    params["weights"] = StateNode(std::string(word));
                                    self->submit("video.blending", std::move(params));
                                };
                            };
                            self->openActions(tr("Weights"), {{tr("Equal"), weights("equal")},
                                                              {tr("Newest frames stronger"), weights("exponential")}});
                            self->_picker.row = exponential ? 1 : 0;
                        }});
    }

    const StateNode viewport = readout("video.viewport");
    if (Flag(viewport, "overscan_available"))
    {
        // Overscan (the Pentagon's larger frame) and its crop; the window view, not this, hides the whole border
        list.push_back({QStringLiteral("display"), tr("Overscan"), OnOff(Flag(viewport, "overscan")), false, {},
                        press("video.overscan_toggle")});
        if (Flag(viewport, "overscan"))
            list.push_back({QStringLiteral("display"), tr("Border"), ViewportTitle(Text(viewport, "viewport")), true,
                            [self] { self->openBorderPicker(); }});
    }
    list.push_back({QStringLiteral("hud"), tr("HUD"), OnOff(settings->hud), false, {}, press("hud.toggle")});
    if (settings->gpuAvailable)
        list.push_back({QStringLiteral("display"), tr("GPU acceleration"), OnOff(settings->gpu), false, {},
                        press("video.gpu_toggle")});
}

void HandheldMenu::soundRows(std::vector<Row>& list) const
{
    // A mixer: the master and every source as a slider with its live peak (◀▶ the volume, A mutes)
    auto* self = const_cast<HandheldMenu*>(this);
    const bool allMuted = Flag(readout("audio.mute"), "muted");
    const StateNode mixer = readout("audio.mixer");
    const std::vector<StateNode>& devices = Items(mixer, "devices");
    double loudest = 0;
    double peak = 0;
    for (const StateNode& device : devices)
    {
        loudest = std::max(loudest, Number(device, "volume"));
        peak = std::max(peak, Number(device, "peak"));
    }
    Row master{allMuted ? QStringLiteral("volume-off") : QStringLiteral("sound"), tr("Volume"),
               allMuted ? tr("Muted") : Percent(loudest), false,
               [self] { self->submit("audio.mute_toggle", StateNode::Object()); },
               [self](int step) { self->submitDelta("audio.volume_by", StateNode::Object(), step); }};
    master.level = std::clamp(loudest, 0.0, 1.0);
    master.peak = peak;
    master.muted = allMuted;
    list.push_back(std::move(master));
    if (devices.empty())
        return;
    list.push_back({{}, tr("Sources"), {}, false, {}, {}, true});
    for (const StateNode& device : devices)
    {
        const std::string source = Text(device, "source").toStdString();
        if (source.empty() || source == "master")
            continue;
        const bool muted = Flag(device, "muted");
        const QString name = Text(device, "name");
        Row row{muted ? QStringLiteral("volume-off") : QStringLiteral("sound"),
                name.isEmpty() ? QString::fromStdString(source) : name, muted ? tr("Muted") : Percent(Number(device, "volume")),
                false,
                [self, source] {
                    StateNode params = StateNode::Object();
                    params["source"] = StateNode(source);
                    self->submit("audio.source_mute", std::move(params));
                },
                [self, source](int step) {
                    StateNode params = StateNode::Object();
                    params["source"] = StateNode(source);
                    self->submitDelta("audio.volume_by", std::move(params), step);
                }};
        row.level = std::clamp(Number(device, "volume"), 0.0, 1.0);
        row.peak = Number(device, "peak");
        row.muted = muted || allMuted;
        list.push_back(std::move(row));
    }
}

void HandheldMenu::networkRows(std::vector<Row>&) const
{
    // A monitor, nothing to focus: paintNetwork draws the page from DeviceState::Network
}

void HandheldMenu::controlRows(std::vector<Row>& list) const
{
    auto* self = const_cast<HandheldMenu*>(this);
    QString keyboard;
    if (_hooks.keyboards && _hooks.keyboard)
    {
        const std::string forced = _hooks.keyboard();
        for (const auto& [id, name] : _hooks.keyboards())
            if (id == forced)
                keyboard = name;
    }
    list.push_back({QStringLiteral("keyboard"), tr("Show the keyboard"), tr("View"), false, [self] {
                        self->Close();
                        if (self->_hooks.showKeyboard)
                            self->_hooks.showKeyboard();
                    }});
    list.push_back({QStringLiteral("keyboard"), tr("Keyboard"), keyboard, true, [self] { self->openKeyboardPicker(); }});
    if (_hooks.driveHaptics && _hooks.setDriveHaptics)
    {
        list.push_back({{}, tr("Haptics"), {}, false, {}, {}, true});
        list.push_back({QStringLiteral("disk"), tr("Floppy drive feel"), OnOff(_hooks.driveHaptics()), false, {}, [self](int) {
                            self->_hooks.setDriveHaptics(!self->_hooks.driveHaptics());
                        }});
    }
    list.push_back({{}, tr("On the keyboard"), {}, false, {}, {}, true});
    list.push_back({QStringLiteral("info"), tr("Press the key"), tr("A / left pad click"), false, {}});
    list.push_back({QStringLiteral("info"), tr("SPACE / ENTER"), tr("X / Y"), false, {}});
    list.push_back({QStringLiteral("info"), tr("LOAD \"\" / RUN \"boot\""), tr("R4 / R5"), false, {}});
    list.push_back({QStringLiteral("info"), tr("ENTER"), tr("L4"), false, {}});
    list.push_back({QStringLiteral("info"), tr("Hide"), tr("B / View"), false, {}});
}

void HandheldMenu::quitRows(std::vector<Row>& list) const
{
    auto* self = const_cast<HandheldMenu*>(this);
    list.push_back({QStringLiteral("save"), tr("Quick save and quit"), tr("slot 1"), false, [self] {
                        StateNode params = StateNode::Object();
                        params["op"] = StateNode("save");
                        params["slot"] = StateNode(1);
                        self->submit("snapshot.quick", std::move(params), [self](bool ok, const QString&, const StateNode&) {
                            if (ok && self->_hooks.quit)
                                self->_hooks.quit();
                        });
                    }});
    list.push_back({QStringLiteral("quit"), tr("Quit without saving"), {}, false, [self] {
                        if (self->_hooks.quit)
                            self->_hooks.quit();
                    }});
}

void HandheldMenu::refreshSlots()
{
    _slots.assign(kSlots, Slot{});
    for (int n = 0; n < kSlots; ++n)
        _slots[static_cast<size_t>(n)].number = n + 1;
    StateNode params = StateNode::Object();
    params["op"] = StateNode("list");
    const QPointer<HandheldMenu> self(this);
    submit("snapshot.quick", std::move(params), [self](bool ok, const QString&, const StateNode& body) {
        if (!self || !ok)
            return;
        const StateNode* list = body.find("slots");
        if (list == nullptr || !list->isArray())
            return;
        for (const StateNode& entry : list->items)
        {
            const StateNode* number = entry.find("slot");
            if (number == nullptr || number->i < 1 || number->i > kSlots)
                continue;
            Slot& slot = self->_slots[static_cast<size_t>(number->i - 1)];
            if (const StateNode* used = entry.find("used"))
                slot.used = used->b;
            if (const StateNode* time = entry.find("time"))
                slot.time = time->i;
            if (const StateNode* picture = entry.find("picture"); picture != nullptr && !picture->s.empty())
                slot.picture.load(QString::fromStdString(picture->s));
        }
        self->update();
        self->emitState();
    });
}

void HandheldMenu::quickSlot(const char* op, int slot)
{
    StateNode params = StateNode::Object();
    params["op"] = StateNode(op);
    params["slot"] = StateNode(slot);
    const std::string what = op;
    const QPointer<HandheldMenu> self(this);
    submit("snapshot.quick", std::move(params), [self, what, slot](bool ok, const QString&, const StateNode&) {
        if (!self)
            return;
        if (what == "load" && ok)
        {
            self->_pausedByMenu = true;  // a loaded state runs on: the menu closes into it
            self->Close();
            return;
        }
        if (ok)
            self->_status = what == "save" ? tr("Saved to slot %1").arg(slot) : tr("Slot %1 deleted").arg(slot);
        self->refreshSlots();
    });
}

// region <Media>

void HandheldMenu::openBrowser(HandheldBrowser::Filter filter, const QString& title,
                               std::function<void(const QString&)> chosen)
{
    _browser.Open(filter);
    _browserRow = 0;
    _browserTitle = title;
    _browserChosen = std::move(chosen);
    _hasPicker = false;
    _hasBrowser = true;
    emitState();
}

void HandheldMenu::openFile(const QString& path)
{
    StateNode params = StateNode::Object();
    params["path"] = StateNode(path.toStdString());
    const QPointer<HandheldMenu> self(this);
    _status = tr("Opening %1…").arg(QFileInfo(path).fileName());
    submit("media.open", std::move(params), [self, path](bool ok, const QString&, const StateNode&) {
        if (!self || !ok)
            return;
        HandheldBrowser::AddRecent(path);
        self->_pausedByMenu = true;  // what was opened runs: the menu closes into it
        self->Close();
    });
}

void HandheldMenu::saveSnapshot()
{
    Emulator* emulator = _hooks.emulator ? _hooks.emulator() : nullptr;
    if (emulator == nullptr)
        return;
    // Named after what runs (the last opened file), else the machine, and the time
    QString base;
    const QStringList recent = HandheldBrowser::Recent();
    if (!recent.isEmpty())
        base = QFileInfo(recent.front()).completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("snapshot");
    const QString path = QDir(HandheldBrowser::Saves())
                             .filePath(QStringLiteral("%1-%2.szx").arg(base, QDateTime::currentDateTime().toString(
                                                                                 QStringLiteral("yyyyMMdd-HHmmss"))));
    StateNode params = StateNode::Object();
    params["path"] = StateNode(path.toStdString());
    const QPointer<HandheldMenu> self(this);
    submit("snapshot.save", std::move(params), [self, path](bool ok, const QString&, const StateNode&) {
        if (self && ok)
        {
            self->_status = tr("Saved %1").arg(QFileInfo(path).fileName());
            HandheldBrowser::AddRecent(path);
        }
    });
}

void HandheldMenu::openMediumActions(const std::string& slot, int kind)
{
    MediaManager* media = machineContext() != nullptr ? machineContext()->pMediaManager : nullptr;
    if (media == nullptr)
        return;
    const std::optional<SlotInfo> info = media->Info(slot);
    if (!info)
        return;
    const QPointer<HandheldMenu> self(this);
    const MediaKind mediaKind = static_cast<MediaKind>(kind);
    const QString label = info->descriptor.label.empty() ? QString::fromStdString(slot)
                                                         : QString::fromStdString(info->descriptor.label);
    auto params = [slot](std::initializer_list<std::pair<const char*, StateNode>> extra) {
        StateNode p = StateNode::Object();
        p["drive"] = StateNode(slot);
        for (const auto& [key, value] : extra)
            p[key] = value;
        return p;
    };
    std::vector<std::pair<QString, std::function<void()>>> actions;
    if (mediaKind == MediaKind::Tape && info->present)
    {
        auto tape = [self](const char* op) {
            return [self, op] {
                StateNode p = StateNode::Object();
                p["op"] = StateNode(std::string(op));
                self->submit("media.tape", std::move(p));
            };
        };
        const bool playing = Flag(readout("media.tape"), "playing");
        actions.push_back({playing ? tr("Pause") : tr("Play"), tape("toggle")});
        actions.push_back({tr("Rewind"), tape("rewind")});
        actions.push_back({tr("Go to block…"), [self] { self->openTapeBlocks(); }});
    }
    const HandheldBrowser::Filter filter = mediaKind == MediaKind::Tape     ? HandheldBrowser::Filter::Tape
                                           : mediaKind == MediaKind::Floppy ? HandheldBrowser::Filter::Disk
                                                                            : HandheldBrowser::Filter::All;
    actions.push_back({info->present ? tr("Replace…") : tr("Insert…"), [self, slot, filter, label, mediaKind] {
                           self->openBrowser(filter, tr("Insert into %1").arg(label), [self, slot, mediaKind](const QString& path) {
                               StateNode p = StateNode::Object();
                               p["path"] = StateNode(path.toStdString());
                               std::string id = "media.insert";
                               if (mediaKind == MediaKind::Tape || mediaKind == MediaKind::Floppy)
                               {
                                   // Through media.open: the fast-load settings apply, nothing starts by itself
                                   id = "media.open";
                                   p["kind"] = StateNode(std::string(mediaKind == MediaKind::Tape ? "tape" : "disk"));
                                   p["autostart"] = StateNode(false);
                                   if (mediaKind == MediaKind::Floppy)
                                       p["drive"] = StateNode(slot.substr(slot.rfind('.') + 1));
                               }
                               else
                                   p["drive"] = StateNode(slot);
                               self->submit(id, std::move(p), [self, path](bool ok, const QString&, const StateNode&) {
                                   if (self && ok)
                                   {
                                       HandheldBrowser::AddRecent(path);
                                       self->_status = tr("Inserted %1").arg(QFileInfo(path).fileName());
                                   }
                               });
                           });
                       }});
    if (info->present)
    {
        if (info->dirty)
            actions.push_back({tr("Save the changes"), [self, params] { self->submit("media.save", params({})); }});
        if (mediaKind == MediaKind::Floppy || mediaKind == MediaKind::Block)
            actions.push_back({info->writeProtect ? tr("Allow writes") : tr("Write protect"),
                               [self, params] { self->submit("media.protect", params({})); }});
        actions.push_back({tr("Eject"), [self, slot, mediaKind, params] {
                               if (mediaKind == MediaKind::Tape)
                               {
                                   StateNode p = StateNode::Object();
                                   p["op"] = StateNode(std::string("eject"));
                                   self->submit("media.tape", std::move(p));
                               }
                               else
                                   self->submit("media.eject", params({}));
                           }});
    }
    QString warning;
    if (info->present)
        warning = QFileInfo(QString::fromStdString(info->source)).fileName() +
                  (info->dirty ? tr(", unsaved: %1").arg(QString::fromStdString(info->changes)) : QString());
    openActions(label, std::move(actions), warning);
}

void HandheldMenu::openTapeBlocks()
{
    const Tape* tape = machineContext() != nullptr ? machineContext()->pTape : nullptr;
    if (tape == nullptr)
        return;
    _picker = Picker{};
    _picker.title = tr("Go to block");
    const int current = static_cast<int>(Number(readout("media.tape"), "block"));
    for (const TapeBlockDescriptor& block : tape->GetBlockCatalog())
    {
        const QString name = QString::fromStdString(block.name).trimmed();
        _picker.items.push_back(QStringLiteral("%1. %2").arg(block.index + 1).arg(name.isEmpty() ? tr("(data)") : name));
    }
    _picker.row = std::clamp(current, 0, std::max(0, static_cast<int>(_picker.items.size()) - 1));
    const QPointer<HandheldMenu> self(this);
    _picker.chosen = [self](int index) {
        if (!self)
            return;
        StateNode params = StateNode::Object();
        params["op"] = StateNode(std::string("seek"));
        params["block"] = StateNode(static_cast<int64_t>(index));
        self->submit("media.tape", std::move(params));
    };
    _hasPicker = !_picker.items.empty();
    emitState();
}

// endregion </Media>

// region <Expansion>

void HandheldMenu::openCardPicker(const std::string& slot)
{
    Emulator* emulator = _hooks.emulator ? _hooks.emulator() : nullptr;
    if (emulator == nullptr || !_hooks.changeSlot)
        return;
    const QPointer<HandheldMenu> self(this);
    auto apply = [self](std::string verb, std::string where, std::string card) {
        if (!self || !self->_hooks.changeSlot)
            return;
        self->_status = tr("Restarting the machine…");
        self->update();
        const QString error = self->_hooks.changeSlot(verb, where, card);
        if (!self)
            return;
        self->_status = error.isEmpty() ? tr("Applied, the machine restarted") : error;
        self->emitState();
    };
    // The catalogue planned into this slot (an empty slot: where the planner would put each card): outcome fits /
    // needs-replace / refused, inSlot for the card there now, label the plain name
    SlotControlRequest request;
    request.verb = "catalog";
    request.emulatorId = emulator->GetId();
    request.slot = slot;
    const SlotControlReply reply = SlotControl::Execute(request);
    std::vector<std::pair<QString, std::function<void()>>> actions;
    if (!slot.empty())
        actions.push_back({tr("Remove the card"), [apply, slot] { apply("remove", slot, {}); }});
    int current = -1;
    for (const StateNode& card : Items(reply.body, "cards"))
    {
        const StateNode* here = Field(card, "thisMachine");
        if (here == nullptr)
            continue;
        const QString outcome = Text(*here, "outcome");
        if (outcome == QStringLiteral("refused") || Text(*here, "fit") == QStringLiteral("unrealistic"))
            continue;  // what the slot cannot take is not offered here (the desktop window allows the override)
        const std::string id = Text(card, "id").toStdString();
        QString name = Text(card, "label").isEmpty() ? Text(card, "name") : Text(card, "label");
        if (Flag(*here, "inSlot"))
        {
            current = static_cast<int>(actions.size());
            actions.push_back({tr("%1 (here now)").arg(name), {}});
            continue;
        }
        const QString fittedIn = Text(*here, "fittedIn");
        const std::vector<StateNode>& removes = Items(*here, "removes");
        if (!fittedIn.isEmpty())
            name = tr("%1 (now in %2)").arg(name, fittedIn);
        else if (outcome == QStringLiteral("needs-replace") && !removes.empty() && removes.front().kind == StateNode::Kind::String)
            name = tr("%1 (replaces %2)").arg(name, QString::fromStdString(removes.front().s).section(QStringLiteral(" = "), 1));
        if (slot.empty() && !Text(*here, "slot").isEmpty())
            name += QStringLiteral(" → %1").arg(Text(*here, "slot"));
        actions.push_back({name, [apply, id, slot] { apply("plug", slot, id); }});
    }
    openActions(slot.empty() ? tr("Add a card") : QString::fromStdString(slot), std::move(actions),
                tr("A change restarts the machine"));
    if (current >= 0)
        _picker.row = current;
}

// endregion </Expansion>

void HandheldMenu::openActions(const QString& title, std::vector<std::pair<QString, std::function<void()>>> actions,
                               const QString& warning)
{
    _picker = Picker{};
    _picker.title = title;
    _picker.warning = warning;
    auto run = std::make_shared<std::vector<std::function<void()>>>();
    for (auto& [label, action] : actions)
    {
        _picker.items.push_back(label);
        run->push_back(std::move(action));
    }
    _picker.chosen = [run](int index) {
        if (index >= 0 && index < static_cast<int>(run->size()) && (*run)[static_cast<size_t>(index)])
            (*run)[static_cast<size_t>(index)]();
    };
    _hasPicker = !_picker.items.empty();
    emitState();
}

void HandheldMenu::openModelPicker()
{
    _picker = Picker{};
    _picker.title = tr("Model");
    _picker.warning = tr("Changing the model restarts the machine");
    auto keys = std::make_shared<std::vector<std::pair<std::string, int>>>();
    const int ramSizes[] = {48, 128, 256, 512, 1024, 2048, 4096};
    for (const TMemModel& model : EmulatorManager::GetInstance()->GetAvailableModels())
    {
        if (!Config::IsModelCreatable(model) || model.FullName == nullptr || model.ShortName == nullptr)
            continue;
        int count = 0;
        for (const int ram : ramSizes)
            if ((model.AvailRAMs & static_cast<unsigned>(ram)) != 0)
                ++count;
        for (const int ram : ramSizes)
        {
            if ((model.AvailRAMs & static_cast<unsigned>(ram)) == 0)
                continue;
            if (EmulatorContext* context = machineContext();
                context != nullptr && model.Model == context->config.mem_model && static_cast<int>(context->config.ramsize) == ram)
                _picker.row = static_cast<int>(_picker.items.size());
            _picker.items.push_back(count == 1 ? QString::fromUtf8(model.FullName)
                                               : QStringLiteral("%1 %2K").arg(QString::fromUtf8(model.FullName)).arg(ram));
            keys->push_back({model.ShortName, ram});
        }
    }
    const QPointer<HandheldMenu> self(this);
    _picker.chosen = [self, keys](int index) {
        if (!self)
            return;
        StateNode params = StateNode::Object();
        params["model"] = StateNode((*keys)[static_cast<size_t>(index)].first);
        params["ram_kb"] = StateNode((*keys)[static_cast<size_t>(index)].second);
        params["replace"] = StateNode(true);
        self->submit("mach.start", std::move(params), [self](bool ok, const QString&, const StateNode&) {
            if (self && ok)
                self->Close();  // the new machine starts running; its pictures replace the old one
        });
    };
    _hasPicker = !_picker.items.empty();
    emitState();
}

void HandheldMenu::openSpeedPicker()
{
    Emulator* emulator = _hooks.emulator ? _hooks.emulator() : nullptr;
    _picker = Picker{};
    _picker.title = tr("Speed");
    for (const int speed : kSpeeds)
    {
        if (emulator != nullptr && emulator->GetSpeedMultiplier() == speed)
            _picker.row = static_cast<int>(_picker.items.size());
        _picker.items.push_back(QStringLiteral("%1×").arg(speed));
    }
    const QPointer<HandheldMenu> self(this);
    _picker.chosen = [self](int index) {
        if (!self)
            return;
        StateNode params = StateNode::Object();
        params["value"] = StateNode(kSpeeds[index]);
        self->submit("set.speed", std::move(params));
    };
    _hasPicker = true;
    emitState();
}

void HandheldMenu::openKeyboardPicker()
{
    if (!_hooks.keyboards || !_hooks.setKeyboard)
        return;
    _picker = Picker{};
    _picker.title = tr("On-screen keyboard");
    auto ids = std::make_shared<std::vector<std::string>>();
    const std::string forced = _hooks.keyboard ? _hooks.keyboard() : std::string();
    for (const auto& [id, name] : _hooks.keyboards())
    {
        if (id == forced)
            _picker.row = static_cast<int>(_picker.items.size());
        _picker.items.push_back(name);
        ids->push_back(id);
    }
    const QPointer<HandheldMenu> self(this);
    _picker.chosen = [self, ids](int index) {
        if (self && self->_hooks.setKeyboard)
            self->_hooks.setKeyboard((*ids)[static_cast<size_t>(index)]);
    };
    _hasPicker = !_picker.items.empty();
    emitState();
}

void HandheldMenu::openCrtPicker()
{
    const std::optional<WindowDisplaySettings> settings = WindowView::PublishedSettings();
    if (!settings)
        return;
    _picker = Picker{};
    _picker.title = tr("CRT effect");
    if (!settings->gpu)
        _picker.warning = tr("The shaders need GPU acceleration");
    auto names = std::make_shared<std::vector<std::string>>();
    _picker.items.push_back(OnOff(false));
    names->push_back({});
    const bool on = settings->crt && settings->crtProfile != "none";
    for (const std::string& name : settings->crtProfiles)
    {
        if (name == "none" || (!settings->gpu && name.rfind("shader:", 0) == 0))
            continue;  // "none" is Off; a shader needs the GPU
        if (on && name == settings->crtProfile)
            _picker.row = static_cast<int>(_picker.items.size());
        _picker.items.push_back(CrtProfileTitle(name));
        names->push_back(name);
    }
    const QPointer<HandheldMenu> self(this);
    const bool crt = settings->crt;
    _picker.chosen = [self, names, crt](int index) {
        if (!self || index < 0 || index >= static_cast<int>(names->size()))
            return;
        const std::string name = (*names)[static_cast<size_t>(index)];
        auto switchCrt = [self](bool value) {
            StateNode params = StateNode::Object();
            params["on"] = StateNode(value);
            self->submit("video.crt_toggle", std::move(params));
        };
        if (name.empty())
        {
            switchCrt(false);
            return;
        }
        StateNode params = StateNode::Object();
        params["profile"] = StateNode(name);
        // The profile is kept while CRT is off: a choice switches CRT on too
        self->submit("video.crt_profile", std::move(params), [self, crt, switchCrt](bool ok, const QString&, const StateNode&) {
            if (self && ok && !crt)
                switchCrt(true);
        });
    };
    _hasPicker = true;
    emitState();
}

void HandheldMenu::openTemporalPicker()
{
    const StateNode temporal = readout("video.temporal");
    const bool on = Flag(temporal, "on");
    const bool dlss = Text(temporal, "effect") == QStringLiteral("dlss");
    const QPointer<HandheldMenu> self(this);
    auto choose = [self, on](const char* effect) {
        return [self, on, effect] {
            if (!self)
                return;
            StateNode params = StateNode::Object();
            params["effect"] = StateNode(std::string(effect));
            // While off the choice is only kept: switch the effects on after it
            self->submit("video.temporal_effect", std::move(params), [self, on](bool ok, const QString&, const StateNode&) {
                if (!self || !ok || on)
                    return;
                StateNode toggle = StateNode::Object();
                toggle["on"] = StateNode(true);
                self->submit("video.temporal_toggle", std::move(toggle));
            });
        };
    };
    openActions(tr("Temporal effect"),
                {{OnOff(false),
                  [self] {
                      if (!self)
                          return;
                      StateNode params = StateNode::Object();
                      params["on"] = StateNode(false);
                      self->submit("video.temporal_toggle", std::move(params));
                  }},
                 {tr("Frame blending"), choose("blend")},
                 {tr("ZX DLSS de-flicker"), choose("dlss")}});
    _picker.row = !on ? 0 : dlss ? 2 : 1;
}

void HandheldMenu::openBorderPicker()
{
    const QString now = Text(readout("video.viewport"), "viewport");
    const QPointer<HandheldMenu> self(this);
    std::vector<std::pair<QString, std::function<void()>>> actions;
    int row = 0;
    for (const char* word : {"full", "symmetric", "standard", "screen_only"})
    {
        if (now == QLatin1String(word))
            row = static_cast<int>(actions.size());
        actions.emplace_back(ViewportTitle(QString::fromLatin1(word)), [self, word] {
            if (!self)
                return;
            StateNode params = StateNode::Object();
            params["viewport"] = StateNode(std::string(word));
            self->submit("video.viewport", std::move(params));
        });
    }
    openActions(tr("Border"), std::move(actions));
    _picker.row = row;
}

void HandheldMenu::Run(const std::string& id, const QString& command)
{
    StateNode params = StateNode::Object();
    params["command"] = StateNode(command.toStdString());
    submit(id, std::move(params));
}

EmulatorContext* HandheldMenu::machineContext() const
{
    return _hooks.context ? _hooks.context() : nullptr;
}

StateNode HandheldMenu::readout(const std::string& name) const
{
    Emulator* emulator = _hooks.emulator ? _hooks.emulator() : nullptr;
    return emulator != nullptr ? actions::ReadReadout(*emulator, name) : StateNode();
}

std::optional<bool> HandheldMenu::toggleOn(const std::string& id, const StateNode& params) const
{
    Emulator* emulator = _hooks.emulator ? _hooks.emulator() : nullptr;
    if (emulator == nullptr)
        return std::nullopt;
    return actions::ActionRegistry::Instance().ToggleState(id, emulator->GetId(), params);
}

void HandheldMenu::submitDelta(const std::string& id, StateNode params, double delta)
{
    Emulator* emulator = _hooks.emulator ? _hooks.emulator() : nullptr;
    if (emulator == nullptr)
        return;
    actions::ActionValue value;
    value.kind = actions::ValueKind::Delta;
    value.delta = delta;
    value.params = std::move(params);
    actions::ActionOrigin origin;
    origin.origin = execution::Origin::Gui;
    origin.identity = "handheld-menu";
    origin.client = "handheld menu";
    const QPointer<HandheldMenu> self(this);
    actions::ActionRegistry::Instance().Submit(id, std::move(value), emulator->GetId(), std::move(origin),
                                               [self](actions::ActionResult result) {
                                                   QCoreApplication* app = QCoreApplication::instance();
                                                   if (app == nullptr)
                                                       return;
                                                   QMetaObject::invokeMethod(
                                                       app,
                                                       [self, result = std::move(result)] {
                                                           if (!self)
                                                               return;
                                                           if (!result.ok)
                                                               self->_status = QString::fromStdString(result.message);
                                                           self->emitState();
                                                       },
                                                       Qt::QueuedConnection);
                                               });
}

void HandheldMenu::submit(const std::string& id, StateNode params,
                          std::function<void(bool, const QString&, const StateNode&)> done)
{
    Emulator* emulator = _hooks.emulator ? _hooks.emulator() : nullptr;
    if (emulator == nullptr)
    {
        _status = tr("No machine");
        update();
        return;
    }
    actions::ActionValue value;
    value.params = std::move(params);
    actions::ActionOrigin origin;
    origin.origin = execution::Origin::Gui;
    origin.identity = "handheld-menu";
    origin.client = "handheld menu";
    const QPointer<HandheldMenu> self(this);
    actions::ActionRegistry::Instance().Submit(
        id, std::move(value), emulator->GetId(), std::move(origin),
        [self, done = std::move(done)](actions::ActionResult result) {
            QCoreApplication* app = QCoreApplication::instance();
            if (app == nullptr)
                return;
            QMetaObject::invokeMethod(
                app,
                [self, done, result = std::move(result)] {
                    if (!self)
                        return;
                    const QString message = QString::fromStdString(result.message);
                    if (!result.ok)
                    {
                        self->_status = message;
                        if (self->_hooks.notice)
                            self->_hooks.notice(message);
                    }
                    if (done)
                        done(result.ok, message, result.body);
                    self->update();
                    self->emitState();
                    // The window publishes its settings after the action: the values follow a moment later
                    QTimer::singleShot(150, self, [self] {
                        if (self)
                            self->update();
                    });
                },
                Qt::QueuedConnection);
        });
}

void HandheldMenu::emitState()
{
    update();
    emit stateChanged();
}

QJsonObject HandheldMenu::State() const
{
    QJsonObject state{{QStringLiteral("open"), _open}};
    if (!_open)
        return state;
    const SectionInfo& section = _sections[static_cast<size_t>(_section)];
    state[QStringLiteral("section")] = section.icon;
    state[QStringLiteral("focus")] = _hasBrowser  ? QStringLiteral("browser")
                                     : _hasPicker ? QStringLiteral("picker")
                                     : _inPage    ? QStringLiteral("page")
                                                  : QStringLiteral("sections");
    state[QStringLiteral("row")] = _row;
    QJsonArray items;
    if (section.page == Page::Slots)
        for (const Slot& slot : _slots)
            items.append(QJsonObject{{QStringLiteral("slot"), slot.number},
                                     {QStringLiteral("used"), slot.used},
                                     {QStringLiteral("time"), slot.time}});
    else
        for (const Row& row : rows())
        {
            QJsonObject item{{QStringLiteral("label"), row.label}, {QStringLiteral("value"), row.value}};
            if (row.header)
                item[QStringLiteral("header")] = true;
            if (row.adjust)
                item[QStringLiteral("adjust")] = true;
            items.append(item);
        }
    state[QStringLiteral("items")] = items;
    if (_hasPicker)
    {
        QJsonArray choices;
        for (const QString& item : _picker.items)
            choices.append(item);
        state[QStringLiteral("picker")] = QJsonObject{{QStringLiteral("title"), _picker.title},
                                                      {QStringLiteral("row"), _picker.row},
                                                      {QStringLiteral("items"), choices}};
    }
    if (_hasBrowser)
    {
        QJsonArray entries;
        for (const HandheldBrowser::Entry& entry : _browser.entries())
            entries.append(entry.name);
        state[QStringLiteral("browser")] = QJsonObject{{QStringLiteral("title"), _browserTitle},
                                                       {QStringLiteral("folder"), _browser.title()},
                                                       {QStringLiteral("row"), _browserRow},
                                                       {QStringLiteral("entries"), entries}};
    }
    if (!_status.isEmpty())
        state[QStringLiteral("status")] = _status;
    return state;
}

// endregion </Pages>

// region <Painting>

qreal HandheldMenu::scale() const
{
    return std::min(width() / kDesignWidth, height() / kDesignHeight);
}

void HandheldMenu::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // An opaque page, as SteamOS draws its own
    p.fillRect(rect(), kBackground);
    _hits.clear();
    const qreal s = scale();
    _hitDragStep = kRowHeight * s;
    const QRectF design((width() - kDesignWidth * s) / 2, (height() - kDesignHeight * s) / 2, kDesignWidth * s,
                        kDesignHeight * s);
    const qreal footer = 56 * s;
    const qreal sidebar = 320 * s;
    paintSidebar(p, QRectF(design.left(), design.top(), sidebar, design.height() - footer));
    const QRectF pageArea(design.left() + sidebar, design.top(), design.width() - sidebar, design.height() - footer);
    if (page() == Page::Slots)
        paintSlots(p, pageArea);
    else if (page() == Page::Network)
        paintNetwork(p, pageArea);
    else
        paintRows(p, pageArea);
    paintFooter(p, QRectF(0, design.bottom() - footer, width(), footer + (height() - design.bottom())));
    if (_hasPicker || _hasBrowser)
    {
        // A popup takes the touches: a tap outside it is Back
        _hits.clear();
        _hits.push_back({QRectF(rect()), [this](QPointF) { Back(); }});
    }
    if (_hasPicker)
        paintPicker(p);
    if (_hasBrowser)
        paintBrowser(p);
}

void HandheldMenu::paintIcon(QPainter& p, const QString& name, const QRectF& rect, const QColor& color) const
{
    if (name.isEmpty())
        return;
    QSvgRenderer renderer(QStringLiteral(":/icons/handheld/%1.svg").arg(name));
    if (!renderer.isValid())
        return;
    const qreal ratio = devicePixelRatioF();
    QImage image((rect.size() * ratio).toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter ip(&image);
        ip.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&ip, QRectF(QPointF(0, 0), QSizeF(image.size())));
        ip.setCompositionMode(QPainter::CompositionMode_SourceIn);
        ip.fillRect(image.rect(), color);
    }
    image.setDevicePixelRatio(ratio);
    p.drawImage(rect.topLeft(), image);
}

void HandheldMenu::paintSidebar(QPainter& p, const QRectF& area)
{
    const qreal s = scale();
    p.fillRect(area, kSidebar);
    QFont font = p.font();
    font.setPixelSize(static_cast<int>(23 * s));
    p.setFont(font);
    const qreal rowHeight = std::min<qreal>(60 * s, (area.height() - 32 * s) / static_cast<qreal>(_sections.size()));
    qreal y = area.top() + 16 * s;
    for (size_t n = 0; n < _sections.size(); ++n)
    {
        const QRectF row(area.left(), y, area.width(), rowHeight);
        _hits.push_back({row, [this, n](QPointF) { tapSection(static_cast<int>(n)); }});
        const bool selected = static_cast<int>(n) == _section;
        if (selected)
        {
            QLinearGradient gradient(row.topLeft(), row.topRight());
            gradient.setColorAt(0, kSelectedFrom);
            gradient.setColorAt(1, kSelectedTo);
            p.fillRect(row, gradient);
            p.fillRect(QRectF(row.left(), row.top(), 4 * s, row.height()), kAccent);
            if (!_inPage && !_hasPicker && !_hasBrowser)
            {
                p.setPen(QPen(kAccent, 2 * s));
                p.setBrush(Qt::NoBrush);
                p.drawRect(row.adjusted(1 * s, 1 * s, -1 * s, -1 * s));
            }
        }
        const QColor color = selected ? QColor(Qt::white) : kText;
        paintIcon(p, _sections[n].icon, QRectF(row.left() + 28 * s, row.center().y() - 14 * s, 28 * s, 28 * s), color);
        p.setPen(color);
        p.drawText(row.adjusted(76 * s, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, _sections[n].label);
        y += rowHeight;
    }
}

void HandheldMenu::paintRows(QPainter& p, const QRectF& area)
{
    const qreal s = scale();
    const qreal margin = 40 * s;
    const qreal top = area.top() + 20 * s;
    const qreal bottom = area.bottom() - 36 * s;  // room for the status line
    QFont font = p.font();
    const std::vector<Row> list = rows();
    // Layout in design units, then the scroll that keeps the focused row in view
    std::vector<qreal> ys;
    qreal y = 0;
    for (const Row& row : list)
    {
        ys.push_back(y);
        y += row.header ? kHeaderHeight : row.level >= 0 ? kSliderHeight : kRowHeight;
    }
    const qreal visible = (bottom - top) / s;
    if (_row >= 0 && _row < static_cast<int>(list.size()))
    {
        const qreal from = ys[static_cast<size_t>(_row)];
        const qreal to = from + (list[static_cast<size_t>(_row)].level >= 0 ? kSliderHeight : kRowHeight);
        // A caption right above the focused row comes into view with it
        const qreal lead = _row > 0 && list[static_cast<size_t>(_row - 1)].header ? kHeaderHeight : 0;
        if (from - lead < _scroll)
            _scroll = from - lead;
        if (to > _scroll + visible)
            _scroll = to - visible;
    }
    _scroll = std::clamp<qreal>(_scroll, 0, std::max<qreal>(0, y - visible));
    p.save();
    p.setClipRect(QRectF(area.left(), top, area.width(), bottom - top));
    for (size_t n = 0; n < list.size(); ++n)
    {
        const Row& row = list[n];
        const qreal rowTop = top + (ys[n] - _scroll) * s;
        if (rowTop > bottom || rowTop + kSliderHeight * s < top)
            continue;
        if (row.header)
        {
            font.setPixelSize(static_cast<int>(17 * s));
            font.setBold(true);
            p.setFont(font);
            p.setPen(kDim);
            p.drawText(QRectF(area.left() + margin + 4 * s, rowTop + 10 * s, area.width() - 2 * margin, kHeaderHeight * s - 14 * s),
                       Qt::AlignBottom | Qt::AlignLeft, row.label.toUpper());
            font.setBold(false);
            continue;
        }
        const bool slider = row.level >= 0;
        const QRectF box(area.left() + margin, rowTop, area.width() - 2 * margin, ((slider ? kSliderHeight : kRowHeight) - 6) * s);
        if (box.bottom() > top && box.top() < bottom)
            _hits.push_back({box.intersected(QRectF(area.left(), top, area.width(), bottom - top)),
                             [this, n, box, slider](QPointF at) { tapRow(static_cast<int>(n), box, slider, at); }});
        const bool focused = _inPage && !_hasPicker && !_hasBrowser && static_cast<int>(n) == _row;
        p.setPen(Qt::NoPen);
        p.setBrush(focused ? QColor(0x2f, 0x35, 0x40) : kRow);
        p.drawRoundedRect(box, 4 * s, 4 * s);
        if (focused)
        {
            p.setPen(QPen(kAccent, 2 * s));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(box.adjusted(1 * s, 1 * s, -1 * s, -1 * s), 4 * s, 4 * s);
        }
        if (slider)
        {
            paintSliderRow(p, row, box, focused);
            continue;
        }
        const bool inert = !row.activate && !row.adjust;
        paintIcon(p, row.icon, QRectF(box.left() + 20 * s, box.center().y() - 13 * s, 26 * s, 26 * s), inert ? kDim : kText);
        font.setPixelSize(static_cast<int>(22 * s));
        p.setFont(font);
        p.setPen(inert ? kDim : kText);
        const qreal labelWidth = QFontMetricsF(font).horizontalAdvance(row.label);
        p.drawText(box.adjusted(64 * s, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, row.label);
        qreal right = box.right() - 20 * s;
        if (row.picker)
        {
            paintIcon(p, QStringLiteral("chevron-right"), QRectF(right - 22 * s, box.center().y() - 11 * s, 22 * s, 22 * s), kDim);
            right -= 32 * s;
        }
        if (!row.value.isEmpty() || row.adjust)
        {
            font.setPixelSize(static_cast<int>(20 * s));
            p.setFont(font);
            const qreal left = box.left() + 64 * s + labelWidth + 24 * s;
            QString value = row.value;
            if (row.adjust && focused)
                value = QStringLiteral("◀  %1  ▶").arg(row.value);
            value = QFontMetricsF(font).elidedText(value, Qt::ElideMiddle, std::max<qreal>(0, right - left));
            p.setPen(focused && row.adjust ? QColor(Qt::white) : kDim);
            p.drawText(QRectF(left, box.top(), right - left, box.height()), Qt::AlignVCenter | Qt::AlignRight, value);
        }
    }
    p.restore();
    // A hint that the page goes on
    if (y - _scroll > visible + 1)
        paintIcon(p, QStringLiteral("chevron-down"), QRectF(area.center().x() - 12 * s, bottom - 6 * s, 24 * s, 24 * s), kDim);
    if (!_status.isEmpty())
    {
        font.setPixelSize(static_cast<int>(16 * s));
        p.setFont(font);
        p.setPen(kDim);
        p.drawText(QRectF(area.left() + margin, area.bottom() - 30 * s, area.width() - 2 * margin, 24 * s), Qt::AlignLeft,
                   _status);
    }
}

void HandheldMenu::paintSliderRow(QPainter& p, const Row& row, const QRectF& box, bool focused)
{
    const qreal s = scale();
    const QColor ink = row.muted ? kDim : kText;
    const QRectF line(box.left() + 64 * s, box.top() + 8 * s, box.width() - 88 * s, 40 * s);
    paintIcon(p, row.icon, QRectF(box.left() + 20 * s, line.center().y() - 13 * s, 26 * s, 26 * s), ink);
    QFont font = p.font();
    font.setPixelSize(static_cast<int>(22 * s));
    p.setFont(font);
    p.setPen(ink);
    p.drawText(line, Qt::AlignVCenter | Qt::AlignLeft, row.label);
    font.setPixelSize(static_cast<int>(20 * s));
    p.setFont(font);
    p.setPen(focused ? QColor(Qt::white) : kDim);
    p.drawText(line, Qt::AlignVCenter | Qt::AlignRight, focused ? QStringLiteral("◀  %1  ▶").arg(row.value) : row.value);

    // The track with the level and its knob, the live peak as a thin meter under it
    const qreal left = line.left();
    const qreal right = line.right();
    const qreal centerY = box.bottom() - 28 * s;
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    p.drawRoundedRect(QRectF(left, centerY - 3 * s, right - left, 6 * s), 3 * s, 3 * s);
    const qreal x = left + (right - left) * std::clamp(row.level, 0.0, 1.0);
    p.setBrush(row.muted ? kDim : kAccent);
    p.drawRoundedRect(QRectF(left, centerY - 3 * s, x - left, 6 * s), 3 * s, 3 * s);
    p.setBrush(focused ? QColor(Qt::white) : ink);
    p.drawEllipse(QPointF(x, centerY), 10 * s, 10 * s);
    if (row.peak > 0.001 && !row.muted)
    {
        QLinearGradient meter(left, 0, right, 0);
        meter.setColorAt(0, QColor(0x59, 0xbf, 0x40));
        meter.setColorAt(0.75, QColor(0xd8, 0xc0, 0x30));
        meter.setColorAt(1, QColor(0xe0, 0x48, 0x30));
        p.setBrush(meter);
        p.drawRoundedRect(QRectF(left, centerY + 14 * s, (right - left) * std::clamp(row.peak, 0.0, 1.0), 4 * s), 2 * s, 2 * s);
    }
}

void HandheldMenu::paintNetwork(QPainter& p, const QRectF& area)
{
    const qreal s = scale();
    const qreal margin = 40 * s;
    const qreal gap = 16 * s;
    EmulatorContext* context = machineContext();
    const StateNode report = context != nullptr ? DeviceState::Network(context) : StateNode();
    QFont font = p.font();
    auto draw = [&](const QRectF& rect, qreal pixels, const QColor& color, int flags, const QString& text, bool bold = false) {
        font.setPixelSize(static_cast<int>(pixels * s));
        font.setBold(bold);
        p.setFont(font);
        p.setPen(color);
        p.drawText(rect, flags, QFontMetricsF(font).elidedText(text, Qt::ElideRight, rect.width()));
        font.setBold(false);
    };
    auto card = [&](const QRectF& rect) {
        p.setPen(Qt::NoPen);
        p.setBrush(kRow);
        p.drawRoundedRect(rect, 6 * s, 6 * s);
    };
    auto bytes = [](double value) {
        if (value < 1024)
            return QStringLiteral("%1 B").arg(static_cast<qint64>(value));
        if (value < 1024 * 1024)
            return QStringLiteral("%1 KB").arg(value / 1024, 0, 'f', 1);
        return QStringLiteral("%1 MB").arg(value / (1024 * 1024), 0, 'f', 1);
    };

    // The card (or why there is none) with the settings as chips and the activity on the right
    const QRectF head(area.left() + margin, area.top() + 24 * s, area.width() - 2 * margin, 108 * s);
    card(head);
    paintIcon(p, QStringLiteral("network"), QRectF(head.left() + 24 * s, head.center().y() - 22 * s, 44 * s, 44 * s), kText);
    const QRectF titleArea(head.left() + 92 * s, head.top() + 14 * s, head.width() - 260 * s, 36 * s);
    if (!Flag(report, "available"))
    {
        draw(titleArea, 26, kText, Qt::AlignVCenter | Qt::AlignLeft, tr("No network"), true);
        draw(titleArea.translated(0, 42 * s), 18, kDim, Qt::AlignVCenter | Qt::AlignLeft, Text(report, "reason"));
        return;
    }
    const StateNode* settings = Field(report, "settings");
    const QString cardName = settings != nullptr ? Text(*settings, "card") : QString();
    draw(titleArea, 26, kText, Qt::AlignVCenter | Qt::AlignLeft,
         cardName.isEmpty() || cardName == QStringLiteral("NONE") ? tr("No network card") : cardName, true);
    if (settings != nullptr)
    {
        QStringList chips{tr("Ethernet %1").arg(Text(*settings, "ethernet_mode")), tr("DNS %1").arg(Text(*settings, "dns_mode").toLower())};
        if (Flag(*settings, "host_access"))
            chips << tr("Host access");
        if (Flag(*settings, "remote_access"))
            chips << tr("Remote access");
        font.setPixelSize(static_cast<int>(16 * s));
        p.setFont(font);
        qreal x = titleArea.left();
        const qreal chipY = head.top() + 60 * s;
        for (const QString& chip : chips)
        {
            const qreal width = QFontMetricsF(font).horizontalAdvance(chip) + 24 * s;
            if (x + width > head.right() - 24 * s)
                break;
            p.setPen(Qt::NoPen);
            p.setBrush(kControl);
            p.drawRoundedRect(QRectF(x, chipY, width, 30 * s), 15 * s, 15 * s);
            p.setPen(kText);
            p.drawText(QRectF(x, chipY, width, 30 * s), Qt::AlignCenter, chip);
            x += width + 8 * s;
        }
    }

    // The traffic: a rate from the counter's samples, kept while the page shows
    const StateNode* traffic = Field(report, "traffic");
    const double total = traffic != nullptr ? Number(*traffic, "bytes") : 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (_trafficSamples.empty() || now - _trafficSamples.back().first >= 450)
    {
        if (!_trafficSamples.empty() && now - _trafficSamples.back().first < 5000)
            _trafficRates.push_back(std::max(0.0, total - _trafficSamples.back().second) * 1000.0 /
                                    static_cast<double>(now - _trafficSamples.back().first));
        _trafficSamples = {{now, total}};
        if (_trafficRates.size() > 60)
            _trafficRates.erase(_trafficRates.begin(), _trafficRates.end() - 60);
    }
    const double rate = _trafficRates.empty() ? 0 : _trafficRates.back();
    const StateNode* virtualNetwork = Field(report, "virtual_network");
    const bool busy = rate > 0 || (virtualNetwork != nullptr && !Items(*virtualNetwork, "sockets").empty());
    const QRectF pill(head.right() - 136 * s, head.top() + 18 * s, 112 * s, 32 * s);
    p.setPen(Qt::NoPen);
    p.setBrush(busy ? kAccent : kControl);
    p.drawRoundedRect(pill, 16 * s, 16 * s);
    draw(pill, 17, busy ? QColor(Qt::white) : kText, Qt::AlignCenter, busy ? tr("Active") : tr("Idle"), true);

    struct Tile
    {
        QString caption;
        QString big;
        std::vector<std::pair<QString, QString>> lines;
        bool graph = false;
    };
    std::vector<Tile> tiles;
    if (traffic != nullptr)
        tiles.push_back({tr("Traffic"), QStringLiteral("%1/s").arg(bytes(rate)),
                         {{tr("Total"), bytes(total)},
                          {tr("Socket operations"), QString::number(static_cast<qint64>(Number(*traffic, "socket_ops")))},
                          {tr("Recording"), OnOff(Field(*traffic, "file") != nullptr && Flag(*Field(*traffic, "file"), "recording"))}},
                         true});
    if (virtualNetwork != nullptr)
        tiles.push_back({tr("Virtual network"), Text(*virtualNetwork, "gateway"),
                         {{tr("Network"), Text(*virtualNetwork, "network")},
                          {tr("DNS"), Text(*virtualNetwork, "dns")},
                          {tr("Leases / sockets"), QStringLiteral("%1 / %2")
                                                       .arg(Items(*virtualNetwork, "dhcp_leases").size())
                                                       .arg(Items(*virtualNetwork, "sockets").size())}}});
    if (const StateNode* com = Field(report, "com_port"); com != nullptr && Flag(*com, "fitted"))
        tiles.push_back({tr("COM port"), Flag(*com, "connected") ? tr("Connected") : tr("Not connected"),
                         {{tr("Peer"), Text(*com, "peer")},
                          {tr("Speed"), tr("%1 baud").arg(static_cast<qint64>(Number(*com, "baud")))},
                          {tr("In / out"), QStringLiteral("%1 / %2").arg(bytes(Number(*com, "bytes_in")), bytes(Number(*com, "bytes_out")))}}});
    // The other ports (a machine serial port, ZiFi, an ATM ESP): fitted ones with their first values as they come
    const std::pair<const char*, QString> others[] = {{"machine_serial", tr("Serial port")}, {"zifi", tr("ZiFi")},
                                                      {"atm2ioesp", tr("ESP module")}};
    for (const auto& [key, caption] : others)
    {
        const StateNode* node = Field(report, key);
        if (node == nullptr || !Flag(*node, "fitted"))
            continue;
        Tile tile{caption, tr("Fitted"), {}};
        for (const auto& [name, value] : node->members)
        {
            if (tile.lines.size() >= 3 || name == "fitted")
                continue;
            if (value.kind == StateNode::Kind::String && !value.s.empty())
                tile.lines.emplace_back(Readable(name), QString::fromStdString(value.s));
            else if (value.kind == StateNode::Kind::Int)
                tile.lines.emplace_back(Readable(name), QString::number(value.i));
            else if (value.kind == StateNode::Kind::Bool)
                tile.lines.emplace_back(Readable(name), OnOff(value.b));
        }
        tiles.push_back(std::move(tile));
    }
    std::vector<std::pair<QString, QString>> cards;
    for (const StateNode& slot : Items(report, "slots"))
        cards.emplace_back(Text(slot, "label").isEmpty() ? Text(slot, "id") : Text(slot, "label"), Text(slot, "card"));
    for (const StateNode& note : Items(report, "not_fitted"))
        if (note.kind == StateNode::Kind::String)
            cards.emplace_back(tr("Not fitted"), QString::fromStdString(note.s));
    if (!cards.empty())
    {
        cards.resize(std::min<size_t>(cards.size(), 3));
        tiles.push_back({tr("Cards"), QString::number(Items(report, "slots").size()), cards});
    }

    // Two columns of tiles under the card
    const qreal top = head.bottom() + gap;
    const int rowsOfTiles = static_cast<int>((tiles.size() + 1) / 2);
    if (rowsOfTiles == 0)
        return;
    const qreal width = (head.width() - gap) / 2;
    const qreal height = std::min<qreal>(220 * s, (area.bottom() - 24 * s - top - gap * (rowsOfTiles - 1)) / rowsOfTiles);
    for (size_t n = 0; n < tiles.size(); ++n)
    {
        const Tile& tile = tiles[n];
        const QRectF box(head.left() + static_cast<qreal>(n % 2) * (width + gap),
                         top + static_cast<qreal>(n / 2) * (height + gap), width, height);
        card(box);
        const QRectF inner = box.adjusted(20 * s, 14 * s, -20 * s, -12 * s);
        draw(QRectF(inner.left(), inner.top(), inner.width(), 22 * s), 15, kDim, Qt::AlignVCenter | Qt::AlignLeft,
             tile.caption.toUpper(), true);
        draw(QRectF(inner.left(), inner.top() + 26 * s, inner.width() * (tile.graph ? 0.5 : 1.0), 38 * s), 28, kText,
             Qt::AlignVCenter | Qt::AlignLeft, tile.big);
        if (tile.graph && _trafficRates.size() >= 2)
        {
            // The rate over the last half minute, scaled to its own peak
            const QRectF graph(inner.left() + inner.width() * 0.52, inner.top() + 26 * s, inner.width() * 0.48, 40 * s);
            const double peak = std::max(1.0, *std::max_element(_trafficRates.begin(), _trafficRates.end()));
            QPainterPath path;
            path.moveTo(graph.left(), graph.bottom());
            for (size_t i = 0; i < _trafficRates.size(); ++i)
                path.lineTo(graph.left() + graph.width() * static_cast<qreal>(i) / static_cast<qreal>(_trafficRates.size() - 1),
                            graph.bottom() - graph.height() * _trafficRates[i] / peak);
            path.lineTo(graph.right(), graph.bottom());
            QColor fill = kAccent;
            fill.setAlpha(70);
            p.setPen(QPen(kAccent, 2 * s));
            p.setBrush(fill);
            p.drawPath(path);
        }
        qreal y = inner.top() + 74 * s;
        for (const auto& [label, value] : tile.lines)
        {
            if (y + 24 * s > inner.bottom() + 4 * s)
                break;
            const QRectF line(inner.left(), y, inner.width(), 24 * s);
            draw(line, 17, kDim, Qt::AlignVCenter | Qt::AlignLeft, label);
            draw(line.adjusted(inner.width() * 0.45, 0, 0, 0), 17, kText, Qt::AlignVCenter | Qt::AlignRight, value);
            y += 28 * s;
        }
    }
}

void HandheldMenu::paintSlots(QPainter& p, const QRectF& area)
{
    const qreal s = scale();
    const qreal margin = 40 * s;
    const qreal gap = 20 * s;
    const qreal top = area.top() + 28 * s;
    const qreal tileWidth = (area.width() - 2 * margin - gap * (kSlotColumns - 1)) / kSlotColumns;
    const qreal pictureHeight = tileWidth * 3 / 4 * 0.78;
    const qreal tileHeight = pictureHeight + 44 * s;
    const qreal available = area.bottom() - top - 40 * s;
    const qreal shrink = std::min<qreal>(1.0, available / (3 * tileHeight + 2 * gap));
    QFont font = p.font();
    for (int n = 0; n < kSlots; ++n)
    {
        const Slot& slot = _slots.size() == static_cast<size_t>(kSlots) ? _slots[static_cast<size_t>(n)] : Slot{n + 1, false, 0, {}};
        const int column = n % kSlotColumns;
        const int line = n / kSlotColumns;
        const QRectF tile(area.left() + margin + column * (tileWidth + gap), top + line * (tileHeight * shrink + gap * shrink),
                          tileWidth, tileHeight * shrink);
        _hits.push_back({tile, [this, n](QPointF) {
                             _inPage = true;
                             _row = n;
                             Accept();  // A tap loads the slot, as A does
                         }});
        const QRectF picture(tile.left(), tile.top(), tile.width(), pictureHeight * shrink);
        p.setPen(Qt::NoPen);
        p.setBrush(kRow);
        p.drawRoundedRect(tile, 4 * s, 4 * s);
        if (slot.used && !slot.picture.isNull())
        {
            const QSizeF fitted = QSizeF(slot.picture.size()).scaled(picture.size(), Qt::KeepAspectRatio);
            p.drawImage(QRectF(picture.center().x() - fitted.width() / 2, picture.top(), fitted.width(), fitted.height()),
                        slot.picture);
        }
        else
        {
            p.fillRect(picture, kControl.darker(140));
            font.setPixelSize(static_cast<int>(18 * s));
            p.setFont(font);
            p.setPen(kDim);
            p.drawText(picture, Qt::AlignCenter, slot.used ? tr("No picture") : tr("Empty"));
        }
        font.setPixelSize(static_cast<int>(17 * s));
        p.setFont(font);
        p.setPen(kText);
        const QRectF caption(tile.left() + 12 * s, picture.bottom(), tile.width() - 24 * s, tile.bottom() - picture.bottom());
        p.drawText(caption, Qt::AlignVCenter | Qt::AlignLeft, tr("Slot %1").arg(slot.number));
        p.setPen(kDim);
        p.drawText(caption, Qt::AlignVCenter | Qt::AlignRight, SlotTime(slot.time));
        if (_inPage && !_hasPicker && n == _row)
        {
            p.setPen(QPen(kAccent, 3 * s));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(tile.adjusted(-2 * s, -2 * s, 2 * s, 2 * s), 5 * s, 5 * s);
        }
    }
    if (!_status.isEmpty())
    {
        font.setPixelSize(static_cast<int>(16 * s));
        p.setFont(font);
        p.setPen(kDim);
        p.drawText(QRectF(area.left() + margin, area.bottom() - 32 * s, area.width() - 2 * margin, 24 * s), Qt::AlignLeft,
                   _status);
    }
}

void HandheldMenu::paintHint(QPainter& p, qreal& x, qreal centerY, const QString& button, const QString& text)
{
    const qreal s = scale();
    const qreal radius = 14 * s;
    QFont font = p.font();
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    if (button.size() == 1)
        p.drawEllipse(QPointF(x + radius, centerY), radius, radius);
    else
        p.drawRoundedRect(QRectF(x, centerY - radius, 2.2 * radius, 2 * radius), radius * 0.6, radius * 0.6);
    const qreal width = button.size() == 1 ? 2 * radius : 2.2 * radius;
    font.setPixelSize(static_cast<int>(15 * s));
    font.setBold(true);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(QRectF(x, centerY - radius, width, 2 * radius), Qt::AlignCenter, button);
    font.setBold(false);
    font.setPixelSize(static_cast<int>(17 * s));
    p.setFont(font);
    p.setPen(kText);
    const qreal textWidth = QFontMetricsF(font).horizontalAdvance(text);
    p.drawText(QRectF(x + width + 8 * s, centerY - radius, textWidth + 2, 2 * radius), Qt::AlignVCenter | Qt::AlignLeft, text);
    // A tap on a hint is its button
    std::function<void()> press;
    if (button == QLatin1String("A"))
        press = [this] { Accept(); };
    else if (button == QLatin1String("B"))
        press = [this] { Back(); };
    else if (button == QLatin1String("X"))
        press = [this] { Option('x'); };
    else if (button == QLatin1String("Y"))
        press = [this] { Option('y'); };
    if (press)
        _hits.push_back({QRectF(x - 8 * s, centerY - 2 * radius, width + textWidth + 32 * s, 4 * radius),
                         [press](QPointF) { press(); }});
    x += width + 8 * s + textWidth + 28 * s;
}

void HandheldMenu::paintFooter(QPainter& p, const QRectF& area)
{
    const qreal s = scale();
    p.fillRect(area, kFooter);
    p.fillRect(QRectF(area.left(), area.top(), area.width(), 1 * s), kDivider);
    const qreal y = area.top() + 28 * s;
    qreal x = area.left() + 32 * s;
    paintIcon(p, QStringLiteral("menu-button"), QRectF(x, y - 13 * s, 26 * s, 26 * s), kText);
    _hits.push_back({QRectF(x - 16 * s, area.top(), 34 * s + 120 * s, area.height()), [this](QPointF) { Close(); }});
    x += 34 * s;
    QFont font = p.font();
    font.setPixelSize(static_cast<int>(17 * s));
    p.setFont(font);
    p.setPen(kText);
    p.drawText(QRectF(x, y - 14 * s, 200 * s, 28 * s), Qt::AlignVCenter | Qt::AlignLeft, tr("Close"));
    // The buttons that mean something here, from the right
    std::vector<std::pair<QString, QString>> hints;
    if (_hasBrowser)
        hints = {{QStringLiteral("A"), tr("Open")}, {QStringLiteral("B"), _browser.atRoots() ? tr("Cancel") : tr("Up")},
                 {QStringLiteral("LR"), tr("Page")}};
    else if (_hasPicker)
        hints = {{QStringLiteral("A"), tr("Choose")}, {QStringLiteral("B"), tr("Cancel")}};
    else if (!_inPage)
        hints = {{QStringLiteral("A"), tr("Open")}, {QStringLiteral("B"), tr("Close")}};
    else if (page() == Page::Slots)
        hints = {{QStringLiteral("A"), tr("Load")}, {QStringLiteral("X"), tr("Save")}, {QStringLiteral("Y"), tr("Delete")},
                 {QStringLiteral("B"), tr("Back")}};
    else
    {
        const std::vector<Row> list = rows();
        const bool adjust = _row >= 0 && _row < static_cast<int>(list.size()) && list[static_cast<size_t>(_row)].adjust;
        if (adjust)
            hints.push_back({QStringLiteral("◀▶"), tr("Change")});
        hints.push_back({QStringLiteral("A"), tr("Select")});
        hints.push_back({QStringLiteral("B"), tr("Back")});
    }
    qreal total = 0;
    for (const auto& [button, text] : hints)
        total += 2.2 * 14 * s + 8 * s + QFontMetricsF(font).horizontalAdvance(text) + 28 * s;
    qreal hx = area.right() - total;
    for (const auto& [button, text] : hints)
        paintHint(p, hx, y, button, text);
}

void HandheldMenu::paintPicker(QPainter& p)
{
    const qreal s = scale();
    p.fillRect(rect(), QColor(0, 0, 0, 140));
    const qreal rowHeight = 52 * s;
    const int visible = 9;
    const int count = static_cast<int>(_picker.items.size());
    const qreal headerHeight = (_picker.warning.isEmpty() ? 64 : 96) * s;
    const qreal panelWidth = 720 * s;
    const QRectF panel(width() / 2.0 - panelWidth / 2, height() / 2.0 - (headerHeight + visible * rowHeight + 24 * s) / 2,
                       panelWidth, headerHeight + std::min(visible, count) * rowHeight + 24 * s);
    p.setPen(Qt::NoPen);
    p.setBrush(kSidebar);
    p.drawRoundedRect(panel, 8 * s, 8 * s);
    _hits.push_back({panel, [](QPointF) {}});  // the panel itself: no Back
    _hitDragStep = rowHeight;
    QFont font = p.font();
    font.setPixelSize(static_cast<int>(24 * s));
    font.setBold(true);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(QRectF(panel.left() + 28 * s, panel.top() + 16 * s, panel.width() - 56 * s, 36 * s),
               Qt::AlignVCenter | Qt::AlignLeft, _picker.title);
    font.setBold(false);
    if (!_picker.warning.isEmpty())
    {
        paintIcon(p, QStringLiteral("warning"), QRectF(panel.left() + 28 * s, panel.top() + 58 * s, 22 * s, 22 * s),
                  QColor(0xff, 0xc8, 0x4a));
        font.setPixelSize(static_cast<int>(17 * s));
        p.setFont(font);
        p.setPen(kDim);
        const QRectF line(panel.left() + 60 * s, panel.top() + 54 * s, panel.width() - 88 * s, 30 * s);
        p.drawText(line, Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetricsF(font).elidedText(_picker.warning, Qt::ElideMiddle, line.width()));
    }
    const int first = std::clamp(_picker.row - visible / 2, 0, std::max(0, count - visible));
    font.setPixelSize(static_cast<int>(21 * s));
    p.setFont(font);
    qreal y = panel.top() + headerHeight;
    for (int n = first; n < std::min(count, first + visible); ++n)
    {
        const QRectF row(panel.left() + 16 * s, y, panel.width() - 32 * s, rowHeight - 4 * s);
        _hits.push_back({row, [this, n](QPointF) {
                             _picker.row = n;
                             Accept();
                         }});
        if (n == _picker.row)
        {
            p.setPen(QPen(kAccent, 2 * s));
            p.setBrush(QColor(0x2f, 0x35, 0x40));
            p.drawRoundedRect(row, 4 * s, 4 * s);
        }
        p.setPen(n == _picker.row ? QColor(Qt::white) : kText);
        const QRectF text = row.adjusted(20 * s, 0, -20 * s, 0);
        p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetricsF(font).elidedText(_picker.items[static_cast<size_t>(n)], Qt::ElideRight, text.width()));
        y += rowHeight;
    }
}

void HandheldMenu::paintBrowser(QPainter& p)
{
    const qreal s = scale();
    p.fillRect(rect(), QColor(0, 0, 0, 160));
    const qreal rowHeight = 50 * s;
    const qreal headerHeight = 92 * s;
    const QRectF panel(width() / 2.0 - 500 * s, height() / 2.0 - (headerHeight + kBrowserRows * rowHeight + 20 * s) / 2,
                       1000 * s, headerHeight + kBrowserRows * rowHeight + 20 * s);
    p.setPen(Qt::NoPen);
    p.setBrush(kSidebar);
    p.drawRoundedRect(panel, 8 * s, 8 * s);
    _hits.push_back({panel, [](QPointF) {}});  // the panel itself: no Back
    _hitDragStep = rowHeight;
    QFont font = p.font();
    font.setPixelSize(static_cast<int>(24 * s));
    font.setBold(true);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(QRectF(panel.left() + 28 * s, panel.top() + 14 * s, panel.width() - 56 * s, 34 * s),
               Qt::AlignVCenter | Qt::AlignLeft, _browserTitle);
    font.setBold(false);
    font.setPixelSize(static_cast<int>(17 * s));
    p.setFont(font);
    p.setPen(kDim);
    const QRectF where(panel.left() + 28 * s, panel.top() + 50 * s, panel.width() - 56 * s, 28 * s);
    const QString folder = _browser.atRoots() ? tr("Choose a place") : _browser.title();
    p.drawText(where, Qt::AlignVCenter | Qt::AlignLeft, QFontMetricsF(font).elidedText(folder, Qt::ElideLeft, where.width()));

    const std::vector<HandheldBrowser::Entry>& entries = _browser.entries();
    const int count = static_cast<int>(entries.size());
    if (count == 0)
    {
        font.setPixelSize(static_cast<int>(20 * s));
        p.setFont(font);
        p.drawText(QRectF(panel.left(), panel.top() + headerHeight, panel.width(), rowHeight), Qt::AlignCenter,
                   tr("Nothing to open here"));
        return;
    }
    const int first = std::clamp(_browserRow - kBrowserRows / 2, 0, std::max(0, count - kBrowserRows));
    qreal y = panel.top() + headerHeight;
    for (int n = first; n < std::min(count, first + kBrowserRows); ++n)
    {
        const HandheldBrowser::Entry& entry = entries[static_cast<size_t>(n)];
        const QRectF row(panel.left() + 16 * s, y, panel.width() - 32 * s, rowHeight - 4 * s);
        _hits.push_back({row, [this, n](QPointF) {
                             _browserRow = n;
                             Accept();
                         }});
        const bool focused = n == _browserRow;
        if (focused)
        {
            p.setPen(QPen(kAccent, 2 * s));
            p.setBrush(QColor(0x2f, 0x35, 0x40));
            p.drawRoundedRect(row, 4 * s, 4 * s);
        }
        paintIcon(p, BrowserIcon(entry.kind, entry.name), QRectF(row.left() + 16 * s, row.center().y() - 12 * s, 24 * s, 24 * s),
                  focused ? QColor(Qt::white) : kText);
        font.setPixelSize(static_cast<int>(16 * s));
        p.setFont(font);
        const qreal detailWidth = std::min<qreal>(QFontMetricsF(font).horizontalAdvance(entry.detail), row.width() * 0.4);
        p.setPen(kDim);
        p.drawText(QRectF(row.right() - 16 * s - detailWidth, row.top(), detailWidth, row.height()),
                   Qt::AlignVCenter | Qt::AlignRight,
                   QFontMetricsF(font).elidedText(entry.detail, Qt::ElideLeft, detailWidth));
        font.setPixelSize(static_cast<int>(20 * s));
        p.setFont(font);
        p.setPen(focused ? QColor(Qt::white) : kText);
        const QRectF name(row.left() + 54 * s, row.top(), row.width() - 54 * s - detailWidth - 32 * s, row.height());
        p.drawText(name, Qt::AlignVCenter | Qt::AlignLeft, QFontMetricsF(font).elidedText(entry.name, Qt::ElideMiddle, name.width()));
        y += rowHeight;
    }
}

// endregion </Painting>

// region <Touch>

void HandheldMenu::tapSection(int section)
{
    if (section != _section)
    {
        _section = section;
        _row = 0;
        _scroll = 0;
        if (page() == Page::Slots)
            refreshSlots();
    }
    _inPage = false;
    clampFocus();
    emitState();
}

void HandheldMenu::tapRow(int index, const QRectF& box, bool slider, QPointF at)
{
    const std::vector<Row> list = rows();
    if (index < 0 || index >= static_cast<int>(list.size()) || list[static_cast<size_t>(index)].header)
        return;
    const Row& row = list[static_cast<size_t>(index)];
    _inPage = true;
    _row = index;
    // A value row: a tap on its value side steps it (left half down, right half up); a slider: toward the tap
    if (row.adjust && !row.picker)
    {
        if (slider && at.y() > box.top() + box.height() * 0.5)
        {
            const qreal s = scale();
            const qreal left = box.left() + 64 * s;
            const qreal knob = left + (box.width() - 88 * s) * std::clamp(row.level, 0.0, 1.0);
            row.adjust(at.x() < knob ? -1 : 1);
            emitState();
            return;
        }
        if (!slider && at.x() > box.left() + box.width() * 0.5)
        {
            row.adjust(at.x() < box.left() + box.width() * 0.75 ? -1 : 1);
            emitState();
            return;
        }
    }
    Accept();
}

void HandheldMenu::touchAt(QPointF at, int phase)
{
    // phase 0 down, 1 move, 2 up: a tap is a down and an up close together; a drag moves the focus a row per row of
    // travel (natural scrolling: the finger up shows the rows below).
    // TUNING: `slop` is how far a finger may slide and still tap (raise it if taps get lost as drags); a drag's travel
    // per row is _hitDragStep (the shown list's row height, set in paintEvent / paintPicker / paintBrowser); where a
    // tap on a value row steps down or up is in tapRow (the value half of the row, split at 3/4)
    if (!_open)
        return;
    if (phase == 0)
    {
        _touchStart = at;
        _touchLast = at;
        _touchDragged = false;
        return;
    }
    const qreal slop = 12 * scale();
    if (phase == 1)
    {
        if (!_touchDragged && QLineF(at, _touchStart).length() < slop)
            return;
        _touchDragged = true;
        const qreal step = std::max<qreal>(_hitDragStep, 20);
        while (_touchLast.y() - at.y() >= step)
        {
            _touchLast.ry() -= step;
            Nav(0, 1);
        }
        while (at.y() - _touchLast.y() >= step)
        {
            _touchLast.ry() += step;
            Nav(0, -1);
        }
        return;
    }
    if (_touchDragged)
        return;
    // The topmost hit under the finger (the popup's rows were painted last)
    for (auto it = _hits.rbegin(); it != _hits.rend(); ++it)
        if (it->rect.contains(at))
        {
            const std::function<void(QPointF)> tap = it->tap;  // a tap may repaint (and clear _hits)
            tap(at);
            update();
            return;
        }
}

bool HandheldMenu::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        {
            auto* touch = static_cast<QTouchEvent*>(event);
            if (!touch->points().isEmpty())
            {
                const QPointF at = touch->points().first().position();
                touchAt(at, event->type() == QEvent::TouchBegin ? 0 : event->type() == QEvent::TouchEnd ? 2 : 1);
            }
            event->accept();
            return true;
        }
        case QEvent::TouchCancel:
            _touchDragged = true;  // no tap from a cancelled touch
            event->accept();
            return true;
        default: break;
    }
    return QWidget::event(event);
}

void HandheldMenu::mousePressEvent(QMouseEvent* event)
{
    touchAt(event->position(), 0);
    event->accept();
}

void HandheldMenu::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton)
        touchAt(event->position(), 1);
    event->accept();
}

void HandheldMenu::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        touchAt(event->position(), 2);
    event->accept();
}

// endregion </Touch>

void HandheldMenu::keyPressEvent(QKeyEvent* event)
{
    // A desktop testing the menu (UNREAL_HANDHELD=1): the arrows, Enter, Escape, X, Y, Q / E
    switch (event->key())
    {
        case Qt::Key_Up: Nav(0, -1); break;
        case Qt::Key_Down: Nav(0, 1); break;
        case Qt::Key_Left: Nav(-1, 0); break;
        case Qt::Key_Right: Nav(1, 0); break;
        case Qt::Key_Return:
        case Qt::Key_Enter: Accept(); break;
        case Qt::Key_Escape:
        case Qt::Key_Backspace: Back(); break;
        case Qt::Key_X: Option('x'); break;
        case Qt::Key_Y: Option('y'); break;
        case Qt::Key_Q: Section(-1); break;
        case Qt::Key_E: Section(1); break;
        default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}
