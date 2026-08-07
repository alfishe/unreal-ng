#pragma once

#include <QObject>
#include <QKeyEvent>
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

    void* framebuffer() const { return m_framebuffer; }
    int framebufferWidth() const { return m_width; }
    int framebufferHeight() const { return m_height; }

signals:
    void frameReady();
    void stateChanged();
    void audioReady(const int16_t* samples, size_t count);

#ifdef HAS_EMULATOR_CORE
public:
    std::shared_ptr<Emulator> emulator() const { return m_emulator; }

private:
    void handleVideoFrameRefresh(int id, Message* message);
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
};
