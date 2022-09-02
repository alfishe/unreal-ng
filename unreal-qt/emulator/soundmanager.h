#pragma once

#ifndef SOUNDMANAGER_H
#define SOUNDMANAGER_H

#include <QObject>

class SoundManager
{
    /// region <Constructors / destructors>
public:
    SoundManager() = default;
    virtual ~SoundManager() = default;
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    static void getDefaultAudioDeviceInfo();
    static void getAudioDevicesInfo();
    /// endregion </Methods>
};

#endif // SOUNDMANAGER_H
