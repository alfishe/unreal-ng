#ifndef GUIEMULATORCONTEXT_H
#define GUIEMULATORCONTEXT_H

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

#endif // GUIEMULATORCONTEXT_H
