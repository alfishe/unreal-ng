// Diagnostic: reproduce the 3D Video Cube instance bootstrap on the host.
//
// Performs the exact embed-API sequence the iOS cube bridge uses
// (app_init -> 6x app_create -> 6x app_start) and then serves the WebAPI so
// the instance list can be queried externally with:
//   curl http://127.0.0.1:<port>/api/v1/emulator
//
// Usage: diag_embed_multiinstance [resource_root] [writable_root] [port]

#include "unrealng_embed.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    const char* resources = argc > 1 ? argv[1] : "data";
    const char* writable  = argc > 2 ? argv[2] : "scratch/embed-diag";
    const int   port      = argc > 3 ? std::atoi(argv[3]) : 18090;

    app_init_params params;
    memset(&params, 0, sizeof(params));
    params.resource_root = resources;
    params.writable_root = writable;
    params.log_level = 2;
    params.automation_mask = APP_AUTOMATION_WEBAPI;  // WebAPI only - no CLI/GDB/etc side servers
    params.webapi_port = static_cast<uint16_t>(port);
    params.cli_port = 0;

    app_result r = app_init(&params);
    std::printf("app_init: %d\n", static_cast<int>(r));
    if (r != APP_OK)
        return 1;

    const char* models[6] = { "PENTAGON", "PENTAGON", "PENTAGON", "48K", "128k", "PENTAGON" };
    const char* ids[6] = {
        "diag-face-0", "diag-face-1", "diag-face-2",
        "diag-face-3", "diag-face-4", "diag-face-5"
    };

    int created = 0;
    int started = 0;
    for (int i = 0; i < 6; i++)
    {
        app_emulator* emu = nullptr;
        app_result c = app_create(models[i], ids[i], &emu);
        std::printf("create %d (%s, %s): res=%d emu=%p\n", i, models[i], ids[i],
                    static_cast<int>(c), static_cast<void*>(emu));
        if (c == APP_OK && emu)
        {
            created++;
            app_result s = app_start(emu);
            std::printf("start  %d: res=%d\n", i, static_cast<int>(s));
            if (s == APP_OK)
                started++;
        }
    }
    std::printf("SUMMARY: created %d/6, started %d/6\n", created, started);
    std::printf("WebAPI on http://127.0.0.1:%d/api/v1/emulator - press Enter to exit\n", port);

    std::string line;
    std::getline(std::cin, line);

    app_shutdown();
    return (created == 6 && started == 6) ? 0 : 2;
}
