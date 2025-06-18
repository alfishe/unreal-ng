#pragma once
#ifndef MEMORYWIDGET_H
#define MEMORYWIDGET_H

#include <QWidget>
#include <QImage>
#include <QLabel>
#include <QCheckBox>
#include <QGridLayout>
#include <QPainter>

#include "emulator/emulator.h"

class MemoryWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MemoryWidget(QWidget *parent = nullptr);
    virtual ~MemoryWidget();

    void setEmulator(Emulator* emulator);
    void reset();
    void refresh();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void toggleReadLayer(bool checked);
    void toggleWriteLayer(bool checked);
    void toggleExecuteLayer(bool checked);

private:
    void createUI();
    void updateMemoryImage();
    QColor getColorForByte(uint8_t value);
    
    Emulator* _emulator = nullptr;
    QImage _memoryImage[4];  // One image for each 16KB bank
    
    bool _showReadLayer = false;
    bool _showWriteLayer = false;
    bool _showExecuteLayer = false;
    
    // Access tracking
    uint8_t* _readAccess[4] = {nullptr};    // Read access bitmap for each bank
    uint8_t* _writeAccess[4] = {nullptr};   // Write access bitmap for each bank
    uint8_t* _executeAccess[4] = {nullptr}; // Execute access bitmap for each bank
    
    // UI elements
    QGridLayout* _layout = nullptr;
    QLabel* _bankLabels[4] = {nullptr};
    QLabel* _imageLabels[4] = {nullptr};
    QCheckBox* _readLayerCheckbox = nullptr;
    QCheckBox* _writeLayerCheckbox = nullptr;
    QCheckBox* _executeLayerCheckbox = nullptr;
};

#endif // MEMORYWIDGET_H
