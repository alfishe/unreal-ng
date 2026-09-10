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
#include <QCheckBox>
#include <QComboBox>
#include <QStackedWidget>
#include <QRadioButton>
#include <QButtonGroup>

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

    QString bg() const { return m_dark ? "#1E1E1E" : "#FFFFFF"; }
    QString fg() const { return m_dark ? "#D4D4D4" : "#1E1E1E"; }
    QString dimFg() const { return m_dark ? "#808080" : "#999999"; }
    QString popupBg() const { return m_dark ? "#2D2D2D" : "#FFFFFF"; }
    QString popupBorder() const { return m_dark ? "#404040" : "#E0E0E0"; }
    QString contentBg() const { return m_dark ? "#252525" : "#F5F5F5"; }
    QString highlight() const { return m_dark ? "#264F78" : "#ADD6FF"; }
    QString pcHighlight() const { return m_dark ? "#4A3000" : "#FFEB9C"; }

    // Syntax colors
    QString addrColor() const { return m_dark ? "#569CD6" : "#0000FF"; }
    QString hexColor() const { return m_dark ? "#808080" : "#999999"; }
    QString opcodeColor() const { return m_dark ? "#C586C0" : "#AF00DB"; }
    QString registerColor() const { return m_dark ? "#9CDCFE" : "#001080"; }
    QString numberColor() const { return m_dark ? "#B5CEA8" : "#098658"; }
    QString labelColor() const { return m_dark ? "#DCDCAA" : "#795E26"; }
    QString commentColor() const { return m_dark ? "#6A9955" : "#008000"; }
    QString condColor() const { return m_dark ? "#4EC9B0" : "#267F99"; }

signals:
    void themeChanged(bool dark);

private:
    ThemeManager() : m_dark(false) {}
    bool m_dark;
};

//=============================================================================
// Instruction data
//=============================================================================
struct Instruction {
    uint16_t addr;
    uint8_t bytes[4];
    int byteCount;
    QString mnemonic;
    QString operands;
    int tStates;
    int tStatesAlt;  // for conditional branches taken
    QString flags;   // affected flags like "SZ-H-PNC"
    QString description;
    bool isCall;
    bool isJump;
    bool isRet;
    uint16_t targetAddr;
};

static QVector<Instruction> g_instructions = {
    {0x3683, {0xCB, 0x6E, 0, 0}, 2, "BIT", "5,(HL)", 12, 0, "SZ5H3P--", "Test bit 5 of memory at HL", false, false, false, 0},
    {0x3685, {0x28, 0xFC, 0, 0}, 2, "JR", "Z,#3683", 12, 7, "--------", "Jump relative if zero", false, true, false, 0x3683},
    {0x3687, {0xCB, 0xAE, 0, 0}, 2, "RES", "5,(HL)", 15, 0, "--------", "Reset bit 5 of memory at HL", false, false, false, 0},
    {0x3689, {0x3A, 0x08, 0x5C}, 3, "LD", "A,(#5C08)", 13, 0, "--------", "Load A from memory address", false, false, false, 0},
    {0x368C, {0x21, 0x41, 0x5C}, 3, "LD", "HL,#5C41", 10, 0, "--------", "Load HL with immediate value", false, false, false, 0},
    {0x368F, {0xCB, 0x86, 0, 0}, 2, "RES", "0,(HL)", 15, 0, "--------", "Reset bit 0 of memory at HL", false, false, false, 0},
    {0x3691, {0xFE, 0x20, 0, 0}, 2, "CP", "#20", 7, 0, "SZ5H3PNC", "Compare A with immediate", false, false, false, 0},
    {0x3693, {0x30, 0x0D, 0, 0}, 2, "JR", "NC,#36A2", 12, 7, "--------", "Jump relative if no carry", false, true, false, 0x36A2},
    {0x3695, {0xFE, 0x10, 0, 0}, 2, "CP", "#10", 7, 0, "SZ5H3PNC", "Compare A with immediate", false, false, false, 0},
    {0x3697, {0x30, 0xE7, 0, 0}, 2, "JR", "NC,#3680", 12, 7, "--------", "Jump relative if no carry", false, true, false, 0x3680},
    {0x3699, {0xFE, 0x06, 0, 0}, 2, "CP", "#06", 7, 0, "SZ5H3PNC", "Compare A with immediate", false, false, false, 0},
    {0x369B, {0x38, 0xE3, 0, 0}, 2, "JR", "C,#3680", 12, 7, "--------", "Jump relative if carry", false, true, false, 0x3680},
    {0x369D, {0xCD, 0xA4, 0x36}, 3, "CALL", "#36A4", 17, 0, "--------", "Call subroutine", true, false, false, 0x36A4},
    {0x36A0, {0x30, 0xDE, 0, 0}, 2, "JR", "NC,#3680", 12, 7, "--------", "Jump relative if no carry", false, true, false, 0x3680},
    {0x36A2, {0xE1, 0, 0, 0}, 1, "POP", "HL", 10, 0, "--------", "Pop HL from stack", false, false, false, 0},
    {0x36A3, {0xC9, 0, 0, 0}, 1, "RET", "", 10, 0, "--------", "Return from subroutine", false, false, true, 0},
    {0x36A4, {0xEF, 0, 0, 0}, 1, "RST", "#28", 11, 0, "--------", "Restart at #0028", true, false, false, 0x0028},
    {0x36A5, {0xDB, 0x10, 0, 0}, 2, "IN", "A,(#10)", 11, 0, "--------", "Input from port", false, false, false, 0},
    {0x36A7, {0xC9, 0, 0, 0}, 1, "RET", "", 10, 0, "--------", "Return from subroutine", false, false, true, 0},
    {0x36A8, {0xE5, 0, 0, 0}, 1, "PUSH", "HL", 11, 0, "--------", "Push HL to stack", false, false, false, 0},
    {0x36A9, {0xCD, 0x3B, 0x37}, 3, "CALL", "#373B", 17, 0, "--------", "Call subroutine", true, false, false, 0x373B},
};

static uint16_t g_currentPC = 0x3683;

//=============================================================================
// Instruction Popup
//=============================================================================
class InstructionPopup : public QFrame {
    Q_OBJECT
public:
    static InstructionPopup& instance() {
        static InstructionPopup popup;
        return popup;
    }

    void showAt(const Instruction& instr, const QPoint& pos) {
        auto& tm = ThemeManager::instance();

        QString tagBg = tm.isDark() ? "#3C3C3C" : "#E8E8E8";

        // Title
        QString fullInstr = instr.operands.isEmpty()
            ? instr.mnemonic
            : QString("%1 %2").arg(instr.mnemonic).arg(instr.operands);
        m_titleLabel->setText(fullInstr);
        m_titleLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(tm.opcodeColor()));

        // Type tag
        QString typeText;
        QString typeColor;
        if (instr.isCall) {
            typeText = "CALL";
            typeColor = "#E06C75";
        } else if (instr.isJump) {
            typeText = "JUMP";
            typeColor = "#61AFEF";
        } else if (instr.isRet) {
            typeText = "RET";
            typeColor = "#98C379";
        } else {
            typeText = "OP";
            typeColor = tm.dimFg();
        }
        m_typeTag->setText(typeText);
        m_typeTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tagBg).arg(typeColor));

        // Address and bytes
        QString hexBytes;
        for (int i = 0; i < instr.byteCount; i++) {
            if (i > 0) hexBytes += " ";
            hexBytes += QString("%1").arg(instr.bytes[i], 2, 16, QChar('0')).toUpper();
        }
        m_addrLabel->setText(QString("Address: $%1").arg(instr.addr, 4, 16, QChar('0')).toUpper());
        m_bytesLabel->setText(QString("Bytes: %1").arg(hexBytes));

        // T-states
        QString tStr;
        if (instr.tStatesAlt > 0) {
            tStr = QString("T-states: %1 / %2").arg(instr.tStates).arg(instr.tStatesAlt);
        } else {
            tStr = QString("T-states: %1").arg(instr.tStates);
        }
        m_tstatesLabel->setText(tStr);

        // Flags
        m_flagsLabel->setText(QString("Flags: %1").arg(instr.flags));

        // Description
        m_descLabel->setText(instr.description);
        m_descLabel->setStyleSheet(QString("color: %1; font-style: italic;").arg(tm.dimFg()));

        // Target address for jumps/calls
        if (instr.isCall || instr.isJump) {
            m_targetLabel->setText(QString("Target: $%1").arg(instr.targetAddr, 4, 16, QChar('0')).toUpper());
            m_targetLabel->show();
        } else {
            m_targetLabel->hide();
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
    InstructionPopup() : QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint) {
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

        // Details box
        m_detailsBox = new QFrame();
        m_detailsBox->setFrameStyle(QFrame::NoFrame);
        auto* detailsLayout = new QVBoxLayout(m_detailsBox);
        detailsLayout->setContentsMargins(8, 6, 8, 6);
        detailsLayout->setSpacing(2);

        m_addrLabel = new QLabel();
        m_addrLabel->setFont(g_monoFont);
        detailsLayout->addWidget(m_addrLabel);

        m_bytesLabel = new QLabel();
        m_bytesLabel->setFont(g_monoFont);
        detailsLayout->addWidget(m_bytesLabel);

        m_tstatesLabel = new QLabel();
        m_tstatesLabel->setFont(g_monoFont);
        detailsLayout->addWidget(m_tstatesLabel);

        m_flagsLabel = new QLabel();
        m_flagsLabel->setFont(g_monoFont);
        detailsLayout->addWidget(m_flagsLabel);

        m_targetLabel = new QLabel();
        m_targetLabel->setFont(g_monoFont);
        detailsLayout->addWidget(m_targetLabel);

        layout->addWidget(m_detailsBox);

        // Description
        m_descLabel = new QLabel();
        m_descLabel->setFont(g_monoFont);
        m_descLabel->setWordWrap(true);
        layout->addWidget(m_descLabel);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &InstructionPopup::applyTheme);
        applyTheme();
    }

    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString(
            "InstructionPopup { background: %1; border: 1px solid %2; border-radius: 8px; }"
        ).arg(tm.popupBg()).arg(tm.popupBorder()));
        m_detailsBox->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(tm.contentBg()));
    }

    QLabel* m_titleLabel;
    QLabel* m_typeTag;
    QFrame* m_detailsBox;
    QLabel* m_addrLabel;
    QLabel* m_bytesLabel;
    QLabel* m_tstatesLabel;
    QLabel* m_flagsLabel;
    QLabel* m_targetLabel;
    QLabel* m_descLabel;
};

//=============================================================================
// Instruction Row Widget (for layouts that need hover)
//=============================================================================
class InstructionRow : public QFrame {
    Q_OBJECT
public:
    InstructionRow(const Instruction& instr, QWidget* parent = nullptr)
        : QFrame(parent), m_instr(instr) {
        setMouseTracking(true);
        m_hoverTimer.setSingleShot(true);
        m_hoverTimer.setInterval(400);
        connect(&m_hoverTimer, &QTimer::timeout, this, &InstructionRow::showPopup);
    }

    void setCurrentPC(bool isCurrent) {
        m_isCurrent = isCurrent;
        update();
    }

signals:
    void hidePopup();

protected:
    void enterEvent(QEnterEvent*) override {
        emit hidePopup();
        m_hoverTimer.start();
    }

    void leaveEvent(QEvent*) override {
        m_hoverTimer.stop();
        emit hidePopup();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        m_mousePos = mapToGlobal(e->pos());
    }

    void showPopup() {
        InstructionPopup::instance().showAt(m_instr, m_mousePos + QPoint(15, 15));
    }

    Instruction m_instr;
    bool m_isCurrent = false;
    QTimer m_hoverTimer;
    QPoint m_mousePos;
};

//=============================================================================
// Layout 1: Basic - Address | Hex | Mnemonic Operands
//=============================================================================
class Layout1 : public QGroupBox {
    Q_OBJECT
public:
    Layout1(QWidget* parent = nullptr) : QGroupBox("Layout 1: Basic Columns", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);
        layout->setContentsMargins(8, 8, 8, 8);

        for (const auto& instr : g_instructions) {
            auto* row = new InstructionRow(instr);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 2, 4, 2);
            rowLayout->setSpacing(12);

            auto& tm = ThemeManager::instance();

            // Address
            auto* addrLabel = new QLabel(QString("%1").arg(instr.addr, 4, 16, QChar('0')).toUpper());
            addrLabel->setFont(g_monoFont);
            addrLabel->setFixedWidth(50);
            addrLabel->setStyleSheet(QString("color: %1;").arg(tm.addrColor()));
            rowLayout->addWidget(addrLabel);

            // Hex bytes
            QString hexStr;
            for (int i = 0; i < instr.byteCount; i++) {
                hexStr += QString("%1").arg(instr.bytes[i], 2, 16, QChar('0')).toUpper();
            }
            auto* hexLabel = new QLabel(hexStr);
            hexLabel->setFont(g_monoFont);
            hexLabel->setFixedWidth(80);
            hexLabel->setStyleSheet(QString("color: %1;").arg(tm.hexColor()));
            rowLayout->addWidget(hexLabel);

            // Mnemonic
            auto* mnemoLabel = new QLabel(instr.mnemonic);
            mnemoLabel->setFont(g_monoFont);
            mnemoLabel->setFixedWidth(50);
            mnemoLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(tm.opcodeColor()));
            rowLayout->addWidget(mnemoLabel);

            // Operands
            auto* operLabel = new QLabel(formatOperands(instr.operands));
            operLabel->setFont(g_monoFont);
            operLabel->setTextFormat(Qt::RichText);
            rowLayout->addWidget(operLabel);

            rowLayout->addStretch();

            if (instr.addr == g_currentPC) {
                row->setStyleSheet(QString("background: %1;").arg(tm.pcHighlight()));
            }

            connect(row, &InstructionRow::hidePopup, this, []() {
                InstructionPopup::instance().hide();
            });

            layout->addWidget(row);
        }

        layout->addStretch();
    }

private:
    QString formatOperands(const QString& op) {
        auto& tm = ThemeManager::instance();
        QString result = op;
        // Highlight registers
        QStringList regs = {"A", "B", "C", "D", "E", "H", "L", "AF", "BC", "DE", "HL", "SP", "IX", "IY", "PC"};
        for (const auto& r : regs) {
            result.replace(QRegularExpression(QString("\\b%1\\b").arg(r)),
                QString("<span style='color:%1;'>%2</span>").arg(tm.registerColor()).arg(r));
        }
        // Highlight numbers
        result.replace(QRegularExpression("#([0-9A-Fa-f]+)"),
            QString("<span style='color:%1;'>#\\1</span>").arg(tm.numberColor()));
        return result;
    }
};

//=============================================================================
// Layout 2: Compact Single Line
//=============================================================================
class Layout2 : public QGroupBox {
    Q_OBJECT
public:
    Layout2(QWidget* parent = nullptr) : QGroupBox("Layout 2: Compact", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);

        for (const auto& instr : g_instructions) {
            auto* row = new InstructionRow(instr);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 1, 4, 1);
            rowLayout->setSpacing(8);

            auto& tm = ThemeManager::instance();

            QString line = QString("<span style='color:%1;'>%2</span> <span style='color:%3;font-weight:bold;'>%4</span> <span style='color:%5;'>%6</span>")
                .arg(tm.addrColor())
                .arg(instr.addr, 4, 16, QChar('0')).toUpper()
                .arg(tm.opcodeColor())
                .arg(instr.mnemonic)
                .arg(tm.fg())
                .arg(instr.operands);

            auto* label = new QLabel(line);
            label->setFont(g_monoFont);
            label->setTextFormat(Qt::RichText);
            rowLayout->addWidget(label);
            rowLayout->addStretch();

            if (instr.addr == g_currentPC) {
                row->setStyleSheet(QString("background: %1;").arg(tm.pcHighlight()));
            }

            connect(row, &InstructionRow::hidePopup, this, []() {
                InstructionPopup::instance().hide();
            });

            layout->addWidget(row);
        }
        layout->addStretch();
    }
};

//=============================================================================
// Layout 3: With T-States Column
//=============================================================================
class Layout3 : public QGroupBox {
    Q_OBJECT
public:
    Layout3(QWidget* parent = nullptr) : QGroupBox("Layout 3: With T-States", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);

        for (const auto& instr : g_instructions) {
            auto* row = new InstructionRow(instr);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 2, 4, 2);
            rowLayout->setSpacing(12);

            auto& tm = ThemeManager::instance();

            // Address
            auto* addrLabel = new QLabel(QString("%1").arg(instr.addr, 4, 16, QChar('0')).toUpper());
            addrLabel->setFont(g_monoFont);
            addrLabel->setFixedWidth(50);
            addrLabel->setStyleSheet(QString("color: %1;").arg(tm.addrColor()));
            rowLayout->addWidget(addrLabel);

            // Mnemonic + Operands
            QString mnemo = QString("%1 %2").arg(instr.mnemonic, -5).arg(instr.operands);
            auto* mnemoLabel = new QLabel(mnemo);
            mnemoLabel->setFont(g_monoFont);
            mnemoLabel->setFixedWidth(150);
            mnemoLabel->setStyleSheet(QString("color: %1;").arg(tm.opcodeColor()));
            rowLayout->addWidget(mnemoLabel);

            // T-states
            QString tStr = instr.tStatesAlt > 0
                ? QString("%1/%2").arg(instr.tStates).arg(instr.tStatesAlt)
                : QString::number(instr.tStates);
            auto* tLabel = new QLabel(tStr);
            tLabel->setFont(g_monoFont);
            tLabel->setFixedWidth(40);
            tLabel->setAlignment(Qt::AlignRight);
            tLabel->setStyleSheet(QString("color: %1;").arg(tm.numberColor()));
            rowLayout->addWidget(tLabel);

            rowLayout->addStretch();

            if (instr.addr == g_currentPC) {
                row->setStyleSheet(QString("background: %1;").arg(tm.pcHighlight()));
            }

            connect(row, &InstructionRow::hidePopup, this, []() {
                InstructionPopup::instance().hide();
            });

            layout->addWidget(row);
        }
        layout->addStretch();
    }
};

//=============================================================================
// Layout 4: With Annotations/Comments
//=============================================================================
class Layout4 : public QGroupBox {
    Q_OBJECT
public:
    Layout4(QWidget* parent = nullptr) : QGroupBox("Layout 4: With Comments", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);

        for (const auto& instr : g_instructions) {
            auto* row = new InstructionRow(instr);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 2, 4, 2);
            rowLayout->setSpacing(12);

            auto& tm = ThemeManager::instance();

            // Address
            auto* addrLabel = new QLabel(QString("%1").arg(instr.addr, 4, 16, QChar('0')).toUpper());
            addrLabel->setFont(g_monoFont);
            addrLabel->setFixedWidth(50);
            addrLabel->setStyleSheet(QString("color: %1;").arg(tm.addrColor()));
            rowLayout->addWidget(addrLabel);

            // Mnemonic + Operands
            QString mnemo = QString("%1 %2").arg(instr.mnemonic, -5).arg(instr.operands);
            auto* mnemoLabel = new QLabel(mnemo);
            mnemoLabel->setFont(g_monoFont);
            mnemoLabel->setFixedWidth(150);
            mnemoLabel->setStyleSheet(QString("color: %1;").arg(tm.opcodeColor()));
            rowLayout->addWidget(mnemoLabel);

            // Comment/Description
            auto* commentLabel = new QLabel(QString("; %1").arg(instr.description));
            commentLabel->setFont(g_monoFont);
            commentLabel->setStyleSheet(QString("color: %1;").arg(tm.commentColor()));
            rowLayout->addWidget(commentLabel);

            rowLayout->addStretch();

            if (instr.addr == g_currentPC) {
                row->setStyleSheet(QString("background: %1;").arg(tm.pcHighlight()));
            }

            connect(row, &InstructionRow::hidePopup, this, []() {
                InstructionPopup::instance().hide();
            });

            layout->addWidget(row);
        }
        layout->addStretch();
    }
};

//=============================================================================
// Layout 5: With Flags Column
//=============================================================================
class Layout5 : public QGroupBox {
    Q_OBJECT
public:
    Layout5(QWidget* parent = nullptr) : QGroupBox("Layout 5: With Flags Affected", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);

        for (const auto& instr : g_instructions) {
            auto* row = new InstructionRow(instr);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 2, 4, 2);
            rowLayout->setSpacing(12);

            auto& tm = ThemeManager::instance();

            // Address
            auto* addrLabel = new QLabel(QString("%1").arg(instr.addr, 4, 16, QChar('0')).toUpper());
            addrLabel->setFont(g_monoFont);
            addrLabel->setFixedWidth(50);
            addrLabel->setStyleSheet(QString("color: %1;").arg(tm.addrColor()));
            rowLayout->addWidget(addrLabel);

            // Mnemonic + Operands
            QString mnemo = QString("%1 %2").arg(instr.mnemonic, -5).arg(instr.operands);
            auto* mnemoLabel = new QLabel(mnemo);
            mnemoLabel->setFont(g_monoFont);
            mnemoLabel->setFixedWidth(150);
            mnemoLabel->setStyleSheet(QString("color: %1;").arg(tm.opcodeColor()));
            rowLayout->addWidget(mnemoLabel);

            // Flags
            QString flagsColored = formatFlags(instr.flags);
            auto* flagsLabel = new QLabel(flagsColored);
            flagsLabel->setFont(g_monoFont);
            flagsLabel->setTextFormat(Qt::RichText);
            flagsLabel->setFixedWidth(100);
            rowLayout->addWidget(flagsLabel);

            rowLayout->addStretch();

            if (instr.addr == g_currentPC) {
                row->setStyleSheet(QString("background: %1;").arg(tm.pcHighlight()));
            }

            connect(row, &InstructionRow::hidePopup, this, []() {
                InstructionPopup::instance().hide();
            });

            layout->addWidget(row);
        }
        layout->addStretch();
    }

private:
    QString formatFlags(const QString& flags) {
        auto& tm = ThemeManager::instance();
        QString result;
        for (QChar c : flags) {
            if (c == '-') {
                result += QString("<span style='color:%1;'>-</span>").arg(tm.dimFg());
            } else {
                result += QString("<span style='color:%1;font-weight:bold;'>%2</span>").arg(tm.condColor()).arg(c);
            }
        }
        return result;
    }
};

//=============================================================================
// Layout 6: Code Flow View (showing branch arrows)
//=============================================================================
class Layout6 : public QGroupBox {
    Q_OBJECT
public:
    Layout6(QWidget* parent = nullptr) : QGroupBox("Layout 6: Flow Markers", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);

        for (const auto& instr : g_instructions) {
            auto* row = new InstructionRow(instr);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 2, 4, 2);
            rowLayout->setSpacing(8);

            auto& tm = ThemeManager::instance();

            // Flow marker
            QString marker = "  ";
            QString markerColor = tm.dimFg();
            if (instr.isCall) {
                marker = "→";
                markerColor = "#E06C75";
            } else if (instr.isJump) {
                marker = "↗";
                markerColor = "#61AFEF";
            } else if (instr.isRet) {
                marker = "←";
                markerColor = "#98C379";
            }
            auto* markerLabel = new QLabel(marker);
            markerLabel->setFont(g_monoFont);
            markerLabel->setFixedWidth(20);
            markerLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(markerColor));
            rowLayout->addWidget(markerLabel);

            // Address
            auto* addrLabel = new QLabel(QString("%1").arg(instr.addr, 4, 16, QChar('0')).toUpper());
            addrLabel->setFont(g_monoFont);
            addrLabel->setFixedWidth(50);
            addrLabel->setStyleSheet(QString("color: %1;").arg(tm.addrColor()));
            rowLayout->addWidget(addrLabel);

            // Mnemonic + Operands
            QString mnemo = QString("%1 %2").arg(instr.mnemonic, -5).arg(instr.operands);
            auto* mnemoLabel = new QLabel(mnemo);
            mnemoLabel->setFont(g_monoFont);
            mnemoLabel->setStyleSheet(QString("color: %1;").arg(tm.opcodeColor()));
            rowLayout->addWidget(mnemoLabel);

            rowLayout->addStretch();

            if (instr.addr == g_currentPC) {
                row->setStyleSheet(QString("background: %1;").arg(tm.pcHighlight()));
            }

            connect(row, &InstructionRow::hidePopup, this, []() {
                InstructionPopup::instance().hide();
            });

            layout->addWidget(row);
        }
        layout->addStretch();
    }
};

//=============================================================================
// Layout 7: Grouped by Subroutines (visual separator)
//=============================================================================
class Layout7 : public QGroupBox {
    Q_OBJECT
public:
    Layout7(QWidget* parent = nullptr) : QGroupBox("Layout 7: Subroutine Blocks", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);

        bool inSubroutine = false;
        QFrame* currentBlock = nullptr;
        QVBoxLayout* blockLayout = nullptr;

        for (int i = 0; i < g_instructions.size(); i++) {
            const auto& instr = g_instructions[i];
            auto& tm = ThemeManager::instance();

            // Start new block at CALL targets or after RET
            bool startNewBlock = (i == 0) || (i > 0 && g_instructions[i-1].isRet);

            if (startNewBlock && currentBlock) {
                layout->addWidget(currentBlock);
                layout->addSpacing(8);
            }

            if (startNewBlock || !currentBlock) {
                currentBlock = new QFrame();
                currentBlock->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(tm.contentBg()));
                blockLayout = new QVBoxLayout(currentBlock);
                blockLayout->setContentsMargins(4, 4, 4, 4);
                blockLayout->setSpacing(0);

                // Subroutine label
                auto* subLabel = new QLabel(QString("sub_%1:").arg(instr.addr, 4, 16, QChar('0')).toUpper());
                subLabel->setFont(g_monoFont);
                subLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(tm.labelColor()));
                blockLayout->addWidget(subLabel);
            }

            auto* row = new InstructionRow(instr);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(12, 1, 4, 1);
            rowLayout->setSpacing(12);

            // Address
            auto* addrLabel = new QLabel(QString("%1").arg(instr.addr, 4, 16, QChar('0')).toUpper());
            addrLabel->setFont(g_monoFont);
            addrLabel->setFixedWidth(50);
            addrLabel->setStyleSheet(QString("color: %1;").arg(tm.addrColor()));
            rowLayout->addWidget(addrLabel);

            // Mnemonic + Operands
            QString mnemo = QString("%1 %2").arg(instr.mnemonic, -5).arg(instr.operands);
            auto* mnemoLabel = new QLabel(mnemo);
            mnemoLabel->setFont(g_monoFont);
            mnemoLabel->setStyleSheet(QString("color: %1;").arg(tm.opcodeColor()));
            rowLayout->addWidget(mnemoLabel);

            rowLayout->addStretch();

            if (instr.addr == g_currentPC) {
                row->setStyleSheet(QString("background: %1;").arg(tm.pcHighlight()));
            }

            connect(row, &InstructionRow::hidePopup, this, []() {
                InstructionPopup::instance().hide();
            });

            blockLayout->addWidget(row);
        }

        if (currentBlock) {
            layout->addWidget(currentBlock);
        }

        layout->addStretch();
    }
};

//=============================================================================
// Layout 8: With Breakpoint Markers
//=============================================================================
class Layout8 : public QGroupBox {
    Q_OBJECT
public:
    Layout8(QWidget* parent = nullptr) : QGroupBox("Layout 8: Breakpoint Gutter", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);

        // Demo breakpoints
        QSet<uint16_t> breakpoints = {0x3687, 0x369D, 0x36A4};

        for (const auto& instr : g_instructions) {
            auto* row = new InstructionRow(instr);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 2, 4, 2);
            rowLayout->setSpacing(8);

            auto& tm = ThemeManager::instance();

            // Breakpoint marker
            auto* bpLabel = new QLabel(breakpoints.contains(instr.addr) ? "●" : " ");
            bpLabel->setFont(g_monoFont);
            bpLabel->setFixedWidth(16);
            bpLabel->setStyleSheet("color: #E06C75; font-weight: bold;");
            rowLayout->addWidget(bpLabel);

            // PC marker
            auto* pcLabel = new QLabel(instr.addr == g_currentPC ? "▶" : " ");
            pcLabel->setFont(g_monoFont);
            pcLabel->setFixedWidth(16);
            pcLabel->setStyleSheet(QString("color: %1;").arg(tm.numberColor()));
            rowLayout->addWidget(pcLabel);

            // Address
            auto* addrLabel = new QLabel(QString("%1").arg(instr.addr, 4, 16, QChar('0')).toUpper());
            addrLabel->setFont(g_monoFont);
            addrLabel->setFixedWidth(50);
            addrLabel->setStyleSheet(QString("color: %1;").arg(tm.addrColor()));
            rowLayout->addWidget(addrLabel);

            // Mnemonic + Operands
            QString mnemo = QString("%1 %2").arg(instr.mnemonic, -5).arg(instr.operands);
            auto* mnemoLabel = new QLabel(mnemo);
            mnemoLabel->setFont(g_monoFont);
            mnemoLabel->setStyleSheet(QString("color: %1;").arg(tm.opcodeColor()));
            rowLayout->addWidget(mnemoLabel);

            rowLayout->addStretch();

            connect(row, &InstructionRow::hidePopup, this, []() {
                InstructionPopup::instance().hide();
            });

            layout->addWidget(row);
        }
        layout->addStretch();
    }
};

//=============================================================================
// Layout 9: With Memory Preview Column
//=============================================================================
class Layout9 : public QGroupBox {
    Q_OBJECT
public:
    Layout9(QWidget* parent = nullptr) : QGroupBox("Layout 9: Memory Context", parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);

        for (const auto& instr : g_instructions) {
            auto* row = new InstructionRow(instr);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 2, 4, 2);
            rowLayout->setSpacing(12);

            auto& tm = ThemeManager::instance();

            // Address
            auto* addrLabel = new QLabel(QString("%1").arg(instr.addr, 4, 16, QChar('0')).toUpper());
            addrLabel->setFont(g_monoFont);
            addrLabel->setFixedWidth(50);
            addrLabel->setStyleSheet(QString("color: %1;").arg(tm.addrColor()));
            rowLayout->addWidget(addrLabel);

            // Mnemonic + Operands
            QString mnemo = QString("%1 %2").arg(instr.mnemonic, -5).arg(instr.operands);
            auto* mnemoLabel = new QLabel(mnemo);
            mnemoLabel->setFont(g_monoFont);
            mnemoLabel->setFixedWidth(150);
            mnemoLabel->setStyleSheet(QString("color: %1;").arg(tm.opcodeColor()));
            rowLayout->addWidget(mnemoLabel);

            // Memory preview (fake data for demo)
            QString memPreview;
            if (instr.operands.contains("(HL)") || instr.operands.contains("(#")) {
                memPreview = QString("[%1 %2 %3 %4]")
                    .arg((instr.addr * 17) & 0xFF, 2, 16, QChar('0'))
                    .arg((instr.addr * 23) & 0xFF, 2, 16, QChar('0'))
                    .arg((instr.addr * 31) & 0xFF, 2, 16, QChar('0'))
                    .arg((instr.addr * 37) & 0xFF, 2, 16, QChar('0')).toUpper();
            }
            auto* memLabel = new QLabel(memPreview);
            memLabel->setFont(g_monoFont);
            memLabel->setStyleSheet(QString("color: %1;").arg(tm.dimFg()));
            rowLayout->addWidget(memLabel);

            rowLayout->addStretch();

            if (instr.addr == g_currentPC) {
                row->setStyleSheet(QString("background: %1;").arg(tm.pcHighlight()));
            }

            connect(row, &InstructionRow::hidePopup, this, []() {
                InstructionPopup::instance().hide();
            });

            layout->addWidget(row);
        }
        layout->addStretch();
    }
};

//=============================================================================
// Main Window
//=============================================================================
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow() {
        setWindowTitle("Disassembly View POC");

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
        connect(themeGroup, &QButtonGroup::idClicked, this, [this](int id) {
            ThemeManager::instance().setDark(id == 1);
            applyTheme();
        });
        topBar->addWidget(lightRadio);
        topBar->addWidget(darkRadio);

        auto* layoutCombo = new QComboBox();
        for (int i = 1; i <= 9; i++) {
            layoutCombo->addItem(QString("Layout %1").arg(i));
        }
        topBar->addWidget(layoutCombo);
        topBar->addStretch();
        mainLayout->addLayout(topBar);

        // Stacked widget for layouts
        m_stack = new QStackedWidget();
        m_stack->addWidget(new Layout1());
        m_stack->addWidget(new Layout2());
        m_stack->addWidget(new Layout3());
        m_stack->addWidget(new Layout4());
        m_stack->addWidget(new Layout5());
        m_stack->addWidget(new Layout6());
        m_stack->addWidget(new Layout7());
        m_stack->addWidget(new Layout8());
        m_stack->addWidget(new Layout9());

        auto* scroll = new QScrollArea();
        scroll->setWidget(m_stack);
        scroll->setWidgetResizable(true);
        mainLayout->addWidget(scroll);

        connect(layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), m_stack, &QStackedWidget::setCurrentIndex);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &MainWindow::applyTheme);
        applyTheme();
    }

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("QWidget { background: %1; color: %2; }").arg(tm.bg()).arg(tm.fg()));
    }

    QStackedWidget* m_stack;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    g_monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    g_monoFont.setPointSize(12);

    MainWindow w;
    w.resize(700, 600);
    w.show();

    return app.exec();
}

#include "main.moc"
