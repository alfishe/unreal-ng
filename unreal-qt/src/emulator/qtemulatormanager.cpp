#include "qtemulatormanager.h"

QtEmulatorManager* QtEmulatorManager::_instance = nullptr;

QtEmulatorManager::QtEmulatorManager()
{

}

QtEmulatorManager::~QtEmulatorManager()
{

}

QtEmulatorManager* QtEmulatorManager::defaultInstance()
{
    if (!_instance)
    {
        _instance = new QtEmulatorManager();
    }

    return _instance;
}

Emulator* QtEmulatorManager::createEmulatorInstance()
{
    Emulator* result = new Emulator();


    return result;
}

void QtEmulatorManager::destroyEmulatorInstance(Emulator* emulator)
{
    if (emulator)
    {
        emulator->Release();
        delete emulator;
    }
}

void QtEmulatorManager::sendKeyEvent(uint8_t key, KeyEventEnum type)
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
