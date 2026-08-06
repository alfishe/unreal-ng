#pragma once

#include <QWidget>
#include <QImage>
#include <QString>

/// Video mode definitions with their native resolutions
enum class VideoMode
{
    // Standard ZX Spectrum
    ZX_256x192,         // Inner screen only (no border)
    ZX_320x240,         // With partial border
    ZX_352x288,         // Full PAL with border
    ZX_384x304,         // Extended border

    // ATM Turbo / Profi
    ATM_320x200,        // EGA-like
    ATM_640x200,        // ATM hi-res
    Profi_512x240,      // Profi hi-res

    // ZX Evolution / TS-Conf
    Evo_360x288,        // Extended PAL
    TSConf_640x400,     // VGA
    TSConf_640x480,     // VGA extended

    // ZX Next
    Next_320x256,       // Tilemap / Layer 2
    Next_640x256,       // Hi-res mode

    // Generic hi-res
    HiRes_512x384,      // 2x inner screen
    HiRes_640x512       // Maximum supported
};

struct VideoModeInfo
{
    VideoMode mode;
    uint16_t width;
    uint16_t height;
    const char* name;
    const char* patternFile;
};

/// Minimal framebuffer display widget.
/// Renders an external RGBA8888 buffer with smooth scaling and aspect ratio preservation.
class ScreenWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ScreenWidget(QWidget* parent = nullptr);
    ~ScreenWidget() override;

    /// Attach to an external framebuffer. Widget does NOT own the buffer.
    void attachFramebuffer(uint16_t width, uint16_t height, void* buffer);

    /// Detach from current framebuffer
    void detachFramebuffer();

    /// Load test pattern from resources for given mode
    void loadTestPattern(int modeIndex);

    /// Check if framebuffer is attached
    bool hasFramebuffer() const { return m_image != nullptr; }

    /// Get current framebuffer dimensions
    uint16_t framebufferWidth() const { return m_baseWidth; }
    uint16_t framebufferHeight() const { return m_baseHeight; }

    /// Get size for 1:1 pixel mapping
    QSize nativeSize() const { return QSize(m_baseWidth, m_baseHeight); }

    QSize sizeHint() const override { return QSize(m_baseWidth * 2, m_baseHeight * 2); }
    QSize minimumSizeHint() const override { return QSize(m_baseWidth, m_baseHeight); }

    /// Get list of available video modes
    static const VideoModeInfo* videoModes();
    static int videoModeCount();

    /// Calculate scaled image rect for given container size
    QRect scaledRect(const QSize& containerSize) const;

public slots:
    /// Schedule a repaint (call after framebuffer content changes)
    void refresh();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage* m_image = nullptr;
    QImage m_ownedImage;  // For test patterns loaded from resources
    uint16_t m_baseWidth = 352;
    uint16_t m_baseHeight = 288;
    float m_aspectRatio = 352.0f / 288.0f;
};
