#pragma once

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"
#include "emulator/io/keyboard/keyboard.h"
#include "soundmanager.h"

class EmulatorManager
{
    /// region <Fields>
private:
    static EmulatorManager* _instance;

protected:
    AppSoundManager _soundManager;

    /// endregion </Fields>

    /// region <Constructors / destructors>
private:
    EmulatorManager();     // Do not allow to create object explicitly. Use singleton method EmulatorManager::defaultInstance();

public:
    virtual ~EmulatorManager();
    /// endregion </Constructors / destructors>


    /// region <Methods>
public:
    static EmulatorManager* defaultInstance();
    Emulator* createEmulatorInstance();
    void destroyEmulatorInstance(Emulator* emulator);

    AppSoundManager& getSoundManager() { return _soundManager; };

    void sendKeyEvent(uint8_t key, KeyEventEnum type);
    /// endregion </Methods>

private:

};

