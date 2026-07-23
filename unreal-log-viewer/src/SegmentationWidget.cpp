#include "SegmentationWidget.h"
#include "WebAPIClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QScrollArea>
#include <QFrame>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QMessageBox>
#include <QDialog>
#include <QTableWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <QHelpEvent>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

#include <cstring>
#include <algorithm>

#include "emulator/monitoring/manifest.h"

using namespace monitoring;

// ============================================================================
// Color helpers (static, shared by model and widget)
// ============================================================================

static QColor colorForBlockTypeStatic(uint8_t bt)
{
    switch (bt) {
        case 1: return QColor(0x4c, 0xaf, 0x50);  // CODE    — green
        case 2: return QColor(0x21, 0x96, 0xf3);  // DATA    — blue
        case 3: return QColor(0x00, 0xbc, 0xd4);  // VARIABLE— cyan
        case 4: return QColor(0xff, 0x98, 0x00);  // SMC     — amber
        case 5: return QColor(0xf4, 0x43, 0x36);  // STACK   — red
        default: return QColor(0x42, 0x42, 0x42); // UNKNOWN — dark grey
    }
}

static const char* kTypeNames[] = { "UNKNOWN","CODE","DATA","VARIABLE","SMC","STACK" };

// ============================================================================
// RegionTableModel
// ============================================================================

RegionTableModel::RegionTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void RegionTableModel::setRegions(QVector<RegionEntry> regions)
{
    beginResetModel();
    _regions = std::move(regions);
    endResetModel();
}

int RegionTableModel::rowCount(const QModelIndex&) const { return _regions.size(); }
int RegionTableModel::columnCount(const QModelIndex&) const { return 5; }

QVariant RegionTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const char* kHdr[] = { "Start", "End", "Type", "Page", "Tags" };
    return (section >= 0 && section < 5) ? kHdr[section] : QVariant{};
}

QVariant RegionTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= _regions.size()) return {};
    const auto& r = _regions[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return QString("$%1").arg(r.startAddress, 4, 16, QChar('0')).toUpper();
            case 1: return QString("$%1").arg(r.endAddress,   4, 16, QChar('0')).toUpper();
            case 2: return (r.blockType < 6) ? kTypeNames[r.blockType] : "???";
            case 3: return pageLabel(r.pageType, r.pageIndex);
            case 4: return tagsLabel(r.tags);
        }
    }
    if (role == Qt::ForegroundRole) {
        if (index.column() == 2) return colorForBlockTypeStatic(r.blockType);
        if (index.column() == 4) return QColor(0xaa, 0xaa, 0xaa);
    }
    return {};
}

QColor RegionTableModel::colorForBlockType(uint8_t bt) { return colorForBlockTypeStatic(bt); }

QString RegionTableModel::pageLabel(uint8_t pageType, uint16_t pageIndex)
{
    if (pageIndex == 0xFFFF) return "Z80";
    switch (pageType) {
        case 0: return QString("RAM %1").arg(pageIndex);
        case 1: return QString("ROM %1").arg(pageIndex);
        case 2: return QString("Cache %1").arg(pageIndex);
        case 3: return QString("Misc %1").arg(pageIndex);
        default: return QString("Page %1").arg(pageIndex);
    }
}

QString RegionTableModel::tagsLabel(uint32_t tags)
{
    if (tags == 0) return "(none)";
    static const struct { uint32_t bit; const char* name; } kTags[] = {
        { 1u<< 0,"GenericCode"}, { 1u<< 1,"GenericData"}, { 1u<< 2,"SysVars"},
        { 1u<< 3,"Stack"},      { 1u<< 4,"ScreenBitmap"},{ 1u<< 5,"ScreenAttr"},
        { 1u<< 6,"GfxCode"},   { 1u<< 7,"ImageData"},   { 1u<< 8,"Music"},
        { 1u<< 9,"MusicData"}, { 1u<<10,"Beeper"},       { 1u<<11,"Covox"},
        { 1u<<12,"Loader"},    { 1u<<13,"Tape"},          { 1u<<14,"Compressed"},
        { 1u<<15,"Decomp"},    { 1u<<16,"SMCOut"},        { 1u<<17,"SMCPatch"},
        { 1u<<18,"SMCTarget"}, { 1u<<23,"ISR"},           { 1u<<24,"IM2Table"},
        { 1u<<25,"TRDOSWork"},
    };
    QStringList names;
    for (const auto& t : kTags)
        if (tags & t.bit) names << t.name;
    return names.join(", ");
}

// ============================================================================
// Constructor
// ============================================================================

SegmentationWidget::SegmentationWidget(QWidget* parent) : QWidget(parent)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(4, 4, 4, 4);
    outerLayout->setSpacing(4);

    // — Toolbar —
    auto* toolbar = new QHBoxLayout();
    _analyzeBtn = new QPushButton("Analyze", this);
    _analyzeBtn->setCheckable(true);
    _analyzeBtn->setToolTip("Enable/disable memory region segmentation analyzer");
    connect(_analyzeBtn, &QPushButton::clicked, this, &SegmentationWidget::onAnalyzeToggled);
    toolbar->addWidget(_analyzeBtn);

    _resetBtn = new QPushButton("Reset", this);
    _resetBtn->setToolTip("Clear accumulated data and force re-analysis");
    _resetBtn->setEnabled(false);
    connect(_resetBtn, &QPushButton::clicked, this, &SegmentationWidget::onResetClicked);
    toolbar->addWidget(_resetBtn);

    toolbar->addStretch();
    _frameLabel = new QLabel("", this);
    _frameLabel->setFont(QFont("Menlo", 9));
    _frameLabel->setStyleSheet("color: #888;");
    toolbar->addWidget(_frameLabel);
    outerLayout->addLayout(toolbar);

    // — Behavior warning banner —
    _warningLabel = new QLabel("⚠  Behavior changing — regions are being re-analyzed", this);
    _warningLabel->setStyleSheet("background: #7f4000; color: #ffcc80; padding: 4px 8px; border-radius: 2px;");
    _warningLabel->setVisible(false);
    outerLayout->addWidget(_warningLabel);

    // — Z80 memory map strip —
    auto* mapTitle = new QLabel("Z80 Memory Map", this);
    mapTitle->setStyleSheet("font-weight: bold; font-size: 12px;");
    outerLayout->addWidget(mapTitle);

    _mapImageLabel = new QLabel(this);
    _mapImageLabel->setMinimumHeight(MAP_HEIGHT);
    _mapImageLabel->setMaximumHeight(MAP_HEIGHT * 2);
    _mapImageLabel->setScaledContents(true);
    _mapImageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _mapImageLabel->setMinimumWidth(0);
    _mapImageLabel->setStyleSheet("border: 1px solid #555;");
    _mapImageLabel->setToolTip("");            // no static tip — dynamic only
    _mapImageLabel->setCursor(Qt::PointingHandCursor);
    _mapImageLabel->setMouseTracking(true);    // needed for MouseMove without button held
    _mapImageLabel->installEventFilter(this);

    auto* legend = new QLabel(
        "<span style='color:#4caf50'>■ CODE</span>  "
        "<span style='color:#2196f3'>■ DATA</span>  "
        "<span style='color:#00bcd4'>■ VARIABLE</span>  "
        "<span style='color:#ff9800'>■ SMC</span>  "
        "<span style='color:#f44336'>■ STACK</span>  "
        "<span style='color:#888'>■ UNKNOWN</span>", this);
    legend->setFont(QFont("Menlo", 9));
    legend->setTextFormat(Qt::RichText);
    outerLayout->addWidget(_mapImageLabel);
    outerLayout->addWidget(legend);

    // — Separator —
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    outerLayout->addWidget(sep);

    // — Region table (QTableView + model) —
    auto* tableTitle = new QLabel("Regions", this);
    tableTitle->setStyleSheet("font-weight: bold; font-size: 12px;");
    outerLayout->addWidget(tableTitle);

    _model = new RegionTableModel(this);
    _table = new QTableView(this);
    _table->setModel(_model);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setAlternatingRowColors(true);
    _table->setFont(QFont("Menlo", 10));
    _table->verticalHeader()->setDefaultSectionSize(18);
    _table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed); // uniform height = fast scroll
    _table->verticalHeader()->hide();
    _table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    _table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(_table, &QTableView::doubleClicked, this, &SegmentationWidget::onRegionDoubleClicked);
    outerLayout->addWidget(_table, 1);

    // — Status —
    _statusLabel = new QLabel("No segmentation data", this);
    _statusLabel->setStyleSheet("color: #888; font-size: 10px;");
    outerLayout->addWidget(_statusLabel);

    // — Async plumbing —
    // Connect the signal (emitted from bg thread via invokeMethod) to the apply slot
    connect(this, &SegmentationWidget::regionUpdateReady,
            this, &SegmentationWidget::applyRegionUpdate,
            Qt::QueuedConnection);

    // — Timer —
    _refreshTimer = new QTimer(this);
    _refreshTimer->setTimerType(Qt::CoarseTimer); // less precise, less overhead
    connect(_refreshTimer, &QTimer::timeout, this, &SegmentationWidget::onRefresh);
}

// ============================================================================
// API client
// ============================================================================

void SegmentationWidget::setApiClient(WebAPIClient* client, const QString& emulatorId)
{
    _apiClient = client;
    _emulatorId = emulatorId;

    if (!emulatorId.isEmpty()) {
        auto* nam = new QNetworkAccessManager(this);
        QUrl url;
        url.setScheme("http"); url.setHost("localhost"); url.setPort(8090);
        url.setPath(QString("/api/v1/emulator/%1/analyzer/memory-region").arg(emulatorId));
        auto* reply = nam->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
            if (reply->error() == QNetworkReply::NoError) {
                auto doc = QJsonDocument::fromJson(reply->readAll());
                _analyzerActive = doc.object()["enabled"].toBool();
                _analyzeBtn->blockSignals(true);
                _analyzeBtn->setChecked(_analyzerActive);
                _analyzeBtn->setText(_analyzerActive ? "Analyzing…" : "Analyze");
                _analyzeBtn->blockSignals(false);
                _resetBtn->setEnabled(_analyzerActive);
            }
            reply->deleteLater(); nam->deleteLater();
        });
    }
}

// ============================================================================
// SHM attach / detach
// ============================================================================

void SegmentationWidget::attachToShm(void* base, size_t size)
{
    _shmBase = base; _shmSize = size;
    _refreshTimer->start(REFRESH_MS);
}

void SegmentationWidget::detachFromShm()
{
    _refreshTimer->stop();
    _shmBase = nullptr; _shmSize = 0;
    _hasData = false;
    _model->setRegions({});
    _mapImageLabel->clear();
    _warningLabel->setVisible(false);
    _frameLabel->clear();
    _statusLabel->setText("No segmentation data");
}

void SegmentationWidget::onFrameReady()
{
    static int _counter = 0;
    if (++_counter % FRAME_DIVISOR != 0) return;
    onRefresh();
}

void SegmentationWidget::setFifoActive(bool active)
{
    if (_shmBase)
        _refreshTimer->start(active ? FALLBACK_MS : REFRESH_MS);
}

// ============================================================================
// SHM read — fires async task, never blocks the event loop
// ============================================================================

void SegmentationWidget::onRefresh()
{
    if (!_shmBase) return;
    // If a previous read is still in flight, skip — don't pile up work
    if (_shmReadPending.exchange(true)) return;

    void* shmBase = _shmBase;   // safe: only nulled on detach (main thread)

    // Capture everything the background lambda needs by value
    uint32_t lastFrame    = _refreshFrame;
    bool     lastChanging = _behaviorChanging;
    bool     hasData      = _hasData;

    auto* watcher = new QFutureWatcher<void>(this);

    // Run SHM parse on the thread pool — NO Qt calls here
    auto future = QtConcurrent::run([this, shmBase, lastFrame, lastChanging, hasData]()
    {
        auto* manifest = static_cast<ManifestHeader*>(shmBase);
        for (uint16_t i = 0; i < manifest->section_count; i++)
        {
            auto* desc = getDescriptor(shmBase, manifest, i);
            if (!desc || desc->type != SectionType::SEGMENTATION_MAP) continue;

            uint64_t before = desc->epoch.load(std::memory_order_acquire);
            if (before == EPOCH_UPDATING) continue;

            auto* data = static_cast<const uint8_t*>(getSectionData(shmBase, desc));
            if (!data) continue;

            auto* hdr = reinterpret_cast<const SegmentationMapHeader*>(data);
            uint16_t count = std::min(hdr->region_count, hdr->max_regions);
            bool changing  = (hdr->flags & SEGMAP_BEHAVIOR_CHANGED) != 0;

            if (hdr->refresh_frame == lastFrame && changing == lastChanging && hasData)
                break;  // nothing new

            QVector<RegionEntry> regions;
            regions.reserve(count);
            const auto* src = reinterpret_cast<const SegmentationRegionPOD*>(
                data + sizeof(SegmentationMapHeader));
            for (uint16_t r = 0; r < count; r++)
            {
                RegionEntry e;
                e.startAddress = src[r].start_address;
                e.endAddress   = src[r].end_address;
                e.blockType    = src[r].block_type;
                e.pageType     = src[r].page_type;
                e.pageIndex    = src[r].page_index;
                e.tags         = src[r].tags;
                regions.append(e);
            }

            std::atomic_thread_fence(std::memory_order_acquire);
            uint64_t after = desc->epoch.load(std::memory_order_relaxed);
            if (before != after) break; // torn — skip this cycle

            bool active = (count > 0 || hdr->refresh_frame > 0);
            uint32_t frame = hdr->refresh_frame;

            // Post to main thread via queued signal
            emit regionUpdateReady(std::move(regions), frame, changing, active);
            break;
        }
        _shmReadPending.store(false);
    });

    watcher->setFuture(future);
    // Auto-clean the watcher when done
    connect(watcher, &QFutureWatcher<void>::finished, watcher, &QObject::deleteLater);
}

// ============================================================================
// Apply update on main thread
// ============================================================================

void SegmentationWidget::applyRegionUpdate(QVector<RegionEntry> regions, uint32_t frame,
                                            bool changing, bool active)
{
    _refreshFrame    = frame;
    _behaviorChanging = changing;
    _analyzerActive  = active;
    _hasData         = true;

    _lastRegions = regions;           // cache for map-click hit-testing
    renderMap(regions);
    _model->setRegions(std::move(regions));

    _warningLabel->setVisible(changing);
    _frameLabel->setText(QString("frame: %1  |  %2 regions").arg(frame).arg(_model->rowCount()));
    _statusLabel->setText(active ? "Analyzer active" : "Analyzer inactive");

    _analyzeBtn->blockSignals(true);
    _analyzeBtn->setChecked(active);
    _analyzeBtn->setText(active ? "Analyzing…" : "Analyze");
    _analyzeBtn->blockSignals(false);
    _resetBtn->setEnabled(active);
}

// ============================================================================
// Render the Z80 address strip — fast scanline fill
// ============================================================================

void SegmentationWidget::renderMap(const QVector<RegionEntry>& regions)
{
    // 1024 logical pixels — well within macOS/Metal texture limits (max 16384).
    // setScaledContents(true) scales this to the actual label width at paint time.
    // Hit-testing uses (mapX * 65536) / labelW independently of this size.
    constexpr int W = 1024;
    _mapImage = QImage(W, MAP_HEIGHT, QImage::Format_RGB32);

    QRgb* row0 = reinterpret_cast<QRgb*>(_mapImage.bits());
    std::fill(row0, row0 + W, qRgb(0x42, 0x42, 0x42));  // UNKNOWN baseline

    for (const auto& r : regions) {
        QRgb rgb = colorForBlockTypeStatic(r.blockType).rgb();
        int startX = (int(r.startAddress) * W) / 65536;
        int endX   = std::min<int>((int(r.endAddress) * W) / 65536, W - 1);
        if (startX <= endX)
            std::fill(row0 + startX, row0 + endX + 1, rgb);
    }

    // White bank dividers at 16KB boundaries
    for (int bank = 1; bank < 4; ++bank) {
        int x = (bank * 0x4000 * W) / 65536;
        if (x < W) row0[x] = qRgb(255, 255, 255);
    }

    // Replicate row 0 across all MAP_HEIGHT rows
    for (int y = 1; y < MAP_HEIGHT; ++y)
        memcpy(_mapImage.scanLine(y), row0, W * sizeof(QRgb));

    _mapImageLabel->setPixmap(QPixmap::fromImage(_mapImage));
    _mapImageLabel->update();
}

// ============================================================================
// Map strip single-click — open region dialog
// ============================================================================

bool SegmentationWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == _mapImageLabel)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton)
                openRegionAt(me->pos().x());
            return true;
        }

        // MouseMove: show tooltip immediately (no dwell-time delay)
        if (event->type() == QEvent::MouseMove)
        {
            auto* me = static_cast<QMouseEvent*>(event);
            showMapTooltip(me->pos().x(), me->globalPosition().toPoint());
            return false;  // don't consume — let Qt process normally too
        }

        // ToolTip: standard hover fallback (catches cases where MouseMove
        // wasn't delivered, e.g. cursor entered after being still)
        if (event->type() == QEvent::ToolTip)
        {
            auto* he = static_cast<QHelpEvent*>(event);
            showMapTooltip(he->pos().x(), he->globalPos());
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void SegmentationWidget::showMapTooltip(int mapX, const QPoint& globalPos)
{
    int labelW = _mapImageLabel->width();
    if (labelW <= 0 || _lastRegions.isEmpty()) { QToolTip::hideText(); return; }

    uint16_t addr = static_cast<uint16_t>(
        std::clamp<int>((mapX * 65536) / labelW, 0, 65535));

    for (const auto& r : _lastRegions)
    {
        if (addr >= r.startAddress && addr <= r.endAddress)
        {
            QString typeName = (r.blockType < 6) ? kTypeNames[r.blockType] : "???";
            QString tags     = RegionTableModel::tagsLabel(r.tags);
            QToolTip::showText(globalPos,
                QString("<b>$%1 – $%2</b> &nbsp;(%3 bytes)<br>"
                        "Type: <b>%4</b><br>Tags: %5")
                    .arg(r.startAddress, 4, 16, QChar('0')).toUpper()
                    .arg(r.endAddress,   4, 16, QChar('0')).toUpper()
                    .arg(r.endAddress - r.startAddress + 1)
                    .arg(typeName)
                    .arg(tags == "(none)" || tags.isEmpty() ? "–" : tags),
                _mapImageLabel, QRect(), 5000);
            return;
        }
    }
    QToolTip::hideText();
}

void SegmentationWidget::openRegionAt(int mapX)
{
    int labelW = _mapImageLabel->width();
    if (labelW <= 0 || _lastRegions.isEmpty()) return;

    uint16_t addr = static_cast<uint16_t>(
        std::clamp<int>((mapX * 65536) / labelW, 0, 65535));

    for (const auto& r : _lastRegions)
    {
        if (addr >= r.startAddress && addr <= r.endAddress)
        {
            // Forward to onRegionDoubleClicked by selecting the row in the table
            // and triggering the same logic — find the matching row in the model
            for (int row = 0; row < _model->rowCount(); ++row)
            {
                auto si = _model->index(row, 0);
                auto st = _model->data(si).toString();  // "$XXXX"
                bool ok;
                uint16_t rowStart = st.mid(1).toUShort(&ok, 16);
                if (ok && rowStart == r.startAddress)
                {
                    _table->selectRow(row);
                    onRegionDoubleClicked(_model->index(row, 0));
                    return;
                }
            }
            return;
        }
    }
}


// ============================================================================
// Start/Stop button
// ============================================================================

void SegmentationWidget::onAnalyzeToggled()
{
    if (_emulatorId.isEmpty() || _pendingApiCall) {
        _analyzeBtn->setChecked(_analyzerActive);
        return;
    }

    bool enable = _analyzeBtn->isChecked();
    _pendingApiCall = true;
    _analyzeBtn->setEnabled(false);

    auto* nam = new QNetworkAccessManager(this);
    QUrl url;
    url.setScheme("http"); url.setHost("localhost"); url.setPort(8090);
    url.setPath(QString("/api/v1/emulator/%1/analyzer/memory-region/session").arg(_emulatorId));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QString body = enable ? "{\"action\": \"activate\"}" : "{\"action\": \"deactivate\"}";
    auto* reply = nam->sendCustomRequest(req, "POST", body.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, enable, reply, nam]() {
        _pendingApiCall = false;
        _analyzeBtn->setEnabled(true);
        if (reply->error() == QNetworkReply::NoError) {
            _analyzerActive = enable;
            _analyzeBtn->setText(enable ? "Analyzing…" : "Analyze");
            _resetBtn->setEnabled(enable);
        } else {
            _analyzeBtn->setChecked(_analyzerActive);
        }
        reply->deleteLater(); nam->deleteLater();
    });
}

// ============================================================================
// Reset button
// ============================================================================

void SegmentationWidget::onResetClicked()
{
    if (_emulatorId.isEmpty() || !_apiClient) return;

    auto* nam = new QNetworkAccessManager(this);
    QUrl url;
    url.setScheme("http"); url.setHost("localhost"); url.setPort(8090);
    url.setPath(QString("/api/v1/emulator/%1/analyzer/memory-region/session").arg(_emulatorId));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = nam->post(req, QByteArray(R"({"action": "reset"})"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        if (reply->error() == QNetworkReply::NoError)
            _statusLabel->setText("Reset — re-analyzing…");
        reply->deleteLater(); nam->deleteLater();
    });
}

// ============================================================================
// Disassembly Dialog — code regions, with history navigation
// ============================================================================

class DisassemblyDialog : public QDialog
{
public:
    DisassemblyDialog(const QString& emulatorId, uint16_t startAddress, uint16_t endAddress, QWidget* parent = nullptr)
        : QDialog(parent), _emulatorId(emulatorId), _regionStart(startAddress), _regionEnd(endAddress)
    {
        setWindowTitle(QString("Disassembly: $%1-$%2").arg(startAddress, 4, 16, QChar('0')).arg(endAddress, 4, 16, QChar('0')));
        resize(450, 480);

        auto* layout = new QVBoxLayout(this);
        _table = new QTableWidget(0, 3, this);
        _table->setHorizontalHeaderLabels({"Address", "Bytes", "Instruction"});
        _table->horizontalHeader()->setStretchLastSection(true);
        _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        _table->setSelectionBehavior(QAbstractItemView::SelectRows);
        _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        _table->setAlternatingRowColors(true);
        _table->setFont(QFont("Menlo", 11));
        _table->verticalHeader()->setDefaultSectionSize(20);
        _table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        _table->verticalHeader()->hide();
        _table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        connect(_table, &QTableWidget::cellDoubleClicked, this, &DisassemblyDialog::onCellDoubleClicked);
        layout->addWidget(_table);

        auto* footer = new QLabel("ESC: back / Double-click jump/call to follow", this);
        footer->setStyleSheet("color: #888; font-size: 10px;");
        layout->addWidget(footer);

        fetchDisassembly(startAddress);
    }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape) {
            if (!_history.isEmpty())
                fetchDisassembly(_history.takeLast(), false);
            else
                QDialog::keyPressEvent(event);
        } else {
            QDialog::keyPressEvent(event);
        }
    }

private:
    void fetchDisassembly(uint16_t startAddr, bool pushHistory = false)
    {
        if (pushHistory) _history.push_back(_currentStartAddr);
        _currentStartAddr = startAddr;

        auto* nam = new QNetworkAccessManager(this);
        QUrl url;
        url.setScheme("http"); url.setHost("localhost"); url.setPort(8090);
        url.setPath(QString("/api/v1/emulator/%1/disasm").arg(_emulatorId));
        QUrlQuery query;
        query.addQueryItem("address", QString("0x%1").arg(startAddr, 4, 16, QChar('0')));
        query.addQueryItem("count", "100");
        url.setQuery(query);

        auto* reply = nam->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
            if (reply->error() == QNetworkReply::NoError) {
                auto doc = QJsonDocument::fromJson(reply->readAll());
                auto instrs = doc.object()["instructions"].toArray();

                _table->setUpdatesEnabled(false);
                // Clear old items without resizing (avoids destroy/create cost)
                _table->clearContents();
                _table->setRowCount(instrs.size());

                int rowIdx = 0;
                for (const auto& v : instrs) {
                    auto obj = v.toObject();
                    uint16_t addrInt = obj["address"].toInt();
                    QString addr  = QString::asprintf("%04X", addrInt);
                    QString bytes = obj["bytes"].toString();
                    QString mnem  = obj["mnemonic"].toString();

                    auto* addrItem  = new QTableWidgetItem(addr);
                    auto* bytesItem = new QTableWidgetItem(bytes);
                    auto* mnemItem  = new QTableWidgetItem(mnem);

                    if (obj.contains("target"))
                        mnemItem->setData(Qt::UserRole, (uint)(obj["target"].toInt()));

                    if (obj.contains("label")) {
                        QString lbl = obj["label"].toString();
                        if (!lbl.isEmpty()) {
                            mnemItem->setText(lbl + ":\n  " + mnem);
                            mnemItem->setForeground(QColor(0x4c, 0xaf, 0x50));
                        }
                    }

                    // Dim instructions outside the region
                    if (addrInt < _regionStart || addrInt > _regionEnd) {
                        QColor dim(120, 120, 120);
                        addrItem->setForeground(dim);
                        bytesItem->setForeground(dim);
                        if (!obj.contains("label") || obj["label"].toString().isEmpty())
                            mnemItem->setForeground(dim);
                    }

                    _table->setItem(rowIdx, 0, addrItem);
                    _table->setItem(rowIdx, 1, bytesItem);
                    _table->setItem(rowIdx, 2, mnemItem);
                    rowIdx++;
                }
                _table->setUpdatesEnabled(true);
            }
            reply->deleteLater(); nam->deleteLater();
        });
    }

    void onCellDoubleClicked(int row, int /*col*/)
    {
        auto* item = _table->item(row, 2);
        if (item) {
            QVariant tv = item->data(Qt::UserRole);
            if (tv.isValid())
                fetchDisassembly(static_cast<uint16_t>(tv.toUInt()), true);
        }
    }

    QString _emulatorId;
    uint16_t _regionStart, _regionEnd, _currentStartAddr = 0;
    QList<uint16_t> _history;
    QTableWidget* _table = nullptr;
};

// ============================================================================
// Data Region Dialog — hex + label view for DATA/VARIABLE/UNKNOWN/STACK
// ============================================================================

class DataRegionDialog : public QDialog
{
public:
    DataRegionDialog(const QString& emulatorId, uint16_t startAddress, uint16_t endAddress,
                     const QString& regionType, QWidget* parent = nullptr)
        : QDialog(parent), _emulatorId(emulatorId), _regionStart(startAddress), _regionEnd(endAddress)
    {
        setWindowTitle(QString("%1 Region: $%2-$%3")
            .arg(regionType)
            .arg(startAddress, 4, 16, QChar('0'))
            .arg(endAddress, 4, 16, QChar('0')));
        resize(580, 480);

        auto* layout = new QVBoxLayout(this);
        auto* info = new QLabel(QString("Address range: $%1 – $%2  |  Size: %3 bytes  |  Type: %4")
            .arg(startAddress, 4, 16, QChar('0'))
            .arg(endAddress, 4, 16, QChar('0'))
            .arg(endAddress - startAddress + 1)
            .arg(regionType), this);
        info->setStyleSheet("font-family: Menlo; font-size: 10px; color: #aaa;");
        layout->addWidget(info);

        _table = new QTableWidget(0, 4, this);
        _table->setHorizontalHeaderLabels({"Address", "Hex (8 bytes)", "ASCII", "Label"});
        _table->horizontalHeader()->setStretchLastSection(true);
        _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        _table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        _table->setColumnWidth(1, 200);
        _table->setSelectionBehavior(QAbstractItemView::SelectRows);
        _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        _table->setAlternatingRowColors(true);
        _table->setFont(QFont("Menlo", 11));
        _table->verticalHeader()->setDefaultSectionSize(20);
        _table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        _table->verticalHeader()->hide();
        _table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        layout->addWidget(_table, 1);

        auto* footer = new QLabel("Rows outside region are grayed (context).  ESC to close.", this);
        footer->setStyleSheet("color: #888; font-size: 10px;");
        layout->addWidget(footer);

        // Fetch 32 bytes context before + region + 32 after
        uint16_t fetchStart = (startAddress >= 32) ? startAddress - 32 : 0;
        uint16_t fetchEnd   = std::min<int>(endAddress + 32, 0xFFFF);
        uint16_t fetchLen   = fetchEnd - fetchStart + 1;

        auto* nam1 = new QNetworkAccessManager(this);
        QUrl url1;
        url1.setScheme("http"); url1.setHost("localhost"); url1.setPort(8090);
        url1.setPath(QString("/api/v1/emulator/%1/memory/read/%2").arg(_emulatorId).arg(fetchStart));
        QUrlQuery q1;
        q1.addQueryItem("length", QString::number(fetchLen));
        url1.setQuery(q1);

        auto* reply1 = nam1->get(QNetworkRequest(url1));
        connect(reply1, &QNetworkReply::finished, this, [this, reply1, nam1, fetchStart]() {
            if (reply1->error() == QNetworkReply::NoError) {
                auto doc = QJsonDocument::fromJson(reply1->readAll());
                auto dataArr = doc.object()["data"].toArray();
                QVector<uint8_t> bytes;
                bytes.reserve(dataArr.size());
                for (const auto& v : dataArr) bytes.push_back(static_cast<uint8_t>(v.toInt()));
                _bytes = std::move(bytes);
                _fetchStart = fetchStart;
                populateTable();
            }
            reply1->deleteLater(); nam1->deleteLater();
        });
    }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape) accept();
        else QDialog::keyPressEvent(event);
    }

private:
    void populateTable()
    {
        static constexpr int COLS = 8;
        int numRows = (_bytes.size() + COLS - 1) / COLS;
        _table->setUpdatesEnabled(false);
        _table->setRowCount(numRows);

        for (int r = 0; r < numRows; r++) {
            uint16_t rowAddr = _fetchStart + r * COLS;
            // A row is "in region" if ANY byte in it overlaps
            bool inRegion = (static_cast<int>(rowAddr + COLS - 1) >= _regionStart)
                         && (rowAddr <= _regionEnd);

            QString hexStr, asciiStr;
            for (int c = 0; c < COLS; c++) {
                int idx = r * COLS + c;
                if (idx < _bytes.size()) {
                    uint8_t b = _bytes[idx];
                    hexStr  += QString::asprintf("%02X ", b);
                    asciiStr += (b >= 0x20 && b < 0x7F) ? QChar(b) : QChar('.');
                } else { hexStr += "   "; asciiStr += " "; }
            }

            auto* addrItem  = new QTableWidgetItem(QString::asprintf("%04X", rowAddr));
            auto* hexItem   = new QTableWidgetItem(hexStr.trimmed());
            auto* asciiItem = new QTableWidgetItem(asciiStr);
            auto* labelItem = new QTableWidgetItem();

            if (!inRegion) {
                QColor dim(110, 110, 110);
                addrItem->setForeground(dim);
                hexItem->setForeground(dim);
                asciiItem->setForeground(dim);
                labelItem->setForeground(dim);
            }

            _table->setItem(r, 0, addrItem);
            _table->setItem(r, 1, hexItem);
            _table->setItem(r, 2, asciiItem);
            _table->setItem(r, 3, labelItem);
        }
        _table->setUpdatesEnabled(true);
        fetchLabels();
    }

    void fetchLabels()
    {
        auto* nam = new QNetworkAccessManager(this);
        QUrl url;
        url.setScheme("http"); url.setHost("localhost"); url.setPort(8090);
        url.setPath(QString("/api/v1/emulator/%1/disasm").arg(_emulatorId));
        QUrlQuery q;
        q.addQueryItem("address", QString("0x%1").arg(_fetchStart, 4, 16, QChar('0')));
        q.addQueryItem("count", "100");
        url.setQuery(q);
        auto* reply = nam->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
            if (reply->error() == QNetworkReply::NoError) {
                auto doc = QJsonDocument::fromJson(reply->readAll());
                for (const auto& v : doc.object()["instructions"].toArray()) {
                    auto obj = v.toObject();
                    if (!obj.contains("label")) continue;
                    QString lbl = obj["label"].toString();
                    if (lbl.isEmpty()) continue;
                    uint16_t addr = obj["address"].toInt();
                    int row = (addr - _fetchStart) / 8;
                    if (row >= 0 && row < _table->rowCount()) {
                        auto* item = _table->item(row, 3);
                        if (item) {
                            item->setText(item->text().isEmpty() ? lbl : item->text() + ", " + lbl);
                            item->setForeground(QColor(0x4c, 0xaf, 0x50));
                        }
                    }
                }
            }
            reply->deleteLater(); nam->deleteLater();
        });
    }

    QString _emulatorId;
    uint16_t _regionStart, _regionEnd, _fetchStart = 0;
    QVector<uint8_t> _bytes;
    QTableWidget* _table = nullptr;
};

// ============================================================================
// Double click routing — CODE/SMC → disassembly, else → hex/data
// ============================================================================

static inline bool blockTypeIsCode(uint8_t bt) { return bt == 1 || bt == 4; }

void SegmentationWidget::onRegionDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;

    // Get the region from the model
    int row = index.row();
    // DataRole returns the text; reconstruct the RegionEntry from model
    // Easiest: keep a const-ref copy inside the model
    // We do this by storing regions in model and having a getter
    // But we passed QVector by move — use a const accessor:
    // Since model is our class, we can add a getter inline:
    auto startText = _model->data(_model->index(row, 0), Qt::DisplayRole).toString();
    auto endText   = _model->data(_model->index(row, 1), Qt::DisplayRole).toString();
    auto typeText  = _model->data(_model->index(row, 2), Qt::DisplayRole).toString();

    bool ok1, ok2;
    uint16_t startAddr = startText.mid(1).toUShort(&ok1, 16);
    uint16_t endAddr   = endText.mid(1).toUShort(&ok2, 16);
    if (!ok1 || !ok2) return;

    // Determine blockType from type text
    uint8_t bt = 0;
    for (uint8_t i = 0; i < 6; i++)
        if (typeText == kTypeNames[i]) { bt = i; break; }

    if (blockTypeIsCode(bt)) {
        auto* d = new DisassemblyDialog(_emulatorId, startAddr, endAddr, this);
        d->exec(); d->deleteLater();
    } else {
        auto* d = new DataRegionDialog(_emulatorId, startAddr, endAddr, typeText, this);
        d->exec(); d->deleteLater();
    }
}

