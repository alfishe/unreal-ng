/**
 * @file mediapanelwindow.h
 * @brief MediaPanelWindow — every media slot of the machine in one table
 *        (floppy drives, SD cards, later tape / IDE / CD): insert a file or a
 *        folder, eject, save, export, discard, protect, create a blank medium.
 *
 * A thin adapter over MediaControl (core/src/emulator/media/mediacontrol.h),
 * like the WebAPI, CLI, MCP, Lua and Python: the same verbs, selectors and
 * errors. Requests from the GUI are async (the UI never waits for a swap
 * delay); the table follows the manager's revision counter.
 * Design: docs/inprogress/2026-09-28-storage-manager/media-control-design.md §3.9
 */

#pragma once

#include <QStringList>
#include <QTableWidget>
#include <QWidget>

#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "media/core/flattenchoice.h"
#include "media/core/mediapanelmodel.h"
#include "emulator/state/statenode.h"

class EmulatorBinding;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

/// The slot table; files dropped on a row go into that slot
class MediaSlotTable : public QTableWidget
{
    Q_OBJECT

public:
    explicit MediaSlotTable(QWidget* parent = nullptr);

signals:
    void filesDropped(int row, const QStringList& paths);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

class MediaPanelWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MediaPanelWindow(QWidget* parent = nullptr);
    ~MediaPanelWindow() override;

    /// Connect to the central binding (mirrors TapeManagerWindow::setBinding)
    void setBinding(EmulatorBinding* binding);

    /// A folder into `slot` off the UI thread (BUGS.md #3), for the main window's drop and
    /// File > Open too: the same worker, "pending" row and completion refresh as Insert Folder.
    /// A FAT volume, a TR-DOS disk, a tape or, in a CD drive, an audio CD of MP3 / FLAC / WAV.
    /// False (with `reason`) while another folder is still being built
    bool insertFolder(const std::string& slot, const QString& path, QString* reason = nullptr);

signals:
    /// Visibility changed via the window's own close box (keeps the menu in sync)
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void refresh();
    void onInsertFile();
    void onInsertFolder();
    void onEject();
    void onSave();
    void onExport();
    void onDiscard();
    void onProtect();
    void onCreate();
    void onCompactFlash();
    void onLayers();
    void onFilesDropped(int row, const QStringList& paths);

private:
    void buildUi();
    void rebuildTable();
    void updateButtons();
    const MediaPanelRow* selectedRow() const;

    /// A composite's unsaved writes through the strategy dialog (DT-9); false when cancelled or refused
    bool saveComposite(const std::string& slot);
    /// Run one verb; a refused request because of unsaved writes asks
    /// Save / Export / Discard and runs again with the answer
    StateNode run(const std::string& verb, const std::string& slot, const std::string& path,
                  std::map<std::string, std::string> options, bool askDisposition = true);
    void report(const StateNode& reply, const QString& what);
    void insertInto(const std::string& slot, const QString& path);

    /// BUGS.md #3: a folder insert's scan + FAT/TRD volume build can take
    /// seconds on a large or slow/network folder - run it on a worker thread
    /// so the UI stays responsive, instead of blocking inside run()
    void insertFolderAsync(const std::string& slot, const QString& path, std::map<std::string, std::string> options);
    void onInsertFolderFinished(const StateNode& reply);
    void checkScanWatchdog();
    void cancelInsertWorker();  // best-effort: sets the flag, does not join

    EmulatorBinding* _binding = nullptr;
    MediaSlotTable* _table = nullptr;
    QPushButton* _insertFile = nullptr;
    QPushButton* _insertFolder = nullptr;
    QPushButton* _eject = nullptr;
    QPushButton* _save = nullptr;
    QPushButton* _export = nullptr;
    QPushButton* _discard = nullptr;
    QPushButton* _protect = nullptr;
    QPushButton* _create = nullptr;
    QPushButton* _compactFlash = nullptr;
    QPushButton* _layers = nullptr;  ///< a composite's layers and the guest's changes
    std::map<std::string, bool> _cfChoice;  ///< empty IDE units: whether the next disk image is a CompactFlash card
    bool WantsCompactFlash(const MediaPanelRow& row) const;
    QTimer* _timer = nullptr;

    /// Async folder insert (BUGS.md #3): one at a time, guarded by
    /// _insertWorker.joinable(). The worker never touches `this` directly -
    /// it builds a StateNode reply on its own thread and marshals it back via
    /// QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection); the
    /// destructor cancels and joins so that queued call can never fire after
    /// this object is gone.
    QLabel* _scanStatus = nullptr;
    QProgressBar* _scanProgress = nullptr;
    QTimer* _scanWatchdog = nullptr;
    std::thread _insertWorker;
    std::atomic<bool> _insertCancelRequested{false};
    std::atomic<uint64_t> _insertEntriesScanned{0};
    std::atomic<uint64_t> _insertBytesScanned{0};
    uint64_t _insertLastSeenEntries = 0;
    int _insertStalledTicks = 0;
    int _insertStallTimeoutSeconds = 30;  ///< "configurable" per BUGS.md #3; a test can lower it
    /// The slot an async insert is scanning for, "" when none - touched only
    /// on the UI thread (set in insertFolderAsync, cleared in
    /// onInsertFolderFinished), used by rebuildTable() to highlight that row
    /// so it is obvious which mount point a running scan belongs to
    std::string _insertSlot;
    QString _insertPath;

    std::vector<MediaPanelRow> _rows;
    uint64_t _revision = UINT64_MAX;
    QString _lastDirectory;
};
