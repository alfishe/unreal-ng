#pragma once

class Emulator;
class EmulatorContext;
class LogViewer;

class GUIEmulatorContext
{
public:
    GUIEmulatorContext();

public:
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;

    LogViewer* logViewer = nullptr;
};
