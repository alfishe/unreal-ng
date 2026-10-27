// The device multiplexer (daemon.md §3): the daemon is the only USB owner, software decks join it over the device
// wire v2 as viewers. A real deck is attached once and the runtime drives the transport AttachDeck returns: every
// report also goes to the deck's mirror viewers, their input merges into the runtime's, a late viewer gets the
// current picture replayed. A viewer may also ask for a deck of its own (role "own"): onOwnDeck then gets a
// transport for a runtime of its own.
//
// Threads: the hub lives on the network loop. The transports it hands out are used on the device threads (one per
// logical deck); they post to the loop and never wait for a viewer. Viewers have bounded queues; a slow one loses
// queued images and gets a full replay when it catches up.
#pragma once
#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sdb/eventloop.h"
#include "sdb/models.h"
#include "sdb/transport.h"

namespace sdb
{

class DeckHub
{
public:
    struct Options
    {
        std::string host = "127.0.0.1";
        int port = 37050;                    // 0 = any free port
        std::size_t viewerQueueFrames = 64;  // per viewer, then images are dropped and a replay follows
        int viewerSendBuffer = 256 << 10;    // kernel buffer per viewer: what may sit there unseen by the queue
        std::chrono::milliseconds joinTimeout{2000};
        std::chrono::milliseconds featureTimeout{1000};  // own role: a viewer's GetFeature answer
        std::string daemonVersion = "dev";
    };

    DeckHub(net::EventLoop& loop, Options options);
    ~DeckHub();
    DeckHub(const DeckHub&) = delete;
    DeckHub& operator=(const DeckHub&) = delete;

    // Loop thread (or before the loop runs).
    bool Listen(std::string* error);
    [[nodiscard]] int Port() const
    {
        return _port;
    }
    [[nodiscard]] std::size_t ViewerCount() const;

    // What the hub knows of each logical deck, for the daemon's automation (GET /api/v1/decks). Any thread: a copy
    // the loop refreshes whenever a deck or a viewer comes or goes.
    struct ViewerInfo
    {
        int id = 0;
        std::string role;  // mirror | own
        std::string name;  // the Join's "name" (e.g. "Stream Deck Studio")
        long long pid = 0;
    };
    struct DeckInfo
    {
        std::string serial;
        const Model* model = nullptr;
        bool real = true;
        bool connected = false;
        std::vector<ViewerInfo> viewers;
    };
    struct Snapshot
    {
        std::vector<DeckInfo> decks;  // real decks in attach order, then own decks
        int waiting = 0;              // mirrors waiting for a deck
    };
    [[nodiscard]] Snapshot Info() const;

    // Any thread: a Control frame (JSON, device-wire.md) to every viewer of the deck. Returns how many viewers it
    // goes to (0 = an unknown deck, or nobody watches it).
    int SendControl(const std::string& serial, const std::string& json);

    // Any thread. The returned transport wraps `usb`; destroying it detaches the deck (its mirrors wait for it to
    // come back, with the last picture).
    std::unique_ptr<Transport> AttachDeck(std::unique_ptr<Transport> usb, const Model& model,
                                          const std::string& serial);

    // Loop thread: a viewer joined with role "own". The transport ends (Read returns -1) when the viewer leaves.
    std::function<void(std::unique_ptr<Transport>, const Model&, const std::string& serial)> onOwnDeck;
    // Loop thread: that viewer left; its transport has ended.
    std::function<void(const std::string& serial)> onOwnDeckGone;

    struct Impl;

private:
    std::shared_ptr<Impl> _impl;
    int _port = 0;
};

}  // namespace sdb
