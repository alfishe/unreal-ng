#include "emulatormanager.h"

EmulatorManager* EmulatorManager::_instance = nullptr;

EmulatorManager::EmulatorManager()
{

}

EmulatorManager::~EmulatorManager()
{

}

EmulatorManager* EmulatorManager::defaultInstance()
{
    if (!_instance)
    {
        _instance = new EmulatorManager();
    }

    return _instance;
}

Emulator* EmulatorManager::createEmulatorInstance()
{
    Emulator* result = nullptr;

    return result;
}

void EmulatorManager::destroyEmulatorInstance(Emulator* emulator)
{

}

void EmulatorManager::sendKeyEvent(uint8_t key, KeyEventEnum type)
{
    static MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

    KeyboardEvent* event = new KeyboardEvent(key, type);
    std::string topic;

    switch (type)
    {
        case KEY_PRESSED:
            topic = MC_KEY_PRESSED;
            break;
        case KEY_RELEASED:
            topic = MC_KEY_RELEASED;
            break;
    }

    messageCenter.Post(topic, event);
}
