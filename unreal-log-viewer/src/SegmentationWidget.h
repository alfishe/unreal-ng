#pragma once

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QAbstractTableModel>
#include <QVector>
#include <atomic>

class WebAPIClient;
class QNetworkAccessManager;
class QNetworkReply;
class QVBoxLayout;

// ============================================================================
// Lightweight table model — zero heap allocation per row during scroll
// ============================================================================
struct RegionEntry
{
    uint16_t startAddress;
    uint16_t endAddress;
    uint8_t  blockType;
    uint8_t  pageType;
    uint16_t pageIndex;
    uint32_t tags;
};

class RegionTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit RegionTableModel(QObject* parent = nullptr);

    void setRegions(QVector<RegionEntry> regions);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    static QColor colorForBlockType(uint8_t bt);
    static QString pageLabel(uint8_t pageType, uint16_t pageIndex);
    static QString tagsLabel(uint32_t tags);  // public: used by tooltip code

private:
    QVector<RegionEntry> _regions;
};

// ============================================================================
// SegmentationWidget
// ============================================================================

/// Live viewer for the SEGMENTATION_MAP SHM section.
/// Shows the Z80 address space colored by BlockType and a sortable region list.
class SegmentationWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentationWidget(QWidget* parent = nullptr);

    void setApiClient(WebAPIClient* client, const QString& emulatorId);

public slots:
    void attachToShm(void* base, size_t size);
    void detachFromShm();
    void onFrameReady();
    void setFifoActive(bool active);

private slots:
    void onRefresh();
    void onAnalyzeToggled();
    void onResetClicked();
    void onRegionDoubleClicked(const QModelIndex& index);

    // Called from background thread result (via queued signal)
    void applyRegionUpdate(QVector<RegionEntry> regions, uint32_t frame,
                           bool changing, bool active);

signals:
    void regionUpdateReady(QVector<RegionEntry> regions, uint32_t frame,
                           bool changing, bool active);

private:
    void renderMap(const QVector<RegionEntry>& regions);
    void openRegionAt(int mapX);
    void showMapTooltip(int mapX, const QPoint& globalPos);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

    // SHM
    void*  _shmBase = nullptr;
    size_t _shmSize = 0;
    QTimer* _refreshTimer = nullptr;
    std::atomic<bool> _shmReadPending{false};

    // Cached state
    uint32_t _refreshFrame = 0;
    bool _behaviorChanging = false;
    bool _hasData = false;
    bool _analyzerActive = false;

    // Model
    RegionTableModel* _model = nullptr;

    // UI
    QPushButton* _analyzeBtn = nullptr;
    QPushButton* _resetBtn   = nullptr;
    QLabel*      _frameLabel = nullptr;
    QLabel*      _warningLabel = nullptr;
    QLabel*      _mapImageLabel = nullptr;
    QTableView*  _table = nullptr;
    QLabel*      _statusLabel = nullptr;
    QImage       _mapImage;
    QVector<RegionEntry> _lastRegions;       // cached for map click hit-testing

    // WebAPI
    WebAPIClient* _apiClient = nullptr;
    QString       _emulatorId;
    bool          _pendingApiCall = false;

    static constexpr int REFRESH_MS    = 500;
    static constexpr int FALLBACK_MS   = 1000;
    static constexpr int FRAME_DIVISOR = 10;
    static constexpr int MAP_HEIGHT    = 24;
};
