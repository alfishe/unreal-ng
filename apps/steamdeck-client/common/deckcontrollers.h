#pragma once

// The Deck client's host controllers: what unreal-qt's HostControllers does, without Qt. The native backends (on the
// Deck: hidraw for the built-in controller, evdev for the rest) through HostInputService, the play profiles compiled
// into the controllers library, the user's (~/.config/unreal-ng/controllers/profiles) and the shipped game profiles
// (controllers/profiles/games next to the binary, chosen by the loaded file's SHA-256), the machine default player
// connectors, the port-read auto decision, the haptics (a game profile's, else the sound's) behind the gate, and the
// host verbs of the automation surfaces (WebAPI /controllers/...). Everything runs on the thread that calls Poll():
// the surfaces' requests wait for it.

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Emulator;

namespace controllers
{
class HostInputService;
class IHapticFeed;
}

namespace deckpoc
{

class DeckControllers
{
public:
    struct Options
    {
        /// No game profile with haptics: the motors follow the sound (AY, beeper). Off by default, as in unreal-qt
        /// (Controllers/SoundHaptics): a tune's bass line keeps the motors running; the haptics come from the game
        /// profiles' events (hits, explosions, a life lost)
        bool soundHaptics = false;
    };

    DeckControllers(Emulator& emulator, Options options);
    ~DeckControllers();
    DeckControllers(const DeckControllers&) = delete;
    DeckControllers& operator=(const DeckControllers&) = delete;

    /// Once per rendered frame: the requests of the surfaces, the gate, the auto decision, a re-apply when a device
    /// or the machine changed. @p focused: the window has the focus (Game Mode: always)
    void Poll(bool focused);

    /// Router actions (`osd.keyboard`, ...): the client acts on the ones it has
    void SetActionSink(std::function<void(const std::string& action, bool pressed)> sink);

    controllers::HostInputService& Service();
    /// One line per connected device and its player connector, for the log
    std::vector<std::string> Describe() const;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace deckpoc
