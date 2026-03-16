#pragma once

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QScrollArea>
#include <QMap>
#include <QByteArray>

class QGridLayout;
class WebAPIClient;

/// Per-page tile widget — renders full 128×128 heatmap of 16384 counters
/// with optional memory content as gray base layer.
class PageTileWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PageTileWidget(int id, const QString& label, QWidget* parent = nullptr);

    void renderFromCounters(const uint32_t* rd, const uint32_t* wr, const uint32_t* ex,
                            int count, int mode, bool showContent = false);
    void setContentBytes(const QByteArray& bytes);
    void renderBlank();
    void setLabel(const QString& label);
    int tileId() const { return _tileId; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updatePixmap();
    int _tileId;
    QLabel* _titleLabel;
    QLabel* _imageLabel;
    QByteArray _contentBytes;
    QImage _lastImage;  // cached for resize
};

class HeatmapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HeatmapWidget(QWidget* parent = nullptr);

    enum Mode { Read = 0, Write, Execute, Combined };

    void setApiClient(WebAPIClient* client, const QString& emulatorId);

public slots:
    void attachToShm(void* base, size_t size);
    void detachFromShm();
    void onFrameReady();
    void setFifoActive(bool active);

private slots:
    void onRefresh();
    void onModeChanged(int index);
    void onCaptureToggled();
    void onResetClicked();
    void onContentToggled(bool checked);

private:
    void renderZ80Heatmap();
    void renderAllPageTiles();
    void fetchMemoryState();
    void fetchPageContent();

    static QString labelForPage(int pageIndex, const QMap<int, QString>& romDesc);

    // SHM
    void* _shmBase = nullptr;
    size_t _shmSize = 0;
    QTimer* _refreshTimer = nullptr;

    // Z80 heatmap
    QImage _z80Image;

    // UI
    QLabel* _infoLabel = nullptr;
    QComboBox* _modeCombo = nullptr;
    QCheckBox* _contentCheck = nullptr;
    QPushButton* _captureBtn = nullptr;
    QPushButton* _resetBtn = nullptr;

    QScrollArea* _scrollArea = nullptr;
    QWidget* _scrollContent = nullptr;
    QLabel* _z80ImageLabel = nullptr;
    QLabel* _z80BankInfo = nullptr;
    QLabel* _romTitle = nullptr;
    QLabel* _ramTitle = nullptr;
    QWidget* _romGridWidget = nullptr;
    QGridLayout* _romGrid = nullptr;
    QWidget* _ramGridWidget = nullptr;
    QGridLayout* _ramGrid = nullptr;

    // Dynamic page tiles (keyed by abs page index)
    QMap<int, PageTileWidget*> _pageTiles;

    // Page-level counters from HEATMAP_PAGES
    static constexpr uint16_t PAGE_COUNT = 323;
    uint32_t _pageReadCounters[PAGE_COUNT] = {};
    uint32_t _pageWriteCounters[PAGE_COUNT] = {};
    uint32_t _pageExecCounters[PAGE_COUNT] = {};

    // Bank mapping from WebAPI
    struct BankMappingEntry { bool isRom; int page; };
    BankMappingEntry _bankMapping[4] = {};
    bool _hasBankMapping = false;
    int _bankPollCounter = 0;

    // HEATMAP_PHYS per-page per-address data
    struct PhysPageData
    {
        uint16_t absPageIndex;
        uint8_t  pageType;
        uint8_t  logicalPageNum;
        bool     isMapped;
        int8_t   mappedBank;
        uint32_t readCounters[16384];
        uint32_t writeCounters[16384];
        uint32_t execCounters[16384];
    };
    QVector<PhysPageData> _physPages;
    bool _hasPhysData = false;

    // Z80 counter data
    uint32_t _readCounters[65536] = {};
    uint32_t _writeCounters[65536] = {};
    uint32_t _execCounters[65536] = {};

    Mode _mode = Combined;
    bool _hasData = false;
    bool _showContent = false;
    bool _contentFetchPending = false;
    int _contentPollCounter = 0;
    QByteArray _z80Content;  // 64KB Z80 address space content for overview

    QMap<int, QString> _romDescriptions;
    static constexpr uint16_t FIRST_ROM = 259;

    WebAPIClient* _apiClient = nullptr;
    QString _emulatorId;
    bool _featureEnabled = false;
    bool _pendingApiCall = false;
    uint64_t _lastResetEpoch = 0;

    uint32_t _frameCounter = 0;
    bool _fifoActive = false;

    static constexpr int REFRESH_MS = 200;
    static constexpr int FALLBACK_MS = 500;
    static constexpr int FRAME_DIVISOR = 10;
    static constexpr int Z80_DISPLAY = 512;
    static constexpr int TILE_COLS = 3;
};
