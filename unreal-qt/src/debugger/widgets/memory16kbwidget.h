#pragma once
#ifndef MEMORY16KBWIDGET_H
#define MEMORY16KBWIDGET_H

#include <QEvent>
#include <QImage>
#include <QLabel>
#include <QWidget>

#include "emulator/emulator.h"

class Memory16KBWidget : public QWidget
{
    Q_OBJECT
public:
    enum class DisplayMode
    {
        Z80Bank,       // Hard-mapped Z80 address space bank (0-3)
        PhysicalPage   // Freely selected physical RAM page
    };

    explicit Memory16KBWidget(int bankIndex, QWidget* parent = nullptr);
    virtual ~Memory16KBWidget();

    void setEmulator(Emulator* emulator);
    void setBankIndex(int bankIndex);
    void setPhysicalPage(int pageNumber);
    int getPhysicalPage() const { return _physicalPageNumber; }
    DisplayMode getDisplayMode() const { return _displayMode; }
    int getBankIndex() const
    {
        return _bankIndex;
    }

    void reset();
    void refresh();

    void setShowReadOverlay(bool show);
    void setShowWriteOverlay(bool show);
    void setShowExecuteOverlay(bool show);
    void setHideValues(bool hide);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateMemoryImage();
    void updateCounterLabels();
    int mapMouseToOffset(const QPoint& pos) const;
    static void initColorLUT();

    Emulator* _emulator = nullptr;

    // Pre-computed color lookup table (256 entries as QRgb for direct scanLine writes)
    static QRgb s_colorLUT[256];
    static bool s_colorLUTInitialized;
    DisplayMode _displayMode = DisplayMode::Z80Bank;
    int _bankIndex = 0;
    int _physicalPageNumber = 0;  // Used in PhysicalPage mode

    QImage _memoryImage;
    QLabel* _titleLabel = nullptr;
    QLabel* _imageLabel = nullptr;
    QLabel* _countersLabel = nullptr;

    bool _showReadOverlay = false;
    bool _showWriteOverlay = false;
    bool _showExecuteOverlay = false;
    bool _hideValues = false;

    int _currentImageWidth = IMAGE_WIDTH;
    int _currentImageHeight = IMAGE_HEIGHT;

    static constexpr int IMAGE_WIDTH = 256;
    static constexpr int IMAGE_HEIGHT = 64;
    static constexpr int BANK_SIZE = 16384;
};

#endif  // MEMORY16KBWIDGET_H
