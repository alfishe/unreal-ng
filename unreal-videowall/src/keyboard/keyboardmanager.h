#pragma once

#ifndef KEYBOARDMANAGER_H
#define KEYBOARDMANAGER_H

#include <common/collectionhelper.h>
#include <emulator/io/keyboard/keyboard.h>

#include <Qt>
#include <QtGlobal>

class KeyboardManager
{
public:
    KeyboardManager();
    ~KeyboardManager();

public:
    static quint8 mapQtKeyToEmulatorKey(int qtKey);
    static quint8 mapQtKeyToEmulatorKeyWithModifiers(int qtKey, Qt::KeyboardModifiers modifiers);

protected:
    static std::map<quint32, ZXKeysEnum> _keyMap;
};

#endif  // KEYBOARDMANAGER_H
