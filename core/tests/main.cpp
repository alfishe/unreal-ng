#include "gtest/gtest.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/ttdv1tests.h"
#include "common/filehelper.h"
#include "emulator/memory/atm/evoflash.h"

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

/// The frozen real-time clocks (Ds12887::SetFixedTime) show the host's LOCAL civil time, and the golden pictures,
/// RAM hashes and BCD expectations were recorded in US Eastern time (2026-01-01 12:00:30 UTC reads 07:00:30). Left
/// to the host's zone the same tests fail on a UTC machine (the Linux Docker run, CI). Pin the zone for the whole
/// process. A POSIX TZ rule, not an IANA name: it works with glibc, macOS and the MSVC runtime alike
static void PinTimeZone()
{
#ifdef _WIN32
  _putenv_s("TZ", "EST5EDT");
  _tzset();
#else
  setenv("TZ", "EST5EDT", 1);
  tzset();
#endif
}

int main(int argc, char **argv)
{
  PinTimeZone();
  // The engine records, as in the application; v1's own tests select v1 (TtdBackendListener below)
  Emulator::SetDefaultTimeTravelBackend(Emulator::TimeTravelBackend::Engine);
  ::testing::InitGoogleTest(&argc, argv);

  // The settings folder (FileHelper::GetWritablePath) is this process's scratch folder: files a machine saves there
  // on its own (the ZX-Evo's flashed ROM, NeoGS FlashWrite=persist, screenshots) never reach the user's settings and
  // never leak from one test into another run; the folder goes away with the process's scratch folder
  {
    const std::string writable = TestPathHelper::GetUniqueTestScratchPath("writable");
    std::error_code ec;
    std::filesystem::create_directories(FileHelper::ToFsPath(writable), ec);
    FileHelper::SetWritablePathOverride(writable);
  }
  // A test that flashes a ZX-Evo ROM must not change the ROM the next test boots: the flash's persistence file is
  // off except in the tests of the persistence itself (EvoFlashPersist_Test)
  EvoFlash::SetPersistenceAllowed(false);

  // Sound devices (AY / TurboSound / TSFM, General Sound, MoonSound) are left
  // out of every machine unless a test opts in with a SoundCardScope (see
  // soundcardscope.h for why)
  SoundCardScope::InstallPolicy();

  // v1's own tests (the files in ttdv1tests.h) record with v1; every other test with the engine
  ::testing::UnitTest::GetInstance()->listeners().Append(new ttdtest::TtdBackendListener());

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
