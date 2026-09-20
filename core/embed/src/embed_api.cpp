#include "unrealng_embed.h"
#include "embed_audio.h"
#include "embed_video.h"

#include "common/filehelper.h"
#include "common/logger.h"
#include "emulator/emulatormanager.h"
#include "emulator/emulator.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/io/keyboard/keyboard.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "3rdparty/message-center/messagecenter.h"
#include "automation.h"

#include <mutex>
#include <string>

struct app_emulator {
    std::mutex mutex;
    std::shared_ptr<Emulator> emu;
    EmbedAudio audio;
    EmbedVideo video;
};

static bool s_initialized = false;
static app_notification_fn s_notification_cb = nullptr;
static void* s_notification_user_data = nullptr;
static std::mutex s_notification_mutex;

static void OnMessageCenterNotification(int id, Message* msg)
{
    app_notification_fn cb = nullptr;
    void* userData = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_notification_mutex);
        cb = s_notification_cb;
        userData = s_notification_user_data;
    }

    if (!cb)
        return;

    std::string topicName = MessageCenter::DefaultMessageCenter().GetTopicByID(id);
    std::string jsonPayload = "{}";

    if (topicName == NC_EMULATOR_SELECTION_CHANGED)
    {
        if (msg && msg->obj)
        {
            if (auto* p = dynamic_cast<EmulatorSelectionPayload*>(msg->obj))
            {
                jsonPayload = "{\"previous_id\":\"" + p->previousEmulatorId.toString() + "\",\"new_id\":\"" + p->newEmulatorId.toString() + "\"}";
            }
        }
    }
    else if (topicName == NC_EMULATOR_INSTANCE_CREATED)
    {
        if (msg && msg->obj)
        {
            if (auto* p = dynamic_cast<SimpleTextPayload*>(msg->obj))
            {
                jsonPayload = "{\"id\":\"" + p->_payloadText + "\"}";
            }
        }
    }
    else if (topicName == NC_EMULATOR_INSTANCE_DESTROYED)
    {
        if (msg && msg->obj)
        {
            if (auto* p = dynamic_cast<SimpleTextPayload*>(msg->obj))
            {
                jsonPayload = "{\"id\":\"" + p->_payloadText + "\"}";
            }
        }
    }
    else if (topicName == NC_FDD_DISK_INSERTED)
    {
        if (msg && msg->obj)
        {
            if (auto* p = dynamic_cast<FDDDiskPayload*>(msg->obj))
            {
                jsonPayload = "{\"emulator_id\":\"" + p->_emulatorId.toString() + "\",\"drive_id\":" + std::to_string(p->_driveId) + ",\"path\":\"" + p->_diskPath + "\"}";
            }
        }
    }
    else if (topicName == NC_FDD_DISK_EJECTED)
    {
        if (msg && msg->obj)
        {
            if (auto* p = dynamic_cast<FDDDiskPayload*>(msg->obj))
            {
                jsonPayload = "{\"emulator_id\":\"" + p->_emulatorId.toString() + "\",\"drive_id\":" + std::to_string(p->_driveId) + "}";
            }
        }
    }
    else if (topicName == NC_DISK_AUTOSTART)
    {
        if (msg && msg->obj)
        {
            if (auto* p = dynamic_cast<DiskAutostartPayload*>(msg->obj))
            {
                jsonPayload = "{\"emulator_id\":\"" + p->emulatorId.toString() + "\",\"message\":\"" + p->message + "\",\"started\":" + (p->started ? "true" : "false") + "}";
            }
        }
    }

    cb(topicName.c_str(), jsonPayload.c_str(), userData);
}

app_result app_init(const app_init_params* params)
{
    if (!params)
        return APP_ERR_ARG;

    if (params->resource_root && params->resource_root[0] != '\0')
    {
        FileHelper::SetResourcesPathOverride(params->resource_root);
    }
    if (params->writable_root && params->writable_root[0] != '\0')
    {
        FileHelper::SetWritablePathOverride(params->writable_root);
    }

#if defined(ENABLE_AUTOMATION)
    try {
        Automation::GetInstance().start();
    } catch (...) {
        LOGERROR("app_init - Automation initialization failed");
    }
#endif

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    mc.AddObserver(NC_EMULATOR_SELECTION_CHANGED, OnMessageCenterNotification);
    mc.AddObserver(NC_EMULATOR_INSTANCE_CREATED, OnMessageCenterNotification);
    mc.AddObserver(NC_EMULATOR_INSTANCE_DESTROYED, OnMessageCenterNotification);
    mc.AddObserver(NC_FDD_DISK_INSERTED, OnMessageCenterNotification);
    mc.AddObserver(NC_FDD_DISK_EJECTED, OnMessageCenterNotification);
    mc.AddObserver(NC_DISK_AUTOSTART, OnMessageCenterNotification);

    s_initialized = true;
    return APP_OK;
}

void app_shutdown(void)
{
    if (!s_initialized)
        return;

#if defined(ENABLE_AUTOMATION)
    Automation::GetInstance().stop();
#endif

    EmulatorManager::GetInstance()->ShutdownAllEmulators();
    s_initialized = false;
}

app_result app_create(const char* model, const char* symbolic_id, app_emulator** out)
{
    if (!out)
        return APP_ERR_ARG;

    *out = nullptr;
    const char* modelName = (model && model[0] != '\0') ? model : "PENTAGON";
    const char* symId     = (symbolic_id && symbolic_id[0] != '\0') ? symbolic_id : "ios-01";

    std::string err;
    std::shared_ptr<Emulator> emu = EmulatorManager::GetInstance()->CreateEmulatorWithModel(symId, modelName, LoggerLevel::LogInfo, &err);
    if (!emu)
    {
        LOGERROR("app_create - Failed to create emulator model %s: %s", modelName, err.c_str());
        return APP_ERR_INTERNAL;
    }

    app_emulator* wrapper = new (std::nothrow) app_emulator();
    if (!wrapper)
    {
        EmulatorManager::GetInstance()->RemoveEmulator(emu->GetId());
        return APP_ERR_INTERNAL;
    }

    wrapper->emu = emu;
    *out = wrapper;
    return APP_OK;
}

app_result app_start(app_emulator* emu)
{
    if (!emu)
        return APP_ERR_ARG;

    std::lock_guard<std::mutex> lock(emu->mutex);
    if (!emu->emu)
        return APP_ERR_ARG;

    bool started = EmulatorManager::GetInstance()->StartEmulatorAsync(emu->emu->GetId());
    return started ? APP_OK : APP_ERR_STATE;
}

void app_destroy(app_emulator* emu)
{
    if (!emu)
        return;

    std::shared_ptr<Emulator> targetEmu;
    {
        std::lock_guard<std::mutex> lock(emu->mutex);
        emu->audio.Close();
        targetEmu = emu->emu;
        emu->emu.reset();
    }

    if (targetEmu)
    {
        EmulatorManager::GetInstance()->RemoveEmulator(targetEmu->GetId());
    }
    delete emu;
}

static bool IsEmulatorValid(const std::shared_ptr<Emulator>& emu)
{
    if (!emu)
        return false;

    EmulatorContext* ctx = emu->GetContext();
    if (!ctx || !ctx->pScreen)
        return false;

    return true;
}

static std::shared_ptr<Emulator> GetActiveEmulator(app_emulator* wrapper)
{
    if (!wrapper)
        return nullptr;

    auto manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> activeEmu;

    // 1. Check selected emulator ID from EmulatorManager
    std::string selectedId = manager->GetSelectedEmulatorId();
    if (!selectedId.empty())
    {
        activeEmu = manager->GetEmulator(selectedId);
    }

    // 2. Fallback to latest emulator in manager
    if (!activeEmu)
    {
        auto ids = manager->GetEmulatorIds();
        if (!ids.empty())
        {
            activeEmu = manager->GetEmulator(ids.back());
        }
    }

    // 3. Fallback to wrapper's stored emulator ONLY if valid
    if (!activeEmu && IsEmulatorValid(wrapper->emu))
    {
        activeEmu = wrapper->emu;
    }

    // Validate chosen activeEmu
    if (!IsEmulatorValid(activeEmu))
    {
        if (wrapper->emu)
        {
            wrapper->emu.reset();
        }
        return nullptr;
    }

    // Dynamic adoption when active selection changes
    if (activeEmu != wrapper->emu)
    {
        if (IsEmulatorValid(wrapper->emu))
        {
            wrapper->emu->SetAudioCallback(nullptr, nullptr, nullptr, nullptr);
        }

        wrapper->emu = activeEmu;

        uint32_t rate = wrapper->audio.GetSampleRate();
        if (rate == 0) rate = 44100;
        activeEmu->SetAudioCallback(&wrapper->audio, EmbedAudio::AudioProducerCallback,
                                    wrapper->audio.GetOccupancyFrames(), &wrapper->audio.GetDeviceDescriptor());
        activeEmu->SetAudioDeviceSampleRate(rate);

        LOGINFO("embed_api - Adopted active emulator instance '%s'",
                activeEmu->GetId().c_str());
    }

    return activeEmu;
}

app_result app_frame_info(app_emulator* emu, uint16_t* w, uint16_t* h, uint64_t* latch_ts_us)
{
    if (!emu)
        return APP_ERR_ARG;

    std::lock_guard<std::mutex> lock(emu->mutex);
    std::shared_ptr<Emulator> activeEmu = GetActiveEmulator(emu);
    if (!activeEmu)
        return APP_ERR_STATE;

    return emu->video.GetFrameInfo(activeEmu.get(), w, h, latch_ts_us);
}

app_result app_copy_frame(app_emulator* emu, void* dst_rgba8, size_t dst_size)
{
    if (!emu)
        return APP_ERR_ARG;

    std::lock_guard<std::mutex> lock(emu->mutex);
    std::shared_ptr<Emulator> activeEmu = GetActiveEmulator(emu);
    if (!activeEmu)
        return APP_ERR_STATE;

    return emu->video.CopyFrame(activeEmu.get(), dst_rgba8, dst_size);
}

void app_set_present_delay(app_emulator* emu, uint8_t frames)
{
    if (!emu)
        return;

    std::lock_guard<std::mutex> lock(emu->mutex);
    std::shared_ptr<Emulator> activeEmu = GetActiveEmulator(emu);
    if (activeEmu)
    {
        emu->video.SetPresentDelay(activeEmu.get(), frames);
    }
}

app_result app_audio_open_device(app_emulator* emu)
{
    if (!emu)
        return APP_ERR_ARG;

    std::lock_guard<std::mutex> lock(emu->mutex);
    std::shared_ptr<Emulator> activeEmu = GetActiveEmulator(emu);
    if (!activeEmu)
        return APP_ERR_STATE;

    return emu->audio.OpenDevice(activeEmu.get());
}

app_result app_audio_attach_pull(app_emulator* emu, uint32_t device_rate)
{
    if (!emu)
        return APP_ERR_ARG;

    std::lock_guard<std::mutex> lock(emu->mutex);
    std::shared_ptr<Emulator> activeEmu = GetActiveEmulator(emu);
    if (!activeEmu)
        return APP_ERR_STATE;

    return emu->audio.AttachPull(activeEmu.get(), device_rate);
}

size_t app_audio_pull_f32(app_emulator* emu, float* interleaved, size_t frames)
{
    if (!emu)
        return 0;

    std::lock_guard<std::mutex> lock(emu->mutex);
    return emu->audio.PullF32(interleaved, frames);
}

void app_audio_set_active(app_emulator* emu, int active)
{
    if (!emu)
        return;

    std::lock_guard<std::mutex> lock(emu->mutex);
    emu->audio.SetActive(active);
}

app_result app_keyboard_press(app_emulator* emu, const char* key_name)
{
    if (!emu || !key_name || key_name[0] == '\0')
        return APP_ERR_ARG;

    std::lock_guard<std::mutex> lock(emu->mutex);
    std::shared_ptr<Emulator> activeEmu = GetActiveEmulator(emu);
    if (!activeEmu)
        return APP_ERR_STATE;

    ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(key_name);
    if (key == ZXKEY_NONE)
        return APP_ERR_ARG;

    std::string targetId = activeEmu->GetId();
    MessageCenter::DefaultMessageCenter().Post(
        MC_KEY_PRESSED,
        new KeyboardEvent(static_cast<uint8_t>(key), KEY_PRESSED, targetId)
    );
    return APP_OK;
}

app_result app_keyboard_release(app_emulator* emu, const char* key_name)
{
    if (!emu || !key_name || key_name[0] == '\0')
        return APP_ERR_ARG;

    std::lock_guard<std::mutex> lock(emu->mutex);
    std::shared_ptr<Emulator> activeEmu = GetActiveEmulator(emu);
    if (!activeEmu)
        return APP_ERR_STATE;

    ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(key_name);
    if (key == ZXKEY_NONE)
        return APP_ERR_ARG;

    std::string targetId = activeEmu->GetId();
    MessageCenter::DefaultMessageCenter().Post(
        MC_KEY_RELEASED,
        new KeyboardEvent(static_cast<uint8_t>(key), KEY_RELEASED, targetId)
    );
    return APP_OK;
}

app_result app_keyboard_tap(app_emulator* emu, const char* key_name, uint16_t hold_frames)
{
    (void)hold_frames;
    if (!emu || !key_name || key_name[0] == '\0')
        return APP_ERR_ARG;

    std::lock_guard<std::mutex> lock(emu->mutex);
    std::shared_ptr<Emulator> activeEmu = GetActiveEmulator(emu);
    if (!activeEmu)
        return APP_ERR_STATE;

    ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(key_name);
    if (key == ZXKEY_NONE)
        return APP_ERR_ARG;

    std::string targetId = activeEmu->GetId();
    MessageCenter::DefaultMessageCenter().Post(
        MC_KEY_PRESSED,
        new KeyboardEvent(static_cast<uint8_t>(key), KEY_PRESSED, targetId)
    );
    MessageCenter::DefaultMessageCenter().Post(
        MC_KEY_RELEASED,
        new KeyboardEvent(static_cast<uint8_t>(key), KEY_RELEASED, targetId)
    );
    return APP_OK;
}

app_result app_keyboard_release_all(app_emulator* emu)
{
    if (!emu)
        return APP_ERR_ARG;

    std::lock_guard<std::mutex> lock(emu->mutex);
    std::shared_ptr<Emulator> activeEmu = GetActiveEmulator(emu);
    if (!activeEmu)
        return APP_ERR_STATE;

    std::string targetId = activeEmu->GetId();
    auto keys = DebugKeyboardManager::GetAllKeyNames();
    for (const auto& kn : keys)
    {
        ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(kn);
        if (key != ZXKEY_NONE)
        {
            MessageCenter::DefaultMessageCenter().Post(
                MC_KEY_RELEASED,
                new KeyboardEvent(static_cast<uint8_t>(key), KEY_RELEASED, targetId)
            );
        }
    }
    return APP_OK;
}

void app_set_notification_callback(app_notification_fn callback, void* user_data)
{
    std::lock_guard<std::mutex> lock(s_notification_mutex);
    s_notification_cb = callback;
    s_notification_user_data = user_data;
}

const char* app_get_version_string(void)
{
    return "1.0.0-ios";
}
