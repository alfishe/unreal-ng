#pragma once

#include <memory>
#ifndef BREAKPOINTDIALOG_H
#define BREAKPOINTDIALOG_H

#include <QDialog>
#include <QHash>
#include <QSet>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QShortcut>
#include <QSettings>
#include <QMenu>
#include <QHeaderView>
#include <QBoxLayout>
#include <QMessageBox>
#include <QMutex>
#include <QMetaObject>
#include <QMetaMethod>
#include <QApplication>
#include <QThread>
#include "emulator/emulator.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/targets/debugcpuid.h"

#include <vector>

class Emulator;
struct Message;

class BreakpointDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BreakpointDialog(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);
    ~BreakpointDialog();

    /// The hits shown under an expanded breakpoint that only counts: at most this many of its record (the first ones
    /// of a record of the first N, the newest of a record of the newest)
    static constexpr int kShownHits = 200;
    /// How often the expanded records are read (a read never parks the emulation: BreakpointManager::GetHitLog)
    static constexpr int kHitPollMs = 250;

    /// Reads the expanded breakpoints' records and updates their rows (the poll timer's tick); a record whose counts
    /// did not move is not read again unless `force`
    void refreshHitLogs(bool force = false);

private slots:
    // Table actions
    void addBreakpoint();
    void editBreakpoint();
    void deleteBreakpoint();
    void toggleBreakpoint();
    
    // Group management
    void createGroup();
    void manageGroups();
    void filterByGroup(const QString& group);
    
    // Table events
    void onBreakpointSelectionChanged();
    void onBreakpointDoubleClicked(QTreeWidgetItem* item, int column);
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemCollapsed(QTreeWidgetItem* item);
    void showContextMenu(const QPoint& pos);
    
    // Dialog events
    void refreshBreakpointList();
    void applyFilters();
    void clearFilters();
    void applyChanges();

private:
    void setupUI();
    void setupFilterUI();
    void setupGroupFilter();
    void setupTableContextMenu();
    void setupShortcuts();
    void populateBreakpointTable();
    void populateGroupComboBox();
    void updateButtonStates();
    void updateStatusBar();
    /// The selected breakpoint's row (a hit row selects its breakpoint), nullptr when none
    QTreeWidgetItem* selectedBreakpointItem() const;
    static uint16_t breakpointIdOf(const QTreeWidgetItem* item);
    /// Whose breakpoint a row is (the CPU column's data)
    static DebugCpuId cpuOf(const QTreeWidgetItem* item);
    /// A breakpoint's key in the list: its CPU and its id (two CPUs' managers give the same ids)
    static uint32_t keyOf(DebugCpuId cpu, uint16_t id) { return static_cast<uint32_t>(cpu) << 16 | id; }
    static uint32_t keyOf(const QTreeWidgetItem* item) { return keyOf(cpuOf(item), breakpointIdOf(item)); }
    /// The CPUs whose breakpoints are listed: the main CPU, then each card CPU fitted
    std::vector<DebugCpuId> listedCpus() const;
    BreakpointManager* managerOf(DebugCpuId cpu) const;
    /// Enables or disables the selected breakpoint
    void setSelectedActive(bool active);
    /// Sets the Status column of a breakpoint row from its state
    static void setStatus(QTreeWidgetItem* item, bool active, bool countOnly, bool conditionFailed,
                          const QString& conditionError);

    /// The expanded row under a breakpoint that only counts: its record's summary and hits
    struct HitView
    {
        QTreeWidgetItem* row = nullptr;   ///< the child row holding the widget
        QLabel* summary = nullptr;
        QTableWidget* table = nullptr;
        uint64_t lastHitCount = UINT64_MAX;  ///< the counts the rows were made from (a record only moves with them)
        uint64_t lastTotal = UINT64_MAX;
        bool sized = false;                  ///< the columns fitted to the first hits
    };
    void refreshHitView(uint32_t key, HitView& view, bool force);
    
    std::shared_ptr<Emulator> _emulator;
    uint64_t m_breakpointObserverId = 0;  // Store the observer ID for cleanup
    QMutex m_mutex;  // For thread-safe access to the dialog
    QVBoxLayout* _mainLayout = nullptr;
    QTreeWidget* _breakpointTable = nullptr;
    QTimer* _hitTimer = nullptr;            ///< runs while a record is expanded
    QSet<uint32_t> _expandedIds;            ///< kept across a rebuild of the list (keyOf)
    QHash<uint32_t, HitView> _hitViews;     ///< by keyOf
    
    // Filter UI elements
    QComboBox* _groupFilter = nullptr;
    QComboBox* _cpuFilter = nullptr;        ///< a card fitted: all CPUs or one (item data: the DebugCpuId, -1 all)
    QLineEdit* _searchField = nullptr;
    QPushButton* _clearSearchButton = nullptr;
    
    // Action buttons
    QPushButton* _addButton = nullptr;
    QPushButton* _editButton = nullptr;
    QPushButton* _deleteButton = nullptr;
    QPushButton* _enableButton = nullptr;
    QPushButton* _disableButton = nullptr;
    QPushButton* _groupButton = nullptr;
    
    // Bottom dialog elements
    QLabel* _statusLabel = nullptr;
    QPushButton* _closeButton = nullptr;
    QPushButton* _applyButton = nullptr;
};

#endif // BREAKPOINTDIALOG_H
