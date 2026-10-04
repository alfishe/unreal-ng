// The Machine menu's aboutToShow reads the host keyboard route of the instance
// the UI is bound to (HostKeyboardMenu, wired by MainWindow). Automation can
// remove that instance on its own thread at any moment; the UI unbinds one
// queued event later, so the menu must cope with a binding to an instance that
// is being removed or already released (2026-10-02 crashes: pKeyboard read
// through a null context in the aboutToShow handler; a selection-change
// adoption reading the TimeTravelManager of an instance freed under it).
//
// The menu is a real QMenu popped up on the offscreen platform: popup() emits
// aboutToShow exactly as opening it from the menu bar does.

#include <QApplication>
#include <QMenu>
#include <QMetaObject>
#include <atomic>
#include <functional>
#include <gtest/gtest.h>
#include <optional>
#include <thread>

#include "_helpers/testwaithelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatormanager.h"
#include "emulator/hostkeyboardmenu.h"

class HostKeyboardMenu_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        _menu = std::make_unique<QMenu>(QStringLiteral("Machine"));
        _menu->addAction(QStringLiteral("Reset"));
        QObject::connect(_menu.get(), &QMenu::aboutToShow, _menu.get(), [this] {
            _shows++;
            _lastState = HostKeyboardMenu::Read(&_binding);
        });
    }

    void TearDown() override
    {
        _binding.unbind();
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
    }

    /// Opens the Machine menu (aboutToShow fires) and closes it again
    void OpenMachineMenu()
    {
        _lastState.reset();
        const int before = _shows;
        _menu->popup(QPoint(0, 0));
        _menu->hide();
        ASSERT_EQ(_shows, before + 1) << "popup() must emit aboutToShow";
    }

    /// Removal from another thread, the way a WebAPI DELETE arrives
    void RemoveFromAutomationThread(const std::string& id)
    {
        std::thread remover([this, id] { _manager->RemoveEmulator(id); });
        remover.join();
    }

    EmulatorManager* _manager = nullptr;
    EmulatorBinding _binding;
    std::unique_ptr<QMenu> _menu;
    int _shows = 0;
    std::optional<HostKeyboardMenuState> _lastState;
};

TEST_F(HostKeyboardMenu_Test, NoEmulatorShowsNoRoute)
{
    ASSERT_FALSE(_binding.isBound());
    OpenMachineMenu();
    EXPECT_FALSE(_lastState.has_value());

    std::string error;
    EXPECT_FALSE(HostKeyboardMenu::Request(&_binding, QStringLiteral("auto"), error).has_value());
    EXPECT_TRUE(error.empty());
}

TEST_F(HostKeyboardMenu_Test, BoundEmulatorShowsItsRoute)
{
    auto emulator = _manager->CreateEmulator();
    ASSERT_NE(emulator, nullptr);
    _binding.bind(emulator.get());

    OpenMachineMenu();
    ASSERT_TRUE(_lastState.has_value());
    EXPECT_FALSE(_lastState->route.isEmpty());
    EXPECT_FALSE(_lastState->effective.isEmpty());

    std::string error;
    const auto requested = HostKeyboardMenu::Request(&_binding, QStringLiteral("matrix"), error);
    ASSERT_TRUE(requested.has_value());
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_EQ(requested->route, QStringLiteral("MATRIX"));
}

// The first crash: the instance is removed by automation while the UI is still
// bound to it (its unbind is a queued event that has not run yet), and the user
// opens the Machine menu in that window
TEST_F(HostKeyboardMenu_Test, InstanceRemovedWhileBoundShowsNoRoute)
{
    auto emulator = _manager->CreateEmulator();  // the UI keeps its shared_ptr, as MainWindow does
    ASSERT_NE(emulator, nullptr);
    _binding.bind(emulator.get());
    OpenMachineMenu();
    ASSERT_TRUE(_lastState.has_value());

    RemoveFromAutomationThread(emulator->GetId());
    ASSERT_TRUE(emulator->IsReleased());
    ASSERT_EQ(emulator->GetContext(), nullptr);
    ASSERT_TRUE(_binding.isBound()) << "the queued unbind has not run yet";

    OpenMachineMenu();
    EXPECT_FALSE(_lastState.has_value());

    std::string error;
    EXPECT_FALSE(HostKeyboardMenu::Request(&_binding, QStringLiteral("matrix"), error).has_value());
}

// The second crash: a selection change queues the adoption of an instance
// (holding its shared_ptr); automation removes it and creates another one
// before - or while - the queued adoption runs. The adoption follows
// MainWindow::adoptEmulator()'s rule: lease the context first, refuse when the
// lease is refused, read the instance only under the lease
TEST_F(HostKeyboardMenu_Test, InstanceRecreatedAroundQueuedAdoption)
{
    QObject receiver;
    std::atomic<int> adoptions{0};
    auto queueAdoption = [&](std::shared_ptr<Emulator> emulator, std::function<void()> whileLeased) {
        QMetaObject::invokeMethod(
            &receiver,
            [&, emulator, whileLeased] {
                const Emulator::ContextLease lease = emulator->LeaseContext();
                if (!lease)
                    return;  // removed before the adoption ran
                _binding.bind(emulator.get());
                if (whileLeased)
                    whileLeased();
                // Everything the adoption reads stays alive under the lease
                ASSERT_NE(lease->pKeyboard, nullptr);
                ASSERT_NE(lease->pTimeTravelManager, nullptr);
                lease->pTimeTravelManager->GetSessionInfo();
                adoptions++;
            },
            Qt::QueuedConnection);
    };

    // 1. Removed and recreated before the queued adoption runs: refused
    auto first = _manager->CreateEmulator();
    ASSERT_NE(first, nullptr);
    queueAdoption(first, nullptr);
    RemoveFromAutomationThread(first->GetId());
    auto second = _manager->CreateEmulator();
    ASSERT_NE(second, nullptr);
    QCoreApplication::processEvents();
    EXPECT_EQ(adoptions.load(), 0);
    EXPECT_FALSE(_binding.isBound());
    OpenMachineMenu();
    EXPECT_FALSE(_lastState.has_value());

    // 2. Removed while the adoption runs: the removal waits for the lease, so
    // the adoption completes on a live context; the menu afterwards finds the
    // instance gone instead of reading freed memory
    std::thread remover;
    std::atomic<bool> removed{false};
    queueAdoption(second, [&] {
        remover = std::thread([&] {
            _manager->RemoveEmulator(second->GetId());
            removed = true;
        });
        ASSERT_TRUE(TestWait::For([&] { return second->IsRetiring(); }));
        EXPECT_FALSE(removed.load()) << "the removal must wait for the adoption's lease";
        EXPECT_FALSE(second->IsReleased());
    });
    QCoreApplication::processEvents();
    if (remover.joinable())
        remover.join();
    EXPECT_EQ(adoptions.load(), 1);
    EXPECT_TRUE(removed.load());
    EXPECT_TRUE(second->IsReleased());
    ASSERT_TRUE(_binding.isBound()) << "bound to the removed instance until the queued unbind";

    OpenMachineMenu();
    EXPECT_FALSE(_lastState.has_value());

    // 3. A fresh instance: the menu follows it again
    auto third = _manager->CreateEmulator();
    ASSERT_NE(third, nullptr);
    queueAdoption(third, nullptr);
    QCoreApplication::processEvents();
    EXPECT_EQ(adoptions.load(), 2);
    OpenMachineMenu();
    EXPECT_TRUE(_lastState.has_value());
}
