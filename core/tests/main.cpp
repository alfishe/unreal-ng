#include "gtest/gtest.h"

#include <cstdio>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulatormanager.h"
#include "_helpers/soundcardscope.h"

/// Tests share one process-wide EmulatorManager. An instance a test leaves
/// registered outlives it and changes what later tests see (sole-instance
/// real-time selection, instance lists, CLI/WebAPI "the emulator"
/// resolution) - failures that then depend on test order. Every test must
/// leave the manager as it found it; offenders are listed and fail the run.
class ManagerLeakGuard : public ::testing::EmptyTestEventListener
{
public:
  bool Leaked() const { return _leaked; }

private:
  size_t _before = 0;
  bool _leaked = false;

  void OnTestStart(const ::testing::TestInfo&) override
  {
    _before = EmulatorManager::GetInstance()->GetEmulatorIds().size();
  }

  void OnTestEnd(const ::testing::TestInfo& test) override
  {
    const size_t after = EmulatorManager::GetInstance()->GetEmulatorIds().size();
    if (after > _before)
    {
      _leaked = true;
      fprintf(stderr, "[  LEAKED  ] %s.%s left %zu emulator instance(s) in EmulatorManager\n",
              test.test_suite_name(), test.name(), after - _before);
    }
  }
};

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);

  // Sound devices (AY / TurboSound / TSFM, General Sound, MoonSound) are left
  // out of every machine unless a test opts in with a SoundCardScope (see
  // soundcardscope.h for why)
  SoundCardScope::InstallPolicy();

  auto* leakGuard = new ManagerLeakGuard();  // owned by gtest once appended
  ::testing::UnitTest::GetInstance()->listeners().Append(leakGuard);

  int result = RUN_ALL_TESTS();
  if (leakGuard->Leaked() && result == 0)
  {
    fprintf(stderr, "EmulatorManager instances leaked by tests (see [  LEAKED  ] lines)\n");
    result = 1;
  }

  // Tear down the process-global singletons BEFORE static destruction starts.
  //
  // The EmulatorManager Meyers singleton's destructor runs during static
  // destruction and posts final state-change notifications through the
  // MessageCenter worker thread. By that point suite-global observer state
  // (file-static capture vectors/mutexes in the notification tests) may
  // already be destroyed, and a dispatch into it aborts the process AFTER all
  // tests passed ("mutex lock failed: Invalid argument" / heap corruption),
  // failing the whole parallel shard. Shutting the emulators down here runs
  // the same final posts while every observer is still alive, and disposing
  // the MessageCenter joins the worker so nothing dispatches during the
  // subsequent static destruction (the later singleton destructor then finds
  // an empty emulator map and posts nothing).
  EmulatorManager::GetInstance()->ShutdownAllEmulators();
  MessageCenter::DisposeDefaultMessageCenter();

  return result;
}
