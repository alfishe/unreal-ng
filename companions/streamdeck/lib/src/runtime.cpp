#include "sdb/runtime.h"

#include <algorithm>
#include <cstdio>

#include "sdb/format.h"
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
    for (const auto* s : {&f.icon, &f.svg, &f.image, &f.fit})
        h = Mix(h, HashStr(*s));
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
    if (!_opt.syncActions && !_monitor.joinable())
        _monitor = std::thread([this] {
            // time travel on the strip (strip.status "ttd"): the recording span while it records, the position after
            auto next = std::chrono::steady_clock::now();
            while (!_workerStop)
            {
                // 50 readouts a second on a fixed beat: the poll's own time does not stretch the period
                next += std::chrono::milliseconds(20);
                const auto now = std::chrono::steady_clock::now();
                if (next < now)
                    next = now;
                std::this_thread::sleep_until(next);
                double fps = 50;
                {
                    std::lock_guard l(_mu);
                    const bool wanted = _page.strip && _page.strip->status == "ttd";
                    if (!wanted)
                        continue;
                    _monitorKick = false;
                    fps = _page.strip->fps;
                }
                const json t = _host->TtdState();
                std::string text = TtdReadout(t, fps);
                std::lock_guard l(_mu);
                if (_page.strip && _page.strip->status == "ttd")
                    _status = std::move(text);
            }
        });
    NoteMenu();
    while (!stop)
        Step(10);
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
            force = true;
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
    Render(force);
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
        for (int k = 0; k < _m.keys; ++k)
        {
            const bool pressed = _pressed[static_cast<size_t>(k)];
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
                const bool on = _toggles.count(n->id) && _toggles.at(n->id);
                const Face& f = on && n->faceOn ? *n->faceOn : n->face;
                h = Mix(Mix(Mix(HashFace(f), HashStr(n->id + n->title)), pressed), n->IsMenu());
                if (force || h != _sentKey[static_cast<size_t>(k)])
                    img = RenderFace(f, n->title, pressed, n->IsMenu(), _safePad, _faceSize);
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
            double left = -1;  // auto exit countdown: redrawn at 30 fps so the bar glides
            if (_page.autoExit > 0 && _nav.Depth() > 0)
            {
                left = std::max(0.0, _page.autoExit - (NowMs() - _lastActivity) / 1000.0);
                h = Mix(h, static_cast<std::size_t>(left * 30) + 7);
            }
            if (force || h != _sentStrip)
            {
                strip = RenderStrip(_page, _m.lcdLogicalW, _m.lcdLogicalH, _m.dialTileW,
                                    bannerOn ? _banner : std::string(), left, _status);
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
        pageJson = DescribePage(_s, _nav, _m, _page, _toggles).dump();
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
            _pressed[static_cast<size_t>(e.index)] = true;
            _downAt[e.index] = now;
            if (e.index != _page.backKey)
                Activate(e.index);  // the back key acts on release (hold = home)
            break;
        case EventType::KeyUp: {
            if (e.index < 0 || e.index >= _m.keys)
                return;
            _pressed[static_cast<size_t>(e.index)] = false;
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
                    Enqueue({*d->turn, sdb::Format("dial {}", e.index), {{"ticks", e.delta}, {"rate", rate}}, "stay"});
                }
            break;
        case EventType::DialDown:
            if (e.index >= 0 && e.index < static_cast<int>(_page.dials.size()))
                if (const Dial* d = _page.dials[static_cast<size_t>(e.index)]; d && d->press)
                    Enqueue({*d->press, sdb::Format("dial {} press", e.index), json::object(), "stay"});
            break;
        case EventType::LcdTap:
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

void Runtime::Activate(int key)
{
    const Node* n = _page.keys[static_cast<size_t>(key)];
    if (!n)
        return;
    if (n->IsMenu())
    {
        _nav.Enter(*n);
        _page = Layout(_s, _nav, _m);
        NoteMenu();
        return;
    }
    Job j{n->action, n->id, json::object(), n->then, n->action.kind == "toggle"};
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
    if (j.toggle)
        withLock([&] { state = _toggles[j.node]; });
    ActionResult r;
    if (_opt.dryRun)
        r = {true, "dry run: " + j.action.kind + " " + j.action.spec.dump()};
    else
    {
        _cancel = false;
        try
        {
            r = Execute(j.action, *_host, j.vars, j.toggle ? &state : nullptr);
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
        _last.push_back({{"node", j.node}, {"ok", r.ok}, {"message", r.message}});
        if (_last.size() > 20)
            _last.erase(_last.begin());
        if (!r.ok)
            SetBanner(j.node + " FAILED", 2000);
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
    if (moved)
    {
        _page = Layout(_s, _nav, _m);
        NoteMenu();
        _lastActivity = NowMs();
    }
}

void Runtime::SetBanner(const std::string& text, int ms)
{
    _banner = text;
    _bannerUntil = NowMs() + ms;
}

void Runtime::NoteMenu()
{
    _status.clear();  // a readout belongs to the menu that showed it
    _dev.Note("menu: " + JoinPath(_page.path));
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
    return DescribePage(_s, _nav, _m, _page, _toggles);
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
            {"page", DescribePage(_s, _nav, _m, _page, _toggles)},
            {"toggles", _toggles},
            {"last_actions", _last},
            {"brightness", _brightness},
            {"stats",
             {{"key_writes", _stats.keyWrites}, {"strip_writes", _stats.stripWrites}, {"actions", _stats.actions}}}};
}

}  // namespace sdb::scn
