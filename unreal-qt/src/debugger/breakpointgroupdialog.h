#pragma once
#ifndef BREAKPOINTGROUPDIALOG_H
#define BREAKPOINTGROUPDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include "emulator/emulator.h"
#include "debugger/breakpoints/breakpointmanager.h"

class BreakpointGroupDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BreakpointGroupDialog(Emulator* emulator, QWidget* parent = nullptr);
    ~BreakpointGroupDialog();
    
private slots:
    void addGroup();
    void renameGroup();
    void deleteGroup();
    void onGroupSelectionChanged();
    
private:
    void setupUI();
    void populateGroupList();
    void updateStatusBar();
    
    Emulator* _emulator;
    QListWidget* _groupList;
    
    QPushButton* _addButton;
    QPushButton* _renameButton;
    QPushButton* _deleteButton;
    QPushButton* _closeButton;
    
    QLabel* _statusLabel;
};

#endif // BREAKPOINTGROUPDIALOG_H
