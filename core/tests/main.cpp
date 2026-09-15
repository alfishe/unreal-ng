#include "gtest/gtest.h"

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulatormanager.h"

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();

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
