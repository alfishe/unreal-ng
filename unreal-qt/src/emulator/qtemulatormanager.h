#pragma once

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"
#include "emulator/io/keyboard/keyboard.h"
#include "soundmanager.h"

class QtEmulatorManager
{
    /// region <Fields>
private:
    static QtEmulatorManager* _instance;

protected:
    AppSoundManager _soundManager;

    /// endregion </Fields>

    /// region <Constructors / destructors>
private:
    QtEmulatorManager();     // Do not allow to create object explicitly. Use singleton method EmulatorManager::defaultInstance();

public:
    virtual ~QtEmulatorManager();
    /// endregion </Constructors / destructors>


    /// region <Methods>
public:
    static QtEmulatorManager* defaultInstance();
    Emulator* createEmulatorInstance();
    void destroyEmulatorInstance(Emulator* emulator);

    AppSoundManager& getSoundManager() { return _soundManager; };

    void sendKeyEvent(uint8_t key, KeyEventEnum type);
    /// endregion </Methods>

private:

};

