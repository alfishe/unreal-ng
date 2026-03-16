#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QHash>
#include <QTimer>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QShortcut>
#include <QSocketNotifier>
#include <QSet>
#include <QVector>

#include <cstdint>

/// Stored log message for backing model
struct LogMessage
{
    uint8_t  level;
    uint8_t  moduleId;       // PlatformModulesEnum (1-12), 0 = emulog
    uint16_t submoduleMask;
    uint64_t timestamp;
    QString  text;
    uint32_t categoryId;     // raw category for display
};

namespace emu { struct SPSCRingBuffer; }
namespace monitoring { struct ManifestHeader; struct ControlBlock; struct SectionDescriptor; }

class LogViewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LogViewWidget(QWidget* parent = nullptr);
    ~LogViewWidget() override;

    bool isAttached() const { return _isAttached; }
    uint64_t messageCount() const { return _messageCount; }
    uint64_t droppedCount() const;
    QString shmName() const { return _shmName; }
    bool autoScroll() const { return _autoScroll; }
    bool isFifoActive() const { return _fifoNotifier != nullptr; }

    // SHM sharing — other widgets can access the mapped region
    void* shmBase() const { return _shmPtr; }
    size_t shmSize() const { return _shmSize; }

public slots:
    void attachToShm(const QString& emulatorId, const QString& shmName);
    void detachFromShm();
    void clearLog();
    void setAutoScroll(bool on);
    void setMaxMessages(int count);

    // Control commands sent to emulator via CONTROL_RING
    void enableCategory(const QString& pattern);
    void disableCategory(const QString& pattern);
    void setLogLevel(const QString& pattern, uint8_t level);
    void requestCategoryList();

    // ModuleLogger control commands
    void enableModule(uint8_t module, uint16_t submodule);
    void disableModule(uint8_t module, uint16_t submodule);
    void setModuleLogLevel(uint8_t moduleId, uint8_t level);  // moduleId=0 for global
    void requestModuleList();

    // Display filtering (multi-module visibility)
    void setModuleFilter(int moduleId, int submoduleId = -1);  // legacy single-module
    void setModuleVisible(int moduleId, bool visible);          // toggle module in display
    void setSubmoduleVisibility(int moduleId, uint16_t mask);   // set submodule bitmask
    void setAllModulesVisible();                                // show all
    void repopulateDisplay();                                   // re-render from backing store

signals:
    void attached(const QString& shmName);
    void detached();
    void statsUpdated(uint64_t msgCount, uint64_t dropped);
    void categoriesReceived(const QString& responseData);
    void modulesReceived(const QByteArray& binaryState);
    void shmAttached(void* base, size_t size);
    void shmDetached();
    void frameReady();
    void moduleActivity(int moduleId, int submoduleId);  // emitted per received message
    void loggerStateReceived(uint32_t moduleMask, QVector<uint16_t> submoduleMasks,
                             QVector<uint8_t> moduleLevels, uint8_t globalLevel);

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onAttachRetry();
    void onDrainTimer();
    void onFindNext();
    void onFindPrev();
    void showFindBar();
    void hideFindBar();
    void onFifoReadable();

private:
    bool tryAttach();
    bool findSections();
    void storeAndAppend(uint8_t level, uint32_t categoryId,
                        uint64_t timestamp, const char* text, uint32_t len);
    void renderMessage(const LogMessage& msg);
    bool isMessageVisible(const LogMessage& msg) const;
    bool sendControlCommand(uint8_t type, const char* pattern,
                            uint8_t logLevel = 0, uint8_t sectionIndex = 0);
    void updateThemeColors();
    void updateFindHighlights();
    void openFifoNotifier();
    void closeFifoNotifier();
    static QString deriveFifoPath(const QString& shmName);

    // SHM state
    void* _shmPtr = nullptr;
    size_t _shmSize = 0;
    int _shmFd = -1;
    QString _shmName;
    QString _emulatorId;
    bool _isAttached = false;

    // Manifest pointers (into SHM)
    monitoring::ManifestHeader* _manifest = nullptr;
    emu::SPSCRingBuffer* _ringBuffer = nullptr;
    monitoring::ControlBlock* _controlBlock = nullptr;
    const monitoring::SectionDescriptor* _loggerStateDesc = nullptr;
    uint64_t _lastLoggerEpoch = 0;

    // Timers
    QTimer* _attachTimer = nullptr;
    QTimer* _drainTimer = nullptr;
    int _attachRetries = 0;

    // UI — log display
    QPlainTextEdit* _textEdit = nullptr;
    bool _autoScroll = true;
    bool _isDarkTheme = false;

    // UI — find bar
    QWidget* _findBar = nullptr;
    QLineEdit* _findEdit = nullptr;
    QLabel* _findCountLabel = nullptr;

    // FIFO frame notification
    int _fifoFd = -1;
    QSocketNotifier* _fifoNotifier = nullptr;

    // Message backing store
    QVector<LogMessage> _messages;
    QSet<int> _visibleModules;  // empty = show all; non-empty = only listed module IDs
    QHash<int, uint16_t> _submoduleVisibility;  // moduleId → visible submodule bitmask (absent = all visible)

    // Text filter (from find bar)
    QString _filterText;
    bool _autoScrollBeforeFilter = true;
    int _filterMatchCount = 0;

    // Stats
    uint64_t _messageCount = 0;
    uint64_t _refTimestamp = 0;
    uint64_t _lastCategoryEpoch = 0;
    bool _waitingForModules = false;

    static constexpr int ATTACH_RETRY_MS = 100;
    static constexpr int ATTACH_MAX_RETRIES = 50;
    static constexpr int DRAIN_INTERVAL_MS = 10;
    static constexpr int DRAIN_BATCH_SIZE = 200;
    static constexpr int MAX_MESSAGE_COUNT = 50000;
    static constexpr int MAX_BLOCK_COUNT = 10000;
};
