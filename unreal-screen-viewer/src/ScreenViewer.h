#pragma once

#include <QWidget>
#include <QTimer>
#include <QImage>

/**
 * @brief Widget for displaying ZX Spectrum screen from shared memory
 * 
 * Renders the emulator screen using raw RAM data from shared memory.
 * Supports:
 * - Single mode: switching between Page 5 and Page 7 via click
 * - Dual mode: displaying both screens with horizontal or vertical layout
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

    /// View mode for screen display
    enum class ViewMode
    {
        Single,      ///< Single screen with click-to-toggle
        Dual         ///< Both screens displayed simultaneously
    };

    /// Layout for dual screen mode
    enum class DualLayout
    {
        Horizontal,  ///< Side-by-side (5 | 7)
        Vertical     ///< Stacked (5 above 7)
    };

    explicit ScreenViewer(QWidget* parent = nullptr);
    ~ScreenViewer() override;

    /// Get the currently displayed screen page (single mode)
    ScreenPage currentPage() const { return _currentPage; }
    
    /// Get current view mode
    ViewMode viewMode() const { return _viewMode; }
    
    /// Get current dual layout
    DualLayout dualLayout() const { return _dualLayout; }

public slots:
    /// Attach to shared memory region
    void attachToSharedMemory(const QString& emulatorId, const QString& shmName, qint64 shmSize);

    /// Detach from current shared memory
    void detachFromSharedMemory();

    /// Set the screen page to display (single mode only)
    void setScreenPage(ScreenPage page);

    /// Toggle between main and shadow screen (single mode only)
    void toggleScreenPage();

    /// Set the view mode (single or dual)
    void setViewMode(ViewMode mode);

    /// Set the layout for dual mode
    void setDualLayout(DualLayout layout);

signals:
    /// Emitted when screen page changes (single mode)
    void screenPageChanged(ScreenPage page);
    
    /// Emitted when view mode changes
    void viewModeChanged(ViewMode mode);
    
    /// Emitted when dual layout changes
    void dualLayoutChanged(DualLayout layout);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void startRefreshTimer();
    void stopRefreshTimer();
    void refreshScreen();
    
    /// Render ZX Spectrum screen from raw RAM data
    QImage renderScreen(const uint8_t* ramData);

    /// Get pointer to screen data for specified page
    const uint8_t* getScreenData(ScreenPage page) const;
    
    /// Legacy wrapper for current page
    const uint8_t* getScreenData() const { return getScreenData(_currentPage); }
    
    /// Draw a single screen with label
    void drawScreenWithLabel(QPainter& painter, const QRect& targetRect, 
                             const QImage& image, const QString& label);

    // Shared memory
    void* _shmData = nullptr;
    qint64 _shmSize = 0;
    int _shmFd = -1;
    QString _shmName;
    QString _emulatorId;
    bool _isAttached = false;

    // Display state
    ScreenPage _currentPage = ScreenPage::Main;
    ViewMode _viewMode = ViewMode::Single;
    DualLayout _dualLayout = DualLayout::Horizontal;
    QImage _currentImage;
    QImage _shadowImage;  // For dual mode
    QTimer* _refreshTimer = nullptr;

    // ZX Spectrum constants
    static constexpr int PAGE_SIZE = 0x4000;
    static constexpr int SCREEN_BITMAP_SIZE = 6144;
    static constexpr int SCREEN_ATTR_SIZE = 768;
    static constexpr int SCREEN_TOTAL_SIZE = 6912;
    static constexpr int SCREEN_WIDTH = 256;
    static constexpr int SCREEN_HEIGHT = 192;
    static constexpr int REFRESH_RATE_MS = 20;

    // ZX Spectrum color palette
    static const uint32_t _zxPalette[16];
};

