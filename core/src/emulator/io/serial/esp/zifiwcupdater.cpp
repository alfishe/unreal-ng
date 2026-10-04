#include "emulator/io/serial/esp/zifiwcupdater.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "emulator/io/serial/esp/zifibridgehost.h"
#include "emulator/io/serial/esp/zifistate.h"

// The firmware's updater, procedure by procedure (ZiFi-ESP32-S3-Zero 2e5ba83 src/wc_updater.cpp and
// src/wc_update_service.cpp). The coroutines below keep its control flow and texts; what it asked of the outside
// is a primitive (Ready / Complete), logged so a TTD load can re-run the session to where it was.

namespace
{
constexpr uint32_t kVfsNormalWaitMs = 15000;
constexpr uint32_t kVfsMutateWaitMs = 70000;
constexpr uint32_t kVfsCloseWaitMs = 190000;
constexpr uint32_t kEventWaitMs = 5000;            // main.cpp kWcuEventWaitMs
constexpr uint32_t kProgressIntervalMs = 250;
constexpr unsigned kDownloadAttempts = 3;
constexpr unsigned kWriteAttempts = 3;
constexpr size_t kMaxEventPayload = 63;
constexpr size_t kChunk = 1024;
constexpr const char* kApiHost = "api.github.com";
constexpr const char* kRawHost = "raw.githubusercontent.com";
constexpr const char* kTempName = "WCUPD.TMP";
constexpr const char* kAsideName = "WCUPD.OLD";
constexpr const char* kLeftoverReason = "CHKDSK: old WCUPD.* in folder";
constexpr uint8_t kMoveUnsupported = 0xFE;
constexpr uint8_t kFilexCommittedCleanup = 0x25;
constexpr uint8_t kCommandApply = 1, kCommandStop = 2, kCommandSync = 3;

using Op = ZiFiVfsBridge::Op;

bool EqualNoCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const unsigned char x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
        if ((x < 0x80 ? std::toupper(x) : x) != (y < 0x80 ? std::toupper(y) : y))
            return false;
    }
    return true;
}

bool SafeName(const std::string& text, bool allowSlash)
{
    if (text.empty())
        return false;
    for (char value : text)
    {
        if (std::isalnum(static_cast<unsigned char>(value)) || value == '-' || value == '_' || value == '.' ||
            (allowSlash && value == '/'))
            continue;
        return false;
    }
    return true;
}

std::string Printf(const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

std::string Printf(const char* format, ...)
{
    char text[256];
    va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    return length < 0 ? std::string() : std::string(text, std::min<size_t>(static_cast<size_t>(length), sizeof(text) - 1));
}

std::string Clip(const std::string& s, size_t capacity)
{
    return s.size() < capacity ? s : s.substr(0, capacity - 1);
}

void Le16(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

void Le24(std::vector<uint8_t>& v, uint32_t x)
{
    if (x > 0xFFFFFFUL)
        x = 0xFFFFFFUL;
    Le16(v, x);
    v.push_back(static_cast<uint8_t>(x >> 16));
}

std::string LocalPath(const std::string& relative)
{
    return "/" + relative;
}

int ListGroup(const std::string& path)
{
    const size_t slash = path.rfind('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos && slash == std::string::npos && EqualNoCase(name.substr(dot), ".$C"))
        return 0;
    if (dot != std::string::npos && EqualNoCase(name.substr(dot), ".WMF"))
        return 1;
    return 2;
}

bool PathBefore(const std::string& left, const std::string& right)
{
    for (size_t i = 0;; ++i)
    {
        const unsigned char a = i < left.size() ? static_cast<unsigned char>(left[i]) : 0;
        const unsigned char b = i < right.size() ? static_cast<unsigned char>(right[i]) : 0;
        const int ua = a < 0x80 ? std::toupper(a) : a;
        const int ub = b < 0x80 ? std::toupper(b) : b;
        if (ua != ub || a == 0)
            return ua < ub;
    }
}
}  // namespace

ZiFiWcUpdater::ZiFiWcUpdater(ZiFiBridgeHost& host, ZiFiHttpFetch& fetch) : _host(host), _fetch(fetch)
{
}

ZiFiWcUpdater::~ZiFiWcUpdater() = default;

// --- The service (WcUpdateService) --------------------------------------------------------------------------------

bool ZiFiWcUpdater::Start(const std::vector<uint8_t>& payload, std::string& error)
{
    if (_session.h && !_finished)
    {
        error = "already running";
        return false;
    }
    Release();
    if (!Configure(payload, error))
        return false;
    _payload = payload;
    _finished = false;
    _applying = false;
    _stop = false;
    // The task runs from the next Poll: WCU_START's answer leaves before the first event (the plugin's waitFor
    // would drop an event that came first)
    _session = Session();
    _begun = false;
    return true;
}

bool ZiFiWcUpdater::Apply(const std::vector<uint8_t>& indices)
{
    if (!_session.h || _finished || _applying || _stop || indices.size() > kMaxFiles || _commands.size() >= 2)
        return false;
    _applying = true;
    _commands.push_back({kCommandApply, indices});
    return true;   // the task takes it at the next Poll: the answer leaves first (the plugin waits for it)
}

bool ZiFiWcUpdater::Sync()
{
    if (!_session.h || _finished || _stop || _commands.size() >= 2)
        return false;
    _commands.push_back({kCommandSync, {}});
    return true;
}

void ZiFiWcUpdater::RequestStop()
{
    if (!_session.h)
        return;
    _stop = true;
    if (_fetch.Busy())
        _fetch.Cancel();   // the HTTPS receive ends at once
    if (_commands.size() < 2)
        _commands.push_back({kCommandStop, {}});
    Poll();
}

void ZiFiWcUpdater::Release()
{
    _suspended = nullptr;
    _session = Task();
    _begun = false;
    _finished = true;
    _applying = false;
    _stop = false;
    _commands.clear();
    _log.clear();
    _logPos = 0;
    _replaying = false;
    _pending = Request();
    _pendingIssued = false;
    _reclaiming = false;
    _download.clear();
    _downloadRefs.clear();
    _files.clear();
    _payload.clear();
    _stateText.clear();
    _commit.clear();
}

std::string ZiFiWcUpdater::Activity() const
{
    if (!_session.h)
        return "idle";
    if (_finished)
        return "finished: " + _stateText;
    static const char* const kPrims[] = {"", "vfs", "event", "", "", "GitHub branch", "GitHub tree", "download", "",
                                         "", "", "", "", "", "", "waiting for a command", "", ""};
    const size_t p = static_cast<size_t>(_pending.prim);
    std::string text = std::string(p < sizeof(kPrims) / sizeof(kPrims[0]) ? kPrims[p] : "") + (_pending.path.empty() ? "" : " " + _pending.path);
    return text + " | " + _stateText;
}

// --- Primitives ----------------------------------------------------------------------------------------------------

ZiFiWcUpdater::Await ZiFiWcUpdater::Vfs(Op op, const std::string& path, uint32_t value, uint32_t waitMs)
{
    Request r;
    r.prim = Prim::Vfs;
    r.op = static_cast<uint8_t>(op);
    r.path = path;
    r.a = value;
    r.b = waitMs;
    return Await{*this, r};
}

ZiFiWcUpdater::Await ZiFiWcUpdater::VfsRename(const std::string& oldPath, const std::string& newName, uint32_t waitMs)
{
    Request r;
    r.prim = Prim::Vfs;
    r.op = static_cast<uint8_t>(Op::Rename);
    r.path = oldPath;
    r.path2 = newName;
    r.b = waitMs;
    return Await{*this, r};
}

ZiFiWcUpdater::Await ZiFiWcUpdater::VfsMove(const std::string& oldPath, const std::string& newPath, bool replace, uint32_t waitMs)
{
    Request r;
    r.prim = Prim::Vfs;
    r.op = static_cast<uint8_t>(Op::MoveRename);
    r.path = oldPath;
    r.path2 = newPath;
    r.a = replace ? 1 : 0;
    r.b = waitMs;
    return Await{*this, r};
}

ZiFiWcUpdater::Await ZiFiWcUpdater::Event(uint8_t cmd, const std::vector<uint8_t>& payload)
{
    Request r;
    r.prim = Prim::Event;
    r.op = cmd;
    r.data = payload;
    return Await{*this, r};
}

ZiFiWcUpdater::Await ZiFiWcUpdater::Prim0(Prim prim, uint32_t a, uint32_t b)
{
    Request r;
    r.prim = prim;
    r.a = a;
    r.b = b;
    return Await{*this, r};
}

bool ZiFiWcUpdater::Ready(const Request& request)
{
    if (_replaying)
    {
        if (_logPos < _log.size())
        {
            _ready = _log[_logPos++];
            return true;
        }
        // The log is used up: the primitive in flight at the checkpoint is already issued; the next Poll completes
        // it (a load has no side effects)
        _replaying = false;
        if (_pendingIssued)
            return false;
    }
    _pending = request;
    _pendingIssued = true;
    Issue(request);
    if (Complete(_ready))
    {
        _pendingIssued = false;
        Record(_ready);
        return true;
    }
    return false;
}

ZiFiWcUpdater::Entry ZiFiWcUpdater::Resume()
{
    return _ready;
}

void ZiFiWcUpdater::Issue(const Request& q)
{
    const uint64_t now = _host.BridgeNow();
    _reclaiming = false;
    switch (q.prim)
    {
        case Prim::Vfs:
            _pendingDeadline = now + _host.BridgeMicros(static_cast<uint64_t>(q.b) * 1000);
            break;
        case Prim::Event: _pendingDeadline = now + _host.BridgeMicros(static_cast<uint64_t>(kEventWaitMs) * 1000); break;
        case Prim::FetchRef:
        case Prim::FetchTree: _fetch.Start(kApiHost, 443, q.path, kMaxJson); break;
        case Prim::Download: StartDownload(); break;
        default: break;
    }
}

bool ZiFiWcUpdater::Complete(Entry& out)
{
    const Request& q = _pending;
    out = Entry();
    out.prim = q.prim;
    ZiFiVfsBridge& vfs = _host.BridgeVfs();
    switch (q.prim)
    {
        case Prim::None: return true;
        case Prim::Vfs:
        {
            // vfs(): submit (busy -> "vfs busy"), awaitVfs with the updater's wait; after it ran out the bridge's
            // result is still taken (reclaim) before the failure is reported
            if (!_pending.submitted && !_reclaiming)
            {
                const Op op = static_cast<Op>(q.op);
                bool submitted = false;
                if (op == Op::Rename)
                    submitted = vfs.SubmitRename(q.path, q.path2, false);
                else if (op == Op::MoveRename)
                    submitted = vfs.SubmitMoveRename(q.path, q.path2, false, q.a != 0);
                else
                    submitted = vfs.Submit(op, q.path, q.a);
                if (!submitted)
                {
                    out.ok = false;
                    out.text = "vfs busy";
                    return true;
                }
                _pending.submitted = true;
            }
            ZiFiVfsBridge::Result result;
            if (vfs.TakeResult(result))
            {
                if (_reclaiming)
                {
                    out.ok = false;
                    out.text = "vfs timeout";
                    return true;
                }
                out.ok = result.success;
                out.text = result.success ? std::string() : result.error;
                out.vfs = result;
                return true;
            }
            if (_host.BridgeNow() >= _pendingDeadline)
            {
                if (!_reclaiming)
                {
                    _reclaiming = true;
                    _pendingDeadline = _host.BridgeNow() + _host.BridgeMicros(static_cast<uint64_t>(kVfsCloseWaitMs) * 1000);
                    return false;
                }
                out.ok = false;
                out.text = "vfs timeout";
                return true;
            }
            return false;
        }
        case Prim::Event:
            if (_host.BridgeEventRoom())
            {
                _host.BridgeEvent(q.op, q.data);
                out.ok = true;
                return true;
            }
            if (_host.BridgeNow() >= _pendingDeadline)
            {
                out.ok = false;
                return true;
            }
            return false;
        case Prim::Millis:
            out.a = static_cast<uint32_t>(_host.BridgeNow() / std::max<uint64_t>(1, _host.BridgeMicros(1000)));
            return true;
        case Prim::Stopped: out.ok = _stop; return true;
        case Prim::FetchRef:
        case Prim::FetchTree:
        {
            if (!_fetch.Done())
                return false;
            ZiFiHttpFetch::Result r = _fetch.Take();
            out.ok = r.ok;
            out.a = r.status;
            out.text = r.error;
            if (!r.ok || r.status != 200)
                return true;
            if (q.prim == Prim::FetchRef)
            {
                char commit[41] = {};
                std::string json(r.body.begin(), r.body.end());
                out.b = zifigit::ParseRefCommit(json.data(), json.size(), commit) ? 1 : 0;
                out.blob.assign(commit, commit + 40);
            }
            else
            {
                std::vector<zifigit::TreeEntry> entries(kMaxFiles + kMaxDirectories);
                size_t count = 0;
                bool truncated = true;
                std::string json(r.body.begin(), r.body.end());
                const bool parsed = zifigit::ParseTree(json.data(), json.size(), entries.data(), entries.size(), count, truncated);
                out.b = parsed && !truncated ? 1 : 0;
                entries.resize(count);
                out.tree = std::move(entries);
            }
            return true;
        }
        case Prim::Download:
            PollDownload();
            if (_fetch.Busy() || _downloadAttempt != 0xFF)
                return false;
            out.ok = _downloadError.empty();
            out.text = _downloadError;
            return true;
        case Prim::HashBegin:
            zifigit::GitBlobBegin(_sha, q.a);
            return true;
        case Prim::HashChunk:
        {
            // readForNetwork into chunk_; a chunk beyond the expected size fails before the SHA sees it
            uint8_t buffer[kChunk];
            const size_t wanted = std::min(vfs.ToNetAvailable(), kChunk);
            const size_t received = vfs.ReadForNetwork(buffer, wanted);
            if (received <= q.a)
                _sha.Update(buffer, received);
            out.a = static_cast<uint32_t>(received);
            return true;
        }
        case Prim::HashFinish:
        {
            uint8_t digest[zifigit::Sha1::kDigestSize];
            _sha.Finish(digest);
            out.blob.assign(digest, digest + sizeof(digest));
            return true;
        }
        case Prim::RingToNet: out.a = static_cast<uint32_t>(vfs.ToNetAvailable()); return true;
        case Prim::RingQueued: out.a = static_cast<uint32_t>(vfs.FromNetAvailable()); return true;
        case Prim::RingFree: out.a = static_cast<uint32_t>(vfs.FromNetFree()); return true;
        case Prim::RingWrite:
        {
            const size_t from = std::min<size_t>(q.a, _download.size());
            const size_t n = std::min<size_t>(q.b, _download.size() - from);
            out.a = static_cast<uint32_t>(vfs.WriteFromNetwork(_download.data() + from, n));
            return true;
        }
        case Prim::Command:
            if (_commands.empty())
                return false;
            out.a = _commands.front().kind;
            out.blob = _commands.front().indices;
            _commands.pop_front();
            return true;
        case Prim::SetApplying: _applying = q.a != 0; return true;
        case Prim::DropDownload:
            _download.clear();
            _downloadRefs.clear();
            return true;
    }
    return true;
}

void ZiFiWcUpdater::Poll()
{
    if (_fetch.Busy())
        _fetch.Poll();
    if (_session.h && !_begun)
    {
        _begun = true;
        _session.h.resume();
        if (_session.h.done())
            _finished = true;
        return;
    }
    if (!_suspended || !_pendingIssued)
        return;
    if (!Complete(_ready))
        return;
    _pendingIssued = false;
    Record(_ready);
    auto h = _suspended;
    _suspended = nullptr;
    h.resume();
    if (_session.h && _session.h.done())
        _finished = true;
}

void ZiFiWcUpdater::StartDownload()
{
    _downloadAttempt = 0;
    _downloadError.clear();
    _download.clear();
    _downloadRefs.clear();
    PollDownload();
}

void ZiFiWcUpdater::PollDownload()
{
    // download(): up to three fetches of raw.githubusercontent.com/{repo}/{commit}/{dir}/{path}, the length and
    // the git SHA checked; a stop ends it (error_ keeps what it was)
    const Request& q = _pending;
    if (_downloadAttempt == 0xFF)
        return;
    if (_fetch.Busy())
        return;
    if (_fetch.Done())
    {
        ZiFiHttpFetch::Result r = _fetch.Take();
        if (!r.ok)
            _downloadError = "download: " + r.error;
        else if (r.status != 200 || r.body.size() != q.b)
            _downloadError = Printf("download: HTTP %u, %lu bytes", static_cast<unsigned>(r.status), static_cast<unsigned long>(r.body.size()));
        else
        {
            zifigit::Sha1 sha;
            zifigit::GitBlobBegin(sha, q.b);
            sha.Update(r.body.data(), r.body.size());
            uint8_t digest[zifigit::Sha1::kDigestSize];
            sha.Finish(digest);
            if (q.data.size() == sizeof(digest) && std::memcmp(digest, q.data.data(), sizeof(digest)) == 0)
            {
                _download = std::move(r.body);
                _downloadRefs = std::move(r.refs);
                _downloadError.clear();
                _downloadAttempt = 0xFF;
                return;
            }
            _downloadError = "download: SHA mismatch";
        }
    }
    if (_downloadAttempt >= kDownloadAttempts || _stop)
    {
        if (_downloadError.empty())
            _downloadError = "stopped";
        _downloadAttempt = 0xFF;
        return;
    }
    ++_downloadAttempt;
    // Buffer one byte longer than the file: an extra byte is a different file
    _fetch.Start(kRawHost, 443, q.path, static_cast<size_t>(q.b) + 1);
}

// --- configure ---------------------------------------------------------------------------------------------------

bool ZiFiWcUpdater::Configure(const std::vector<uint8_t>& payload, std::string& error)
{
    std::vector<std::string> fields;
    size_t start = 0;
    for (size_t index = 0; index < payload.size(); ++index)
    {
        if (payload[index] != 0)
            continue;
        if (index == start)
            break;   // an empty string ends the list
        if (fields.size() == 3 + kMaxProtected)
        {
            error = "too many fields";
            return false;
        }
        fields.emplace_back(reinterpret_cast<const char*>(payload.data() + start), index - start);
        start = index + 1;
    }
    if (fields.size() < 3 || fields[0].find('/') == std::string::npos || !SafeName(fields[0], true) ||
        !SafeName(fields[1], true) || !SafeName(fields[2], true) || fields[0].size() >= 96 || fields[1].size() >= 64 ||
        fields[2].size() >= 64)
    {
        error = "bad repository";
        return false;
    }
    _repo = fields[0];
    _branch = fields[1];
    _directory = fields[2];
    _protected.clear();
    for (size_t index = 3; index < fields.size(); ++index)
    {
        if (fields[index].size() >= zifigit::TreeEntry::kPathSize)
        {
            error = "bad protected path";
            return false;
        }
        _protected.push_back(fields[index]);
    }
    return true;
}

// --- Helpers -------------------------------------------------------------------------------------------------------

bool ZiFiWcUpdater::Updatable(Status s)
{
    return s == Status::Different || s == Status::New || s == Status::ReadError || s == Status::Failed;
}

bool ZiFiWcUpdater::CanUpdate(const File& f)
{
    return f.remote && Updatable(f.status) && (!f.kept || !f.local);
}

bool ZiFiWcUpdater::AutoSelect(const File& f)
{
    if (!CanUpdate(f))
        return false;
    const size_t dot = f.path.rfind('.');
    const size_t slash = f.path.rfind('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return false;
    const std::string ext = f.path.substr(dot);
    return EqualNoCase(ext, ".WMF") || EqualNoCase(ext, ".$C") || EqualNoCase(ext, ".SPG");
}

bool ZiFiWcUpdater::IsProtected(const std::string& path) const
{
    for (const std::string& p : _protected)
    {
        if (EqualNoCase(p, path))
            return true;
    }
    return false;
}

ZiFiWcUpdater::File* ZiFiWcUpdater::FindFile(const std::string& path)
{
    for (File& f : _files)
    {
        if (EqualNoCase(f.path, path))
            return &f;
    }
    return nullptr;
}

int ZiFiWcUpdater::DirectoryIndex(const std::string& dir) const
{
    for (size_t i = 0; i < _directories.size(); ++i)
    {
        if (EqualNoCase(_directories[i], dir))
            return static_cast<int>(i);
    }
    return -1;
}

int ZiFiWcUpdater::DirectoryOf(const std::string& relativeFile) const
{
    const size_t slash = relativeFile.rfind('/');
    return DirectoryIndex(slash == std::string::npos ? std::string() : relativeFile.substr(0, slash));
}

bool ZiFiWcUpdater::LeftoverIn(const std::string& relativeFile) const
{
    const int folder = DirectoryOf(relativeFile);
    return folder >= 0 && _leftover[static_cast<size_t>(folder)];
}

void ZiFiWcUpdater::MarkLeftover(const std::string& relativeFile)
{
    const int folder = DirectoryOf(relativeFile);
    if (folder >= 0)
        _leftover[static_cast<size_t>(folder)] = true;
    _leftoverSeen = true;
}

bool ZiFiWcUpdater::UrlEncodePath(const std::string& path, std::string& out)
{
    static const char kDigits[] = "0123456789ABCDEF";
    out.clear();
    for (unsigned char value : path)
    {
        const bool plain = std::isalnum(value) || value == '-' || value == '_' || value == '.' || value == '~' || value == '/';
        if (plain)
            out.push_back(static_cast<char>(value));
        else
        {
            out.push_back('%');
            out.push_back(kDigits[value >> 4]);
            out.push_back(kDigits[value & 0x0F]);
        }
    }
    return out.size() < 3 * zifigit::TreeEntry::kPathSize;
}

void ZiFiWcUpdater::SortFiles()
{
    std::stable_sort(_files.begin(), _files.end(), [](const File& left, const File& right) {
        const int a = ListGroup(left.path), b = ListGroup(right.path);
        if (a != b)
            return a < b;
        return PathBefore(left.path, right.path);
    });
}

// --- Events --------------------------------------------------------------------------------------------------------

ZiFiWcUpdater::Task ZiFiWcUpdater::SendState(Phase phase, uint16_t current, uint16_t total, std::string text)
{
    // [phase][current LE16][total LE16][percent][text]
    _statePhase = phase;
    _stateCurrent = current;
    _stateTotal = total;
    _stateText = Clip(text, 64);
    std::vector<uint8_t> payload = {static_cast<uint8_t>(phase)};
    Le16(payload, current);
    Le16(payload, total);
    payload.push_back(_progressPercent);
    const size_t room = kMaxEventPayload - 6;
    payload.insert(payload.end(), _stateText.begin(), _stateText.begin() + static_cast<std::ptrdiff_t>(std::min(room, _stateText.size())));
    Entry ms = co_await Prim0(Prim::Millis);
    _progressSentMs = ms.a;
    Entry e = co_await Event(kEventState, payload);
    co_return e.ok;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::AddProgress(uint32_t bytes)
{
    _progressDone += bytes;
    if (_progressTotal == 0)
        co_return true;
    uint32_t percent = static_cast<uint32_t>(static_cast<uint64_t>(_progressDone) * 100U / _progressTotal);
    if (percent > 100)
        percent = 100;
    const bool changed = percent != _progressPercent;
    _progressPercent = static_cast<uint8_t>(percent);
    Entry ms = co_await Prim0(Prim::Millis);
    if (!changed || ms.a - _progressSentMs < kProgressIntervalMs)
        co_return true;
    const std::string text = _stateText;
    co_await SendState(_statePhase, _stateCurrent, _stateTotal, text);
    co_return true;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::SendEntry(size_t index)
{
    const File& entry = _files[index];
    std::vector<uint8_t> payload = {static_cast<uint8_t>(index), static_cast<uint8_t>(entry.status),
                                    static_cast<uint8_t>((entry.remote ? 1 : 0) | (entry.local ? 2 : 0) | (entry.kept ? 4 : 0) |
                                                         (CanUpdate(entry) ? 8 : 0) | (AutoSelect(entry) ? 16 : 0))};
    Le24(payload, entry.local ? entry.localSize : 0);
    Le24(payload, entry.remote ? entry.remoteSize : 0);
    // A path longer than the event: its tail, cut at a UTF-8 character boundary
    std::string path = entry.path;
    const size_t room = kMaxEventPayload - 9;
    if (path.size() > room)
    {
        size_t from = path.size() - room;
        while (from < path.size() && (static_cast<uint8_t>(path[from]) & 0xC0) == 0x80)
            ++from;
        path = path.substr(from);
    }
    payload.insert(payload.end(), path.begin(), path.end());
    Entry e = co_await Event(kEventEntry, payload);
    co_return e.ok;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::SendReady()
{
    _progressPercent = 100;
    size_t same = 0, pending = 0, failed = 0;
    for (const File& f : _files)
    {
        if (f.status == Status::Same || f.status == Status::Updated || f.status == Status::KeptSame ||
            f.status == Status::KeptDifferent)
            ++same;
        if (CanUpdate(f))
            ++pending;
        if (f.status == Status::Failed)
            ++failed;
    }
    (void)same;
    const char* cut = _leftoverSeen ? ", CHKDSK: old WCUPD.*" : _listTruncated ? ", SD list cut" : "";
    const uint16_t total = static_cast<uint16_t>(_files.size());
    if (failed != 0)
    {
        std::string reason;
        for (size_t index = _files.size(); index-- > 0;)
        {
            if (_files[index].status == Status::Failed)
            {
                reason = _files[index].reason;
                break;
            }
        }
        const char* note = reason.find("CHKDSK") != std::string::npos ? "" : _leftoverSeen ? ", CHKDSK"
                                                                       : _listTruncated  ? ", list cut"
                                                                                         : "";
        const bool ok = co_await SendState(Phase::Ready, static_cast<uint16_t>(pending), total,
                                           Printf("%u to update, %u failed%s: %s", static_cast<unsigned>(pending),
                                                  static_cast<unsigned>(failed), note, reason.c_str()));
        co_return ok;
    }
    if (pending == 0)
    {
        const bool ok = co_await SendState(Phase::Ready, 0, total, Printf("All files match GitHub %.7s%s", _commit.c_str(), cut));
        co_return ok;
    }
    const bool ok = co_await SendState(Phase::Ready, static_cast<uint16_t>(pending), total,
                                       Printf("%u to update from GitHub %.7s%s", static_cast<unsigned>(pending), _commit.c_str(), cut));
    co_return ok;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::Fail(std::string text)
{
    _error = Clip(text, 96);
    co_await SendState(Phase::Error, 0, 0, _error);
    co_return false;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::Resend()
{
    // The marker "the whole list follows, this many lines", the lines, the last state again
    std::vector<uint8_t> marker = {static_cast<uint8_t>(Phase::Sync)};
    Le16(marker, 0);
    Le16(marker, static_cast<uint32_t>(_files.size()));
    marker.push_back(_progressPercent);
    Entry e = co_await Event(kEventState, marker);
    if (!e.ok)
        co_return false;
    for (size_t index = 0; index < _files.size(); ++index)
    {
        if (!co_await SendEntry(index))
            co_return false;
    }
    const std::string text = _stateText;
    const bool ok = co_await SendState(_statePhase, _stateCurrent, _stateTotal, text);
    co_return ok;
}

// --- VFS helpers ---------------------------------------------------------------------------------------------------

ZiFiWcUpdater::Task ZiFiWcUpdater::QuietVfs(Op op, std::string path, uint32_t waitMs)
{
    // A cleanup request: its failure does not replace the reason in error_
    Entry e = co_await Vfs(op, path, 0, waitMs);
    co_return e.ok;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::StopNow()
{
    Entry e = co_await Prim0(Prim::Stopped);
    co_return e.ok;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::RenameEntry(std::string oldPath, std::string newName)
{
    Entry e = co_await VfsRename(oldPath, newName, kVfsMutateWaitMs);
    if (!e.ok)
        _error = Clip(e.text, 96);
    co_return e.ok;
}

// --- check ---------------------------------------------------------------------------------------------------------

ZiFiWcUpdater::Task ZiFiWcUpdater::Session()
{
    co_await Check();
    for (;;)
    {
        if (co_await StopNow())
            break;
        Entry command = co_await Prim0(Prim::Command);
        if (command.a == kCommandStop || co_await StopNow())
            break;
        if (command.a == kCommandApply)
        {
            co_await ApplyFiles(command.blob);
            co_await Prim0(Prim::SetApplying, 0);
        }
        else if (command.a == kCommandSync)
            co_await Resend();
    }
    co_return true;
}

ZiFiWcUpdater::Await ZiFiWcUpdater::Fetch(Prim prim, const std::string& path, uint32_t size, const uint8_t* sha)
{
    Request r;
    r.prim = prim;
    r.path = path;
    r.b = size;
    if (sha)
        r.data.assign(sha, sha + zifigit::Sha1::kDigestSize);
    return Await{*this, r};
}

// --- fetchRemote ---------------------------------------------------------------------------------------------------

ZiFiWcUpdater::Task ZiFiWcUpdater::FetchRemote()
{
    if (!co_await SendState(Phase::Github, 0, 0, "GitHub: " + _repo))
        co_return false;
    Entry ref = co_await Fetch(Prim::FetchRef, "/repos/" + _repo + "/git/ref/heads/" + _branch);
    if (!ref.ok)
    {
        co_await Fail("GitHub: " + (ref.text.empty() ? std::string("no answer") : ref.text));
        co_return false;
    }
    if (ref.a == 403 || ref.a == 429)
    {
        co_await Fail("GitHub API limit, retry later");
        co_return false;
    }
    if (ref.a != 200)
    {
        co_await Fail(Printf("GitHub branch: HTTP %u", ref.a));
        co_return false;
    }
    if (ref.b == 0)
    {
        co_await Fail("GitHub branch: bad answer");
        co_return false;
    }
    _commit.assign(ref.blob.begin(), ref.blob.end());
    std::string encoded;
    if (!UrlEncodePath(_directory, encoded) || encoded.size() >= 128)
    {
        co_await Fail("bad directory");
        co_return false;
    }
    const std::string path = "/repos/" + _repo + "/git/trees/" + _commit + ":" + encoded + "?recursive=1";
    Entry tree;
    const bool announced = co_await SendState(Phase::Github, 0, 0, Printf("GitHub: %.7s %s", _commit.c_str(), _directory.c_str()));
    if (announced)
        tree = co_await Fetch(Prim::FetchTree, path);
    if (!announced || !tree.ok)
    {
        co_await Fail("GitHub: " + (tree.text.empty() ? std::string("no answer") : tree.text));
        co_return false;
    }
    if (tree.a == 403 || tree.a == 429)
    {
        co_await Fail("GitHub API limit, retry later");
        co_return false;
    }
    if (tree.a != 200)
    {
        co_await Fail(Printf("GitHub list: HTTP %u", tree.a));
        co_return false;
    }
    if (tree.b == 0)
    {
        co_await Fail("GitHub list: bad or too long");
        co_return false;
    }
    _files.clear();
    _directories.assign(1, std::string());   // the WC root
    bool fits = true;
    for (const zifigit::TreeEntry& entry : tree.tree)
    {
        if (entry.directory)
        {
            if (_directories.size() == kMaxDirectories)
            {
                fits = false;
                break;
            }
            _directories.emplace_back(entry.path);
            continue;
        }
        if (_files.size() == kMaxFiles)
        {
            fits = false;
            break;
        }
        File f;
        f.path = entry.path;
        std::memcpy(f.sha, entry.sha, sizeof(f.sha));
        f.remoteSize = entry.size;
        f.remote = true;
        f.kept = IsProtected(f.path);
        _files.push_back(f);
    }
    _leftover.assign(_directories.size(), false);
    _present.assign(_directories.size(), false);
    if (!fits)
    {
        co_await Fail("GitHub list: too many files");
        co_return false;
    }
    // FAT does not tell case apart: two GitHub paths equal without it would be one file on the SD
    bool unique = true;
    for (size_t first = 0; first < _files.size() && unique; ++first)
    {
        for (size_t second = first + 1; second < _files.size(); ++second)
        {
            if (EqualNoCase(_files[first].path, _files[second].path))
            {
                unique = false;
                break;
            }
        }
        for (size_t folder = 1; folder < _directories.size() && unique; ++folder)
            unique = !EqualNoCase(_files[first].path, _directories[folder]);
    }
    for (size_t first = 0; first < _directories.size() && unique; ++first)
    {
        for (size_t second = first + 1; second < _directories.size(); ++second)
        {
            if (EqualNoCase(_directories[first], _directories[second]))
            {
                unique = false;
                break;
            }
        }
    }
    if (!unique)
    {
        co_await Fail("GitHub list: names differ only in case");
        co_return false;
    }
    bool reserved = false;
    const auto nameOf = [](const std::string& p) {
        const size_t slash = p.rfind('/');
        return slash == std::string::npos ? p : p.substr(slash + 1);
    };
    for (size_t index = 0; index < _files.size() && !reserved; ++index)
        reserved = EqualNoCase(nameOf(_files[index].path), kTempName) || EqualNoCase(nameOf(_files[index].path), kAsideName);
    for (size_t folder = 1; folder < _directories.size() && !reserved; ++folder)
        reserved = EqualNoCase(nameOf(_directories[folder]), kTempName) || EqualNoCase(nameOf(_directories[folder]), kAsideName);
    if (reserved)
    {
        co_await Fail(Printf("GitHub list: reserved name %s", kTempName));
        co_return false;
    }
    co_return true;
}

// --- The SD side ---------------------------------------------------------------------------------------------------

ZiFiWcUpdater::Task ZiFiWcUpdater::ProbeLeftovers(size_t index, std::string directoryPath)
{
    // A leftover of an interrupted replacement, also by a direct STAT (a read error mid-listing looks like the end)
    const bool slash = !directoryPath.empty() && directoryPath.back() == '/';
    for (const char* name : {kTempName, kAsideName})
    {
        Entry st = co_await Vfs(Op::Stat, directoryPath + (slash ? "" : "/") + name, 0, kVfsNormalWaitMs);
        if (st.ok)
        {
            _leftover[index] = true;
            _leftoverSeen = true;
        }
        else if (st.text != "stat-1")
            co_return false;
    }
    co_return true;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::ScanDirectory(size_t index, bool reportExtra)
{
    const std::string directory = _directories[index];
    const std::string path = LocalPath(directory);
    Entry opened = co_await Vfs(Op::OpenDirectory, path, 0, kVfsNormalWaitMs);
    if (!opened.ok)
    {
        _error = opened.text;
        // The plugin's refusal (status 1) means no such folder - unless a STAT finds it (a read error)
        if (opened.text == "opendir-1")
        {
            Entry st = co_await Vfs(Op::Stat, path, 0, kVfsNormalWaitMs);
            if (!st.ok && st.text == "stat-1")
                co_return true;
        }
        co_await Fail("SD: cannot read " + path);
        co_return false;
    }
    _present[index] = true;
    for (;;)
    {
        if (co_await StopNow())
            co_return false;
        Entry r = co_await Vfs(Op::ReadDirectory, {}, 0, kVfsNormalWaitMs);
        if (!r.ok)
        {
            _error = r.text;
            co_await Fail("SD: cannot read " + path);
            co_return false;
        }
        if (r.vfs.atEnd)
        {
            if (!co_await ProbeLeftovers(index, path))
            {
                co_await Fail("SD: cannot read " + path);
                co_return false;
            }
            co_return true;
        }
        const std::string& name = r.vfs.name;
        if (r.vfs.isDirectory || name == "." || name == "..")
            continue;
        if (EqualNoCase(name, kTempName) || EqualNoCase(name, kAsideName))
        {
            _leftover[index] = true;
            _leftoverSeen = true;
            continue;
        }
        const std::string relative = directory.empty() ? name : directory + "/" + name;
        if (relative.size() >= zifigit::TreeEntry::kPathSize)
        {
            _listTruncated = _listTruncated || reportExtra;
            continue;
        }
        if (File* known = FindFile(relative))
        {
            known->local = true;
            known->localSize = r.vfs.size;
            continue;
        }
        // Files GitHub does not have: shown inside WC's folders only (the card's root holds anything)
        if (!reportExtra)
            continue;
        if (_files.size() == kMaxFiles)
        {
            _listTruncated = true;
            continue;
        }
        File extra;
        extra.path = relative;
        extra.local = true;
        extra.localSize = r.vfs.size;
        extra.kept = IsProtected(relative);
        extra.status = Status::LocalOnly;
        _files.push_back(extra);
    }
}

ZiFiWcUpdater::Task ZiFiWcUpdater::ScanLocal()
{
    if (!co_await SendState(Phase::Local, 0, 0, "SD: reading WC folders"))
        co_return false;
    for (size_t index = 0; index < _directories.size(); ++index)
    {
        if (!co_await ScanDirectory(index, !_directories[index].empty()))
            co_return false;
    }
    co_return true;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::HashLocal(std::string path, uint32_t size, uint8_t* digest)
{
    // The length first: a right start with an extra tail must not pass for the whole file
    Entry st = co_await Vfs(Op::Stat, path, 0, kVfsNormalWaitMs);
    if (!st.ok)
    {
        _error = st.text;
        co_return false;
    }
    if (st.vfs.isDirectory || st.vfs.size != size)
    {
        _error = Printf("SD size %lu, expected %lu", static_cast<unsigned long>(st.vfs.size), static_cast<unsigned long>(size));
        co_return false;
    }
    Entry reset = co_await Vfs(Op::ResetBuffers, {}, 0, kVfsNormalWaitMs);
    if (!reset.ok)
    {
        _error = reset.text;
        co_return false;
    }
    Entry opened = co_await Vfs(Op::OpenRead, path, 0, kVfsNormalWaitMs);
    if (!opened.ok)
    {
        _error = opened.text;
        co_return false;
    }
    co_await Prim0(Prim::HashBegin, size);
    uint32_t done = 0;
    bool ok = true;
    while (done < size)
    {
        if (co_await StopNow())
        {
            ok = false;
            break;
        }
        Entry available = co_await Prim0(Prim::RingToNet);
        if (available.a == 0)
        {
            const uint32_t wanted = std::min<uint32_t>(size - done, ZiFiVfsBridge::kTransferWindow);
            Entry read = co_await Vfs(Op::Read, {}, wanted, kVfsNormalWaitMs);
            if (!read.ok)
                _error = read.text;
            if (!read.ok || read.vfs.transferred == 0)
            {
                ok = false;
                break;
            }
        }
        Entry chunk = co_await Prim0(Prim::HashChunk, size - done);
        if (chunk.a == 0 || done + chunk.a > size)
        {
            ok = false;
            break;
        }
        done += chunk.a;
        co_await AddProgress(chunk.a);
    }
    bool closeOk = false;
    if (ok)
    {
        Entry closed = co_await Vfs(Op::CloseCommit, {}, 0, kVfsCloseWaitMs);
        if (!closed.ok)
            _error = closed.text;
        closeOk = closed.ok;
    }
    else
        closeOk = co_await QuietVfs(Op::CloseCommit, {}, kVfsCloseWaitMs);
    if (!ok || !closeOk)
        co_return false;
    Entry fin = co_await Prim0(Prim::HashFinish);
    std::memcpy(digest, fin.blob.data(), std::min<size_t>(fin.blob.size(), zifigit::Sha1::kDigestSize));
    co_return true;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::HashFiles()
{
    size_t total = 0;
    uint32_t bytes = 0;
    for (const File& f : _files)
    {
        if (f.remote && f.local)
        {
            ++total;
            if (f.localSize == f.remoteSize)
                bytes += f.localSize;
        }
    }
    _progressTotal = bytes;
    _progressDone = 0;
    _progressPercent = 0;
    size_t current = 0;
    for (size_t index = 0; index < _files.size(); ++index)
    {
        if (co_await StopNow())
            co_return false;
        File& entry = _files[index];
        if (!entry.remote)
            entry.status = Status::LocalOnly;
        else if (!entry.local)
            entry.status = Status::New;   // even a protected one: WC does not start without it
        else
        {
            ++current;
            if (!co_await SendState(Phase::Check, static_cast<uint16_t>(current), static_cast<uint16_t>(total), "SHA " + entry.path))
                co_return false;
            bool same = false, readable = true;
            if (entry.localSize == entry.remoteSize)
            {
                uint8_t digest[zifigit::Sha1::kDigestSize] = {};
                readable = co_await HashLocal(LocalPath(entry.path), entry.localSize, digest);
                same = readable && std::memcmp(digest, _files[index].sha, sizeof(digest)) == 0;
            }
            if (co_await StopNow())
                co_return false;   // the reading was cut: the comparison means nothing
            File& e = _files[index];
            if (e.kept)
                e.status = same ? Status::KeptSame : Status::KeptDifferent;
            else if (!readable)
                e.status = Status::ReadError;
            else
                e.status = same ? Status::Same : Status::Different;
        }
        if (!co_await SendEntry(index))
            co_return false;
    }
    co_return true;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::Check()
{
    _files.clear();
    bool ok = co_await FetchRemote();
    if (ok)
        ok = co_await ScanLocal();
    if (!ok)
    {
        const bool stopped = co_await StopNow();
        co_await Fail(stopped ? std::string("stopped") : !_error.empty() ? _error : std::string("check failed"));
        co_return false;
    }
    SortFiles();
    if (!co_await HashFiles())
    {
        const bool stopped = co_await StopNow();
        co_await Fail(stopped ? std::string("stopped") : !_error.empty() ? _error : std::string("check failed"));
        co_return false;
    }
    const bool ready = co_await SendReady();
    co_return ready;
}

// --- apply ---------------------------------------------------------------------------------------------------------

ZiFiWcUpdater::Task ZiFiWcUpdater::WriteLocal(std::string path, uint32_t size, bool* left)
{
    *left = false;
    Entry reset = co_await Vfs(Op::ResetBuffers, {}, 0, kVfsNormalWaitMs);
    if (!reset.ok)
    {
        _error = reset.text;
        co_return false;
    }
    Entry opened = co_await Vfs(Op::OpenWrite, path, 0, kVfsMutateWaitMs);
    if (!opened.ok)
    {
        // Not opened: nothing of ours to delete, and the plugin did not touch a file of that name
        _error = opened.text;
        co_return false;
    }
    *left = true;
    uint32_t offset = 0;
    bool ok = true;
    const uint32_t window = static_cast<uint32_t>(std::min(ZiFiVfsBridge::kTransferWindow, ZiFiVfsBridge::kRingCapacity));
    for (;;)
    {
        if (!ok)
            break;
        if (offset >= size)
        {
            Entry queued = co_await Prim0(Prim::RingQueued);
            if (queued.a == 0)
                break;
        }
        if (co_await StopNow())
        {
            ok = false;
            break;
        }
        while (offset < size)
        {
            Entry free = co_await Prim0(Prim::RingFree);
            if (free.a == 0)
                break;
            Entry written = co_await Prim0(Prim::RingWrite, offset, std::min(size - offset, free.a));
            if (written.a == 0)
                break;
            offset += written.a;
        }
        Entry queued = co_await Prim0(Prim::RingQueued);
        if (queued.a < window && offset < size)
            continue;   // the ring has no whole window yet: more data first
        const uint32_t wanted = std::min<uint32_t>(queued.a, ZiFiVfsBridge::kTransferWindow);
        Entry w = co_await Vfs(Op::Write, {}, wanted, kVfsMutateWaitMs);
        if (!w.ok)
            _error = w.text;
        if (!w.ok || w.vfs.transferred == 0)
            ok = false;
        else
            co_await AddProgress(w.vfs.transferred);
    }
    if (ok)
    {
        Entry closed = co_await Vfs(Op::CloseCommit, {}, 0, kVfsCloseWaitMs);
        if (closed.ok)
            co_return true;
        _error = closed.text;
    }
    // Our own unfinished copy (this OPEN made it): cleared away; the cleanup's failures keep the reason
    co_await QuietVfs(Op::CloseAbort, {}, kVfsCloseWaitMs);
    *left = !co_await QuietVfs(Op::Delete, path, kVfsMutateWaitMs);
    co_return false;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::EnsureDirectory(std::string relativeFile)
{
    size_t slash = relativeFile.find('/');
    while (slash != std::string::npos)
    {
        const std::string prefix = relativeFile.substr(0, slash);
        const std::string partial = "/" + prefix;
        const int folder = DirectoryIndex(prefix);
        Entry st = co_await Vfs(Op::Stat, partial, 0, kVfsNormalWaitMs);
        if (st.ok)
        {
            if (!st.vfs.isDirectory)
            {
                _error = partial + " is a file";
                co_return false;
            }
            // Listed as missing, found now: the card reads with errors and a leftover may hide there
            if (folder >= 0 && !_present[static_cast<size_t>(folder)])
            {
                _diskSuspect = true;
                _error = "SD read errors: CHKDSK";
                co_return false;
            }
        }
        else
        {
            Entry mk = co_await Vfs(Op::Mkdir, partial, 0, kVfsMutateWaitMs);
            if (!mk.ok)
            {
                _error = Clip("cannot create " + partial, 96);
                co_return false;
            }
            if (folder >= 0)
                _present[static_cast<size_t>(folder)] = true;   // made now: no leftovers in it
        }
        slash = relativeFile.find('/', slash + 1);
    }
    co_return true;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::RenameReplace(std::string tempPath, std::string finalPath, std::string finalName,
                                                std::string asidePath, std::string relativeFile, bool keepExisting,
                                                Replace* out)
{
    // The fallback without FILEX MOVE: the old file aside as WCUPD.OLD, the copy onto the name, then the old one
    // deleted. Any RENAME refusal here is an unknown outcome: no copy is deleted, writing stops
    Entry st = co_await Vfs(Op::Stat, finalPath, 0, kVfsNormalWaitMs);
    const bool exists = st.ok;
    if (!exists && st.text != "stat-1")
    {
        _error = "SD: cannot stat file";
        *out = Replace::Untouched;
        co_return true;
    }
    if (exists && keepExisting)
    {
        _error = "protected file is on SD";
        *out = Replace::Untouched;
        co_return true;
    }
    if (exists)
    {
        Entry aside = co_await Vfs(Op::Stat, asidePath, 0, kVfsNormalWaitMs);
        if (aside.ok)
        {
            MarkLeftover(relativeFile);
            _error = kLeftoverReason;
            *out = Replace::Untouched;
            co_return true;
        }
        if (aside.text != "stat-1")
        {
            _error = Printf("SD: cannot stat %s", kAsideName);
            *out = Replace::Untouched;
            co_return true;
        }
        if (!co_await RenameEntry(finalPath, kAsideName))
        {
            _error = "rename failed: CHKDSK";
            *out = Replace::Unknown;
            co_return true;
        }
    }
    if (!co_await RenameEntry(tempPath, finalName))
    {
        if (exists)
            co_await RenameEntry(asidePath, finalName);   // an attempt to put the old one back
        _error = "rename failed: CHKDSK";
        *out = Replace::Unknown;
        co_return true;
    }
    if (exists && !co_await QuietVfs(Op::Delete, asidePath, kVfsMutateWaitMs))
        MarkLeftover(relativeFile);   // WCUPD.OLD stays: the folder is read-only for this session
    *out = Replace::Done;
    co_return true;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::VfsReplace(std::string tempPath, std::string finalPath, std::string finalName,
                                             std::string asidePath, std::string relativeFile, bool keepExisting,
                                             Replace* out)
{
    // FILEX MOVE_RENAME with REPLACE (WC Improved): one sector write moves the name onto the copy; a protected file
    // moves without REPLACE (FILEX refuses if it is there after all)
    Entry m = co_await VfsMove(tempPath, finalPath, !keepExisting, kVfsMutateWaitMs);
    if (!m.ok && m.text == "vfs busy")
    {
        _error = "vfs busy";
        *out = Replace::Untouched;
        co_return true;
    }
    if (!m.ok)
        _error = m.text;
    if (m.ok || m.vfs.status == kFilexCommittedCleanup)
    {
        *out = Replace::Done;   // #25: moved, the old chain's clusters are lost
        co_return true;
    }
    if (m.vfs.status == kMoveUnsupported)
    {
        co_await RenameReplace(tempPath, finalPath, finalName, asidePath, relativeFile, keepExisting, out);
        co_return true;
    }
    if (m.vfs.status == 1 || (m.vfs.status >= 0x10 && m.vfs.status <= 0x1D))
    {
        _error = Clip("replace refused: " + m.vfs.error, 96);
        *out = Replace::Untouched;
        co_return true;
    }
    _error = Clip("check the disk: replace " + m.vfs.error, 96);
    *out = Replace::Unknown;
    co_return true;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::UpdateFile(size_t index)
{
    File& entry = _files[index];
    entry.reason.clear();
    std::string refuse;
    if (_diskSuspect)
        refuse = "check the disk first";
    else if (entry.remoteSize > kMaxFileSize)
        refuse = "file too big for ESP";
    else if (LeftoverIn(entry.path))
        refuse = kLeftoverReason;
    if (!refuse.empty())
    {
        entry.reason = Clip(refuse, 40);
        entry.status = Status::Failed;
        co_return false;
    }
    const std::string finalPath = LocalPath(entry.path);
    const size_t lastSlash = entry.path.rfind('/');
    const std::string folder = lastSlash == std::string::npos ? std::string() : entry.path.substr(0, lastSlash);
    const std::string tempPath = folder.empty() ? "/" + std::string(kTempName) : "/" + folder + "/" + kTempName;
    const std::string asidePath = folder.empty() ? "/" + std::string(kAsideName) : "/" + folder + "/" + kAsideName;
    const std::string finalName = lastSlash == std::string::npos ? entry.path : entry.path.substr(lastSlash + 1);
    const std::string relative = entry.path;
    const uint32_t size = entry.remoteSize;
    uint8_t sha[zifigit::Sha1::kDigestSize];
    std::memcpy(sha, entry.sha, sizeof(sha));
    const bool kept = entry.kept;

    std::string reason;
    // note(): the reason at once, before the cleanup; a stop says "stopped"
    const auto errorOr = [this](const char* fallback) { return _error.empty() ? std::string(fallback) : _error; };
    bool done = false, installed = false, keptThere = false;
    for (unsigned round = 0; round < 2 && !done; ++round)
    {
        if (co_await StopNow())
            break;
        if (kept)
        {
            // A protected file is installed only when missing; found now, it is only compared
            Entry st = co_await Vfs(Op::Stat, finalPath, 0, kVfsNormalWaitMs);
            if (st.ok)
            {
                keptThere = true;
                _files[index].local = true;
                _files[index].localSize = st.vfs.size;
                uint8_t digest[zifigit::Sha1::kDigestSize] = {};
                done = !st.vfs.isDirectory && st.vfs.size == size;
                if (done)
                    done = co_await HashLocal(finalPath, size, digest);
                done = done && std::memcmp(digest, sha, sizeof(digest)) == 0;
                break;
            }
            if (st.text != "stat-1")
            {
                reason = (co_await StopNow()) ? "stopped" : "SD: cannot stat file";
                break;
            }
        }
        if (LeftoverIn(relative))
        {
            reason = (co_await StopNow()) ? "stopped" : kLeftoverReason;
            break;
        }
        _error.clear();
        // download(): the path as GitHub's raw host wants it
        std::string encoded, directory;
        bool fetched = false;
        if (!UrlEncodePath(relative, encoded) || !UrlEncodePath(_directory, directory))
            _error = "path too long";
        else
        {
            Entry d = co_await Fetch(Prim::Download, "/" + _repo + "/" + _commit + "/" + directory + "/" + encoded, size, sha);
            if (!d.ok)
                _error = Clip(d.text, 96);
            fetched = d.ok;
        }
        if (fetched)
            fetched = co_await EnsureDirectory(relative);
        if (!fetched)
        {
            reason = (co_await StopNow()) ? "stopped" : errorOr("download failed");
            break;
        }
        // The copy's name must be free; a leftover is never deleted (it may share a chain with the file)
        Entry probe = co_await Vfs(Op::Stat, tempPath, 0, kVfsNormalWaitMs);
        if (probe.ok)
        {
            MarkLeftover(relative);
            reason = (co_await StopNow()) ? "stopped" : kLeftoverReason;
            break;
        }
        if (probe.text != "stat-1")
        {
            reason = (co_await StopNow()) ? "stopped" : "SD: cannot stat copy";
            break;
        }
        bool staged = false, ours = false;
        for (unsigned attempt = 1; attempt <= kWriteAttempts; ++attempt)
        {
            if (co_await StopNow())
                break;
            _error.clear();
            if (!co_await WriteLocal(tempPath, size, &ours))
            {
                // OPEN refused although STAT found no such name: it is taken (a hidden leftover) or not writable
                const std::string why =
                    _error.rfind("open-", 0) == 0 ? std::string("cannot create WCUPD.TMP: CHKDSK") : errorOr("SD copy: write failed");
                reason = (co_await StopNow()) ? "stopped" : why;
                if (ours)
                    break;   // our unfinished copy did not go away
                continue;
            }
            uint8_t digest[zifigit::Sha1::kDigestSize] = {};
            _error.clear();
            if (!co_await HashLocal(tempPath, size, digest))
                reason = (co_await StopNow()) ? "stopped" : errorOr("SD copy: read failed");
            else if (std::memcmp(digest, sha, sizeof(digest)) != 0)
                reason = (co_await StopNow()) ? "stopped" : "SD copy: SHA mismatch";
            else
            {
                staged = true;
                break;
            }
            if (!co_await QuietVfs(Op::Delete, tempPath, kVfsMutateWaitMs))
                break;
            ours = false;
        }
        if (!staged)
        {
            if (ours)
                MarkLeftover(relative);   // our copy stayed: read-only
            break;
        }
        _error.clear();
        Replace replaced = Replace::Untouched;
        co_await VfsReplace(tempPath, finalPath, finalName, asidePath, relative, kept, &replaced);
        if (replaced == Replace::Untouched)
        {
            reason = (co_await StopNow()) ? "stopped" : errorOr("replace refused");
            if (!co_await QuietVfs(Op::Delete, tempPath, kVfsMutateWaitMs))
            {
                MarkLeftover(relative);
                break;
            }
            if (kept)
                continue;   // the protected file may be on the card after all: the next round compares it
            break;
        }
        installed = installed || replaced == Replace::Done;
        const bool unknown = replaced == Replace::Unknown;
        if (unknown)
            reason = (co_await StopNow()) ? "stopped" : errorOr("replace failed: CHKDSK");
        // The outcome by the card itself, the unknown one too: the file may be in place
        uint8_t digest[zifigit::Sha1::kDigestSize] = {};
        _error.clear();
        const bool read = co_await HashLocal(finalPath, size, digest);
        const bool good = read && std::memcmp(digest, sha, sizeof(digest)) == 0;
        if (unknown)
        {
            done = good;
            _diskSuspect = true;
            break;
        }
        if (good)
            done = true;
        else
            reason = (co_await StopNow()) ? "stopped" : read ? std::string("SD file: SHA mismatch") : errorOr("SD file: read failed");
    }
    if (!done && reason.empty())
        reason = (co_await StopNow()) ? "stopped" : "not updated";
    co_await Prim0(Prim::DropDownload);
    File& e = _files[index];
    e.reason = Clip(reason, 40);
    if (done)
    {
        e.status = keptThere && !installed ? Status::KeptSame : Status::Updated;
        e.local = true;
        e.localSize = e.remoteSize;
    }
    else if (keptThere)
        e.status = Status::KeptDifferent;   // the user's file stays
    else
        e.status = Status::Failed;
    co_return done;
}

ZiFiWcUpdater::Task ZiFiWcUpdater::ApplyFiles(std::vector<uint8_t> indices)
{
    if (_diskSuspect)
    {
        co_await Fail("check the disk first");
        co_return false;
    }
    size_t total = 0;
    uint32_t bytes = 0;
    for (uint8_t index : indices)
    {
        if (index < _files.size() && CanUpdate(_files[index]))
        {
            ++total;
            bytes += 3U * _files[index].remoteSize;
        }
    }
    _progressTotal = bytes;
    _progressDone = 0;
    _progressPercent = 0;
    size_t current = 0;
    for (uint8_t index : indices)
    {
        if (co_await StopNow())
            break;
        if (index >= _files.size() || !CanUpdate(_files[index]))
            continue;
        ++current;
        if (!co_await SendState(Phase::Apply, static_cast<uint16_t>(current), static_cast<uint16_t>(total), "Update " + _files[index].path))
            co_return false;
        co_await UpdateFile(index);
        if (!co_await SendEntry(index))
            co_return false;
        if (_diskSuspect)
        {
            co_await Fail(!_files[index].reason.empty() ? _files[index].reason : std::string("check the disk"));
            co_return false;
        }
    }
    if (co_await StopNow())
    {
        _error = "stopped";
        co_return false;
    }
    const bool ready = co_await SendReady();
    co_return ready;
}

// --- TTD -----------------------------------------------------------------------------------------------------------

namespace
{
constexpr uint8_t kStateVersion = 1;

void SaveVfsResult(ZiFiStateWriter& w, const ZiFiVfsBridge::Result& x)
{
    w.Bool(x.success), w.Bool(x.atEnd), w.Bool(x.wouldBlock), w.Bool(x.isDirectory);
    w.U8(x.status), w.U8(x.appliedAttributes);
    w.U32(x.size);
    w.U16(x.writeDate), w.U16(x.writeTime);
    w.Bool(x.hasMetadata);
    w.U32(x.transferred);
    w.Str(x.name), w.Str(x.error);
}

void LoadVfsResult(ZiFiStateReader& r, ZiFiVfsBridge::Result& x)
{
    x.success = r.Bool(), x.atEnd = r.Bool(), x.wouldBlock = r.Bool(), x.isDirectory = r.Bool();
    x.status = r.U8(), x.appliedAttributes = r.U8();
    x.size = r.U32();
    x.writeDate = r.U16(), x.writeTime = r.U16();
    x.hasMetadata = r.Bool();
    x.transferred = r.U32();
    x.name = r.Str(256), x.error = r.Str(128);
}

void SaveRequest(ZiFiStateWriter& w, const ZiFiWcUpdater::Request& q)
{
    w.U8(static_cast<uint8_t>(q.prim)), w.U8(q.op);
    w.U32(q.a), w.U32(q.b);
    w.Str(q.path), w.Str(q.path2);
    w.Bytes(q.data);
    w.Bool(q.submitted);
}

void LoadRequest(ZiFiStateReader& r, ZiFiWcUpdater::Request& q)
{
    q.prim = static_cast<ZiFiWcUpdater::Prim>(r.U8()), q.op = r.U8();
    q.a = r.U32(), q.b = r.U32();
    q.path = r.Str(1024), q.path2 = r.Str(1024);
    q.data = r.Bytes(4096);
    q.submitted = r.Bool();
}
}  // namespace

void ZiFiWcUpdater::Save(ZiFiStateWriter& w) const
{
    w.U8(kStateVersion);
    w.Bool(_session.h != nullptr);
    if (!_session.h)
        return;
    w.Bytes(_payload);
    w.Bool(_begun);
    w.Bool(_finished), w.Bool(_applying), w.Bool(_stop);
    w.U32(static_cast<uint32_t>(_commands.size()));
    for (const QueuedCommand& c : _commands)
        w.U8(c.kind), w.Bytes(c.indices);
    w.U32(static_cast<uint32_t>(_log.size()));
    for (const Entry& e : _log)
    {
        w.U8(static_cast<uint8_t>(e.prim));
        w.Bool(e.ok);
        w.U32(e.a), w.U32(e.b);
        w.Str(e.text);
        w.Bytes(e.blob);
        if (e.prim == Prim::Vfs)
            SaveVfsResult(w, e.vfs);
        w.U32(static_cast<uint32_t>(e.tree.size()));
        for (const zifigit::TreeEntry& t : e.tree)
        {
            w.Str(t.path);
            w.Bytes(t.sha, sizeof(t.sha));
            w.U32(t.size);
            w.Bool(t.directory);
        }
    }
    SaveRequest(w, _pending);
    w.Bool(_pendingIssued);
    w.U64(_pendingDeadline);
    w.Bool(_reclaiming);
    _sha.Save(w);
    ZiFiHttpFetch::SaveBody(w, _download, _downloadRefs);
    w.U8(_downloadAttempt);
    w.Str(_downloadError);
}

bool ZiFiWcUpdater::Load(ZiFiStateReader& r, const EspStack::ByteSource& bytes)
{
    Release();
    if (r.U8() != kStateVersion)
        return false;
    if (!r.Bool())
        return r.Ok();
    std::vector<uint8_t> payload = r.Bytes(4096);
    const bool begun = r.Bool();
    const bool finished = r.Bool(), applying = r.Bool(), stop = r.Bool();
    const uint32_t commands = r.U32();
    for (uint32_t i = 0; i < commands && r.Ok() && i < 2; ++i)
    {
        QueuedCommand c;
        c.kind = r.U8();
        c.indices = r.Bytes(kMaxFiles);
        _commands.push_back(std::move(c));
    }
    const uint32_t entries = r.U32();
    for (uint32_t i = 0; i < entries && r.Ok(); ++i)
    {
        Entry e;
        e.prim = static_cast<Prim>(r.U8());
        e.ok = r.Bool();
        e.a = r.U32(), e.b = r.U32();
        e.text = r.Str(256);
        e.blob = r.Bytes(4096);
        if (e.prim == Prim::Vfs)
            LoadVfsResult(r, e.vfs);
        const uint32_t tree = r.U32();
        for (uint32_t t = 0; t < tree && r.Ok() && t <= kMaxFiles + kMaxDirectories; ++t)
        {
            zifigit::TreeEntry te{};
            const std::string path = r.Str(sizeof(te.path) - 1);
            std::memcpy(te.path, path.data(), path.size());
            const std::vector<uint8_t> sha = r.Bytes(sizeof(te.sha));
            std::memcpy(te.sha, sha.data(), std::min(sha.size(), sizeof(te.sha)));
            te.size = r.U32();
            te.directory = r.Bool();
            e.tree.push_back(te);
        }
        _log.push_back(std::move(e));
    }
    LoadRequest(r, _pending);
    const bool pendingIssued = r.Bool();
    const uint64_t pendingDeadline = r.U64();
    const bool reclaiming = r.Bool();
    _sha.Load(r);
    const bool complete = ZiFiHttpFetch::LoadBody(r, bytes, _download, _downloadRefs);
    _downloadAttempt = r.U8();
    _downloadError = r.Str(256);
    if (!r.Ok() || !complete)
    {
        Release();
        return false;
    }
    std::string error;
    if (!Configure(payload, error))
    {
        Release();
        return false;
    }
    // Re-run the session against the log: no primitive acts on the outside until the log is used up; the one in
    // flight at the checkpoint is then waited on again (not issued twice)
    _payload = std::move(payload);
    _applying = applying;
    _stop = stop;
    _pendingIssued = pendingIssued;
    _pendingDeadline = pendingDeadline;
    _reclaiming = reclaiming;
    _finished = false;
    _replaying = true;
    _logPos = 0;
    _session = Session();
    _begun = begun;
    if (begun)
        _session.h.resume();
    _replaying = false;
    _finished = finished || _session.h.done();
    return true;
}
