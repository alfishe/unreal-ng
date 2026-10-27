// P-08 input inspector: what does an SDL3 app see of the Deck controller, and how fast?
//
// Shows every joystick / gamepad SDL reports (type, VID / PID, touchpads, sensors, rumble), the live state
// of buttons, axes, both trackpads and the gyro, and per-second event rates. Every input event goes to a CSV
// in ~/steamdeck-poc-logs/ with the event's own timestamp and the time it was handled; a summary is written
// on exit. Run it as a non-Steam shortcut with Steam Input on, then off, then as a Flatpak (see README.md).
//
// Controls: Y = rumble high band (right pad), X = rumble low band (left pad), B = both triggers rumble,
// hold View + Menu for 1 s (or Esc) = quit.

#include "timinglog.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{

struct Pad
{
    SDL_Gamepad* gamepad = nullptr;
    SDL_JoystickID id = 0;
    std::string name;
    int touchpads = 0;
    bool gyro = false;
    bool accel = false;
    float gyroData[3] = {};
    float accelData[3] = {};
    struct Finger { bool down = false; float x = 0, y = 0, pressure = 0; };
    std::vector<std::array<Finger, 4>> fingers;
};

struct App
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    deckpoc::CsvLog log;
    std::map<SDL_JoystickID, Pad> pads;
    std::vector<std::string> deviceLines;
    std::vector<std::string> envLines;
    std::map<std::string, uint64_t> countsThisSecond;
    std::map<std::string, uint64_t> ratesLastSecond;
    std::vector<uint64_t> handlingDelayNs;   // handled - event timestamp
    uint64_t secondStart = 0;
    uint64_t quitChordSince = 0;
    bool quit = false;
};

const char* EnvOr(const char* name, const char* fallback)
{
    const char* v = SDL_getenv(name);
    return v ? v : fallback;
}

void Enumerate(App& app)
{
    app.deviceLines.clear();
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    for (int i = 0; i < count; i++)
    {
        const SDL_JoystickID id = ids[i];
        char line[512];
        std::snprintf(line, sizeof line, "joy %u '%s' vid=%04x pid=%04x gamepad=%d type=%s path=%s", id,
                      SDL_GetJoystickNameForID(id), SDL_GetJoystickVendorForID(id), SDL_GetJoystickProductForID(id),
                      SDL_IsGamepad(id) ? 1 : 0,
                      SDL_IsGamepad(id) ? SDL_GetGamepadStringForType(SDL_GetGamepadTypeForID(id)) : "-",
                      SDL_GetJoystickPathForID(id) ? SDL_GetJoystickPathForID(id) : "-");
        app.deviceLines.emplace_back(line);
    }
    SDL_free(ids);

    for (auto& [id, pad] : app.pads)
    {
        SDL_PropertiesID props = SDL_GetGamepadProperties(pad.gamepad);
        char line[512];
        std::snprintf(line, sizeof line,
                      "  pad %u '%s' touchpads=%d gyro=%d accel=%d rumble=%d trigger_rumble=%d steam_handle=%llu",
                      id, pad.name.c_str(), pad.touchpads, pad.gyro, pad.accel,
                      SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false),
                      SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false),
                      static_cast<unsigned long long>(SDL_GetGamepadSteamHandle(pad.gamepad)));
        app.deviceLines.emplace_back(line);
    }
}

void OpenPad(App& app, SDL_JoystickID id)
{
    SDL_Gamepad* gamepad = SDL_OpenGamepad(id);
    if (!gamepad)
        return;
    Pad pad;
    pad.gamepad = gamepad;
    pad.id = id;
    pad.name = SDL_GetGamepadName(gamepad) ? SDL_GetGamepadName(gamepad) : "?";
    pad.touchpads = SDL_GetNumGamepadTouchpads(gamepad);
    pad.fingers.resize(static_cast<size_t>(pad.touchpads));
    pad.gyro = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO) && SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_GYRO, true);
    pad.accel = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_ACCEL) && SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_ACCEL, true);
    app.pads[id] = pad;
    Enumerate(app);
}

void Count(App& app, const char* kind)
{
    app.countsThisSecond[kind]++;
}

void HandleEvent(App& app, const SDL_Event& e)
{
    const uint64_t now = deckpoc::NowNs();
    const uint64_t evt = e.common.timestamp;
    if (evt && now >= evt && (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN))
        app.handlingDelayNs.push_back(now - evt);

    switch (e.type)
    {
    case SDL_EVENT_QUIT:
        app.quit = true;
        break;
    case SDL_EVENT_KEY_DOWN:
        app.log.Row(evt, "%llu,key_down,%d,%s,", static_cast<unsigned long long>(now), e.key.scancode, SDL_GetScancodeName(e.key.scancode));
        Count(app, "key");
        if (e.key.key == SDLK_ESCAPE)
            app.quit = true;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        app.log.Row(evt, "%llu,mouse_motion,%u,%.2f,%.2f", static_cast<unsigned long long>(now), e.motion.which, e.motion.xrel, e.motion.yrel);
        Count(app, "mouse");
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        app.log.Row(evt, "%llu,mouse_button,%u,%d,%d", static_cast<unsigned long long>(now), e.button.which, e.button.button, e.button.down);
        Count(app, "mouse");
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
        app.log.Row(evt, "%llu,touchscreen,%llu,%.4f,%.4f", static_cast<unsigned long long>(now),
                    static_cast<unsigned long long>(e.tfinger.fingerID), e.tfinger.x, e.tfinger.y);
        Count(app, "touchscreen");
        break;
    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_JOYSTICK_REMOVED:
        app.log.Row(evt, "%llu,joystick_%s,%u,,", static_cast<unsigned long long>(now),
                    e.type == SDL_EVENT_JOYSTICK_ADDED ? "added" : "removed", e.jdevice.which);
        Enumerate(app);
        break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
        // Raw buttons: the Deck's back buttons show here even where the gamepad mapping has no slot
        app.log.Row(evt, "%llu,joy_button,%u,%d,%d", static_cast<unsigned long long>(now), e.jbutton.which, e.jbutton.button, e.jbutton.down);
        Count(app, "joy_button");
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        OpenPad(app, e.gdevice.which);
        app.log.Row(evt, "%llu,gamepad_added,%u,,", static_cast<unsigned long long>(now), e.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (auto it = app.pads.find(e.gdevice.which); it != app.pads.end())
        {
            SDL_CloseGamepad(it->second.gamepad);
            app.pads.erase(it);
        }
        Enumerate(app);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    {
        const auto button = static_cast<SDL_GamepadButton>(e.gbutton.button);
        app.log.Row(evt, "%llu,pad_button,%u,%s,%d", static_cast<unsigned long long>(now), e.gbutton.which,
                    SDL_GetGamepadStringForButton(button), e.gbutton.down);
        Count(app, "pad_button");
        if (e.gbutton.down)
        {
            if (auto it = app.pads.find(e.gbutton.which); it != app.pads.end())
            {
                SDL_Gamepad* g = it->second.gamepad;
                if (button == SDL_GAMEPAD_BUTTON_NORTH)
                    SDL_RumbleGamepad(g, 0, 0xFFFF, 200);
                else if (button == SDL_GAMEPAD_BUTTON_WEST)
                    SDL_RumbleGamepad(g, 0xFFFF, 0, 200);
                else if (button == SDL_GAMEPAD_BUTTON_EAST)
                    SDL_RumbleGamepadTriggers(g, 0xFFFF, 0xFFFF, 200);
            }
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        app.log.Row(evt, "%llu,pad_axis,%u,%s,%d", static_cast<unsigned long long>(now), e.gaxis.which,
                    SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(e.gaxis.axis)), e.gaxis.value);
        Count(app, "pad_axis");
        break;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
    {
        app.log.Row(evt, "%llu,pad_touch,%u,%d/%d,%.4f %.4f %.4f", static_cast<unsigned long long>(now), e.gtouchpad.which,
                    e.gtouchpad.touchpad, e.gtouchpad.finger, e.gtouchpad.x, e.gtouchpad.y, e.gtouchpad.pressure);
        Count(app, "pad_touch");
        if (auto it = app.pads.find(e.gtouchpad.which); it != app.pads.end())
        {
            auto& pads = it->second.fingers;
            if (e.gtouchpad.touchpad >= 0 && static_cast<size_t>(e.gtouchpad.touchpad) < pads.size() && e.gtouchpad.finger >= 0 && e.gtouchpad.finger < 4)
            {
                auto& f = pads[static_cast<size_t>(e.gtouchpad.touchpad)][static_cast<size_t>(e.gtouchpad.finger)];
                f.down = e.type != SDL_EVENT_GAMEPAD_TOUCHPAD_UP;
                f.x = e.gtouchpad.x;
                f.y = e.gtouchpad.y;
                f.pressure = e.gtouchpad.pressure;
            }
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
        Count(app, e.gsensor.sensor == SDL_SENSOR_GYRO ? "gyro" : "accel");
        if (auto it = app.pads.find(e.gsensor.which); it != app.pads.end())
        {
            float* dst = e.gsensor.sensor == SDL_SENSOR_GYRO ? it->second.gyroData : it->second.accelData;
            std::memcpy(dst, e.gsensor.data, sizeof(float) * 3);
        }
        // Sensor timestamps (sensor_timestamp) give the controller's own report rate
        app.log.Row(evt, "%llu,sensor,%u,%d,%llu %.4f %.4f %.4f", static_cast<unsigned long long>(now), e.gsensor.which,
                    e.gsensor.sensor, static_cast<unsigned long long>(e.gsensor.sensor_timestamp),
                    e.gsensor.data[0], e.gsensor.data[1], e.gsensor.data[2]);
        break;
    default:
        break;
    }
}

void Draw(App& app)
{
    SDL_Renderer* r = app.renderer;
    SDL_SetRenderDrawColor(r, 16, 16, 20, 255);
    SDL_RenderClear(r);
    SDL_SetRenderScale(r, 2.0f, 2.0f);
    SDL_SetRenderDrawColor(r, 220, 220, 220, 255);

    float y = 4;
    auto text = [&](const std::string& s) { SDL_RenderDebugText(r, 4, y, s.c_str()); y += 10; };
    text("P-08 input inspector   Y/X rumble pads  B rumble triggers  View+Menu 1s = quit");
    text("log: " + app.log.Path());
    for (const auto& l : app.envLines)
        text(l);
    text("");
    for (const auto& l : app.deviceLines)
        text(l.substr(0, 150));
    text("");

    std::string rates = "events/s:";
    for (const auto& [k, v] : app.ratesLastSecond)
        rates += " " + k + "=" + std::to_string(v);
    text(rates);

    for (auto& [id, pad] : app.pads)
    {
        std::string buttons = "buttons:";
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
            if (SDL_GetGamepadButton(pad.gamepad, static_cast<SDL_GamepadButton>(b)))
                buttons += std::string(" ") + SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(b));
        text(buttons);

        SDL_Joystick* joy = SDL_GetGamepadJoystick(pad.gamepad);
        std::string raw = "raw joystick buttons:";
        for (int b = 0; b < SDL_GetNumJoystickButtons(joy); b++)
            if (SDL_GetJoystickButton(joy, b))
                raw += " " + std::to_string(b);
        text(raw + "  (of " + std::to_string(SDL_GetNumJoystickButtons(joy)) + ")");

        char axes[256];
        std::snprintf(axes, sizeof axes, "LX %6d LY %6d RX %6d RY %6d LT %6d RT %6d",
                      SDL_GetGamepadAxis(pad.gamepad, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(pad.gamepad, SDL_GAMEPAD_AXIS_LEFTY),
                      SDL_GetGamepadAxis(pad.gamepad, SDL_GAMEPAD_AXIS_RIGHTX), SDL_GetGamepadAxis(pad.gamepad, SDL_GAMEPAD_AXIS_RIGHTY),
                      SDL_GetGamepadAxis(pad.gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), SDL_GetGamepadAxis(pad.gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        text(axes);
        char sensors[256];
        std::snprintf(sensors, sizeof sensors, "gyro %7.3f %7.3f %7.3f   accel %7.3f %7.3f %7.3f",
                      pad.gyroData[0], pad.gyroData[1], pad.gyroData[2], pad.accelData[0], pad.accelData[1], pad.accelData[2]);
        text(sensors);

        // Trackpads as boxes, fingers as dots (size by pressure)
        for (size_t t = 0; t < pad.fingers.size(); t++)
        {
            const SDL_FRect box{4.0f + static_cast<float>(t) * 140.0f, y + 4, 128, 128};
            SDL_SetRenderDrawColor(r, 90, 90, 110, 255);
            SDL_RenderRect(r, &box);
            for (const auto& f : pad.fingers[t])
                if (f.down)
                {
                    const float s = 4 + f.pressure * 8;
                    const SDL_FRect dot{box.x + f.x * box.w - s / 2, box.y + f.y * box.h - s / 2, s, s};
                    SDL_SetRenderDrawColor(r, 0, 220, 220, 255);
                    SDL_RenderFillRect(r, &dot);
                }
        }
        if (!pad.fingers.empty())
            y += 140;
        SDL_SetRenderDrawColor(r, 220, 220, 220, 255);
    }

    SDL_SetRenderScale(r, 1.0f, 1.0f);
    SDL_RenderPresent(r);
}

void WriteSummary(App& app)
{
    const std::string path = deckpoc::LogPath("p08-summary", "txt");
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f)
        return;
    std::fprintf(f, "SDL %d.%d.%d, video driver %s\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION,
                 SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?");
    for (const auto& l : app.envLines)
        std::fprintf(f, "%s\n", l.c_str());
    for (const auto& l : app.deviceLines)
        std::fprintf(f, "%s\n", l.c_str());
    std::fprintf(f, "button event handling delay (handled - event timestamp): %s\n",
                 deckpoc::Describe(deckpoc::ComputeSamples(app.handlingDelayNs)).c_str());
    std::fprintf(f, "events log: %s\n", app.log.Path().c_str());
    std::fclose(f);
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--no-hidapi-steamdeck") == 0)
            SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "0");
        else if (std::strcmp(argv[i], "--hidapi-steamdeck") == 0)
            SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "1");
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }

    App app;
    if (!SDL_CreateWindowAndRenderer("P-08 input inspector", 1280, 800, SDL_WINDOW_FULLSCREEN, &app.window, &app.renderer))
    {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(app.renderer, 1);
    app.log.Open(deckpoc::LogPath("p08-events", "csv"), "t_handled_ns,kind,device,a,b");

    for (const char* name : {"SteamAppId", "SteamGameId", "SteamDeck", "SteamOS", "SDL_GAMECONTROLLER_IGNORE_DEVICES",
                             "SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "XDG_CURRENT_DESKTOP", "WAYLAND_DISPLAY", "DISPLAY",
                             "FLATPAK_ID", "PRESSURE_VESSEL_RUNTIME"})
        app.envLines.push_back(std::string(name) + "=" + EnvOr(name, "(unset)"));
    app.envLines.push_back(std::string("hint HIDAPI_STEAMDECK=") +
                           (SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK) ? SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK) : "(default)"));
    Enumerate(app);

    app.secondStart = deckpoc::NowNs();
    while (!app.quit)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            HandleEvent(app, e);

        // View + Menu held for a second quits (no keyboard in Game Mode)
        bool chord = false;
        for (auto& [id, pad] : app.pads)
            chord |= SDL_GetGamepadButton(pad.gamepad, SDL_GAMEPAD_BUTTON_BACK) && SDL_GetGamepadButton(pad.gamepad, SDL_GAMEPAD_BUTTON_START);
        const uint64_t now = deckpoc::NowNs();
        if (!chord)
            app.quitChordSince = 0;
        else if (app.quitChordSince == 0)
            app.quitChordSince = now;
        else if (now - app.quitChordSince > 1'000'000'000ull)
            app.quit = true;

        if (now - app.secondStart >= 1'000'000'000ull)
        {
            app.ratesLastSecond = app.countsThisSecond;
            app.countsThisSecond.clear();
            app.secondStart = now;
            app.log.Flush();
        }
        Draw(app);
    }

    WriteSummary(app);
    for (auto& [id, pad] : app.pads)
        SDL_CloseGamepad(pad.gamepad);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
