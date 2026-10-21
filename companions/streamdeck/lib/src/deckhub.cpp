#include "sdb/deckhub.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <vector>

#include "sdb/devicewire.h"
#include "sdb/picturecache.h"

namespace sdb
{
namespace
{

using json = nlohmann::json;
using clk = std::chrono::steady_clock;

std::string Bytes(std::span<const std::uint8_t> r)
{
    return std::string(reinterpret_cast<const char*>(r.data()), r.size());
}

// Input from viewers to a runtime (and, for an own deck, the answer to a GetFeature), shared by the loop and the
// device thread.
struct DeckInput
{
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::string> reports;
    std::optional<std::string> featureReply;
    bool closed = false;

    void Push(std::string report)
    {
        {
            std::lock_guard lock(mutex);
            reports.push_back(std::move(report));
        }
        changed.notify_all();
    }
    void Close()
    {
        {
            std::lock_guard lock(mutex);
            closed = true;
        }
        changed.notify_all();
    }
    // A queued report into buf: its length, or 0 when none is queued.
    int Pop(std::span<std::uint8_t> buf)
    {
        std::lock_guard lock(mutex);
        return PopLocked(buf);
    }
    int PopLocked(std::span<std::uint8_t> buf)
    {
        if (reports.empty())
            return 0;
        const std::string r = std::move(reports.front());
        reports.pop_front();
        const std::size_t n = std::min(buf.size(), r.size());
        std::copy_n(reinterpret_cast<const std::uint8_t*>(r.data()), n, buf.begin());
        std::fill(buf.begin() + static_cast<std::ptrdiff_t>(n), buf.end(), 0);
        return static_cast<int>(n);
    }
};

struct OutFrame
{
    std::string bytes;
    bool image = false;  // may be dropped when the viewer falls behind
};

struct Viewer
{
    net::SocketHandle socket = net::kInvalidSocket;
    wire::FrameReader reader;
    std::deque<OutFrame> out;
    std::size_t frontSent = 0;
    bool joined = false;
    bool own = false;
    bool waiting = false;  // a mirror without its deck yet
    std::string wanted;    // the serial a waiting mirror asked for, or "first"
    std::string deck;      // bound deck serial
    std::string name;      // the Join's "name" and "pid": who the viewer is (GET /api/v1/decks)
    long long pid = 0;
    bool needsReplay = false;
    net::EventLoop::TimerId joinTimer = 0;
};

struct Deck
{
    std::string serial;
    const Model* model = nullptr;
    bool real = true;
    bool connected = false;
    std::unique_ptr<PictureCache> cache;
    std::string page;  // the last HostPage
    std::set<int> viewers;
    std::shared_ptr<DeckInput> input;
    int ownViewer = -1;
};

}  // namespace

struct DeckHub::Impl : std::enable_shared_from_this<DeckHub::Impl>
{
    net::EventLoop& loop;
    Options options;
    DeckHub* owner;
    net::SocketHandle listener = net::kInvalidSocket;
    std::map<int, Viewer> viewers;
    std::map<std::string, Deck> decks;
    std::vector<std::string> attachOrder;  // real decks, the first attached first
    int nextViewer = 1;
    int nextOwn = 1;
    mutable std::mutex infoMutex;  // guards `info`: read from any thread, written on the loop
    DeckHub::Snapshot info;

    Impl(net::EventLoop& l, Options o, DeckHub* h) : loop(l), options(std::move(o)), owner(h) {}

    // Any thread: run fn on the loop while the hub lives.
    void Post(std::function<void(Impl&)> fn)
    {
        loop.Post([weak = weak_from_this(), fn = std::move(fn)] {
            if (auto self = weak.lock())
                fn(*self);
        });
    }

    // ------------------------------------------------------------------ viewers
    void Accept()
    {
        for (net::SocketHandle s = net::Accept(listener); s != net::kInvalidSocket; s = net::Accept(listener))
        {
            const int id = nextViewer++;
            Viewer& v = viewers[id];
            v.socket = s;
            net::SetSendBuffer(s, options.viewerSendBuffer);
            loop.Watch(s, true, false, [this, id](bool r, bool w, bool e) { OnIo(id, r, w, e); });
            v.joinTimer = loop.After(options.joinTimeout, [this, id] {
                if (auto it = viewers.find(id); it != viewers.end())
                {
                    it->second.joinTimer = 0;
                    if (!it->second.joined)
                        Close(id);
                }
            });
        }
    }

    void OnIo(int id, bool readable, bool writable, bool error)
    {
        auto it = viewers.find(id);
        if (it == viewers.end())
            return;
        if (writable)
            Flush(id);
        if (!readable && !error)
            return;
        char buf[16384];
        bool gone = false;
        for (;;)
        {
            const long n = net::Recv(it->second.socket, buf, sizeof buf);
            if (n == 0)
                break;
            if (n < 0)
            {
                gone = true;
                break;
            }
            it->second.reader.Feed(std::string_view(buf, static_cast<std::size_t>(n)));
        }
        wire::Frame f;
        while (viewers.count(id) && viewers[id].reader.Next(f))
            HandleFrame(id, f);
        it = viewers.find(id);
        if (it != viewers.end() && (gone || it->second.reader.Broken()))
            Close(id);
    }

    void HandleFrame(int id, wire::Frame& f)
    {
        Viewer& v = viewers[id];
        if (!v.joined && f.type != wire::Join)
        {
            Close(id);
            return;
        }
        switch (f.type)
        {
            case wire::Join:
                HandleJoin(id, f.payload);
                Publish();
                return;
            case wire::Input:
                if (auto d = decks.find(v.deck); d != decks.end() && d->second.input)
                    d->second.input->Push(std::move(f.payload));
                return;
            case wire::FeatureReply:
                if (auto d = decks.find(v.deck); v.own && d != decks.end() && d->second.input)
                {
                    {
                        std::lock_guard lock(d->second.input->mutex);
                        d->second.input->featureReply = std::move(f.payload);
                    }
                    d->second.input->changed.notify_all();
                }
                return;
            case wire::Leave:
                Close(id);
                return;
            default:
                return;  // unknown types are ignored (a newer viewer)
        }
    }

    void SendJson(int id, std::uint8_t type, const json& j)
    {
        Enqueue(id, wire::EncodeFrame(type, j.dump()), false, true);
    }

    void HandleJoin(int id, const std::string& payload)
    {
        try
        {
            Join(id, payload);
        }
        catch (const json::exception&)
        {
            // A field of the wrong type
            SendJson(id, wire::Welcome, {{"v", wire::kVersion}, {"error", "bad_request"}});
            CloseAfterFlush(id);
        }
    }

    void Join(int id, const std::string& payload)
    {
        Viewer& v = viewers[id];
        json j = json::parse(payload, nullptr, false);
        if (j.is_discarded() || !j.is_object())
        {
            SendJson(id, wire::Welcome, {{"v", wire::kVersion}, {"error", "bad_request"}});
            CloseAfterFlush(id);
            return;
        }
        if (!j.contains("v") || !j["v"].is_number_integer() || j["v"].get<int>() != wire::kVersion)
        {
            SendJson(id, wire::Welcome, {{"v", wire::kVersion}, {"error", "version"}});
            CloseAfterFlush(id);
            return;
        }
        v.joined = true;
        if (const auto n = j.find("name"); n != j.end() && n->is_string())
            v.name = n->get<std::string>();
        if (const auto p = j.find("pid"); p != j.end() && p->is_number_integer())
            v.pid = p->get<long long>();
        if (v.joinTimer)
        {
            loop.Cancel(v.joinTimer);
            v.joinTimer = 0;
        }
        const std::string role = j.value("role", "");
        if (role == "mirror")
        {
            v.wanted = j.value("deck", "first");
            if (const Deck* d = FindRealDeck(v.wanted))
                Bind(id, d->serial);
            else
            {
                v.waiting = true;
                SendJson(id, wire::Welcome, {{"v", wire::kVersion}, {"error", "no_deck"}, {"waiting", true}});
            }
            return;
        }
        if (role == "own")
        {
            const Model* model = FindModelByName(j.value("model", "plusxl"));
            if (!model)
            {
                SendJson(id, wire::Welcome, {{"v", wire::kVersion}, {"error", "bad_model"}});
                CloseAfterFlush(id);
                return;
            }
            std::string serial = j.value("serial", "");
            if (serial.empty() || decks.count(serial))
                serial = "EMU" + std::to_string(nextOwn++);
            Deck& d = decks[serial];
            d.serial = serial;
            d.model = model;
            d.real = false;
            d.connected = true;
            d.cache = std::make_unique<PictureCache>(*model);
            d.input = std::make_shared<DeckInput>();
            d.ownViewer = id;
            v.own = true;
            v.deck = serial;
            d.viewers.insert(id);
            // the runtime first: once the viewer has its Welcome, the daemon's routes know its deck (its frames are
            // posted to this loop, so they still follow the Welcome)
            if (owner->onOwnDeck)
                owner->onOwnDeck(MakeOwnTransport(id, serial, d.input), *model, serial);
            SendWelcome(id, decks[serial]);
            return;
        }
        SendJson(id, wire::Welcome, {{"v", wire::kVersion}, {"error", "bad_role"}});
        CloseAfterFlush(id);
    }

    const Deck* FindRealDeck(const std::string& wanted) const
    {
        for (const std::string& serial : attachOrder)
        {
            const auto it = decks.find(serial);
            if (it != decks.end() && it->second.connected && (wanted == "first" || wanted == serial))
                return &it->second;
        }
        return nullptr;
    }

    void SendWelcome(int id, const Deck& d)
    {
        SendJson(id, wire::Welcome,
                 {{"v", wire::kVersion},
                  {"deck", d.serial},
                  {"model", std::string(d.model->name)},
                  {"pid", d.model->pid},
                  {"real", d.real},
                  {"daemon", options.daemonVersion}});
    }

    void Bind(int id, const std::string& serial)
    {
        Viewer& v = viewers[id];
        Deck& d = decks[serial];
        v.waiting = false;
        v.deck = serial;
        d.viewers.insert(id);
        SendWelcome(id, d);
        SendReplay(id);
    }

    void SendReplay(int id)
    {
        Viewer& v = viewers[id];
        const auto d = decks.find(v.deck);
        if (d == decks.end())
            return;
        Enqueue(id, wire::EncodeFrame(wire::ReplayBegin, {}), false, true);
        for (const auto& e : d->second.cache->Replay())
            Enqueue(id, wire::EncodeFrame(e.wireType, e.payload), false, true);
        if (!d->second.page.empty())
            Enqueue(id, wire::EncodeFrame(wire::HostPage, d->second.page), false, true);
        Enqueue(id, wire::EncodeFrame(wire::ReplayEnd, {}), false, true);
    }

    // force: past the queue limit (replies, replays). Images over the limit drop the queued images instead.
    void Enqueue(int id, std::string bytes, bool image, bool force = false)
    {
        auto it = viewers.find(id);
        if (it == viewers.end())
            return;
        Viewer& v = it->second;
        if (!force && v.out.size() >= options.viewerQueueFrames)
        {
            // Drop queued images not yet started; keep features and notes; redraw everything once it drains.
            const std::size_t keepFront = v.frontSent > 0 ? 1 : 0;
            std::deque<OutFrame> kept;
            for (std::size_t i = 0; i < v.out.size(); ++i)
                if (i < keepFront || !v.out[i].image)
                    kept.push_back(std::move(v.out[i]));
            v.out.swap(kept);
            v.needsReplay = true;
            if (image)
                return;
        }
        if (v.needsReplay && image)
            return;  // the replay will carry it
        v.out.push_back(OutFrame{std::move(bytes), image});
        Flush(id);
    }

    void Flush(int id)
    {
        auto it = viewers.find(id);
        if (it == viewers.end())
            return;
        Viewer& v = it->second;
        while (!v.out.empty())
        {
            const std::string& b = v.out.front().bytes;
            const long n = net::Send(v.socket, b.data() + v.frontSent, b.size() - v.frontSent);
            if (n < 0)
            {
                Close(id);
                return;
            }
            if (n == 0)
                break;
            v.frontSent += static_cast<std::size_t>(n);
            if (v.frontSent == b.size())
            {
                v.out.pop_front();
                v.frontSent = 0;
            }
        }
        if (v.out.empty() && v.needsReplay)
        {
            v.needsReplay = false;
            SendReplay(id);
            return;
        }
        loop.Modify(v.socket, true, !v.out.empty());
    }

    void CloseAfterFlush(int id)
    {
        // The answer is small and the socket buffer empty: one flush sends it.
        Flush(id);
        Close(id);
    }

    void Close(int id)
    {
        auto it = viewers.find(id);
        if (it == viewers.end())
            return;
        Viewer& v = it->second;
        if (v.joinTimer)
            loop.Cancel(v.joinTimer);
        loop.Unwatch(v.socket);
        net::CloseSocket(v.socket);
        if (auto d = decks.find(v.deck); d != decks.end())
        {
            d->second.viewers.erase(id);
            if (d->second.ownViewer == id)
            {
                d->second.input->Close();
                const std::string serial = d->second.serial;
                decks.erase(d);
                if (owner->onOwnDeckGone)
                    owner->onOwnDeckGone(serial);
            }
        }
        viewers.erase(it);
        Publish();
    }

    // The copy Info() hands out: decks in attach order (own decks after the real ones), their viewers.
    void Publish()
    {
        DeckHub::Snapshot s;
        auto add = [&](const Deck& d) {
            DeckHub::DeckInfo di{d.serial, d.model, d.real, d.connected, {}};
            for (int id : d.viewers)
                if (const auto v = viewers.find(id); v != viewers.end())
                    di.viewers.push_back({id, v->second.own ? "own" : "mirror", v->second.name, v->second.pid});
            s.decks.push_back(std::move(di));
        };
        for (const std::string& serial : attachOrder)
            if (const auto d = decks.find(serial); d != decks.end())
                add(d->second);
        for (const auto& [serial, d] : decks)
            if (!d.real)
                add(d);
        for (const auto& [id, v] : viewers)
            s.waiting += v.waiting ? 1 : 0;
        std::lock_guard lock(infoMutex);
        info = std::move(s);
    }

    void SendControl(const std::string& serial, const std::string& payload)
    {
        const auto d = decks.find(serial);
        if (d == decks.end())
            return;
        const std::string frame = wire::EncodeFrame(wire::Control, payload);
        for (int id : d->second.viewers)
            Enqueue(id, frame, false, true);
    }

    // ------------------------------------------------------------------ decks (posted from device threads)
    void DeckAttached(const std::string& serial, const Model* model, std::shared_ptr<DeckInput> input)
    {
        Deck& d = decks[serial];
        const bool replug = d.model != nullptr;
        d.serial = serial;
        d.model = model;
        d.real = true;
        d.connected = true;
        d.input = std::move(input);
        if (!d.cache || !replug)
            d.cache = std::make_unique<PictureCache>(*model);
        if (std::find(attachOrder.begin(), attachOrder.end(), serial) == attachOrder.end())
            attachOrder.push_back(serial);
        for (int id : d.viewers)
            SendJson(id, wire::DeckState, {{"connected", true}});
        std::vector<int> waiting;
        for (const auto& [id, v] : viewers)
        {
            if (v.waiting && (v.wanted == "first" || v.wanted == serial))
                waiting.push_back(id);
            else if (!v.own && !v.waiting && v.wanted == "first" && v.deck != serial)
                if (const auto old = decks.find(v.deck); old != decks.end() && old->second.real && !old->second.connected)
                {
                    old->second.viewers.erase(id);  // a mirror of "the first deck" follows the deck the daemon took
                    waiting.push_back(id);
                }
        }
        for (int id : waiting)
            Bind(id, serial);  // a new Welcome (model, serial) and the picture
        // an unplugged deck no runtime plays any more (the daemon took this one instead) and nobody watches is forgotten
        for (auto it = decks.begin(); it != decks.end();)
        {
            const Deck& old = it->second;
            if (old.real && old.serial != serial && !old.connected && !old.input && old.viewers.empty())
            {
                std::erase(attachOrder, old.serial);
                it = decks.erase(it);
            }
            else
                ++it;
        }
        Publish();
    }

    // The runtime let the deck go (its transport is gone): unplugged for its viewers, no more input to it.
    void DeckReleased(const std::string& serial)
    {
        DeckDetached(serial);
        if (auto it = decks.find(serial); it != decks.end())
            it->second.input.reset();
    }

    void DeckDetached(const std::string& serial)
    {
        auto it = decks.find(serial);
        if (it == decks.end() || !it->second.connected)
            return;
        it->second.connected = false;
        for (int id : it->second.viewers)
            SendJson(id, wire::DeckState, {{"connected", false}});
        Publish();
    }

    void DeckFrame(const std::string& serial, std::uint8_t type, std::string bytes)
    {
        auto it = decks.find(serial);
        if (it == decks.end())
            return;
        Deck& d = it->second;
        const auto* r = reinterpret_cast<const std::uint8_t*>(bytes.data());
        if (type == wire::Output)
            d.cache->Output(std::span(r, bytes.size()));
        else if (type == wire::SetFeature)
            d.cache->Feature(std::span(r, bytes.size()));
        else if (type == wire::HostPage)
            d.page = bytes;
        const std::string frame = wire::EncodeFrame(type, bytes);
        for (int id : d.viewers)
            Enqueue(id, frame, type == wire::Output);
    }

    std::unique_ptr<Transport> MakeOwnTransport(int viewer, const std::string& serial,
                                                std::shared_ptr<DeckInput> input);
};

namespace
{

// A real deck: the USB transport, with every report copied to the mirrors and their input merged in.
class MirrorTransport final : public Transport
{
public:
    MirrorTransport(std::unique_ptr<Transport> usb, std::weak_ptr<DeckHub::Impl> hub, const Model& model,
                    std::string serial, std::shared_ptr<DeckInput> input)
        : _usb(std::move(usb)),
          _hub(std::move(hub)),
          _model(&model),
          _serial(std::move(serial)),
          _input(std::move(input)),
          _connected(_usb->Connected())
    {
    }
    ~MirrorTransport() override
    {
        Post([s = _serial](DeckHub::Impl& h) { h.DeckReleased(s); });
    }

    int Write(std::span<const std::uint8_t> r) override
    {
        const int n = _usb->Write(r);
        Publish(wire::Output, r);
        Follow();
        return n;
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        const int n = _usb->SendFeature(r);
        Publish(wire::SetFeature, r);
        Follow();
        return n;
    }
    int GetFeature(std::span<std::uint8_t> buf) override
    {
        return _usb->GetFeature(buf);  // the real deck answers
    }
    int Read(std::span<std::uint8_t> buf, int timeoutMs) override
    {
        const auto deadline = clk::now() + std::chrono::milliseconds(std::max(0, timeoutMs));
        for (;;)
        {
            if (const int n = _input->Pop(buf); n > 0)
            {
                _last = InputSource::Emulator;
                return n;
            }  // a viewer's input never waits
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clk::now()).count();
            const int slice = static_cast<int>(std::clamp<long long>(left, 0, 5));
            const int n = _usb->Read(buf, slice);
            Follow();
            if (n != 0)
            {
                if (n > 0)
                {
                    _last = _usb->LastSource();
                    Publish(wire::HostInput, buf.first(static_cast<std::size_t>(n)));
                }
                return n;
            }
            if (left <= 0)
                return 0;
        }
    }
    std::string LastError() const override
    {
        return _usb->LastError();
    }
    [[nodiscard]] InputSource LastSource() const override
    {
        return _last;
    }
    void Note(std::string_view text) override
    {
        Publish(wire::HostNote, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }
    void PublishPage(std::string_view j) override
    {
        Publish(wire::HostPage, std::span(reinterpret_cast<const std::uint8_t*>(j.data()), j.size()));
    }
    [[nodiscard]] bool Connected() const override
    {
        return _usb->Connected();
    }
    bool Reconnect(std::chrono::milliseconds away) override
    {
        return _usb->Reconnect(away);
    }

private:
    void Post(std::function<void(DeckHub::Impl&)> fn)
    {
        if (auto h = _hub.lock())
            h->Post(std::move(fn));
    }
    void Publish(std::uint8_t type, std::span<const std::uint8_t> r)
    {
        Post([s = _serial, type, b = Bytes(r)](DeckHub::Impl& h) mutable { h.DeckFrame(s, type, std::move(b)); });
    }
    // The deck underneath went or came back (sdb/reconnect.h): its viewers are told (DeckState), it stays attached.
    void Follow()
    {
        const bool c = _usb->Connected();
        if (c == _connected)
            return;
        _connected = c;
        if (c)
            Post([s = _serial, m = _model, in = _input](DeckHub::Impl& h) { h.DeckAttached(s, m, in); });
        else
            Post([s = _serial](DeckHub::Impl& h) { h.DeckDetached(s); });
    }

    std::unique_ptr<Transport> _usb;
    std::weak_ptr<DeckHub::Impl> _hub;
    const Model* _model;
    std::string _serial;
    std::shared_ptr<DeckInput> _input;
    InputSource _last = InputSource::Usb;
    bool _connected;  // as last told to the hub
};

// A viewer's own deck: the viewer is the device.
class OwnTransport final : public Transport
{
public:
    OwnTransport(std::weak_ptr<DeckHub::Impl> hub, int viewer, std::string serial, std::shared_ptr<DeckInput> input,
                 std::chrono::milliseconds featureTimeout)
        : _hub(std::move(hub)),
          _viewer(viewer),
          _serial(std::move(serial)),
          _input(std::move(input)),
          _featureTimeout(featureTimeout)
    {
    }
    ~OwnTransport() override
    {
        Post([v = _viewer](DeckHub::Impl& h) { h.Close(v); });
    }

    int Write(std::span<const std::uint8_t> r) override
    {
        return Send(wire::Output, r) ? static_cast<int>(r.size()) : -1;
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        return Send(wire::SetFeature, r) ? static_cast<int>(r.size()) : -1;
    }
    int GetFeature(std::span<std::uint8_t> buf) override
    {
        {
            std::lock_guard lock(_input->mutex);
            if (_input->closed)
                return -1;
            _input->featureReply.reset();
        }
        Send(wire::GetFeature, buf);
        std::unique_lock lock(_input->mutex);
        if (!_input->changed.wait_for(lock, _featureTimeout,
                                      [&] { return _input->closed || _input->featureReply.has_value(); }) ||
            !_input->featureReply)
            return -1;
        const std::string& r = *_input->featureReply;
        const std::size_t n = std::min(buf.size(), r.size());
        std::copy_n(reinterpret_cast<const std::uint8_t*>(r.data()), n, buf.begin());
        return static_cast<int>(n);
    }
    int Read(std::span<std::uint8_t> buf, int timeoutMs) override
    {
        std::unique_lock lock(_input->mutex);
        _input->changed.wait_for(lock, std::chrono::milliseconds(std::max(0, timeoutMs)),
                                 [&] { return _input->closed || !_input->reports.empty(); });
        if (const int n = _input->PopLocked(buf); n > 0)
            return n;
        return _input->closed ? -1 : 0;
    }
    std::string LastError() const override
    {
        return "the viewer left";
    }
    [[nodiscard]] InputSource LastSource() const override
    {
        return InputSource::Emulator;
    }
    void Note(std::string_view text) override
    {
        Send(wire::HostNote, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }
    void PublishPage(std::string_view j) override
    {
        Send(wire::HostPage, std::span(reinterpret_cast<const std::uint8_t*>(j.data()), j.size()));
    }

private:
    void Post(std::function<void(DeckHub::Impl&)> fn)
    {
        if (auto h = _hub.lock())
            h->Post(std::move(fn));
    }
    bool Send(std::uint8_t type, std::span<const std::uint8_t> r)
    {
        {
            std::lock_guard lock(_input->mutex);
            if (_input->closed)
                return false;
        }
        Post([s = _serial, type, b = Bytes(r)](DeckHub::Impl& h) mutable { h.DeckFrame(s, type, std::move(b)); });
        return true;
    }

    std::weak_ptr<DeckHub::Impl> _hub;
    int _viewer;
    std::string _serial;
    std::shared_ptr<DeckInput> _input;
    std::chrono::milliseconds _featureTimeout;
};

}  // namespace

std::unique_ptr<Transport> DeckHub::Impl::MakeOwnTransport(int viewer, const std::string& serial,
                                                           std::shared_ptr<DeckInput> input)
{
    return std::make_unique<OwnTransport>(weak_from_this(), viewer, serial, std::move(input), options.featureTimeout);
}

DeckHub::DeckHub(net::EventLoop& loop, Options options) : _impl(std::make_shared<Impl>(loop, std::move(options), this))
{
}

DeckHub::~DeckHub()
{
    std::vector<int> ids;
    for (const auto& [id, v] : _impl->viewers)
        ids.push_back(id);
    for (int id : ids)
        _impl->Close(id);
    if (_impl->listener != net::kInvalidSocket)
    {
        _impl->loop.Unwatch(_impl->listener);
        net::CloseSocket(_impl->listener);
    }
}

bool DeckHub::Listen(std::string* error)
{
    _impl->listener = net::Listen(_impl->options.host, _impl->options.port, &_port, error);
    if (_impl->listener == net::kInvalidSocket)
        return false;
    _impl->loop.Watch(_impl->listener, true, false, [impl = _impl.get()](bool, bool, bool) { impl->Accept(); });
    return true;
}

std::size_t DeckHub::ViewerCount() const
{
    return _impl->viewers.size();
}

DeckHub::Snapshot DeckHub::Info() const
{
    std::lock_guard lock(_impl->infoMutex);
    return _impl->info;
}

int DeckHub::SendControl(const std::string& serial, const std::string& json)
{
    int viewers = 0;
    {
        std::lock_guard lock(_impl->infoMutex);
        for (const auto& d : _impl->info.decks)
            if (d.serial == serial)
                viewers = static_cast<int>(d.viewers.size());
    }
    if (viewers > 0)
        _impl->Post([serial, json](Impl& h) { h.SendControl(serial, json); });
    return viewers;
}

std::unique_ptr<Transport> DeckHub::AttachDeck(std::unique_ptr<Transport> usb, const Model& model,
                                               const std::string& serial)
{
    auto input = std::make_shared<DeckInput>();
    const bool connected = usb->Connected();
    _impl->Post([serial, model = &model, input, connected](Impl& h) {
        h.DeckAttached(serial, model, input);
        if (!connected)
            h.DeckDetached(serial);
    });
    return std::make_unique<MirrorTransport>(std::move(usb), _impl, model, serial, std::move(input));
}

}  // namespace sdb
