#pragma once

#include <QObject>
#include <QKeyEvent>
#include <QSet>
#include <memory>
#include <string>

#ifdef HAS_EMULATOR_CORE
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "3rdparty/message-center/messagecenter.h"
class Emulator;
#endif

class EmulatorWidget : public QObject
#ifdef HAS_EMULATOR_CORE
    , public Observer
#endif
{
    Q_OBJECT

public:
    explicit EmulatorWidget(QObject* parent = nullptr);
    ~EmulatorWidget() override;

    bool start();
    void stop();
    void pause();
    void resume();
    void reset();

    bool isRunning() const;
    bool isPaused() const;

    bool loadFile(const QString& path);

    void handleKeyPress(QKeyEvent* event);
    void handleKeyRelease(QKeyEvent* event);

    // Release every ZX key currently held in the emulator matrix. Call at the
    // start of fullscreen transitions: a modifier press (e.g. Cmd -> SYM_SHIFT)
    // from the toggle shortcut otherwise loses its release during the
    // transition and stays stuck in the matrix — "dead" keyboard until reset.
    void releaseAllKeys();

    void* framebuffer() const { return m_framebuffer; }
    int framebufferWidth() const { return m_width; }
    int framebufferHeight() const { return m_height; }

signals:
    void frameReady();
    void stateChanged();
    void audioReady(const int16_t* samples, size_t count);
    void resolutionChanged(int width, int height);

#ifdef HAS_EMULATOR_CORE
public:
    std::shared_ptr<Emulator> emulator() const { return m_emulator; }

private:
    void handleVideoFrameRefresh(int id, Message* message);
    void handleResolutionChanged(int id, Message* message);
    void handleEmulatorStateChanged(int id, Message* message);
    void subscribeToMessages();
    void unsubscribeFromMessages();

private:
    EmulatorManager* m_emulatorManager = nullptr;
    std::shared_ptr<Emulator> m_emulator;
    std::string m_emulatorId;
#endif

private:
    void* m_framebuffer = nullptr;
    int m_width = 352;
    int m_height = 288;
    bool m_subscribed = false;
    QSet<quint8> m_pressedKeys;  // ZX keys currently pressed (for releaseAllKeys)
};
