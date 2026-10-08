// P-18 logind sleep: does a game in SteamOS Game Mode get PrepareForSleep(true) through a "sleep" delay
// inhibitor before the device sleeps, how much time does it get, and does audio come back without a click?
//
// - takes Inhibit("sleep", "p18", "save state", "delay") at start and after every wake
// - on PrepareForSleep(true): logs the time, pauses the audio stream, simulates a state write
//   (--save-ms, default 50), closes the inhibitor fd
// - on PrepareForSleep(false): logs, re-takes the inhibitor, clears + resumes the audio stream
// - a 10 ms heartbeat thread logs gaps > 50 ms (with CLOCK_MONOTONIC vs CLOCK_BOOTTIME) to show whether
//   the process was frozen before or after the signal, and for how long the device slept
// - a 440 Hz tone (--tone) to hear clicks at sleep and wake
//
// Log: ~/steamdeck-poc-logs/p18-*.csv. Quit: View + Menu, Esc.

#include "timinglog.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <dbus/dbus.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{

uint64_t ClockNs(clockid_t id)
{
    timespec ts{};
    clock_gettime(id, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull + static_cast<uint64_t>(ts.tv_nsec);
}

struct State
{
    deckpoc::CsvLog log;
    std::mutex logMutex;
    std::vector<std::string> lines;   // on-screen history
    int inhibitFd = -1;
    SDL_AudioStream* tone = nullptr;
    double phase = 0;
    int saveMs = 50;
    std::atomic<bool> quit{false};

    void Event(const std::string& what)
    {
        std::lock_guard<std::mutex> lock(logMutex);
        const uint64_t mono = ClockNs(CLOCK_MONOTONIC);
        const uint64_t boot = ClockNs(CLOCK_BOOTTIME);
        log.Row(mono, "%llu,%s", static_cast<unsigned long long>(boot), what.c_str());
        log.Flush();
        char line[256];
        std::snprintf(line, sizeof line, "%10.3f  %s", static_cast<double>(mono) / 1e9, what.c_str());
        lines.emplace_back(line);
        if (lines.size() > 30)
            lines.erase(lines.begin());
        SDL_Log("%s", line);
    }
};

bool TakeInhibitor(DBusConnection* bus, State& s)
{
    DBusMessage* msg = dbus_message_new_method_call("org.freedesktop.login1", "/org/freedesktop/login1",
                                                    "org.freedesktop.login1.Manager", "Inhibit");
    const char* what = "sleep";
    const char* who = "p18-logind-sleep";
    const char* why = "save emulator state";
    const char* mode = "delay";
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &what, DBUS_TYPE_STRING, &who, DBUS_TYPE_STRING, &why,
                             DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, msg, 2000, &err);
    dbus_message_unref(msg);
    if (!reply)
    {
        s.Event(std::string("inhibit FAILED: ") + (err.message ? err.message : "?"));
        dbus_error_free(&err);
        return false;
    }
    int fd = -1;
    dbus_message_get_args(reply, &err, DBUS_TYPE_UNIX_FD, &fd, DBUS_TYPE_INVALID);
    dbus_message_unref(reply);
    s.inhibitFd = fd;
    s.Event("inhibitor taken, fd=" + std::to_string(fd));
    return fd >= 0;
}

void ReleaseInhibitor(State& s)
{
    if (s.inhibitFd >= 0)
    {
        close(s.inhibitFd);
        s.inhibitFd = -1;
        s.Event("inhibitor released");
    }
}

void SDLCALL ToneCallback(void* user, SDL_AudioStream* stream, int additional, int /*total*/)
{
    auto* s = static_cast<State*>(user);
    std::vector<float> buf(static_cast<size_t>(additional) / sizeof(float));
    for (size_t i = 0; i < buf.size(); i += 2)
    {
        const float v = 0.15f * static_cast<float>(std::sin(s->phase));
        buf[i] = buf[i + 1] = v;
        s->phase += 2 * M_PI * 440.0 / 48000.0;
    }
    SDL_PutAudioStreamData(stream, buf.data(), additional);
}

void OnSleepSignal(State& s, bool start, DBusConnection* bus)
{
    if (start)
    {
        s.Event("PrepareForSleep(true)");
        if (s.tone)
        {
            SDL_PauseAudioStreamDevice(s.tone);
            SDL_ClearAudioStream(s.tone);
            s.Event("audio paused");
        }
        const uint64_t t0 = deckpoc::NowNs();
        std::this_thread::sleep_for(std::chrono::milliseconds(s.saveMs));   // the state write stand-in
        s.Event("state saved (simulated " + std::to_string((deckpoc::NowNs() - t0) / 1000000) + " ms)");
        ReleaseInhibitor(s);
    }
    else
    {
        s.Event("PrepareForSleep(false) - woke");
        TakeInhibitor(bus, s);
        if (s.tone)
        {
            SDL_ClearAudioStream(s.tone);
            SDL_ResumeAudioStreamDevice(s.tone);
            s.Event("audio resumed");
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    State s;
    bool tone = false;
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--tone") == 0)
            tone = true;
        else if (std::strcmp(argv[i], "--save-ms") == 0 && i + 1 < argc)
            s.saveMs = std::atoi(argv[++i]);
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
        return 1;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_CreateWindowAndRenderer("P-18 logind sleep", 1280, 800, SDL_WINDOW_FULLSCREEN, &window, &renderer);
    SDL_SetRenderVSync(renderer, 1);

    s.log.Open(deckpoc::LogPath("p18-events", "csv"), "boottime_ns,event");
    s.Event("start, save-ms=" + std::to_string(s.saveMs));

    if (tone)
    {
        SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
        s.tone = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, ToneCallback, &s);
        if (s.tone)
            SDL_ResumeAudioStreamDevice(s.tone);
    }

    DBusError err;
    dbus_error_init(&err);
    DBusConnection* bus = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (!bus)
    {
        s.Event(std::string("system bus FAILED: ") + (err.message ? err.message : "?"));
        dbus_error_free(&err);
    }
    else
    {
        dbus_bus_add_match(bus, "type='signal',interface='org.freedesktop.login1.Manager',member='PrepareForSleep'", &err);
        dbus_connection_flush(bus);
        TakeInhibitor(bus, s);
    }

    // Heartbeat: a gap means the process did not run (frozen or asleep)
    std::thread heartbeat([&s]
    {
        uint64_t lastMono = ClockNs(CLOCK_MONOTONIC);
        uint64_t lastBoot = ClockNs(CLOCK_BOOTTIME);
        while (!s.quit)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            const uint64_t mono = ClockNs(CLOCK_MONOTONIC);
            const uint64_t boot = ClockNs(CLOCK_BOOTTIME);
            if (mono - lastMono > 50'000'000ull || boot - lastBoot > 50'000'000ull)
            {
                char what[160];
                std::snprintf(what, sizeof what, "heartbeat gap: monotonic %.3f s, boottime %.3f s (slept %.3f s)",
                              (mono - lastMono) / 1e9, (boot - lastBoot) / 1e9,
                              ((boot - lastBoot) - (mono - lastMono)) / 1e9);
                s.Event(what);
            }
            lastMono = mono;
            lastBoot = boot;
        }
    });

    SDL_Gamepad* pad = nullptr;
    while (!s.quit)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
                s.quit = true;
            if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad)
                pad = SDL_OpenGamepad(e.gdevice.which);
            if (e.type == SDL_EVENT_WILL_ENTER_BACKGROUND || e.type == SDL_EVENT_DID_ENTER_FOREGROUND ||
                e.type == SDL_EVENT_WINDOW_FOCUS_LOST || e.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
                e.type == SDL_EVENT_WINDOW_HIDDEN || e.type == SDL_EVENT_WINDOW_SHOWN || e.type == SDL_EVENT_WINDOW_OCCLUDED)
                s.Event("SDL event 0x" + std::to_string(e.type));
        }
        if (pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK) && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START))
            s.quit = true;

        if (bus)
        {
            dbus_connection_read_write(bus, 0);
            while (DBusMessage* msg = dbus_connection_pop_message(bus))
            {
                if (dbus_message_is_signal(msg, "org.freedesktop.login1.Manager", "PrepareForSleep"))
                {
                    dbus_bool_t start = false;
                    dbus_message_get_args(msg, nullptr, DBUS_TYPE_BOOLEAN, &start, DBUS_TYPE_INVALID);
                    OnSleepSignal(s, start, bus);
                }
                dbus_message_unref(msg);
            }
        }

        SDL_SetRenderDrawColor(renderer, 16, 16, 20, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderScale(renderer, 2.0f, 2.0f);
        SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
        SDL_RenderDebugText(renderer, 4, 4, "P-18 logind sleep: press Power, wait, wake. View+Menu = quit");
        SDL_RenderDebugText(renderer, 4, 14, ("log: " + s.log.Path()).c_str());
        {
            std::lock_guard<std::mutex> lock(s.logMutex);
            float y = 34;
            for (const auto& l : s.lines)
            {
                SDL_RenderDebugText(renderer, 4, y, l.c_str());
                y += 10;
            }
        }
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        SDL_RenderPresent(renderer);
    }

    s.quit = true;
    heartbeat.join();
    ReleaseInhibitor(s);
    if (s.tone)
        SDL_DestroyAudioStream(s.tone);
    if (pad)
        SDL_CloseGamepad(pad);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
