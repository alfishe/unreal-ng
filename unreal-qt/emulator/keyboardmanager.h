#pragma once

#ifndef KEYBOARDMANAGER_H
#define KEYBOARDMANAGER_H

#include <Qt>
#include <QtGlobal>

#include "common/collectionhelper.h"
#include "emulator/io/keyboard/keyboard.h"

class KeyboardManager
{
public:
    KeyboardManager();
    ~KeyboardManager();

public:
    static quint8 mapQtKeyToEmulatorKey(int qtKey);

protected:
    static std::map<quint32, ZXKeysEnum> _keyMap;
};

#endif // KEYBOARDMANAGER_H
