#include "ChipStateWidget.h"
#include "SparklineWidget.h"
#include "EnvelopeShapeWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFont>
#include <QScrollArea>
#include <QColor>
#include <QSet>

#include <cstring>

#include "emulator/monitoring/manifest.h"

using namespace monitoring;

// =============================================================================
// Color palette & tuning constants
// =============================================================================

// Label name color (dimmed gray)
static const QColor COLOR_LABEL_NAME(128, 128, 128);

// Indicator color ratios (RGB components scaled by brightness value 0-255)
// Value-change indicator — red (registers are "written" by emulator)
static constexpr double CHANGE_INDICATOR_R_RATIO = 1.0;
static constexpr double CHANGE_INDICATOR_G_RATIO = 0.0;
static constexpr double CHANGE_INDICATOR_B_RATIO = 0.0;

// AY register read indicator — emerald green
static constexpr double READ_INDICATOR_R_RATIO   = 0.25;
static constexpr double READ_INDICATOR_G_RATIO   = 1.0;
static constexpr double READ_INDICATOR_B_RATIO   = 0.5;

// AY register write indicator — red
static constexpr double WRITE_INDICATOR_R_RATIO  = 1.0;
static constexpr double WRITE_INDICATOR_G_RATIO  = 0.0;
static constexpr double WRITE_INDICATOR_B_RATIO  = 0.0;

// Heat dynamics
static constexpr float HEAT_DECAY       = 0.92f;   // Per-frame decay multiplier
static constexpr float HEAT_MAX         = 10.0f;    // Saturation cap
static constexpr float HEAT_BOOST       = 1.0f;     // Added per value-change event
static constexpr float HEAT_DIVISOR     = 5.0f;     // Maps heat to 0..1 intensity (saturates at 5)
static constexpr float INDICATOR_MIN    = 0.02f;    // Below this intensity, hide indicator

// Indicator square font size (px)
static constexpr int INDICATOR_FONT_PX  = 8;

// Decoded fields eligible for sparkline graphs
static const QSet<QString> SPARKLINE_FIELDS = {
    "ToneA", "ToneB", "ToneC", "Noise", "Envelope",
    "VolA", "VolB", "VolC"
};

// Decoded fields that don't need value-change indicators
static const QSet<QString> NO_INDICATOR_FIELDS = {
    "SelReg"
};

static QLabel* makeValueLabel()
{
    auto* lbl = new QLabel("----");
    lbl->setFont(QFont("Menlo", 11));
    lbl->setContentsMargins(14, 0, 0, 0);  // ~2 char gap from name label
    return lbl;
}

static QLabel* makeNameLabel(const QString& text)
{
    auto* lbl = new QLabel(text);
    lbl->setFont(QFont("Menlo", 11));
    QPalette pal = lbl->palette();
    pal.setColor(QPalette::WindowText, COLOR_LABEL_NAME);
    lbl->setPalette(pal);
    return lbl;
}

static QLabel* makeIndicatorLabel()
{
    auto* lbl = new QLabel();
    lbl->setTextFormat(Qt::RichText);
    lbl->setFont(QFont("Menlo", 11));
    lbl->setFixedWidth(12);
    lbl->setContentsMargins(7, 0, 0, 0);
    return lbl;
}

ChipStateWidget::ChipStateWidget(QWidget* parent)
    : QWidget(parent)
{
    _activityClock.start();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    _tabs = new QTabWidget(this);
    _tabs->addTab(createZ80Page(), "Z80 CPU");
    _tabs->addTab(createAYPage(), "AY-8910");
    _tabs->addTab(createFDCPage(), "WD1793 FDC");
    layout->addWidget(_tabs);

    _refreshTimer = new QTimer(this);
    connect(_refreshTimer, &QTimer::timeout, this, &ChipStateWidget::onRefresh);
}

void ChipStateWidget::attachToShm(void* base, size_t size)
{
    _shmBase = base;
    _shmSize = size;
    _refreshTimer->start(REFRESH_MS);
}

void ChipStateWidget::detachFromShm()
{
    _refreshTimer->stop();
    _shmBase = nullptr;
    _shmSize = 0;
    _fifoActive = false;

    for (auto* lbl : _fields)
    {
        lbl->setTextFormat(Qt::PlainText);
        lbl->setText("----");
        lbl->setStyleSheet("");
    }

    for (auto* lbl : _indicators)
        lbl->setText("");

    for (auto* spark : _sparklines)
        spark->clear();

    for (auto* env : _envShapes)
        env->clear();

    _valueActivity.clear();
    _prevAYRegsValid[0] = _prevAYRegsValid[1] = false;
}

void ChipStateWidget::onFrameReady()
{
    onRefresh();
}

void ChipStateWidget::setFifoActive(bool active)
{
    _fifoActive = active;
    if (_shmBase)
        _refreshTimer->start(active ? FALLBACK_MS : REFRESH_MS);
}

void ChipStateWidget::setField(const QString& key, const QString& value)
{
    auto it = _fields.find(key);
    if (it == _fields.end())
        return;

    ValueActivity& act = _valueActivity[key];

    if (act.value != value)
    {
        act.value = value;
        act.lastChangeMs = _activityClock.elapsed();
        act.heat = qMin(act.heat + HEAT_BOOST, HEAT_MAX);
        act.peakHeat = qMin(act.heat / HEAT_DIVISOR, 1.0f);
    }
    else
    {
        act.heat *= HEAT_DECAY;
    }
    // Label text is rendered by updateActivityColors()
}

// =============================================================================
// Z80 page
// =============================================================================

QWidget* ChipStateWidget::createZ80Page()
{
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);

    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);

    // Main registers — 4 per row, each group = name(0)+value(1)+indicator(2), spacer(3)
    auto* mainGroup = new QGroupBox("Main Registers");
    auto* mainGrid = new QGridLayout(mainGroup);
    mainGrid->setHorizontalSpacing(0);

    int row = 0;
    auto addReg = [&](const QString& name) {
        int r = row / 4;
        int g = row % 4;
        int col = g * 4;  // cols: 0-2, 4-6, 8-10, 12-14
        mainGrid->addWidget(makeNameLabel(name), r, col);
        auto* v = makeValueLabel();
        _fields["z80." + name] = v;
        mainGrid->addWidget(v, r, col + 1);
        auto* ind = makeIndicatorLabel();
        _indicators["z80." + name] = ind;
        mainGrid->addWidget(ind, r, col + 2);
        row++;
    };
    for (const char* r : {"AF", "BC", "DE", "HL", "AF'", "BC'", "DE'", "HL'",
                           "IX", "IY", "SP", "PC", "IR", "WZ"})
        addReg(r);
    // Spacer columns between groups and trailing
    for (int s : {3, 7, 11, 15})
        mainGrid->setColumnStretch(s, 1);
    layout->addWidget(mainGroup);

    // Flags
    auto* flagGroup = new QGroupBox("Flags");
    auto* flagGrid = new QGridLayout(flagGroup);
    flagGrid->setHorizontalSpacing(0);

    row = 0;
    for (const char* f : {"S", "Z", "H", "PV", "N", "C"})
    {
        int col = row * 4;
        flagGrid->addWidget(makeNameLabel(f), 0, col);
        auto* v = makeValueLabel();
        v->setAlignment(Qt::AlignCenter);
        _fields[QString("z80.flag.") + f] = v;
        flagGrid->addWidget(v, 0, col + 1);
        auto* ind = makeIndicatorLabel();
        ind->setAlignment(Qt::AlignCenter);
        _indicators[QString("z80.flag.") + f] = ind;
        flagGrid->addWidget(ind, 0, col + 2);
        row++;
    }
    // Spacer after each group
    for (int s : {3, 7, 11, 15, 19, 23})
        flagGrid->setColumnStretch(s, 1);
    layout->addWidget(flagGroup);

    // Interrupt + timing
    auto* intGroup = new QGroupBox("Interrupt / Timing");
    auto* intGrid = new QGridLayout(intGroup);
    intGrid->setHorizontalSpacing(0);

    row = 0;
    for (const char* name : {"IFF1", "IFF2", "IM", "HALT", "T-state", "Total"})
    {
        intGrid->addWidget(makeNameLabel(name), row, 0);
        auto* v = makeValueLabel();
        _fields[QString("z80.") + name] = v;
        intGrid->addWidget(v, row, 1);
        auto* ind = makeIndicatorLabel();
        _indicators[QString("z80.") + name] = ind;
        intGrid->addWidget(ind, row, 2);
        row++;
    }
    intGrid->setColumnStretch(3, 1);
    layout->addWidget(intGroup);

    layout->addStretch();
    scroll->setWidget(page);
    return scroll;
}

// =============================================================================
// AY page — dual-chip layout for TurboSound support
// =============================================================================

QGroupBox* ChipStateWidget::createAYChipPanel(int chipIndex)
{
    QString prefix = QString("ay%1.").arg(chipIndex);
    QString title = chipIndex == 0 ? "AY #0 (Primary)" : "AY #1 (TurboSound)";

    auto* group = new QGroupBox(title);
    group->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(group);

    auto* regGroup = new QGroupBox("Registers (R0-R15)");
    auto* regGrid = new QGridLayout(regGroup);
    regGrid->setHorizontalSpacing(0);
    regGrid->setContentsMargins(9, 16, 9, 9);

    for (int i = 0; i < 16; i++)
    {
        QString key = prefix + QString("R%1").arg(i);
        int r = i / 4;
        int col = (i % 4) * 4;
        regGrid->addWidget(makeNameLabel(QString("R%1").arg(i, 2, 10, QChar('0'))), r, col);
        auto* v = makeValueLabel();
        _fields[key] = v;
        regGrid->addWidget(v, r, col + 1);
        auto* ind = makeIndicatorLabel();
        _indicators[key] = ind;
        regGrid->addWidget(ind, r, col + 2);
    }
    for (int s : {3, 7, 11, 15})
        regGrid->setColumnStretch(s, 1);
    layout->addWidget(regGroup);

    auto* decGroup = new QGroupBox("Decoded");
    auto* decGrid = new QGridLayout(decGroup);
    decGrid->setHorizontalSpacing(0);
    decGrid->setContentsMargins(9, 16, 9, 9);

    int row = 0;
    for (const char* name : {"ToneA", "ToneB", "ToneC", "Noise", "Envelope",
                              "VolA", "VolB", "VolC", "EnvShape",
                              "ToneEn", "NoiseEn", "SelReg"})
    {
        QString fieldName(name);
        decGrid->addWidget(makeNameLabel(name), row, 0);
        auto* v = makeValueLabel();
        v->setMinimumWidth(46);  // 4 chars + left margin at Menlo 11
        v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        _fields[prefix + fieldName] = v;
        decGrid->addWidget(v, row, 1);

        // Skip indicator for excluded fields
        if (!NO_INDICATOR_FIELDS.contains(fieldName))
        {
            auto* ind = makeIndicatorLabel();
            _indicators[prefix + fieldName] = ind;
            decGrid->addWidget(ind, row, 2);
        }

        // Sparkline for numeric values
        if (SPARKLINE_FIELDS.contains(fieldName))
        {
            auto* spark = new SparklineWidget(200, decGroup);
            _sparklines[prefix + fieldName] = spark;
            decGrid->addWidget(spark, row, 3);
        }

        // Envelope shape waveform visualization
        if (fieldName == "EnvShape")
        {
            auto* envShape = new EnvelopeShapeWidget(decGroup);
            _envShapes[prefix + fieldName] = envShape;
            decGrid->addWidget(envShape, row, 3);
        }

        row++;
    }
    decGrid->setColumnStretch(3, 1);
    layout->addWidget(decGroup);

    layout->setSpacing(12);
    layout->addStretch();
    return group;
}

QWidget* ChipStateWidget::createAYPage()
{
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);

    auto* page = new QWidget();
    auto* hLayout = new QHBoxLayout(page);

    hLayout->addWidget(createAYChipPanel(0), 1);

    _ay1Panel = createAYChipPanel(1);
    _ay1Panel->setVisible(false);  // Hidden until TurboSound detected
    hLayout->addWidget(_ay1Panel, 1);

    scroll->setWidget(page);
    return scroll;
}

// =============================================================================
// FDC page
// =============================================================================

QWidget* ChipStateWidget::createFDCPage()
{
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);

    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);

    auto* regGroup = new QGroupBox("WD1793 Registers");
    auto* regGrid = new QGridLayout(regGroup);
    regGrid->setHorizontalSpacing(0);

    int row = 0;
    for (const char* name : {"Command", "Status", "Track", "Sector", "Data",
                              "Drive", "Side", "FSM", "Beta128", "LastCmd"})
    {
        regGrid->addWidget(makeNameLabel(name), row, 0);
        auto* v = makeValueLabel();
        _fields[QString("fdc.") + name] = v;
        regGrid->addWidget(v, row, 1);
        auto* ind = makeIndicatorLabel();
        _indicators[QString("fdc.") + name] = ind;
        regGrid->addWidget(ind, row, 2);
        row++;
    }
    regGrid->setColumnStretch(3, 1);
    layout->addWidget(regGroup);

    auto* sigGroup = new QGroupBox("Signals / Errors");
    auto* sigGrid = new QGridLayout(sigGroup);
    sigGrid->setHorizontalSpacing(0);

    row = 0;
    for (const char* name : {"INTRQ", "DRQ", "LostData", "CRC", "NotFound", "WriteFault", "Disks"})
    {
        sigGrid->addWidget(makeNameLabel(name), row, 0);
        auto* v = makeValueLabel();
        _fields[QString("fdc.") + name] = v;
        sigGrid->addWidget(v, row, 1);
        auto* ind = makeIndicatorLabel();
        _indicators[QString("fdc.") + name] = ind;
        sigGrid->addWidget(ind, row, 2);
        row++;
    }
    sigGrid->setColumnStretch(3, 1);
    layout->addWidget(sigGroup);

    layout->addStretch();
    scroll->setWidget(page);
    return scroll;
}

// =============================================================================
// Refresh
// =============================================================================

void ChipStateWidget::onRefresh()
{
    if (!_shmBase) return;
    refreshZ80();
    refreshAY();
    refreshFDC();
    updateActivityColors();
}

void ChipStateWidget::refreshZ80()
{
    auto* manifest = static_cast<ManifestHeader*>(_shmBase);
    for (uint16_t i = 0; i < manifest->section_count; i++)
    {
        auto* desc = getDescriptor(_shmBase, manifest, i);
        if (!desc || desc->type != SectionType::CHIP_STATE_Z80)
            continue;

        Z80StateSnapshot snap;
        if (!epochSafeRead(desc, _shmBase, &snap, sizeof(snap)))
            return;

        auto hex4 = [](uint16_t v) { return QString("%1").arg(v, 4, 16, QChar('0')).toUpper(); };

        setField("z80.AF", hex4(snap.af));
        setField("z80.BC", hex4(snap.bc));
        setField("z80.DE", hex4(snap.de));
        setField("z80.HL", hex4(snap.hl));
        setField("z80.AF'", hex4(snap.af_alt));
        setField("z80.BC'", hex4(snap.bc_alt));
        setField("z80.DE'", hex4(snap.de_alt));
        setField("z80.HL'", hex4(snap.hl_alt));
        setField("z80.IX", hex4(snap.ix));
        setField("z80.IY", hex4(snap.iy));
        setField("z80.SP", hex4(snap.sp));
        setField("z80.PC", hex4(snap.pc));
        setField("z80.IR", hex4(snap.ir));
        setField("z80.WZ", hex4(snap.memptr));

        setField("z80.flag.S", QString::number(snap.flag_s));
        setField("z80.flag.Z", QString::number(snap.flag_z));
        setField("z80.flag.H", QString::number(snap.flag_h));
        setField("z80.flag.PV", QString::number(snap.flag_pv));
        setField("z80.flag.N", QString::number(snap.flag_n));
        setField("z80.flag.C", QString::number(snap.flag_c));

        setField("z80.IFF1", snap.iff1 ? "ON" : "off");
        setField("z80.IFF2", snap.iff2 ? "ON" : "off");
        setField("z80.IM", QString::number(snap.im));
        setField("z80.HALT", snap.halted ? "YES" : "no");
        setField("z80.T-state", QString::number(snap.t));
        setField("z80.Total", QString::number(snap.total_tstates));
        return;
    }
}

void ChipStateWidget::refreshAY()
{
    auto* manifest = static_cast<ManifestHeader*>(_shmBase);
    bool hasChip1 = false;

    for (uint16_t i = 0; i < manifest->section_count; i++)
    {
        auto* desc = getDescriptor(_shmBase, manifest, i);
        if (!desc || desc->type != SectionType::CHIP_STATE_AY)
            continue;

        AYStateSnapshot snap;
        if (!epochSafeRead(desc, _shmBase, &snap, sizeof(snap)))
            continue;

        int idx = snap.chip_index;
        if (idx < 0 || idx > 1)
            continue;

        QString prefix = QString("ay%1.").arg(idx);

        // Detect TurboSound: chip1_selected is set once software selects chip 1
        if (snap.chip1_selected)
            hasChip1 = true;

        // Format register values and update indicator labels
        formatAYRegLabels(idx, snap);

        // Decoded values use standard setField (value-change heat tracking)
        setField(prefix + "ToneA", QString::number(snap.tone_period_a));
        setField(prefix + "ToneB", QString::number(snap.tone_period_b));
        setField(prefix + "ToneC", QString::number(snap.tone_period_c));
        setField(prefix + "Noise", QString::number(snap.noise_period));
        setField(prefix + "Envelope", QString::number(snap.envelope_period));
        setField(prefix + "VolA", QString::number(snap.volume_a));
        setField(prefix + "VolB", QString::number(snap.volume_b));
        setField(prefix + "VolC", QString::number(snap.volume_c));
        setField(prefix + "EnvShape", QString::number(snap.envelope_shape));
        setField(prefix + "ToneEn", QString("%1%2%3")
            .arg(snap.tone_enable & 1 ? "A" : "-")
            .arg(snap.tone_enable & 2 ? "B" : "-")
            .arg(snap.tone_enable & 4 ? "C" : "-"));
        setField(prefix + "NoiseEn", QString("%1%2%3")
            .arg(snap.noise_enable & 1 ? "A" : "-")
            .arg(snap.noise_enable & 2 ? "B" : "-")
            .arg(snap.noise_enable & 4 ? "C" : "-"));
        setField(prefix + "SelReg", QString::number(snap.current_register));

        // Feed sparkline graphs
        auto feedSparkline = [this, &prefix](const char* name, float value) {
            auto it = _sparklines.find(prefix + name);
            if (it != _sparklines.end())
                it.value()->addValue(value);
        };
        feedSparkline("ToneA", static_cast<float>(snap.tone_period_a));
        feedSparkline("ToneB", static_cast<float>(snap.tone_period_b));
        feedSparkline("ToneC", static_cast<float>(snap.tone_period_c));
        feedSparkline("Noise", static_cast<float>(snap.noise_period));
        feedSparkline("Envelope", static_cast<float>(snap.envelope_period));
        feedSparkline("VolA", static_cast<float>(snap.volume_a));
        feedSparkline("VolB", static_cast<float>(snap.volume_b));
        feedSparkline("VolC", static_cast<float>(snap.volume_c));
        // Feed envelope shape visualization
        auto envIt = _envShapes.find(prefix + "EnvShape");
        if (envIt != _envShapes.end())
            envIt.value()->setShape(snap.envelope_shape);
    }

    // Show AY #1 immediately when active; hide after 5s timeout when inactive
    if (_ay1Panel)
    {
        if (hasChip1)
        {
            _ay1ActiveTimer.start();
            _ay1WasActive = true;
            _ay1Panel->setVisible(true);
        }
        else if (_ay1WasActive && _ay1ActiveTimer.elapsed() > AY1_HIDE_TIMEOUT_MS)
        {
            _ay1WasActive = false;
            _ay1Panel->setVisible(false);
        }
    }
}

void ChipStateWidget::refreshFDC()
{
    auto* manifest = static_cast<ManifestHeader*>(_shmBase);
    for (uint16_t i = 0; i < manifest->section_count; i++)
    {
        auto* desc = getDescriptor(_shmBase, manifest, i);
        if (!desc || desc->type != SectionType::CHIP_STATE_FDC)
            continue;

        FDCStateSnapshot snap;
        if (!epochSafeRead(desc, _shmBase, &snap, sizeof(snap)))
            return;

        auto hex2 = [](uint8_t v) { return QString("%1").arg(v, 2, 16, QChar('0')).toUpper(); };

        setField("fdc.Command", hex2(snap.command_register));
        setField("fdc.Status", hex2(snap.status_register));
        setField("fdc.Track", QString::number(snap.track_register));
        setField("fdc.Sector", QString::number(snap.sector_register));
        setField("fdc.Data", hex2(snap.data_register));
        setField("fdc.Drive", QString::number(snap.selected_drive));
        setField("fdc.Side", QString::number(snap.side));
        setField("fdc.FSM", QString::number(snap.fsm_state));
        setField("fdc.Beta128", hex2(snap.beta128_status));
        setField("fdc.LastCmd", hex2(snap.last_command));
        setField("fdc.INTRQ", snap.intrq ? "YES" : "no");
        setField("fdc.DRQ", snap.drq ? "YES" : "no");
        setField("fdc.LostData", snap.error_lost_data ? "ERR" : "ok");
        setField("fdc.CRC", snap.error_crc ? "ERR" : "ok");
        setField("fdc.NotFound", snap.error_not_found ? "ERR" : "ok");
        setField("fdc.WriteFault", snap.error_write_fault ? "ERR" : "ok");

        QString disks;
        for (int d = 0; d < 4; d++)
            disks += (snap.disk_inserted & (1 << d)) ? QString("D%1 ").arg(d) : "-- ";
        setField("fdc.Disks", disks.trimmed());
        return;
    }
}

// =============================================================================
// Shared indicator helpers
// =============================================================================

float ChipStateWidget::indicatorIntensity(const ValueActivity& act, qint64 now)
{
    if (act.lastChangeMs == 0)
        return 0.0f;

    qint64 elapsed = now - act.lastChangeMs;
    if (elapsed > ACTIVITY_TIMEOUT_MS)
        return 0.0f;

    // peakHeat = snapshot of heat/HEAT_DIVISOR at last change event (0..1)
    // timeFade = linear 1.0 at lastChange → 0.0 at timeout (smooth 3s fade)
    float timeFade = 1.0f - static_cast<float>(elapsed) / ACTIVITY_TIMEOUT_MS;

    return act.peakHeat * timeFade;
}

QString ChipStateWidget::indicatorSquare(float intensity,
                                          double rRatio, double gRatio, double bRatio)
{
    // Direct intensity → brightness mapping for smooth fade over full timeout
    int bright = static_cast<int>(255 * intensity);
    QString color = QColor(
        static_cast<int>(bright * rRatio),
        static_cast<int>(bright * gRatio),
        static_cast<int>(bright * bRatio)).name();
    return QString("<span style='color:%1;font-size:%2px'>&#9632;</span>")
        .arg(color).arg(INDICATOR_FONT_PX);
}

// =============================================================================
// Activity Indicators — Z80, FDC, AY decoded fields (value-change based)
// =============================================================================

void ChipStateWidget::updateActivityColors()
{
    qint64 now = _activityClock.elapsed();

    for (auto it = _valueActivity.begin(); it != _valueActivity.end(); ++it)
    {
        const QString& key = it.key();
        ValueActivity& act = it.value();

        // Skip internal AY activity keys (e.g. "ay0.R0.w", "ay0.R0.r")
        if (key.contains(".w") || key.contains(".r"))
            continue;

        // Skip AY register labels — handled by formatAYRegLabels
        if ((key.startsWith("ay0.R") || key.startsWith("ay1.R")) && key.length() <= 7)
            continue;

        // Update value label as plain text
        auto fieldIt = _fields.find(key);
        if (fieldIt != _fields.end())
        {
            QLabel* lbl = fieldIt.value();
            lbl->setTextFormat(Qt::PlainText);
            lbl->setText(act.value);
            lbl->setStyleSheet("");
        }

        // Update separate indicator label
        auto indIt = _indicators.find(key);
        if (indIt == _indicators.end())
            continue;

        QLabel* ind = indIt.value();
        float intensity = indicatorIntensity(act, now);

        if (intensity < INDICATOR_MIN)
        {
            ind->setText("");
        }
        else
        {
            ind->setText(indicatorSquare(intensity,
                                         CHANGE_INDICATOR_R_RATIO,
                                         CHANGE_INDICATOR_G_RATIO,
                                         CHANGE_INDICATOR_B_RATIO));
        }
    }
}

// =============================================================================
// AY Register Labels with Separate Activity Indicator Column
// =============================================================================

void ChipStateWidget::formatAYRegLabels(int chipIdx, const AYStateSnapshot& snap)
{
    QString prefix = QString("ay%1.").arg(chipIdx);
    qint64 now = _activityClock.elapsed();

    // Check if emulator provides activity counters
    bool hasEmulatorCounters = false;
    for (int i = 0; i < 16; i++)
    {
        if (snap.reg_write_counts[i] > 0 || snap.reg_read_mask != 0)
        {
            hasEmulatorCounters = true;
            break;
        }
    }

    for (int reg = 0; reg < 16; reg++)
    {
        QString key = prefix + QString("R%1").arg(reg);
        auto it = _fields.find(key);
        if (it == _fields.end())
            continue;

        QLabel* lbl = it.value();
        QString hexVal = QString("%1").arg(snap.registers[reg], 2, 16, QChar('0')).toUpper();

        // Track write activity heat
        QString wKey = key + ".w";
        ValueActivity& wAct = _valueActivity[wKey];

        if (hasEmulatorCounters)
        {
            if (snap.reg_write_counts[reg] > 0)
            {
                wAct.lastChangeMs = now;
                wAct.heat = qMin(wAct.heat + static_cast<float>(snap.reg_write_counts[reg]), HEAT_MAX);
                wAct.peakHeat = qMin(wAct.heat / HEAT_DIVISOR, 1.0f);
            }
            else
            {
                wAct.heat *= HEAT_DECAY;
            }
        }
        else
        {
            // Fallback: detect value changes from companion side
            bool changed = _prevAYRegsValid[chipIdx] &&
                           (snap.registers[reg] != _prevAYRegs[chipIdx][reg]);
            if (changed)
            {
                wAct.lastChangeMs = now;
                wAct.heat = qMin(wAct.heat + 2.0f, HEAT_MAX);
                wAct.peakHeat = qMin(wAct.heat / HEAT_DIVISOR, 1.0f);
            }
            else
            {
                wAct.heat *= HEAT_DECAY;
            }
        }

        // Track read activity heat
        QString rKey = key + ".r";
        ValueActivity& rAct = _valueActivity[rKey];
        if (hasEmulatorCounters)
        {
            bool wasRead = (snap.reg_read_mask & (1 << reg)) != 0;
            if (wasRead)
            {
                rAct.lastChangeMs = now;
                rAct.heat = qMin(rAct.heat + HEAT_BOOST, HEAT_MAX);
                rAct.peakHeat = qMin(rAct.heat / HEAT_DIVISOR, 1.0f);
            }
            else
            {
                rAct.heat *= HEAT_DECAY;
            }
        }
        else
        {
            rAct.heat *= HEAT_DECAY;
        }

        // Set value as plain text
        lbl->setTextFormat(Qt::PlainText);
        lbl->setText(hexVal);
        lbl->setStyleSheet("");

        // Update separate indicator label with read/write squares
        auto indIt = _indicators.find(key);
        if (indIt == _indicators.end())
            continue;

        QLabel* ind = indIt.value();
        float readIntensity = indicatorIntensity(rAct, now);
        float writeIntensity = indicatorIntensity(wAct, now);

        if (readIntensity < INDICATOR_MIN && writeIntensity < INDICATOR_MIN)
        {
            ind->setText("");
        }
        else
        {
            QString html;
            if (readIntensity >= INDICATOR_MIN)
                html += indicatorSquare(readIntensity,
                                        READ_INDICATOR_R_RATIO,
                                        READ_INDICATOR_G_RATIO,
                                        READ_INDICATOR_B_RATIO);
            if (writeIntensity >= INDICATOR_MIN)
                html += indicatorSquare(writeIntensity,
                                        WRITE_INDICATOR_R_RATIO,
                                        WRITE_INDICATOR_G_RATIO,
                                        WRITE_INDICATOR_B_RATIO);
            ind->setText(html);
        }
    }

    // Store current values for next-frame comparison (fallback mode)
    std::memcpy(_prevAYRegs[chipIdx], snap.registers, 16);
    _prevAYRegsValid[chipIdx] = true;
}
