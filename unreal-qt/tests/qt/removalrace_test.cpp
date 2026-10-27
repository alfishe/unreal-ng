// GUI windows against an instance that automation removes while they show it (BUGS.md #11,
// docs/inprogress/2026-10-10-gui-instance-lifetime). The removal runs on another thread, as a WebAPI DELETE does;
// then the window's late paths run (its refresh, a queued event, a repaint) before the GUI lets go. The window holds
// the instance, so it reads a retired machine, never freed memory (the ASan build turns a regression into a report);
// once the window lets go the instance is freed.

#include <QApplication>
#include <QMetaObject>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <thread>

#include "cardslots/slotswindow.h"
#include "debugger/devicememorydialog.h"
#include "debugger/diskfilesdialog.h"
#include "debugger/disksectordialog.h"
#include "debugger/livesourcewindow.h"
#include "debugger/pchistorydialog.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "network/trafficwindow.h"
#include "widgets/ttdwidget.h"

class RemovalRace_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = EmulatorManager::GetInstance();

    std::shared_ptr<Emulator> Create(const char* name)
    {
        auto emulator = _manager->CreateEmulatorWithModel(name, "PENTAGON", LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr);
        return emulator;
    }

    /// The WebAPI's DELETE: another thread, the GUI thread does not run meanwhile
    void RemoveFromAutomationThread(const std::string& id)
    {
        std::thread remover([this, id]() { _manager->RemoveEmulator(id); });
        remover.join();
    }

    /// The GUI thread catches up: queued events, timers due, repaints
    static void Pump()
    {
        for (int i = 0; i < 3; ++i)
            QApplication::processEvents();
    }

    /// One dialog that keeps one instance: shown, the instance removed, then the dialog's late paths, then closed
    template <typename Dialog>
    void DialogOutlivesTheRemoval(const char* name, const char* lateSlot)
    {
        auto emulator = Create(name);
        ASSERT_NE(emulator, nullptr);
        std::weak_ptr<Emulator> watch = emulator;
        {
            Dialog dialog(emulator);
            const std::string id = emulator->GetId();
            emulator.reset();   // the dialog is the GUI's only holder now
            dialog.show();
            Pump();

            RemoveFromAutomationThread(id);
            {
                // Checked for null: gcc's -Wstringop-overflow takes the unchecked lock() for a null object
                auto held = watch.lock();
                ASSERT_NE(held, nullptr) << "the dialog holds the instance";
                EXPECT_TRUE(held->IsRetired());
            }

            if (lateSlot)
                QMetaObject::invokeMethod(&dialog, lateSlot, Qt::DirectConnection);
            dialog.repaint();
            Pump();
        }
        EXPECT_TRUE(watch.expired()) << "closing the dialog frees the removed instance";
    }

    /// A window that follows the bound instance through EmulatorBinding: the queued unbind comes after the removal
    template <typename Window>
    void BoundWindowOutlivesTheRemoval(Window& window, EmulatorBinding& binding, const char* lateSlot)
    {
        auto emulator = Create("bound-window");
        ASSERT_NE(emulator, nullptr);
        std::weak_ptr<Emulator> watch = emulator;
        const std::string id = emulator->GetId();
        binding.bind(emulator);
        emulator.reset();   // the binding is the GUI's holder, as MainWindow's is
        window.show();
        Pump();

        RemoveFromAutomationThread(id);
        ASSERT_TRUE(binding.isBound()) << "the queued unbind has not run yet";
        if (lateSlot)
            QMetaObject::invokeMethod(&window, lateSlot, Qt::DirectConnection);
        window.repaint();
        Pump();

        binding.unbind();   // what MainWindow's queued destroy handler does
        Pump();
        EXPECT_TRUE(watch.expired()) << "the unbind frees the removed instance";
    }
};

TEST_F(RemovalRace_Test, DeviceMemoryDialog)
{
    DialogOutlivesTheRemoval<DeviceMemoryDialog>("removal-device-memory", nullptr);
}

TEST_F(RemovalRace_Test, DiskSectorDialog)
{
    DialogOutlivesTheRemoval<DiskSectorDialog>("removal-disk-sector", nullptr);
}

TEST_F(RemovalRace_Test, DiskFilesDialog)
{
    DialogOutlivesTheRemoval<DiskFilesDialog>("removal-disk-files", nullptr);
}

TEST_F(RemovalRace_Test, PcHistoryDialog)
{
    DialogOutlivesTheRemoval<PcHistoryDialog>("removal-pc-history", nullptr);
}

// The Live Source window keeps the instance's id, not the instance: after the removal it is freed at once and the
// window's refresh finds nothing to read
TEST_F(RemovalRace_Test, LiveSourceWindow)
{
    auto emulator = Create("removal-live-source");
    ASSERT_NE(emulator, nullptr);
    std::weak_ptr<Emulator> watch = emulator;
    const std::string id = emulator->GetId();
    LiveSourceWindow window(emulator);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    emulator.reset();
    window.show();
    Pump();

    RemoveFromAutomationThread(id);
    EXPECT_TRUE(watch.expired()) << "nobody holds it: freed by the removal";
    window.refresh();
    window.repaint();
    Pump();
}

TEST_F(RemovalRace_Test, TrafficWindow)
{
    EmulatorBinding binding;
    TrafficWindow window;
    window.setBinding(&binding);
    BoundWindowOutlivesTheRemoval(window, binding, "refresh");
}

// The TTD panel keeps the active instance it was last updated with; its buttons pressed after the removal act on a
// retired machine, which refuses: nothing runs, nothing is freed under them
TEST_F(RemovalRace_Test, TtdWidget)
{
    auto emulator = Create("removal-ttd");
    ASSERT_NE(emulator, nullptr);
    std::weak_ptr<Emulator> watch = emulator;
    const std::string id = emulator->GetId();
    TtdWidget widget;
    widget.updateState(emulator);
    emulator.reset();
    widget.show();
    Pump();

    RemoveFromAutomationThread(id);
    ASSERT_FALSE(watch.expired());
    widget.onRecordToggled();
    widget.onStepBack();
    widget.onStepForward();
    widget.onJumpStart();
    widget.onJumpEnd();
    widget.onResumeFromHere();
    widget.repaint();
    Pump();
    EXPECT_FALSE(watch.lock()->IsRunning());

    widget.updateState(nullptr);   // MainWindow's unbind
    EXPECT_TRUE(watch.expired());
}
