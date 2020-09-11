#pragma once

#ifndef EMULATORMANAGER_H
#define EMULATORMANAGER_H

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"
#include "emulator/io/keyboard/keyboard.h"

class EmulatorManager
{   
public:
    EmulatorManager();
    virtual ~EmulatorManager();

public:
    Emulator* createEmulatorInstance();
    void destroyEmulatorInstanc(Emulator* emulator);

    void sendKeyEvent(uint8_t key, KeyEventEnum type);
};

#endif // EMULATORMANAGER_H
