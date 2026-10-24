// A removal retires an instance; its last holder frees it (BUGS.md #11, docs/inprogress/2026-10-10-gui-instance-lifetime).
// A GUI handler that runs late or a WebAPI request in flight holds a shared_ptr<Emulator>: it reads a stopped, valid
// machine instead of freed memory, and the machine refuses to run or change. Every wait in a removal is bounded.

#include <3rdparty/message-center/messagecenter.h>
#include <emulator/coordinator/machinecoordinator.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include <emulator/zxpoly/zxpolygroup.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "_helpers/testwaithelper.h"
#include "pch.h"

namespace
{
void SilentAudio(void*, int16_t*, size_t) {}
}  // namespace

class EmulatorLifetime_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = EmulatorManager::GetInstance();

    std::shared_ptr<Emulator> Create(const char* name)
    {
        auto emulator = _manager->CreateEmulatorWithModel(name, "48K", LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr);
        return emulator;
    }

    void TearDown() override
    {
        _manager->SetRemovalTimeouts(EmulatorManager::kLeaseWaitTimeout, EmulatorManager::kStopJoinTimeout,
                                     EmulatorManager::kShutdownFreeTimeout);
        _manager->SetRetiredWarningTimes(EmulatorManager::kRetiredAliveWarnAfter,
                                         EmulatorManager::kRetiredAliveWarnEvery);
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
    }
};

TEST_F(EmulatorLifetime_Test, RemovedInstanceStaysReadableWhileHeld)
{
    auto emulator = Create("held");
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    Memory* memory = emulator->GetMemory();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(memory, nullptr);

    ASSERT_TRUE(_manager->RemoveEmulator(emulator->GetId()));

    EXPECT_FALSE(_manager->HasEmulator(emulator->GetId()));
    EXPECT_TRUE(emulator->IsRetiring());
    EXPECT_TRUE(emulator->IsRetired());
    EXPECT_FALSE(emulator->IsReleased());
    EXPECT_EQ(emulator->GetState(), StateRetired);
    EXPECT_EQ(emulator->GetContext(), context);
    EXPECT_EQ(emulator->GetMemory(), memory);
    EXPECT_NE(context->pCore, nullptr);
    EXPECT_NE(context->pDebugManager, nullptr);
    EXPECT_NE(context->pMediaManager, nullptr);
    (void)memory->DirectReadFromZ80Memory(0x0000);   // the ROM, readable
    EXPECT_FALSE(emulator->LeaseContext()) << "a retired machine is not live: no lease";
}

TEST_F(EmulatorLifetime_Test, LastReferenceFrees)
{
    auto emulator = Create("freed-by-last");
    ASSERT_NE(emulator, nullptr);
    const std::weak_ptr<Emulator> watch = emulator;
    ASSERT_TRUE(_manager->RemoveEmulator(emulator->GetId()));
    EXPECT_FALSE(watch.expired());

    emulator.reset();
    EXPECT_TRUE(watch.expired()) << "the last holder's drop frees the instance";
}

TEST_F(EmulatorLifetime_Test, NobodyHoldingFreesAtRemoval)
{
    std::weak_ptr<Emulator> watch;
    std::string id;
    {
        auto emulator = Create("not-held");
        ASSERT_NE(emulator, nullptr);
        watch = emulator;
        id = emulator->GetId();
    }
    ASSERT_TRUE(_manager->RemoveEmulator(id));
    EXPECT_TRUE(watch.expired());
}

TEST_F(EmulatorLifetime_Test, RetiredInstanceRefusesExecution)
{
    auto emulator = Create("refuses-execution");
    ASSERT_NE(emulator, nullptr);
    ASSERT_TRUE(_manager->RemoveEmulator(emulator->GetId()));
    EmulatorContext* context = emulator->GetContext();
    const uint64_t frames = context->emulatorState.frame_counter;

    emulator->StartAsync();
    EXPECT_FALSE(emulator->IsRunning());
    emulator->RunNFrames(2);
    emulator->RunSingleCPUCycle(false);
    emulator->Resume();
    EXPECT_EQ(context->emulatorState.frame_counter, frames) << "a retired machine never executes";

    execution::Ticket ticket = emulator->GetArbiter().SubmitIntent(
        execution::Intent::Run, execution::Requester{execution::Origin::Internal, {}});
    EXPECT_EQ(ticket.Peek().outcome, execution::Outcome::Refused);
    EXPECT_EQ(ticket.Peek().detail, "retired");
}

TEST_F(EmulatorLifetime_Test, RetiredInstanceRefusesMachineChanges)
{
    auto emulator = Create("refuses-changes");
    ASSERT_NE(emulator, nullptr);
    ASSERT_TRUE(_manager->RemoveEmulator(emulator->GetId()));

    coordinator::MachineOp op;
    op.kind = coordinator::OpKind::Reset;
    const coordinator::OpResult result = emulator->MachineOps().Run(op);
    EXPECT_EQ(result.status, coordinator::OpStatus::Refused);
    EXPECT_EQ(result.reason, coordinator::MachineCoordinator::kRetiredReason);
    EXPECT_FALSE(emulator->LoadSnapshot("never-read.sna"));
}

TEST_F(EmulatorLifetime_Test, RetiredInstanceHasNoAudioCallback)
{
    auto emulator = Create("no-audio");
    ASSERT_NE(emulator, nullptr);
    int host = 0;
    emulator->SetAudioCallback(&host, &SilentAudio);
    ASSERT_NE(emulator->GetContext()->pAudioCallback.load(), nullptr);

    ASSERT_TRUE(_manager->RemoveEmulator(emulator->GetId()));
    EXPECT_EQ(emulator->GetContext()->pAudioCallback.load(), nullptr);
    EXPECT_EQ(emulator->GetContext()->pAudioManagerObj.load(), nullptr);
}

TEST_F(EmulatorLifetime_Test, FreeOnPlainThread)
{
    auto emulator = Create("freed-on-a-thread");
    ASSERT_NE(emulator, nullptr);
    const std::weak_ptr<Emulator> watch = emulator;
    ASSERT_TRUE(_manager->RemoveEmulator(emulator->GetId()));

    std::thread holder([last = std::move(emulator)]() mutable { last.reset(); });
    holder.join();
    EXPECT_TRUE(watch.expired());
}

// The last holder can be a message handler (the GUI's MessageCenter observer copying the instance): the free runs on
// the MessageCenter worker, where removing an observer must not wait for its own dispatch
TEST_F(EmulatorLifetime_Test, FreeOnMessageCenterWorker)
{
    auto emulator = Create("freed-on-the-worker");
    ASSERT_NE(emulator, nullptr);
    const std::weak_ptr<Emulator> watch = emulator;
    emulator->StartAsync();
    ASSERT_TRUE(_manager->RemoveEmulator(emulator->GetId()));

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    std::shared_ptr<Emulator> carried = std::move(emulator);
    std::atomic<bool> done{false};
    const uint64_t observer = mc.AddObserver("test_lifetime_free_on_worker", [&](int, Message*) {
        carried.reset();
        done = true;
    });
    mc.Post("test_lifetime_free_on_worker");
    const bool finished = TestWait::For([&] { return done.load(); }, std::chrono::seconds(5));
    mc.RemoveObserverById("test_lifetime_free_on_worker", observer);
    ASSERT_TRUE(finished) << "the free on the MessageCenter worker hung";
    EXPECT_TRUE(watch.expired());
}

// A reader thread that holds the instance (the contract) reads it in a loop while another thread removes it.
// AddressSanitizer turns a read of freed memory into a report. Boot-bound: 20 running machines, a few ms each
TEST_F(EmulatorLifetime_Test, ConcurrentReaderDuringRemove)
{
    for (int round = 0; round < 20; ++round)
    {
        SCOPED_TRACE(round);
        auto emulator = Create("read-while-removed");
        ASSERT_NE(emulator, nullptr);
        emulator->EnableTurboMode();
        emulator->StartAsync();
        const std::string id = emulator->GetId();

        std::atomic<bool> stop{false};
        std::atomic<uint64_t> reads{0};
        std::thread reader([held = emulator, &stop, &reads]() {
            while (!stop.load())
            {
                EmulatorContext* context = held->GetContext();
                if (context && context->pMemory && context->pCore)
                {
                    (void)context->pMemory->DirectReadFromZ80Memory(0x4000);
                    (void)context->emulatorState.frame_counter;
                }
                reads.fetch_add(1);
            }
        });
        EXPECT_TRUE(TestWait::For([&] { return reads.load() > 100; }));
        emulator.reset();   // the reader is the only holder from now on
        EXPECT_TRUE(_manager->RemoveEmulator(id));
        const uint64_t afterRemoval = reads.load();
        EXPECT_TRUE(TestWait::For([&] { return reads.load() > afterRemoval + 100; }));
        stop = true;
        reader.join();   // the reader's copy goes here: the free runs on the reader thread
    }
}

TEST_F(EmulatorLifetime_Test, RetiredAliveCounterReturnsToZero)
{
    const size_t before = _manager->RetiredAliveCount();
    for (int cycle = 0; cycle < 4; ++cycle)
    {
        auto emulator = Create("counted");
        ASSERT_NE(emulator, nullptr);
        ASSERT_TRUE(_manager->RemoveEmulator(emulator->GetId()));
        EXPECT_EQ(_manager->RetiredAliveCount(), before + 1);
    }
    EXPECT_EQ(_manager->RetiredAliveCount(), before);
}

// A lease held by the removing thread itself waited forever before; now the removal gives up after the timeout and
// retires: nothing is freed, the holder keeps reading
TEST_F(EmulatorLifetime_Test, LeaseWaitTimesOut)
{
    _manager->SetRemovalTimeouts(std::chrono::milliseconds(30), EmulatorManager::kStopJoinTimeout,
                                 EmulatorManager::kShutdownFreeTimeout);
    auto emulator = Create("lease-held-over-removal");
    ASSERT_NE(emulator, nullptr);
    std::optional<Emulator::ContextLease> lease(emulator->LeaseContext());
    ASSERT_TRUE(*lease);

    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(_manager->RemoveEmulator(emulator->GetId()));
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(2));
    EXPECT_TRUE(emulator->IsRetired());
    EXPECT_NE((*lease)->pMemory, nullptr);
    (void)(*lease)->pMemory->DirectReadFromZ80Memory(0x0000);

    lease.reset();
    const std::weak_ptr<Emulator> watch = emulator;
    emulator.reset();
    EXPECT_TRUE(watch.expired());
}

// An emulation thread that does not leave: the removal gives up after the timeout, retires the instance and keeps it
// for good (that thread may still use it). Never freed; the thread finishes later on its own
TEST_F(EmulatorLifetime_Test, StuckEmulationThreadIsNeverFreed)
{
    _manager->SetRemovalTimeouts(EmulatorManager::kLeaseWaitTimeout, std::chrono::milliseconds(30),
                                 EmulatorManager::kShutdownFreeTimeout);
    auto emulator = Create("stuck");
    ASSERT_NE(emulator, nullptr);
    // Owned by the hook too: the thread outlives the test body (an early ASSERT returns while it waits)
    struct Hook
    {
        std::atomic<bool> release{false};
        std::atomic<bool> inHook{false};
        std::atomic<bool> left{false};
    };
    const auto hook = std::make_shared<Hook>();
    emulator->SetThreadExitHookForTest([hook]() {
        hook->inHook = true;
        TestWait::For([&] { return hook->release.load(); }, std::chrono::seconds(10));
        hook->left = true;
    });
    emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return emulator->IsRunning(); }));

    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(_manager->RemoveEmulator(emulator->GetId()));
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(2));
    EXPECT_TRUE(hook->inHook.load());
    EXPECT_TRUE(emulator->IsRetired());

    const std::weak_ptr<Emulator> watch = emulator;
    emulator.reset();
    EXPECT_FALSE(watch.expired()) << "a stuck instance is kept by the manager";
    hook->release = true;
    EXPECT_TRUE(TestWait::For([&] { return hook->left.load(); })) << "the thread leaves the hook";
}

TEST_F(EmulatorLifetime_Test, RetiredAliveWatchdogWarns)
{
    _manager->SetRetiredWarningTimes(std::chrono::milliseconds(20), std::chrono::hours(1));
    auto emulator = Create("never-let-go");
    ASSERT_NE(emulator, nullptr);
    const uint64_t before = _manager->RetiredWarningCount();
    ASSERT_TRUE(_manager->RemoveEmulator(emulator->GetId()));
    EXPECT_TRUE(TestWait::For([&] { return _manager->RetiredWarningCount() > before; }, std::chrono::seconds(3)));
}

TEST_F(EmulatorLifetime_Test, ShutdownDoesNotWaitForHolders)
{
    _manager->SetRemovalTimeouts(EmulatorManager::kLeaseWaitTimeout, EmulatorManager::kStopJoinTimeout,
                                 std::chrono::milliseconds(10));
    auto emulator = Create("held-over-shutdown");
    ASSERT_NE(emulator, nullptr);
    const auto started = std::chrono::steady_clock::now();
    _manager->ShutdownAllEmulators();
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(2));
    EXPECT_TRUE(emulator->IsRetired());
    EXPECT_NE(emulator->GetMemory(), nullptr);
}

// Boot-bound: a group creates four instances (~70 ms)
TEST_F(EmulatorLifetime_Test, ZXPolyGroupRetiresAndFrees)
{
    std::string error;
    std::shared_ptr<Emulator> master = _manager->CreateZXPolyMachine("zxpoly-lifetime", "PENTAGON", "", &error);
    ASSERT_TRUE(master) << error;
    ZXPolyGroup* group = _manager->GetZXPolyGroup(master->GetId());
    ASSERT_NE(group, nullptr);
    const ZXPolyGroup::Status status = group->GetStatus();
    std::shared_ptr<Emulator> slave = _manager->GetEmulator(status.memberIds[2]);
    ASSERT_TRUE(slave);
    const std::weak_ptr<Emulator> masterWatch = master;
    const std::weak_ptr<Emulator> slaveWatch = slave;

    ASSERT_TRUE(_manager->RemoveEmulator(master->GetId()));
    for (const std::string& id : status.memberIds)
        EXPECT_FALSE(_manager->HasEmulator(id)) << id;
    EXPECT_TRUE(master->IsRetired());
    EXPECT_TRUE(slave->IsRetired());
    EXPECT_NE(slave->GetMemory(), nullptr);

    master.reset();
    slave.reset();
    EXPECT_TRUE(masterWatch.expired());
    EXPECT_TRUE(slaveWatch.expired());
}
