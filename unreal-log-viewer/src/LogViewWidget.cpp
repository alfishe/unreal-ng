#include "LogViewWidget.h"

#include <QScrollBar>
#include <QTextCharFormat>
#include <QFont>
#include <QDebug>
#include <QEvent>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QShortcut>
#include <QKeySequence>
#include <QApplication>

#include <chrono>
#include <cstring>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "emulator/monitoring/manifest.h"
#include "common/spsc_ringbuffer.h"

using namespace monitoring;

// =============================================================================
// Level colors — dark and light theme variants
// =============================================================================

struct LevelColors
{
    QColor dark;
    QColor light;
};

static const LevelColors kLevelColors[] = {
    { QColor(128, 128, 128), QColor(120, 120, 120) },  // TRACE — gray
    { QColor(0, 188, 212),   QColor(0, 131, 143) },    // DEBUG — cyan
    { QColor(76, 175, 80),   QColor(27, 94, 32) },     // INFO  — green
    { QColor(255, 193, 7),   QColor(230, 150, 0) },    // WARN  — amber
    { QColor(244, 67, 54),   QColor(198, 40, 40) },    // ERROR — red
};

static QColor levelColor(uint8_t level, bool isDark)
{
    if (level > 4) level = 4;
    return isDark ? kLevelColors[level].dark : kLevelColors[level].light;
}

static const char* levelName(uint8_t level)
{
    switch (level)
    {
        case 0: return "TRACE";
        case 1: return "DEBUG";
        case 2: return "INFO ";
        case 3: return "WARN ";
        case 4: return "ERROR";
        default: return "?????";
    }
}

// Module names matching PlatformModulesEnum (0-12)
static const char* kModuleNames[] = {
    "???", "Core", "Z80", "Mem", "IO", "Disk",
    "Video", "Sound", "DMA", "Loader", "Debug", "Disasm", "Rec"
};
static constexpr int kModuleNameCount = int(sizeof(kModuleNames) / sizeof(kModuleNames[0]));

/// Decode category_id: if high 16 bits ≤ 12 it's a ModuleLogger message,
/// otherwise it's an emulog FNV-1a hash.
static QString decodeCategoryId(uint32_t categoryId)
{
    uint16_t moduleId = categoryId >> 16;
    if (moduleId > 0 && moduleId < kModuleNameCount)
    {
        return QString(kModuleNames[moduleId]);
    }
    // emulog FNV-1a hash — display as hex
    return QString("%1").arg(categoryId, 8, 16, QChar('0'));
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

LogViewWidget::LogViewWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Local log controls bar
    auto* controlBar = new QWidget(this);
    auto* controlLayout = new QHBoxLayout(controlBar);
    controlLayout->setContentsMargins(2, 1, 2, 1);
    controlLayout->setSpacing(4);

    auto* clearBtn = new QPushButton("Clear", controlBar);
    connect(clearBtn, &QPushButton::clicked, this, &LogViewWidget::clearLog);
    controlLayout->addWidget(clearBtn);

    auto* autoScrollCheck = new QCheckBox("Auto-scroll", controlBar);
    autoScrollCheck->setChecked(true);
    connect(autoScrollCheck, &QCheckBox::toggled, this, &LogViewWidget::setAutoScroll);
    controlLayout->addWidget(autoScrollCheck);

    controlLayout->addStretch();
    layout->addWidget(controlBar);

    _textEdit = new QPlainTextEdit(this);
    _textEdit->setReadOnly(true);
    _textEdit->setMaximumBlockCount(MAX_BLOCK_COUNT);
    _textEdit->setWordWrapMode(QTextOption::NoWrap);

    QFont mono("Menlo");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(11);
    _textEdit->setFont(mono);

    layout->addWidget(_textEdit, 1);

    // Find bar (hidden by default)
    _findBar = new QWidget(this);
    auto* findLayout = new QHBoxLayout(_findBar);
    findLayout->setContentsMargins(4, 2, 4, 2);
    findLayout->setSpacing(4);

    _findEdit = new QLineEdit(_findBar);
    _findEdit->setPlaceholderText("Search...");
    _findEdit->setClearButtonEnabled(true);
    findLayout->addWidget(_findEdit, 1);

    auto* prevBtn = new QPushButton("<", _findBar);
    prevBtn->setFixedWidth(30);
    prevBtn->setToolTip("Previous (Shift+Enter)");
    connect(prevBtn, &QPushButton::clicked, this, &LogViewWidget::onFindPrev);
    findLayout->addWidget(prevBtn);

    auto* nextBtn = new QPushButton(">", _findBar);
    nextBtn->setFixedWidth(30);
    nextBtn->setToolTip("Next (Enter)");
    connect(nextBtn, &QPushButton::clicked, this, &LogViewWidget::onFindNext);
    findLayout->addWidget(nextBtn);

    _findCountLabel = new QLabel("", _findBar);
    _findCountLabel->setMinimumWidth(60);
    findLayout->addWidget(_findCountLabel);

    auto* closeBtn = new QPushButton("x", _findBar);
    closeBtn->setFixedWidth(24);
    connect(closeBtn, &QPushButton::clicked, this, &LogViewWidget::hideFindBar);
    findLayout->addWidget(closeBtn);

    _findBar->hide();
    layout->addWidget(_findBar);

    // Keyboard shortcuts
    connect(_findEdit, &QLineEdit::returnPressed, this, &LogViewWidget::onFindNext);
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, &LogViewWidget::showFindBar);
    auto* escShortcut = new QShortcut(Qt::Key_Escape, _findBar);
    connect(escShortcut, &QShortcut::activated, this, &LogViewWidget::hideFindBar);
    connect(_findEdit, &QLineEdit::textChanged, this, &LogViewWidget::updateFindHighlights);

    // Detect theme and apply colors
    updateThemeColors();

    // Reference timestamp for relative display
    auto now = std::chrono::steady_clock::now();
    _refTimestamp = static_cast<uint64_t>(now.time_since_epoch().count());

    // Attach retry timer (single-shot driven)
    _attachTimer = new QTimer(this);
    _attachTimer->setSingleShot(true);
    connect(_attachTimer, &QTimer::timeout, this, &LogViewWidget::onAttachRetry);

    // Drain timer
    _drainTimer = new QTimer(this);
    connect(_drainTimer, &QTimer::timeout, this, &LogViewWidget::onDrainTimer);
}

LogViewWidget::~LogViewWidget()
{
    detachFromShm();
}

// =============================================================================
// Theme detection
// =============================================================================

void LogViewWidget::updateThemeColors()
{
    QPalette pal = QApplication::palette();
    QColor bg = pal.color(QPalette::Base);
    // Dark if background luminance < 128
    _isDarkTheme = (bg.red() * 299 + bg.green() * 587 + bg.blue() * 114) / 1000 < 128;

    // Use system palette — no hardcoded colors
    _textEdit->setPalette(pal);
}

void LogViewWidget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange)
        updateThemeColors();
    QWidget::changeEvent(event);
}

// =============================================================================
// Find bar — live filter mode
// =============================================================================

void LogViewWidget::showFindBar()
{
    _autoScrollBeforeFilter = _autoScroll;
    _findBar->show();
    _findEdit->setFocus();
    _findEdit->selectAll();
}

void LogViewWidget::hideFindBar()
{
    _findBar->hide();
    _findCountLabel->clear();

    // Clear text filter and restore full display
    _filterText.clear();
    repopulateDisplay();

    // Restore auto-scroll state
    setAutoScroll(_autoScrollBeforeFilter);
    _textEdit->setFocus();
}

void LogViewWidget::onFindNext()
{
    // In filter mode, Enter applies the filter (same as text change)
    updateFindHighlights();
}

void LogViewWidget::onFindPrev()
{
    // In filter mode, scroll to top of filtered results
    QScrollBar* sb = _textEdit->verticalScrollBar();
    sb->setValue(sb->minimum());
}

void LogViewWidget::updateFindHighlights()
{
    QString text = _findEdit->text().trimmed();

    if (text != _filterText)
    {
        _filterText = text;

        // Pause auto-scroll while filter is active so results stay visible
        if (!_filterText.isEmpty())
            setAutoScroll(false);

        repopulateDisplay();

        // Count matching messages
        int count = 0;
        for (const auto& msg : _messages)
        {
            if (isMessageVisible(msg))
                count++;
        }
        _filterMatchCount = count;
        if (!_filterText.isEmpty())
            _findCountLabel->setText(QString("%1 found").arg(_filterMatchCount));
        else
            _findCountLabel->clear();
    }
}

// =============================================================================
// SHM attach / detach
// =============================================================================

void LogViewWidget::attachToShm(const QString& emulatorId, const QString& shmName)
{
    detachFromShm();

    _emulatorId = emulatorId;
    _shmName = shmName;
    _attachRetries = 0;

    if (tryAttach())
        return;

    _attachTimer->start(ATTACH_RETRY_MS);
}

void LogViewWidget::detachFromShm()
{
    _drainTimer->stop();
    _attachTimer->stop();
    closeFifoNotifier();

    _ringBuffer = nullptr;
    _controlBlock = nullptr;
    _manifest = nullptr;

#ifndef _WIN32
    if (_shmPtr && _shmPtr != MAP_FAILED)
    {
        munmap(_shmPtr, _shmSize);
    }
    if (_shmFd != -1)
    {
        ::close(_shmFd);
    }
#endif

    bool wasAttached = _isAttached;
    _shmPtr = nullptr;
    _shmSize = 0;
    _shmFd = -1;

    if (wasAttached)
    {
        _isAttached = false;
        emit shmDetached();
        emit detached();
    }
}

bool LogViewWidget::tryAttach()
{
#ifdef _WIN32
    return false;
#else
    QByteArray name = _shmName.toUtf8();
    int fd = shm_open(name.constData(), O_RDWR, 0);
    if (fd == -1)
        return false;

    struct stat st;
    if (fstat(fd, &st) == -1)
    {
        ::close(fd);
        return false;
    }

    size_t size = static_cast<size_t>(st.st_size);
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
    {
        ::close(fd);
        return false;
    }

    auto* manifest = static_cast<ManifestHeader*>(ptr);
    if (manifest->magic != MANIFEST_MAGIC)
    {
        munmap(ptr, size);
        ::close(fd);
        return false;
    }

    _shmPtr = ptr;
    _shmSize = size;
    _shmFd = fd;
    _manifest = manifest;

    if (!findSections())
    {
        munmap(ptr, size);
        ::close(fd);
        _shmPtr = nullptr;
        _shmSize = 0;
        _shmFd = -1;
        _manifest = nullptr;
        _loggerStateDesc = nullptr;
        _lastLoggerEpoch = 0;
        return false;
    }

    _isAttached = true;
    _messageCount = 0;
    _lastCategoryEpoch = 0;

    _drainTimer->start(DRAIN_INTERVAL_MS);

    // Try to set up FIFO notification for event-driven frame updates
    openFifoNotifier();

    emit attached(_shmName);
    emit shmAttached(_shmPtr, _shmSize);
    return true;
#endif
}

bool LogViewWidget::findSections()
{
    if (!_manifest)
        return false;

    _ringBuffer = nullptr;
    _controlBlock = nullptr;

    for (uint16_t i = 0; i < _manifest->section_count; i++)
    {
        auto* desc = getDescriptor(_shmPtr, _manifest, i);
        if (!desc)
            continue;

        if (desc->type == SectionType::LOG_STREAM)
        {
            auto* data = getSectionData(_shmPtr, desc);
            if (data)
                _ringBuffer = emu::SPSCRingBuffer::attach(data);
        }
        else if (desc->type == SectionType::CONTROL_RING)
        {
            auto* data = getSectionData(_shmPtr, desc);
            if (data)
                _controlBlock = static_cast<ControlBlock*>(data);
        }
        else if (desc->type == SectionType::LOGGER_STATE)
        {
            _loggerStateDesc = desc;
        }
    }

    return _ringBuffer != nullptr;
}

void LogViewWidget::onAttachRetry()
{
    _attachRetries++;

    if (tryAttach())
        return;

    if (_attachRetries < ATTACH_MAX_RETRIES)
        _attachTimer->start(ATTACH_RETRY_MS);
    else
        qWarning() << "LogViewWidget: Failed to attach to SHM after"
                    << ATTACH_MAX_RETRIES << "retries:" << _shmName;
}

// =============================================================================
// Drain timer — reads messages from SPSC ring buffer
// =============================================================================

void LogViewWidget::onDrainTimer()
{
    if (!_ringBuffer)
        return;

    emu::SPSCRingBuffer::MessageHeader hdr{};
    char payload[4096];

    int count = 0;
    while (count < DRAIN_BATCH_SIZE)
    {
        if (!_ringBuffer->try_read(hdr, payload, sizeof(payload) - 1))
            break;

        payload[hdr.length < sizeof(payload) ? hdr.length : sizeof(payload) - 1] = '\0';
        storeAndAppend(hdr.level, hdr.category_id, hdr.timestamp, payload, hdr.length);
        count++;
    }

    if (count > 0)
    {
        _messageCount += count;

        if (_autoScroll)
        {
            QScrollBar* sb = _textEdit->verticalScrollBar();
            sb->setValue(sb->maximum());
        }

        emit statsUpdated(_messageCount, droppedCount());
    }

    // Poll for category list response
    if (_controlBlock && _lastCategoryEpoch > 0)
    {
        uint64_t epoch = _controlBlock->response_epoch.load(std::memory_order_acquire);
        if (epoch > _lastCategoryEpoch)
        {
            _lastCategoryEpoch = 0;

            if (_waitingForModules)
            {
                // Binary module state blob (41 bytes: modules + submodules + globalLevel + moduleLevels)
                _waitingForModules = false;
                constexpr size_t kBlobSize = sizeof(uint32_t) + sizeof(uint16_t) * 12 + sizeof(uint8_t) + 12;
                QByteArray blob(_controlBlock->response_data, static_cast<int>(kBlobSize));
                emit modulesReceived(blob);
            }
            else
            {
                // Text category list
                QString data = QString::fromUtf8(_controlBlock->response_data);
                emit categoriesReceived(data);
            }
        }
    }

    // Read logger state from SHM (epoch-safe, only emit on change)
    if (_loggerStateDesc)
    {
        uint64_t epoch = _loggerStateDesc->epoch.load(std::memory_order_acquire);
        if (epoch != EPOCH_UPDATING && epoch != _lastLoggerEpoch && epoch > 0)
        {
            LoggerStateSnapshot snapshot;
            if (epochSafeRead(_loggerStateDesc, _shmPtr, &snapshot, sizeof(snapshot)))
            {
                _lastLoggerEpoch = epoch;

                QVector<uint16_t> subMasks(12);
                QVector<uint8_t> modLevels(12);
                for (int i = 0; i < 12; i++)
                {
                    subMasks[i] = snapshot.submodule_masks[i];
                    modLevels[i] = snapshot.module_levels[i];
                }
                emit loggerStateReceived(snapshot.module_enable_mask, subMasks,
                                          modLevels, snapshot.global_level);
            }
        }
    }
}

// =============================================================================
// Log message formatting
// =============================================================================

void LogViewWidget::storeAndAppend(uint8_t level, uint32_t categoryId,
                                    uint64_t timestamp, const char* text, uint32_t len)
{
    uint16_t msgModule = categoryId >> 16;
    uint16_t msgSubmodule = categoryId & 0xFFFF;

    // Always store in backing model
    LogMessage msg;
    msg.level = level;
    msg.moduleId = static_cast<uint8_t>(msgModule);
    msg.submoduleMask = msgSubmodule;
    msg.timestamp = timestamp;
    msg.text = QString::fromUtf8(text, static_cast<int>(len));
    msg.categoryId = categoryId;
    _messages.append(msg);

    // Cap backing store
    if (_messages.size() > MAX_MESSAGE_COUNT)
        _messages.remove(0, _messages.size() - MAX_MESSAGE_COUNT);

    // Emit activity signal for CategoryPanel (convert submodule bitmask to bit index)
    int subIndex = -1;
    if (msgSubmodule > 0)
    {
        for (int b = 0; b < 16; b++)
        {
            if (msgSubmodule & (1u << b)) { subIndex = b; break; }
        }
    }
    emit moduleActivity(static_cast<int>(msgModule), subIndex);

    // Only render if visible
    if (isMessageVisible(msg))
    {
        renderMessage(msg);

        // Update live filter count
        if (!_filterText.isEmpty())
        {
            _filterMatchCount++;
            _findCountLabel->setText(QString("%1 found").arg(_filterMatchCount));
        }
    }
}

bool LogViewWidget::isMessageVisible(const LogMessage& msg) const
{
    // Module-level gate
    if (!_visibleModules.isEmpty())
    {
        int modId = static_cast<int>(msg.moduleId);
        if (!_visibleModules.contains(modId))
            return false;
    }

    // Submodule-level gate
    if (!_submoduleVisibility.isEmpty())
    {
        int modId = static_cast<int>(msg.moduleId);
        auto it = _submoduleVisibility.find(modId);
        if (it != _submoduleVisibility.end())
        {
            if ((it.value() & msg.submoduleMask) == 0)
                return false;
        }
    }

    // Text filter gate
    if (!_filterText.isEmpty())
    {
        if (!msg.text.contains(_filterText, Qt::CaseInsensitive))
            return false;
    }

    return true;
}

void LogViewWidget::renderMessage(const LogMessage& msg)
{
    double relMs = static_cast<double>(msg.timestamp - _refTimestamp) / 1e6;
    if (relMs < 0) relMs = 0;

    QString catName = decodeCategoryId(msg.categoryId);

    QString line = QString("[%1] [%2] [%3] %4")
        .arg(relMs, 10, 'f', 3)
        .arg(levelName(msg.level))
        .arg(catName)
        .arg(msg.text);

    // Use QPlainTextEdit's native append mechanism — properly handles maximumBlockCount
    _textEdit->moveCursor(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(levelColor(msg.level, _isDarkTheme));
    QTextCursor cursor = _textEdit->textCursor();
    if (!_textEdit->document()->isEmpty())
        cursor.insertBlock();
    cursor.insertText(line, fmt);
}

// =============================================================================
// Public slots
// =============================================================================

void LogViewWidget::clearLog()
{
    _textEdit->clear();
    _messages.clear();
    _messageCount = 0;
    _submoduleVisibility.clear();
    emit statsUpdated(0, droppedCount());
}

void LogViewWidget::setAutoScroll(bool on)
{
    _autoScroll = on;
}

void LogViewWidget::setMaxMessages(int count)
{
    _textEdit->setMaximumBlockCount(count);
}

uint64_t LogViewWidget::droppedCount() const
{
    if (_ringBuffer)
        return _ringBuffer->droppedCount();
    return 0;
}

// =============================================================================
// Control commands
// =============================================================================

bool LogViewWidget::sendControlCommand(uint8_t type, const char* pattern,
                                        uint8_t logLevel, uint8_t sectionIndex)
{
    if (!_controlBlock)
        return false;

    uint32_t writeIdx = _controlBlock->write_idx.load(std::memory_order_relaxed);
    auto& cmd = _controlBlock->commands[writeIdx % CONTROL_CMD_SLOTS];

    cmd.type.store(type, std::memory_order_relaxed);
    cmd.section_index = sectionIndex;
    cmd.log_level = logLevel;
    cmd.reserved = 0;
    std::memset(cmd.pattern, 0, sizeof(cmd.pattern));
    if (pattern)
        std::strncpy(cmd.pattern, pattern, sizeof(cmd.pattern) - 1);
    cmd.processed.store(false, std::memory_order_relaxed);

    _controlBlock->write_idx.store(writeIdx + 1, std::memory_order_release);
    return true;
}

void LogViewWidget::enableCategory(const QString& pattern)
{
    QByteArray p = pattern.toUtf8();
    sendControlCommand(static_cast<uint8_t>(ControlCommandType::ENABLE_LOG_CATEGORY),
                       p.constData());
}

void LogViewWidget::disableCategory(const QString& pattern)
{
    QByteArray p = pattern.toUtf8();
    sendControlCommand(static_cast<uint8_t>(ControlCommandType::DISABLE_LOG_CATEGORY),
                       p.constData());
}

void LogViewWidget::setLogLevel(const QString& pattern, uint8_t level)
{
    QByteArray p = pattern.toUtf8();
    sendControlCommand(static_cast<uint8_t>(ControlCommandType::SET_LOG_LEVEL),
                       p.constData(), level);
}

void LogViewWidget::requestCategoryList()
{
    if (!_controlBlock)
        return;

    _lastCategoryEpoch = _controlBlock->response_epoch.load(std::memory_order_relaxed);
    sendControlCommand(static_cast<uint8_t>(ControlCommandType::LIST_CATEGORIES), nullptr);
}

// =============================================================================
// ModuleLogger control commands
// =============================================================================

void LogViewWidget::enableModule(uint8_t module, uint16_t submodule)
{
    if (!_controlBlock) return;

    uint32_t writeIdx = _controlBlock->write_idx.load(std::memory_order_relaxed);
    auto& cmd = _controlBlock->commands[writeIdx % CONTROL_CMD_SLOTS];

    cmd.type.store(static_cast<uint8_t>(ControlCommandType::SET_MODULE_ENABLED),
                   std::memory_order_relaxed);
    cmd.section_index = module;
    cmd.log_level = 0;
    cmd.reserved = 1;  // enable
    std::memset(cmd.pattern, 0, sizeof(cmd.pattern));
    std::memcpy(cmd.pattern, &submodule, sizeof(uint16_t));
    cmd.processed.store(false, std::memory_order_relaxed);

    _controlBlock->write_idx.store(writeIdx + 1, std::memory_order_release);
}

void LogViewWidget::disableModule(uint8_t module, uint16_t submodule)
{
    if (!_controlBlock) return;

    uint32_t writeIdx = _controlBlock->write_idx.load(std::memory_order_relaxed);
    auto& cmd = _controlBlock->commands[writeIdx % CONTROL_CMD_SLOTS];

    cmd.type.store(static_cast<uint8_t>(ControlCommandType::SET_MODULE_ENABLED),
                   std::memory_order_relaxed);
    cmd.section_index = module;
    cmd.log_level = 0;
    cmd.reserved = 0;  // disable
    std::memset(cmd.pattern, 0, sizeof(cmd.pattern));
    std::memcpy(cmd.pattern, &submodule, sizeof(uint16_t));
    cmd.processed.store(false, std::memory_order_relaxed);

    _controlBlock->write_idx.store(writeIdx + 1, std::memory_order_release);
}

void LogViewWidget::setModuleLogLevel(uint8_t moduleId, uint8_t level)
{
    if (!_controlBlock) return;

    uint32_t writeIdx = _controlBlock->write_idx.load(std::memory_order_relaxed);
    auto& cmd = _controlBlock->commands[writeIdx % CONTROL_CMD_SLOTS];

    cmd.type.store(static_cast<uint8_t>(ControlCommandType::SET_MODULE_LOG_LEVEL),
                   std::memory_order_relaxed);
    cmd.section_index = moduleId;  // 0 = global, 1-12 = per-module
    cmd.log_level = level;
    cmd.reserved = 0;
    std::memset(cmd.pattern, 0, sizeof(cmd.pattern));
    cmd.processed.store(false, std::memory_order_relaxed);

    _controlBlock->write_idx.store(writeIdx + 1, std::memory_order_release);
}

void LogViewWidget::requestModuleList()
{
    if (!_controlBlock) return;

    _lastCategoryEpoch = _controlBlock->response_epoch.load(std::memory_order_relaxed);
    _waitingForModules = true;
    sendControlCommand(static_cast<uint8_t>(ControlCommandType::LIST_MODULES), nullptr);
}

// =============================================================================
void LogViewWidget::setModuleFilter(int moduleId, int /*submoduleId*/)
{
    // Legacy single-module filter: set only that module visible
    _visibleModules.clear();
    if (moduleId > 0)
        _visibleModules.insert(moduleId);
    repopulateDisplay();
}

void LogViewWidget::setModuleVisible(int moduleId, bool visible)
{
    if (visible)
    {
        // If already in show-all mode (empty set), module is already visible — nothing to do
        if (!_visibleModules.isEmpty())
        {
            _visibleModules.insert(moduleId);

            // If all module IDs (0-12) are now visible, revert to show-all mode
            bool allPresent = true;
            for (int i = 0; i <= 12; i++)
            {
                if (!_visibleModules.contains(i)) { allPresent = false; break; }
            }
            if (allPresent)
                _visibleModules.clear();  // empty = show all
        }
    }
    else
    {
        // If empty (show-all mode), populate with all module IDs first
        if (_visibleModules.isEmpty())
        {
            for (int i = 0; i <= 12; i++)  // 0=emulog, 1-12=modules
                _visibleModules.insert(i);
        }
        _visibleModules.remove(moduleId);
    }
    repopulateDisplay();
}

void LogViewWidget::setSubmoduleVisibility(int moduleId, uint16_t mask)
{
    if (mask == 0xFFFF || mask == 0)
        _submoduleVisibility.remove(moduleId);  // all visible or none → module-level handles it
    else
        _submoduleVisibility[moduleId] = mask;
    repopulateDisplay();
}

void LogViewWidget::setAllModulesVisible()
{
    _visibleModules.clear();
    _submoduleVisibility.clear();
    repopulateDisplay();
}

void LogViewWidget::repopulateDisplay()
{
    _textEdit->clear();
    _textEdit->setUpdatesEnabled(false);

    for (const auto& msg : _messages)
    {
        if (isMessageVisible(msg))
            renderMessage(msg);
    }

    _textEdit->setUpdatesEnabled(true);

    if (_autoScroll)
    {
        QScrollBar* sb = _textEdit->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

// =============================================================================
// FIFO frame notification (macOS/Linux)
// =============================================================================

QString LogViewWidget::deriveFifoPath(const QString& shmName)
{
    const QString prefix = "/unreal_monitor_";
    if (shmName.startsWith(prefix))
    {
        QString instanceId = shmName.mid(prefix.length());
        return "/tmp/unreal_frame_" + instanceId;
    }
    return QString();
}

void LogViewWidget::openFifoNotifier()
{
#ifndef _WIN32
    closeFifoNotifier();

    QString fifoPath = deriveFifoPath(_shmName);
    if (fifoPath.isEmpty())
        return;

    QByteArray pathBytes = fifoPath.toUtf8();
    _fifoFd = ::open(pathBytes.constData(), O_RDONLY | O_NONBLOCK);
    if (_fifoFd == -1)
    {
        qDebug() << "LogViewWidget: Could not open FIFO" << fifoPath
                 << "- falling back to timer-based polling";
        return;
    }

    _fifoNotifier = new QSocketNotifier(_fifoFd, QSocketNotifier::Read, this);
    connect(_fifoNotifier, &QSocketNotifier::activated,
            this, &LogViewWidget::onFifoReadable);

    qDebug() << "LogViewWidget: FIFO notification active on" << fifoPath;
#endif
}

void LogViewWidget::closeFifoNotifier()
{
#ifndef _WIN32
    if (_fifoNotifier)
    {
        _fifoNotifier->setEnabled(false);
        delete _fifoNotifier;
        _fifoNotifier = nullptr;
    }
    if (_fifoFd != -1)
    {
        ::close(_fifoFd);
        _fifoFd = -1;
    }
#endif
}

void LogViewWidget::onFifoReadable()
{
#ifndef _WIN32
    if (_fifoFd == -1)
        return;

    // Drain all pending bytes (coalesce multiple frame signals)
    char buf[64];
    ssize_t n;
    bool gotData = false;
    while ((n = ::read(_fifoFd, buf, sizeof(buf))) > 0)
        gotData = true;

    if (n == 0)
    {
        // EOF: writer (emulator) disconnected
        closeFifoNotifier();
        return;
    }

    if (gotData)
        emit frameReady();
#endif
}
