#pragma once

#include <QWidget>
#include <QTimer>
#include <QImage>

/**
 * @brief Widget for displaying ZX Spectrum screen from shared memory
 * 
 * Renders the emulator screen using raw RAM data from shared memory.
 * Supports switching between main screen (Page 5) and shadow screen (Page 7).
 * Click anywhere on the widget to toggle between screens.
 */
class ScreenViewer : public QWidget
{
    Q_OBJECT

public:
    /// Screen page selection (RAM page numbers)
    enum class ScreenPage
    {
        Main = 5,    ///< Main screen at RAM page 5
        Shadow = 7   ///< Shadow screen at RAM page 7 (128K mode)
    };

    explicit ScreenViewer(QWidget* parent = nullptr);
    ~ScreenViewer() override;

    /// Get the currently displayed screen page
    ScreenPage currentPage() const { return _currentPage; }

public slots:
    /// Attach to shared memory region
    /// @param emulatorId Emulator identifier (for logging)
    /// @param shmName Shared memory region name
    /// @param shmSize Size of the shared memory region
    void attachToSharedMemory(const QString& emulatorId, const QString& shmName, qint64 shmSize);

    /// Detach from current shared memory
    void detachFromSharedMemory();

    /// Set the screen page to display
    void setScreenPage(ScreenPage page);

    /// Toggle between main and shadow screen
    void toggleScreenPage();

signals:
    /// Emitted when screen page changes
    void screenPageChanged(ScreenPage page);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void startRefreshTimer();
    void stopRefreshTimer();
    void refreshScreen();
    
    /// Render ZX Spectrum screen from raw RAM data
    /// @param ramData Pointer to 6912 bytes of screen data
    /// @return Rendered QImage (256x192)
    QImage renderScreen(const uint8_t* ramData);

    /// Get pointer to screen data for current page
    const uint8_t* getScreenData() const;

    // Shared memory
    void* _shmData = nullptr;
    qint64 _shmSize = 0;
    int _shmFd = -1;
    QString _shmName;
    QString _emulatorId;
    bool _isAttached = false;

    // Display state
    ScreenPage _currentPage = ScreenPage::Main;
    QImage _currentImage;
    QTimer* _refreshTimer = nullptr;

    // ZX Spectrum constants (from core/src/emulator/platform.h)
    static constexpr int PAGE_SIZE = 0x4000;           // 16KB per page
    static constexpr int SCREEN_BITMAP_SIZE = 6144;    // 256x192 / 8 bits = 6144 bytes
    static constexpr int SCREEN_ATTR_SIZE = 768;       // 32x24 attributes
    static constexpr int SCREEN_TOTAL_SIZE = 6912;     // SCREEN_BITMAP_SIZE + SCREEN_ATTR_SIZE
    static constexpr int SCREEN_WIDTH = 256;
    static constexpr int SCREEN_HEIGHT = 192;
    static constexpr int REFRESH_RATE_MS = 20;         // 50Hz refresh

    // ZX Spectrum color palette (standard colors)
    static const uint32_t _zxPalette[16];
};
