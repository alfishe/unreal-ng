#include "sdb/runtime.h"

#include <algorithm>
#include <cstdio>
#include <deque>

#include "sdb/format.h"
#include "sdb/iconpacks.h"
#include "sdb/scenariorender.h"

namespace sdb::scn
{

namespace
{
std::int64_t NowMs()
{
    return NowUs() / 1000;
}

std::size_t Mix(std::size_t h, std::size_t v)
{
    return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}
std::size_t HashStr(const std::string& s)
{
    return std::hash<std::string>{}(s);
}
std::size_t HashFace(const Face& f)
{
    std::size_t h = HashStr(f.label);
    for (const auto* s : {&f.icon, &f.svg, &f.image, &f.fit, &f.glyph})
        h = Mix(h, HashStr(*s));
    if (f.value)
        h = Mix(h, HashStr(f.value->readout + "|" + f.value->format) + 11);
    return Mix(h, (static_cast<std::size_t>(f.bg.r) << 16 | f.bg.g << 8 | f.bg.b) * 31 +
                      (static_cast<std::size_t>(f.fg.r) << 16 | f.fg.g << 8 | f.fg.b));
}

std::string JoinPath(const std::vector<std::string>& p)
{
    std::string s;
    for (size_t i = 0; i < p.size(); ++i)
        s += (i ? " › " : "") + p[i];
    return s;
}
}  // namespace

Runtime::Runtime(Device& dev, Scenario s, std::unique_ptr<ActionHost> host, RuntimeOptions opt)
    : _dev(dev), _m(dev.GetModel()), _s(std::move(s)), _nav(_s), _opt(opt)
{
    if (!host)
    {
        SystemHostCallbacks cb;
        cb.navigate = [this](const json& r) { Navigate(r); };
        cb.brightness = [this](const json& r) {
            std::lock_guard l(_mu);
            _brightness =
                std::clamp(r.contains("value") ? r.value("value", 70) : _brightness + r.value("delta", 0), 0, 100);
            _brightnessDirty = true;
        };
        cb.note = [this](const std::string& line) { _dev.Note(line); };
        cb.executor = _opt.executor;
        host = MakeSystemHost(_s.settings, cb, &_cancel);
    }
    _host = std::move(host);
    _brightness = _s.settings.brightness;
    _pressed.assign(_m.keys, false);
    _sentKey.assign(_m.keys, 0);
    // faces are drawn at the key's own size: the device pipeline then sends them as they are (sharp, no resize)
    _faceSize = _m.key.w > 0 && _m.key.w == _m.key.h ? _m.key.w : kFaceSize;
    _safePad = _m.keySafeInset > 0 && _m.key.w > 0 ? (_m.keySafeInset * _faceSize + _m.key.w / 2) / _m.key.w : 0;
    _page = Layout(_s, _nav, _m);
    _lastActivity = NowMs();
}

Runtime::~Runtime()
{
    _workerStop = true;
    _cancel = true;
    _qcv.notify_all();
    if (_worker.joinable())
        _worker.join();
    if (_monitor.joinable())
        _monitor.join();
    {
        std::lock_guard l(_steps->m);
        _steps->closed = true;  // a waiting route answers now (its ticket keeps the signal alive)
    }
    _steps->cv.notify_all();
}

void Runtime::Run(const std::atomic<bool>& stop)
{
    if (!_opt.syncActions && !_worker.joinable())
        _worker = std::thread([this] {
            while (!_workerStop)
            {
                Job j;
                {
                    std::unique_lock l(_qm);
                    _qcv.wait_for(l, std::chrono::milliseconds(200), [&] { return !_jobs.empty() || _workerStop; });
                    if (_jobs.empty())
                        continue;
                    j = std::move(_jobs.front());
                    _jobs.pop_front();
                }
                RunJob(j);
            }
        });
    _running = true;
    NoteMenu();
    while (!stop)
        Step(10);
}

// The REST fallback's poller: time travel for the strip and the faces' readouts nobody pushes. Started by the device
// thread while the surface client is not live, ended once it is (a connected deck has no poller thread).
void Runtime::EnsureMonitor()
{
    {
        std::lock_guard l(_mu);
        if (_live || _monitorRunning)
            return;
    }
    if (_monitor.joinable())
        _monitor.join();  // the last one ended (live came): it is gone already
    _monitorRunning = true;
    _monitor = std::thread([this] {
        // time travel on the strip (strip.status "ttd"): the recording span while it records, the position after;
        // and the readouts of the page's state-bound faces nobody pushes (a few times a second, at once after an
        // action)
        auto next = std::chrono::steady_clock::now();
        auto nextReadouts = next;
        while (!_workerStop)
        {
            {
                std::lock_guard l(_mu);
                if (_live)
                {
                    _monitorRunning = false;
                    return;
                }
            }
            // 50 readouts a second on a fixed beat: the poll's own time does not stretch the period
            next += std::chrono::milliseconds(20);
            auto now = std::chrono::steady_clock::now();
            if (next < now)
                next = now;
            std::this_thread::sleep_until(next);
            now = std::chrono::steady_clock::now();
            double fps = 50;
            bool ttd = false;
            std::vector<std::string> paths;
            {
                std::lock_guard l(_mu);
                // live: the surface client pushes the timeline and the readouts; nothing to ask
                ttd = !_live && _page.strip && _page.strip->status == "ttd";
                if (ttd)
                    fps = _page.strip->fps;
                const bool kick = _monitorKick.exchange(false);
                if (!_live && (kick || now >= nextReadouts))
                {
                    nextReadouts = now + std::chrono::milliseconds(std::max(20, _opt.readoutPollMs));
                    // the same few paths a page's keys name: its state-bound faces and its value faces
                    auto add = [&](const std::string& path) {
                        if (!path.empty() && !_pushed.count(path) &&
                            std::find(paths.begin(), paths.end(), path) == paths.end())
                            paths.push_back(path);
                    };
                    for (const Node* n : _page.keys)
                        if (n)
                        {
                            add(n->state);
                            for (const Face* f : {&n->face, n->faceOn ? &*n->faceOn : nullptr})
                                if (f && f->value)
                                    add(f->value->readout);
                        }
                }
            }
            if (ttd)
            {
                const json t = _host->TtdState();
                std::string text = TtdReadout(t, fps);
                std::lock_guard l(_mu);
                if (!_live && _page.strip && _page.strip->status == "ttd")
                    _status = std::move(text);
            }
            if (!paths.empty())
            {
                // one ask for the page's states and values (the system host: one request)
                const json values = _host->Readouts(paths);
                std::lock_guard l(_mu);
                for (const auto& [path, v] : values.items())
                    if (!v.is_null() && !_pushed.count(path))
                        _readouts[path] = v;
            }
        }
        std::lock_guard l(_mu);
        _monitorRunning = false;
    });
}

// One line for the strip's status: frames and time codes from the start of the recording. Every form is the same
// length (kStatusChars, padded), so the strip lays it out once and nothing moves while it changes.
std::string Runtime::TtdReadout(const json& t, double fps)
{
    auto pad = [](std::string s) {
        s.resize(std::max<size_t>(s.size(), kStatusChars), ' ');
        return s;
    };
    if (!t.is_object())
        return pad("TTD NOT AVAILABLE");
    const std::string state = t.value("state", "");
    const long long first = t.value("first", 0LL), last = t.value("last", 0LL), pos = t.value("pos", last);
    auto tc = [fps](long long frames) {
        const double s = std::max(0LL, frames) / fps;
        return sdb::Format("{:02}:{:02}.{:02}", static_cast<int>(s) / 60, static_cast<int>(s) % 60,
                           static_cast<int>(std::fmod(s, 1.0) * fps));
    };
    const long long len = std::max(0LL, last - first), at = std::clamp(pos - first, 0LL, len);
    if (state == "recording")
        return pad(sdb::Format("REC {:06}-{:06}  {}-{}", 0, len, tc(0), tc(len)));
    if (state == "detached" || state == "stopped" || state == "paused")
        return pad(sdb::Format("AT  {:06}/{:06}  {}/{}", at, len, tc(at), tc(len)));
    return pad("READY - PRESS TO RECORD");
}

void Runtime::Step(int timeoutMs)
{
    for (const auto& e : _dev.PollEvents(timeoutMs))
    {
        std::lock_guard l(_mu);
        Handle(e, NowMs());
    }
    bool force = false;
    bool reloaded = false;
    std::uint64_t handled = 0;  // the requests this step applies: everything queued below
    {
        std::lock_guard l(_mu);
        handled = _posted;
        while (!_injected.empty())
        {
            const Event e = _injected.front();
            _injected.pop_front();
            Handle(e, NowMs());
        }
        while (!_navRequests.empty())
        {
            const json r = _navRequests.front();
            _navRequests.pop_front();
            ApplyNav(r);
        }
        if (_pendingReload)
        {
            // the stack's ids first: its nodes live in the scenario the assignment below destroys
            const auto ids = _nav.Ids();
            _s = std::move(*_pendingReload);
            _pendingReload.reset();
            _nav.Rebind(_s, ids);
            _toggles.clear();
            _holds.clear();
            force = true;
            reloaded = true;
            _dev.Note("scenario reloaded: " + _s.name + ", menu " + JoinPath(Layout(_s, _nav, _m).path));
        }
        // auto exit: pop one level after the menu's idle time
        if (_page.autoExit > 0 && _nav.Depth() > 0 && NowMs() - _lastActivity > _page.autoExit * 1000LL)
        {
            _nav.Back();
            _lastActivity = NowMs();
            _dev.Note("auto exit -> " + JoinPath(Layout(_s, _nav, _m).path));
        }
        _page = Layout(_s, _nav, _m);
    }
    if (_running && !_opt.syncActions)
        EnsureMonitor();
    Render(force);
    if (reloaded && onScenarioChanged)
        onScenarioChanged();
    {
        std::lock_guard l(_steps->m);
        _steps->applied = std::max(_steps->applied, handled);
    }
    _steps->cv.notify_all();
}

Runtime::Ticket Runtime::Posted() const
{
    std::lock_guard l(_mu);
    Ticket t;
    t._signal = _steps;
    t._seq = _posted;
    return t;
}

bool Runtime::Ticket::Wait(std::chrono::milliseconds timeout) const
{
    if (!_signal)
        return false;
    std::unique_lock l(_signal->m);
    _signal->cv.wait_for(l, timeout, [&] { return _signal->applied >= _seq || _signal->closed; });
    return _signal->applied >= _seq;
}

void Runtime::Render(bool force)
{
    std::vector<std::pair<int, Rgb>> keys;
    std::optional<Rgb> strip;
    int brightness = -1;
    {
        std::lock_guard l(_mu);
        if (_brightnessDirty)
        {
            brightness = _brightness;
            _brightnessDirty = false;
        }
        const std::int64_t now = NowMs();
        for (auto it = _flashUntil.begin(); it != _flashUntil.end();)
            it = now >= it->second ? _flashUntil.erase(it) : std::next(it);
        for (int k = 0; k < _m.keys; ++k)
        {
            // an activity flash looks like a press (scenarios.md §5)
            const bool pressed = _pressed[static_cast<size_t>(k)] || _flashUntil.count(k);
            std::size_t h = 1;
            Rgb img;
            if (k == _page.backKey)
            {
                const std::string parent = _page.path.size() >= 2 ? _page.path[_page.path.size() - 2] : "";
                h = Mix(Mix(HashStr("back:" + parent), _page.path.size()), pressed);
                if (force || h != _sentKey[static_cast<size_t>(k)])
                    img = RenderBackKey(parent, static_cast<int>(_page.path.size()) - 1, pressed, _safePad, _faceSize);
            }
            else if (const Node* n = _page.keys[static_cast<size_t>(k)])
            {
                const bool on = IsOn(*n);
                const Face& f = on && n->faceOn ? *n->faceOn : n->face;
                h = Mix(Mix(Mix(HashFace(f), HashStr(n->id + n->title)), pressed), n->IsMenu());
                const std::string pack = f.glyph.empty() ? std::string() : PackLocked();
                if (!f.glyph.empty())
                    h = Mix(h, HashStr(pack));  // another pack: the key is drawn again
                std::optional<ValueShown> shown;
                if (f.value)
                {
                    // a value face: drawn again only when its text or level changed (a readout pushed at 10 Hz
                    // whose other fields move redraws nothing)
                    shown = ShownLocked(*f.value);
                    h = Mix(Mix(h, HashStr(shown->text) + 13), static_cast<std::size_t>(shown->level) + 17);
                }
                const bool unavailable = !UnavailableLocked(n->action).empty();
                h = Mix(h, unavailable ? 29 : 31);
                if (force || h != _sentKey[static_cast<size_t>(k)])
                {
                    img = RenderFace(f, n->title, pressed, n->IsMenu(), _safePad, _faceSize, _s.dir, pack,
                                     shown ? &*shown : nullptr);
                    if (unavailable)
                        DimUnavailable(img);
                }
            }
            if (h == 1 && (force || _sentKey[static_cast<size_t>(k)] != 1))
                img = RenderEmptyKey(_faceSize);
            if (!img.Empty())
            {
                keys.emplace_back(k, std::move(img));
                _sentKey[static_cast<size_t>(k)] = h;
            }
        }
        if (_m.HasLcd())
        {
            const bool bannerOn = NowMs() < _bannerUntil;
            std::size_t h = Mix(HashStr(JoinPath(_page.path)), static_cast<std::size_t>(_page.crumbs));
            if (_page.strip)
                h = Mix(h, HashStr(_page.strip->content + _page.strip->text + _page.strip->image));
            for (const Dial* d : _page.dials)
                h = Mix(h, d ? HashStr(d->label) + 1 : 0);
            if (bannerOn)
                h = Mix(h, HashStr(_banner));
            h = Mix(h, HashStr(_status) + 3);
            StripLive live;
            live.link = _live ? StripLive::Link::Live : _wasLive ? StripLive::Link::Lost : StripLive::Link::None;
            live.readouts = &_strip;
            live.target = &_target;
            live.notice = _notice;
            if (NowMs() < _activityUntil)
                live.activity = _activityLine;
            live.showTarget = _page.strip ? _page.strip->target.value_or(true) : true;
            if (NowMs() < _dialHintUntil)
                live.dialHint = _dialHint;
            live.bannerInfo = _bannerInfo;
            h = Mix(h, HashStr(live.Key()) + 5);
            double left = -1;  // auto exit countdown: redrawn at 30 fps so the bar glides
            if (_page.autoExit > 0 && _nav.Depth() > 0)
            {
                left = std::max(0.0, _page.autoExit - (NowMs() - _lastActivity) / 1000.0);
                h = Mix(h, static_cast<std::size_t>(left * 30) + 7);
            }
            if (force || h != _sentStrip)
            {
                strip = RenderStrip(_page, _m.lcdLogicalW, _m.lcdLogicalH, _m.dialTileW,
                                    bannerOn ? _banner : std::string(), left, _status, _s.dir, &live);
                _sentStrip = h;
            }
        }
        _stats.keyWrites += keys.size();
        if (strip)
            ++_stats.stripWrites;
    }
    std::string pageJson;
    {
        std::lock_guard l(_mu);
        pageJson = PageLocked().dump();
        if (pageJson == _sentPage && !force)
            pageJson.clear();
        else
            _sentPage = pageJson;
    }
    if (!pageJson.empty())
        _dev.PublishPage(pageJson);
    if (brightness >= 0)
        _dev.SetBrightness(brightness);
    // flat graphics (pixel font, icons, chips): JPEG at 100, so no ringing round their hard edges
    constexpr int kQuality = 100;
    for (auto& [k, img] : keys)
        (void)_dev.SetKeyImage(k, img, kQuality);
    if (strip)
    {
        // only what changed: runs of columns that differ, full height (the window takes partial writes that span its
        // height); a live readout at 50 fps sends the few digits that turned, each run on its own
        std::vector<std::pair<int, int>> runs;
        if (!force && _stripPrev.w == strip->w && _stripPrev.h == strip->h)
        {
            std::vector<char> dirty(static_cast<size_t>(strip->w), 0);
            for (int y = 0; y < strip->h; ++y)
                for (int x = 0; x < strip->w; ++x)
                    if (!dirty[static_cast<size_t>(x)] && std::memcmp(strip->At(x, y), _stripPrev.At(x, y), 3) != 0)
                        dirty[static_cast<size_t>(x)] = 1;
            for (int x = 0; x < strip->w; ++x)
            {
                if (!dirty[static_cast<size_t>(x)])
                    continue;
                if (!runs.empty() && x - runs.back().second < 24)
                    runs.back().second = x + 1;  // a short gap: one write
                else
                    runs.emplace_back(x, x + 1);
            }
        }
        else
        {
            runs.emplace_back(0, strip->w);
        }
        for (auto [x0, x1] : runs)
        {
            x0 = std::max(0, x0 - 2) & ~7;  // a little margin, aligned: JPEG blocks
            x1 = std::min(strip->w, ((x1 + 2 + 7) & ~7));
            if (x0 == 0 && x1 == strip->w)
                (void)_dev.SetLcdFull(*strip, kQuality);
            else
                (void)_dev.SetLcdRegion(x0, 0, Crop(*strip, x0, 0, x1 - x0, strip->h), kQuality);
        }
        _stripPrev = std::move(*strip);
    }
}

// ---------------------------------------------------------------- input
void Runtime::Handle(const Event& e, std::int64_t now)
{
    _lastActivity = now;
    switch (e.type)
    {
        case EventType::KeyDown:
            if (e.index < 0 || e.index >= _m.keys)
                return;
            if (_nav.Current().readonly && e.index != _page.backKey)
                if (const Node* n = _page.keys[static_cast<size_t>(e.index)]; !n || !n->IsMenu())
                    return;  // a read-only menu (monitoring): its keys only show, a press changes nothing (its
                             // pages still open)
            _pressed[static_cast<size_t>(e.index)] = true;
            _downAt[e.index] = now;
            if (e.index != _page.backKey)
                Activate(e.index);  // the back key acts on release (hold = home)
            break;
        case EventType::KeyUp: {
            if (e.index < 0 || e.index >= _m.keys)
                return;
            _pressed[static_cast<size_t>(e.index)] = false;
            if (auto held = _holds.find(e.index); held != _holds.end())
            {  // the release of a held action (hold: true): the job the press made, on whatever page shows now
                Job release = std::move(held->second);
                _holds.erase(held);
                Enqueue(std::move(release));
            }
            if (e.index == _page.backKey && _downAt.count(e.index))
            {
                const bool hold = now - _downAt[e.index] >= _opt.holdMs;
                if (hold && _s.settings.homeOnHold)
                    _nav.Home();
                else
                    _nav.Back();
                _page = Layout(_s, _nav, _m);
                NoteMenu();
            }
            _downAt.erase(e.index);
            break;
        }
        case EventType::DialTurn:
            if (e.index >= 0 && e.index < static_cast<int>(_page.dials.size()))
                if (const Dial* d = _page.dials[static_cast<size_t>(e.index)]; d && d->turn)
                {
                    // how fast it turns (detents a second, smoothed): actions accelerate with it (ttd_seek)
                    auto& [lastMs, rate] = _turnRate[e.index];
                    const double inst =
                        lastMs > 0 ? std::abs(e.delta) * 1000.0 / std::max<std::int64_t>(1, now - lastMs) : 0.0;
                    rate = now - lastMs > 400 ? inst * 0.3 : rate * 0.5 + inst * 0.5;
                    lastMs = now;
                    _dialHint = e.index, _dialHintUntil = now + 1500;  // its label over the timeline while it turns
                    Enqueue({*d->turn, sdb::Format("dial {}", e.index), {{"ticks", e.delta}, {"rate", rate}}, "stay"});
                }
            break;
        case EventType::DialDown:
            if (e.index >= 0 && e.index < static_cast<int>(_page.dials.size()))
                if (const Dial* d = _page.dials[static_cast<size_t>(e.index)]; d && d->press)
                {
                    Job j{*d->press, sdb::Format("dial {} press", e.index), json::object(), "stay"};
                    if (d->press->kind == "action" && d->press->spec.value("hold", false))
                    {
                        j.phase = 1;
                        Job release = j;
                        release.phase = -1;
                        _holds[1000 + e.index] = std::move(release);
                    }
                    Enqueue(std::move(j));
                }
            break;
        case EventType::DialUp:
            if (auto held = _holds.find(1000 + e.index); held != _holds.end())
            {
                Job release = std::move(held->second);
                _holds.erase(held);
                Enqueue(std::move(release));
            }
            break;
        case EventType::LcdTap:
            if (!_notice.empty() && onNoticeTap)
            {
                onNoticeTap();  // NOT PAIRED / REVOKED: the tap pairs
                break;
            }
            if (SeekOnTimeline(e.x, e.x, false))
                break;
            if (_page.crumbs == Crumbs::Strip)
            {
                const int depth = CrumbHit(_page.path, _m.lcdLogicalW, e.x);
                if (depth >= 0 && static_cast<size_t>(depth) < _nav.Depth())
                {
                    _nav.PopTo(static_cast<size_t>(depth));
                    _page = Layout(_s, _nav, _m);
                    NoteMenu();
                }
            }
            break;
        case EventType::LcdSwipe:
            if (SeekOnTimeline(e.x, e.x2, true))
                break;
            if (e.x2 > e.x + 40 && _nav.Back())
            {
                _page = Layout(_s, _nav, _m);
                NoteMenu();
            }
            break;
        case EventType::LcdLongPress:
            if (_nav.Depth() > 0)
            {
                _nav.Home();
                _page = Layout(_s, _nav, _m);
                NoteMenu();
            }
            break;
        default:
            break;
    }
}

// The host's reason an action cannot run now ("" = it can, or nobody said): the actions.unavailable readout
std::string Runtime::UnavailableLocked(const Action& a) const
{
    if (a.kind != "action")
        return {};
    const auto it = _readouts.find("actions.unavailable");
    if (it == _readouts.end() || !it->second.is_object())
        return {};
    const auto r = it->second.find(a.spec.value("id", ""));
    return r != it->second.end() && r->is_string() ? r->get<std::string>() : std::string();
}

// An unavailable action's key: the face at a third of its light, so it reads as off but stays recognisable
void Runtime::DimUnavailable(Rgb& img)
{
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
        {
            std::uint8_t* p = img.At(x, y);
            const int grey = (p[0] * 3 + p[1] * 6 + p[2]) / 10;  // half the colour goes
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<std::uint8_t>((p[c] + grey) / 2 * 35 / 100);
        }
}

void Runtime::Activate(int key)
{
    const Node* n = _page.keys[static_cast<size_t>(key)];
    if (!n)
        return;
    if (const std::string why = UnavailableLocked(n->action); !why.empty())
    {
        SetBanner(why, 2500, true);  // greyed out: say why instead of sending what the host would refuse
        return;
    }
    if (n->IsMenu())
    {
        _nav.Enter(*n);
        _page = Layout(_s, _nav, _m);
        NoteMenu();
        return;
    }
    if (n->action.kind.empty())
        return;  // a display-only key (a value face without "do")
    Job j{n->action, n->id, json::object(), n->then, n->action.kind == "toggle"};
    j.state = n->state;
    if (n->action.kind == "action" && n->action.spec.value("hold", false))
    {
        // press now, release on the key's release (KeyUp); "then" follows the release
        j.phase = 1;
        Job release = j;
        release.phase = -1;
        j.then = "stay";
        _holds[key] = std::move(release);
    }
    Enqueue(std::move(j));
}

void Runtime::Enqueue(Job j)
{
    ++_stats.actions;
    if (_opt.syncActions)
    {
        RunJob(j);
        return;
    }  // tests: m_ is held by the caller; runJob takes no lock on m_ for state below
    {
        std::lock_guard l(_qm);
        // a dial turning faster than its action runs: the waiting turn takes the new detents (one action, not a
        // backlog)
        if (!_jobs.empty() && j.vars.contains("ticks") && _jobs.back().node == j.node &&
            _jobs.back().vars.contains("ticks"))
        {
            auto& w = _jobs.back().vars;
            w["ticks"] = w["ticks"].get<int>() + j.vars["ticks"].get<int>();
            w["rate"] = std::max(w.value("rate", 0.0), j.vars.value("rate", 0.0));
        }
        else
        {
            _jobs.push_back(std::move(j));
        }
    }
    _qcv.notify_one();
}

void Runtime::RunJob(Job& j)
{
    // toggle state: read and written under m_ (syncActions: the caller already holds m_)
    bool state = false;
    auto withLock = [this](auto&& f) {
        if (_opt.syncActions)
            f();
        else
        {
            std::lock_guard l(_mu);
            f();
        }
    };
    std::string dir;
    withLock([&] {
        if (j.toggle)
            state = _toggles[j.node];
        dir = _s.dir;
    });
    json vars = j.vars;
    if (j.phase != 0)
        vars["phase"] = j.phase;
    if (!dir.empty())
        vars["__dir"] = dir;
    ActionResult r;
    if (_opt.dryRun)
        r = {true, "dry run: " + j.action.kind + " " + j.action.spec.dump()};
    else
    {
        _cancel = false;
        try
        {
            r = Execute(j.action, *_host, vars, j.toggle ? &state : nullptr);
        }
        catch (const std::exception& e)
        {
            r = {false, std::string("error: ") + e.what()};
        }  // one bad action never stops the deck
    }
    const std::string line = sdb::Format("{} {}: {}", r.ok ? "action" : "FAILED", j.node, r.message);
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
    _dev.Note(line);
    withLock([&] {
        if (j.toggle && r.ok)
            _toggles[j.node] = state;
        // a state-bound face nobody reports yet: flips on success, as a toggle does (the readout wins once it comes)
        if (!j.toggle && r.ok && j.phase == 0 && !j.state.empty() && !_readouts.count(j.state))
            _toggles[j.node] = !_toggles[j.node];
        _last.push_back({{"node", j.node}, {"ok", r.ok}, {"message", r.message}});
        if (_last.size() > 20)
            _last.erase(_last.begin());
        if (!r.ok)
            FailureBanner(j, r.message);
        if (r.ok && !r.status.empty() && !(_page.strip && _page.strip->status == "ttd"))
            _status = r.status;
        _monitorKick = true;  // the readout follows at once
        if (r.ok && j.then != "stay")
            _navRequests.push_back({{"to", j.then}});
    });
}

void Runtime::ApplyNav(const json& r)
{
    const std::string to = r.value("to", "");
    bool moved = false;
    if (to == "back")
        moved = _nav.Back();
    else if (to == "home")
    {
        moved = _nav.Depth() > 0;
        _nav.Home();
    }
    else if (to == "node")
        moved = _nav.GoTo(r.value("node", ""));
    else if (to == "scenario" && onSwitchScenario)
        onSwitchScenario(r.value("file", ""));
    else if (to == "context")
    {
        // the nearest menu declaring it: the current path (innermost first), below the current menu, then anywhere
        const std::string ctx = r.value("context", "");
        const Node* target = nullptr;
        const auto& stack = _nav.Stack();
        for (auto it = stack.rbegin(); it != stack.rend() && !target; ++it)
            if ((*it)->context == ctx)
                target = *it;
        auto below = [&](const Node& from) -> const Node* {
            std::deque<const Node*> q{&from};
            while (!q.empty())
            {
                const Node* n = q.front();
                q.pop_front();
                if (n->IsMenu() && n->context == ctx)
                    return n;
                for (const auto& c : n->items)
                    q.push_back(&c);
            }
            return nullptr;
        };
        if (!target && !ctx.empty())
            target = below(_nav.Current());
        if (!target && !ctx.empty())
            target = below(_s.root);
        _context = ctx;  // the core's: not sent back
        _contextSent = true;
        if (target && target != &_nav.Current())
            moved = _nav.GoTo(target->id);
    }
    if (moved)
    {
        _page = Layout(_s, _nav, _m);
        NoteMenu();
        _lastActivity = NowMs();
    }
}

// A tap or a swipe on the TTD timeline (a page that shows it, live): a tap seeks to the frame under the finger; a
// swipe steps the position its way. The strip reports a swipe once, about 50 px after it starts (never where the
// finger stops), so its length says nothing: a swipe is a step of a second, doubled by each quick swipe the same way
// (within 1 s: 1, 2, 4, 8 s), at most a quarter of the history. False: not on a timeline.
bool Runtime::SeekOnTimeline(int x, int x2, bool swipe)
{
    if (!_live || !PageScrubsTime(_page))
        return false;
    const auto p = _strip.find("ttd.position");
    if (p == _strip.end() || !p->second.is_object())
        return false;
    const auto [x0, x1] = TimelineSpan(_page, _m.lcdLogicalW);
    if (!swipe && (x < x0 - 6 || x > x1 + 6))
        return false;  // the chips: the crumbs' own tap
    const json& t = p->second;
    const long long first = t.value("first", 0LL), end = std::max(first, t.value("end", 0LL));
    if (!t.value("history", end > first) || end <= first)
    {
        SetBanner("NO TTD HISTORY - PRESS TTD REC TO RECORD", 2500, true);
        return true;
    }
    const double span = std::max(1, x1 - x0 - 1);
    long long frame;
    if (swipe)
    {
        const int dir = x2 < x ? -1 : 1;
        const std::int64_t now = NowMs();
        _swipeStep = dir == _swipeDir && now - _swipeAt < 1000 ? std::min(_swipeStep * 2, 8) : 1;
        _swipeDir = dir, _swipeAt = now;
        const double fps = t.contains("fps") && t["fps"].is_number() ? t["fps"].get<double>() : 50.0;
        const long long step = std::min(std::llround(_swipeStep * fps), std::max(1LL, (end - first) / 4));
        frame = t.value("frame", end) + dir * step;
    }
    else
        frame = first + std::llround(static_cast<double>(std::clamp(x, x0, x1 - 1) - x0) / span * static_cast<double>(end - first));
    frame = std::clamp(frame, first, end);
    Action seek;
    seek.kind = "action";
    seek.spec = {{"kind", "action"}, {"id", "ttd.seek"}, {"value", {{"frame", frame}}}};
    Enqueue({seek, swipe ? "strip swipe" : "strip tap", json::object(), "stay"});
    _lastActivity = NowMs();
    return true;
}

void Runtime::SetBanner(const std::string& text, int ms, bool info)
{
    _banner = text;
    _bannerUntil = NowMs() + ms;
    _bannerInfo = info;
}

// A failed step on the strip. What the host refused for the state it is in (409: the start of the history, nothing
// recorded, not now) is information in the calm look, in the host's words; a real error is red with its reason.
void Runtime::FailureBanner(const Job& j, const std::string& message)
{
    // the message: "action <id> -> <error> (<ms>, <path>): <text>"
    std::string error, text;
    if (const auto arrow = message.find(" -> "); arrow != std::string::npos)
    {
        const auto paren = message.find(" (", arrow);
        error = message.substr(arrow + 4, paren == std::string::npos ? std::string::npos : paren - arrow - 4);
        if (const auto colon = message.find("): ", arrow); colon != std::string::npos)
            text = message.substr(colon + 3);
    }
    const std::string id = j.action.kind == "action" ? j.action.spec.value("id", "") : j.action.kind;
    const bool ttd = id.rfind("ttd.", 0) == 0 || id == "ttd_seek";
    // nothing recorded (no history, or one emptied: its start is its end): say how to get one
    if (ttd && id != "ttd.record_toggle" && id != "ttd.start" && id != "ttd.load")
        if (const auto p = _strip.find("ttd.position"); p != _strip.end() && p->second.is_object())
        {
            const json& t = p->second;
            auto frameOf = [](const json& v) {
                return v.is_object() ? v.value("frame", 0LL) : v.is_number() ? v.get<long long>() : 0LL;
            };
            const long long first = t.contains("first") ? frameOf(t["first"]) : frameOf(t.value("first_frame", json()));
            const long long end = t.contains("end") ? frameOf(t["end"]) : frameOf(t.value("session_end", json()));
            if (!t.value("history", end > first) || end <= first)
            {
                SetBanner("NO TTD HISTORY - PRESS TTD REC TO RECORD", 2500, true);
                return;
            }
        }
    if (error == "Conflict" || error == "not_now" || error == "held_by_session" || error == "Unavailable")
    {
        SetBanner(text.empty() ? "NOT NOW" : text, 2000, true);
        return;
    }
    SetBanner(j.node + " FAILED" + (text.empty() ? std::string() : ": " + text), 2500, false);
}

void Runtime::NoteMenu()
{
    _status.clear();  // a readout belongs to the menu that showed it
    _dialHint = StripLive::kAllDials, _dialHintUntil = NowMs() + 2000;  // a page's dial labels for a moment
    _dev.Note("menu: " + JoinPath(_page.path));
    // contexts on menus (scenarios.md §7): the innermost menu of the path that declares one sets it; leaving every
    // such menu returns to the default ("")
    std::string ctx;
    const auto& stack = _nav.Stack();
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
        if (!(*it)->context.empty())
        {
            ctx = (*it)->context;
            break;
        }
    if (ctx == _context && (_contextSent || ctx.empty()))
        return;
    _context = ctx;
    _contextSent = true;
    const json spec{{"kind", "action"}, {"id", "ctx.set"}, {"value", ctx}};
    Enqueue({Action{"action", spec}, "context " + (ctx.empty() ? std::string("(default)") : ctx), json::object(),
             "stay"});
}

bool Runtime::IsOn(const Node& n) const
{
    if (!n.state.empty())
        if (auto it = _readouts.find(n.state); it != _readouts.end())
        {
            const json& v = it->second;  // true / false, a number, or a readout object ({on}: toggle.<id>)
            if (v.is_object())
                return v.contains("on") && v["on"].is_boolean() && v["on"].get<bool>();
            return v.is_boolean() ? v.get<bool>() : v.is_number() ? v.get<double>() != 0 : !v.is_null();
        }
    auto t = _toggles.find(n.id);
    return t != _toggles.end() && t->second;
}

std::map<std::string, bool> Runtime::OnStates() const
{
    std::map<std::string, bool> on = _toggles;
    for (const Node* n : _page.keys)
        if (n && !n->state.empty())
            on[n->id] = IsOn(*n);
    return on;
}

void Runtime::SetReadout(const std::string& path, const json& value)
{
    std::lock_guard l(_mu);
    _pushed[path] = true;
    _readouts[path] = value;
}

void Runtime::SetLive(bool live)
{
    std::lock_guard l(_mu);
    if (live == _live)
        return;
    _live = live;
    if (live)
    {
        _wasLive = true;
        _status.clear();  // the timeline replaces the polled line
    }
    else
    {
        _pushed.clear();  // the faces' readouts are polled again
        _strip.clear();
        _monitorKick = true;
    }
}

void Runtime::SetStripReadout(const std::string& name, const json& value)
{
    std::lock_guard l(_mu);
    _strip[name] = value;
}

void Runtime::SetTarget(const json& target)
{
    std::lock_guard l(_mu);
    _target = target;
}

void Runtime::SetNotice(const std::string& text)
{
    std::lock_guard l(_mu);
    _notice = text;
}

std::string Runtime::Notice() const
{
    std::lock_guard l(_mu);
    return _notice;
}

void Runtime::SetActivityLook(const std::string& look)
{
    std::lock_guard l(_mu);
    _activityLook = look;
}

void Runtime::SetPack(const std::string& pack)
{
    std::lock_guard l(_mu);
    _pack = pack;
    ++_posted;  // a Ticket taken now is done once a step has drawn the keys in the new pack
}

std::string Runtime::PackSetting() const
{
    std::lock_guard l(_mu);
    return _pack;
}

std::string Runtime::Pack() const
{
    std::lock_guard l(_mu);
    return PackLocked();
}

std::string Runtime::ScenarioPack() const
{
    std::lock_guard l(_mu);
    return _s.settings.pack;
}

std::string Runtime::PackLocked() const
{
    if (!_pack.empty())
        return _pack;
    return _s.settings.pack.empty() ? std::string(packs::kDefaultPack) : _s.settings.pack;
}

SurfaceNeeds Runtime::Needs() const
{
    std::lock_guard l(_mu);
    return NeedsOf(_s);
}

void Runtime::OnContext(const std::string& context)
{
    std::lock_guard l(_mu);
    _navRequests.push_back({{"to", "context"}, {"context", context}});
    ++_posted;
}

std::string Runtime::Context() const
{
    std::lock_guard l(_mu);
    return _context;
}

void Runtime::ShowActivity(const std::string& actionId, const std::string& text)
{
    std::lock_guard l(_mu);
    Activity a = _s.settings.activity;
    if (_activityLook == "off")
        a.enabled = false;
    else if (_activityLook == "flash" || _activityLook == "line")
    {
        a.enabled = true;
        a.flashKeys = true;
        a.strip = _activityLook == "line" ? "line" : "none";
    }
    if (!a.enabled || std::find(a.ignore.begin(), a.ignore.end(), actionId) != a.ignore.end())
        return;
    if (a.flashKeys)
        for (int k = 0; k < static_cast<int>(_page.keys.size()); ++k)
            if (const Node* n = _page.keys[static_cast<size_t>(k)]; n && !n->IsMenu() && n->activity &&
                                                                     n->action.kind == "action" &&
                                                                     n->action.spec.value("id", "") == actionId)
                _flashUntil[k] = NowMs() + 200;
    if (a.strip == "line" && !text.empty())
    {
        // a quiet line, not the failure banner
        _activityLine = text;
        _activityUntil = NowMs() + static_cast<std::int64_t>(a.seconds * 1000);
    }
}

// ---------------------------------------------------------------- controls
void Runtime::Reload(Scenario s)
{
    std::lock_guard l(_mu);
    _pendingReload = std::move(s);
    ++_posted;
}

void Runtime::Inject(const Event& e)
{
    std::lock_guard l(_mu);
    _injected.push_back(e);
    ++_posted;
}

bool Runtime::PressNode(const std::string& id, std::string* error)
{
    std::lock_guard l(_mu);
    for (int k = 0; k < static_cast<int>(_page.keys.size()); ++k)
        if (_page.keys[static_cast<size_t>(k)] && _page.keys[static_cast<size_t>(k)]->id == id)
        {
            _injected.push_back({EventType::KeyDown, k});
            _injected.push_back({EventType::KeyUp, k});
            ++_posted;
            return true;
        }
    if (error)
        *error = "'" + id + "' is not on the current page (" + JoinPath(_page.path) + ")";
    return false;
}

void Runtime::Navigate(const json& request)
{
    std::lock_guard l(_mu);
    _navRequests.push_back(request);
    ++_posted;
}

void Runtime::SetBrightness(int percent)
{
    std::lock_guard l(_mu);
    _brightness = std::clamp(percent, 0, 100);
    _brightnessDirty = true;
}

int Runtime::Brightness() const
{
    std::lock_guard l(_mu);
    return _brightness;
}

json Runtime::CurrentPage() const
{
    std::lock_guard l(_mu);
    return PageLocked();
}

ValueShown Runtime::ShownLocked(const FaceValue& v) const
{
    auto it = _readouts.find(v.readout);
    return ShowValue(v, it != _readouts.end() ? it->second : json());
}

json Runtime::PageLocked() const
{
    json page = DescribePage(_s, _nav, _m, _page, OnStates());
    // value faces: what each key shows now ({readout, text, level})
    if (page.contains("keys"))
        for (auto& e : page["keys"])
        {
            const int k = e.value("key", -1);
            const Node* n = k >= 0 && k < static_cast<int>(_page.keys.size()) ? _page.keys[static_cast<size_t>(k)]
                                                                              : nullptr;
            if (!n || k == _page.backKey)
                continue;
            const Face& f = IsOn(*n) && n->faceOn ? *n->faceOn : n->face;
            if (!f.value)
                continue;
            const ValueShown v = ShownLocked(*f.value);
            e["value"] = {{"readout", f.value->readout}, {"text", v.text}, {"level", v.level}};
        }
    return page;
}

json Runtime::Nodes(const json& filter) const
{
    std::lock_guard l(_mu);
    return ListNodes(ToJson(_s), filter);
}

json Runtime::State() const
{
    std::lock_guard l(_mu);
    json stack = json::array();
    for (const Node* n : _nav.Stack())
        stack.push_back({{"id", n->id}, {"title", n->title}});
    return {{"scenario", _s.name},
            {"model", std::string(_m.name)},
            {"stack", stack},
            {"path", _page.path},
            {"page", PageLocked()},
            {"toggles", _toggles},
            {"readouts", _readouts},
            {"context", _context},
            {"surface",
             {{"live", _live}, {"target", _target}, {"notice", _notice}, {"strip", _strip}}},
            {"last_actions", _last},
            {"brightness", _brightness},
            {"pack", PackLocked()},
            {"scenario_pack", _s.settings.pack},
            {"stats",
             {{"key_writes", _stats.keyWrites}, {"strip_writes", _stats.stripWrites}, {"actions", _stats.actions}}}};
}

}  // namespace sdb::scn
