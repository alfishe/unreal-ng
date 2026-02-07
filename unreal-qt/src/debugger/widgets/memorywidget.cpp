#include "memorywidget.h"

#include <QDebug>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "memory16kbwidget.h"

MemoryWidget::MemoryWidget(QWidget* parent) : QWidget(parent)
{
    createUI();
    setMinimumSize(900, 350);
}

MemoryWidget::~MemoryWidget() {}

void MemoryWidget::createUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    _layout = new QGridLayout();
    _layout->setSpacing(4);

    // Equal column and row stretch for uniform 2x3 grid
    _layout->setColumnStretch(0, 1);
    _layout->setColumnStretch(1, 1);
    _layout->setColumnStretch(2, 1);
    _layout->setRowStretch(0, 1);
    _layout->setRowStretch(1, 1);

    // Columns 0-1: Z80 hard-mapped banks (2x2 grid)
    for (int i = 0; i < 4; i++)
    {
        _bankWidgets[i] = new Memory16KBWidget(i, this);
        _layout->addWidget(_bankWidgets[i], i / 2, i % 2);
    }

    // Column 2: Freely selectable memory pages (2 rows)
    for (int i = 0; i < 2; i++)
    {
        _freeWidgets[i] = new Memory16KBWidget(0, this);
        _freeWidgets[i]->setPhysicalPage(i);
        _layout->addWidget(_freeWidgets[i], i, 2);
    }

    mainLayout->addLayout(_layout, 1);

    QHBoxLayout* controlLayout = new QHBoxLayout();
    _readLayerCheckbox = new QCheckBox("Read", this);
    _writeLayerCheckbox = new QCheckBox("Write", this);
    _executeLayerCheckbox = new QCheckBox("Execute", this);
    _hideValuesCheckbox = new QCheckBox("Hide Values", this);

    connect(_readLayerCheckbox, &QCheckBox::toggled, this, &MemoryWidget::toggleReadLayer);
    connect(_writeLayerCheckbox, &QCheckBox::toggled, this, &MemoryWidget::toggleWriteLayer);
    connect(_executeLayerCheckbox, &QCheckBox::toggled, this, &MemoryWidget::toggleExecuteLayer);
    connect(_hideValuesCheckbox, &QCheckBox::toggled, this, &MemoryWidget::toggleHideValues);

    controlLayout->addWidget(_readLayerCheckbox);
    controlLayout->addWidget(_writeLayerCheckbox);
    controlLayout->addWidget(_executeLayerCheckbox);
    controlLayout->addWidget(_hideValuesCheckbox);
    controlLayout->addStretch();

    // Color legend
    auto createSwatch = [](QColor color, QWidget* parent) -> QLabel* {
        QLabel* swatch = new QLabel(parent);
        swatch->setFixedSize(12, 12);
        swatch->setStyleSheet(QString("background-color: %1; border: 1px solid #666;").arg(color.name()));
        return swatch;
    };

    QHBoxLayout* legendLayout = new QHBoxLayout();
    legendLayout->setSpacing(6);

    // Group 1: Memory Values (using QGroupBox like Profiler Features)
    QGroupBox* valuesGroup = new QGroupBox("Values", this);
    QHBoxLayout* valuesLayout = new QHBoxLayout(valuesGroup);
    valuesLayout->setContentsMargins(6, 2, 6, 2);
    valuesLayout->setSpacing(6);
    valuesLayout->addWidget(createSwatch(QColor(0, 0, 0), valuesGroup));       // Black for 0x00
    valuesLayout->addWidget(new QLabel("0x00", valuesGroup));
    valuesLayout->addSpacing(4);
    valuesLayout->addWidget(createSwatch(QColor(255, 255, 255), valuesGroup)); // White for 0xFF
    valuesLayout->addWidget(new QLabel("0xFF", valuesGroup));
    valuesLayout->addSpacing(4);
    valuesLayout->addWidget(createSwatch(QColor(140, 140, 140), valuesGroup)); // Gray for other
    valuesLayout->addWidget(new QLabel("other", valuesGroup));
    legendLayout->addWidget(valuesGroup);

    // Group 2: Access Counters (R/W/X overlays)
    QGroupBox* accessGroup = new QGroupBox("Access", this);
    QHBoxLayout* accessLayout = new QHBoxLayout(accessGroup);
    accessLayout->setContentsMargins(6, 2, 6, 2);
    accessLayout->setSpacing(6);
    accessLayout->addWidget(createSwatch(QColor(80, 80, 255), accessGroup));   // Blue for Read
    accessLayout->addWidget(new QLabel("R", accessGroup));
    accessLayout->addSpacing(4);
    accessLayout->addWidget(createSwatch(QColor(255, 80, 80), accessGroup));   // Red for Write
    accessLayout->addWidget(new QLabel("W", accessGroup));
    accessLayout->addSpacing(4);
    accessLayout->addWidget(createSwatch(QColor(80, 255, 80), accessGroup));   // Green for Execute
    accessLayout->addWidget(new QLabel("X", accessGroup));
    legendLayout->addWidget(accessGroup);

    controlLayout->addLayout(legendLayout);

    mainLayout->addLayout(controlLayout);

    setLayout(mainLayout);
}

void MemoryWidget::setEmulator(Emulator* emulator)
{
    _emulator = emulator;

    for (int i = 0; i < 4; i++)
    {
        if (_bankWidgets[i])
            _bankWidgets[i]->setEmulator(_emulator);
    }

    for (int i = 0; i < 2; i++)
    {
        if (_freeWidgets[i])
            _freeWidgets[i]->setEmulator(_emulator);
    }

    reset();
}

void MemoryWidget::reset()
{
    for (int i = 0; i < 4; i++)
    {
        if (_bankWidgets[i])
            _bankWidgets[i]->reset();
    }

    for (int i = 0; i < 2; i++)
    {
        if (_freeWidgets[i])
            _freeWidgets[i]->reset();
    }
}

void MemoryWidget::refresh()
{
    for (int i = 0; i < 4; i++)
    {
        if (_bankWidgets[i])
            _bankWidgets[i]->refresh();
    }

    for (int i = 0; i < 2; i++)
    {
        if (_freeWidgets[i])
            _freeWidgets[i]->refresh();
    }
}

void MemoryWidget::toggleReadLayer(bool checked)
{
    _showReadLayer = checked;
    for (int i = 0; i < 4; i++)
    {
        if (_bankWidgets[i])
            _bankWidgets[i]->setShowReadOverlay(checked);
    }
    for (int i = 0; i < 2; i++)
    {
        if (_freeWidgets[i])
            _freeWidgets[i]->setShowReadOverlay(checked);
    }
}

void MemoryWidget::toggleWriteLayer(bool checked)
{
    _showWriteLayer = checked;
    for (int i = 0; i < 4; i++)
    {
        if (_bankWidgets[i])
            _bankWidgets[i]->setShowWriteOverlay(checked);
    }
    for (int i = 0; i < 2; i++)
    {
        if (_freeWidgets[i])
            _freeWidgets[i]->setShowWriteOverlay(checked);
    }
}

void MemoryWidget::toggleExecuteLayer(bool checked)
{
    _showExecuteLayer = checked;
    for (int i = 0; i < 4; i++)
    {
        if (_bankWidgets[i])
            _bankWidgets[i]->setShowExecuteOverlay(checked);
    }
    for (int i = 0; i < 2; i++)
    {
        if (_freeWidgets[i])
            _freeWidgets[i]->setShowExecuteOverlay(checked);
    }
}

void MemoryWidget::toggleHideValues(bool checked)
{
    _hideValues = checked;
    for (int i = 0; i < 4; i++)
    {
        if (_bankWidgets[i])
            _bankWidgets[i]->setHideValues(checked);
    }
    for (int i = 0; i < 2; i++)
    {
        if (_freeWidgets[i])
            _freeWidgets[i]->setHideValues(checked);
    }
}

void MemoryWidget::setFreePageNumber(int viewerSlot, int pageNumber)
{
    if (viewerSlot >= 0 && viewerSlot < 2 && _freeWidgets[viewerSlot])
    {
        _freeWidgets[viewerSlot]->setPhysicalPage(pageNumber);
    }
}
