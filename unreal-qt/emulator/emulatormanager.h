#pragma once

#ifndef EMULATORMANAGER_H
#define EMULATORMANAGER_H

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"
#include "emulator/io/keyboard/keyboard.h"

class EmulatorManager
{   
private:
    static EmulatorManager* _instance;
    EmulatorManager();     // Do not allow to create object explicitly. Use singleton method EmulatorManager::defaultInstance();

public:
    virtual ~EmulatorManager();


public:
    static EmulatorManager* defaultInstance();
    Emulator* createEmulatorInstance();
    void destroyEmulatorInstance(Emulator* emulator);

    void sendKeyEvent(uint8_t key, KeyEventEnum type);

private:

};

#endif // EMULATORMANAGER_H
