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
#include <QScrollArea>
#include <QMouseEvent>
#include <QTimer>
#include <QFontDatabase>
#include <QScreen>
#include <QPainter>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
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

    // Colors
    QString bg() const { return m_isDark ? "#1E1E1E" : "#FFFFFF"; }
    QString fg() const { return m_isDark ? "#D4D4D4" : "#1E1E1E"; }
    QString popupBg() const { return m_isDark ? "#252526" : "#FAFAFA"; }
    QString popupBorder() const { return m_isDark ? "#3C3C3C" : "#E0E0E0"; }
    QString tagBg() const { return m_isDark ? "#3C3C3C" : "#E8E8E8"; }
    QString contentBg() const { return m_isDark ? "#1E1E1E" : "#F5F5F5"; }
    QString hintColor() const { return m_isDark ? "#666666" : "#999999"; }
    QString selectedBg() const { return m_isDark ? "#264F78" : "#ADD6FF"; }
    QString hoverBg() const { return m_isDark ? "#2A2D2E" : "#E8E8E8"; }

    // CMOS-specific colors
    QString rtcColor() const { return m_isDark ? "#CE9178" : "#A31515"; }
    QString statusColor() const { return m_isDark ? "#DCDCAA" : "#795E26"; }
    QString nvramColor() const { return m_isDark ? "#4EC9B0" : "#267F99"; }
    QString reservedColor() const { return m_isDark ? "#808080" : "#6E6E6E"; }

signals:
    void themeChanged(bool isDark);

private:
    ThemeManager() : m_isDark(false) {}
    bool m_isDark = false;
};

//=============================================================================
// Data change notifier
//=============================================================================
class DataNotifier : public QObject {
    Q_OBJECT
public:
    static DataNotifier& instance() {
        static DataNotifier dn;
        return dn;
    }
    void notifyDataChanged() { emit dataChanged(); }
signals:
    void dataChanged();
private:
    DataNotifier() {}
};

//=============================================================================
// RTC Register definitions
//=============================================================================
struct RTCRegister {
    int addr;
    QString name;
    QString description;
    enum Type { RTC, Status, NVRAM, Reserved } type;
};

static const RTCRegister g_rtcRegisters[] = {
    {0x00, "Seconds",    "Current seconds (0-59)", RTCRegister::RTC},
    {0x01, "SecAlarm",   "Seconds alarm", RTCRegister::RTC},
    {0x02, "Minutes",    "Current minutes (0-59)", RTCRegister::RTC},
    {0x03, "MinAlarm",   "Minutes alarm", RTCRegister::RTC},
    {0x04, "Hours",      "Current hours (0-23/1-12)", RTCRegister::RTC},
    {0x05, "HourAlarm",  "Hours alarm", RTCRegister::RTC},
    {0x06, "DayOfWeek",  "Day of week (1-7)", RTCRegister::RTC},
    {0x07, "DayOfMonth", "Day of month (1-31)", RTCRegister::RTC},
    {0x08, "Month",      "Month (1-12)", RTCRegister::RTC},
    {0x09, "Year",       "Year (0-99)", RTCRegister::RTC},
    {0x0A, "Status A",   "RTC update in progress", RTCRegister::Status},
    {0x0B, "Status B",   "RTC enable/format flags", RTCRegister::Status},
    {0x0C, "Status C",   "Interrupt flags (read clears)", RTCRegister::Status},
    {0x0D, "Status D",   "Valid RAM / battery status", RTCRegister::Status},
    {0x32, "Century",    "Century (19/20)", RTCRegister::RTC},
};
static const int g_numRtcRegisters = sizeof(g_rtcRegisters) / sizeof(g_rtcRegisters[0]);

const RTCRegister* findRTCRegister(int addr) {
    for (int i = 0; i < g_numRtcRegisters; i++) {
        if (g_rtcRegisters[i].addr == addr) return &g_rtcRegisters[i];
    }
    return nullptr;
}

RTCRegister::Type getAddrType(int addr) {
    auto* reg = findRTCRegister(addr);
    if (reg) return reg->type;
    if (addr >= 0x0E && addr <= 0x3F) return RTCRegister::Reserved;
    return RTCRegister::NVRAM;
}

// Simulated CMOS data
static uint8_t g_cmosData[128];

void initCMOSData() {
    // Initialize with sample data
    g_cmosData[0x00] = 0x45; // Seconds
    g_cmosData[0x02] = 0x30; // Minutes
    g_cmosData[0x04] = 0x14; // Hours (14:30:45)
    g_cmosData[0x06] = 0x05; // Friday
    g_cmosData[0x07] = 0x22; // 22nd
    g_cmosData[0x08] = 0x08; // August
    g_cmosData[0x09] = 0x26; // 2026
    g_cmosData[0x0A] = 0x26; // Status A
    g_cmosData[0x0B] = 0x02; // Status B - 24h mode
    g_cmosData[0x0C] = 0x00; // Status C
    g_cmosData[0x0D] = 0x80; // Status D - battery OK
    g_cmosData[0x32] = 0x20; // Century (20xx)

    // Fill rest with pattern
    for (int i = 0x40; i < 128; i++) {
        g_cmosData[i] = i ^ 0x55;
    }
}

QString formatBCD(uint8_t val) {
    return QString("%1%2").arg((val >> 4) & 0x0F).arg(val & 0x0F);
}

QString decodeRTCValue(int addr, uint8_t val) {
    switch (addr) {
        case 0x00: case 0x01: return QString("%1 sec").arg(formatBCD(val));
        case 0x02: case 0x03: return QString("%1 min").arg(formatBCD(val));
        case 0x04: case 0x05: return QString("%1 hr").arg(formatBCD(val));
        case 0x06: {
            const char* days[] = {"", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
            return val <= 7 ? days[val] : "?";
        }
        case 0x07: return QString("Day %1").arg(formatBCD(val));
        case 0x08: return QString("Month %1").arg(formatBCD(val));
        case 0x09: return QString("Year %1").arg(formatBCD(val));
        case 0x32: return QString("Century %1").arg(formatBCD(val));
        case 0x0A: return (val & 0x80) ? "Update in progress" : "Ready";
        case 0x0B: return (val & 0x02) ? "24h mode" : "12h mode";
        case 0x0D: return (val & 0x80) ? "Battery OK" : "Battery low";
        default: return "";
    }
}

//=============================================================================
// Modern popup for CMOS byte details
//=============================================================================
class CMOSPopup : public QFrame {
    Q_OBJECT
public:
    static CMOSPopup& instance() {
        static CMOSPopup popup;
        return popup;
    }

    void showAt(int addr, uint8_t value, const QPoint& pos) {
        auto& tm = ThemeManager::instance();
        auto* reg = findRTCRegister(addr);
        auto type = getAddrType(addr);

        // Title
        QString title = reg ? reg->name : QString("NVRAM $%1").arg(addr, 2, 16, QChar('0')).toUpper();
        m_titleLabel->setText(title);

        QString typeColor, typeName;
        switch (type) {
            case RTCRegister::RTC:
                typeName = "RTC";
                typeColor = tm.rtcColor();
                break;
            case RTCRegister::Status:
                typeName = "STATUS";
                typeColor = tm.statusColor();
                break;
            case RTCRegister::Reserved:
                typeName = "RESERVED";
                typeColor = tm.reservedColor();
                break;
            default:
                typeName = "NVRAM";
                typeColor = tm.nvramColor();
                break;
        }
        m_titleLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(typeColor));
        m_typeTag->setText(typeName);
        QString tagBg = tm.isDark() ? "#3C3C3C" : "#E8E8E8";
        m_typeTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tagBg).arg(typeColor));

        // Address
        m_addrLabel->setText(QString("Address: $%1").arg(addr, 2, 16, QChar('0')).toUpper());

        // Value details
        QString details;
        details += QString("Hex:    $%1\n").arg(value, 2, 16, QChar('0')).toUpper();
        details += QString("Dec:    %1\n").arg(value);
        details += QString("Binary: %1").arg(value, 8, 2, QChar('0'));
        m_details->setText(details);

        // Decoded value for RTC registers
        QString decoded = decodeRTCValue(addr, value);
        if (!decoded.isEmpty()) {
            m_decodedBox->show();
            m_decoded->setText(decoded);
        } else {
            m_decodedBox->hide();
        }

        // Description
        if (reg) {
            m_descLabel->show();
            m_descLabel->setText(reg->description);
        } else {
            m_descLabel->hide();
        }

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
    CMOSPopup() : QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint) {
        setFrameStyle(QFrame::NoFrame);
        setAttribute(Qt::WA_ShowWithoutActivating);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(6);

        // Header row
        auto* headerRow = new QHBoxLayout();
        m_titleLabel = new QLabel();
        m_titleLabel->setFont(g_monoFont);
        headerRow->addWidget(m_titleLabel);
        m_typeTag = new QLabel();
        headerRow->addWidget(m_typeTag);
        headerRow->addStretch();
        layout->addLayout(headerRow);

        // Address
        m_addrLabel = new QLabel();
        m_addrLabel->setFont(g_monoFont);
        layout->addWidget(m_addrLabel);

        // Value details in box
        m_detailsBox = new QFrame();
        m_detailsBox->setFrameStyle(QFrame::NoFrame);
        auto* detailsLayout = new QVBoxLayout(m_detailsBox);
        detailsLayout->setContentsMargins(8, 6, 8, 6);
        m_details = new QLabel();
        m_details->setFont(g_monoFont);
        detailsLayout->addWidget(m_details);
        layout->addWidget(m_detailsBox);

        // Decoded value box
        m_decodedBox = new QFrame();
        m_decodedBox->setFrameStyle(QFrame::NoFrame);
        auto* decodedLayout = new QVBoxLayout(m_decodedBox);
        decodedLayout->setContentsMargins(8, 6, 8, 6);
        m_decoded = new QLabel();
        m_decoded->setFont(g_monoFont);
        m_decoded->setStyleSheet("font-weight: bold;");
        decodedLayout->addWidget(m_decoded);
        layout->addWidget(m_decodedBox);

        // Description
        m_descLabel = new QLabel();
        m_descLabel->setWordWrap(true);
        layout->addWidget(m_descLabel);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &CMOSPopup::applyTheme);
        applyTheme();
    }

    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString(
            "CMOSPopup { background: %1; border: 1px solid %2; border-radius: 8px; }"
        ).arg(tm.popupBg()).arg(tm.popupBorder()));
        m_detailsBox->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(tm.contentBg()));
        m_decodedBox->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(tm.contentBg()));
        m_descLabel->setStyleSheet(QString("color: %1;").arg(tm.hintColor()));
    }

    QLabel* m_titleLabel;
    QLabel* m_typeTag;
    QLabel* m_addrLabel;
    QFrame* m_detailsBox;
    QLabel* m_details;
    QFrame* m_decodedBox;
    QLabel* m_decoded;
    QLabel* m_descLabel;
};

//=============================================================================
// Clickable CMOS byte cell with hover
//=============================================================================
class CMOSCell : public QLabel {
    Q_OBJECT
public:
    CMOSCell(int addr, QWidget* parent = nullptr)
        : QLabel(parent), m_addr(addr), m_hovered(false) {
        setFont(g_monoFont);
        setAlignment(Qt::AlignCenter);
        setFixedSize(28, 22);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);

        m_hoverTimer.setSingleShot(true);
        connect(&m_hoverTimer, &QTimer::timeout, this, &CMOSCell::showPopup);

        updateDisplay();
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](bool) {
            updateDisplay();
        });
        connect(&DataNotifier::instance(), &DataNotifier::dataChanged, this, &CMOSCell::updateDisplay);
    }

    void updateDisplay() {
        uint8_t val = g_cmosData[m_addr];
        setText(QString("%1").arg(val, 2, 16, QChar('0')).toUpper());
        applyStyle();
    }

signals:
    void clicked(int addr);

protected:
    void enterEvent(QEnterEvent*) override {
        m_hovered = true;
        applyStyle();
        CMOSPopup::instance().hide();
        m_lastPos = QCursor::pos();
        m_hoverTimer.start(400);
    }

    void leaveEvent(QEvent*) override {
        m_hovered = false;
        applyStyle();
        m_hoverTimer.stop();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        m_lastPos = e->globalPosition().toPoint();
        if (m_hoverTimer.isActive()) {
            m_hoverTimer.start(400);
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            emit clicked(m_addr);
        }
    }

private slots:
    void showPopup() {
        CMOSPopup::instance().showAt(m_addr, g_cmosData[m_addr], m_lastPos + QPoint(15, 15));
    }

private:
    void applyStyle() {
        auto& tm = ThemeManager::instance();
        auto type = getAddrType(m_addr);

        QString bgColor;
        QString fgColor;

        switch (type) {
            case RTCRegister::RTC:
                fgColor = tm.rtcColor();
                break;
            case RTCRegister::Status:
                fgColor = tm.statusColor();
                break;
            case RTCRegister::Reserved:
                fgColor = tm.reservedColor();
                break;
            default:
                fgColor = tm.nvramColor();
                break;
        }

        if (m_hovered) {
            bgColor = tm.hoverBg();
        } else {
            bgColor = "transparent";
        }

        setStyleSheet(QString("background: %1; color: %2; border-radius: 3px;")
            .arg(bgColor).arg(fgColor));
    }

    int m_addr;
    bool m_hovered;
    QTimer m_hoverTimer;
    QPoint m_lastPos;
};

//=============================================================================
// Layout 1: Classic hex editor grid (16 bytes per row)
//=============================================================================
class CMOSLayout1 : public QGroupBox {
    Q_OBJECT
public:
    CMOSLayout1(QWidget* parent = nullptr) : QGroupBox("Layout 1: Classic Hex Grid (16 per row)", parent) {
        auto* layout = new QGridLayout(this);
        layout->setSpacing(2);

        // Header
        auto* addrHeader = new QLabel("Addr");
        addrHeader->setFont(g_monoFont);
        layout->addWidget(addrHeader, 0, 0);

        for (int i = 0; i < 16; i++) {
            auto* colHeader = new QLabel(QString("%1").arg(i, 1, 16).toUpper());
            colHeader->setFont(g_monoFont);
            colHeader->setAlignment(Qt::AlignCenter);
            colHeader->setFixedWidth(28);
            layout->addWidget(colHeader, 0, i + 1);
        }

        // Rows
        for (int row = 0; row < 8; row++) {
            auto* rowLabel = new QLabel(QString("$%1").arg(row * 16, 2, 16, QChar('0')).toUpper());
            rowLabel->setFont(g_monoFont);
            layout->addWidget(rowLabel, row + 1, 0);

            for (int col = 0; col < 16; col++) {
                int addr = row * 16 + col;
                auto* cell = new CMOSCell(addr);
                connect(cell, &CMOSCell::clicked, this, &CMOSLayout1::onCellClicked);
                m_cells[addr] = cell;
                layout->addWidget(cell, row + 1, col + 1);
            }
        }
    }

signals:
    void cellClicked(int addr);

private slots:
    void onCellClicked(int addr) { emit cellClicked(addr); }

private:
    CMOSCell* m_cells[128] = {};
};

//=============================================================================
// Layout 2: Compact 8 bytes per row with ASCII
//=============================================================================
class CMOSLayout2 : public QGroupBox {
    Q_OBJECT
public:
    CMOSLayout2(QWidget* parent = nullptr) : QGroupBox("Layout 2: 8 Bytes + ASCII", parent) {
        auto* layout = new QGridLayout(this);
        layout->setSpacing(2);

        for (int row = 0; row < 16; row++) {
            auto* rowLabel = new QLabel(QString("$%1").arg(row * 8, 2, 16, QChar('0')).toUpper());
            rowLabel->setFont(g_monoFont);
            layout->addWidget(rowLabel, row, 0);

            for (int col = 0; col < 8; col++) {
                int addr = row * 8 + col;
                auto* cell = new CMOSCell(addr);
                m_cells[addr] = cell;
                layout->addWidget(cell, row, col + 1);
            }

            // ASCII
            auto* ascii = new QLabel();
            ascii->setFont(g_monoFont);
            QString asciiStr;
            for (int col = 0; col < 8; col++) {
                int addr = row * 8 + col;
                uint8_t val = g_cmosData[addr];
                asciiStr += (val >= 32 && val < 127) ? QChar(val) : '.';
            }
            ascii->setText(asciiStr);
            m_asciiLabels[row] = ascii;
            layout->addWidget(ascii, row, 9);
        }
    }

private:
    CMOSCell* m_cells[128] = {};
    QLabel* m_asciiLabels[16] = {};
};

//=============================================================================
// Layout 3: RTC-focused with time display
//=============================================================================
class CMOSLayout3 : public QGroupBox {
    Q_OBJECT
public:
    CMOSLayout3(QWidget* parent = nullptr) : QGroupBox("Layout 3: RTC Focus", parent) {
        auto* layout = new QVBoxLayout(this);

        // Current time display
        m_timeDisplay = new QLabel();
        m_timeDisplay->setFont(g_monoFont);
        m_timeDisplay->setStyleSheet("font-size: 18px; font-weight: bold; padding: 10px;");
        layout->addWidget(m_timeDisplay, 0, Qt::AlignCenter);

        // RTC registers grid
        auto* grid = new QGridLayout();
        grid->setSpacing(8);

        const char* labels[] = {"Sec", "Min", "Hour", "DOW", "Day", "Mon", "Year"};
        const int addrs[] = {0x00, 0x02, 0x04, 0x06, 0x07, 0x08, 0x09};

        for (int i = 0; i < 7; i++) {
            auto* label = new QLabel(labels[i]);
            label->setFont(g_monoFont);
            label->setAlignment(Qt::AlignCenter);
            grid->addWidget(label, 0, i);

            auto* cell = new CMOSCell(addrs[i]);
            m_cells[i] = cell;
            grid->addWidget(cell, 1, i, Qt::AlignCenter);
        }
        layout->addLayout(grid);

        // Status registers
        auto* statusLabel = new QLabel("Status Registers:");
        statusLabel->setFont(g_monoFont);
        layout->addWidget(statusLabel);

        auto* statusGrid = new QHBoxLayout();
        for (int i = 0; i < 4; i++) {
            auto* box = new QVBoxLayout();
            auto* l = new QLabel(QString("Reg %1").arg(QChar('A' + i)));
            l->setFont(g_monoFont);
            l->setAlignment(Qt::AlignCenter);
            box->addWidget(l);
            auto* cell = new CMOSCell(0x0A + i);
            box->addWidget(cell, 0, Qt::AlignCenter);
            statusGrid->addLayout(box);
        }
        layout->addLayout(statusGrid);

        updateTimeDisplay();
    }

    void updateTimeDisplay() {
        QString time = QString("%1:%2:%3")
            .arg(formatBCD(g_cmosData[0x04]))
            .arg(formatBCD(g_cmosData[0x02]))
            .arg(formatBCD(g_cmosData[0x00]));
        QString date = QString("%1-%2-20%3")
            .arg(formatBCD(g_cmosData[0x07]))
            .arg(formatBCD(g_cmosData[0x08]))
            .arg(formatBCD(g_cmosData[0x09]));
        m_timeDisplay->setText(QString("%1  %2").arg(time).arg(date));
    }

private:
    QLabel* m_timeDisplay;
    CMOSCell* m_cells[7] = {};
};

//=============================================================================
// Layout 4: Vertical list with descriptions
//=============================================================================
class CMOSLayout4 : public QGroupBox {
    Q_OBJECT
public:
    CMOSLayout4(QWidget* parent = nullptr) : QGroupBox("Layout 4: Descriptive List", parent) {
        auto* scrollArea = new QScrollArea();
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameStyle(QFrame::NoFrame);
        scrollArea->setMaximumHeight(250);

        auto* container = new QWidget();
        auto* layout = new QVBoxLayout(container);
        layout->setSpacing(4);

        for (int i = 0; i < g_numRtcRegisters; i++) {
            auto& reg = g_rtcRegisters[i];
            auto* row = new QHBoxLayout();

            auto* addrLabel = new QLabel(QString("$%1").arg(reg.addr, 2, 16, QChar('0')).toUpper());
            addrLabel->setFont(g_monoFont);
            addrLabel->setFixedWidth(40);
            row->addWidget(addrLabel);

            auto* cell = new CMOSCell(reg.addr);
            row->addWidget(cell);

            auto* nameLabel = new QLabel(reg.name);
            nameLabel->setFont(g_monoFont);
            nameLabel->setFixedWidth(80);
            row->addWidget(nameLabel);

            auto* descLabel = new QLabel(reg.description);
            descLabel->setStyleSheet("color: gray;");
            row->addWidget(descLabel);

            row->addStretch();
            layout->addLayout(row);
        }

        scrollArea->setWidget(container);

        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->addWidget(scrollArea);
    }
};

//=============================================================================
// Layout 5: Grouped by type
//=============================================================================
class CMOSLayout5 : public QGroupBox {
    Q_OBJECT
public:
    CMOSLayout5(QWidget* parent = nullptr) : QGroupBox("Layout 5: Grouped by Type", parent) {
        auto* layout = new QHBoxLayout(this);

        // RTC group
        auto* rtcBox = new QGroupBox("RTC Registers");
        auto* rtcLayout = new QGridLayout(rtcBox);
        int row = 0, col = 0;
        for (int i = 0; i < g_numRtcRegisters; i++) {
            if (g_rtcRegisters[i].type == RTCRegister::RTC) {
                auto* cell = new CMOSCell(g_rtcRegisters[i].addr);
                rtcLayout->addWidget(cell, row, col++);
                if (col >= 5) { col = 0; row++; }
            }
        }
        layout->addWidget(rtcBox);

        // Status group
        auto* statusBox = new QGroupBox("Status Registers");
        auto* statusLayout = new QHBoxLayout(statusBox);
        for (int i = 0; i < g_numRtcRegisters; i++) {
            if (g_rtcRegisters[i].type == RTCRegister::Status) {
                auto* cell = new CMOSCell(g_rtcRegisters[i].addr);
                statusLayout->addWidget(cell);
            }
        }
        layout->addWidget(statusBox);

        // NVRAM sample
        auto* nvramBox = new QGroupBox("NVRAM (first 16)");
        auto* nvramLayout = new QGridLayout(nvramBox);
        for (int i = 0; i < 16; i++) {
            auto* cell = new CMOSCell(0x40 + i);
            nvramLayout->addWidget(cell, i / 8, i % 8);
        }
        layout->addWidget(nvramBox);
    }
};

//=============================================================================
// Layout 6: Table view with sorting
//=============================================================================
class CMOSLayout6 : public QGroupBox {
    Q_OBJECT
public:
    CMOSLayout6(QWidget* parent = nullptr) : QGroupBox("Layout 6: Table View", parent) {
        auto* layout = new QVBoxLayout(this);

        m_table = new QTableWidget(g_numRtcRegisters, 5);
        m_table->setFont(g_monoFont);
        m_table->setHorizontalHeaderLabels({"Addr", "Name", "Hex", "Dec", "Decoded"});
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setMaximumHeight(200);

        for (int i = 0; i < g_numRtcRegisters; i++) {
            auto& reg = g_rtcRegisters[i];
            uint8_t val = g_cmosData[reg.addr];

            m_table->setItem(i, 0, new QTableWidgetItem(QString("$%1").arg(reg.addr, 2, 16, QChar('0')).toUpper()));
            m_table->setItem(i, 1, new QTableWidgetItem(reg.name));
            m_table->setItem(i, 2, new QTableWidgetItem(QString("$%1").arg(val, 2, 16, QChar('0')).toUpper()));
            m_table->setItem(i, 3, new QTableWidgetItem(QString::number(val)));
            m_table->setItem(i, 4, new QTableWidgetItem(decodeRTCValue(reg.addr, val)));
        }

        m_table->resizeColumnsToContents();
        layout->addWidget(m_table);
    }

private:
    QTableWidget* m_table;
};

//=============================================================================
// Layout 7: Binary editor mode
//=============================================================================
class CMOSLayout7 : public QGroupBox {
    Q_OBJECT
public:
    CMOSLayout7(QWidget* parent = nullptr) : QGroupBox("Layout 7: Binary Editor", parent) {
        auto* layout = new QVBoxLayout(this);

        // Address selector
        auto* addrRow = new QHBoxLayout();
        addrRow->addWidget(new QLabel("Address:"));
        m_addrSpin = new QSpinBox();
        m_addrSpin->setRange(0, 127);
        m_addrSpin->setDisplayIntegerBase(16);
        m_addrSpin->setPrefix("$");
        m_addrSpin->setFont(g_monoFont);
        connect(m_addrSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &CMOSLayout7::onAddrChanged);
        addrRow->addWidget(m_addrSpin);
        addrRow->addStretch();
        layout->addLayout(addrRow);

        // Value display
        m_valueLabel = new QLabel();
        m_valueLabel->setFont(g_monoFont);
        m_valueLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
        layout->addWidget(m_valueLabel);

        // Bit toggles
        auto* bitRow = new QHBoxLayout();
        bitRow->addWidget(new QLabel("Bits: "));
        for (int i = 7; i >= 0; i--) {
            auto* cb = new QCheckBox(QString::number(i));
            m_bitBoxes[i] = cb;
            connect(cb, &QCheckBox::toggled, this, [this, i](bool checked) {
                int addr = m_addrSpin->value();
                if (checked)
                    g_cmosData[addr] |= (1 << i);
                else
                    g_cmosData[addr] &= ~(1 << i);
                updateValueLabel();
            });
            bitRow->addWidget(cb);
        }
        layout->addLayout(bitRow);

        layout->addStretch();
        onAddrChanged(0);
    }

private slots:
    void onAddrChanged(int addr) {
        uint8_t val = g_cmosData[addr];
        for (int i = 0; i < 8; i++) {
            m_bitBoxes[i]->blockSignals(true);
            m_bitBoxes[i]->setChecked(val & (1 << i));
            m_bitBoxes[i]->blockSignals(false);
        }
        updateValueLabel();
    }

    void updateValueLabel() {
        int addr = m_addrSpin->value();
        uint8_t val = g_cmosData[addr];
        auto* reg = findRTCRegister(addr);
        QString name = reg ? reg->name : "NVRAM";
        m_valueLabel->setText(QString("%1: $%2 = %3 = %4")
            .arg(name)
            .arg(val, 2, 16, QChar('0')).toUpper()
            .arg(val)
            .arg(val, 8, 2, QChar('0')));
    }

private:
    QSpinBox* m_addrSpin;
    QLabel* m_valueLabel;
    QCheckBox* m_bitBoxes[8] = {};
};

//=============================================================================
// Layout 8: Visual memory map
//=============================================================================
class CMOSLayout8 : public QGroupBox {
    Q_OBJECT
public:
    CMOSLayout8(QWidget* parent = nullptr) : QGroupBox("Layout 8: Visual Map", parent) {
        auto* layout = new QVBoxLayout(this);

        // Legend
        auto* legend = new QHBoxLayout();
        auto& tm = ThemeManager::instance();

        auto addLegend = [&](const QString& name, const QString& color) {
            auto* box = new QLabel("  ");
            box->setStyleSheet(QString("background: %1; border-radius: 2px;").arg(color));
            box->setFixedSize(16, 16);
            legend->addWidget(box);
            legend->addWidget(new QLabel(name));
            legend->addSpacing(10);
        };

        addLegend("RTC", tm.rtcColor());
        addLegend("Status", tm.statusColor());
        addLegend("Reserved", tm.reservedColor());
        addLegend("NVRAM", tm.nvramColor());
        legend->addStretch();
        layout->addLayout(legend);

        // Memory map grid
        auto* grid = new QGridLayout();
        grid->setSpacing(1);

        for (int addr = 0; addr < 128; addr++) {
            auto* cell = new QLabel();
            cell->setFixedSize(8, 8);

            auto type = getAddrType(addr);
            QString color;
            switch (type) {
                case RTCRegister::RTC: color = tm.rtcColor(); break;
                case RTCRegister::Status: color = tm.statusColor(); break;
                case RTCRegister::Reserved: color = tm.reservedColor(); break;
                default: color = tm.nvramColor(); break;
            }
            cell->setStyleSheet(QString("background: %1; border-radius: 1px;").arg(color));

            grid->addWidget(cell, addr / 16, addr % 16);
        }

        layout->addLayout(grid);

        // Address ranges
        auto* ranges = new QLabel(
            "$00-$09: RTC time | $0A-$0D: Status | $0E-$3F: Reserved | $40-$7F: NVRAM"
        );
        ranges->setStyleSheet("color: gray;");
        ranges->setWordWrap(true);
        layout->addWidget(ranges);
    }
};

//=============================================================================
// Layout 9: Editable hex input
//=============================================================================
class CMOSLayout9 : public QGroupBox {
    Q_OBJECT
public:
    CMOSLayout9(QWidget* parent = nullptr) : QGroupBox("Layout 9: Quick Edit", parent) {
        auto* layout = new QVBoxLayout(this);

        // RTC quick edit
        auto* rtcGrid = new QGridLayout();
        rtcGrid->setSpacing(4);

        const char* labels[] = {"Sec:", "Min:", "Hour:", "Day:", "Mon:", "Year:"};
        const int addrs[] = {0x00, 0x02, 0x04, 0x07, 0x08, 0x09};

        for (int i = 0; i < 6; i++) {
            auto* label = new QLabel(labels[i]);
            label->setFont(g_monoFont);
            rtcGrid->addWidget(label, i / 3, (i % 3) * 2);

            auto* edit = new QLineEdit();
            edit->setFont(g_monoFont);
            edit->setMaxLength(2);
            edit->setFixedWidth(40);
            edit->setText(QString("%1").arg(g_cmosData[addrs[i]], 2, 16, QChar('0')).toUpper());
            edit->setPlaceholderText("00");
            m_edits[i] = edit;

            connect(edit, &QLineEdit::textChanged, this, [this, addrs, i](const QString& text) {
                bool ok;
                int val = text.toInt(&ok, 16);
                if (ok && val >= 0 && val <= 255) {
                    g_cmosData[addrs[i]] = val;
                }
            });

            rtcGrid->addWidget(edit, i / 3, (i % 3) * 2 + 1);
        }

        layout->addLayout(rtcGrid);

        // Apply button
        auto* btnRow = new QHBoxLayout();
        auto* randomBtn = new QPushButton("Randomize NVRAM");
        connect(randomBtn, &QPushButton::clicked, this, [this]() {
            for (int i = 0x40; i < 128; i++) {
                g_cmosData[i] = rand() & 0xFF;
            }
            DataNotifier::instance().notifyDataChanged();
        });
        btnRow->addWidget(randomBtn);

        auto* resetBtn = new QPushButton("Reset RTC");
        connect(resetBtn, &QPushButton::clicked, this, &CMOSLayout9::resetRTC);
        btnRow->addWidget(resetBtn);
        btnRow->addStretch();

        layout->addLayout(btnRow);

        // Connect to data change notifications
        connect(&DataNotifier::instance(), &DataNotifier::dataChanged, this, &CMOSLayout9::refreshEdits);
    }

private slots:
    void resetRTC() {
        g_cmosData[0x00] = 0x00;
        g_cmosData[0x02] = 0x00;
        g_cmosData[0x04] = 0x12;
        g_cmosData[0x07] = 0x01;
        g_cmosData[0x08] = 0x01;
        g_cmosData[0x09] = 0x00;
        DataNotifier::instance().notifyDataChanged();
    }

    void refreshEdits() {
        const int addrs[] = {0x00, 0x02, 0x04, 0x07, 0x08, 0x09};
        for (int i = 0; i < 6; i++) {
            m_edits[i]->blockSignals(true);
            m_edits[i]->setText(QString("%1").arg(g_cmosData[addrs[i]], 2, 16, QChar('0')).toUpper());
            m_edits[i]->blockSignals(false);
        }
    }

private:
    QLineEdit* m_edits[6] = {};
};

//=============================================================================
// Main window
//=============================================================================
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow() {
        setWindowTitle("CMOS/RTC Editor POC");

        auto* mainLayout = new QVBoxLayout(this);

        // Theme radio buttons
        auto* topBar = new QHBoxLayout();
        topBar->addWidget(new QLabel("Theme:"));
        auto* lightRadio = new QRadioButton("Light");
        auto* darkRadio = new QRadioButton("Dark");
        lightRadio->setChecked(true);
        auto* themeGroup = new QButtonGroup(this);
        themeGroup->addButton(lightRadio, 0);
        themeGroup->addButton(darkRadio, 1);
        connect(themeGroup, &QButtonGroup::idClicked, this, [](int id) {
            ThemeManager::instance().setDark(id == 1);
        });
        topBar->addWidget(lightRadio);
        topBar->addWidget(darkRadio);
        topBar->addStretch();
        mainLayout->addLayout(topBar);

        // Scroll area for layouts
        auto* scrollArea = new QScrollArea();
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameStyle(QFrame::NoFrame);

        auto* container = new QWidget();
        auto* layoutsGrid = new QGridLayout(container);
        layoutsGrid->setSpacing(8);

        // Add all layouts
        layoutsGrid->addWidget(new CMOSLayout1(), 0, 0);
        layoutsGrid->addWidget(new CMOSLayout2(), 0, 1);
        layoutsGrid->addWidget(new CMOSLayout3(), 1, 0);
        layoutsGrid->addWidget(new CMOSLayout4(), 1, 1);
        layoutsGrid->addWidget(new CMOSLayout5(), 2, 0);
        layoutsGrid->addWidget(new CMOSLayout6(), 2, 1);
        layoutsGrid->addWidget(new CMOSLayout7(), 3, 0);
        layoutsGrid->addWidget(new CMOSLayout8(), 3, 1);
        layoutsGrid->addWidget(new CMOSLayout9(), 4, 0, 1, 2);

        scrollArea->setWidget(container);
        mainLayout->addWidget(scrollArea);

        // Apply theme
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &MainWindow::applyTheme);
        applyTheme(ThemeManager::instance().isDark());
    }

private:
    void applyTheme(bool) {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("QWidget { background: %1; color: %2; }")
            .arg(tm.bg()).arg(tm.fg()));
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    initMonoFont();
    initCMOSData();

    MainWindow w;
    w.resize(900, 800);
    w.show();

    return app.exec();
}

#include "main.moc"
