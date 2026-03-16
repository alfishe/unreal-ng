#include "HeatmapWidget.h"
#include "WebAPIClient.h"

#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QResizeEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>

#include <cmath>
#include <cstring>
#include <algorithm>

#include "emulator/monitoring/manifest.h"

using namespace monitoring;

// =============================================================================
// Color helpers — matching calltrace-viz: Read=BLUE, Write=RED, Execute=GREEN
// =============================================================================

static int logNorm(uint32_t v, double logMax)
{
    if (v == 0) return 0;
    return std::min(255, static_cast<int>(255.0 * std::log(v + 1.0) / logMax));
}

static QRgb pixelForMode(HeatmapWidget::Mode mode, uint32_t rv, uint32_t wv, uint32_t xv, double logMax)
{
    if (mode == HeatmapWidget::Combined)
    {
        // R=write(red), G=execute(green), B=read(blue) — matches calltrace-viz palette
        return qRgb(logNorm(wv, logMax), logNorm(xv, logMax), logNorm(rv, logMax));
    }
    uint32_t v = 0;
    switch (mode)
    {
        case HeatmapWidget::Read:    v = rv; break;
        case HeatmapWidget::Write:   v = wv; break;
        case HeatmapWidget::Execute: v = xv; break;
        default: break;
    }
    int intensity = logNorm(v, logMax);
    switch (mode)
    {
        case HeatmapWidget::Read:    return qRgb(intensity / 4, intensity / 3, intensity);  // blue
        case HeatmapWidget::Write:   return qRgb(intensity, intensity / 4, intensity / 4);  // red
        case HeatmapWidget::Execute: return qRgb(intensity / 4, intensity, intensity / 3);  // green
        default: return qRgb(0, 0, 0);
    }
}

// =============================================================================
// PageTileWidget — 128×128 heatmap
// =============================================================================

PageTileWidget::PageTileWidget(int id, const QString& label, QWidget* parent)
    : QWidget(parent), _tileId(id)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    _titleLabel = new QLabel(label, this);
    _titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    _titleLabel->setFont(QFont("Menlo", 10, QFont::Bold));
    _titleLabel->setWordWrap(true);
    layout->addWidget(_titleLabel);

    _imageLabel = new QLabel(this);
    _imageLabel->setAlignment(Qt::AlignCenter);
    _imageLabel->setMinimumSize(64, 64);
    _imageLabel->setScaledContents(true);
    _imageLabel->setStyleSheet("border: 1px solid #555;");
    layout->addWidget(_imageLabel, 1);
}

void PageTileWidget::setLabel(const QString& label) { _titleLabel->setText(label); }

void PageTileWidget::setContentBytes(const QByteArray& bytes) { _contentBytes = bytes; }

void PageTileWidget::renderFromCounters(const uint32_t* rd, const uint32_t* wr, const uint32_t* ex,
                                         int count, int mode, bool showContent)
{
    constexpr int W = 128, H = 128;
    auto m = static_cast<HeatmapWidget::Mode>(mode);

    QImage img(W, H, QImage::Format_RGB32);

    // Base layer: memory content as gamma-corrected gray gradient (like calltrace-viz)
    if (showContent && _contentBytes.size() >= count)
    {
        const uint8_t* mem = reinterpret_cast<const uint8_t*>(_contentBytes.constData());
        constexpr double gamma = 1.8;
        for (int a = 0; a < std::min(count, W * H); a++)
        {
            // Gamma-corrected phosphor tint (slightly warm, like CRT)
            double v = std::pow(mem[a] / 255.0, 1.0 / gamma);
            int g = static_cast<int>(v * 255.0);
            int x = a & 0x7F;
            int y = a >> 7;
            img.setPixel(x, y, qRgb(g, g, static_cast<int>(g * 0.95)));
        }
    }
    else
    {
        img.fill(Qt::black);
    }

    // Counter overlay: additive blend on top of base
    uint32_t maxVal = 1;
    for (int a = 0; a < count; a++)
    {
        if (m == HeatmapWidget::Read || m == HeatmapWidget::Combined) maxVal = std::max(maxVal, rd[a]);
        if (m == HeatmapWidget::Write || m == HeatmapWidget::Combined) maxVal = std::max(maxVal, wr[a]);
        if (m == HeatmapWidget::Execute || m == HeatmapWidget::Combined) maxVal = std::max(maxVal, ex[a]);
    }
    double logMax = std::log(static_cast<double>(maxVal) + 1.0);

    int pixels = std::min(count, W * H);
    for (int a = 0; a < pixels; a++)
    {
        int x = a & 0x7F;
        int y = a >> 7;
        QRgb overlay = pixelForMode(m, rd[a], wr[a], ex[a], logMax);
        if (showContent && _contentBytes.size() >= count)
        {
            // Additive blend: base + overlay
            QRgb base = img.pixel(x, y);
            int r = std::min(255, qRed(base) + qRed(overlay));
            int g = std::min(255, qGreen(base) + qGreen(overlay));
            int b = std::min(255, qBlue(base) + qBlue(overlay));
            img.setPixel(x, y, qRgb(r, g, b));
        }
        else
        {
            img.setPixel(x, y, overlay);
        }
    }

    _lastImage = img;
    _imageLabel->setPixmap(QPixmap::fromImage(img));
}

void PageTileWidget::renderBlank()
{
    QImage img(128, 128, QImage::Format_RGB32);
    img.fill(Qt::black);
    _lastImage = img;
    _imageLabel->setPixmap(QPixmap::fromImage(img));
}

void PageTileWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // Proportional margins: 5% on each side, 90% for content
    int w = width();
    int margin = std::max(2, w / 20);  // 5% each side, minimum 2px
    layout()->setContentsMargins(margin, 2, margin, margin);
    // Force square: image height = image width
    int imgW = _imageLabel->width();
    if (imgW > 0)
        _imageLabel->setFixedHeight(imgW);
}

void PageTileWidget::updatePixmap()
{
    // setScaledContents handles scaling — just re-set the pixmap
    if (!_lastImage.isNull())
        _imageLabel->setPixmap(QPixmap::fromImage(_lastImage));
}

// =============================================================================
// Page labeling
// =============================================================================

QString HeatmapWidget::labelForPage(int pageIndex, const QMap<int, QString>& romDesc)
{
    if (pageIndex < 256)
        return QString("RAM %1").arg(pageIndex);
    if (pageIndex < 258)
        return QString("Cache %1").arg(pageIndex - 256);
    if (pageIndex == 258)
        return "Misc";
    int romPage = pageIndex - 259;
    QString desc = romDesc.value(romPage);
    if (!desc.isEmpty())
        return QString("ROM %1 \u2014 %2").arg(romPage).arg(desc);
    return QString("ROM %1").arg(romPage);
}

// =============================================================================
// HeatmapWidget constructor
// =============================================================================

HeatmapWidget::HeatmapWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(4, 4, 4, 4);
    outerLayout->setSpacing(4);

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel("Mode:", this));

    _modeCombo = new QComboBox(this);
    _modeCombo->addItems({"Read", "Write", "Execute", "Combined (RGB)"});
    _modeCombo->setCurrentIndex(3);
    connect(_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HeatmapWidget::onModeChanged);
    controls->addWidget(_modeCombo);

    _contentCheck = new QCheckBox("Content", this);
    _contentCheck->setToolTip("Show memory content as gray base layer");
    connect(_contentCheck, &QCheckBox::toggled, this, &HeatmapWidget::onContentToggled);
    controls->addWidget(_contentCheck);

    controls->addStretch();

    _captureBtn = new QPushButton("Capture", this);
    _captureBtn->setCheckable(true);
    connect(_captureBtn, &QPushButton::clicked, this, &HeatmapWidget::onCaptureToggled);
    controls->addWidget(_captureBtn);

    _resetBtn = new QPushButton("Reset", this);
    connect(_resetBtn, &QPushButton::clicked, this, &HeatmapWidget::onResetClicked);
    controls->addWidget(_resetBtn);

    outerLayout->addLayout(controls);

    _infoLabel = new QLabel("No heatmap data", this);
    outerLayout->addWidget(_infoLabel);

    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    _scrollContent = new QWidget();
    auto* scrollLayout = new QVBoxLayout(_scrollContent);
    scrollLayout->setContentsMargins(4, 4, 4, 4);
    scrollLayout->setSpacing(8);

    // Z80 overview
    auto* z80Title = new QLabel("Z80 Address Space (64KB)");
    z80Title->setStyleSheet("font-weight: bold; font-size: 13px;");
    scrollLayout->addWidget(z80Title);

    _z80ImageLabel = new QLabel();
    _z80ImageLabel->setAlignment(Qt::AlignCenter);
    scrollLayout->addWidget(_z80ImageLabel);

    _z80BankInfo = new QLabel("Bank 0: $0000\u2013$3FFF  |  Bank 1: $4000\u2013$7FFF  |  "
                              "Bank 2: $8000\u2013$BFFF  |  Bank 3: $C000\u2013$FFFF");
    _z80BankInfo->setFont(QFont("Menlo", 9));
    _z80BankInfo->setAlignment(Qt::AlignCenter);
    _z80BankInfo->setStyleSheet("color: #888;");
    scrollLayout->addWidget(_z80BankInfo);

    // ROM pages section
    auto* sep1 = new QFrame();
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Sunken);
    scrollLayout->addWidget(sep1);

    _romTitle = new QLabel("Active ROM Pages");
    _romTitle->setStyleSheet("font-weight: bold; font-size: 13px;");
    scrollLayout->addWidget(_romTitle);

    _romGridWidget = new QWidget();
    _romGrid = new QGridLayout(_romGridWidget);
    _romGrid->setContentsMargins(0, 0, 0, 0);
    _romGrid->setSpacing(8);
    scrollLayout->addWidget(_romGridWidget);

    // RAM pages section
    auto* sep2 = new QFrame();
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFrameShadow(QFrame::Sunken);
    scrollLayout->addWidget(sep2);

    _ramTitle = new QLabel("Active RAM Pages");
    _ramTitle->setStyleSheet("font-weight: bold; font-size: 13px;");
    scrollLayout->addWidget(_ramTitle);

    _ramGridWidget = new QWidget();
    _ramGrid = new QGridLayout(_ramGridWidget);
    _ramGrid->setContentsMargins(0, 0, 0, 0);
    _ramGrid->setSpacing(8);
    scrollLayout->addWidget(_ramGridWidget);

    scrollLayout->addStretch();

    _scrollArea->setWidget(_scrollContent);
    outerLayout->addWidget(_scrollArea, 1);

    _refreshTimer = new QTimer(this);
    connect(_refreshTimer, &QTimer::timeout, this, &HeatmapWidget::onRefresh);
}

// =============================================================================
// API Client — fetch bank mapping + ROM descriptions
// =============================================================================

void HeatmapWidget::setApiClient(WebAPIClient* client, const QString& emulatorId)
{
    _apiClient = client;
    _emulatorId = emulatorId;
    fetchMemoryState();

    if (!emulatorId.isEmpty())
    {
        auto* nam = new QNetworkAccessManager(this);
        QUrl url;
        url.setScheme("http"); url.setHost("localhost"); url.setPort(8090);
        url.setPath(QString("/api/v1/emulator/%1/profiler/memory/status").arg(emulatorId));
        QNetworkRequest req(url);
        auto* reply = nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
            if (reply->error() == QNetworkReply::NoError)
            {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                bool capturing = doc.object()["capturing"].toBool();
                _featureEnabled = capturing;
                _captureBtn->blockSignals(true);
                _captureBtn->setChecked(capturing);
                _captureBtn->blockSignals(false);
                _captureBtn->setText(capturing ? "Capturing\u2026" : "Capture");
            }
            reply->deleteLater(); nam->deleteLater();
        });
    }
}

void HeatmapWidget::fetchMemoryState()
{
    if (_emulatorId.isEmpty()) return;

    // Fetch ROM descriptions
    {
        auto* nam = new QNetworkAccessManager(this);
        QUrl url;
        url.setScheme("http"); url.setHost("localhost"); url.setPort(8090);
        url.setPath(QString("/api/v1/emulator/%1/state/memory/rom").arg(_emulatorId));
        QNetworkRequest req(url);
        auto* reply = nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
            if (reply->error() == QNetworkReply::NoError)
            {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                QJsonArray pages = doc.object()["pages"].toArray();
                _romDescriptions.clear();
                for (const auto& p : pages)
                {
                    QJsonObject pg = p.toObject();
                    _romDescriptions[pg["page"].toInt()] = pg["description"].toString();
                }
                // Re-render tiles to apply ROM descriptions to labels
                if (_hasData) renderAllPageTiles();
            }
            reply->deleteLater(); nam->deleteLater();
        });
    }

    // Fetch current bank mapping (which page is mapped to which Z80 bank)
    {
        auto* nam = new QNetworkAccessManager(this);
        QUrl url;
        url.setScheme("http"); url.setHost("localhost"); url.setPort(8090);
        url.setPath(QString("/api/v1/emulator/%1/state/memory/ram").arg(_emulatorId));
        QNetworkRequest req(url);
        auto* reply = nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
            if (reply->error() == QNetworkReply::NoError)
            {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                QJsonObject root = doc.object();
                QJsonObject banks = root["banks"].toObject();

                std::memset(_bankMapping, 0, sizeof(_bankMapping));

                // Bank 0: ROM or RAM
                QJsonObject b0 = banks["bank0"].toObject();
                if (b0["type"].toString() == "ROM")
                    _bankMapping[0] = { true, b0["page"].toInt() };
                else
                    _bankMapping[0] = { false, b0["page"].toInt() };

                // Banks 1-3: always RAM
                _bankMapping[1] = { false, banks["bank1"].toObject()["page"].toInt() };
                _bankMapping[2] = { false, banks["bank2"].toObject()["page"].toInt() };
                _bankMapping[3] = { false, banks["bank3"].toObject()["page"].toInt() };

                _hasBankMapping = true;

                // Re-render tiles now that we know which pages map to which Z80 banks
                if (_hasData) renderAllPageTiles();
            }
            reply->deleteLater(); nam->deleteLater();
        });
    }
}

void HeatmapWidget::fetchPageContent()
{
    if (_emulatorId.isEmpty() || !_hasBankMapping || _contentFetchPending) return;
    _contentFetchPending = true;

    // Ensure Z80 content buffer is 64KB
    if (_z80Content.size() < 65536)
        _z80Content.resize(65536);

    // Fetch content for each currently mapped bank via WebAPI
    for (int bank = 0; bank < 4; bank++)
    {
        const auto& bm = _bankMapping[bank];
        QString type = bm.isRom ? "rom" : "ram";
        int page = bm.page;
        int absPage = bm.isRom ? (FIRST_ROM + page) : page;

        auto* nam = new QNetworkAccessManager(this);
        QUrl url;
        url.setScheme("http"); url.setHost("localhost"); url.setPort(8090);
        url.setPath(QString("/api/v1/emulator/%1/memory/page/%2/%3")
                    .arg(_emulatorId).arg(type).arg(page));
        QUrlQuery query;
        query.addQueryItem("offset", "0");
        query.addQueryItem("length", "16384");
        url.setQuery(query);

        QNetworkRequest req(url);
        auto* reply = nam->get(req);
        int capturedBank = bank;
        connect(reply, &QNetworkReply::finished, this, [this, absPage, capturedBank, reply, nam]() {
            if (reply->error() == QNetworkReply::NoError)
            {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                QJsonArray data = doc.object()["data"].toArray();
                QByteArray bytes;
                bytes.reserve(data.size());
                for (const auto& v : data)
                    bytes.append(static_cast<char>(v.toInt()));

                // Set per-tile content
                if (_pageTiles.contains(absPage))
                    _pageTiles[absPage]->setContentBytes(bytes);

                // Copy into Z80 address space content at bank offset
                int z80Offset = capturedBank * 16384;
                int len = std::min(static_cast<int>(bytes.size()), 16384);
                std::memcpy(_z80Content.data() + z80Offset, bytes.constData(), len);
            }
            reply->deleteLater(); nam->deleteLater();
            if (capturedBank == 3)
            {
                _contentFetchPending = false;
                if (_hasData && _showContent)
                {
                    renderZ80Heatmap();
                    renderAllPageTiles();
                }
            }
        });
    }
}

// =============================================================================
// SHM
// =============================================================================

void HeatmapWidget::attachToShm(void* base, size_t size)
{
    _shmBase = base; _shmSize = size;
    _refreshTimer->start(REFRESH_MS);
}

void HeatmapWidget::detachFromShm()
{
    _refreshTimer->stop();
    _shmBase = nullptr; _shmSize = 0;
    _hasData = false;
    _infoLabel->setText("No heatmap data");
}

void HeatmapWidget::onFrameReady()
{
    _frameCounter++;
    if (_frameCounter % FRAME_DIVISOR != 0) return;
    onRefresh();
}

void HeatmapWidget::setFifoActive(bool active)
{
    _fifoActive = active;
    if (_shmBase) _refreshTimer->start(active ? FALLBACK_MS : REFRESH_MS);
}

void HeatmapWidget::onModeChanged(int index)
{
    _mode = static_cast<Mode>(index);
    if (_hasData) { renderZ80Heatmap(); renderAllPageTiles(); }
}

void HeatmapWidget::onContentToggled(bool checked)
{
    _showContent = checked;
    if (checked && _hasBankMapping)
        fetchPageContent();
    if (_hasData)
        renderAllPageTiles();
}

// =============================================================================
// Capture toggle
// =============================================================================

void HeatmapWidget::onCaptureToggled()
{
    if (_emulatorId.isEmpty()) return;
    bool capture = _captureBtn->isChecked();
    _pendingApiCall = true;

    auto* nam = new QNetworkAccessManager(this);
    QUrl url;
    url.setScheme("http"); url.setHost("localhost"); url.setPort(8090);
    url.setPath(capture
        ? QString("/api/v1/emulator/%1/profiler/memory/start").arg(_emulatorId)
        : QString("/api/v1/emulator/%1/profiler/memory/stop").arg(_emulatorId));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = nam->post(req, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, capture, reply, nam]() {
        if (reply->error() == QNetworkReply::NoError)
        {
            _featureEnabled = capture;
            _captureBtn->setText(capture ? "Capturing\u2026" : "Capture");
            _infoLabel->setText(capture ? "Session active" : "Session inactive");
        }
        else
        {
            _captureBtn->blockSignals(true);
            _captureBtn->setChecked(!capture);
            _captureBtn->blockSignals(false);
        }
        reply->deleteLater(); nam->deleteLater();
        _pendingApiCall = false;
    });
}

// =============================================================================
// Reset
// =============================================================================

void HeatmapWidget::onResetClicked()
{
    if (_emulatorId.isEmpty()) return;

    if (_shmBase)
    {
        auto* manifest = static_cast<ManifestHeader*>(_shmBase);
        for (uint16_t i = 0; i < manifest->section_count; i++)
        {
            auto* desc = getDescriptor(_shmBase, manifest, i);
            if (desc && desc->type == SectionType::HEATMAP_Z80)
            { _lastResetEpoch = desc->epoch.load(std::memory_order_acquire); break; }
        }
    }

    auto* nam = new QNetworkAccessManager(this);
    QUrl url;
    url.setScheme("http"); url.setHost("localhost"); url.setPort(8090);
    url.setPath(QString("/api/v1/emulator/%1/profiler/memory/clear").arg(_emulatorId));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = nam->post(req, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater(); nam->deleteLater();
    });

    std::memset(_readCounters, 0, sizeof(_readCounters));
    std::memset(_writeCounters, 0, sizeof(_writeCounters));
    std::memset(_execCounters, 0, sizeof(_execCounters));
    std::memset(_pageReadCounters, 0, sizeof(_pageReadCounters));
    std::memset(_pageWriteCounters, 0, sizeof(_pageWriteCounters));
    std::memset(_pageExecCounters, 0, sizeof(_pageExecCounters));
    _physPages.clear();
    _hasPhysData = false;
    renderZ80Heatmap();
    renderAllPageTiles();
}

// =============================================================================
// Refresh from SHM — also periodically re-fetch bank mapping
// =============================================================================

void HeatmapWidget::onRefresh()
{
    if (!_shmBase) return;

    // Periodically re-fetch bank mapping (every ~2 seconds)
    _bankPollCounter++;
    if (_bankPollCounter % 10 == 0)
        fetchMemoryState();

    auto* manifest = static_cast<ManifestHeader*>(_shmBase);
    bool gotZ80 = false;
    bool gotPages = false;
    bool gotPhys = false;

    for (uint16_t i = 0; i < manifest->section_count; i++)
    {
        auto* desc = getDescriptor(_shmBase, manifest, i);
        if (!desc) continue;

        if (desc->type == SectionType::HEATMAP_Z80 && !gotZ80)
        {
            uint64_t before = desc->epoch.load(std::memory_order_acquire);
            if (before == EPOCH_UPDATING) continue;
            if (_lastResetEpoch != 0 && before == _lastResetEpoch) continue;
            if (_lastResetEpoch != 0 && before != _lastResetEpoch) _lastResetEpoch = 0;

            auto* data = static_cast<const uint8_t*>(getSectionData(_shmBase, desc));
            if (!data) continue;

            auto* hdr = reinterpret_cast<const HeatmapZ80Header*>(data);
            bool active = hdr->session_active != 0;

            const uint8_t* base = data + sizeof(HeatmapZ80Header);
            std::memcpy(_readCounters, base, 65536 * sizeof(uint32_t));
            std::memcpy(_writeCounters, base + 65536 * sizeof(uint32_t), 65536 * sizeof(uint32_t));
            std::memcpy(_execCounters, base + 2 * 65536 * sizeof(uint32_t), 65536 * sizeof(uint32_t));

            std::atomic_thread_fence(std::memory_order_acquire);
            uint64_t after = desc->epoch.load(std::memory_order_relaxed);
            if (before != after) continue;

            _featureEnabled = active;
            if (!_pendingApiCall)
            {
                _captureBtn->blockSignals(true);
                _captureBtn->setChecked(active);
                _captureBtn->blockSignals(false);
                _captureBtn->setText(active ? "Capturing\u2026" : "Capture");
            }
            _infoLabel->setText(active ? "Session active" : "Session inactive");
            _hasData = true;
            gotZ80 = true;
        }

        if (desc->type == SectionType::HEATMAP_PAGES && !gotPages)
        {
            uint64_t before = desc->epoch.load(std::memory_order_acquire);
            if (before == EPOCH_UPDATING) continue;
            if (_lastResetEpoch != 0) continue;

            auto* data = static_cast<const uint8_t*>(getSectionData(_shmBase, desc));
            if (!data) continue;

            auto* hdr = reinterpret_cast<const HeatmapPagesHeader*>(data);
            uint16_t count = std::min(hdr->page_count, PAGE_COUNT);
            const uint8_t* base = data + sizeof(HeatmapPagesHeader);
            constexpr size_t arrSize = PAGE_COUNT * sizeof(uint32_t);

            std::memcpy(_pageReadCounters, base, count * sizeof(uint32_t));
            std::memcpy(_pageWriteCounters, base + arrSize, count * sizeof(uint32_t));
            std::memcpy(_pageExecCounters, base + 2 * arrSize, count * sizeof(uint32_t));

            std::atomic_thread_fence(std::memory_order_acquire);
            uint64_t after = desc->epoch.load(std::memory_order_relaxed);
            if (before != after) continue;
            gotPages = true;
        }

        if (desc->type == SectionType::HEATMAP_PHYS && !gotPhys)
        {
            uint64_t before = desc->epoch.load(std::memory_order_acquire);
            if (before == EPOCH_UPDATING) continue;
            if (_lastResetEpoch != 0) continue;

            auto* data = static_cast<const uint8_t*>(getSectionData(_shmBase, desc));
            if (!data) continue;

            auto* hdr = reinterpret_cast<const HeatmapPhysHeader*>(data);
            uint16_t activeCount = std::min(hdr->active_page_count, static_cast<uint16_t>(32));

            _physPages.clear();
            _physPages.reserve(activeCount);
            const uint8_t* ptr = data + sizeof(HeatmapPhysHeader);

            for (uint16_t p = 0; p < activeCount; p++)
            {
                auto* pageDesc = reinterpret_cast<const HeatmapPhysPageDescriptor*>(ptr);
                ptr += sizeof(HeatmapPhysPageDescriptor);

                PhysPageData pd;
                pd.absPageIndex = pageDesc->abs_page_index;
                pd.pageType = pageDesc->page_type;
                pd.logicalPageNum = pageDesc->logical_page_num;
                pd.isMapped = pageDesc->is_mapped != 0;
                pd.mappedBank = pageDesc->mapped_bank;
                std::memcpy(pd.readCounters, ptr, 16384 * sizeof(uint32_t));
                ptr += 16384 * sizeof(uint32_t);
                std::memcpy(pd.writeCounters, ptr, 16384 * sizeof(uint32_t));
                ptr += 16384 * sizeof(uint32_t);
                std::memcpy(pd.execCounters, ptr, 16384 * sizeof(uint32_t));
                ptr += 16384 * sizeof(uint32_t);

                _physPages.append(pd);
            }

            std::atomic_thread_fence(std::memory_order_acquire);
            uint64_t after = desc->epoch.load(std::memory_order_relaxed);
            if (before != after) { _physPages.clear(); continue; }

            _hasPhysData = true;
            gotPhys = true;
        }
    }

    if (gotZ80) renderZ80Heatmap();
    if (gotZ80 || gotPages || gotPhys) renderAllPageTiles();
}

// =============================================================================
// Z80 heatmap
// =============================================================================

void HeatmapWidget::renderZ80Heatmap()
{
    _z80Image = QImage(256, 256, QImage::Format_RGB32);

    // Base layer: memory content as gray gradient (when Content checkbox is on)
    if (_showContent && _z80Content.size() >= 65536)
    {
        const uint8_t* mem = reinterpret_cast<const uint8_t*>(_z80Content.constData());
        constexpr double gamma = 1.8;
        for (int a = 0; a < 65536; a++)
        {
            double v = std::pow(mem[a] / 255.0, 1.0 / gamma);
            int g = static_cast<int>(v * 255.0);
            int x = a & 0xFF, y = a >> 8;
            _z80Image.setPixel(x, y, qRgb(g, g, static_cast<int>(g * 0.95)));
        }
    }
    else
    {
        _z80Image.fill(Qt::black);
    }

    // Counter overlay
    uint32_t maxVal = 1;
    for (int a = 0; a < 65536; a++)
    {
        maxVal = std::max(maxVal, _readCounters[a]);
        maxVal = std::max(maxVal, _writeCounters[a]);
        maxVal = std::max(maxVal, _execCounters[a]);
    }
    double logMax = std::log(static_cast<double>(maxVal) + 1.0);

    for (int a = 0; a < 65536; a++)
    {
        int x = a & 0xFF, y = a >> 8;
        QRgb overlay = pixelForMode(_mode, _readCounters[a], _writeCounters[a], _execCounters[a], logMax);
        if (_showContent && _z80Content.size() >= 65536)
        {
            QRgb base = _z80Image.pixel(x, y);
            int r = std::min(255, qRed(base) + qRed(overlay));
            int g = std::min(255, qGreen(base) + qGreen(overlay));
            int b = std::min(255, qBlue(base) + qBlue(overlay));
            _z80Image.setPixel(x, y, qRgb(r, g, b));
        }
        else
        {
            _z80Image.setPixel(x, y, overlay);
        }
    }

    for (int bank = 1; bank < 4; bank++)
    {
        int lineY = bank * 64;
        for (int x = 0; x < 256; x++)
            if (x % 4 < 2) _z80Image.setPixel(x, lineY, qRgb(255, 255, 255));
    }

    _z80ImageLabel->setPixmap(QPixmap::fromImage(_z80Image).scaled(
        Z80_DISPLAY, Z80_DISPLAY, Qt::KeepAspectRatio, Qt::FastTransformation));
}

// =============================================================================
// Render all page tiles
// =============================================================================

void HeatmapWidget::renderAllPageTiles()
{
    // Build bank mapping: abs page index -> Z80 bank number
    // This comes from the WebAPI /state/memory/ram response
    QMap<int, int> pageToBankMap;  // absPageIndex -> bank (0-3)
    if (_hasBankMapping)
    {
        for (int bank = 0; bank < 4; bank++)
        {
            const auto& bm = _bankMapping[bank];
            int absPage;
            if (bm.isRom)
                absPage = FIRST_ROM + bm.page;  // ROM page -> abs index 259+
            else
                absPage = bm.page;  // RAM page -> abs index 0-255
            pageToBankMap[absPage] = bank;
        }
    }

    // Build PHYS index
    QMap<int, int> physIndex;
    if (_hasPhysData)
    {
        for (int i = 0; i < _physPages.size(); i++)
            physIndex[_physPages[i].absPageIndex] = i;
    }

    // Gather active pages from HEATMAP_PAGES
    QSet<int> activePages;
    for (int p = 0; p < PAGE_COUNT; p++)
    {
        if (_pageReadCounters[p] + _pageWriteCounters[p] + _pageExecCounters[p] > 0)
            activePages.insert(p);
    }

    // Remove dead tiles
    QList<int> toRemove;
    for (auto it = _pageTiles.begin(); it != _pageTiles.end(); ++it)
        if (!activePages.contains(it.key())) toRemove.append(it.key());
    for (int pi : toRemove)
    {
        auto* w = _pageTiles.take(pi);
        w->setParent(nullptr);
        w->deleteLater();
    }

    // Create new tiles
    bool layoutChanged = !toRemove.isEmpty();
    for (int pi : activePages)
    {
        if (!_pageTiles.contains(pi))
        {
            auto* tile = new PageTileWidget(pi, labelForPage(pi, _romDescriptions));
            _pageTiles[pi] = tile;
            layoutChanged = true;
        }
    }

    // Rebuild grid layout if page set changed
    if (layoutChanged)
    {
        while (_romGrid->count())
        {
            auto* item = _romGrid->takeAt(0);
            if (item->widget()) _romGrid->removeWidget(item->widget());
            delete item;
        }
        while (_ramGrid->count())
        {
            auto* item = _ramGrid->takeAt(0);
            if (item->widget()) _ramGrid->removeWidget(item->widget());
            delete item;
        }

        // Set equal column stretch so each tile gets 1/3 of the width
        for (int c = 0; c < TILE_COLS; c++)
        {
            _romGrid->setColumnStretch(c, 1);
            _ramGrid->setColumnStretch(c, 1);
        }

        QList<int> sortedPages = _pageTiles.keys();
        std::sort(sortedPages.begin(), sortedPages.end());

        int romSlot = 0, ramSlot = 0;
        for (int pi : sortedPages)
        {
            auto* tile = _pageTiles[pi];
            bool isRom = (pi >= FIRST_ROM);

            if (isRom)
            {
                tile->setParent(_romGridWidget);
                _romGrid->addWidget(tile, romSlot / TILE_COLS, romSlot % TILE_COLS);
                romSlot++;
            }
            else
            {
                tile->setParent(_ramGridWidget);
                _ramGrid->addWidget(tile, ramSlot / TILE_COLS, ramSlot % TILE_COLS);
                ramSlot++;
            }
        }

        _romTitle->setText(QString("Active ROM Pages (%1)").arg(romSlot));
        _ramTitle->setText(QString("Active RAM Pages (%1)").arg(ramSlot));
    }

    // Render each tile
    for (auto it = _pageTiles.begin(); it != _pageTiles.end(); ++it)
    {
        int pi = it.key();
        auto* tile = it.value();

        // Priority 1: HEATMAP_PHYS per-address data (best quality)
        if (physIndex.contains(pi))
        {
            const auto& pd = _physPages[physIndex[pi]];
            tile->renderFromCounters(pd.readCounters, pd.writeCounters, pd.execCounters,
                                      16384, _mode, _showContent);
            QString label = labelForPage(pi, _romDescriptions);
            if (pd.isMapped)
                label += QString(" [Bank %1]").arg(pd.mappedBank);
            tile->setLabel(label);
            continue;
        }

        // Priority 2: Z80 counter slice (via bank mapping from WebAPI)
        if (_hasData && pageToBankMap.contains(pi))
        {
            int bank = pageToBankMap[pi];
            int base = bank * 16384;
            tile->renderFromCounters(
                _readCounters + base, _writeCounters + base, _execCounters + base,
                16384, _mode, _showContent);
            tile->setLabel(labelForPage(pi, _romDescriptions) +
                           QString(" [Bank %1]").arg(bank));
            continue;
        }

        // Priority 3: page has activity but not currently mapped — show blank
        tile->renderBlank();
        tile->setLabel(labelForPage(pi, _romDescriptions) + " (paged out)");
    }
}
