#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QFrame>
#include <QMouseEvent>
#include <QTimer>
#include <QFontDatabase>
#include <QScreen>
#include <QPainter>
#include <QProgressBar>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>

// Global monospace font
static QFont g_monoFont;

void initMonoFont() {
    g_monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    g_monoFont.setPointSize(12);
}

//=============================================================================
// Theme manager
//=============================================================================
class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance() {
        static ThemeManager tm;
        return tm;
    }

    bool isDark() const { return m_isDark; }
    void setDark(bool dark) {
        if (m_isDark != dark) {
            m_isDark = dark;
            emit themeChanged(dark);
        }
    }

    QString bg() const { return m_isDark ? "#1E1E1E" : "#FFFFFF"; }
    QString fg() const { return m_isDark ? "#D4D4D4" : "#1E1E1E"; }
    QString popupBg() const { return m_isDark ? "#252526" : "#FAFAFA"; }
    QString popupBorder() const { return m_isDark ? "#3C3C3C" : "#E0E0E0"; }
    QString tagBg() const { return m_isDark ? "#3C3C3C" : "#E8E8E8"; }
    QString contentBg() const { return m_isDark ? "#1E1E1E" : "#F5F5F5"; }
    QString hintColor() const { return m_isDark ? "#666666" : "#999999"; }

    QString accentColor() const { return m_isDark ? "#569CD6" : "#0066CC"; }
    QString highlightColor() const { return m_isDark ? "#4EC9B0" : "#267F99"; }
    QString warningColor() const { return m_isDark ? "#DCDCAA" : "#795E26"; }
    QString errorColor() const { return m_isDark ? "#CE9178" : "#A31515"; }

signals:
    void themeChanged(bool isDark);

private:
    ThemeManager() : m_isDark(false) {}
    bool m_isDark = false;
};

//=============================================================================
// T-state data model
//=============================================================================
struct TStateData {
    uint64_t totalTStates = 124432029;
    uint64_t frameTStates = 62792;
    uint32_t frameNumber = 1754;
    uint16_t rayLine = 234;
    uint16_t rayPixel = 260;
    double cpuFreqMHz = 3.5;
    uint32_t tStatesPerFrame = 69888;
    uint32_t tStatesPerLine = 224;

    double frameTimeUs() const { return frameTStates / cpuFreqMHz; }
    double framePercent() const { return (frameTStates * 100.0) / tStatesPerFrame; }
    double totalTimeMs() const { return totalTStates / (cpuFreqMHz * 1000.0); }
};

static TStateData g_data;

//=============================================================================
// Timing popup
//=============================================================================
class TimingPopup : public QFrame {
    Q_OBJECT
public:
    static TimingPopup& instance() {
        static TimingPopup popup;
        return popup;
    }

    void showAt(const QString& title, const TStateData& data, const QPoint& pos) {
        auto& tm = ThemeManager::instance();

        m_titleLabel->setText(title);
        m_titleLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(tm.accentColor()));

        QString tagBg = tm.tagBg();
        m_cpuTag->setText(QString("%1 MHz").arg(data.cpuFreqMHz));
        m_cpuTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tagBg).arg(tm.highlightColor()));

        QString details = QString(
            "Total T-states:    %1\n"
            "Frame T-states:    %2 / %3\n"
            "Frame progress:    %4%\n"
            "Frame time:        %5 us\n"
            "Total time:        %6 ms\n"
            "Raster position:   line %7, pixel %8"
        ).arg(data.totalTStates)
         .arg(data.frameTStates)
         .arg(data.tStatesPerFrame)
         .arg(data.framePercent(), 0, 'f', 1)
         .arg(data.frameTimeUs(), 0, 'f', 2)
         .arg(data.totalTimeMs(), 0, 'f', 2)
         .arg(data.rayLine)
         .arg(data.rayPixel);

        m_details->setText(details);

        adjustSize();
        QPoint finalPos = pos;
        QRect screen = QApplication::primaryScreen()->availableGeometry();
        if (finalPos.x() + width() > screen.right()) {
            finalPos.setX(pos.x() - width() - 10);
        }
        if (finalPos.y() + height() > screen.bottom()) {
            finalPos.setY(screen.bottom() - height());
        }
        move(finalPos);
        show();
    }

private:
    TimingPopup() : QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint) {
        setFrameStyle(QFrame::NoFrame);
        setAttribute(Qt::WA_ShowWithoutActivating);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(6);

        auto* headerRow = new QHBoxLayout();
        m_titleLabel = new QLabel();
        m_titleLabel->setFont(g_monoFont);
        headerRow->addWidget(m_titleLabel);
        m_cpuTag = new QLabel();
        headerRow->addWidget(m_cpuTag);
        headerRow->addStretch();
        layout->addLayout(headerRow);

        m_detailsBox = new QFrame();
        m_detailsBox->setFrameStyle(QFrame::NoFrame);
        auto* detailsLayout = new QVBoxLayout(m_detailsBox);
        detailsLayout->setContentsMargins(8, 6, 8, 6);
        m_details = new QLabel();
        m_details->setFont(g_monoFont);
        detailsLayout->addWidget(m_details);
        layout->addWidget(m_detailsBox);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &TimingPopup::applyTheme);
        applyTheme();
    }

    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString(
            "TimingPopup { background: %1; border: 1px solid %2; border-radius: 8px; }"
        ).arg(tm.popupBg()).arg(tm.popupBorder()));
        m_detailsBox->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(tm.contentBg()));
    }

    QLabel* m_titleLabel;
    QLabel* m_cpuTag;
    QFrame* m_detailsBox;
    QLabel* m_details;
};

//=============================================================================
// Hoverable base widget
//=============================================================================
class HoverableWidget : public QFrame {
    Q_OBJECT
public:
    HoverableWidget(QWidget* parent = nullptr) : QFrame(parent) {
        setMouseTracking(true);
        m_hoverTimer.setSingleShot(true);
        m_hoverTimer.setInterval(400);
        connect(&m_hoverTimer, &QTimer::timeout, this, &HoverableWidget::showPopup);
    }

protected:
    void enterEvent(QEnterEvent*) override {
        TimingPopup::instance().hide();
        m_hoverTimer.start();
    }

    void leaveEvent(QEvent*) override {
        m_hoverTimer.stop();
        TimingPopup::instance().hide();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        m_lastMousePos = e->globalPosition().toPoint();
    }

    virtual void showPopup() {
        TimingPopup::instance().showAt(popupTitle(), g_data, m_lastMousePos + QPoint(15, 15));
    }

    virtual QString popupTitle() const { return "Timing"; }

    QTimer m_hoverTimer;
    QPoint m_lastMousePos;
};

//=============================================================================
// Layout 1: Simple horizontal counters
//=============================================================================
class Layout1 : public HoverableWidget {
    Q_OBJECT
public:
    Layout1(QWidget* parent = nullptr) : HoverableWidget(parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 4, 8, 4);
        layout->setSpacing(16);

        auto addCounter = [&](const QString& label, QLabel*& valueLabel) {
            auto* lbl = new QLabel(label);
            lbl->setFont(g_monoFont);
            layout->addWidget(lbl);
            valueLabel = new QLabel();
            valueLabel->setFont(g_monoFont);
            layout->addWidget(valueLabel);
        };

        addCounter("T:", m_totalLabel);
        addCounter("Frame:", m_frameLabel);
        addCounter("Line:", m_lineLabel);
        addCounter("Pixel:", m_pixelLabel);
        layout->addStretch();

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout1::applyTheme);
        applyTheme();
        updateData();
    }

    void updateData() {
        m_totalLabel->setText(QString::number(g_data.totalTStates));
        m_frameLabel->setText(QString("%1/%2").arg(g_data.frameTStates).arg(g_data.tStatesPerFrame));
        m_lineLabel->setText(QString::number(g_data.rayLine));
        m_pixelLabel->setText(QString::number(g_data.rayPixel));
    }

    QString popupTitle() const override { return "T-State Counter"; }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("Layout1 { background: %1; border-radius: 4px; }").arg(tm.contentBg()));
    }

    QLabel* m_totalLabel;
    QLabel* m_frameLabel;
    QLabel* m_lineLabel;
    QLabel* m_pixelLabel;
};

//=============================================================================
// Layout 2: Compact single-line with frame counter
//=============================================================================
class Layout2 : public HoverableWidget {
    Q_OBJECT
public:
    Layout2(QWidget* parent = nullptr) : HoverableWidget(parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 4, 8, 4);
        layout->setSpacing(8);

        m_label = new QLabel();
        m_label->setFont(g_monoFont);
        layout->addWidget(m_label);
        layout->addStretch();

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout2::applyTheme);
        applyTheme();
        updateData();
    }

    void updateData() {
        m_label->setText(QString("Frame %1  T:%2  Ray:%3,%4")
            .arg(g_data.frameNumber)
            .arg(g_data.frameTStates)
            .arg(g_data.rayLine)
            .arg(g_data.rayPixel));
    }

    QString popupTitle() const override { return "Frame Timing"; }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("Layout2 { background: %1; border-radius: 4px; }").arg(tm.contentBg()));
    }

    QLabel* m_label;
};

//=============================================================================
// Layout 3: Grid layout with labeled values
//=============================================================================
class Layout3 : public HoverableWidget {
    Q_OBJECT
public:
    Layout3(QWidget* parent = nullptr) : HoverableWidget(parent) {
        auto* grid = new QGridLayout(this);
        grid->setContentsMargins(8, 6, 8, 6);
        grid->setSpacing(4);

        auto addRow = [&](int row, const QString& label, QLabel*& valueLabel) {
            auto* lbl = new QLabel(label);
            lbl->setFont(g_monoFont);
            grid->addWidget(lbl, row, 0);
            valueLabel = new QLabel();
            valueLabel->setFont(g_monoFont);
            valueLabel->setAlignment(Qt::AlignRight);
            grid->addWidget(valueLabel, row, 1);
        };

        addRow(0, "Total T:", m_totalLabel);
        addRow(1, "Frame T:", m_frameLabel);
        addRow(2, "Frame #:", m_frameNumLabel);
        addRow(3, "Ray:", m_rayLabel);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout3::applyTheme);
        applyTheme();
        updateData();
    }

    void updateData() {
        auto& tm = ThemeManager::instance();
        m_totalLabel->setText(QString("<span style='color:%1'>%2</span>")
            .arg(tm.accentColor()).arg(g_data.totalTStates));
        m_frameLabel->setText(QString("<span style='color:%1'>%2</span> / %3")
            .arg(tm.highlightColor()).arg(g_data.frameTStates).arg(g_data.tStatesPerFrame));
        m_frameNumLabel->setText(QString::number(g_data.frameNumber));
        m_rayLabel->setText(QString("%1, %2").arg(g_data.rayLine).arg(g_data.rayPixel));
    }

    QString popupTitle() const override { return "Detailed Timing"; }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("Layout3 { background: %1; border-radius: 4px; }").arg(tm.contentBg()));
        updateData();
    }

    QLabel* m_totalLabel;
    QLabel* m_frameLabel;
    QLabel* m_frameNumLabel;
    QLabel* m_rayLabel;
};

//=============================================================================
// Layout 4: Progress bar for frame position
//=============================================================================
class Layout4 : public HoverableWidget {
    Q_OBJECT
public:
    Layout4(QWidget* parent = nullptr) : HoverableWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(4);

        auto* topRow = new QHBoxLayout();
        m_frameLabel = new QLabel();
        m_frameLabel->setFont(g_monoFont);
        topRow->addWidget(m_frameLabel);
        topRow->addStretch();
        m_percentLabel = new QLabel();
        m_percentLabel->setFont(g_monoFont);
        topRow->addWidget(m_percentLabel);
        layout->addLayout(topRow);

        m_progressBar = new QProgressBar();
        m_progressBar->setRange(0, g_data.tStatesPerFrame);
        m_progressBar->setTextVisible(false);
        m_progressBar->setFixedHeight(8);
        layout->addWidget(m_progressBar);

        auto* bottomRow = new QHBoxLayout();
        m_tLabel = new QLabel();
        m_tLabel->setFont(g_monoFont);
        bottomRow->addWidget(m_tLabel);
        bottomRow->addStretch();
        m_rayLabel = new QLabel();
        m_rayLabel->setFont(g_monoFont);
        bottomRow->addWidget(m_rayLabel);
        layout->addLayout(bottomRow);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout4::applyTheme);
        applyTheme();
        updateData();
    }

    void updateData() {
        m_frameLabel->setText(QString("Frame %1").arg(g_data.frameNumber));
        m_percentLabel->setText(QString("%1%").arg(g_data.framePercent(), 0, 'f', 1));
        m_progressBar->setValue(g_data.frameTStates);
        m_tLabel->setText(QString("T: %1").arg(g_data.frameTStates));
        m_rayLabel->setText(QString("Ray: %1,%2").arg(g_data.rayLine).arg(g_data.rayPixel));
    }

    QString popupTitle() const override { return "Frame Progress"; }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("Layout4 { background: %1; border-radius: 4px; }").arg(tm.contentBg()));
        m_progressBar->setStyleSheet(QString(
            "QProgressBar { background: %1; border: none; border-radius: 4px; }"
            "QProgressBar::chunk { background: %2; border-radius: 4px; }"
        ).arg(tm.tagBg()).arg(tm.accentColor()));
    }

    QLabel* m_frameLabel;
    QLabel* m_percentLabel;
    QProgressBar* m_progressBar;
    QLabel* m_tLabel;
    QLabel* m_rayLabel;
};

//=============================================================================
// Layout 5: Visual raster position indicator
//=============================================================================
class RasterWidget : public QWidget {
    Q_OBJECT
public:
    RasterWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(80, 60);
    }

    void setPosition(int line, int pixel, int maxLines = 312, int maxPixels = 448) {
        m_line = line;
        m_pixel = pixel;
        m_maxLines = maxLines;
        m_maxPixels = maxPixels;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        auto& tm = ThemeManager::instance();

        // Screen area
        p.fillRect(rect(), QColor(tm.contentBg()));
        QRect screen(4, 4, width() - 8, height() - 8);
        p.fillRect(screen, QColor(tm.tagBg()));

        // Active display area (lines 64-256 approx)
        int activeTop = screen.top() + (64 * screen.height() / m_maxLines);
        int activeBottom = screen.top() + (256 * screen.height() / m_maxLines);
        int activeLeft = screen.left() + (128 * screen.width() / m_maxPixels);
        int activeRight = screen.left() + (384 * screen.width() / m_maxPixels);
        p.fillRect(activeLeft, activeTop, activeRight - activeLeft, activeBottom - activeTop,
                   QColor(tm.isDark() ? "#333333" : "#DDDDDD"));

        // Raster position
        int x = screen.left() + (m_pixel * screen.width() / m_maxPixels);
        int y = screen.top() + (m_line * screen.height() / m_maxLines);
        p.setPen(QPen(QColor(tm.errorColor()), 2));
        p.drawLine(screen.left(), y, screen.right(), y);
        p.drawLine(x, screen.top(), x, screen.bottom());
        p.setBrush(QColor(tm.errorColor()));
        p.drawEllipse(QPoint(x, y), 3, 3);
    }

private:
    int m_line = 0, m_pixel = 0;
    int m_maxLines = 312, m_maxPixels = 448;
};

class Layout5 : public HoverableWidget {
    Q_OBJECT
public:
    Layout5(QWidget* parent = nullptr) : HoverableWidget(parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(12);

        m_rasterWidget = new RasterWidget();
        layout->addWidget(m_rasterWidget);

        auto* infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(2);

        m_frameLabel = new QLabel();
        m_frameLabel->setFont(g_monoFont);
        infoLayout->addWidget(m_frameLabel);

        m_lineLabel = new QLabel();
        m_lineLabel->setFont(g_monoFont);
        infoLayout->addWidget(m_lineLabel);

        m_pixelLabel = new QLabel();
        m_pixelLabel->setFont(g_monoFont);
        infoLayout->addWidget(m_pixelLabel);

        layout->addLayout(infoLayout);
        layout->addStretch();

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout5::applyTheme);
        applyTheme();
        updateData();
    }

    void updateData() {
        m_rasterWidget->setPosition(g_data.rayLine, g_data.rayPixel);
        m_frameLabel->setText(QString("Frame: %1").arg(g_data.frameNumber));
        m_lineLabel->setText(QString("Line:  %1").arg(g_data.rayLine));
        m_pixelLabel->setText(QString("Pixel: %1").arg(g_data.rayPixel));
    }

    QString popupTitle() const override { return "Raster Position"; }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("Layout5 { background: %1; border-radius: 4px; }").arg(tm.contentBg()));
        m_rasterWidget->update();
    }

    RasterWidget* m_rasterWidget;
    QLabel* m_frameLabel;
    QLabel* m_lineLabel;
    QLabel* m_pixelLabel;
};

//=============================================================================
// Layout 6: LED-style segment display
//=============================================================================
class SegmentDisplay : public QWidget {
    Q_OBJECT
public:
    SegmentDisplay(int digits = 8, QWidget* parent = nullptr) : QWidget(parent), m_digits(digits) {
        setFixedSize(digits * 14 + 4, 24);
    }

    void setValue(uint64_t value) {
        m_value = value;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        auto& tm = ThemeManager::instance();

        p.fillRect(rect(), QColor(tm.isDark() ? "#0A0A0A" : "#1A1A1A"));

        QFont f = g_monoFont;
        f.setPointSize(14);
        f.setBold(true);
        p.setFont(f);

        QString text = QString("%1").arg(m_value, m_digits, 10, QChar('0'));
        p.setPen(QColor(tm.isDark() ? "#00FF00" : "#00CC00"));
        p.drawText(rect().adjusted(2, 0, -2, 0), Qt::AlignRight | Qt::AlignVCenter, text);
    }

private:
    int m_digits;
    uint64_t m_value = 0;
};

class Layout6 : public HoverableWidget {
    Q_OBJECT
public:
    Layout6(QWidget* parent = nullptr) : HoverableWidget(parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(16);

        auto addDisplay = [&](const QString& label, int digits, SegmentDisplay*& display) {
            auto* col = new QVBoxLayout();
            col->setSpacing(2);
            auto* lbl = new QLabel(label);
            lbl->setFont(g_monoFont);
            lbl->setAlignment(Qt::AlignCenter);
            col->addWidget(lbl);
            display = new SegmentDisplay(digits);
            col->addWidget(display);
            layout->addLayout(col);
        };

        addDisplay("TOTAL", 12, m_totalDisplay);
        addDisplay("FRAME", 5, m_frameDisplay);
        addDisplay("LINE", 3, m_lineDisplay);
        addDisplay("PIXEL", 3, m_pixelDisplay);
        layout->addStretch();

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout6::applyTheme);
        applyTheme();
        updateData();
    }

    void updateData() {
        m_totalDisplay->setValue(g_data.totalTStates);
        m_frameDisplay->setValue(g_data.frameTStates);
        m_lineDisplay->setValue(g_data.rayLine);
        m_pixelDisplay->setValue(g_data.rayPixel);
    }

    QString popupTitle() const override { return "Segment Display"; }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("Layout6 { background: %1; border-radius: 4px; }").arg(tm.contentBg()));
    }

    SegmentDisplay* m_totalDisplay;
    SegmentDisplay* m_frameDisplay;
    SegmentDisplay* m_lineDisplay;
    SegmentDisplay* m_pixelDisplay;
};

//=============================================================================
// Layout 7: Accumulate mode toggle (like Xpeccy checkbox)
//=============================================================================
class Layout7 : public HoverableWidget {
    Q_OBJECT
public:
    Layout7(QWidget* parent = nullptr) : HoverableWidget(parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(12);

        m_accumulateCheck = new QCheckBox("Accumulate T");
        m_accumulateCheck->setFont(g_monoFont);
        layout->addWidget(m_accumulateCheck);

        m_totalLabel = new QLabel();
        m_totalLabel->setFont(g_monoFont);
        layout->addWidget(m_totalLabel);

        layout->addStretch();

        m_frameLabel = new QLabel();
        m_frameLabel->setFont(g_monoFont);
        layout->addWidget(m_frameLabel);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout7::applyTheme);
        applyTheme();
        updateData();
    }

    void updateData() {
        auto& tm = ThemeManager::instance();
        m_totalLabel->setText(QString("<span style='color:%1; font-weight:bold;'>%2</span> / %3")
            .arg(tm.accentColor()).arg(g_data.totalTStates).arg(g_data.frameTStates));
        m_frameLabel->setText(QString("Frame %1").arg(g_data.frameNumber));
    }

    QString popupTitle() const override { return "Accumulator Mode"; }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("Layout7 { background: %1; border-radius: 4px; }").arg(tm.contentBg()));
        updateData();
    }

    QCheckBox* m_accumulateCheck;
    QLabel* m_totalLabel;
    QLabel* m_frameLabel;
};

//=============================================================================
// Layout 8: Vertical stack with time conversion
//=============================================================================
class Layout8 : public HoverableWidget {
    Q_OBJECT
public:
    Layout8(QWidget* parent = nullptr) : HoverableWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(2);

        auto addRow = [&](const QString& label) -> QLabel* {
            auto* row = new QHBoxLayout();
            auto* lbl = new QLabel(label);
            lbl->setFont(g_monoFont);
            lbl->setFixedWidth(80);
            row->addWidget(lbl);
            auto* val = new QLabel();
            val->setFont(g_monoFont);
            row->addWidget(val);
            row->addStretch();
            layout->addLayout(row);
            return val;
        };

        m_totalLabel = addRow("Total T:");
        m_frameLabel = addRow("Frame T:");
        m_timeLabel = addRow("Time:");
        m_rayLabel = addRow("Ray:");

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout8::applyTheme);
        applyTheme();
        updateData();
    }

    void updateData() {
        auto& tm = ThemeManager::instance();
        m_totalLabel->setText(QString("<span style='color:%1'>%2</span>")
            .arg(tm.accentColor()).arg(g_data.totalTStates));
        m_frameLabel->setText(QString("%1 (%2%)")
            .arg(g_data.frameTStates)
            .arg(g_data.framePercent(), 0, 'f', 1));
        m_timeLabel->setText(QString("%1 us").arg(g_data.frameTimeUs(), 0, 'f', 2));
        m_rayLabel->setText(QString("L:%1 P:%2").arg(g_data.rayLine).arg(g_data.rayPixel));
    }

    QString popupTitle() const override { return "Time Conversion"; }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("Layout8 { background: %1; border-radius: 4px; }").arg(tm.contentBg()));
        updateData();
    }

    QLabel* m_totalLabel;
    QLabel* m_frameLabel;
    QLabel* m_timeLabel;
    QLabel* m_rayLabel;
};

//=============================================================================
// Layout 9: CPU frequency selector with timing
//=============================================================================
class Layout9 : public HoverableWidget {
    Q_OBJECT
public:
    Layout9(QWidget* parent = nullptr) : HoverableWidget(parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(12);

        auto* freqLayout = new QVBoxLayout();
        freqLayout->setSpacing(2);
        auto* freqLabel = new QLabel("CPU Freq:");
        freqLabel->setFont(g_monoFont);
        freqLayout->addWidget(freqLabel);

        m_freqCombo = new QComboBox();
        m_freqCombo->addItem("3.5 MHz (Spectrum)");
        m_freqCombo->addItem("3.54 MHz (Pentagon)");
        m_freqCombo->addItem("7.0 MHz (Turbo)");
        m_freqCombo->addItem("14.0 MHz (Turbo+)");
        m_freqCombo->setFont(g_monoFont);
        freqLayout->addWidget(m_freqCombo);
        layout->addLayout(freqLayout);

        auto* infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(2);
        m_tLabel = new QLabel();
        m_tLabel->setFont(g_monoFont);
        infoLayout->addWidget(m_tLabel);
        m_timeLabel = new QLabel();
        m_timeLabel->setFont(g_monoFont);
        infoLayout->addWidget(m_timeLabel);
        layout->addLayout(infoLayout);

        layout->addStretch();

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout9::applyTheme);
        applyTheme();
        updateData();
    }

    void updateData() {
        auto& tm = ThemeManager::instance();
        m_tLabel->setText(QString("T: <span style='color:%1'>%2</span>")
            .arg(tm.accentColor()).arg(g_data.totalTStates));
        m_timeLabel->setText(QString("Time: %1 ms").arg(g_data.totalTimeMs(), 0, 'f', 2));
    }

    QString popupTitle() const override { return "CPU Frequency"; }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("Layout9 { background: %1; border-radius: 4px; }").arg(tm.contentBg()));
        updateData();
    }

    QComboBox* m_freqCombo;
    QLabel* m_tLabel;
    QLabel* m_timeLabel;
};

//=============================================================================
// Main window
//=============================================================================
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow() {
        setWindowTitle("T-States POC - Layout Comparison");

        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->setSpacing(8);

        // Theme radio buttons
        auto* topRow = new QHBoxLayout();
        topRow->addWidget(new QLabel("Theme:"));
        auto* lightRadio = new QRadioButton("Light");
        auto* darkRadio = new QRadioButton("Dark");
        lightRadio->setChecked(true);
        auto* themeGroup = new QButtonGroup(this);
        themeGroup->addButton(lightRadio, 0);
        themeGroup->addButton(darkRadio, 1);
        connect(themeGroup, &QButtonGroup::idClicked, this, [this](int id) {
            ThemeManager::instance().setDark(id == 1);
            applyTheme();
        });
        topRow->addWidget(lightRadio);
        topRow->addWidget(darkRadio);

        auto* simBtn = new QPushButton("Simulate Step");
        connect(simBtn, &QPushButton::clicked, this, &MainWindow::simulateStep);
        topRow->addWidget(simBtn);

        topRow->addStretch();
        mainLayout->addLayout(topRow);

        // Layouts
        auto addLayout = [&](const QString& title, QWidget* widget) {
            auto* group = new QGroupBox(title);
            auto* l = new QVBoxLayout(group);
            l->setContentsMargins(4, 4, 4, 4);
            l->addWidget(widget);
            mainLayout->addWidget(group);
            return widget;
        };

        m_layout1 = static_cast<Layout1*>(addLayout("Layout 1: Horizontal Counters", new Layout1()));
        m_layout2 = static_cast<Layout2*>(addLayout("Layout 2: Compact Single Line", new Layout2()));
        m_layout3 = static_cast<Layout3*>(addLayout("Layout 3: Grid with Labels", new Layout3()));
        m_layout4 = static_cast<Layout4*>(addLayout("Layout 4: Progress Bar", new Layout4()));
        m_layout5 = static_cast<Layout5*>(addLayout("Layout 5: Visual Raster", new Layout5()));
        m_layout6 = static_cast<Layout6*>(addLayout("Layout 6: LED Segment Display", new Layout6()));
        m_layout7 = static_cast<Layout7*>(addLayout("Layout 7: Accumulate Mode", new Layout7()));
        m_layout8 = static_cast<Layout8*>(addLayout("Layout 8: Vertical + Time", new Layout8()));
        m_layout9 = static_cast<Layout9*>(addLayout("Layout 9: CPU Frequency Selector", new Layout9()));

        mainLayout->addStretch();

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &MainWindow::applyTheme);
        applyTheme();
    }

private slots:
    void simulateStep() {
        g_data.totalTStates += 4;
        g_data.frameTStates += 4;
        g_data.rayPixel += 4;
        if (g_data.rayPixel >= 448) {
            g_data.rayPixel = 0;
            g_data.rayLine++;
            if (g_data.rayLine >= 312) {
                g_data.rayLine = 0;
                g_data.frameNumber++;
                g_data.frameTStates = 0;
            }
        }
        updateAll();
    }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("QWidget { background: %1; color: %2; }")
            .arg(tm.bg()).arg(tm.fg()));
    }

    void updateAll() {
        m_layout1->updateData();
        m_layout2->updateData();
        m_layout3->updateData();
        m_layout4->updateData();
        m_layout5->updateData();
        m_layout6->updateData();
        m_layout7->updateData();
        m_layout8->updateData();
        m_layout9->updateData();
    }

    Layout1* m_layout1;
    Layout2* m_layout2;
    Layout3* m_layout3;
    Layout4* m_layout4;
    Layout5* m_layout5;
    Layout6* m_layout6;
    Layout7* m_layout7;
    Layout8* m_layout8;
    Layout9* m_layout9;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    initMonoFont();

    MainWindow w;
    w.resize(500, 800);
    w.show();

    return app.exec();
}

#include "main.moc"
