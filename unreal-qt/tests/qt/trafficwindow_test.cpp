// The Network traffic window (TrafficWindow, network #91 T4) on a real machine: the tap's records become rows, the
// filter and the kind pick them, the decode and hex show the selected one, and Seek here time-travels to its frame
// only inside a TTD recording.

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "base/featuremanager.h"
#include "debugger/ttd/ttdsession.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/traffic/networktraffictap.h"
#include "emulator/memory/memory.h"
#include "network/trafficwindow.h"

namespace
{
/// An ARP who-has 10.0.2.2 from 10.0.2.15
std::vector<uint8_t> ArpRequest()
{
    std::vector<uint8_t> f = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x20, 0xAF, 0x11, 0x22, 0x33, 0x08, 0x06,
                              0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0x00, 0x20, 0xAF, 0x11, 0x22, 0x33,
                              10,   0,    2,    15,   0,    0,    0,    0,    0,    0,    10,   0,    2,    2};
    return f;
}

QPushButton* Button(QWidget& w, const QString& text)
{
    for (QPushButton* b : w.findChildren<QPushButton*>())
    {
        if (b->text() == text)
            return b;
    }
    return nullptr;
}

bool TreeHas(QTreeWidget* tree, const QString& text)
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
    {
        if ((*it)->text(0).contains(text))
            return true;
    }
    return false;
}
}  // namespace

TEST(TrafficWindow_Test, RowsFilterDecodeAndSeekInsideTheRecording)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("traffic-window-test", "PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    NetworkManager* network = context->pCore->GetNetworkManager();
    ASSERT_NE(network, nullptr);
    NetworkTrafficTap& tap = network->Traffic();

    // A record before the TTD recording: listed, but not reachable
    emulator->RunNFrames(2);
    const std::vector<uint8_t> arp = ArpRequest();
    tap.Frame("isa2.eth", true, arp.data(), arp.size());
    emulator->RunNFrames(1);

    FeatureManager* features = emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    ttd::TTDSessionRef ttd(context);
    ASSERT_TRUE(ttd->StartRecording());
    emulator->RunNFrames(3);
    const uint64_t arpFrame = context->emulatorState.frame_counter;
    tap.Frame("isa2.eth", true, arp.data(), arp.size());
    emulator->RunNFrames(3);
    const uint8_t hello[] = {'h', 'i'};
    tap.Socket("zxnetusb", false, "data", 3, NetProto::Tcp, NetEndpoint{NetIp(93, 184, 216, 34), 80}, 1025, hello, 2);
    emulator->RunNFrames(3);
    ttd->GetPublishedSessionInfo();  // ask for the next publish ...
    emulator->RunNFrames(1);         // ... at this frame boundary

    EmulatorBinding binding;
    binding.bind(emulator.get());
    TrafficWindow window;
    window.setBinding(&binding);
    QMetaObject::invokeMethod(&window, "refresh");

    auto* table = window.findChild<QTableWidget*>();
    auto* filter = window.findChild<QLineEdit*>();
    auto* kind = window.findChild<QComboBox*>();
    auto* decode = window.findChild<QTreeWidget*>();
    auto* hex = window.findChild<QPlainTextEdit*>();
    QPushButton* seek = Button(window, "Seek here");
    ASSERT_TRUE(table && filter && kind && decode && hex && seek);
    ASSERT_EQ(table->rowCount(), 3);
    EXPECT_EQ(table->item(2, 2)->text(), "zxnetusb");

    // The record before the recording: decoded, no seek
    table->selectRow(0);
    EXPECT_TRUE(TreeHas(decode, "ARP request"));
    EXPECT_TRUE(hex->toPlainText().startsWith("0000  FF FF FF FF FF FF 00 20"));
    EXPECT_FALSE(seek->isEnabled()) << seek->toolTip().toStdString();

    // Inside it: Seek here ends the recording (a seek is refused while recording) and goes to the record's frame
    table->selectRow(1);
    ASSERT_TRUE(seek->isEnabled()) << seek->toolTip().toStdString();
    seek->click();
    EXPECT_FALSE(ttd->IsRecording());
    EXPECT_EQ(context->emulatorState.frame_counter, arpFrame);

    // The filter and the kind
    filter->setText("zxnetusb");
    ASSERT_EQ(table->rowCount(), 1);
    table->selectRow(0);
    EXPECT_TRUE(TreeHas(decode, "socket 3"));
    filter->clear();
    kind->setCurrentIndex(1);  // Ethernet frames
    EXPECT_EQ(table->rowCount(), 2);
    kind->setCurrentIndex(0);

    // New records arrive on the next poll; Clear empties the ring and the table
    tap.Socket("zxnetusb", true, "close", 3, NetProto::Tcp, NetEndpoint{NetIp(93, 184, 216, 34), 80}, 1025, nullptr, 0);
    QMetaObject::invokeMethod(&window, "refresh");
    EXPECT_EQ(table->rowCount(), 4);
    Button(window, "Clear")->click();
    EXPECT_EQ(table->rowCount(), 0);
    EXPECT_EQ(tap.Records({}).size(), 0u);

    binding.unbind();
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}
