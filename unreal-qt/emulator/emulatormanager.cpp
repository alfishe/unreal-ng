#include "emulatormanager.h"

EmulatorManager::EmulatorManager()
{

}

EmulatorManager::~EmulatorManager()
{

}

Emulator* createEmulatorInstance()
{

}

void destroyEmulatorInstanc(Emulator* emulator)
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
