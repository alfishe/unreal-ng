# DONE: TTD heap breakdown crash in the Qt toolbar tooltip (2026-10-03)

Landed on branch `qt-heapbreakdown-crash` (Qt part on master via 1de1b07bc): observers read `TimeTravelManager::GetPublishedSessionInfo()`
(a snapshot the session-driving thread publishes) under a context lease; `StopRecording` parks the
machine first; `SetHistoryLimit` from another thread defers eviction to the machine's thread.

- Analysis and fix: [crash-ttd-heap-breakdown.md](crash-ttd-heap-breakdown.md)
- Tests: `core/tests/debugger/ttd/timetravelmanager_publishedinfo_test.cpp`,
  `unreal-qt/tests/qt/ttdsessionobserver_test.cpp`
- Automation surfaces (WebAPI, CLI, Lua, Python, GDB): status via `ReadSessionInfo()`, every other
  session access through a `SessionOperation` (control lock, machine parked while a session is active)
- Remaining limits (direct stepping by two control planes, outside Resume): the note's Limits section
