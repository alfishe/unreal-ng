#pragma once
#ifndef MEMORYWIDGET_H
#define MEMORYWIDGET_H

#include <QCheckBox>
#include <QGridLayout>
#include <QWidget>

#include "emulator/emulator.h"

class Memory16KBWidget;

class MemoryWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MemoryWidget(QWidget* parent = nullptr);
    virtual ~MemoryWidget();

    void setEmulator(Emulator* emulator);
    void reset();
    void refresh();

private slots:
    void toggleReadLayer(bool checked);
    void toggleWriteLayer(bool checked);
    void toggleExecuteLayer(bool checked);
    void toggleHideValues(bool checked);

public slots:
    void setFreePageNumber(int viewerSlot, int pageNumber);

private:
    void createUI();

    Emulator* _emulator = nullptr;
    Memory16KBWidget* _bankWidgets[4] = {nullptr};   // Z80 hard-mapped banks
    Memory16KBWidget* _freeWidgets[2] = {nullptr};    // Freely selectable pages

    bool _showReadLayer = false;
    bool _showWriteLayer = false;
    bool _showExecuteLayer = false;
    bool _hideValues = false;

    QGridLayout* _layout = nullptr;
    QCheckBox* _readLayerCheckbox = nullptr;
    QCheckBox* _writeLayerCheckbox = nullptr;
    QCheckBox* _executeLayerCheckbox = nullptr;
    QCheckBox* _hideValuesCheckbox = nullptr;
};

#endif  // MEMORYWIDGET_H
