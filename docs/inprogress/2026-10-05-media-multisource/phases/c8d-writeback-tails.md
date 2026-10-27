# C8d — the tails of C8: write-back of attributes, to the host trash, of partitioned disks; a commit in bounded memory

**Status:** done, 2026-10-06 (as built: §6). Owner request 2026-10-06: "write-back tails (C8b): attributes, the host trash,
partitioned disks; and the commit (C8a) that holds the list of sectors to write in memory". Follows
[c8-commit-writeback.md](c8-commit-writeback.md) (its "not done here" list).

Exit: each of the four works end to end with tests; the C8 suites (on both session tiers) still pass.

## 1. Attributes (write-back)

Today a guest's attribute change (read-only, hidden, system: `FileChange::Op::Attributes`) is a note: a host folder
has no portable place for FAT attribute bits, and the folder manifest has no attribute field.

- **Where they go**: `<descriptor>.attributes`, next to the descriptor, one line per file, `<bits> <guest path>`
  (`RHS /DSS/COMMAND.COM`, `- /README.TXT` for "none"; a partition's paths prefixed `p2:`). Machine-owned, like the
  `.whiteout` sidecar of C8b (the user's YAML is never rewritten). The host files are not touched (a read-only host
  file would refuse the next write-back of its content).
- **When they apply**: the factory reads the sidecar into `ComposeDescriptor::attributes` and sets the bits on the
  merged tree's nodes after the union (after the whiteouts). A path that no longer exists is reported and dropped
  at the next write-back.
- **Write-back step** `Attributes`: the bits as the guest's volume has them now, into the sidecar (replacing the
  file's line). A file whose bits equal what its layer gives it (hidden for a dot file, else none) drops its line.
- Applies to every file the guest changed attributes of, whatever layer owns it (a read-only layer's file too: the
  bits live in the sidecar, not in the layer).

## 2. The host trash (`onDelete: trash`)

Today a plan error. `HostTrash::Move(path, error)` (`common/hosttrash.{h,cpp}`):

| Host | How |
|---|---|
| Windows | `SHFileOperationW` with `FO_DELETE` and `FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI` (the Recycle Bin, restorable from Explorer) |
| macOS | a move into `~/.Trash` (a unique name when taken). Finder shows it in the Trash; "Put Back" is not offered (that needs Finder's own bookkeeping) |
| Linux / BSD | the freedesktop.org Trash specification 1.0: `$XDG_DATA_HOME/Trash` (`~/.local/share/Trash`) when the file is on the home's device, else `$topdir/.Trash-$uid`; `files/<name>` and `info/<name>.trashinfo` (`Path=` percent-encoded, `DeletionDate=` local time) |

A host where none of that works (no home, the top directory not writable) makes the plan step fail with the reason;
nothing is deleted then. Write-back step kind `Trash` (planned like `Remove`, gated by DT-12 the same way).

## 3. Partitioned disks (write-back)

Today refused. A partitioned composite (C7) has composed partitions, each with its own layers, and passthrough
partitions (an image's partition as it is: no folder layers, nothing to write back to).

- **Plan**: the changes of `media changes` are already per partition (`p2:/PATH`, each with its layer). They are
  grouped by partition; each composed partition gets its own planner over its own descriptor (its layers, its
  `writes.upper`), reading the guest's files through that partition's window of the disk. A passthrough
  partition's changes are an error ("partition p1 is an image as it is: commit or flatten it"), unless the plan has
  no change there.
- **Steps** carry their partition (`WriteBackStep::partition`); the report and the journal name it.
- **Whiteouts and attributes** of a partition go to the same sidecars with the partition prefix (`p2:/OLD.TXT`); the
  descriptor loader hands each composed partition its own lines.
- **After** the apply the whole composite is rebuilt, as for a plain one.

## 4. A commit in bounded memory (C8a)

Today `CommitComposite` collects every sector to write into a `std::set<uint64_t>` (about 40 bytes a sector) and the
undo journal takes a `std::vector` of them; a session of many GB of guest writes (C10e keeps them on disk) would
need that list in memory.

- The sectors to write come from three sorted sources: the graft's patch sectors, its grafted runs, the session's
  changed sectors (`NextChanged`). A merge of the three yields each LBA once, in order, without a set.
- `CommitJournal::Write` takes that sequence instead of a vector: the entry count is written as a placeholder and
  patched at the end; the old sectors stream to the file. The writing pass walks the same merge again.
- The plan report counts from the same walk.

Memory: the graft's own patch list (inherent to the graft, bounded by the directories it re-encoded) and a few
buffers; nothing per guest-written sector.

## 5. Tests

| Test | Checks |
|---|---|
| `ComposeWriteBack_Test.AttributesGoToTheSidecar` | the guest sets read-only + hidden on a folder file and system on a base file: the sidecar has both lines, the rebuilt composite shows the bits, the host files are untouched; clearing them drops the lines |
| `ComposeWriteBack_Test.TrashMovesToTheHostTrash` | `onDelete: trash` on Linux with `XDG_DATA_HOME` pointed at a scratch folder: the file is in `Trash/files`, a `.trashinfo` names its old path; on Windows / macOS the same test checks the file left its folder (and, on macOS, is in `~/.Trash` under a scratch `HOME`) |
| `ComposePartitions_Test.WriteBackPerPartition` | a two-partition composite (FAT16 composed partitions, each with a writable folder): creates, a modify and a delete in both go to each partition's own folders; a passthrough partition's change is a plan error |
| `HostTrash_Test.*` | the freedesktop names (a clash, percent-encoding, the `.trashinfo` content), the topdir rule |
| `CommitJournal_Test.StreamedSequenceRolledBack` | the undo journal fed one sector at a time (683 of 2048): the count patched in at the end, a rollback restores every sector |
| existing | C8a / C8b suites on both tiers |

## 6. As built (2026-10-06)

| Piece | Where | Differences from the design |
|---|---|---|
| Attributes sidecar | `ComposeDescriptor::attributes`, `AttributeBitsText` / `ParseAttributeBits` (`composedescriptor.{h,cpp}`); applied in `CompositeMediumFactory::Build` after the guest's deletes; step `WriteBackStep::Kind::Attributes` | Lines are `<bits><TAB><path>` (`RH\t/WORK/TOOL.TXT`, `S\t/README.TXT`). Clearing every bit writes `-` (the line is kept, not dropped: the layer may give the file a bit, a dot file hidden, and `-` overrides it). A path no longer on the disk is reported at the build |
| Host trash | `common/hosttrash.{h,cpp}`: `HostTrash::Move`, `TrashInfo`, `TopDirTrash`; Shell32 linked on Windows (MinGW and MSVC) | The plan does not probe the trash: a move that fails at apply stops the write-back part way (the journal stays, nothing is deleted, the next insert of the descriptor retries it), instead of failing the plan as §2 and flatten-strategies.md say. macOS: a file on another volume goes to `<volume>/.Trashes/<uid>`. Linux: the home trash is created when missing; a topdir trash gets mode 0700. The journal line `trash` re-runs the move on recovery when the file is still there |
| Partitioned write-back | `WriteBack::Plan`: changes grouped by the partition prefix (`work:/TODO.TXT`); one planner per composed partition over its own descriptor, its window of the disk (`SubRangeDevice`) and its layout (`OffsetLayout` for a graft over an image partition); `WriteBackStep::partition` | The partition prefix is the partition's name (`pN` when it has none), not always `pN`. A change's layer name comes as `<partition>/<layer>` and is cut to the partition's own layer. Qt's flatten dialog no longer refuses write-back for a partitioned disk |
| Commit in bounded memory | `CommitComposite` merges the sorted patch sectors, the grafted runs and `NextChanged` (a generator, each LBA once); `CommitJournal::Write(image, device, next, error)` streams the old sectors and patches the count at the end; `Recover` streams too (a hash pass, then the apply pass) | The vector overload of `Write` stays (it wraps the streamed one) |

Tests as built: `ComposeWriteBack_Test.{PlanWritesNothing, TrashMovesToTheHostTrash, AttributesGoToTheSidecar}` on both
session tiers, `ComposePartitions_Test.WriteBackPerPartition` (two composed partitions with writable folders: a
modify, a create and a delete land in each partition's own folder; a later create on the passthrough partition fails
the plan), `HostTrash_Test.{TrashInfoAndTopDir, MovesIntoTheTrashAndAvoidsClashes}` (a scratch `HOME` /
`XDG_DATA_HOME`; skipped on Windows, where the Recycle Bin is the user's own), `CommitJournal_Test.StreamedSequenceRolledBack`.
