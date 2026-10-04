# DONE: TTD heap breakdown crash in the Qt toolbar tooltip (2026-10-03)

Landed on branch `qt-heapbreakdown-crash`: observers read `TimeTravelManager::GetPublishedSessionInfo()`
(a snapshot the session-driving thread publishes) under a context lease; `StopRecording` parks the
machine first; `SetHistoryLimit` from another thread defers eviction to the machine's thread.

- Analysis and fix: [crash-ttd-heap-breakdown.md](crash-ttd-heap-breakdown.md)
- Tests: `core/tests/debugger/ttd/timetravelmanager_publishedinfo_test.cpp`,
  `unreal-qt/tests/qt/ttdsessionobserver_test.cpp`
- Open follow-up (not part of this fix): the automation surfaces' status reads (see the note's last section)
