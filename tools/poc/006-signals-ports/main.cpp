#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QMouseEvent>
#include <QTimer>
#include <QFontDatabase>
#include <QScreen>
#include <QPainter>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollArea>
#include <QTabWidget>
#include <QRadioButton>
#include <QButtonGroup>

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

    QString activeColor() const { return m_isDark ? "#4EC9B0" : "#267F99"; }
    QString inactiveColor() const { return m_isDark ? "#808080" : "#AAAAAA"; }
    QString portColor() const { return m_isDark ? "#CE9178" : "#A31515"; }
    QString valueColor() const { return m_isDark ? "#B5CEA8" : "#098658"; }
    QString bitOnColor() const { return m_isDark ? "#4EC9B0" : "#267F99"; }
    QString bitOffColor() const { return m_isDark ? "#4E4E4E" : "#CCCCCC"; }

signals:
    void themeChanged(bool isDark);

private:
    ThemeManager() : m_isDark(false) {}
    bool m_isDark = false;
};

//=============================================================================
// Signal and Port definitions
//=============================================================================
struct SignalDef {
    QString name;
    QString shortName;
    QString description;
    bool active;
};

struct PortBit {
    int bit;
    QString name;
    QString description;
};

struct PortDef {
    uint16_t address;
    QString name;
    QString description;
    uint8_t value;
    QVector<PortBit> bits;
};

static QVector<SignalDef> g_signals = {
    {"DOS", "DOS", "TR-DOS ROM is paged in at $0000-$3FFF", true},
    {"ROM", "ROM", "Which ROM bank is active (128K: 0=Editor, 1=48K)", false},
    {"CPM", "CPM", "CP/M mode active (Scorpion)", false},
    {"INT", "INT", "Interrupt line state", true},
    {"NMI", "NMI", "Non-maskable interrupt pending", false},
    {"HALT", "HLT", "CPU is halted waiting for interrupt", false},
    {"MREQ", "MRQ", "Memory request active", true},
    {"IORQ", "IOQ", "I/O request active", false},
};

static QVector<PortDef> g_ports = {
    {0x7FFD, "7FFD", "128K memory paging", 0x10, {
        {0, "RAM0", "RAM bank bit 0"},
        {1, "RAM1", "RAM bank bit 1"},
        {2, "RAM2", "RAM bank bit 2"},
        {3, "SCR", "Screen select (0=RAM5, 1=RAM7)"},
        {4, "ROM", "ROM select (0=128K, 1=48K)"},
        {5, "DIS", "Disable paging (lock)"},
        {6, "-", "Unused"},
        {7, "-", "Unused"},
    }},
    {0x1FFD, "1FFD", "+3 extended paging", 0x00, {
        {0, "SPM", "Special paging mode"},
        {1, "SC0", "Special config bit 0"},
        {2, "SC1", "Special config bit 1"},
        {3, "DSK", "Disk motor"},
        {4, "PRT", "Printer strobe"},
        {5, "-", "Unused"},
        {6, "-", "Unused"},
        {7, "-", "Unused"},
    }},
    {0xFE, "FE", "ULA port (border/speaker/tape)", 0x07, {
        {0, "B0", "Border bit 0"},
        {1, "B1", "Border bit 1"},
        {2, "B2", "Border bit 2"},
        {3, "MIC", "MIC output"},
        {4, "EAR", "Speaker output"},
        {5, "-", "Unused"},
        {6, "-", "Unused"},
        {7, "-", "Unused"},
    }},
    {0xEFF7, "EFF7", "Pentagon 512/1024K paging", 0x00, {
        {0, "R3", "RAM bank bit 3"},
        {1, "R4", "RAM bank bit 4"},
        {2, "TRB", "Turbo mode"},
        {3, "SHA", "Shadow screen"},
        {4, "P0", "Pentagon bit 0"},
        {5, "P1", "Pentagon bit 1"},
        {6, "P2", "Pentagon bit 2"},
        {7, "DIS", "Lock paging"},
    }},
    {0xFFFD, "FFFD", "AY-3-8912 register select", 0x00, {
        {0, "R0", "Register bit 0"},
        {1, "R1", "Register bit 1"},
        {2, "R2", "Register bit 2"},
        {3, "R3", "Register bit 3"},
        {4, "-", "Unused"},
        {5, "-", "Unused"},
        {6, "-", "Unused"},
        {7, "-", "Unused"},
    }},
    {0xBFFD, "BFFD", "AY-3-8912 data write", 0x00, {
        {0, "D0", "Data bit 0"},
        {1, "D1", "Data bit 1"},
        {2, "D2", "Data bit 2"},
        {3, "D3", "Data bit 3"},
        {4, "D4", "Data bit 4"},
        {5, "D5", "Data bit 5"},
        {6, "D6", "Data bit 6"},
        {7, "D7", "Data bit 7"},
    }},
};

//=============================================================================
// Signal Popup
//=============================================================================
class SignalPopup : public QFrame {
    Q_OBJECT
public:
    static SignalPopup& instance() {
        static SignalPopup popup;
        return popup;
    }

    void showAt(const SignalDef& sig, const QPoint& pos) {
        auto& tm = ThemeManager::instance();

        QString stateColor = sig.active ? tm.activeColor() : tm.inactiveColor();
        m_titleLabel->setText(sig.name);
        m_titleLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(stateColor));

        m_stateTag->setText(sig.active ? "ACTIVE" : "INACTIVE");
        m_stateTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tm.tagBg()).arg(stateColor));

        m_description->setText(sig.description);

        adjustSize();
        QPoint finalPos = pos;
        QRect screen = QApplication::primaryScreen()->availableGeometry();
        if (finalPos.x() + width() > screen.right())
            finalPos.setX(pos.x() - width() - 10);
        if (finalPos.y() + height() > screen.bottom())
            finalPos.setY(screen.bottom() - height());
        move(finalPos);
        show();
    }

private:
    SignalPopup() : QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint) {
        setFrameStyle(QFrame::NoFrame);
        setAttribute(Qt::WA_ShowWithoutActivating);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(6);

        auto* headerRow = new QHBoxLayout();
        m_titleLabel = new QLabel();
        m_titleLabel->setFont(g_monoFont);
        headerRow->addWidget(m_titleLabel);
        m_stateTag = new QLabel();
        headerRow->addWidget(m_stateTag);
        headerRow->addStretch();
        layout->addLayout(headerRow);

        m_description = new QLabel();
        m_description->setFont(g_monoFont);
        m_description->setWordWrap(true);
        layout->addWidget(m_description);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &SignalPopup::applyTheme);
        applyTheme();
    }

    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("SignalPopup { background: %1; border: 1px solid %2; border-radius: 8px; }")
            .arg(tm.popupBg()).arg(tm.popupBorder()));
    }

    QLabel* m_titleLabel;
    QLabel* m_stateTag;
    QLabel* m_description;
};

//=============================================================================
// Port Popup
//=============================================================================
class PortPopup : public QFrame {
    Q_OBJECT
public:
    static PortPopup& instance() {
        static PortPopup popup;
        return popup;
    }

    void showAt(const PortDef& port, const QPoint& pos) {
        auto& tm = ThemeManager::instance();

        m_titleLabel->setText(QString("Port $%1").arg(port.address, 4, 16, QChar('0')).toUpper());
        m_titleLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(tm.portColor()));

        m_nameTag->setText(port.name);
        m_nameTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tm.tagBg()).arg(tm.portColor()));

        m_description->setText(port.description);

        m_valueLabel->setText(QString("Value: $%1 (%2)")
            .arg(port.value, 2, 16, QChar('0')).toUpper()
            .arg(port.value, 8, 2, QChar('0')));
        m_valueLabel->setStyleSheet(QString("color: %1;").arg(tm.valueColor()));

        QString bitsHtml;
        for (int i = 7; i >= 0; i--) {
            bool isOn = port.value & (1 << i);
            QString color = isOn ? tm.bitOnColor() : tm.bitOffColor();
            QString bitName = (i < port.bits.size()) ? port.bits[i].name : "-";
            bitsHtml += QString("<span style='color:%1; font-weight:bold;'>%2</span>")
                .arg(color).arg(isOn ? "1" : "0");
            if (i > 0 && i % 4 == 0) bitsHtml += " ";
        }
        m_bitsVisual->setText(bitsHtml);

        QString bitsText;
        for (int i = 7; i >= 0; i--) {
            if (i < port.bits.size() && port.bits[i].name != "-") {
                bool isOn = port.value & (1 << i);
                bitsText += QString("Bit %1 (%2): %3\n")
                    .arg(i)
                    .arg(port.bits[i].name)
                    .arg(isOn ? "1" : "0");
            }
        }
        m_bitsDetail->setText(bitsText.trimmed());

        adjustSize();
        QPoint finalPos = pos;
        QRect screen = QApplication::primaryScreen()->availableGeometry();
        if (finalPos.x() + width() > screen.right())
            finalPos.setX(pos.x() - width() - 10);
        if (finalPos.y() + height() > screen.bottom())
            finalPos.setY(screen.bottom() - height());
        move(finalPos);
        show();
    }

private:
    PortPopup() : QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint) {
        setFrameStyle(QFrame::NoFrame);
        setAttribute(Qt::WA_ShowWithoutActivating);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(6);

        auto* headerRow = new QHBoxLayout();
        m_titleLabel = new QLabel();
        m_titleLabel->setFont(g_monoFont);
        headerRow->addWidget(m_titleLabel);
        m_nameTag = new QLabel();
        headerRow->addWidget(m_nameTag);
        headerRow->addStretch();
        layout->addLayout(headerRow);

        m_description = new QLabel();
        m_description->setFont(g_monoFont);
        layout->addWidget(m_description);

        m_detailsBox = new QFrame();
        m_detailsBox->setFrameStyle(QFrame::NoFrame);
        auto* detailsLayout = new QVBoxLayout(m_detailsBox);
        detailsLayout->setContentsMargins(8, 6, 8, 6);
        detailsLayout->setSpacing(4);

        m_valueLabel = new QLabel();
        m_valueLabel->setFont(g_monoFont);
        detailsLayout->addWidget(m_valueLabel);

        m_bitsVisual = new QLabel();
        m_bitsVisual->setFont(g_monoFont);
        m_bitsVisual->setTextFormat(Qt::RichText);
        detailsLayout->addWidget(m_bitsVisual);

        m_bitsDetail = new QLabel();
        m_bitsDetail->setFont(g_monoFont);
        detailsLayout->addWidget(m_bitsDetail);

        layout->addWidget(m_detailsBox);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &PortPopup::applyTheme);
        applyTheme();
    }

    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("PortPopup { background: %1; border: 1px solid %2; border-radius: 8px; }")
            .arg(tm.popupBg()).arg(tm.popupBorder()));
        m_detailsBox->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(tm.contentBg()));
    }

    QLabel* m_titleLabel;
    QLabel* m_nameTag;
    QLabel* m_description;
    QFrame* m_detailsBox;
    QLabel* m_valueLabel;
    QLabel* m_bitsVisual;
    QLabel* m_bitsDetail;
};

//=============================================================================
// Clickable Signal Widget
//=============================================================================
class SignalWidget : public QFrame {
    Q_OBJECT
public:
    SignalWidget(int index, QWidget* parent = nullptr)
        : QFrame(parent), m_index(index) {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(24);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](bool) { update(); });
    }

signals:
    void hoverEnter(int index, QPoint pos);
    void hoverLeave();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        auto& tm = ThemeManager::instance();
        auto& sig = g_signals[m_index];

        QColor bgColor = sig.active ? QColor(tm.activeColor()) : QColor(tm.inactiveColor());
        bgColor.setAlpha(m_hovered ? 200 : 150);

        p.setBrush(bgColor);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4, 4);

        p.setPen(Qt::white);
        p.setFont(g_monoFont);
        p.drawText(rect(), Qt::AlignCenter, sig.shortName);
    }

    void enterEvent(QEnterEvent*) override {
        m_hovered = true;
        update();
        emit hoverEnter(m_index, mapToGlobal(rect().center()));
    }

    void leaveEvent(QEvent*) override {
        m_hovered = false;
        update();
        emit hoverLeave();
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            g_signals[m_index].active = !g_signals[m_index].active;
            update();
        }
    }

private:
    int m_index;
    bool m_hovered = false;
};

//=============================================================================
// Clickable Port Widget
//=============================================================================
class PortWidget : public QFrame {
    Q_OBJECT
public:
    PortWidget(int index, QWidget* parent = nullptr)
        : QFrame(parent), m_index(index) {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(4);

        m_nameLabel = new QLabel();
        m_nameLabel->setFont(g_monoFont);
        layout->addWidget(m_nameLabel);

        m_valueLabel = new QLabel();
        m_valueLabel->setFont(g_monoFont);
        layout->addWidget(m_valueLabel);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](bool) { applyTheme(); });
        applyTheme();
    }

    void updateDisplay() {
        auto& tm = ThemeManager::instance();
        auto& port = g_ports[m_index];
        m_nameLabel->setText(port.name);
        m_valueLabel->setText(QString("%1").arg(port.value, 2, 16, QChar('0')).toUpper());
        m_nameLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(tm.portColor()));
        m_valueLabel->setStyleSheet(QString("color: %1;").arg(tm.valueColor()));
    }

signals:
    void hoverEnter(int index, QPoint pos);
    void hoverLeave();

protected:
    void enterEvent(QEnterEvent*) override {
        m_hovered = true;
        applyTheme();
        emit hoverEnter(m_index, mapToGlobal(rect().center()));
    }

    void leaveEvent(QEvent*) override {
        m_hovered = false;
        applyTheme();
        emit hoverLeave();
    }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        QString borderColor = m_hovered ? tm.portColor() : tm.popupBorder();
        setStyleSheet(QString("PortWidget { background: %1; border: 1px solid %2; border-radius: 4px; }")
            .arg(tm.contentBg()).arg(borderColor));
        updateDisplay();
    }

    int m_index;
    bool m_hovered = false;
    QLabel* m_nameLabel;
    QLabel* m_valueLabel;
};

//=============================================================================
// Layout 1: Horizontal signal LEDs + port list
//=============================================================================
class Layout1 : public QGroupBox {
    Q_OBJECT
public:
    Layout1(QWidget* parent = nullptr) : QGroupBox("Layout 1: LED Signals + Port List", parent) {
        auto* layout = new QVBoxLayout(this);

        auto* sigRow = new QHBoxLayout();
        sigRow->addWidget(new QLabel("Signals:"));
        for (int i = 0; i < g_signals.size(); i++) {
            auto* sw = new SignalWidget(i);
            connect(sw, &SignalWidget::hoverEnter, this, &Layout1::onSignalHover);
            connect(sw, &SignalWidget::hoverLeave, this, &Layout1::onHoverLeave);
            sigRow->addWidget(sw);
        }
        sigRow->addStretch();
        layout->addLayout(sigRow);

        auto* portRow = new QHBoxLayout();
        portRow->addWidget(new QLabel("Ports:"));
        for (int i = 0; i < g_ports.size(); i++) {
            auto* pw = new PortWidget(i);
            connect(pw, &PortWidget::hoverEnter, this, &Layout1::onPortHover);
            connect(pw, &PortWidget::hoverLeave, this, &Layout1::onHoverLeave);
            portRow->addWidget(pw);
        }
        portRow->addStretch();
        layout->addLayout(portRow);

        layout->addStretch();  // Push content to top

        m_hoverTimer.setSingleShot(true);
        connect(&m_hoverTimer, &QTimer::timeout, this, &Layout1::showPopup);
    }

private slots:
    void onSignalHover(int index, QPoint pos) {
        SignalPopup::instance().hide();
        PortPopup::instance().hide();
        m_pendingSignal = index;
        m_pendingPort = -1;
        m_pendingPos = pos;
        m_hoverTimer.start(400);
    }

    void onPortHover(int index, QPoint pos) {
        SignalPopup::instance().hide();
        PortPopup::instance().hide();
        m_pendingPort = index;
        m_pendingSignal = -1;
        m_pendingPos = pos;
        m_hoverTimer.start(400);
    }

    void onHoverLeave() {
        m_hoverTimer.stop();
        SignalPopup::instance().hide();
        PortPopup::instance().hide();
    }

    void showPopup() {
        if (m_pendingSignal >= 0) {
            SignalPopup::instance().showAt(g_signals[m_pendingSignal], m_pendingPos);
        } else if (m_pendingPort >= 0) {
            PortPopup::instance().showAt(g_ports[m_pendingPort], m_pendingPos);
        }
    }

private:
    QTimer m_hoverTimer;
    int m_pendingSignal = -1;
    int m_pendingPort = -1;
    QPoint m_pendingPos;
};

//=============================================================================
// Layout 2: Compact vertical with binary display
//=============================================================================
class Layout2 : public QGroupBox {
    Q_OBJECT
public:
    Layout2(QWidget* parent = nullptr) : QGroupBox("Layout 2: Vertical Compact", parent) {
        auto* layout = new QHBoxLayout(this);

        auto* sigBox = new QVBoxLayout();
        sigBox->addWidget(new QLabel("Signals"));
        for (int i = 0; i < 4 && i < g_signals.size(); i++) {
            auto* sw = new SignalWidget(i);
            connect(sw, &SignalWidget::hoverEnter, this, &Layout2::onSignalHover);
            connect(sw, &SignalWidget::hoverLeave, this, &Layout2::onHoverLeave);
            sigBox->addWidget(sw);
        }
        sigBox->addStretch();
        layout->addLayout(sigBox);

        auto* portBox = new QVBoxLayout();
        portBox->addWidget(new QLabel("Ports"));
        for (int i = 0; i < 3 && i < g_ports.size(); i++) {
            auto* pw = new PortWidget(i);
            connect(pw, &PortWidget::hoverEnter, this, &Layout2::onPortHover);
            connect(pw, &PortWidget::hoverLeave, this, &Layout2::onHoverLeave);
            portBox->addWidget(pw);
        }
        portBox->addStretch();
        layout->addLayout(portBox);

        m_hoverTimer.setSingleShot(true);
        connect(&m_hoverTimer, &QTimer::timeout, this, &Layout2::showPopup);
    }

private slots:
    void onSignalHover(int index, QPoint pos) {
        SignalPopup::instance().hide();
        PortPopup::instance().hide();
        m_pendingSignal = index;
        m_pendingPort = -1;
        m_pendingPos = pos;
        m_hoverTimer.start(400);
    }

    void onPortHover(int index, QPoint pos) {
        SignalPopup::instance().hide();
        PortPopup::instance().hide();
        m_pendingPort = index;
        m_pendingSignal = -1;
        m_pendingPos = pos;
        m_hoverTimer.start(400);
    }

    void onHoverLeave() {
        m_hoverTimer.stop();
        SignalPopup::instance().hide();
        PortPopup::instance().hide();
    }

    void showPopup() {
        if (m_pendingSignal >= 0) {
            SignalPopup::instance().showAt(g_signals[m_pendingSignal], m_pendingPos);
        } else if (m_pendingPort >= 0) {
            PortPopup::instance().showAt(g_ports[m_pendingPort], m_pendingPos);
        }
    }

private:
    QTimer m_hoverTimer;
    int m_pendingSignal = -1;
    int m_pendingPort = -1;
    QPoint m_pendingPos;
};

//=============================================================================
// Layout 3: Table view
//=============================================================================
class Layout3 : public QGroupBox {
    Q_OBJECT
public:
    Layout3(QWidget* parent = nullptr) : QGroupBox("Layout 3: Table View", parent) {
        auto* layout = new QVBoxLayout(this);

        m_table = new QTableWidget(g_ports.size(), 4);
        m_table->setHorizontalHeaderLabels({"Port", "Name", "Value", "Binary"});
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->verticalHeader()->hide();
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setMouseTracking(true);

        for (int i = 0; i < g_ports.size(); i++) {
            auto& port = g_ports[i];
            m_table->setItem(i, 0, new QTableWidgetItem(QString("$%1").arg(port.address, 4, 16, QChar('0')).toUpper()));
            m_table->setItem(i, 1, new QTableWidgetItem(port.name));
            m_table->setItem(i, 2, new QTableWidgetItem(QString("$%1").arg(port.value, 2, 16, QChar('0')).toUpper()));
            m_table->setItem(i, 3, new QTableWidgetItem(QString("%1").arg(port.value, 8, 2, QChar('0'))));
        }

        connect(m_table, &QTableWidget::cellEntered, this, &Layout3::onCellHover);

        layout->addWidget(m_table);

        m_hoverTimer.setSingleShot(true);
        connect(&m_hoverTimer, &QTimer::timeout, this, &Layout3::showPopup);
    }

private slots:
    void onCellHover(int row, int) {
        PortPopup::instance().hide();
        m_pendingPort = row;
        m_pendingPos = QCursor::pos() + QPoint(10, 10);
        m_hoverTimer.start(400);
    }

    void showPopup() {
        if (m_pendingPort >= 0 && m_pendingPort < g_ports.size()) {
            PortPopup::instance().showAt(g_ports[m_pendingPort], m_pendingPos);
        }
    }

private:
    QTableWidget* m_table;
    QTimer m_hoverTimer;
    int m_pendingPort = -1;
    QPoint m_pendingPos;
};

//=============================================================================
// Layout 4: Grid of bit toggles
//=============================================================================
class BitToggle : public QWidget {
    Q_OBJECT
public:
    BitToggle(int portIdx, int bit, QWidget* parent = nullptr)
        : QWidget(parent), m_portIdx(portIdx), m_bit(bit) {
        setFixedSize(20, 20);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
    }

signals:
    void hoverEnter(int portIdx, QPoint pos);
    void hoverLeave();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        auto& tm = ThemeManager::instance();

        bool isOn = g_ports[m_portIdx].value & (1 << m_bit);
        QColor color = isOn ? QColor(tm.bitOnColor()) : QColor(tm.bitOffColor());

        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(rect().adjusted(2, 2, -2, -2));

        p.setPen(Qt::white);
        QFont f = g_monoFont;
        f.setPointSize(8);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, QString::number(m_bit));
    }

    void enterEvent(QEnterEvent*) override {
        emit hoverEnter(m_portIdx, mapToGlobal(rect().center()));
    }

    void leaveEvent(QEvent*) override {
        emit hoverLeave();
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            g_ports[m_portIdx].value ^= (1 << m_bit);
            update();
        }
    }

private:
    int m_portIdx;
    int m_bit;
};

class Layout4 : public QGroupBox {
    Q_OBJECT
public:
    Layout4(QWidget* parent = nullptr) : QGroupBox("Layout 4: Bit Grid", parent) {
        auto* layout = new QGridLayout(this);
        layout->setVerticalSpacing(2);
        layout->setHorizontalSpacing(4);
        layout->setContentsMargins(8, 8, 8, 8);

        int row = 0;
        for (int p = 0; p < 3 && p < g_ports.size(); p++) {
            auto* nameLabel = new QLabel(g_ports[p].name);
            nameLabel->setFont(g_monoFont);
            layout->addWidget(nameLabel, row, 0);

            for (int b = 7; b >= 0; b--) {
                auto* bt = new BitToggle(p, b);
                connect(bt, &BitToggle::hoverEnter, this, &Layout4::onPortHover);
                connect(bt, &BitToggle::hoverLeave, this, &Layout4::onHoverLeave);
                layout->addWidget(bt, row, 8 - b);
            }
            row++;
        }

        layout->setRowStretch(row, 1);  // Push content to top

        m_hoverTimer.setSingleShot(true);
        connect(&m_hoverTimer, &QTimer::timeout, this, &Layout4::showPopup);
    }

private slots:
    void onPortHover(int index, QPoint pos) {
        PortPopup::instance().hide();
        m_pendingPort = index;
        m_pendingPos = pos;
        m_hoverTimer.start(400);
    }

    void onHoverLeave() {
        m_hoverTimer.stop();
        PortPopup::instance().hide();
    }

    void showPopup() {
        if (m_pendingPort >= 0) {
            PortPopup::instance().showAt(g_ports[m_pendingPort], m_pendingPos);
        }
    }

private:
    QTimer m_hoverTimer;
    int m_pendingPort = -1;
    QPoint m_pendingPos;
};

//=============================================================================
// Layout 5: Dashboard style with big indicators
//=============================================================================
class BigSignal : public QFrame {
    Q_OBJECT
public:
    BigSignal(int index, QWidget* parent = nullptr) : QFrame(parent), m_index(index) {
        setFixedSize(60, 50);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
    }

signals:
    void hoverEnter(int index, QPoint pos);
    void hoverLeave();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        auto& tm = ThemeManager::instance();
        auto& sig = g_signals[m_index];

        QColor color = sig.active ? QColor(tm.activeColor()) : QColor(tm.inactiveColor());
        color.setAlpha(m_hovered ? 220 : 180);

        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 8, 8);

        p.setPen(Qt::white);
        QFont f = g_monoFont;
        f.setBold(true);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, sig.shortName);
    }

    void enterEvent(QEnterEvent*) override {
        m_hovered = true;
        update();
        emit hoverEnter(m_index, mapToGlobal(rect().center()));
    }

    void leaveEvent(QEvent*) override {
        m_hovered = false;
        update();
        emit hoverLeave();
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            g_signals[m_index].active = !g_signals[m_index].active;
            update();
        }
    }

private:
    int m_index;
    bool m_hovered = false;
};

class Layout5 : public QGroupBox {
    Q_OBJECT
public:
    Layout5(QWidget* parent = nullptr) : QGroupBox("Layout 5: Dashboard", parent) {
        auto* layout = new QGridLayout(this);

        for (int i = 0; i < g_signals.size(); i++) {
            auto* bs = new BigSignal(i);
            connect(bs, &BigSignal::hoverEnter, this, &Layout5::onSignalHover);
            connect(bs, &BigSignal::hoverLeave, this, &Layout5::onHoverLeave);
            layout->addWidget(bs, 0, i);
        }

        m_hoverTimer.setSingleShot(true);
        connect(&m_hoverTimer, &QTimer::timeout, this, &Layout5::showPopup);
    }

private slots:
    void onSignalHover(int index, QPoint pos) {
        SignalPopup::instance().hide();
        m_pendingSignal = index;
        m_pendingPos = pos;
        m_hoverTimer.start(400);
    }

    void onHoverLeave() {
        m_hoverTimer.stop();
        SignalPopup::instance().hide();
    }

    void showPopup() {
        if (m_pendingSignal >= 0) {
            SignalPopup::instance().showAt(g_signals[m_pendingSignal], m_pendingPos);
        }
    }

private:
    QTimer m_hoverTimer;
    int m_pendingSignal = -1;
    QPoint m_pendingPos;
};

//=============================================================================
// Layout 6: Memory-style hex dump of ports
//=============================================================================
class Layout6 : public QGroupBox {
    Q_OBJECT
public:
    Layout6(QWidget* parent = nullptr) : QGroupBox("Layout 6: Hex Dump Style", parent) {
        auto* layout = new QVBoxLayout(this);

        m_display = new QLabel();
        m_display->setFont(g_monoFont);
        m_display->setTextFormat(Qt::RichText);
        m_display->setMouseTracking(true);
        layout->addWidget(m_display);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](bool) { updateDisplay(); });
        updateDisplay();
    }

private:
    void updateDisplay() {
        auto& tm = ThemeManager::instance();
        QString html;
        html += QString("<table cellspacing='4'>");
        html += QString("<tr><td style='color:%1'>Addr</td><td style='color:%1'>Name</td><td style='color:%1'>Val</td></tr>")
            .arg(tm.hintColor());

        for (auto& port : g_ports) {
            html += QString("<tr><td style='color:%1'>$%2</td><td style='color:%3'>%4</td><td style='color:%5'>%6</td></tr>")
                .arg(tm.fg())
                .arg(port.address, 4, 16, QChar('0')).toUpper()
                .arg(tm.portColor())
                .arg(port.name)
                .arg(tm.valueColor())
                .arg(port.value, 2, 16, QChar('0')).toUpper();
        }
        html += "</table>";
        m_display->setText(html);
    }

    QLabel* m_display;
};

//=============================================================================
// Layout 7: Signal timeline (conceptual)
//=============================================================================
class Layout7 : public QGroupBox {
    Q_OBJECT
public:
    Layout7(QWidget* parent = nullptr) : QGroupBox("Layout 7: Signal Timeline", parent) {
        auto* layout = new QVBoxLayout(this);

        for (int i = 0; i < 4 && i < g_signals.size(); i++) {
            auto* row = new QHBoxLayout();

            auto* label = new QLabel(g_signals[i].shortName);
            label->setFont(g_monoFont);
            label->setFixedWidth(40);
            row->addWidget(label);

            auto* timeline = new QFrame();
            timeline->setFixedHeight(20);
            timeline->setStyleSheet(QString("background: %1; border-radius: 4px;")
                .arg(g_signals[i].active ? "#4EC9B0" : "#4E4E4E"));
            row->addWidget(timeline, 1);

            layout->addLayout(row);
        }
    }
};

//=============================================================================
// Layout 8: Port history log
//=============================================================================
class Layout8 : public QGroupBox {
    Q_OBJECT
public:
    Layout8(QWidget* parent = nullptr) : QGroupBox("Layout 8: Port History", parent) {
        auto* layout = new QVBoxLayout(this);

        m_log = new QLabel();
        m_log->setFont(g_monoFont);
        m_log->setAlignment(Qt::AlignTop | Qt::AlignLeft);

        QString text;
        text += "T+0000: OUT $7FFD, $10\n";
        text += "T+0124: OUT $FE, $07\n";
        text += "T+0256: OUT $FFFD, $07\n";
        text += "T+0384: OUT $BFFD, $3F\n";
        m_log->setText(text);

        layout->addWidget(m_log);
    }

private:
    QLabel* m_log;
};

//=============================================================================
// Layout 9: Combined compact view
//=============================================================================
class Layout9 : public QGroupBox {
    Q_OBJECT
public:
    Layout9(QWidget* parent = nullptr) : QGroupBox("Layout 9: Compact Combined", parent) {
        auto* layout = new QHBoxLayout(this);

        auto* sigFrame = new QFrame();
        auto* sigLayout = new QGridLayout(sigFrame);
        sigLayout->setSpacing(2);
        sigLayout->setContentsMargins(4, 4, 4, 4);

        for (int i = 0; i < g_signals.size(); i++) {
            auto* sw = new SignalWidget(i);
            sw->setFixedWidth(40);
            connect(sw, &SignalWidget::hoverEnter, this, &Layout9::onSignalHover);
            connect(sw, &SignalWidget::hoverLeave, this, &Layout9::onHoverLeave);
            sigLayout->addWidget(sw, i / 4, i % 4);
        }
        layout->addWidget(sigFrame);

        auto* sep = new QFrame();
        sep->setFrameShape(QFrame::VLine);
        layout->addWidget(sep);

        auto* portFrame = new QFrame();
        auto* portLayout = new QGridLayout(portFrame);
        portLayout->setSpacing(2);
        portLayout->setContentsMargins(4, 4, 4, 4);

        for (int i = 0; i < g_ports.size(); i++) {
            auto* pw = new PortWidget(i);
            connect(pw, &PortWidget::hoverEnter, this, &Layout9::onPortHover);
            connect(pw, &PortWidget::hoverLeave, this, &Layout9::onHoverLeave);
            portLayout->addWidget(pw, i / 3, i % 3);
        }
        layout->addWidget(portFrame);

        m_hoverTimer.setSingleShot(true);
        connect(&m_hoverTimer, &QTimer::timeout, this, &Layout9::showPopup);
    }

private slots:
    void onSignalHover(int index, QPoint pos) {
        SignalPopup::instance().hide();
        PortPopup::instance().hide();
        m_pendingSignal = index;
        m_pendingPort = -1;
        m_pendingPos = pos;
        m_hoverTimer.start(400);
    }

    void onPortHover(int index, QPoint pos) {
        SignalPopup::instance().hide();
        PortPopup::instance().hide();
        m_pendingPort = index;
        m_pendingSignal = -1;
        m_pendingPos = pos;
        m_hoverTimer.start(400);
    }

    void onHoverLeave() {
        m_hoverTimer.stop();
        SignalPopup::instance().hide();
        PortPopup::instance().hide();
    }

    void showPopup() {
        if (m_pendingSignal >= 0) {
            SignalPopup::instance().showAt(g_signals[m_pendingSignal], m_pendingPos);
        } else if (m_pendingPort >= 0) {
            PortPopup::instance().showAt(g_ports[m_pendingPort], m_pendingPos);
        }
    }

private:
    QTimer m_hoverTimer;
    int m_pendingSignal = -1;
    int m_pendingPort = -1;
    QPoint m_pendingPos;
};

//=============================================================================
// Main window
//=============================================================================
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow() {
        setWindowTitle("Signals & Ports POC");
        initMonoFont();
        setFont(g_monoFont);

        auto* mainLayout = new QVBoxLayout(this);

        // Theme radio buttons
        auto* themeRow = new QHBoxLayout();
        themeRow->addWidget(new QLabel("Theme:"));
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
        themeRow->addWidget(lightRadio);
        themeRow->addWidget(darkRadio);
        themeRow->addStretch();
        mainLayout->addLayout(themeRow);

        auto* tabs = new QTabWidget();
        tabs->addTab(new Layout1(), "1: LEDs");
        tabs->addTab(new Layout2(), "2: Details");
        tabs->addTab(new Layout3(), "3: Cards");
        tabs->addTab(new Layout4(), "4: Matrix");
        tabs->addTab(new Layout5(), "5: Big");
        tabs->addTab(new Layout6(), "6: Hex");
        tabs->addTab(new Layout7(), "7: Timeline");
        tabs->addTab(new Layout8(), "8: History");
        tabs->addTab(new Layout9(), "9: Combined");
        mainLayout->addWidget(tabs);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &MainWindow::applyTheme);
        applyTheme();
    }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("QWidget { background: %1; color: %2; }").arg(tm.bg()).arg(tm.fg()));
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    MainWindow w;
    w.resize(800, 900);
    w.show();

    return app.exec();
}

#include "main.moc"
