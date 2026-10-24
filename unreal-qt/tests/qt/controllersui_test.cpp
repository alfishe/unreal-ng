// The controllers dialog's pieces (docs/inprogress/2026-10-17-controller-manager/ui-ux.md §2-§3, §8): the connector
// names, colours and the status-bar connector LED; the patch cords between the panes; the test panel; the toasts; the
// hotspot editor's photo set and its devices.user.json entry, read back by the picture. With
// UNREAL_CONTROLLERS_UI_PNG set to a folder the tests leave their grabs there for a look

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include "_helpers/testpathhelper.h"
#include "controllers/connectorcard.h"
#include "controllers/connectorview.h"
#include "controllers/controllerpicture.h"
#include "controllers/controllertoast.h"
#include "controllers/devicetestpanel.h"
#include "controllers/hotspoteditor.h"
#include "controllers/learnwizard.h"
#include "controllers/patchcordoverlay.h"
#include "devicedatabase.h"
#include "fake/fakebackend.h"
#include "hostinputcontrol.h"
#include "hostinputservice.h"

using namespace controllers;

namespace
{
void Grab(QWidget& widget, const char* name)
{
    if (const char* dir = std::getenv("UNREAL_CONTROLLERS_UI_PNG"))
        widget.grab().save(QDir(dir).filePath(name));
}

Connector Plugged(const std::string& id, ConnectorKind kind, RetroDeviceKind device, uint8_t sinclairPort = 1)
{
    Connector connector;
    connector.id = id;
    connector.kind = kind;
    connector.device = device;
    connector.options.sinclairPort = sinclairPort;
    return connector;
}

/// The Qt client's HostControllers for the learn verb alone: the devices, the service, a user directory
class LearnOwner : public IHostInputOwner
{
public:
    explicit LearnOwner(const std::string& userDir) : _userDir(userDir)
    {
    }
    ProfileStore& Profiles() override
    {
        return _store;
    }
    ProfileContext Context() const override
    {
        return _context;
    }
    const ResolvedProfiles& Resolved() const override
    {
        return _resolved;
    }
    std::string UserProfileDir() const override
    {
        return _userDir;
    }
    std::string ManualProfile() const override
    {
        return {};
    }
    void SetManualProfile(const std::string&) override
    {
    }
    void Reapply() override
    {
    }
    HostInputService* Input() override
    {
        return input;
    }
    ProfileContext& context()
    {
        return _context;
    }
    HostInputService* input = nullptr;

private:
    ProfileStore _store;
    ProfileContext _context;
    ResolvedProfiles _resolved;
    std::string _userDir;
};

QString ShippedControllers()
{
    return QString::fromStdString((TestPathHelper::FindProjectRoot() / "data" / "controllers").string());
}
}  // namespace

/// Each connector by its name, port, colour and tag; the held controls in words
TEST(ControllersUi_Test, ConnectorNamesTagsAndHeldControls)
{
    const Connector kempston = Plugged("kempston", ConnectorKind::Kempston, RetroDeviceKind::KempstonJoystick);
    const Connector sinclair2 =
        Plugged("keyjoy.1", ConnectorKind::KeyboardJoystick, RetroDeviceKind::SinclairJoystick, 2);
    EXPECT_EQ(connectorview::DisplayName(kempston).toStdString(), "Kempston");
    EXPECT_EQ(connectorview::PortText(kempston).toStdString(), "#1F");
    EXPECT_EQ(connectorview::StyleFor(kempston).tag.toStdString(), "K");
    EXPECT_EQ(connectorview::StyleFor(sinclair2).tag.toStdString(), "S2");
    EXPECT_EQ(connectorview::PortText(sinclair2).toStdString(), "keys 1-5");
    EXPECT_NE(connectorview::StyleFor(kempston).colour, connectorview::StyleFor(sinclair2).colour);

    EXPECT_EQ(connectorview::HeldText(RetroDeviceKind::KempstonJoystick, kUp | kFire).toStdString(), "up, fire");
    EXPECT_EQ(connectorview::HeldText(RetroDeviceKind::KempstonJoystick, 0).toStdString(), "—");
    // The MD pad's buttons by their names: bit 4 is B, bit 7 START
    EXPECT_EQ(connectorview::HeldText(RetroDeviceKind::Md6Pad, (1u << 4) | (1u << 7) | kLeft).toStdString(),
              "left, B, START");
}

/// The connector LED: hidden with nothing plugged, lit while one is held, a line per plugged connector naming the
/// host devices that drive it
TEST(ControllersUi_Test, ConnectorLedNamesEveryConnectorAndItsDrivers)
{
    std::vector<Connector> connectors = {Plugged("kempston", ConnectorKind::Kempston, RetroDeviceKind::None),
                                         Plugged("keyjoy.1", ConnectorKind::KeyboardJoystick, RetroDeviceKind::None)};
    EXPECT_FALSE(connectorview::DescribeLed(connectors, {0, 0}, {}).visible);

    connectors[0].device = RetroDeviceKind::KempstonJoystick;
    connectors[1].device = RetroDeviceKind::CursorJoystick;
    const connectorview::Led idle = connectorview::DescribeLed(connectors, {0, 0}, {{"kempston", {"Xbox Elite 2"}}});
    EXPECT_TRUE(idle.visible);
    EXPECT_FALSE(idle.active);
    EXPECT_TRUE(idle.toolTip.contains("Kempston #1F: — ← Xbox Elite 2")) << idle.toolTip.toStdString();
    EXPECT_TRUE(idle.toolTip.contains("Keys socket 1 keys 5-8, 0: —")) << idle.toolTip.toStdString();

    const connectorview::Led held = connectorview::DescribeLed(connectors, {kFire, 0}, {});
    EXPECT_TRUE(held.active);
    EXPECT_TRUE(held.toolTip.contains("Kempston #1F: fire")) << held.toolTip.toStdString();
}

/// The cords leave the device list's right edge, pass behind the picture (its left and right edges at one height)
/// and end on the connector row's left edge; the dialog's three panes as a splitter of the same widgets
TEST(ControllersUi_Test, PatchCordsRunThroughTheGuttersBetweenThePanes)
{
    QWidget window;
    window.resize(1100, 520);
    auto* layout = new QVBoxLayout(&window);
    auto* splitter = new QSplitter(&window);
    splitter->setHandleWidth(26);
    layout->addWidget(splitter);
    auto* devices = new QListWidget(splitter);
    new QListWidgetItem("Xbox Elite Wireless Controller Series 2\nxbox-elite-2 · iohid", devices);
    new QListWidgetItem("8BitDo SN30 Pro\n8bitdo-sn30-pro · iohid", devices);
    devices->setCurrentRow(0);
    auto* centre = new QWidget(splitter);
    auto* centreLayout = new QVBoxLayout(centre);
    centreLayout->setContentsMargins(0, 0, 0, 0);
    ControllerPicture::SetUserDir(ShippedControllers());
    auto* picture = new ControllerPicture(centre);
    ASSERT_TRUE(picture->load("generic-xbox-like"));
    centreLayout->addWidget(picture);
    auto* table = new QTableWidget(0, 3, splitter);
    table->setHorizontalHeaderLabels({"Connector", "Device", "Held"});
    table->verticalHeader()->hide();
    const std::vector<Connector> connectors = {
        Plugged("kempston", ConnectorKind::Kempston, RetroDeviceKind::KempstonJoystick),
        Plugged("sinclair-1", ConnectorKind::KeyboardJoystick, RetroDeviceKind::SinclairJoystick),
        Plugged("keyjoy.1", ConnectorKind::KeyboardJoystick, RetroDeviceKind::CursorJoystick)};
    for (const Connector& connector : connectors)
    {
        const int row = table->rowCount();
        table->insertRow(row);
        const connectorview::Style style = connectorview::StyleFor(connector);
        table->setItem(row, 0, new QTableWidgetItem(style.tag + "  " + connectorview::DisplayName(connector)));
        table->setItem(row, 1, new QTableWidgetItem(QString::fromLatin1(RetroDeviceKindName(connector.device))));
        table->setItem(row, 2, new QTableWidgetItem(connectorview::HeldText(connector.device, row == 0 ? kFire : 0)));
    }
    table->resizeColumnToContents(0);
    splitter->setSizes({260, 560, 280});
    auto* overlay = new PatchCordOverlay(&window);
    window.show();
    QApplication::processEvents();
    overlay->setGeometry(window.rect());
    overlay->raise();

    // The first pad on the Kempston (it sends: the cord pulses), the second on Sinclair 1
    std::vector<PatchCordOverlay::Cord> cords;
    for (int device = 0; device < 2; ++device)
    {
        PatchCordOverlay::Cord cord;
        cord.from = PatchCordOverlay::RowAnchor(&window, devices, devices->visualItemRect(devices->item(device)), true);
        cord.to = PatchCordOverlay::RowAnchor(&window, table, table->visualItemRect(table->item(device, 0)), false);
        const connectorview::Style style = connectorview::StyleFor(connectors[static_cast<size_t>(device)]);
        cord.colour = style.colour;
        cord.tag = style.tag;
        cord.selected = device == 0;
        cord.live = device == 0;
        cords.push_back(cord);
    }
    const QRectF passage(picture->mapTo(&window, QPoint(0, 0)), QSizeF(picture->size()));
    overlay->setCords(cords, passage);

    const double listRight = devices->mapTo(&window, QPoint(devices->width(), 0)).x();
    const double tableLeft = table->mapTo(&window, QPoint(0, 0)).x();
    EXPECT_DOUBLE_EQ(cords[0].from.x(), listRight);
    EXPECT_DOUBLE_EQ(cords[0].to.x(), tableLeft);
    for (size_t i = 0; i < cords.size(); ++i)
    {
        const std::vector<QPainterPath> paths = overlay->pathsOf(i);
        ASSERT_EQ(paths.size(), 2u) << "a cord passes behind the picture in two runs";
        EXPECT_DOUBLE_EQ(paths[0].currentPosition().x(), passage.left());
        EXPECT_DOUBLE_EQ(paths[1].elementAt(0).x, passage.right());
        EXPECT_DOUBLE_EQ(paths[0].currentPosition().y(), paths[1].elementAt(0).y);
        // Both runs stay in their gutter: nothing is drawn over the panes
        EXPECT_LE(paths[0].boundingRect().right(), passage.left() + 0.5);
        EXPECT_GE(paths[1].boundingRect().left(), passage.right() - 0.5);
    }
    EXPECT_NE(overlay->pathsOf(0)[0].currentPosition().y(), overlay->pathsOf(1)[0].currentPosition().y())
        << "two cords pass the picture apart";

    // A cord with no picture between its ends is one run
    overlay->setCords(cords, QRectF());
    EXPECT_EQ(overlay->pathsOf(0).size(), 1u);
    overlay->setCords(cords, passage);
    Grab(window, "patch-cords.png");
    ControllerPicture::SetUserDir(QString());
}

/// The test panel: the sticks and triggers the database names, the buttons in the vocabulary's order, a trigger's
/// highest value this session (an Elite's hair-trigger stop)
TEST(ControllersUi_Test, TestPanelShowsTheDeviceControlsAndTriggerTravel)
{
    DeviceTestPanel panel;
    panel.resize(720, 160);
    panel.setDevice("bt:045e:0b22:1",
                    {"south", "east", "west", "north", "dpad.*", "lb", "rb", "lt", "rt", "lstick.*", "rstick.*", "view",
                     "menu", "guide", "paddle.p1"},
                    "iohid · xbox-elite-2");
    EXPECT_TRUE(panel.hasStick("lstick"));
    EXPECT_TRUE(panel.hasStick("rstick"));
    const auto lamps = panel.lamps();
    ASSERT_FALSE(lamps.empty());
    EXPECT_EQ(lamps.front().first, "south");
    EXPECT_NE(std::find_if(lamps.begin(), lamps.end(), [](const auto& lamp) { return lamp.first == "paddle.p1"; }),
              lamps.end());

    panel.setLive({{"south", 1.0f}, {"rt", 0.61f}, {"lstick.x", 0.7f}, {"lstick.y", -0.2f}});
    EXPECT_TRUE(panel.lamps().front().second);
    panel.setLive({{"rt", 0.3f}});
    EXPECT_FLOAT_EQ(panel.triggerMax("rt"), 0.61f) << "the highest value stays";
    EXPECT_FALSE(panel.lamps().front().second);
    panel.setLive({{"rt", 0.61f}, {"lt", 0.2f}, {"lstick.x", 0.7f}, {"lstick.y", -0.2f}, {"north", 1.0f}});
    panel.show();
    Grab(panel, "test-panel.png");

    // A new device starts over
    panel.setDevice("usb:054c:0ce6:1", {"south", "lt"}, "");
    EXPECT_EQ(panel.endpointRows(), 0);
    EXPECT_FLOAT_EQ(panel.triggerMax("rt"), 0.0f);
    EXPECT_FALSE(panel.hasStick("lstick"));
}

/// One toast, three kinds: the auto profile's Keep / Change…, a first connection's Open controllers, a disconnect's ×
TEST(ControllersUi_Test, ToastKindsShowTheirButtons)
{
    QWidget owner;
    owner.resize(800, 600);
    ControllerToast toast(&owner);
    const auto visible = [&toast](const QString& text) {
        for (QPushButton* button : toast.findChildren<QPushButton*>())
            if (button->text() == text)
                return button->isVisibleTo(&toast);
        return false;
    };
    toast.showText("Xbox Elite 2 connected → Kempston", QRect(0, 0, 800, 600), ControllerToast::Kind::Connected);
    EXPECT_TRUE(visible("Open controllers"));
    EXPECT_FALSE(visible("Keep"));
    EXPECT_TRUE(toast.text().contains("connected → Kempston"));
    bool opened = false;
    QObject::connect(&toast, &ControllerToast::changeRequested, [&opened] { opened = true; });
    for (QPushButton* button : toast.findChildren<QPushButton*>())
        if (button->text() == "Open controllers")
            button->click();
    EXPECT_TRUE(opened);
    EXPECT_FALSE(toast.isVisible());

    toast.showText("Xbox Elite 2 disconnected: paused", QRect(0, 0, 800, 600), ControllerToast::Kind::Disconnected);
    EXPECT_FALSE(visible("Open controllers"));
    EXPECT_FALSE(visible("Keep"));
    Grab(toast, "toast-disconnected.png");
    toast.showText("Kempston on kempston (port reads)", QRect(0, 0, 800, 600));
    EXPECT_TRUE(visible("Keep"));
    EXPECT_TRUE(visible("Change…"));
    EXPECT_FALSE(visible("Open controllers"));
    EXPECT_FALSE(visible("Learn…"));

    // An unknown pad's first connection: the learn toast offers the wizard for that device
    toast.showText("USB Gamepad connected: the emulator does not know this pad", QRect(0, 0, 800, 600),
                   ControllerToast::Kind::Learn, "usb:1234:5678:0100:s1");
    EXPECT_TRUE(visible("Learn…"));
    EXPECT_FALSE(visible("Keep"));
    EXPECT_FALSE(visible("Open controllers"));
    Grab(toast, "toast-learn.png");
    QString learnFor;
    QObject::connect(&toast, &ControllerToast::learnRequested, [&learnFor](const QString& device) { learnFor = device; });
    for (QPushButton* button : toast.findChildren<QPushButton*>())
        if (button->text() == "Learn…")
            button->click();
    EXPECT_EQ(learnFor.toStdString(), "usb:1234:5678:0100:s1");
    EXPECT_FALSE(toast.isVisible());
}

/// The hotspot editor: a photo, controls placed by clicks (the callout side from the x), press-to-select, Save writes
/// the photo set and records a device entry of its own for a device the database does not know; the picture reads
/// the user's set before the shipped ones
TEST(ControllersUi_Test, HotspotEditorSavesAPhotoSetThePictureReads)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    std::string recorded;
    HotspotEditor::Hooks hooks;
    hooks.userDir = dir.path();
    hooks.save = [&recorded](const std::string& entry, QString&) {
        recorded = entry;
        return true;
    };
    ControllerPicture::SetUserDir(QString());
    // No picture to start from: every control starts unplaced
    HotspotEditor editor({"usb:2dc8:abcd:1", "generic", "Mystery Pad", "", {}}, hooks);
    EXPECT_EQ(editor.entryFamily(), "user-2dc8abcd");
    EXPECT_EQ(editor.imageSet(), "user-2dc8abcd");

    QString error;
    EXPECT_FALSE(editor.save(error)) << "no photo yet";
    QImage photo(800, 400, QImage::Format_ARGB32);  // 2:1: letterboxed into the 4:3 canvas
    photo.fill(QColor(60, 64, 72));
    {
        QPainter painter(&photo);
        painter.setBrush(QColor(200, 200, 205));
        painter.drawEllipse(QPoint(600, 200), 40, 40);
    }
    editor.setPhoto(photo);
    editor.selectControl("south");
    editor.placeAt(QPointF(1500, 760));
    const auto south = std::find_if(editor.hotspots().begin(), editor.hotspots().end(),
                                    [](const HotspotEditor::Hotspot& spot) { return spot.control == "south"; });
    ASSERT_NE(south, editor.hotspots().end());
    EXPECT_TRUE(south->placed);
    EXPECT_FALSE(south->left) << "right of the middle: the callout goes right";
    EXPECT_NE(editor.selectedControl(), "south") << "on to the next control to place";

    // Press-to-select: the pad's stick picks the stick's row
    editor.setLive({{"lstick.y", -0.9f}});
    EXPECT_EQ(editor.selectedControl(), "lstick.x");
    editor.placeAt(QPointF(500, 700));

    ASSERT_TRUE(editor.save(error)) << error.toStdString();
    const QString set = dir.filePath("images/user-2dc8abcd/");
    EXPECT_TRUE(QFile::exists(set + "front.png"));
    QFile map(set + "front.hotspots.json");
    ASSERT_TRUE(map.open(QIODevice::ReadOnly));
    const QJsonObject doc = QJsonDocument::fromJson(map.readAll()).object();
    EXPECT_EQ(doc.value("size").toArray().at(0).toInt(), 2048);
    EXPECT_EQ(doc.value("controls").toArray().size(), 2);
    const QJsonObject first = doc.value("controls").toArray().at(0).toObject();
    EXPECT_EQ(first.value("control").toString().toStdString(), "south");
    EXPECT_EQ(first.value("callout").toString().toStdString(), "right");
    EXPECT_EQ(first.value("shape").toObject().value("circle").toArray().at(0).toInt(), 1500);

    const QJsonObject entry = QJsonDocument::fromJson(QByteArray::fromStdString(recorded)).object();
    EXPECT_EQ(entry.value("id").toString().toStdString(), "user-2dc8abcd");
    EXPECT_EQ(entry.value("images").toString().toStdString(), "user-2dc8abcd");
    EXPECT_EQ(entry.value("match").toArray().at(0).toObject().value("vid").toString().toStdString(), "2dc8");
    EXPECT_FALSE(entry.value("controls").toArray().isEmpty());

    // The picture finds the user's set and draws the photo
    ControllerPicture::SetUserDir(dir.path());
    EXPECT_EQ(ControllerPicture::ImageSetDir("user-2dc8abcd"), set);
    ControllerPicture picture;
    picture.resize(800, 360);
    ASSERT_TRUE(picture.load("user-2dc8abcd"));
    EXPECT_FALSE(picture.pictureRect().isEmpty());
    picture.setLive({{"south", 1.0f}});
    picture.show();
    Grab(picture, "user-photo-picture.png");
    editor.show();
    Grab(editor, "hotspot-editor.png");

    // A known family keeps its id and only names the set
    HotspotEditor known({"bt:045e:0b22:1", "xbox-elite-2", "Elite", "", {"south"}}, hooks);
    EXPECT_EQ(known.entryFamily(), "xbox-elite-2");
    EXPECT_EQ(known.imageSet(), "xbox-elite-2") << "images/<device id>/: the user's set is read before the shipped one";
    EXPECT_EQ(QJsonDocument::fromJson(QByteArray::fromStdString(known.entryText())).object().keys(),
              QStringList({"id", "images"}));
    ControllerPicture::SetUserDir(QString());
}

/// A connector card: its retro picture lit live in the connector's colour, the ZX key strip of a Sinclair or Cursor
/// joystick, the plugged devices as chips; a drop it refuses shows the reason and plugs nothing, one it takes asks to
/// plug
TEST(ControllersUi_Test, ConnectorCardShowsTheRetroDeviceAndRefusesWithTheReason)
{
    ControllerPicture::SetUserDir(ShippedControllers());
    Connector sinclair = Plugged("sinclair-1", ConnectorKind::KeyboardJoystick, RetroDeviceKind::SinclairJoystick);
    sinclair.accepts = {RetroDeviceKind::SinclairJoystick, RetroDeviceKind::CursorJoystick};
    ConnectorCard card;
    card.setConnector(sinclair);
    EXPECT_FALSE(card.picture()->empty()) << "the sinclair-joystick set";
    const std::vector<connectorview::StripKey> strip = connectorview::KeyStrip(sinclair);
    ASSERT_EQ(strip.size(), 5u);
    EXPECT_EQ(strip.front().key, "6");
    EXPECT_EQ(strip.back().key, "0");
    Connector two = sinclair;
    two.options.sinclairPort = 2;
    EXPECT_EQ(connectorview::KeyStrip(two).front().key, "1");
    Connector cursor = sinclair;
    cursor.device = RetroDeviceKind::CursorJoystick;
    EXPECT_EQ(connectorview::KeyStrip(cursor).front().key, "5");
    EXPECT_TRUE(connectorview::KeyStrip(Plugged("kempston", ConnectorKind::Kempston, RetroDeviceKind::KempstonJoystick))
                    .empty());
    EXPECT_EQ(connectorview::LiveOf(RetroDeviceKind::Md6Pad, 0x40).count("a"), 1u) << "the pad's A is bit 6";
    EXPECT_EQ(connectorview::LiveOf(RetroDeviceKind::KempstonJoystick, kUp | kFire).size(), 2u);

    std::vector<std::pair<QString, QString>> dropped;
    QObject::connect(&card, &ConnectorCard::deviceDropped,
                     [&dropped](const QString& device, const QString& connector) { dropped.emplace_back(device, connector); });
    card.refusalFor = [](const std::string& device) {
        return device == "keyboard" ? QString("Keyboard does not plug: its bindings name their targets") : QString();
    };
    EXPECT_FALSE(card.dropDevice("keyboard"));
    EXPECT_TRUE(dropped.empty());
    EXPECT_FALSE(card.hoverRefusal().isEmpty());
    EXPECT_TRUE(card.dropDevice("usb:2dc8:6101:1"));
    ASSERT_EQ(dropped.size(), 1u);
    EXPECT_EQ(dropped.front().second, "sinclair-1");

    // The cards a dialog column holds, for a look
    QWidget column;
    auto* layout = new QVBoxLayout(&column);
    Connector kempston = Plugged("kempston", ConnectorKind::Kempston, RetroDeviceKind::KempstonJoystick);
    kempston.fixed = true;
    kempston.accepts = {RetroDeviceKind::KempstonJoystick};
    Connector left = Plugged("next.left", ConnectorKind::NextDb9, RetroDeviceKind::Md6Pad);
    left.alias = "left";
    left.fixed = true;
    left.accepts = {RetroDeviceKind::Md6Pad};
    Connector empty = Plugged("keyjoy.1", ConnectorKind::KeyboardJoystick, RetroDeviceKind::None);
    empty.accepts = {RetroDeviceKind::SinclairJoystick, RetroDeviceKind::CursorJoystick, RetroDeviceKind::KeysJoystick};
    const std::vector<std::pair<Connector, uint16_t>> cards = {
        {kempston, kUp | kFire}, {sinclair, kLeft}, {left, 0x40}, {empty, 0}};
    for (const auto& [connector, held] : cards)
    {
        auto* shown = new ConnectorCard(&column);
        shown->setConnector(connector);
        shown->setHeld(held);
        if (connector.id == "kempston")
            shown->setDrivers({{"usb:045e:0b00:1", "Xbox Elite Wireless Controller Series 2"}});
        if (connector.id == "sinclair-1")
            shown->setDrivers({{"usb:2dc8:6101:1", "8BitDo SN30 Pro"}});
        layout->addWidget(shown);
    }
    layout->addStretch(1);  // as the dialog's column: the cards keep their own height
    column.resize(320, 760);
    column.show();
    QApplication::processEvents();
    Grab(column, "connector-cards.png");
    ControllerPicture::SetUserDir(QString());
}

/// The cord's tag at its connector end, left of it, on the cord's height
TEST(ControllersUi_Test, PatchCordTagSitsAtTheConnectorEnd)
{
    PatchCordOverlay::Cord cord;
    cord.from = QPointF(10, 40);
    cord.to = QPointF(300, 120);
    cord.tag = "S1";
    const QRectF tag = PatchCordOverlay::TagRect(cord, QFontMetrics(QApplication::font()));
    EXPECT_LE(tag.right(), cord.to.x());
    EXPECT_GE(tag.right(), cord.to.x() - 2.0);
    EXPECT_DOUBLE_EQ(tag.center().y(), cord.to.y());
}

/// Swap joystick ports: two pads trade their connectors, one pad moves on to the next joystick
TEST(ControllersUi_Test, SwapPortsTradesOrMovesOn)
{
    const std::vector<std::string> joysticks = {"kempston", "sinclair-1", "sinclair-2"};
    using Pads = std::vector<std::pair<std::string, std::string>>;
    EXPECT_EQ(connectorview::SwapPorts(Pads{{"a", "kempston"}, {"b", "sinclair-1"}}, joysticks),
              (Pads{{"a", "sinclair-1"}, {"b", "kempston"}}));
    EXPECT_EQ(connectorview::SwapPorts(Pads{{"a", "sinclair-2"}}, joysticks), (Pads{{"a", "kempston"}}));
    EXPECT_EQ(connectorview::SwapPorts(Pads{{"a", "kempston"}, {"b", "kempston"}}, joysticks), Pads{});
    EXPECT_EQ(connectorview::SwapPorts(Pads{{"a", ""}}, joysticks), Pads{});
    EXPECT_EQ(connectorview::SwapPorts(Pads{{"a", "kempston"}}, {"kempston"}), Pads{});
}

/// The picture reads ellipses and polygons, hit-tests them by their shape, and has a view per map; compact, it
/// draws the picture alone
TEST(ControllersUi_Test, PictureReadsEllipsesPolygonsAndViews)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString set = dir.filePath("images/shapes/");
    ASSERT_TRUE(QDir().mkpath(set));
    QImage photo(400, 300, QImage::Format_ARGB32);
    photo.fill(QColor(70, 72, 80));
    ASSERT_TRUE(photo.save(set + "front.png"));
    ASSERT_TRUE(photo.save(set + "back.png"));
    const auto write = [&set](const char* view, const QJsonArray& controls) {
        QFile file(set + view + ".hotspots.json");
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"schema", 1}, {"size", QJsonArray{2048, 1536}}, {"controls", controls}})
                       .toJson());
    };
    write("front", QJsonArray{QJsonObject{{"control", "south"},
                                          {"shape", QJsonObject{{"ellipse", QJsonArray{1024, 768, 300, 100}}}},
                                          {"anchor", QJsonArray{1024, 768}},
                                          {"callout", "right"}}});
    write("back", QJsonArray{QJsonObject{
                      {"control", "paddle.p1"},
                      {"shape", QJsonObject{{"polygon", QJsonArray{QJsonArray{200, 200}, QJsonArray{800, 200},
                                                                   QJsonArray{200, 800}}}}},
                      {"anchor", QJsonArray{300, 300}},
                      {"callout", "left"}}});
    ControllerPicture::SetUserDir(dir.path());
    EXPECT_EQ(ControllerPicture::Views("shapes"), QStringList({"front", "back"}));

    ControllerPicture picture;
    picture.setCompact(true);
    picture.resize(2048 / 8, 1536 / 8);  // the picture fills it: design units / 8
    ASSERT_TRUE(picture.load("shapes"));
    EXPECT_EQ(picture.pictureRect(), QRectF(0, 0, 256, 192));
    QString clicked;
    QObject::connect(&picture, &ControllerPicture::controlClicked, [&clicked](const QString& control) { clicked = control; });
    const auto click = [&picture](double x, double y) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(x, y), picture.mapToGlobal(QPointF(x, y)), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&picture, &press);
    };
    click(128 + 30, 96);  // inside the ellipse (rx 37.5 px, ry 12.5 px)
    EXPECT_EQ(clicked, "south");
    clicked.clear();
    click(128 + 35, 96 + 11);  // inside the ellipse's box, outside the ellipse
    EXPECT_TRUE(clicked.isEmpty()) << "an ellipse, not its bounding box";

    ASSERT_TRUE(picture.load("shapes", "back"));
    EXPECT_EQ(picture.view(), "back");
    click(30, 30);  // inside the triangle (25..100 px)
    EXPECT_EQ(clicked, "paddle.p1");
    clicked.clear();
    click(90, 90);  // past its hypotenuse
    EXPECT_TRUE(clicked.isEmpty()) << "a polygon, not its bounding box";
    EXPECT_FALSE(picture.load("shapes", "../x")) << "a view is a name, not a path";
    ControllerPicture::SetUserDir(QString());
}

/// The editor's front / back views, ellipse and polygon spots, a quarter turn and a crop, the set named by the device
/// id, and Share… with its provenance stub
TEST(ControllersUi_Test, HotspotEditorViewsShapesCropAndShare)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ControllerPicture::SetUserDir(QString());
    std::string recorded;
    HotspotEditor::Hooks hooks;
    hooks.userDir = dir.filePath("user");
    hooks.save = [&recorded](const std::string& entry, QString&) {
        recorded = entry;
        return true;
    };
    HotspotEditor editor({"usb:2dc8:abcd:1", "generic", "Mystery Pad", "", {"south", "paddle.p1"}}, hooks);
    QImage photo(800, 400, QImage::Format_ARGB32);
    photo.fill(QColor(60, 64, 72));
    editor.setPhoto(photo);
    // A quarter turn: 400 x 800; a crop of the photo's top half: 400 x 400
    editor.rotateQuarter(1);
    EXPECT_EQ(editor.photo().size(), QSize(400, 800));
    // Letterboxed: 768 x 1536 at x 640; its top half
    editor.cropTo(QRectF(640, 0, 768, 768));
    EXPECT_EQ(editor.photo().size(), QSize(400, 400));
    editor.resetCrop();
    EXPECT_EQ(editor.photo().size(), QSize(400, 800));
    editor.cropTo(QRectF(640, 0, 768, 768));

    editor.selectControl("south");
    editor.setShape(HotspotEditor::Shape::Ellipse);
    editor.placeAt(QPointF(1024, 700));
    // A turn after a placement moves everything: the view's spots go
    editor.setStraighten(2.0);
    EXPECT_TRUE(std::none_of(editor.hotspots().begin(), editor.hotspots().end(),
                             [](const HotspotEditor::Hotspot& spot) { return spot.placed; }));
    editor.setStraighten(0.0);
    editor.selectControl("south");
    editor.setShape(HotspotEditor::Shape::Ellipse);
    editor.placeAt(QPointF(1024, 700));

    // The back: its own photo, a polygon drawn corner by corner
    editor.setView("back");
    EXPECT_TRUE(editor.photo().isNull());
    editor.setPhoto(photo);
    editor.selectControl("paddle.p1");
    editor.setShape(HotspotEditor::Shape::Polygon);
    editor.placeAt(QPointF(400, 400));
    editor.placeAt(QPointF(900, 420));
    EXPECT_FALSE(editor.finishPolygon()) << "two corners are no polygon";
    editor.placeAt(QPointF(600, 900));
    editor.placeAt(QPointF(405, 402));  // near the first corner: closes it
    const auto paddle = std::find_if(editor.hotspots().begin(), editor.hotspots().end(),
                                     [](const HotspotEditor::Hotspot& spot) { return spot.control == "paddle.p1"; });
    ASSERT_NE(paddle, editor.hotspots().end());
    EXPECT_TRUE(paddle->placed);
    EXPECT_EQ(paddle->polygon.size(), 3);

    const QJsonObject front = QJsonDocument::fromJson(editor.mapText("front")).object();
    EXPECT_EQ(front.value("controls").toArray().at(0).toObject().value("shape").toObject().value("ellipse").toArray().size(),
              4);
    const QJsonObject back = QJsonDocument::fromJson(editor.mapText("back")).object();
    EXPECT_EQ(back.value("view").toString(), "back");
    EXPECT_EQ(back.value("controls").toArray().at(0).toObject().value("shape").toObject().value("polygon").toArray().size(),
              3);

    QString error;
    ASSERT_TRUE(editor.save(error)) << error.toStdString();
    const QString saved = dir.filePath("user/images/user-2dc8abcd/");
    for (const char* file : {"front.png", "front.hotspots.json", "back.png", "back.hotspots.json"})
        EXPECT_TRUE(QFile::exists(saved + file)) << file;
    EXPECT_FALSE(QFile::exists(saved + "top.hotspots.json")) << "a view without a photo is not written";
    EXPECT_EQ(QImage(saved + "front.png").size(), QSize(400, 400)) << "turned and cropped";

    ASSERT_TRUE(editor.exportBundle(dir.filePath("share"), error)) << error.toStdString();
    const QString shared = dir.filePath("share/user-2dc8abcd/");
    EXPECT_TRUE(QFile::exists(shared + "back.png"));
    QFile provenance(shared + "provenance.json");
    ASSERT_TRUE(provenance.open(QIODevice::ReadOnly));
    const QJsonObject record = QJsonDocument::fromJson(provenance.readAll()).object();
    EXPECT_EQ(record.value("family").toString(), "user-2dc8abcd");
    EXPECT_EQ(record.value("images").toArray().size(), 2);
    EXPECT_TRUE(record.value("images").toArray().at(0).toObject().contains("licence"));
    QFile entry(shared + "entry.json");
    ASSERT_TRUE(entry.open(QIODevice::ReadOnly));
    EXPECT_EQ(QJsonDocument::fromJson(entry.readAll()).object().value("id").toString(), "user-2dc8abcd");

    // Opened again on the saved set: both views come back with their shapes
    ControllerPicture::SetUserDir(hooks.userDir);
    HotspotEditor again({"usb:2dc8:abcd:1", "generic", "Mystery Pad", "user-2dc8abcd", {"south", "paddle.p1"}}, hooks);
    EXPECT_EQ(again.hotspots().front().shape, HotspotEditor::Shape::Ellipse);
    again.setView("back");
    EXPECT_FALSE(again.photo().isNull());
    EXPECT_EQ(again.hotspots().back().shape, HotspotEditor::Shape::Polygon);
    again.resize(1040, 660);
    again.show();
    QApplication::processEvents();
    Grab(again, "hotspot-editor-back.png");
    ControllerPicture::SetUserDir(QString());
}

/// The test panel's thresholds from the binding up (a profile's own dead zone), the endpoint table and the raw
/// reports with the reason when there are none
TEST(ControllersUi_Test, TestPanelThresholdsEndpointsAndRawReports)
{
    DeviceTestPanel panel;
    panel.setDevice("usb:045e:0b22:1", {"south", "lt", "rt", "lstick.*"}, "iohid · xbox-elite-2");
    EXPECT_FLOAT_EQ(panel.thresholds("lstick").deadzone, 0.15f) << "the defaults";
    DeviceTestPanel::Thresholds profile;
    profile.deadzone = 0.3f;
    profile.triggerClick = 0.2f;
    profile.fromProfile = true;
    panel.setThresholds("lstick", profile);
    panel.setThresholds("rt", profile);
    EXPECT_FLOAT_EQ(panel.thresholds("lstick").deadzone, 0.3f);
    EXPECT_TRUE(panel.thresholds("rt").fromProfile);
    panel.setEndpoints({{"iohid", "Xbox Wireless Controller", "input", "18", "xbox gip", "yes", "—", "yes", ""},
                        {"gamecontroller", "Xbox Wireless Controller", "outputs", "0", "gamecontroller", "yes", "yes",
                         "yes", "confirmed match"}});
    EXPECT_EQ(panel.endpointRows(), 2);
    panel.setRawReports({}, "The xinput backend keeps no raw reports");
    EXPECT_TRUE(panel.rawText().contains("keeps no raw reports"));
    panel.setRawReports({"00123  01 00 80 7f", "00140  01 01 80 7f"}, "");
    EXPECT_TRUE(panel.rawText().contains("01 01 80 7f"));
    panel.setRawReports({}, "");
    EXPECT_TRUE(panel.rawText().contains("01 01 80 7f")) << "the last reports stay while the pad rests";
    panel.setLive({{"lstick.x", 0.5f}, {"lstick.y", 0.1f}, {"rt", 0.4f}, {"south", 1.0f}});
    panel.resize(720, panel.sizeHint().height());
    panel.show();
    QApplication::processEvents();
    Grab(panel, "test-panel-endpoints.png");
}

/// The learn wizard follows the `learn` session: the pad's own press answers the prompt (the map grows, "release it
/// first" until it comes back), Skip, Finish saves and shows the gamecontrollerdb line; a session another plane
/// cancels closes it; a wizard closed while learning cancels the session
TEST(ControllersUi_Test, LearnWizardFollowsTheSessionTheDeviceAnswers)
{
    const std::filesystem::path root = TestPathHelper::GetUniqueTestScratchPath("learn-wizard");
    std::string error;
    ASSERT_TRUE(DeviceDatabase::SetUser("", error)) << "no user layer from an earlier test";
    LearnOwner owner((root / "profiles").string());
    HostInputControl control(owner);
    HostInputService service;
    auto owned = std::make_unique<FakeBackend>();
    FakeBackend* fake = owned.get();
    std::string reason;
    ASSERT_TRUE(service.AddBackend(std::move(owned), reason)) << reason;
    const HostDeviceInfo pad{"usb:1234:5678:0100:s1", "USB Gamepad", "generic", "generic", "fake"};
    ASSERT_TRUE(fake->Connect(pad));
    owner.context().devices = {pad};
    owner.input = &service;
    const auto call = [&control](const HostRequest& request) { return control.Execute(request); };

    {
        LearnWizard wizard(pad.id, "USB Gamepad", call);
        QString learned;
        QObject::connect(&wizard, &LearnWizard::learned, [&learned](const QString& message) { learned = message; });
        ASSERT_TRUE(wizard.start()) << wizard.statusText().toStdString();
        EXPECT_TRUE(wizard.learning());
        EXPECT_EQ(wizard.promptText().toStdString(), "Press Up");
        EXPECT_TRUE(wizard.progressText().startsWith("Control 1 of ")) << wizard.progressText().toStdString();

        ASSERT_TRUE(fake->Set(pad.id, "lstick.y", -1.0f));  // the pad itself
        wizard.refresh();
        EXPECT_EQ(wizard.mapLines(), QStringList{"dpad.up = lstick.y-"});
        ASSERT_TRUE(fake->Set(pad.id, "lstick.y", 0.0f));
        wizard.refresh();
        EXPECT_EQ(wizard.promptText().toStdString(), "Press Down");
        EXPECT_TRUE(wizard.progressText().startsWith("Control 2 of "));
        wizard.skip();
        EXPECT_TRUE(wizard.progressText().startsWith("Control 3 of ")) << "Down skipped";
        wizard.back();
        EXPECT_TRUE(wizard.progressText().startsWith("Control 2 of "));
        wizard.resize(460, 420);
        wizard.show();
        QApplication::processEvents();
        Grab(wizard, "learn-wizard.png");

        ASSERT_TRUE(wizard.finish()) << wizard.statusText().toStdString();
        EXPECT_FALSE(wizard.learning());
        EXPECT_TRUE(wizard.exportLine().startsWith("03000000341200007856000000010000,USB Gamepad,"))
            << wizard.exportLine().toStdString();
        EXPECT_TRUE(learned.contains("saved as user-12345678")) << learned.toStdString();
        EXPECT_EQ(service.ControlMap(pad.id).size(), 1u) << "the pad takes the map at once";
    }

    {
        LearnWizard wizard(pad.id, "USB Gamepad", call);
        ASSERT_TRUE(wizard.start());
        EXPECT_TRUE(control.Execute({"learn", {{"device", pad.id}, {"step", "cancel"}}}).Ok()) << "a script cancels";
        wizard.refresh();
        EXPECT_FALSE(wizard.learning());
        EXPECT_TRUE(wizard.statusText().contains("ended elsewhere")) << wizard.statusText().toStdString();

        ASSERT_TRUE(wizard.start()) << "started again";
        EXPECT_TRUE(control.Execute({"learn", {{"device", pad.id}, {"step", "sample"}, {"control", "button.4"}}}).Ok());
        wizard.refresh();
        EXPECT_EQ(wizard.mapLines(), QStringList{"dpad.up = button.4"}) << "a scripted press shows as the pad's would";
    }
    EXPECT_EQ(control.Execute({"learn", {{"device", pad.id}}}).error, "bad-request")
        << "the wizard closed while learning cancelled the session";
    DeviceDatabase::SetUser("", error);
    service.Stop();
}
