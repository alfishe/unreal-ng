#include "ulabeamwidget.h"

#include <QDebug>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "emulator/video/screen.h"

ULABeamWidget::ULABeamWidget(QWidget* parent) : QWidget(parent)
{
    createUI();
    setMinimumSize(200, 200);
}

ULABeamWidget::~ULABeamWidget()
{
    // Clean up any resources if needed
}

void ULABeamWidget::createUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    _titleLabel = new QLabel("ULA Beam Position", this);
    _titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(_titleLabel);

    // Add spacer to push content to the top
    layout->addStretch();

    setLayout(layout);
}

void ULABeamWidget::setEmulator(Emulator* emulator)
{
    _emulator = emulator;
    reset();
}

void ULABeamWidget::reset()
{
    _beamX = 0;
    _beamY = 0;
    refresh();
}

void ULABeamWidget::refresh()
{
    if (!_emulator)
        return;

    CONFIG& config = _emulator->GetContext()->config;
    if (config.t_line == 0 || config.frame == 0)
        return;

    // Read raster geometry from the screen's descriptor
    Screen* screen = _emulator->GetContext()->pScreen;
    VideoModeEnum mode = screen->GetVideoMode();
    const RasterDescriptor& rd = screen->rasterDescriptors[mode];

    _totalPixelsPerLine = rd.pixelsPerLine;
    _totalLines = rd.vSyncLines + rd.vBlankLines + rd.fullFrameHeight;
    _visibleWidth = rd.fullFrameWidth;
    _visibleHeight = rd.fullFrameHeight;
    _visibleOffsetX = 0;  // Visible pixels start at pixel 0 in the line
    _visibleOffsetY = rd.vSyncLines + rd.vBlankLines;
    _paperOffsetX = rd.screenOffsetLeft;
    _paperOffsetY = rd.screenOffsetTop;
    _paperWidth = rd.screenWidth;
    _paperHeight = rd.screenHeight;

    Z80* cpu = _emulator->GetContext()->pCore->GetZ80();
    if (cpu)
    {
        _currentTstate = cpu->t;
        int tStatesPerLine = config.t_line;
        _currentLine = (_currentTstate % config.frame) / tStatesPerLine;
        _linePosition = _currentTstate % tStatesPerLine;

        // Beam in absolute raster coordinates
        _beamX = _linePosition * 2;   // 2 pixels per t-state
        _beamY = _currentLine;         // Absolute raster line
    }

    updateScreenImage();
    update();
}

void ULABeamWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int margin = 10;
    const int headerHeight = 30;
    const int footerHeight = 35;

    int availableWidth = width() - 2 * margin;
    int availableHeight = height() - headerHeight - footerHeight;

    // Scale to fit the full raster frame (pixelsPerLine × totalLines)
    float scaleX = static_cast<float>(availableWidth) / _totalPixelsPerLine;
    float scaleY = static_cast<float>(availableHeight) / _totalLines;
    float scale = qMin(scaleX, scaleY);

    // Origin of the full raster frame in widget coordinates
    int frameX = margin + (availableWidth - _totalPixelsPerLine * scale) / 2;
    int frameY = headerHeight + (availableHeight - _totalLines * scale) / 2;
    int frameW = _totalPixelsPerLine * scale;
    int frameH = _totalLines * scale;

    // === 1. Draw the full raster frame background (off-screen: dark) ===
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(40, 40, 50));  // Dark blue-gray for off-screen
    painter.drawRect(frameX, frameY, frameW, frameH);

    // === 2. Draw the visible area (border + paper) ===
    int visX = frameX + _visibleOffsetX * scale;
    int visY = frameY + _visibleOffsetY * scale;
    int visW = _visibleWidth * scale;
    int visH = _visibleHeight * scale;

    // Draw screen image or placeholder for the visible area
    if (!_screenImage.isNull())
    {
        QRect destRect(visX, visY, visW, visH);
        painter.drawImage(destRect, _screenImage);
    }
    else
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(160, 160, 160));
        painter.drawRect(visX, visY, visW, visH);
    }

    // === 3. Draw the HBlank strip on the right within visible lines ===
    // HBlank/HSync pixels are at the end of each line (pixel >= fullFrameWidth)
    int hblankX = frameX + _visibleWidth * scale;
    int hblankW = (_totalPixelsPerLine - _visibleWidth) * scale;
    // HBlank only applies to visible lines (within the visible vertical region)
    // The dark raster background already covers VSync/VBlank lines, so we just
    // mark HBlank within the visible height region with a slightly different tone
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(50, 45, 60));  // Slightly purple tint for HBlank
    painter.drawRect(hblankX, visY, hblankW, visH);

    // === 4. Draw region labels ===
    painter.setPen(QColor(120, 120, 140));
    QFont labelFont = painter.font();
    labelFont.setPixelSize(qMax(9, (int)(scale * 12)));
    painter.setFont(labelFont);

    // VSync/VBlank label (top dark area)
    if (_visibleOffsetY * scale > 14)
    {
        QRect vblankRect(frameX, frameY, frameW, _visibleOffsetY * scale);
        painter.drawText(vblankRect, Qt::AlignCenter, "VSync / VBlank");
    }

    // HBlank label (right dark strip)
    if (hblankW > 20)
    {
        painter.save();
        painter.translate(hblankX + hblankW / 2, visY + visH / 2);
        painter.rotate(90);
        painter.drawText(QRect(-visH / 2, -hblankW / 2, visH, hblankW), Qt::AlignCenter, "HBlank");
        painter.restore();
    }

    // === 5. Draw paper area outline ===
    painter.setPen(QPen(Qt::darkGray, 1));
    painter.setBrush(Qt::NoBrush);
    int paperX = visX + _paperOffsetX * scale;
    int paperY = visY + _paperOffsetY * scale;
    painter.drawRect(paperX, paperY, _paperWidth * scale, _paperHeight * scale);

    // === 6. Draw raster frame outline ===
    painter.setPen(QPen(QColor(80, 80, 100), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(frameX, frameY, frameW, frameH);

    // === 7. Draw beam position ===
    // Beam coordinates are in absolute raster space (0..totalPixels, 0..totalLines)
    bool validBeam = _beamX >= 0 && _beamX < _totalPixelsPerLine && _beamY >= 0 && _beamY < _totalLines;

    if (validBeam)
    {
        int beamScreenX = frameX + _beamX * scale;
        int beamScreenY = frameY + _beamY * scale;

        // Determine if beam is on visible area or off-screen
        bool beamOnVisible = _beamX < _visibleWidth && _beamY >= _visibleOffsetY;

        QColor crosshairColor = beamOnVisible ? QColor(255, 0, 0, 100) : QColor(100, 140, 255, 100);
        QColor dotColor = beamOnVisible ? Qt::red : QColor(80, 120, 255);

        // Draw crosshair lines spanning the full raster frame
        painter.setPen(QPen(crosshairColor, 1, Qt::DashLine));
        painter.drawLine(frameX, beamScreenY, frameX + frameW, beamScreenY);
        painter.drawLine(beamScreenX, frameY, beamScreenX, frameY + frameH);

        // Draw beam dot
        painter.setPen(Qt::NoPen);
        painter.setBrush(dotColor);
        painter.drawEllipse(beamScreenX - 4, beamScreenY - 4, 8, 8);
    }

    // === 8. Info text ===
    painter.setPen(Qt::black);
    QFont infoFont = painter.font();
    infoFont.setPixelSize(11);
    painter.setFont(infoFont);

    // Determine region name
    QString region;
    bool inVBlank = _beamY < _visibleOffsetY;
    bool inHBlank = _beamX >= _visibleWidth;
    bool inBorder = false;
    bool inPaper = false;

    if (!inVBlank && !inHBlank)
    {
        int relY = _beamY - _visibleOffsetY;
        bool inPaperV = relY >= _paperOffsetY && relY < (_paperOffsetY + _paperHeight);
        bool inPaperH = _beamX >= _paperOffsetX && _beamX < (_paperOffsetX + _paperWidth);
        inPaper = inPaperV && inPaperH;
        inBorder = !inPaper;
    }

    if (inVBlank)
        region = " [VSync/VBlank]";
    else if (inHBlank)
        region = " [HBlank]";
    else if (inBorder)
        region = " [Border]";
    else if (inPaper)
        region = " [Paper]";

    QString infoText = QString("T-state: %1 | Line: %2 | Pos: %3 | Pixel: (%4, %5)%6")
                           .arg(_currentTstate)
                           .arg(_currentLine)
                           .arg(_linePosition)
                           .arg(_beamX)
                           .arg(_beamY)
                           .arg(region);
    painter.drawText(5, height() - 5, infoText);
}

void ULABeamWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refresh();
}

void ULABeamWidget::updateScreenImage()
{
    if (!_emulator)
        return;

    FramebufferDescriptor fb = _emulator->GetFramebuffer();
    if (!fb.memoryBuffer || fb.width == 0 || fb.height == 0)
        return;

    int imgWidth = _visibleWidth;
    int imgHeight = _visibleHeight;

    if (_screenImage.width() != imgWidth || _screenImage.height() != imgHeight)
        _screenImage = QImage(imgWidth, imgHeight, QImage::Format_RGB32);

    uint32_t* src = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    for (int y = 0; y < imgHeight && y < fb.height; y++)
    {
        QRgb* destLine = reinterpret_cast<QRgb*>(_screenImage.scanLine(y));
        for (int x = 0; x < imgWidth && x < fb.width; x++)
        {
            uint32_t pixel = src[y * fb.width + x];
            int r = (pixel >> 16) & 0xFF;
            int g = (pixel >> 8) & 0xFF;
            int b = pixel & 0xFF;
            int gray = (r + g + b) / 3;
            int faded = 128 + gray / 4;
            destLine[x] = qRgb(faded, faded, faded);
        }
    }
}
