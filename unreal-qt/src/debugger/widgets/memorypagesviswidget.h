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
    /// @param pageNumber The physical RAM page number clicked
    /// @param viewerSlot 0 for left-click (top free viewer), 1 for right-click (bottom free viewer)
    void pageClickedForFreeViewer(int pageNumber, int viewerSlot);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createUI();
    void updatePageDisplay();
    QColor getColorForPage(int pageIndex, bool isAccessed);
    
    Emulator* _emulator = nullptr;
    QGridLayout* _gridLayout = nullptr;   // Grid layout inside scrollable container
    QWidget* _gridWidget = nullptr;       // Container widget for the grid
    QScrollArea* _scrollArea = nullptr;
    QLabel* _titleLabel = nullptr;
    
    // Memory pages tracking
    int _maxPages = 0;          // Maximum number of pages for current configuration
    QVector<bool> _accessedPages; // Tracks which pages have been accessed since reset
    QVector<QLabel*> _pageLabels; // Labels for each memory page

    // Dirty-checking: skip redundant updates when nothing changed
    QVector<QString> _lastLabelTexts;
    QVector<QRgb> _lastLabelColors;
};

#endif // MEMORYPAGESVISWIDGET_H
