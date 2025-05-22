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
#include "emulator/emulator.h"
#include "debugger/breakpoints/breakpointmanager.h"

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
    QVBoxLayout* _mainLayout;
    QTableWidget* _breakpointTable;
    
    // Filter UI elements
    QComboBox* _groupFilter;
    QLineEdit* _searchField;
    QPushButton* _clearSearchButton;
    
    // Action buttons
    QPushButton* _addButton;
    QPushButton* _editButton;
    QPushButton* _deleteButton;
    QPushButton* _enableButton;
    QPushButton* _disableButton;
    QPushButton* _groupButton;
    
    // Bottom dialog elements
    QLabel* _statusLabel;
    QPushButton* _closeButton;
    QPushButton* _applyButton;
};

#endif // BREAKPOINTDIALOG_H
