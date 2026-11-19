/* Guest-side glue of the VDAC4 ESP HLE ROM (tools/vdac4/esp-rom/build.sh).
 *
 * What is easier as guest code than as a host trap: variadic entries (the firmware formats with vsnprintf, so the
 * same newlib code does it here) and the task trampoline. The zm_host_* names are host traps (traps.txt). */
#include <stdarg.h>
#include <stdio.h>

extern void zm_host_log_line(const char* line);
extern void zm_host_task_exit(void);
extern void zm_host_return(void);

/* zm_core_t::log: ModuleApp::coreLog formats into 136 bytes (module_app.cpp:1326-1333) */
void zm_rom_log(const char* format, ...)
{
    char line[136];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    zm_host_log_line(line);
}

/* ModuleApp::taskTrampoline: the task function returns, the task is deleted */
void zm_rom_task_entry(void (*fn)(void*), void* arg)
{
    fn(arg);
    zm_host_task_exit();
}

/* --- FT812 open: ModuleApp::coreFtOpen + ft_host::switchToFtCore / setHost / boot1024x768 ---------------------
 * (FW/src/apps/module_app.cpp:1582-1603, FW/src/ft_host.cpp). Guest code because it waits (vTaskDelay). The LINK
 * words go through host traps; FT memory accesses are the native ft_read / ft_write traps. */
#include <stdint.h>

extern uint32_t zm_host_link_read(uint32_t address);
extern void zm_host_link_write(uint32_t address, uint32_t value);
extern void zm_host_delay(uint32_t ms);
extern uint32_t zm_host_millis(void);
extern int zm_host_ft_read(uint32_t address, uint32_t* value);
extern int zm_host_ft_write(uint32_t address, const uint32_t* words, uint32_t count);
extern void zm_host_ft_opened(int selected);

enum {
    kLinkId = 0x00, kLinkVersion = 0x04, kLinkStatus = 0x08, kLinkFtStat = 0x5C, kLinkFtHost = 0xE8,
    kLinkBoot = 0xF0
};
#define kIdValue 0x56444334u
#define kBootTag 0xB00u
#define kBootMagic 0x424F4F54u

static int CoreHasFt(void)
{
    const uint32_t boot = zm_host_link_read(kLinkBoot);
    if ((boot >> 20) != kBootTag)
        return 1;
    return (boot >> 17) & 1u;
}

static int SwitchToFtCore(uint32_t timeoutMs)
{
    if (CoreHasFt())
        return 1;
    zm_host_link_write(kLinkBoot, kBootMagic);
    for (uint32_t waited = 0; waited < timeoutMs; waited += 100) {
        zm_host_delay(100);
        if (zm_host_link_read(kLinkId) == kIdValue && CoreHasFt())
            return 1;
    }
    return 0;
}

static void SetHost(int esp)
{
    const uint32_t st = zm_host_link_read(kLinkFtStat);
    const uint32_t cfg = ((st >> 27) & 1u) | (((st >> 13) & 1u) << 1) | (esp ? 4u : 0u);
    zm_host_link_write(kLinkFtStat, cfg);
}

static void HostCommand(uint8_t command, uint8_t parameter)
{
    zm_host_link_write(kLinkFtHost, command | ((uint32_t)parameter << 8));
}

static int Boot1024x768(void)
{
    HostCommand(0x68, 0);          /* core reset */
    HostCommand(0x44, 0);          /* CLKEXT */
    HostCommand(0x42, 0);          /* SLEEP */
    HostCommand(0x61, 8 | 0xC0);   /* CLKSEL x8 */
    HostCommand(0x00, 0);          /* ACTIVE */
    zm_host_delay(20);
    uint32_t id = 0;
    for (int i = 0; i < 50 && (id & 0xFF) != 0x7C; ++i) {
        zm_host_delay(10);
        if (zm_host_ft_read(0x302000, &id) != 0)
            id = 0;
    }
    if ((id & 0xFF) != 0x7C)
        return 0;
    static const uint32_t kMode[][2] = {
        {0x302070, 0},   {0x302038, 24},   {0x30203C, 24 + 136}, {0x302030, 24 + 136 + 160},
        {0x302034, 1024}, {0x30202C, 1344}, {0x30204C, 2},        {0x302050, 8},
        {0x302044, 37},  {0x302048, 768},  {0x302040, 806},      {0x30206C, 0},
        {0x302068, 0},   {0x302070, 1},
    };
    for (unsigned i = 0; i < sizeof(kMode) / sizeof(kMode[0]); ++i) {
        if (zm_host_ft_write(kMode[i][0], &kMode[i][1], 1) != 0)
            return 0;
    }
    return 1;
}

int zm_rom_ft_open(uint32_t timeoutMs)
{
    if (zm_host_link_read(kLinkId) != kIdValue)
        return -3;                 /* ZM_NOT_FOUND: Tang not ready */
    if (!SwitchToFtCore(timeoutMs))
        return -3;
    SetHost(1);
    const int selected = zm_host_link_read(kLinkId) == kIdValue && (zm_host_link_read(kLinkVersion) >> 24) == 2 &&
                         (zm_host_link_read(kLinkStatus) & (1u << 12)) != 0;
    zm_host_ft_opened(selected);
    return Boot1024x768() ? 0 : -1;
}

void zm_rom_ft_close(void)
{
    SetHost(0);
}
