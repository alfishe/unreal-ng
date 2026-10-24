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
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>

#include "_helpers/testpathhelper.h"
#include "controllers/connectorview.h"
#include "controllers/controllerpicture.h"
#include "controllers/controllertoast.h"
#include "controllers/devicetestpanel.h"
#include "controllers/hotspoteditor.h"
#include "controllers/patchcordoverlay.h"

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
    EXPECT_EQ(known.imageSet(), "user-xbox-elite-2");
    EXPECT_EQ(QJsonDocument::fromJson(QByteArray::fromStdString(known.entryText())).object().keys(),
              QStringList({"id", "images"}));
    ControllerPicture::SetUserDir(QString());
}
