#include "mainwindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QSplitter>
#include <QTextStream>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("Z80 Memory Visualizer");
    resize(1400, 700);

    // Try loading from command-line argument first
    QStringList args = qApp->arguments();
    bool loaded = false;
    if (args.size() > 1 && args.at(1).endsWith(".uzvd"))
    {
        loaded = _data.loadFromFile(args.at(1).toStdString());
    }
    if (!loaded)
    {
        _data.generate();
    }

    // Central widget
    auto* central = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // Left side: control panel (fixed width sidebar, Python: 190px)
    auto* controlPanel = new QWidget(central);
    controlPanel->setFixedWidth(190);
    buildControlPanel(controlPanel);

    // Divider line
    auto* divider = new QFrame(central);
    divider->setFrameShape(QFrame::VLine);
    divider->setLineWidth(1);

    // Right side: 2x2 bank grid
    auto* bankGrid = new QWidget(central);
    auto* gridLayout = new QGridLayout(bankGrid);
    gridLayout->setSpacing(16);

    for (int i = 0; i < 4; i++)
    {
        _bankWidgets[i] = new Memory16KBWidget(i, bankGrid);
        _bankWidgets[i]->setData(&_data);
        gridLayout->addWidget(_bankWidgets[i], i / 2, i % 2);
    }
    bankGrid->setLayout(gridLayout);

    mainLayout->addWidget(controlPanel, 0);
    mainLayout->addWidget(divider, 0);
    mainLayout->addWidget(bankGrid, 1);

    central->setLayout(mainLayout);
    setCentralWidget(central);

    // Apply default theme
    applyTheme(_darkTheme);
}

void MainWindow::buildControlPanel(QWidget* parent)
{
    auto* layout = new QVBoxLayout(parent);
    layout->setContentsMargins(8, 10, 8, 8);
    layout->setSpacing(6);

    // Title (Python style)
    auto* title = new QLabel("Z80 MEMORY VIZ", parent);
    title->setAlignment(Qt::AlignCenter);
    title->setObjectName("titleLabel");   // styled by applyTheme via QLabel#titleLabel
    layout->addWidget(title);

    auto* subtitle = new QLabel("Unreal-NG Debugger", parent);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setObjectName("subtitleLabel");  // styled by applyTheme via QLabel#subtitleLabel
    layout->addWidget(subtitle);

    // Separator
    auto* sep1 = new QFrame(parent);
    sep1->setFrameShape(QFrame::HLine);
    layout->addWidget(sep1);

    // --- Data source ---
    {
        _btnLoadData = new QPushButton("Load .uzvd...", parent);
        layout->addWidget(_btnLoadData);
        connect(_btnLoadData, &QPushButton::clicked, this, &MainWindow::loadDataFile);

        _lblDataSource = new QLabel(_data.isLoaded ? "<i>Loaded from file</i>" : "<i>Synthetic data</i>", parent);
        _lblDataSource->setAlignment(Qt::AlignCenter);
        _lblDataSource->setWordWrap(true);
        _lblDataSource->setObjectName("subtitleLabel");  // picks up themed font-size + color
        layout->addWidget(_lblDataSource);
    }

    auto* sep2 = new QFrame(parent);
    sep2->setFrameShape(QFrame::HLine);
    layout->addWidget(sep2);

    // --- Access Overlays (Python: defaults ON) ---
    {
        auto* group = new QGroupBox("Access Overlays", parent);
        auto* vl = new QVBoxLayout(group);
        vl->setSpacing(4);

        _cbRead = new QCheckBox("Read", group);
        _cbRead->setChecked(true);
        connect(_cbRead, &QCheckBox::toggled, this, [this](bool c) {
            for (auto* w : _bankWidgets) w->setShowReadOverlay(c);
        });
        vl->addWidget(_cbRead);

        _cbWrite = new QCheckBox("Write", group);
        _cbWrite->setChecked(true);
        connect(_cbWrite, &QCheckBox::toggled, this, [this](bool c) {
            for (auto* w : _bankWidgets) w->setShowWriteOverlay(c);
        });
        vl->addWidget(_cbWrite);

        _cbExecute = new QCheckBox("Execute", group);
        _cbExecute->setChecked(true);
        connect(_cbExecute, &QCheckBox::toggled, this, [this](bool c) {
            for (auto* w : _bankWidgets) w->setShowExecuteOverlay(c);
        });
        vl->addWidget(_cbExecute);

        layout->addWidget(group);
    }

    // --- Control Flow (Python: individual checkboxes) ---
    {
        auto* group = new QGroupBox("Control Flow", parent);
        auto* vl = new QVBoxLayout(group);
        vl->setSpacing(4);

        _cbCFHeatmap = new QCheckBox("Heatmap", group);
        _cbCFHeatmap->setChecked(true);
        connect(_cbCFHeatmap, &QCheckBox::toggled, this, [this](bool c) {
            for (auto* w : _bankWidgets) w->setShowCFHeatmap(c);
        });
        vl->addWidget(_cbCFHeatmap);

        _cbCFSources = new QCheckBox("Sources", group);
        connect(_cbCFSources, &QCheckBox::toggled, this, [this](bool c) {
            for (auto* w : _bankWidgets) w->setShowCFSources(c);
        });
        vl->addWidget(_cbCFSources);

        _cbCFTargets = new QCheckBox("Targets", group);
        connect(_cbCFTargets, &QCheckBox::toggled, this, [this](bool c) {
            for (auto* w : _bankWidgets) w->setShowCFTargets(c);
        });
        vl->addWidget(_cbCFTargets);

        _cbCFArcs = new QCheckBox("Show CF Arcs", group);
        _cbCFArcs->setChecked(true);
        connect(_cbCFArcs, &QCheckBox::toggled, this, [this](bool c) {
            for (auto* w : _bankWidgets) w->setShowCFArcs(c);
        });
        vl->addWidget(_cbCFArcs);

        layout->addWidget(group);
    }

    auto* sep3 = new QFrame(parent);
    sep3->setFrameShape(QFrame::HLine);
    layout->addWidget(sep3);

    // --- Appearance ---
    {
        auto* group = new QGroupBox("Appearance", parent);
        auto* vl = new QVBoxLayout(group);
        vl->setSpacing(4);

        _cbDarkTheme = new QCheckBox("Dark Theme", group);
        _cbDarkTheme->setChecked(_darkTheme);
        connect(_cbDarkTheme, &QCheckBox::toggled, this, [this](bool checked) {
            _darkTheme = checked;
            applyTheme(checked);
            for (auto* w : _bankWidgets) w->setDarkTheme(checked);
        });
        vl->addWidget(_cbDarkTheme);

        _cbHideValues = new QCheckBox("Hide Memory Values", group);
        connect(_cbHideValues, &QCheckBox::toggled, this, [this](bool checked) {
            for (auto* w : _bankWidgets) w->setHideValues(checked);
        });
        vl->addWidget(_cbHideValues);

        // Glow radius slider (Python: 0.0–8.0, default 1.8, step 0.1)
        auto* glowRow = new QHBoxLayout();
        auto* glowLabel = new QLabel("Glow radius", group);
        glowLabel->setObjectName("subtitleLabel");  // themed 10px color
        _glowValueLabel = new QLabel("1.8", group);
        _glowValueLabel->setObjectName("bankStats");  // themed monospace 10px
        _glowValueLabel->setAlignment(Qt::AlignRight);
        glowRow->addWidget(glowLabel);
        glowRow->addWidget(_glowValueLabel);
        vl->addLayout(glowRow);

        _glowSlider = new QSlider(Qt::Horizontal, group);
        _glowSlider->setRange(0, 80);       // 0.0 – 8.0 (value / 10)
        _glowSlider->setValue(18);           // Default 1.8
        _glowSlider->setTickPosition(QSlider::TicksBelow);
        _glowSlider->setTickInterval(10);
        connect(_glowSlider, &QSlider::valueChanged, this, [this](int value) {
            float sigma = value / 10.0f;
            _glowValueLabel->setText(QString::number(sigma, 'f', 1));
            for (auto* w : _bankWidgets) w->setGlowSigma(sigma);
        });
        vl->addWidget(_glowSlider);

        layout->addWidget(group);
    }

    // --- Extra overlays (C++ PoC extras not in Python) ---
    {
        auto* group = new QGroupBox("Analysis Layers", parent);
        auto* vl = new QVBoxLayout(group);
        vl->setSpacing(4);

        _cbOpcodeTrace = new QCheckBox("Opcode Trace (Purple)", group);
        connect(_cbOpcodeTrace, &QCheckBox::toggled, this, [this](bool c) {
            for (auto* w : _bankWidgets) w->setShowOpcodeTraceOverlay(c);
        });
        vl->addWidget(_cbOpcodeTrace);

        _cbEntropy = new QCheckBox("Entropy Map (Viridis)", group);
        connect(_cbEntropy, &QCheckBox::toggled, this, [this](bool c) {
            for (auto* w : _bankWidgets) w->setShowEntropyOverlay(c);
        });
        vl->addWidget(_cbEntropy);

        _cbFreshness = new QCheckBox("Memory Freshness", group);
        connect(_cbFreshness, &QCheckBox::toggled, this, [this](bool c) {
            for (auto* w : _bankWidgets) w->setShowFreshnessOverlay(c);
        });
        vl->addWidget(_cbFreshness);

        _cbRegion = new QCheckBox("Region Classification", group);
        connect(_cbRegion, &QCheckBox::toggled, this, [this](bool c) {
            for (auto* w : _bankWidgets) w->setShowRegionOverlay(c);
        });
        vl->addWidget(_cbRegion);

        layout->addWidget(group);
    }

    layout->addStretch(1);

    // --- Legend ---
    buildLegend(parent);

    parent->setLayout(layout);
}

void MainWindow::buildLegend(QWidget* parent)
{
    auto* group = new QGroupBox("Legend", parent);
    auto* vl = new QVBoxLayout(group);
    vl->setSpacing(2);

    struct LegendEntry { const char* name; QColor color; };
    LegendEntry entries[] = {
        {"Memory value",   QColor(0x99, 0x99, 0x99)},
        {"Read (blue)",    QColor(0x4d, 0x7a, 0xff)},
        {"Write (red)",    QColor(0xff, 0x44, 0x44)},
        {"Execute (green)",QColor(0x33, 0xcc, 0x66)},
        {"CF Heatmap",     QColor(0xff, 0xaa, 0x1e)},
        {"CF Source",      QColor(0xff, 0x66, 0x00)},
        {"CF Target",      QColor(0x22, 0xdd, 0xcc)},
    };

    int swatchIdx = 0;
    for (const auto& e : entries)
    {
        auto* row = new QHBoxLayout();
        auto* swatch = new QLabel(group);
        swatch->setFixedSize(14, 14);
        swatch->setStyleSheet(QString("background-color: rgb(%1, %2, %3); border: 1px solid #888;")
                               .arg(e.color.red()).arg(e.color.green()).arg(e.color.blue()));
        _legendSwatches[swatchIdx++] = swatch;  // saved for theme updates
        auto* label = new QLabel(e.name, group);
        label->setObjectName("bankStats");  // themed 10px monospace
        row->addWidget(swatch);
        row->addWidget(label, 1);
        vl->addLayout(row);
    }

    parent->layout()->addWidget(group);
}

void MainWindow::applyTheme(bool dark)
{
    // ── Palette ────────────────────────────────────────────────────────
    // Matches Python prototype palette exactly.
    // Dark:  BG_DEEP=(14,14,18)  BG_CHROME=(28,28,34)  BG_PANEL=(22,22,28)
    //        FG_BRIGHT=(220,220,228)  ACCENT=(42,130,218)
    // Light: Window=(240,240,244)  WindowText=(30,30,36)  Button=(225,225,232)
    QPalette pal;
    if (dark)
    {
        pal.setColor(QPalette::Window,          QColor(14,  14,  18));   // BG_DEEP
        pal.setColor(QPalette::WindowText,      QColor(220, 220, 228));  // FG_BRIGHT
        pal.setColor(QPalette::Base,            QColor(22,  22,  28));   // BG_PANEL
        pal.setColor(QPalette::AlternateBase,   QColor(28,  28,  34));   // BG_CHROME
        pal.setColor(QPalette::Text,            QColor(220, 220, 228));
        pal.setColor(QPalette::Button,          QColor(28,  28,  34));   // BG_CHROME
        pal.setColor(QPalette::ButtonText,      QColor(220, 220, 228));
        pal.setColor(QPalette::Highlight,       QColor(42,  130, 218));  // ACCENT
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::ToolTipBase,     QColor(35,  35,  45));
        pal.setColor(QPalette::ToolTipText,     QColor(220, 220, 228));
    }
    else
    {
        pal.setColor(QPalette::Window,          QColor(240, 240, 244));
        pal.setColor(QPalette::WindowText,      QColor(30,  30,  36));
        pal.setColor(QPalette::Base,            QColor(255, 255, 255));
        pal.setColor(QPalette::AlternateBase,   QColor(232, 232, 238));
        pal.setColor(QPalette::Text,            QColor(30,  30,  36));
        pal.setColor(QPalette::Button,          QColor(225, 225, 232));
        pal.setColor(QPalette::ButtonText,      QColor(30,  30,  36));
        pal.setColor(QPalette::Highlight,       QColor(42,  130, 218));  // ACCENT
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::ToolTipBase,     QColor(255, 255, 220));
        pal.setColor(QPalette::ToolTipText,     QColor(30,  30,  36));
    }
    qApp->setPalette(pal);

    // ── Stylesheet ─────────────────────────────────────────────────────
    // Mirrors Python STYLESHEET / LIGHT_STYLESHEET as closely as possible:
    //   - QGroupBox: background, left-aligned title, uppercase, letter-spacing
    //   - QPushButton: padding 6px 14px, hover changes border to accent, pressed fills accent
    //   - QScrollBar: slim 8px bar matching Python
    //   - QStatusBar, QToolTip: exact Python values

    if (dark)
    {
        qApp->setStyleSheet(
            // ── global ──────────────────────────────────────────────────
            "QMainWindow, QWidget {"
            "  background-color: rgb(14,14,18);"          // BG_DEEP
            "  color: rgb(220,220,228);"                   // FG_BRIGHT
            "  font-family: -apple-system, 'SF Pro Display', 'Helvetica Neue', Arial;"
            "  font-size: 12px;"
            "}"
            "QScrollArea { border: none; background-color: rgb(14,14,18); }"

            // ── scrollbar ────────────────────────────────────────────────
            "QScrollBar:vertical { background: rgb(28,28,34); width: 8px; border: none; }"
            "QScrollBar::handle:vertical { background: rgb(55,55,70); border-radius: 4px; min-height: 20px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
            "QScrollBar:horizontal { background: rgb(28,28,34); height: 8px; border: none; }"
            "QScrollBar::handle:horizontal { background: rgb(55,55,70); border-radius: 4px; min-width: 20px; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"

            // ── group box ────────────────────────────────────────────────
            // Python: border 1px solid #666, bg BG_PANEL, radius 5px, margin-top 10px
            "QGroupBox {"
            "  background-color: rgb(22,22,28);"           // BG_PANEL
            "  border: 1px solid #666666;"
            "  border-radius: 5px;"
            "  margin-top: 10px;"
            "  padding: 8px 6px 6px 6px;"
            "  color: #bbbbbb;"
            "  font-size: 11px;"
            "  font-weight: bold;"
            "  text-transform: uppercase;"
            "}"
            "QGroupBox::title {"
            "  subcontrol-origin: margin;"
            "  left: 8px;"
            "  padding: 0 3px;"
            "  color: #bbbbbb;"
            "}"

            // ── checkboxes ───────────────────────────────────────────────
            "QCheckBox { spacing: 7px; color: rgb(220,220,228); font-size: 12px; }"
            "QCheckBox::indicator { width: 14px; height: 14px; border: 2px solid #888; border-radius: 3px; background: rgb(28,28,34); }"
            "QCheckBox::indicator:checked { background: rgb(42,130,218); border-color: rgb(42,130,218); image: url(:/checkmark_white.svg); }"

            // ── push button ──────────────────────────────────────────────
            // Python: bg BG_CHROME, border BORDER, radius 4px, padding 6px 14px, weight 600
            "QPushButton {"
            "  background-color: rgb(28,28,34);"           // BG_CHROME
            "  border: 1px solid rgb(55,55,70);"           // BORDER
            "  border-radius: 4px;"
            "  padding: 6px 14px;"
            "  color: rgb(220,220,228);"
            "  font-weight: 600;"
            "}"
            "QPushButton:hover {"
            "  background-color: rgb(42,42,52);"
            "  border-color: rgb(42,130,218);"             // ACCENT on hover
            "  color: white;"
            "}"
            "QPushButton:pressed {"
            "  background-color: rgb(42,130,218);"         // ACCENT fill when pressed
            "  border-color: rgb(42,130,218);"
            "}"

            // ── labels ───────────────────────────────────────────────────
            "QLabel { color: rgb(220,220,228); }"
            "QLabel#bankHeader { color: rgb(255,170,30); font-family: 'Menlo','Monaco','Courier New',monospace; font-size: 11px; font-weight: bold; padding: 2px 0; }"
            "QLabel#bankStats  { color: rgb(130,130,148); font-family: 'Menlo','Monaco','Courier New',monospace; font-size: 10px; padding: 1px 0; }"
            "QLabel#hoverLabel { color: rgb(40,210,210); font-family: 'Menlo','Monaco','Courier New',monospace; font-size: 10px; padding: 1px 0; }"
            "QLabel#titleLabel { color: rgb(255,170,30); font-size: 14px; font-weight: 700; }"
            "QLabel#subtitleLabel { color: rgb(130,130,148); font-size: 10px; }"

            // ── separators ───────────────────────────────────────────────
            "QFrame[frameShape='4'] { color: rgb(55,55,70); }"   // HLine
            "QFrame[frameShape='5'] { color: rgb(55,55,70); }"   // VLine
            "QFrame#separator { background: rgb(55,55,70); max-height: 1px; min-height: 1px; }"
            "QFrame#bankSeparator { background: rgb(55,55,70); max-height: 1px; min-height: 1px; margin: 4px 0; }"

            // ── slider ───────────────────────────────────────────────────
            "QSlider::groove:horizontal { background: rgb(55,55,70); height: 4px; border-radius: 2px; }"
            "QSlider::handle:horizontal { background: rgb(255,170,30); width: 12px; margin: -4px 0; border-radius: 6px; }"

            // ── status bar ───────────────────────────────────────────────
            "QStatusBar { background: rgb(28,28,34); color: rgb(130,130,148); font-size: 10px; border-top: 1px solid rgb(55,55,70); }"

            // ── tooltip ──────────────────────────────────────────────────
            "QToolTip { background-color: #353540; color: rgb(220,220,228); border: 1px solid rgb(55,55,70); padding: 0; font-size: 12px; }"

            // ── combo box ────────────────────────────────────────────────
            "QComboBox { color: rgb(220,220,228); background: rgb(28,28,34); border: 1px solid rgb(55,55,70); border-radius: 4px; padding: 2px 6px; }"
            "QComboBox QAbstractItemView { color: rgb(220,220,228); background: rgb(22,22,28); }"
        );
    }
    else
    {
        qApp->setStyleSheet(
            // ── global ──────────────────────────────────────────────────
            "QMainWindow, QWidget {"
            "  background-color: #f0f0f4;"
            "  color: #1e1e24;"
            "  font-family: -apple-system, 'SF Pro Display', 'Helvetica Neue', Arial;"
            "  font-size: 12px;"
            "}"
            "QScrollArea { border: none; background-color: #f0f0f4; }"

            // ── scrollbar ────────────────────────────────────────────────
            "QScrollBar:vertical { background: #e0e0e8; width: 8px; border: none; }"
            "QScrollBar::handle:vertical { background: #b0b0c0; border-radius: 4px; min-height: 20px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
            "QScrollBar:horizontal { background: #e0e0e8; height: 8px; border: none; }"
            "QScrollBar::handle:horizontal { background: #b0b0c0; border-radius: 4px; min-width: 20px; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"

            // ── group box ────────────────────────────────────────────────
            // Use the same background as the widget base so there's a single
            // solid colour inside the control panel — no two-tone effect.
            "QGroupBox {"
            "  background-color: #f0f0f4;"
            "  border: 1px solid #b8b8c8;"
            "  border-radius: 5px;"
            "  margin-top: 10px;"
            "  padding: 8px 6px 6px 6px;"
            "  color: #555566;"
            "  font-size: 11px;"
            "  font-weight: bold;"
            "  text-transform: uppercase;"
            "}"
            "QGroupBox::title {"
            "  subcontrol-origin: margin;"
            "  left: 8px;"
            "  padding: 0 3px;"
            "  color: #555566;"
            "}"

            // ── checkboxes ───────────────────────────────────────────────
            "QCheckBox { spacing: 7px; color: #1e1e24; font-size: 12px; }"
            "QCheckBox::indicator { width: 14px; height: 14px; border: 2px solid #999; border-radius: 3px; background: #f0f0f4; }"
            "QCheckBox::indicator:checked { background: #2a82da; border-color: #2a82da; image: url(:/checkmark_white.svg); }"

            // ── push button ──────────────────────────────────────────────
            // Python: bg #e0e0e8, border 1px solid #b0b0c0, radius 4px, padding 6px 14px, weight 600
            "QPushButton {"
            "  background-color: #e0e0e8;"
            "  border: 1px solid #b0b0c0;"
            "  border-radius: 4px;"
            "  padding: 6px 14px;"
            "  color: #1e1e24;"
            "  font-weight: 600;"
            "}"
            "QPushButton:hover {"
            "  background-color: #d0d0dc;"
            "  border-color: #2a82da;"                    // ACCENT on hover
            "}"
            "QPushButton:pressed {"
            "  background-color: #2a82da;"               // ACCENT fill when pressed
            "  border-color: #2a82da;"
            "  color: white;"
            "}"

            // ── labels ───────────────────────────────────────────────────
            "QLabel { color: #1e1e24; }"
            "QLabel#bankHeader { color: #b06000; font-family: 'Menlo','Monaco','Courier New',monospace; font-size: 11px; font-weight: bold; padding: 2px 0; }"
            "QLabel#bankStats  { color: #555566; font-family: 'Menlo','Monaco','Courier New',monospace; font-size: 10px; padding: 1px 0; }"
            "QLabel#hoverLabel { color: #007b7b; font-family: 'Menlo','Monaco','Courier New',monospace; font-size: 10px; padding: 1px 0; }"
            "QLabel#titleLabel { color: #b06000; font-size: 14px; font-weight: 700; }"
            "QLabel#subtitleLabel { color: #555566; font-size: 10px; }"

            // ── separators ───────────────────────────────────────────────
            "QFrame[frameShape='4'] { color: #b8b8c8; }"  // HLine
            "QFrame[frameShape='5'] { color: #b8b8c8; }"  // VLine
            "QFrame#separator { background: #b8b8c8; max-height: 1px; min-height: 1px; }"
            "QFrame#bankSeparator { background: #b8b8c8; max-height: 1px; min-height: 1px; margin: 4px 0; }"

            // ── slider ───────────────────────────────────────────────────
            "QSlider::groove:horizontal { background: #b8b8c8; height: 4px; border-radius: 2px; }"
            "QSlider::handle:horizontal { background: #2a82da; width: 12px; margin: -4px 0; border-radius: 6px; }"

            // ── status bar ───────────────────────────────────────────────
            "QStatusBar { background: #e0e0e8; color: #555566; font-size: 10px; border-top: 1px solid #b8b8c8; }"

            // ── tooltip ──────────────────────────────────────────────────
            "QToolTip { background-color: #fffff0; color: #1e1e24; border: 1px solid #b8b8c8; padding: 0; font-size: 12px; }"

            // ── combo box ────────────────────────────────────────────────
            "QComboBox { color: #1e1e24; background: #e0e0e8; border: 1px solid #b0b0c0; border-radius: 4px; padding: 2px 6px; }"
            "QComboBox QAbstractItemView { color: #1e1e24; background: #f0f0f4; }"
        );
    }

    // ── Theme-aware control colors ────────────────────────────────────────
    // R/W/X use identical hues in both themes (blue/red/green — semantic identity).
    // CF channels differ between themes so they stay readable on each background.
    static const char* cbColDark[]  = {"#4d7aff","#ff4444","#33cc66","#ffaa1e","#ff6600","#22ddcc"};
    static const char* cbColLight[] = {"#4d7aff","#ff4444","#33cc66","#cc6600","#cc4400","#1599aa"};
    QCheckBox* coloredCbs[] = {_cbRead, _cbWrite, _cbExecute, _cbCFHeatmap, _cbCFSources, _cbCFTargets};
    for (int i = 0; i < 6; ++i)
        coloredCbs[i]->setStyleSheet(
            QString("QCheckBox { color: %1; }").arg(dark ? cbColDark[i] : cbColLight[i]));

    // Legend swatches: R/W/X identical between themes; CF adapts for readability.
    static const QColor swatchDark[7]  = {
        {0x77,0x77,0x77}, {0x4d,0x7a,0xff}, {0xff,0x44,0x44},
        {0x33,0xcc,0x66}, {0xff,0xaa,0x1e}, {0xff,0x66,0x00}, {0x22,0xdd,0xcc}
    };
    static const QColor swatchLight[7] = {
        {0x55,0x55,0x55}, {0x4d,0x7a,0xff}, {0xff,0x44,0x44},
        {0x33,0xcc,0x66}, {0xcc,0x66,0x00}, {0xcc,0x44,0x00}, {0x15,0x99,0xaa}
    };
    const QColor* sw = dark ? swatchDark : swatchLight;
    const char* sborder = dark ? "#888" : "#aaa";
    for (int i = 0; i < 7; ++i)
        _legendSwatches[i]->setStyleSheet(
            QString("background-color: rgb(%1,%2,%3); border: 1px solid %4;")
                .arg(sw[i].red()).arg(sw[i].green()).arg(sw[i].blue()).arg(sborder));
}

void MainWindow::loadDataFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Open Visualization Data", QString(),
        "Unreal Viz Data (*.uzvd);;All Files (*)");

    if (path.isEmpty())
        return;

    SyntheticData newData;
    if (!newData.loadFromFile(path.toStdString()))
    {
        QMessageBox::warning(this, "Load Error",
            "Failed to load UZVD file. Check that it was exported from the emulator.");
        return;
    }

    _data = std::move(newData);
    for (auto* w : _bankWidgets)
    {
        w->setData(&_data);
        w->update();
    }

    _lblDataSource->setText(QString("<i>%1</i>").arg(QFileInfo(path).fileName()));
    setWindowTitle(QString("Z80 Memory Visualizer \u2014 %1").arg(QFileInfo(path).fileName()));
}
