#pragma once
#ifndef MEMORYPAGESVISWIDGET_H
#define MEMORYPAGESVISWIDGET_H

#include <QWidget>
#include <QGridLayout>
#include <QLabel>
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

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void createUI();
    void updatePageDisplay();
    QColor getColorForPage(int pageIndex, bool isAccessed);
    
    Emulator* _emulator = nullptr;
    QGridLayout* _layout = nullptr;
    QLabel* _titleLabel = nullptr;
    
    // Memory pages tracking
    int _maxPages = 0;          // Maximum number of pages for current configuration
    QVector<bool> _accessedPages; // Tracks which pages have been accessed since reset
    QVector<QLabel*> _pageLabels; // Labels for each memory page
};

#endif // MEMORYPAGESVISWIDGET_H
