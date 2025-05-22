#pragma once
#ifndef BREAKPOINTDIALOG_H
#define BREAKPOINTDIALOG_H

#include <QDialog>
#include <QTableWidget>
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

class Emulator;
class Message;

class BreakpointDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BreakpointDialog(Emulator* emulator, QWidget* parent = nullptr);
    ~BreakpointDialog();

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
    void onBreakpointDoubleClicked(QTableWidgetItem* item);
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
    
    Emulator* _emulator;
    std::function<void(int, Message*)> m_breakpointCallback;  // Store the observer callback for cleanup
    QMutex m_mutex;  // For thread-safe access to the dialog
    QVBoxLayout* _mainLayout = nullptr;
    QTableWidget* _breakpointTable = nullptr;
    
    // Filter UI elements
    QComboBox* _groupFilter = nullptr;
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
