#include "ScreenViewer.h"

#include <QPainter>
#include <QMouseEvent>
#include <QDebug>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <unistd.h>
#endif

// ZX Spectrum standard color palette (ARGB format)
// Index: 0bFBRG where F=flash/bright, B=blue, R=red, G=green
const uint32_t ScreenViewer::_zxPalette[16] = {
    0xFF000000,  // 0: Black
    0xFF0000C0,  // 1: Blue (dark)
    0xFFC00000,  // 2: Red (dark)
    0xFFC000C0,  // 3: Magenta (dark)
    0xFF00C000,  // 4: Green (dark)
    0xFF00C0C0,  // 5: Cyan (dark)
    0xFFC0C000,  // 6: Yellow (dark)
    0xFFC0C0C0,  // 7: White (dark)
    0xFF000000,  // 8: Black (bright)
    0xFF0000FF,  // 9: Blue (bright)
    0xFFFF0000,  // 10: Red (bright)
    0xFFFF00FF,  // 11: Magenta (bright)
    0xFF00FF00,  // 12: Green (bright)
    0xFF00FFFF,  // 13: Cyan (bright)
    0xFFFFFF00,  // 14: Yellow (bright)
    0xFFFFFFFF   // 15: White (bright)
};

ScreenViewer::ScreenViewer(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(SCREEN_WIDTH, SCREEN_HEIGHT);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    
    // Initialize with black placeholder
    _currentImage = QImage(SCREEN_WIDTH, SCREEN_HEIGHT, QImage::Format_ARGB32);
    _currentImage.fill(Qt::black);
    
    _refreshTimer = new QTimer(this);
    connect(_refreshTimer, &QTimer::timeout, this, &ScreenViewer::refreshScreen);
}

ScreenViewer::~ScreenViewer()
{
    detachFromSharedMemory();
}

void ScreenViewer::attachToSharedMemory(const QString& emulatorId, 
                                         const QString& shmName, 
                                         qint64 shmSize)
{
    // Detach from previous if any
    if (_isAttached)
    {
        detachFromSharedMemory();
    }
    
    _emulatorId = emulatorId;
    _shmName = shmName;
    _shmSize = shmSize;
    
    qDebug() << "ScreenViewer: Attaching to shared memory:" << shmName 
             << "size:" << shmSize;

#ifdef _WIN32
    // Windows: Open named file mapping
    std::wstring wname = shmName.toStdWString();
    HANDLE hMapFile = OpenFileMappingW(FILE_MAP_READ, FALSE, wname.c_str());
    if (hMapFile == NULL)
    {
        qDebug() << "ScreenViewer: Failed to open shared memory (Windows):"
                 << GetLastError();
        return;
    }
    
    _shmData = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, shmSize);
    CloseHandle(hMapFile);  // Can close handle after mapping
    
    if (_shmData == NULL)
    {
        qDebug() << "ScreenViewer: Failed to map shared memory (Windows):"
                 << GetLastError();
        return;
    }
#else
    // POSIX: Open shared memory
    QByteArray nameBytes = shmName.toUtf8();
    _shmFd = shm_open(nameBytes.constData(), O_RDONLY, 0);
    if (_shmFd < 0)
    {
        qDebug() << "ScreenViewer: Failed to open shared memory (POSIX):"
                 << strerror(errno);
        return;
    }
    
    _shmData = mmap(nullptr, shmSize, PROT_READ, MAP_SHARED, _shmFd, 0);
    ::close(_shmFd);  // fd can be closed after mmap
    _shmFd = -1;
    
    if (_shmData == MAP_FAILED)
    {
        qDebug() << "ScreenViewer: Failed to mmap shared memory:"
                 << strerror(errno);
        _shmData = nullptr;
        return;
    }
#endif

    _isAttached = true;
    qDebug() << "ScreenViewer: Successfully attached to shared memory";
    
    // Do an initial refresh immediately
    refreshScreen();
    
    // Start the periodic refresh timer
    startRefreshTimer();
}

void ScreenViewer::detachFromSharedMemory()
{
    stopRefreshTimer();
    
    if (!_isAttached)
        return;

#ifdef _WIN32
    if (_shmData != nullptr)
    {
        UnmapViewOfFile(_shmData);
        _shmData = nullptr;
    }
#else
    if (_shmData != nullptr && _shmData != MAP_FAILED)
    {
        munmap(_shmData, _shmSize);
        _shmData = nullptr;
    }
#endif

    _isAttached = false;
    _emulatorId.clear();
    _shmName.clear();
    
    // Reset to black screen
    _currentImage.fill(Qt::black);
    update();
    
    qDebug() << "ScreenViewer: Detached from shared memory";
}

void ScreenViewer::setScreenPage(ScreenPage page)
{
    if (_currentPage != page)
    {
        _currentPage = page;
        emit screenPageChanged(page);
        refreshScreen();
    }
}

void ScreenViewer::toggleScreenPage()
{
    // Only toggle in single mode
    if (_viewMode == ViewMode::Single)
    {
        setScreenPage(_currentPage == ScreenPage::Main 
                      ? ScreenPage::Shadow 
                      : ScreenPage::Main);
    }
}

void ScreenViewer::setViewMode(ViewMode mode)
{
    if (_viewMode != mode)
    {
        _viewMode = mode;
        emit viewModeChanged(mode);
        refreshScreen();
    }
}

void ScreenViewer::setDualLayout(DualLayout layout)
{
    if (_dualLayout != layout)
    {
        _dualLayout = layout;
        emit dualLayoutChanged(layout);
        update();
    }
}

void ScreenViewer::startRefreshTimer()
{
    if (!_refreshTimer->isActive())
    {
        _refreshTimer->start(REFRESH_RATE_MS);
    }
}

void ScreenViewer::stopRefreshTimer()
{
    _refreshTimer->stop();
}

void ScreenViewer::refreshScreen()
{
    if (!_isAttached || _shmData == nullptr)
        return;
    
#ifndef _WIN32
    // Invalidate our view of shared memory to ensure we see latest writes
    msync(const_cast<void*>(_shmData), _shmSize, MS_INVALIDATE);
#endif
    
    // Always render main screen
    const uint8_t* mainData = getScreenData(ScreenPage::Main);
    if (mainData != nullptr)
    {
        _currentImage = renderScreen(mainData);
    }
    
    // In dual mode, also render shadow screen
    if (_viewMode == ViewMode::Dual)
    {
        const uint8_t* shadowData = getScreenData(ScreenPage::Shadow);
        if (shadowData != nullptr)
        {
            _shadowImage = renderScreen(shadowData);
        }
    }
    
    repaint();
}

const uint8_t* ScreenViewer::getScreenData(ScreenPage page) const
{
    if (_shmData == nullptr)
        return nullptr;
    
    // Calculate offset to the requested RAM page
    size_t pageOffset = static_cast<size_t>(page) * PAGE_SIZE;
    
    // Bounds check
    if (pageOffset + SCREEN_TOTAL_SIZE > static_cast<size_t>(_shmSize))
    {
        return nullptr;
    }
    
    return static_cast<const uint8_t*>(_shmData) + pageOffset;
}

QImage ScreenViewer::renderScreen(const uint8_t* ramData)
{
    QImage image(SCREEN_WIDTH, SCREEN_HEIGHT, QImage::Format_ARGB32);
    
    if (ramData == nullptr)
    {
        image.fill(Qt::black);
        return image;
    }
    
    // ZX Spectrum screen layout:
    // - Bitmap: 6144 bytes at offset 0 (256x192 pixels, 1 bit per pixel)
    // - Attributes: 768 bytes at offset 6144 (32x24 chars, 8x8 pixel cells)
    //
    // Bitmap address formula for pixel (x, y):
    // addr = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | (x >> 3)
    //
    // Attribute address formula for cell (cx, cy) where cx=x/8, cy=y/8:
    // addr = 6144 + cy * 32 + cx

    const uint8_t* bitmap = ramData;
    const uint8_t* attrs = ramData + SCREEN_BITMAP_SIZE;
    
    uint32_t* pixels = reinterpret_cast<uint32_t*>(image.bits());
    
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
    {
        for (int x = 0; x < SCREEN_WIDTH; ++x)
        {
            // Calculate bitmap byte address using ZX Spectrum screen layout
            int bitmapAddr = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | (x >> 3);
            int bitIndex = 7 - (x & 7);  // Bits are MSB first
            
            // Calculate attribute byte address
            int attrAddr = (y >> 3) * 32 + (x >> 3);
            
            uint8_t bitmapByte = bitmap[bitmapAddr];
            uint8_t attrByte = attrs[attrAddr];
            
            // Extract attribute components
            bool bright = (attrByte & 0x40) != 0;
            uint8_t paper = (attrByte >> 3) & 0x07;
            uint8_t ink = attrByte & 0x07;
            
            // Apply bright modifier
            if (bright)
            {
                paper += 8;
                ink += 8;
            }
            
            // Get pixel color (0 = paper, 1 = ink)
            bool pixelSet = (bitmapByte & (1 << bitIndex)) != 0;
            uint32_t color = pixelSet ? _zxPalette[ink] : _zxPalette[paper];
            
            pixels[y * SCREEN_WIDTH + x] = color;
        }
    }
    
    return image;
}

void ScreenViewer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.fillRect(rect(), Qt::black);
    
    QSize targetSize = size();
    
    if (_viewMode == ViewMode::Single)
    {
        // Single mode: calculate scaled size maintaining 4:3 aspect ratio
        float scaleX = static_cast<float>(targetSize.width()) / SCREEN_WIDTH;
        float scaleY = static_cast<float>(targetSize.height()) / SCREEN_HEIGHT;
        float scale = qMin(scaleX, scaleY);
        
        int scaledWidth = static_cast<int>(SCREEN_WIDTH * scale);
        int scaledHeight = static_cast<int>(SCREEN_HEIGHT * scale);
        int offsetX = (targetSize.width() - scaledWidth) / 2;
        int offsetY = (targetSize.height() - scaledHeight) / 2;
        
        QRect targetRect(offsetX, offsetY, scaledWidth, scaledHeight);
        
        // In single mode, show current page (main or shadow based on toggle)
        const QImage& displayImage = (_currentPage == ScreenPage::Main) ? _currentImage : _shadowImage;
        QString label = QString("Page %1").arg(static_cast<int>(_currentPage));
        drawScreenWithLabel(painter, targetRect, displayImage.isNull() ? _currentImage : displayImage, label);
    }
    else
    {
        // Dual mode: display both screens
        int screenW, screenH;
        QRect rect1, rect2;
        
        if (_dualLayout == DualLayout::Horizontal)
        {
            // Side-by-side: 8:3 aspect ratio (512x192)
            float scaleX = static_cast<float>(targetSize.width()) / (SCREEN_WIDTH * 2);
            float scaleY = static_cast<float>(targetSize.height()) / SCREEN_HEIGHT;
            float scale = qMin(scaleX, scaleY);
            
            screenW = static_cast<int>(SCREEN_WIDTH * scale);
            screenH = static_cast<int>(SCREEN_HEIGHT * scale);
            
            int totalW = screenW * 2;
            int offsetX = (targetSize.width() - totalW) / 2;
            int offsetY = (targetSize.height() - screenH) / 2;
            
            rect1 = QRect(offsetX, offsetY, screenW, screenH);
            rect2 = QRect(offsetX + screenW, offsetY, screenW, screenH);
        }
        else  // Vertical
        {
            // Stacked: 4:6 aspect ratio (256x384)
            float scaleX = static_cast<float>(targetSize.width()) / SCREEN_WIDTH;
            float scaleY = static_cast<float>(targetSize.height()) / (SCREEN_HEIGHT * 2);
            float scale = qMin(scaleX, scaleY);
            
            screenW = static_cast<int>(SCREEN_WIDTH * scale);
            screenH = static_cast<int>(SCREEN_HEIGHT * scale);
            
            int totalH = screenH * 2;
            int offsetX = (targetSize.width() - screenW) / 2;
            int offsetY = (targetSize.height() - totalH) / 2;
            
            rect1 = QRect(offsetX, offsetY, screenW, screenH);
            rect2 = QRect(offsetX, offsetY + screenH, screenW, screenH);
        }
        
        drawScreenWithLabel(painter, rect1, _currentImage, "Bank 5");
        drawScreenWithLabel(painter, rect2, _shadowImage, "Bank 7");
    }
}

void ScreenViewer::drawScreenWithLabel(QPainter& painter, const QRect& targetRect,
                                        const QImage& image, const QString& label)
{
    painter.drawImage(targetRect, image);
    
    QFont indicatorFont("Monospace", 12, QFont::Bold);
    painter.setFont(indicatorFont);
    
    QFontMetrics fm(indicatorFont);
    int textWidth = fm.horizontalAdvance(label) + 12;
    int textHeight = fm.height() + 6;
    
    QRect labelRect(targetRect.right() - textWidth - 6, 
                    targetRect.bottom() - textHeight - 6, 
                    textWidth, textHeight);
    
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(0, 0, 0, 200));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(labelRect, 4, 4);
    
    painter.setPen(Qt::white);
    painter.drawText(labelRect, Qt::AlignCenter, label);
}

void ScreenViewer::mousePressEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    
    // Toggle screen page on click (single mode only)
    if (_viewMode == ViewMode::Single)
    {
        toggleScreenPage();
    }
}

void ScreenViewer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}
