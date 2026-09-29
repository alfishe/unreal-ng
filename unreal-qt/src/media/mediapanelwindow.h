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

#include <map>
#include <string>
#include <vector>

#include "media/core/mediapanelmodel.h"

class EmulatorBinding;
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

    /// Connect to the central binding (mirrors TapeManagerWindow::setBinding)
    void setBinding(EmulatorBinding* binding);

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
    void onFilesDropped(int row, const QStringList& paths);

private:
    void buildUi();
    void rebuildTable();
    void updateButtons();
    const MediaPanelRow* selectedRow() const;

    /// Run one verb; a refused request because of unsaved writes asks
    /// Save / Export / Discard and runs again with the answer
    StateNode run(const std::string& verb, const std::string& slot, const std::string& path,
                  std::map<std::string, std::string> options, bool askDisposition = true);
    void report(const StateNode& reply, const QString& what);
    void insertInto(const std::string& slot, const QString& path);

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
    QTimer* _timer = nullptr;

    std::vector<MediaPanelRow> _rows;
    uint64_t _revision = UINT64_MAX;
    QString _lastDirectory;
};
