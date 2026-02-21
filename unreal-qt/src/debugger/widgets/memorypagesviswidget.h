#pragma once
#ifndef MEMORYPAGESVISWIDGET_H
#define MEMORYPAGESVISWIDGET_H

#include <QWidget>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVector>

#include "emulator/emulator.h"

class MemoryPagesVisWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MemoryPagesVisWidget(QWidget *parent = nullptr);
    virtual ~MemoryPagesVisWidget();

    void setEmulator(Emulator* emulator);
    void reset();
    void refresh();

signals:
    /// @brief Emitted when a page label is clicked.
    /// @param absPageIndex Absolute page index (RAM 0-255, ROM at FIRST_ROM_PAGE+)
    /// @param viewerSlot 0 for left-click (top free viewer), 1 for right-click (bottom free viewer)
    void pageClickedForFreeViewer(int absPageIndex, int viewerSlot);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createUI();
    void updatePageDisplay();
    QColor getColorForPage(bool isROM, bool isAccessed, bool isMapped);

    Emulator* _emulator = nullptr;
    QGridLayout* _gridLayout = nullptr;   // Grid layout inside scrollable container
    QWidget* _gridWidget = nullptr;       // Container widget for the grid
    QScrollArea* _scrollArea = nullptr;
    QLabel* _titleLabel = nullptr;

    // Page entries: each label maps to an absolute page index
    struct PageEntry
    {
        QLabel* label = nullptr;
        int absPageIndex = 0;       // Absolute page index [0, MAX_PAGES)
        bool isROM = false;
    };
    QVector<PageEntry> _pageEntries;

    // Configuration
    int _maxRamPages = 0;               // Number of RAM pages for current config
    static constexpr int ROM_PAGES_SHOWN = 4;  // First 4 ROM pages always shown

    // Dirty-checking: skip redundant updates when nothing changed
    QVector<QString> _lastLabelTexts;
    QVector<QRgb> _lastLabelColors;
};

#endif // MEMORYPAGESVISWIDGET_H
