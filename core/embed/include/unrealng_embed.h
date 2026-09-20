#ifndef _INCLUDED_UNREALNG_EMBED_H_
#define _INCLUDED_UNREALNG_EMBED_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(BUILD_UNREALNG_EMBED_DLL)
  #define APP_EXPORT __declspec(dllexport)
#elif defined(_WIN32)
  #define APP_EXPORT
#else
  #define APP_EXPORT __attribute__((visibility("default")))
#endif

typedef struct app_emulator app_emulator;

typedef enum {
    APP_OK = 0,
    APP_ERR_ARG = 1,
    APP_ERR_STATE = 2,
    APP_ERR_IO = 3,
    APP_ERR_INTERNAL = 4
} app_result;

enum {
    APP_AUTOMATION_NONE    = 0,
    APP_AUTOMATION_WEBAPI  = (1 << 0),
    APP_AUTOMATION_CLI     = (1 << 1),
    APP_AUTOMATION_GDB     = (1 << 2),
    APP_AUTOMATION_DEZOG   = (1 << 3),
    APP_AUTOMATION_ZESARUX = (1 << 4),
    APP_AUTOMATION_MCP     = (1 << 5),
    APP_AUTOMATION_LUA     = (1 << 6),
    APP_AUTOMATION_ALL     = 0xFFFF
};

typedef struct {
    const char* resource_root;   // read-only: rom/, configs/, fonts/ ...
    const char* writable_root;   // config writes, screenshots, TTD, uploads
    uint32_t    log_level;       // 0=error, 1=warning, 2=info, 3=debug
    uint32_t    automation_mask; // Bitwise OR of APP_AUTOMATION_*
    uint16_t    webapi_port;     // 0 defaults to 8090
    uint16_t    cli_port;        // 0 defaults to 8765
} app_init_params;

APP_EXPORT app_result app_init(const app_init_params* params);
APP_EXPORT void       app_shutdown(void);

APP_EXPORT app_result app_create(const char* model, const char* symbolic_id, app_emulator** out);
APP_EXPORT app_result app_start(app_emulator* emu);
APP_EXPORT void       app_destroy(app_emulator* emu);

// Video — thread-safe; copies presented latched frame
APP_EXPORT app_result app_frame_info(app_emulator* emu, uint16_t* w, uint16_t* h, uint64_t* latch_ts_us);
APP_EXPORT app_result app_copy_frame(app_emulator* emu, void* dst_rgba8, size_t dst_size);
APP_EXPORT void       app_set_present_delay(app_emulator* emu, uint8_t frames);

// Audio
APP_EXPORT app_result app_audio_open_device(app_emulator* emu);
APP_EXPORT app_result app_audio_attach_pull(app_emulator* emu, uint32_t device_rate);
APP_EXPORT size_t     app_audio_pull_f32(app_emulator* emu, float* interleaved, size_t frames);
APP_EXPORT void       app_audio_set_active(app_emulator* emu, int active);

// Keyboard
APP_EXPORT app_result app_keyboard_press(app_emulator* emu, const char* key_name);
APP_EXPORT app_result app_keyboard_release(app_emulator* emu, const char* key_name);
APP_EXPORT app_result app_keyboard_tap(app_emulator* emu, const char* key_name, uint16_t hold_frames);
APP_EXPORT app_result app_keyboard_release_all(app_emulator* emu);

// System info
APP_EXPORT const char* app_get_version_string(void);

#ifdef __cplusplus
}
#endif

#endif // _INCLUDED_UNREALNG_EMBED_H_
