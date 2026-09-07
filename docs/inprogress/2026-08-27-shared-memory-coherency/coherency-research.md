# Shared Memory Coherency Research — Is `SyncToDisk()` Needed At All?

- **Status**: research complete; verdict — **eliminate `SyncToDisk()` entirely**
- **Date**: 2026-08-27
- **Related**:
    - Supersedes case #1 of [Throttler Adoption Analysis](../2026-08-27-throttler-adoption-cases/throttler-adoption-analysis.md)
    - Code under study: `core/src/emulator/memory/memory.cpp`, `unreal-screen-viewer/src/ScreenViewer.cpp`
- **Question**: we export ZX-Spectrum RAM via shared memory for external viewers. Today
  `Memory::SyncToDisk()` runs a full-range flush **twice per emulated frame**. Docs we have
  seen suggest inter-process coherency never requires syncing to disk. Is that true on
  Windows, macOS and Linux — can we eliminate the disk from the equation completely?

## TL;DR

**Yes — confirmed on all three platforms, with vendor citations.** The flush calls are
wasted work on every platform, and on none of them does a durable, user-visible backing
file even exist:

| Platform | Mapping primitive we use | Backing store | Coherency guarantee | Flush call effect |
|----------|--------------------------|---------------|---------------------|------------------|
| Linux | `shm_open()` + `mmap(MAP_SHARED)` | tmpfs (`/dev/shm`) — **RAM only** | automatic, documented | pointless (tmpfs has no disk file) |
| macOS | `shm_open()` + `mmap(MAP_SHARED)` | kernel-resident object — **RAM only**, no filesystem entry | automatic (POSIX semantics) | pointless (no filesystem to write back) |
| Windows | `CreateFileMappingA(INVALID_HANDLE_VALUE, ...)` + `MapViewOfFile` | **system paging file** — not a file in the file system | explicitly documented for cross-process views | pointless (no physical file to copy to) |

Consumers open the **mapping** (`OpenFileMappingW` / `shm_open` + `mmap`), never a file —
so even the theoretical durability argument has no consumer.

## What the Code Does Today

Producer — `core/src/emulator/memory/memory.cpp`:

- Windows (lines 479-528): `CreateFileMappingA(INVALID_HANDLE_VALUE, ...)` with name
  `Local\zxspectrum_memory-<id>` — the first parameter's documented meaning is
  *"backed by the system paging file instead of by a file in the file system"*.
- POSIX (lines 530-574): `shm_open("/zxspectrum_memory-<id>")` + `ftruncate` +
  `mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)`.
- `_mappedMemoryFilepath` (lines 527, 573) stores only the **object name**, not a file path.

Flush — `Memory::SyncToDisk()` (lines 639-666):

- POSIX: `__sync_synchronize()` then `msync(_memory, _memorySize, MS_SYNC | MS_INVALIDATE)`.
- Windows: `FlushViewOfFile(_memory, _memorySize)`.
- The comment above it already concedes: *"msync is for disk persistence"* — yet `MS_SYNC`
  forces **synchronous** writeback of the whole range.

Call sites — twice per emulated frame when the shared-memory feature is on:

- `core/src/emulator/cpu/core.cpp:623` (`Core::CPUFrameCycle()`)
- `core/src/emulator/mainloop.cpp:333` (`MainLoop::OnFrameEnd()`)

Consumer — `unreal-screen-viewer/src/ScreenViewer.cpp:71-101`:

- Windows: `OpenFileMappingW(FILE_MAP_READ, ...)` + `MapViewOfFile`.
- POSIX: `shm_open(O_RDONLY)` + `mmap(PROT_READ, MAP_SHARED)`.

No consumer opens the backing object as a file anywhere in the codebase.

## Platform Evidence

### Linux

1. **Coherency is automatic.** `mmap(2)`, man-pages 6.18:

   > MAP_SHARED — Share this mapping. **Updates to the mapping are visible to other
   > processes mapping the same region**, and (in the case of file-backed mappings) are
   > carried through to the underlying file. (To precisely control **when** updates are
   > carried through to the underlying file requires the use of msync(2).)

   Read carefully: `msync` controls the *timing of file writeback*, never *visibility to
   other processes*. Visibility is part of what `MAP_SHARED` means.

2. **Our object has no disk file.** `shm_open(3)`:

   > The POSIX shared memory object implementation on Linux makes use of a dedicated
   > tmpfs(5) filesystem that is normally mounted under /dev/shm.

   tmpfs lives in RAM and its contents cease to exist on unmount/reboot. There is nothing
   on disk to synchronize with.

3. **The kernel does not even need the hint.** `msync(2)`:

   > Since Linux 2.6.19, MS_ASYNC is in fact a no-op, since the kernel properly tracks
   > dirty pages and flushes them to storage as necessary.

### macOS (Darwin)

1. **Our object has no filesystem entry.** `shm_open(2)`, Darwin man page — the decisive
   quote:

   > When a new shared memory object is created it is given the owner and group
   > corresponding to the effective user and group of the calling process. **There is no
   > visible entry in the file system for the created object in this implementation.**
   > When a shared memory object is created, it persists until it is unlinked and all
   > other references are gone. **Objects do not persist across a system reboot.**

   Darwin implements POSIX shm as a kernel-resident object (RAM-backed), not as a file.
   (This is unlike some BSDs that materialize `/var/tmp` files.)

2. **`msync` targets the filesystem — which we do not have.** `msync(2)`, macOS:

   > The msync() system call writes modified whole pages back to the filesystem and
   > updates the file modification time.

   With a kernel-resident shm object there is no filesystem to write back to; the call is
   at best a no-op and at worst returns an error — and `MS_SYNC` asks it to do so
   synchronously, twice per frame.

### Windows

1. **Our section is paging-file-backed.** `CreateFileMappingA`, Microsoft Learn:

   > If hFile is INVALID_HANDLE_VALUE... CreateFileMapping creates a file mapping object
   > of a specified size that is **backed by the system paging file instead of by a file
   > in the file system**.

2. **Cross-process coherency is explicitly documented.** Same page, Remarks:

   > With one important exception, file views derived from any file mapping object that
   > is backed by the same file are coherent or identical at a specific time. **Coherency
   > is guaranteed for views within a process and for views that are mapped by different
   > processes.** The exception is related to remote files.

   Our section is local (paging-file-backed), so the exception does not apply. Viewers
   opening the same named section see emulator writes with no flush call involved.

3. **`FlushViewOfFile` exists to copy to a physical file.** Microsoft Learn:

   > The FlushViewOfFile function copies the specified number of bytes of the file view
   > to the physical file...

   and

   > Flushing a range of a mapped view initiates writing of dirty pages within that
   > range to the disk.

   For a paging-file-backed section there is no physical file in the file system to copy
   to. The flush is documented machinery with nothing to act on.

## What About the `__sync_synchronize()` Barrier?

Also removable. Inter-process visibility of ordinary stores to a `MAP_SHARED` /
section-backed mapping is provided by the hardware cache-coherence protocol (x86-64,
ARM64) plus the OS mapping of all processes onto the **same physical pages** — not by
software barriers. A full compiler+CPU barrier per frame buys nothing that ordinary
coherent memory traffic does not already provide. (If a future producer-consumer
protocol needs ordering or tearing control — e.g. a lock-free frame header — that is a
job for `std::atomic` with release/acquire on the protocol fields, not for a global
barrier; the codebase already uses `std::atomic` for exactly this in
`pVideoPresentLatencyUs`.)

## What Removing the Flush Would Actually Lose

Walk through every plausible loss:

1. **Viewer visibility** — not lost; coherency is guaranteed on all three platforms
   (cited above). Viewers read the same physical pages.
2. **Durability across a host crash** — nothing to lose: Linux tmpfs and macOS shm
   objects die on reboot **by design** (cited), and the Windows pagefile-backed section
   has no file content to preserve. The object lives exactly as long as the emulator
   keeps the mapping — flush or no flush.
3. **On-disk artifacts for post-mortem inspection** — none exist today; memory dumps are
   taken via debugger/WebAPI, not by copying a backing file.
4. **Tearing/ordering** — unchanged: the mapping is plain coherent memory today; any
   ordering guarantees the viewers rely on (none observed — they poll and render) are
   exactly as strong without the calls.

The only behavioral change is performance: two synchronous full-range flush walks per
frame disappear from the emulation thread.

## Recommendation

1. **Delete the flush work** — either make `Memory::SyncToDisk()` an empty body with a
   comment pointing to this research, or remove the method and both call sites
   (`core.cpp:623`, `mainloop.cpp:333` — the surrounding `try/catch` blocks and
   `IsSharedMemoryEnabled()` guards become dead and should go with it).
2. Keep `UnmapMemory()` exactly as is — `munmap`/`UnmapViewOfFile` + `shm_unlink`/
   `CloseHandle` remain the real cleanup path.
3. **Do not throttle instead** — this supersedes adoption-analysis case #1: the right
   amount of per-frame flush work is zero, which no throttler configuration achieves
   while keeping a trailing flush.
4. Optional hygiene while in the area: the misleading comment "Sync shared memory for
   external viewers" (memory.cpp:639) should not survive the change; the name
   `_mappedMemoryFilepath` (it holds a shm/section name) can be renamed in a follow-up.

### Verification plan

- Build + full `core-tests` run (unchanged behavior expected; no test touches
  `SyncToDisk` semantics beyond compilation).
- Live check per platform: run the emulator with the shared-memory feature on, launch
  `unreal-screen-viewer`, confirm live screen updates (coherency proof).
- Perf check: turbo-mode frame rate with shared memory on, before/after — expect the
  gain that motivated case #1 originally.

## Sources

- Linux `mmap(2)`, man-pages 6.18 — MAP_SHARED visibility:
  <https://man7.org/linux/man-pages/man2/mmap.2.html>
- Linux `msync(2)`, man-pages 6.18 — writeback timing, MS_ASYNC no-op since 2.6.19:
  <https://man7.org/linux/man-pages/man2/msync.2.html>
- Linux `shm_open(3)`, man-pages 6.18 — tmpfs /dev/shm implementation:
  <https://man7.org/linux/man-pages/man3/shm_open.3.html>
- macOS `shm_open(2)`, Darwin man page — "no visible entry in the file system",
  non-persistence across reboot: <https://keith.github.io/xcode-man-pages/shm_open.2.html>
- macOS `msync(2)`, Darwin man page — "writes modified whole pages back to the
  filesystem": <https://keith.github.io/xcode-man-pages/msync.2.html>
- Windows `CreateFileMappingA`, Microsoft Learn — paging-file backing (hFile =
  INVALID_HANDLE_VALUE) and the cross-process coherency remark:
  <https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createfilemappinga>
- Windows `FlushViewOfFile`, Microsoft Learn — "copies ... to the physical file" / dirty
  pages to disk:
  <https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-flushviewoffile>
- Windows "Reading and Writing From a File View", Microsoft Learn — flush usage in
  context: <https://learn.microsoft.com/en-us/windows/win32/memory/reading-and-writing-from-a-file-view>
