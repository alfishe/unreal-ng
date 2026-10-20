// unreal-streamdeck pair [--webapi URL] [--name NAME] [--file F]: pair as an unreal-ng companion (approved at the
// emulator's GUI). The 6-digit code is shown on the touch strip and the console; the token is stored in the companion
// file (mode 0600).
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "cli.h"
#include "sdb/actions.h"
#include "sdb/device.h"
#include "sdb/http.h"
#include "sdb/scenes.h"

using json = nlohmann::json;
using namespace sdb;
using scenes::Status;

namespace cli
{
namespace
{

std::string Upper(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

int CmdPair(const Args& a)
{
    const std::string url = a.Str("webapi", "http://127.0.0.1:8090");
    const std::string file = a.Str("file", scn::DefaultCompanionFile());
    std::string err;
    std::unique_ptr<Device> dev = OpenFirst(a.Str("device"), &err);
    const std::string devName = dev ? std::string(dev->GetModel().name) : std::string("Stream Deck");
    const json req = {{"name", a.Str("name", devName)}, {"device", devName + " (unreal-streamdeck)"}};
    auto r = HttpRequest(url, "POST", "/api/v1/companions/pairings", req.dump());
    json j;
    try
    {
        j = json::parse(r.body);
    }
    catch (...)
    {
    }
    if (r.status != 202)
    {
        std::fprintf(stderr, "error: pairing request refused: %d %s %s\n", r.status, j.value("error", r.error).c_str(),
                     j.value("message", "").c_str());
        return r.status == 0 ? kNoDevice : kDeviceError;
    }
    const std::string pid = j.value("pairing_id", ""), code = j.value("code", "");
    std::printf("Pairing code %s - allow it at the emulator (unreal-qt shows the same code). Waiting %d s...\n",
                code.c_str(), j.value("expires_in", 120));
    std::fflush(stdout);
    const bool lcd = dev && dev->GetModel().HasLcd();
    const scenes::Size sz{lcd ? dev->GetModel().lcdLogicalW : 1200, lcd ? dev->GetModel().lcdLogicalH : 100};
    for (int i = 0; i < 300; ++i)
    {
        if (lcd)
            (void)dev->SetLcdFull(scenes::StatusScene(sz, Status::Info,
                                                      "PAIR CODE " + code.substr(0, 3) + " " + code.substr(3),
                                                      "ALLOW IT AT THE EMULATOR", i));
        auto p = HttpRequest(url, "GET", "/api/v1/companions/pairings/" + pid);
        json pj;
        try
        {
            pj = json::parse(p.body);
        }
        catch (...)
        {
        }
        const std::string state = pj.value("state", p.status == 404 ? "unknown" : "");
        if (state == "approved")
        {
            std::filesystem::create_directories(std::filesystem::path(file).parent_path());
            json store = {{"webapi", url},
                          {"token", pj.value("token", "")},
                          {"companion_id", pj.value("companion_id", "")},
                          {"scopes", pj.value("scopes", json::array())}};
            {
                std::ofstream out(file);
                out << store.dump(2);
            }
            std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                         std::filesystem::perm_options::replace);
            std::printf("paired as %s, scopes %s; token stored in %s\n",
                        store["companion_id"].get<std::string>().c_str(), store["scopes"].dump().c_str(), file.c_str());
            if (lcd)
                (void)dev->SetLcdFull(
                    scenes::StatusScene(sz, Status::Ok, "PAIRED", "STREAM DECK IS A COMPANION NOW", 10));
            return kOk;
        }
        if (state == "denied" || state == "expired" || state == "unknown")
        {
            std::fprintf(stderr, "pairing %s\n", state.c_str());
            if (lcd)
                (void)dev->SetLcdFull(scenes::StatusScene(sz, Status::Error, "PAIRING " + Upper(state), "", 10));
            return kDeviceError;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    return kDeviceError;
}

}  // namespace cli
