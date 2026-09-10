#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QScrollArea>
#include <QTimer>
#include <QMouseEvent>
#include <QFontDatabase>
#include <QScreen>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QRandomGenerator>

// Global monospace font
static QFont g_monoFont;

//=============================================================================
// Theme Manager
//=============================================================================
class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance() {
        static ThemeManager tm;
        return tm;
    }

    bool isDark() const { return m_dark; }
    void setDark(bool dark) {
        if (m_dark != dark) {
            m_dark = dark;
            emit themeChanged(m_dark);
        }
    }
    void toggle() { setDark(!m_dark); }

    // Colors
    QString windowBg() const { return m_dark ? "#1E1E1E" : "#F5F5F5"; }
    QString panelBg() const { return m_dark ? "#252526" : "#FFFFFF"; }
    QString textColor() const { return m_dark ? "#D4D4D4" : "#1E1E1E"; }
    QString dimText() const { return m_dark ? "#808080" : "#999999"; }
    QString borderColor() const { return m_dark ? "#3C3C3C" : "#E0E0E0"; }
    QString hoverBg() const { return m_dark ? "#2A2D2E" : "#E8E8E8"; }
    QString selectedBg() const { return m_dark ? "#094771" : "#0078D4"; }
    QString pcColor() const { return m_dark ? "#569CD6" : "#0066CC"; }
    QString spColor() const { return m_dark ? "#4EC9B0" : "#008080"; }
    QString regColor() const { return m_dark ? "#CE9178" : "#A31515"; }
    QString altRegColor() const { return m_dark ? "#DCDCAA" : "#795E26"; }
    QString ixiyColor() const { return m_dark ? "#C586C0" : "#AF00DB"; }
    QString popupBg() const { return m_dark ? "#2D2D30" : "#FFFFFF"; }
    QString popupBorder() const { return m_dark ? "#454545" : "#CCCCCC"; }
    QString contentBg() const { return m_dark ? "#1E1E1E" : "#F0F0F0"; }
    QString asciiColor() const { return m_dark ? "#6A9955" : "#008000"; }
    QString addressColor() const { return m_dark ? "#9CDCFE" : "#001080"; }

signals:
    void themeChanged(bool dark);

private:
    ThemeManager() : m_dark(false) {}
    bool m_dark;
};

//=============================================================================
// Simulated memory and registers
//=============================================================================
struct Z80State {
    uint16_t PC = 0x3683;
    uint16_t AF = 0x1D74;
    uint16_t BC = 0x0100;
    uint16_t DE = 0x2F6F;
    uint16_t HL = 0x5C3B;
    uint16_t SP = 0x5BFB;
    uint16_t AF_ = 0x0044;
    uint16_t BC_ = 0x0A1A;
    uint16_t DE_ = 0x0007;
    uint16_t HL_ = 0xFFFF;
    uint16_t IX = 0xFD6C;
    uint16_t IY = 0x5C3A;
    uint8_t I = 0x00;
    uint8_t R = 0x24;

    uint8_t memory[65536];

    Z80State() {
        // Fill with semi-random data
        for (int i = 0; i < 65536; i++) {
            memory[i] = (i * 7 + 13) & 0xFF;
        }
        // Some recognizable patterns at register addresses
        memory[PC] = 0xCB; memory[PC+1] = 0x6E;
        memory[SP] = 0x83; memory[SP+1] = 0x36;
        memory[HL] = 0x1D; memory[HL+1] = 0x20;
        memory[IY] = 0xFF; memory[IY+1] = 0x1D;
    }
};

static Z80State g_z80;

//=============================================================================
// Register definition
//=============================================================================
struct RegDef {
    const char* name;
    uint16_t* valuePtr;
    bool isAlt;
    bool isIndex;
};

static RegDef g_registers[] = {
    {"PC", &g_z80.PC, false, false},
    {"BC", &g_z80.BC, false, false},
    {"DE", &g_z80.DE, false, false},
    {"HL", &g_z80.HL, false, false},
    {"SP", &g_z80.SP, false, false},
    {"AF'", &g_z80.AF_, true, false},
    {"BC'", &g_z80.BC_, true, false},
    {"DE'", &g_z80.DE_, true, false},
    {"HL'", &g_z80.HL_, true, false},
    {"IX", &g_z80.IX, false, true},
    {"IY", &g_z80.IY, false, true},
};

QString getRegColor(const RegDef& reg) {
    auto& tm = ThemeManager::instance();
    if (strcmp(reg.name, "PC") == 0) return tm.pcColor();
    if (strcmp(reg.name, "SP") == 0) return tm.spColor();
    if (reg.isAlt) return tm.altRegColor();
    if (reg.isIndex) return tm.ixiyColor();
    return tm.regColor();
}

QString formatHexDump(uint16_t addr, int count = 16) {
    QString hex;
    for (int i = 0; i < count; i++) {
        if (i > 0) hex += " ";
        hex += QString("%1").arg(g_z80.memory[(addr + i) & 0xFFFF], 2, 16, QChar('0')).toUpper();
    }
    return hex;
}

QString formatAscii(uint16_t addr, int count = 16) {
    QString ascii;
    for (int i = 0; i < count; i++) {
        uint8_t c = g_z80.memory[(addr + i) & 0xFFFF];
        ascii += (c >= 32 && c < 127) ? QChar(c) : QChar('.');
    }
    return ascii;
}

//=============================================================================
// Modern Popup for register details
//=============================================================================
class RegDumpPopup : public QFrame {
    Q_OBJECT
public:
    static RegDumpPopup& instance() {
        static RegDumpPopup popup;
        return popup;
    }

    void showAt(const RegDef& reg, const QPoint& pos) {
        auto& tm = ThemeManager::instance();
        uint16_t addr = *reg.valuePtr;
        QString color = getRegColor(reg);

        m_titleLabel->setText(QString("%1").arg(reg.name));
        m_titleLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(color));

        QString tagBg = tm.isDark() ? "#3C3C3C" : "#E8E8E8";
        m_addrTag->setText(QString("$%1").arg(addr, 4, 16, QChar('0')).toUpper());
        m_addrTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tagBg).arg(tm.addressColor()));

        // Memory content
        QString hexLines;
        for (int row = 0; row < 4; row++) {
            uint16_t rowAddr = (addr + row * 8) & 0xFFFF;
            hexLines += QString("$%1: %2  %3\n")
                .arg(rowAddr, 4, 16, QChar('0')).toUpper()
                .arg(formatHexDump(rowAddr, 8))
                .arg(formatAscii(rowAddr, 8));
        }
        m_hexDump->setText(hexLines.trimmed());
        m_hexDump->setStyleSheet(QString("color: %1;").arg(tm.textColor()));

        // Annotations
        QString annotations;
        if (strcmp(reg.name, "PC") == 0) {
            annotations = "Program Counter - next instruction";
        } else if (strcmp(reg.name, "SP") == 0) {
            annotations = "Stack Pointer - top of stack";
        } else if (strcmp(reg.name, "IY") == 0 && addr == 0x5C3A) {
            annotations = "System variables base (BASIC)";
        } else if (strcmp(reg.name, "HL") == 0) {
            annotations = "General purpose / memory pointer";
        } else if (reg.isAlt) {
            annotations = "Alternate register set";
        } else if (reg.isIndex) {
            annotations = "Index register";
        }
        m_annotations->setText(annotations);
        m_annotations->setVisible(!annotations.isEmpty());

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
    RegDumpPopup() : QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint) {
        setFrameStyle(QFrame::NoFrame);
        setAttribute(Qt::WA_ShowWithoutActivating);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(8);

        // Header
        auto* header = new QHBoxLayout();
        m_titleLabel = new QLabel();
        m_titleLabel->setFont(g_monoFont);
        header->addWidget(m_titleLabel);
        m_addrTag = new QLabel();
        m_addrTag->setFont(g_monoFont);
        header->addWidget(m_addrTag);
        header->addStretch();
        layout->addLayout(header);

        // Hex dump box
        m_hexBox = new QFrame();
        m_hexBox->setFrameStyle(QFrame::NoFrame);
        auto* hexLayout = new QVBoxLayout(m_hexBox);
        hexLayout->setContentsMargins(8, 6, 8, 6);
        m_hexDump = new QLabel();
        m_hexDump->setFont(g_monoFont);
        hexLayout->addWidget(m_hexDump);
        layout->addWidget(m_hexBox);

        // Annotations
        m_annotations = new QLabel();
        m_annotations->setFont(g_monoFont);
        m_annotations->setWordWrap(true);
        layout->addWidget(m_annotations);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &RegDumpPopup::applyTheme);
        applyTheme();
    }

    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString(
            "RegDumpPopup { background: %1; border: 1px solid %2; border-radius: 8px; }"
        ).arg(tm.popupBg()).arg(tm.popupBorder()));
        m_hexBox->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(tm.contentBg()));
        m_annotations->setStyleSheet(QString("color: %1; font-style: italic;").arg(tm.dimText()));
    }

    QLabel* m_titleLabel;
    QLabel* m_addrTag;
    QFrame* m_hexBox;
    QLabel* m_hexDump;
    QLabel* m_annotations;
};

//=============================================================================
// Hoverable register row widget
//=============================================================================
class RegisterRow : public QFrame {
    Q_OBJECT
public:
    RegisterRow(int regIndex, QWidget* parent = nullptr)
        : QFrame(parent), m_regIndex(regIndex), m_hovered(false) {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(16);

        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setSingleShot(true);
        connect(m_hoverTimer, &QTimer::timeout, this, &RegisterRow::showPopup);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
                this, &RegisterRow::updateStyle);
    }

    void updateContent() {
        update();
    }

signals:
    void hidePopup();

protected:
    void enterEvent(QEnterEvent* e) override {
        m_hovered = true;
        m_lastMousePos = QCursor::pos();  // Capture current cursor position
        updateStyle();
        emit hidePopup();
        m_hoverTimer->start(400);
    }

    void leaveEvent(QEvent*) override {
        m_hovered = false;
        updateStyle();
        m_hoverTimer->stop();
        RegDumpPopup::instance().hide();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        m_lastMousePos = mapToGlobal(e->pos());
    }

    void updateStyle() {
        auto& tm = ThemeManager::instance();
        QString bg = m_hovered ? tm.hoverBg() : "transparent";
        setStyleSheet(QString("RegisterRow { background: %1; border-radius: 4px; }").arg(bg));
    }

private slots:
    void showPopup() {
        if (m_hovered && m_regIndex >= 0 && m_regIndex < 11) {
            RegDumpPopup::instance().showAt(g_registers[m_regIndex],
                m_lastMousePos + QPoint(15, 15));
        }
    }

private:
    int m_regIndex;
    bool m_hovered;
    QTimer* m_hoverTimer;
    QPoint m_lastMousePos;
};

//=============================================================================
// Layout 1: Xpeccy Classic - exact format from reference debugger
// REG (ADDR): HH HH HH HH  HH HH HH HH  HH HH HH HH  HH HH HH HH
//=============================================================================
class Layout1 : public QGroupBox {
    Q_OBJECT
public:
    Layout1(QWidget* parent = nullptr) : QGroupBox("Layout 1: Xpeccy Classic", parent) {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);
        layout->setContentsMargins(4, 4, 4, 4);

        for (int i = 0; i < 11; i++) {
            auto* row = new RegisterRow(i);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 1, 0, 1);
            rowLayout->setSpacing(0);

            // "PC (3683):" format
            auto* regLabel = new QLabel();
            regLabel->setFont(g_monoFont);
            regLabel->setFixedWidth(85);
            m_regLabels.append(regLabel);
            rowLayout->addWidget(regLabel);

            // Memory bytes - 8 bytes only for compact display
            auto* hexLabel = new QLabel();
            hexLabel->setFont(g_monoFont);
            m_hexLabels.append(hexLabel);
            rowLayout->addWidget(hexLabel);
            rowLayout->addStretch();

            layout->addWidget(row);
        }

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout1::updateAll);
        updateAll();
    }

    void updateAll() {
        auto& tm = ThemeManager::instance();
        for (int i = 0; i < 11; i++) {
            uint16_t addr = *g_registers[i].valuePtr;
            QString regText = QString("<span style='color:%1;font-weight:bold;'>%2</span> <span style='color:%3;'>(%4):</span>")
                .arg(getRegColor(g_registers[i]))
                .arg(g_registers[i].name, -3)
                .arg(tm.textColor())
                .arg(addr, 4, 16, QChar('0')).toUpper();
            m_regLabels[i]->setText(regText);
            m_regLabels[i]->setTextFormat(Qt::RichText);

            // Format hex - 8 bytes for compact display
            m_hexLabels[i]->setText(formatHexDump(addr, 8));
            m_hexLabels[i]->setStyleSheet(QString("color: %1;").arg(tm.textColor()));
        }
    }

private:
    QVector<QLabel*> m_regLabels;
    QVector<QLabel*> m_hexLabels;
};

//=============================================================================
// Layout 2: With ASCII - hex dump plus ASCII representation
//=============================================================================
class Layout2 : public QGroupBox {
    Q_OBJECT
public:
    Layout2(QWidget* parent = nullptr) : QGroupBox("Layout 2: With ASCII", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);
        layout->setContentsMargins(4, 4, 4, 4);

        for (int i = 0; i < 11; i++) {
            auto* row = new RegisterRow(i);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 1, 0, 1);
            rowLayout->setSpacing(4);

            auto* regLabel = new QLabel();
            regLabel->setFont(g_monoFont);
            regLabel->setFixedWidth(90);
            m_regLabels.append(regLabel);
            rowLayout->addWidget(regLabel);

            auto* hexLabel = new QLabel();
            hexLabel->setFont(g_monoFont);
            m_hexLabels.append(hexLabel);
            rowLayout->addWidget(hexLabel);

            auto* asciiLabel = new QLabel();
            asciiLabel->setFont(g_monoFont);
            asciiLabel->setFixedWidth(130);
            m_asciiLabels.append(asciiLabel);
            rowLayout->addWidget(asciiLabel);

            layout->addWidget(row);
        }

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout2::updateAll);
        updateAll();
    }

    void updateAll() {
        auto& tm = ThemeManager::instance();
        for (int i = 0; i < 11; i++) {
            uint16_t addr = *g_registers[i].valuePtr;
            m_regLabels[i]->setText(QString("<span style='color:%1;font-weight:bold;'>%2</span> <span style='color:%3;'>(%4):</span>")
                .arg(getRegColor(g_registers[i]))
                .arg(g_registers[i].name, -3)
                .arg(tm.textColor())
                .arg(addr, 4, 16, QChar('0')).toUpper());
            m_regLabels[i]->setTextFormat(Qt::RichText);
            m_hexLabels[i]->setText(formatHexDump(addr, 16));
            m_hexLabels[i]->setStyleSheet(QString("color: %1;").arg(tm.textColor()));
            m_asciiLabels[i]->setText("|" + formatAscii(addr, 16) + "|");
            m_asciiLabels[i]->setStyleSheet(QString("color: %1;").arg(tm.asciiColor()));
        }
    }

private:
    QVector<QLabel*> m_regLabels;
    QVector<QLabel*> m_hexLabels;
    QVector<QLabel*> m_asciiLabels;
};

//=============================================================================
// Layout 3: Compact Table - clean table with selection
//=============================================================================
class Layout3 : public QGroupBox {
    Q_OBJECT
public:
    Layout3(QWidget* parent = nullptr) : QGroupBox("Layout 3: Table", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(4, 4, 4, 4);

        m_table = new QTableWidget(11, 3);
        m_table->setFont(g_monoFont);
        m_table->setHorizontalHeaderLabels({"Reg", "Value", "Memory at address"});
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->verticalHeader()->setVisible(false);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->setColumnWidth(0, 35);
        m_table->setColumnWidth(1, 50);
        m_table->setShowGrid(false);
        m_table->setAlternatingRowColors(true);

        for (int i = 0; i < 11; i++) {
            auto* regItem = new QTableWidgetItem(g_registers[i].name);
            regItem->setForeground(QColor(getRegColor(g_registers[i])));
            regItem->setFont(g_monoFont);
            m_table->setItem(i, 0, regItem);
            m_table->setItem(i, 1, new QTableWidgetItem());
            m_table->setItem(i, 2, new QTableWidgetItem());
            m_table->setRowHeight(i, 22);
        }

        layout->addWidget(m_table);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout3::updateAll);
        updateAll();
    }

    void updateAll() {
        for (int i = 0; i < 11; i++) {
            uint16_t addr = *g_registers[i].valuePtr;
            m_table->item(i, 1)->setText(QString("%1").arg(addr, 4, 16, QChar('0')).toUpper());
            m_table->item(i, 2)->setText(formatHexDump(addr, 16));
        }
    }

private:
    QTableWidget* m_table;
};

//=============================================================================
// Layout 4: Compact 8-byte - denser view for small screens
//=============================================================================
class Layout4 : public QGroupBox {
    Q_OBJECT
public:
    Layout4(QWidget* parent = nullptr) : QGroupBox("Layout 4: Compact", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);
        layout->setContentsMargins(4, 4, 4, 4);

        for (int i = 0; i < 11; i++) {
            auto* row = new RegisterRow(i);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(4);

            auto* label = new QLabel();
            label->setFont(g_monoFont);
            m_labels.append(label);
            rowLayout->addWidget(label);
            rowLayout->addStretch();

            layout->addWidget(row);
        }

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout4::updateAll);
        updateAll();
    }

    void updateAll() {
        auto& tm = ThemeManager::instance();
        for (int i = 0; i < 11; i++) {
            uint16_t addr = *g_registers[i].valuePtr;
            QString text = QString("<span style='color:%1;font-weight:bold;'>%2</span> %3: %4")
                .arg(getRegColor(g_registers[i]))
                .arg(g_registers[i].name, -3)
                .arg(addr, 4, 16, QChar('0')).toUpper()
                .arg(formatHexDump(addr, 8));
            m_labels[i]->setText(text);
            m_labels[i]->setTextFormat(Qt::RichText);
        }
    }

private:
    QVector<QLabel*> m_labels;
};

//=============================================================================
// Layout 5: Main/Alt Split - main registers left, alternate right
//=============================================================================
class Layout5 : public QGroupBox {
    Q_OBJECT
public:
    Layout5(QWidget* parent = nullptr) : QGroupBox("Layout 5: Main / Alternate", parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setSpacing(16);

        // Main registers (PC, BC, DE, HL, SP)
        auto* mainBox = new QGroupBox("Main");
        auto* mainLayout = new QVBoxLayout(mainBox);
        mainLayout->setSpacing(0);
        mainLayout->setContentsMargins(4, 2, 4, 2);
        for (int i = 0; i < 5; i++) {
            mainLayout->addWidget(createRow(i));
        }
        layout->addWidget(mainBox);

        // Alternate + Index (AF', BC', DE', HL', IX, IY)
        auto* altBox = new QGroupBox("Alt / Index");
        auto* altLayout = new QVBoxLayout(altBox);
        altLayout->setSpacing(0);
        altLayout->setContentsMargins(4, 2, 4, 2);
        for (int i = 5; i < 11; i++) {
            altLayout->addWidget(createRow(i));
        }
        layout->addWidget(altBox);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout5::updateAll);
        updateAll();
    }

    void updateAll() {
        auto& tm = ThemeManager::instance();
        for (int i = 0; i < 11; i++) {
            uint16_t addr = *g_registers[i].valuePtr;
            m_labels[i]->setText(QString("%1: %2")
                .arg(addr, 4, 16, QChar('0')).toUpper()
                .arg(formatHexDump(addr, 8)));
            m_labels[i]->setStyleSheet(QString("color: %1;").arg(tm.textColor()));
        }
    }

private:
    QWidget* createRow(int i) {
        auto* row = new RegisterRow(i);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(4, 1, 4, 1);

        auto* name = new QLabel(g_registers[i].name);
        name->setFont(g_monoFont);
        name->setFixedWidth(30);
        name->setStyleSheet(QString("color: %1; font-weight: bold;").arg(getRegColor(g_registers[i])));
        layout->addWidget(name);

        auto* data = new QLabel();
        data->setFont(g_monoFont);
        m_labels.append(data);
        layout->addWidget(data, 1);

        return row;
    }

    QVector<QLabel*> m_labels;
};

//=============================================================================
// Layout 6: Detail Panel - select register to see extended memory view
//=============================================================================
class Layout6 : public QGroupBox {
    Q_OBJECT
public:
    Layout6(QWidget* parent = nullptr) : QGroupBox("Layout 6: Detail Panel", parent) {
        auto* layout = new QHBoxLayout(this);

        // Register list on left
        auto* listBox = new QGroupBox("Registers");
        auto* listLayout = new QVBoxLayout(listBox);
        listLayout->setSpacing(0);
        listBox->setFixedWidth(150);

        for (int i = 0; i < 11; i++) {
            auto* row = new RegisterRow(i);
            row->setCursor(Qt::PointingHandCursor);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 2, 4, 2);

            auto* label = new QLabel();
            label->setFont(g_monoFont);
            m_listLabels.append(label);
            rowLayout->addWidget(label);

            connect(row, &RegisterRow::hidePopup, this, [this, i]() { selectRegister(i); });
            listLayout->addWidget(row);
        }
        listLayout->addStretch();
        layout->addWidget(listBox);

        // Detail view on right
        auto* detailBox = new QGroupBox("Memory at selected register");
        auto* detailLayout = new QVBoxLayout(detailBox);
        m_detailLabel = new QLabel();
        m_detailLabel->setFont(g_monoFont);
        m_detailLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        detailLayout->addWidget(m_detailLabel);
        layout->addWidget(detailBox, 1);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout6::updateAll);
        m_selected = 0;
        updateAll();
    }

    void updateAll() {
        auto& tm = ThemeManager::instance();
        for (int i = 0; i < 11; i++) {
            uint16_t addr = *g_registers[i].valuePtr;
            QString style = (i == m_selected) ? QString("background: %1;").arg(tm.selectedBg()) : "";
            m_listLabels[i]->setText(QString("<span style='color:%1;font-weight:bold;'>%2</span> %3")
                .arg(getRegColor(g_registers[i]))
                .arg(g_registers[i].name, -3)
                .arg(addr, 4, 16, QChar('0')).toUpper());
            m_listLabels[i]->setTextFormat(Qt::RichText);
            m_listLabels[i]->parentWidget()->setStyleSheet(style);
        }
        updateDetail();
    }

private:
    void selectRegister(int idx) {
        m_selected = idx;
        updateAll();
    }

    void updateDetail() {
        auto& tm = ThemeManager::instance();
        uint16_t addr = *g_registers[m_selected].valuePtr;
        QString detail;
        detail += QString("<span style='color:%1;font-weight:bold;'>%2</span> = $%3\n\n")
            .arg(getRegColor(g_registers[m_selected]))
            .arg(g_registers[m_selected].name)
            .arg(addr, 4, 16, QChar('0')).toUpper();

        for (int row = 0; row < 8; row++) {
            uint16_t rowAddr = (addr + row * 8) & 0xFFFF;
            detail += QString("<span style='color:%1;'>$%2:</span> %3  <span style='color:%4;'>|%5|</span>\n")
                .arg(tm.addressColor())
                .arg(rowAddr, 4, 16, QChar('0')).toUpper()
                .arg(formatHexDump(rowAddr, 8))
                .arg(tm.asciiColor())
                .arg(formatAscii(rowAddr, 8));
        }
        m_detailLabel->setText(detail.trimmed());
        m_detailLabel->setTextFormat(Qt::RichText);
    }

    int m_selected = 0;
    QVector<QLabel*> m_listLabels;
    QLabel* m_detailLabel;
};

//=============================================================================
// Layout 7: Grouped - organized by register type (Main/Alt/Index)
//=============================================================================
class Layout7 : public QGroupBox {
    Q_OBJECT
public:
    Layout7(QWidget* parent = nullptr) : QGroupBox("Layout 7: Grouped", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(8);

        // Main registers (PC, BC, DE, HL, SP)
        auto* mainGroup = new QGroupBox("Main Registers");
        auto* mainLayout = new QVBoxLayout(mainGroup);
        mainLayout->setSpacing(0);
        for (int i = 0; i < 5; i++) {
            mainLayout->addWidget(createRow(i));
        }
        layout->addWidget(mainGroup);

        // Alternate registers (AF', BC', DE', HL')
        auto* altGroup = new QGroupBox("Alternate Set");
        auto* altLayout = new QVBoxLayout(altGroup);
        altLayout->setSpacing(0);
        for (int i = 5; i < 9; i++) {
            altLayout->addWidget(createRow(i));
        }
        layout->addWidget(altGroup);

        // Index registers (IX, IY)
        auto* idxGroup = new QGroupBox("Index Registers");
        auto* idxLayout = new QVBoxLayout(idxGroup);
        idxLayout->setSpacing(0);
        for (int i = 9; i < 11; i++) {
            idxLayout->addWidget(createRow(i));
        }
        layout->addWidget(idxGroup);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout7::updateAll);
        updateAll();
    }

    void updateAll() {
        auto& tm = ThemeManager::instance();
        for (int i = 0; i < 11; i++) {
            uint16_t addr = *g_registers[i].valuePtr;
            m_hexLabels[i]->setText(QString("(%1): %2")
                .arg(addr, 4, 16, QChar('0')).toUpper()
                .arg(formatHexDump(addr, 8)));
            m_hexLabels[i]->setStyleSheet(QString("color: %1;").arg(tm.textColor()));
        }
    }

private:
    QWidget* createRow(int i) {
        auto* row = new RegisterRow(i);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(4, 1, 4, 1);

        auto* nameLabel = new QLabel(g_registers[i].name);
        nameLabel->setFont(g_monoFont);
        nameLabel->setFixedWidth(30);
        nameLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(getRegColor(g_registers[i])));
        layout->addWidget(nameLabel);

        auto* hexLabel = new QLabel();
        hexLabel->setFont(g_monoFont);
        layout->addWidget(hexLabel, 1);
        m_hexLabels.append(hexLabel);

        return row;
    }

    QVector<QLabel*> m_hexLabels;
};

//=============================================================================
// Layout 8: Wide Format - full 16 bytes + ASCII, like hex editor row
//=============================================================================
class Layout8 : public QGroupBox {
    Q_OBJECT
public:
    Layout8(QWidget* parent = nullptr) : QGroupBox("Layout 8: Wide Format", parent) {
        auto* scrollArea = new QScrollArea();
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scrollArea->setFrameStyle(QFrame::NoFrame);

        auto* content = new QWidget();
        auto* layout = new QVBoxLayout(content);
        layout->setSpacing(0);
        layout->setContentsMargins(4, 4, 4, 4);

        for (int i = 0; i < 11; i++) {
            auto* row = new RegisterRow(i);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 1, 0, 1);
            rowLayout->setSpacing(8);

            auto* label = new QLabel();
            label->setFont(g_monoFont);
            m_labels.append(label);
            rowLayout->addWidget(label);

            layout->addWidget(row);
        }
        layout->addStretch();

        scrollArea->setWidget(content);

        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->addWidget(scrollArea);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout8::updateAll);
        updateAll();
    }

    void updateAll() {
        auto& tm = ThemeManager::instance();
        for (int i = 0; i < 11; i++) {
            uint16_t addr = *g_registers[i].valuePtr;

            // Format: PC (3683): CB 6E 28 FC ... |.n(......|
            QString hex;
            for (int j = 0; j < 16; j++) {
                hex += QString("%1").arg(g_z80.memory[(addr + j) & 0xFFFF], 2, 16, QChar('0')).toUpper();
                if (j < 15) hex += (j % 4 == 3) ? "  " : " ";
            }

            QString text = QString("<span style='color:%1;font-weight:bold;'>%2</span> "
                                   "<span style='color:%3;'>(%4):</span> %5  "
                                   "<span style='color:%6;'>|%7|</span>")
                .arg(getRegColor(g_registers[i]))
                .arg(g_registers[i].name, -3)
                .arg(tm.textColor())
                .arg(addr, 4, 16, QChar('0')).toUpper()
                .arg(hex)
                .arg(tm.asciiColor())
                .arg(formatAscii(addr, 16));

            m_labels[i]->setText(text);
            m_labels[i]->setTextFormat(Qt::RichText);
        }
    }

private:
    QVector<QLabel*> m_labels;
};

//=============================================================================
// Layout 9: System Vars - shows annotations for IY-based system variables
//=============================================================================
class Layout9 : public QGroupBox {
    Q_OBJECT
public:
    Layout9(QWidget* parent = nullptr) : QGroupBox("Layout 9: With Annotations", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);
        layout->setContentsMargins(4, 4, 4, 4);

        for (int i = 0; i < 11; i++) {
            auto* row = new RegisterRow(i);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 1, 0, 1);
            rowLayout->setSpacing(4);

            auto* regLabel = new QLabel();
            regLabel->setFont(g_monoFont);
            regLabel->setFixedWidth(90);
            m_regLabels.append(regLabel);
            rowLayout->addWidget(regLabel);

            auto* hexLabel = new QLabel();
            hexLabel->setFont(g_monoFont);
            m_hexLabels.append(hexLabel);
            rowLayout->addWidget(hexLabel);

            auto* annotLabel = new QLabel();
            annotLabel->setFont(g_monoFont);
            m_annotLabels.append(annotLabel);
            rowLayout->addWidget(annotLabel, 1);

            layout->addWidget(row);
        }

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout9::updateAll);
        updateAll();
    }

    void updateAll() {
        auto& tm = ThemeManager::instance();
        for (int i = 0; i < 11; i++) {
            uint16_t addr = *g_registers[i].valuePtr;

            m_regLabels[i]->setText(QString("<span style='color:%1;font-weight:bold;'>%2</span> <span style='color:%3;'>(%4):</span>")
                .arg(getRegColor(g_registers[i]))
                .arg(g_registers[i].name, -3)
                .arg(tm.textColor())
                .arg(addr, 4, 16, QChar('0')).toUpper());
            m_regLabels[i]->setTextFormat(Qt::RichText);

            m_hexLabels[i]->setText(formatHexDump(addr, 8));
            m_hexLabels[i]->setStyleSheet(QString("color: %1;").arg(tm.textColor()));

            // Add annotations for known addresses
            QString annot = getAnnotation(g_registers[i].name, addr);
            m_annotLabels[i]->setText(annot);
            QString hintCol = tm.isDark() ? "#808080" : "#666666";
            m_annotLabels[i]->setStyleSheet(QString("color: %1; font-style: italic;").arg(hintCol));
        }
    }

private:
    QString getAnnotation(const char* regName, uint16_t addr) {
        // PC annotations
        if (strcmp(regName, "PC") == 0) {
            if (addr < 0x4000) return "ROM";
            if (addr >= 0x4000 && addr < 0x5B00) return "Screen/Attrs";
            if (addr >= 0x5B00 && addr < 0x5C00) return "Printer buffer";
            if (addr >= 0x5C00 && addr < 0x5E00) return "System vars";
            return "User RAM";
        }
        // SP annotations
        if (strcmp(regName, "SP") == 0) {
            if (addr < 0x4000) return "Stack in ROM!";
            if (addr >= 0x5B00 && addr < 0x5C00) return "Stack in printer buf";
            return "Stack";
        }
        // IY typically points to system vars
        if (strcmp(regName, "IY") == 0) {
            if (addr == 0x5C3A) return "ERR_NR (sys vars base)";
            if (addr >= 0x5C00 && addr < 0x5E00) return "System variables";
        }
        // HL common uses
        if (strcmp(regName, "HL") == 0) {
            if (addr >= 0x4000 && addr < 0x5800) return "Screen data";
            if (addr >= 0x5800 && addr < 0x5B00) return "Attributes";
        }
        return "";
    }

    QVector<QLabel*> m_regLabels;
    QVector<QLabel*> m_hexLabels;
    QVector<QLabel*> m_annotLabels;
};

//=============================================================================
// Main Window
//=============================================================================
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow() {
        setWindowTitle("Register Dump POC - Layout Comparison");

        auto* mainLayout = new QVBoxLayout(this);

        // Header with theme radio buttons
        auto* header = new QHBoxLayout();
        header->addWidget(new QLabel("Theme:"));
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
        header->addWidget(lightRadio);
        header->addWidget(darkRadio);

        auto* randomBtn = new QPushButton("Randomize Values");
        connect(randomBtn, &QPushButton::clicked, this, &MainWindow::randomizeValues);
        header->addWidget(randomBtn);

        header->addStretch();
        mainLayout->addLayout(header);

        // Tabs for layouts
        auto* tabs = new QTabWidget();
        tabs->addTab(new Layout1(), "1: Xpeccy");
        tabs->addTab(new Layout2(), "2: +ASCII");
        tabs->addTab(new Layout3(), "3: Table");
        tabs->addTab(new Layout4(), "4: Compact");
        tabs->addTab(new Layout5(), "5: Main/Alt");
        tabs->addTab(new Layout6(), "6: Detail");
        tabs->addTab(new Layout7(), "7: Grouped");
        tabs->addTab(new Layout8(), "8: Wide");
        tabs->addTab(new Layout9(), "9: Annotated");
        mainLayout->addWidget(tabs);

        // Apply initial theme
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &MainWindow::applyTheme);
        applyTheme();
    }

private slots:
    void randomizeValues() {
        auto* gen = QRandomGenerator::global();
        g_z80.PC = gen->bounded(0x10000);
        g_z80.BC = gen->bounded(0x10000);
        g_z80.DE = gen->bounded(0x10000);
        g_z80.HL = gen->bounded(0x10000);
        g_z80.SP = gen->bounded(0x10000);
        g_z80.AF_ = gen->bounded(0x10000);
        g_z80.BC_ = gen->bounded(0x10000);
        g_z80.DE_ = gen->bounded(0x10000);
        g_z80.HL_ = gen->bounded(0x10000);
        g_z80.IX = gen->bounded(0x10000);
        g_z80.IY = gen->bounded(0x10000);

        // Trigger theme change to update all layouts
        auto& tm = ThemeManager::instance();
        emit tm.themeChanged(tm.isDark());
    }

    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("QWidget { background: %1; color: %2; } "
                             "QGroupBox { border: 1px solid %3; border-radius: 4px; margin-top: 8px; padding-top: 8px; } "
                             "QGroupBox::title { subcontrol-origin: margin; left: 8px; }")
            .arg(tm.windowBg()).arg(tm.textColor()).arg(tm.borderColor()));
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    g_monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    g_monoFont.setPointSize(12);

    MainWindow w;
    w.resize(720, 520);
    w.setMinimumSize(700, 450);
    w.setMaximumSize(800, 700);
    w.show();

    return app.exec();
}

#include "main.moc"
