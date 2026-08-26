#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QFrame>
#include <QTableWidget>
#include <QHeaderView>
#include <QMouseEvent>
#include <QTimer>
#include <QFontDatabase>
#include <QScreen>
#include <QScrollArea>
#include <QListWidget>
#include <QTreeWidget>
#include <QRadioButton>
#include <QButtonGroup>
#include <QTabWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextEdit>
#include <QToolBox>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QComboBox>

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

    // Breakpoint type colors
    QString fetchColor() const { return m_isDark ? "#569CD6" : "#0000FF"; }
    QString readColor() const { return m_isDark ? "#4EC9B0" : "#267F99"; }
    QString writeColor() const { return m_isDark ? "#CE9178" : "#A31515"; }
    QString activeColor() const { return m_isDark ? "#4CAF50" : "#2E7D32"; }
    QString disabledColor() const { return m_isDark ? "#666666" : "#999999"; }
    QString hitColor() const { return m_isDark ? "#DCDCAA" : "#795E26"; }
    QString condColor() const { return m_isDark ? "#C586C0" : "#AF00DB"; }

signals:
    void themeChanged(bool isDark);

private:
    ThemeManager() : m_isDark(false) {}
    bool m_isDark = false;
};

//=============================================================================
// Breakpoint data model
//=============================================================================
struct Breakpoint {
    bool enabled;
    bool onFetch;
    bool onRead;
    bool onWrite;
    uint16_t address;
    QString condition;
    int hitCount;
    int hitLimit;
    QString label;
};

static Breakpoint g_breakpoints[] = {
    {true,  true,  false, false, 0x3683, "", 0, 0, "main_loop"},
    {true,  false, true,  true,  0x5B66, "A == #FF", 12, 0, "screen_attr"},
    {false, true,  false, false, 0x265C, "", 0, 0, "keyboard_scan"},
    {true,  false, false, true,  0xFF00, "HL > #4000", 5, 10, "port_write"},
    {true,  true,  true,  true,  0x0000, "", 142, 0, "rom_entry"},
    {true,  false, true,  false, 0x0223, "C != 0", 3, 0, "tape_load"},
};
static const int g_numBreakpoints = sizeof(g_breakpoints) / sizeof(g_breakpoints[0]);

//=============================================================================
// Breakpoint Popup
//=============================================================================
class BreakpointPopup : public QFrame {
    Q_OBJECT
public:
    static BreakpointPopup& instance() {
        static BreakpointPopup popup;
        return popup;
    }

    void showAt(const Breakpoint& bp, const QPoint& pos) {
        auto& tm = ThemeManager::instance();

        m_titleLabel->setText(QString("$%1").arg(bp.address, 4, 16, QChar('0')).toUpper());
        if (!bp.label.isEmpty()) {
            m_labelTag->setText(bp.label);
            m_labelTag->show();
        } else {
            m_labelTag->hide();
        }

        // Type tags
        QString tagBg = tm.tagBg();
        m_fetchTag->setVisible(bp.onFetch);
        m_readTag->setVisible(bp.onRead);
        m_writeTag->setVisible(bp.onWrite);

        m_fetchTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tagBg).arg(tm.fetchColor()));
        m_readTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tagBg).arg(tm.readColor()));
        m_writeTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tagBg).arg(tm.writeColor()));

        // Status
        m_statusLabel->setText(bp.enabled ? "Active" : "Disabled");
        m_statusLabel->setStyleSheet(QString("color: %1; font-weight: bold;")
            .arg(bp.enabled ? tm.activeColor() : tm.disabledColor()));

        // Details
        QString details;
        if (!bp.condition.isEmpty()) {
            details += QString("Condition: %1\n").arg(bp.condition);
        }
        details += QString("Hits:      %1").arg(bp.hitCount);
        if (bp.hitLimit > 0) {
            details += QString(" / %1").arg(bp.hitLimit);
        }
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
    BreakpointPopup() : QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint) {
        setFrameStyle(QFrame::NoFrame);
        setAttribute(Qt::WA_ShowWithoutActivating);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(6);

        // Header row
        auto* headerRow = new QHBoxLayout();
        m_titleLabel = new QLabel();
        m_titleLabel->setFont(g_monoFont);
        m_titleLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
        headerRow->addWidget(m_titleLabel);
        m_labelTag = new QLabel();
        headerRow->addWidget(m_labelTag);
        headerRow->addStretch();
        layout->addLayout(headerRow);

        // Type tags row
        auto* tagsRow = new QHBoxLayout();
        m_fetchTag = new QLabel("Fetch");
        m_readTag = new QLabel("Read");
        m_writeTag = new QLabel("Write");
        tagsRow->addWidget(m_fetchTag);
        tagsRow->addWidget(m_readTag);
        tagsRow->addWidget(m_writeTag);
        tagsRow->addStretch();
        layout->addLayout(tagsRow);

        // Status
        m_statusLabel = new QLabel();
        layout->addWidget(m_statusLabel);

        // Details box
        m_detailsBox = new QFrame();
        m_detailsBox->setFrameStyle(QFrame::NoFrame);
        auto* detailsLayout = new QVBoxLayout(m_detailsBox);
        detailsLayout->setContentsMargins(8, 6, 8, 6);
        m_details = new QLabel();
        m_details->setFont(g_monoFont);
        detailsLayout->addWidget(m_details);
        layout->addWidget(m_detailsBox);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &BreakpointPopup::applyTheme);
        applyTheme();
    }

    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString(
            "BreakpointPopup { background: %1; border: 1px solid %2; border-radius: 8px; }"
        ).arg(tm.popupBg()).arg(tm.popupBorder()));
        m_labelTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px;"
        ).arg(tm.tagBg()).arg(tm.condColor()));
        m_detailsBox->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(tm.contentBg()));
    }

    QLabel* m_titleLabel;
    QLabel* m_labelTag;
    QLabel* m_fetchTag;
    QLabel* m_readTag;
    QLabel* m_writeTag;
    QLabel* m_statusLabel;
    QFrame* m_detailsBox;
    QLabel* m_details;
};

//=============================================================================
// Condition expression syntax highlighter
//=============================================================================
class ConditionHighlighter : public QSyntaxHighlighter {
public:
    ConditionHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {
        // Registers - blue
        m_registerFormat.setForeground(QColor("#569CD6"));
        m_registerFormat.setFontWeight(QFont::Bold);

        // Flags - cyan
        m_flagFormat.setForeground(QColor("#4EC9B0"));

        // Numbers - green
        m_numberFormat.setForeground(QColor("#B5CEA8"));

        // Operators - yellow
        m_operatorFormat.setForeground(QColor("#DCDCAA"));

        // Memory/Bus - purple
        m_memoryFormat.setForeground(QColor("#C586C0"));

        // Paging - orange
        m_pagingFormat.setForeground(QColor("#CE9178"));

        // Error - red underline
        m_errorFormat.setUnderlineColor(Qt::red);
        m_errorFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    }

protected:
    void highlightBlock(const QString& text) override {
        // Registers
        static QRegularExpression regs(R"(\b(A|F|B|C|D|E|H|L|AF|BC|DE|HL|IX|IY|IXH|IXL|IYH|IYL|SP|PC|I|R|IM|A'|F'|B'|C'|D'|E'|H'|L'|AF'|BC'|DE'|HL')\b)", QRegularExpression::CaseInsensitiveOption);
        for (auto it = regs.globalMatch(text); it.hasNext(); ) {
            auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), m_registerFormat);
        }

        // Flags
        static QRegularExpression flags(R"(\b(Z|NZ|CY|NC|PE|PO|FP|FM|FS|FN|FH|IFF1|IFF2|HALTED)\b)", QRegularExpression::CaseInsensitiveOption);
        for (auto it = flags.globalMatch(text); it.hasNext(); ) {
            auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), m_flagFormat);
        }

        // Memory/Bus access
        static QRegularExpression mem(R"(\b(MRA|MWA|MRV|MWV|PRA|PWA|PRV|PWV|ADDR|VAL|PORT)\b)", QRegularExpression::CaseInsensitiveOption);
        for (auto it = mem.globalMatch(text); it.hasNext(); ) {
            auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), m_memoryFormat);
        }

        // Paging
        static QRegularExpression paging(R"(\b(PG0|PG1|PG2|PG3|ROMPG|DOS|SHADOW|T|FRAME|DT)\b)", QRegularExpression::CaseInsensitiveOption);
        for (auto it = paging.globalMatch(text); it.hasNext(); ) {
            auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), m_pagingFormat);
        }

        // Numbers (hex and decimal)
        static QRegularExpression nums(R"(\b(\$[0-9A-Fa-f]+|#[0-9A-Fa-f]+|0x[0-9A-Fa-f]+|[0-9A-Fa-f]+h|\d+)\b)");
        for (auto it = nums.globalMatch(text); it.hasNext(); ) {
            auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), m_numberFormat);
        }

        // Operators
        static QRegularExpression ops(R"(==|!=|<>|<=|>=|&&|\|\||<<|>>|[+\-*/%&|^~!<>])");
        for (auto it = ops.globalMatch(text); it.hasNext(); ) {
            auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), m_operatorFormat);
        }
    }

private:
    QTextCharFormat m_registerFormat;
    QTextCharFormat m_flagFormat;
    QTextCharFormat m_numberFormat;
    QTextCharFormat m_operatorFormat;
    QTextCharFormat m_memoryFormat;
    QTextCharFormat m_pagingFormat;
    QTextCharFormat m_errorFormat;
};

//=============================================================================
// Clickable slot widget for visual builder
//=============================================================================
class ClickableSlot : public QPushButton {
    Q_OBJECT
public:
    enum SlotType { Operand, Operator };

    ClickableSlot(SlotType type, QWidget* parent = nullptr)
        : QPushButton(parent), m_type(type), m_value("") {
        setFont(g_monoFont);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(24);
        updateDisplay();
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &ClickableSlot::updateDisplay);
        connect(this, &QPushButton::clicked, this, &ClickableSlot::onClicked);
    }

    void setValue(const QString& val) { m_value = val; updateDisplay(); emit valueChanged(); }
    void clear() { m_value.clear(); updateDisplay(); emit valueChanged(); }
    QString value() const { return m_value; }
    bool isEmpty() const { return m_value.isEmpty(); }

signals:
    void valueChanged();
    void slotClicked();

private slots:
    void onClicked() {
        if (!m_value.isEmpty()) {
            clear();
        } else {
            emit slotClicked();
        }
    }

    void updateDisplay() {
        auto& tm = ThemeManager::instance();
        if (m_value.isEmpty()) {
            setText(m_type == Operand ? "value" : "op");
            setToolTip("");
            QString bg = tm.isDark() ? "#2D2D2D" : "#E8E8E8";
            QString fg = tm.isDark() ? "#888888" : "#666666";
            QString border = tm.isDark() ? "#666666" : "#AAAAAA";
            setStyleSheet(QString("QPushButton { background: %1; color: %2; border: 1px dashed %3; "
                "border-radius: 10px; padding: 2px 8px; font-style: italic; }"
                "QPushButton:hover { border-color: %2; background: %4; }")
                .arg(bg).arg(fg).arg(border).arg(tm.isDark() ? "#3C3C3C" : "#D8D8D8"));
        } else {
            setText(m_value + " ×");
            setToolTip("Click to clear");
            QString color = m_type == Operand ? "#569CD6" : "#DCDCAA";
            QString bg = m_type == Operand
                ? (tm.isDark() ? "#264F78" : "#D0E4F7")
                : (tm.isDark() ? "#4D4D26" : "#F5F5D0");
            QString fgFilled = m_type == Operand
                ? (tm.isDark() ? "#9CDCFE" : "#0055AA")
                : (tm.isDark() ? "#DCDCAA" : "#806000");
            setStyleSheet(QString("QPushButton { background: %1; color: %2; border: none; "
                "border-radius: 10px; padding: 2px 8px; }"
                "QPushButton:hover { background: %3; }").arg(bg).arg(fgFilled).arg(color));
        }
        setMinimumWidth(fontMetrics().horizontalAdvance(text()) + 20);
    }

private:
    SlotType m_type;
    QString m_value;
};

//=============================================================================
// Single clause: [operand] [op] [operand]
//=============================================================================
class VisualClause : public QFrame {
    Q_OBJECT
public:
    VisualClause(QWidget* parent = nullptr) : QFrame(parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(4);

        m_left = new ClickableSlot(ClickableSlot::Operand);
        m_op = new ClickableSlot(ClickableSlot::Operator);
        m_right = new ClickableSlot(ClickableSlot::Operand);

        layout->addWidget(m_left);
        layout->addWidget(m_op);
        layout->addWidget(m_right);

        // Remove button
        m_removeBtn = new QPushButton("×");
        m_removeBtn->setFixedSize(18, 18);
        m_removeBtn->setCursor(Qt::PointingHandCursor);
        m_removeBtn->setStyleSheet("border-radius: 9px; background: #F44747; color: white; font-weight: bold;");
        m_removeBtn->hide();
        connect(m_removeBtn, &QPushButton::clicked, this, &VisualClause::removeRequested);
        layout->addWidget(m_removeBtn);

        connect(m_left, &ClickableSlot::valueChanged, this, &VisualClause::changed);
        connect(m_op, &ClickableSlot::valueChanged, this, &VisualClause::changed);
        connect(m_right, &ClickableSlot::valueChanged, this, &VisualClause::changed);

        applyTheme();
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &VisualClause::applyTheme);
    }

    void showRemove(bool show) { m_removeBtn->setVisible(show); }
    ClickableSlot* leftSlot() { return m_left; }
    ClickableSlot* opSlot() { return m_op; }
    ClickableSlot* rightSlot() { return m_right; }

    QString toExpression() const {
        if (m_left->isEmpty() || m_op->isEmpty() || m_right->isEmpty()) return "";
        return QString("%1 %2 %3").arg(m_left->value()).arg(m_op->value()).arg(m_right->value());
    }

    bool isComplete() const {
        return !m_left->isEmpty() && !m_op->isEmpty() && !m_right->isEmpty();
    }

signals:
    void changed();
    void removeRequested();

private:
    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("VisualClause { background: %1; border: 1px solid %2; border-radius: 8px; }")
            .arg(tm.contentBg()).arg(tm.popupBorder()));
    }

    ClickableSlot* m_left;
    ClickableSlot* m_op;
    ClickableSlot* m_right;
    QPushButton* m_removeBtn;
};

//=============================================================================
// Connector button (AND/OR toggle)
//=============================================================================
class ConnectorButton : public QPushButton {
    Q_OBJECT
public:
    ConnectorButton(QWidget* parent = nullptr) : QPushButton("&&", parent), m_isAnd(true) {
        setFixedSize(36, 20);
        setCursor(Qt::PointingHandCursor);
        connect(this, &QPushButton::clicked, this, &ConnectorButton::toggle);
        updateStyle();
    }

    QString connector() const { return m_isAnd ? "&&" : "||"; }
    void setAnd(bool isAnd) { m_isAnd = isAnd; updateStyle(); }

signals:
    void connectorChanged();

private:
    void toggle() { m_isAnd = !m_isAnd; updateStyle(); emit connectorChanged(); }
    void updateStyle() {
        setText(m_isAnd ? "&&" : "||");
        QString color = m_isAnd ? "#4EC9B0" : "#DCDCAA";
        setStyleSheet(QString("QPushButton { background: transparent; color: %1; border: 1px solid %1; "
            "border-radius: 10px; font-weight: bold; font-size: 11px; }"
            "QPushButton:hover { background: %1; color: white; }").arg(color));
    }
    bool m_isAnd;
};

//=============================================================================
// Visual expression builder (interactive Scratch-like)
//=============================================================================
class VisualExpressionBuilder : public QFrame {
    Q_OBJECT
public:
    VisualExpressionBuilder(QWidget* parent = nullptr) : QFrame(parent) {
        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(8, 6, 8, 6);
        mainLayout->setSpacing(4);

        m_clausesLayout = new QVBoxLayout();
        m_clausesLayout->setSpacing(4);
        mainLayout->addLayout(m_clausesLayout);

        // Add clause button row
        auto* addRow = new QHBoxLayout();
        auto* addAndBtn = new QPushButton("+ AND");
        addAndBtn->setCursor(Qt::PointingHandCursor);
        addAndBtn->setStyleSheet("QPushButton { color: #4EC9B0; background: transparent; border: 1px dashed #4EC9B0; "
            "border-radius: 8px; padding: 2px 8px; } QPushButton:hover { background: #1D4D4D; }");
        connect(addAndBtn, &QPushButton::clicked, this, [this]() { addClause(true); });
        addRow->addWidget(addAndBtn);

        auto* addOrBtn = new QPushButton("+ OR");
        addOrBtn->setCursor(Qt::PointingHandCursor);
        addOrBtn->setStyleSheet("QPushButton { color: #DCDCAA; background: transparent; border: 1px dashed #DCDCAA; "
            "border-radius: 8px; padding: 2px 8px; } QPushButton:hover { background: #4D4D26; }");
        connect(addOrBtn, &QPushButton::clicked, this, [this]() { addClause(false); });
        addRow->addWidget(addOrBtn);
        addRow->addStretch();
        mainLayout->addLayout(addRow);

        // Add initial clause
        addClause(true, true);

        applyTheme();
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &VisualExpressionBuilder::applyTheme);
    }

    bool fillFirstEmptyOperand(const QString& val) {
        for (auto* clause : m_clauses) {
            if (clause->leftSlot()->isEmpty()) {
                clause->leftSlot()->setValue(val);
                return true;
            }
            if (clause->rightSlot()->isEmpty()) {
                clause->rightSlot()->setValue(val);
                return true;
            }
        }
        return false;
    }

    bool fillFirstEmptyOp(const QString& val) {
        for (auto* clause : m_clauses) {
            if (clause->opSlot()->isEmpty()) {
                clause->opSlot()->setValue(val);
                return true;
            }
        }
        return false;
    }

    QString toExpression() const {
        QString result;
        for (int i = 0; i < m_clauses.size(); i++) {
            QString clauseExpr = m_clauses[i]->toExpression();
            if (clauseExpr.isEmpty()) continue;
            if (!result.isEmpty() && i - 1 < m_connectors.size()) {
                result += " " + m_connectors[i - 1]->connector() + " ";
            }
            result += clauseExpr;
        }
        return result;
    }

signals:
    void expressionChanged();

private:
    void addClause(bool andConnector, bool isFirst = false) {
        if (!isFirst && !m_clauses.isEmpty()) {
            auto* connRow = new QWidget();
            auto* connLayout = new QHBoxLayout(connRow);
            connLayout->setContentsMargins(20, 0, 0, 0);
            auto* conn = new ConnectorButton();
            conn->setAnd(andConnector);
            connect(conn, &ConnectorButton::connectorChanged, this, &VisualExpressionBuilder::expressionChanged);
            connLayout->addWidget(conn);
            connLayout->addStretch();
            m_clausesLayout->addWidget(connRow);
            m_connectors.append(conn);
        }

        auto* clause = new VisualClause();
        clause->showRemove(m_clauses.size() > 0);
        connect(clause, &VisualClause::changed, this, &VisualExpressionBuilder::expressionChanged);
        connect(clause, &VisualClause::removeRequested, this, [this, clause]() { removeClause(clause); });
        m_clausesLayout->addWidget(clause);
        m_clauses.append(clause);
    }

    void removeClause(VisualClause* clause) {
        int idx = m_clauses.indexOf(clause);
        if (idx < 0 || m_clauses.size() <= 1) return;

        // Remove connector
        int connIdx = idx > 0 ? idx - 1 : 0;
        if (connIdx < m_connectors.size()) {
            auto* connWidget = m_connectors[connIdx]->parentWidget();
            m_connectors.removeAt(connIdx);
            connWidget->deleteLater();
        }

        m_clauses.removeAt(idx);
        clause->deleteLater();

        // Update first clause remove button visibility
        if (!m_clauses.isEmpty()) m_clauses[0]->showRemove(m_clauses.size() > 1);

        emit expressionChanged();
    }

    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("VisualExpressionBuilder { background: %1; border: 1px solid %2; border-radius: 8px; }")
            .arg(tm.popupBg()).arg(tm.popupBorder()));
    }

    QVBoxLayout* m_clausesLayout;
    QVector<VisualClause*> m_clauses;
    QVector<ConnectorButton*> m_connectors;
};

//=============================================================================
// Conditional Breakpoint Editor Dialog
//=============================================================================
class ConditionEditorDialog : public QDialog {
    Q_OBJECT
public:
    ConditionEditorDialog(const QString& address, const QString& currentCondition, QWidget* parent = nullptr)
        : QDialog(parent), m_address(address), m_updatingFromBuilder(false) {
        setWindowTitle(QString("Condition - %1").arg(address));
        resize(520, 440);
        setMinimumSize(440, 360);

        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->setSpacing(8);
        mainLayout->setContentsMargins(12, 12, 12, 12);

        // Visual expression builder
        auto* builderLabel = new QLabel("Visual Builder (click slots, then pick from toolbar)");
        builderLabel->setStyleSheet("color: gray; font-size: 10px;");
        mainLayout->addWidget(builderLabel);

        m_builder = new VisualExpressionBuilder();
        mainLayout->addWidget(m_builder);

        // Token toolbars (pill buttons)
        auto* tokenContainer = new QWidget();
        auto* tokenLayout = new QVBoxLayout(tokenContainer);
        tokenLayout->setSpacing(4);
        tokenLayout->setContentsMargins(0, 0, 0, 0);

        addTokenSection(tokenLayout, "Regs", {"A","B","C","D","E","H","L","BC","DE","HL","IX","IY","SP","PC"}, "#569CD6", 'o');
        addTokenSection(tokenLayout, "Flags", {"Z","NZ","CY","NC","PE","PO"}, "#4EC9B0", 'o');
        addTokenSection(tokenLayout, "Mem", {"(HL)","(DE)","(BC)","(IX)","(IY)","MRA","MWA","MRV"}, "#C586C0", 'o');
        addTokenSection(tokenLayout, "Ops", {"==","!=","<",">","<=",">=","&","|"}, "#DCDCAA", 'p');
        addTokenSection(tokenLayout, "Vals", {"$00","$FF","$0000","$FFFF","0","1","255"}, "#B5CEA8", 'o');

        mainLayout->addWidget(tokenContainer);

        // Text editor
        auto* editorLabel = new QLabel("Text Expression");
        editorLabel->setStyleSheet("color: gray; font-size: 10px;");
        mainLayout->addWidget(editorLabel);

        m_editor = new QTextEdit();
        m_editor->setFont(g_monoFont);
        m_editor->setPlainText(currentCondition);
        m_editor->setMaximumHeight(40);
        m_editor->setPlaceholderText("A == 0 && (HL) != $FF");
        new ConditionHighlighter(m_editor->document());
        mainLayout->addWidget(m_editor);

        // Validation
        m_validationLabel = new QLabel();
        mainLayout->addWidget(m_validationLabel);

        // Hit count
        auto* hitRow = new QHBoxLayout();
        hitRow->addWidget(new QLabel("Hits:"));
        m_hitMode = new QComboBox();
        m_hitMode->addItems({"Always", "== N", "% N", ">= N"});
        m_hitMode->setFixedWidth(70);
        hitRow->addWidget(m_hitMode);
        m_hitValue = new QSpinBox();
        m_hitValue->setRange(1, 99999);
        m_hitValue->setFixedWidth(60);
        hitRow->addWidget(m_hitValue);
        hitRow->addStretch();
        mainLayout->addLayout(hitRow);

        mainLayout->addStretch();

        // Buttons
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch();
        auto* cancelBtn = new QPushButton("Cancel");
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        btnRow->addWidget(cancelBtn);
        auto* okBtn = new QPushButton("OK");
        okBtn->setDefault(true);
        connect(okBtn, &QPushButton::clicked, this, [this]() { if (m_valid) accept(); });
        btnRow->addWidget(okBtn);
        mainLayout->addLayout(btnRow);

        // Wire up builder -> text sync
        connect(m_builder, &VisualExpressionBuilder::expressionChanged, this, [this]() {
            m_updatingFromBuilder = true;
            m_editor->setPlainText(m_builder->toExpression());
            m_updatingFromBuilder = false;
            validate();
        });


        connect(m_editor, &QTextEdit::textChanged, this, [this]() {
            if (!m_updatingFromBuilder) validate();
        });

        validate();
        applyTheme();
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &ConditionEditorDialog::applyTheme);
    }

    QString condition() const { return m_editor->toPlainText().trimmed(); }

private slots:
    void validate() {
        QString expr = m_editor->toPlainText().trimmed();
        if (expr.isEmpty()) {
            m_validationLabel->setText("Unconditional");
            m_validationLabel->setStyleSheet("color: gray;");
            m_valid = true;
        } else {
            int parens = 0;
            for (QChar c : expr) { if (c == '(') parens++; if (c == ')') parens--; }
            if (parens != 0) {
                m_validationLabel->setText("Unbalanced ()");
                m_validationLabel->setStyleSheet("color: #F44747;");
                m_valid = false;
            } else {
                m_validationLabel->setText("OK");
                m_validationLabel->setStyleSheet("color: #6A9955;");
                m_valid = true;
            }
        }
    }

    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("QDialog { background: %1; color: %2; }"
            "QTextEdit { background: %3; border: 1px solid %4; border-radius: 4px; }"
            "QComboBox, QSpinBox { background: %3; border: 1px solid %4; padding: 2px; }"
            "QPushButton { background: %5; border: none; padding: 4px 12px; border-radius: 4px; }"
            "QPushButton:hover { background: %4; }")
            .arg(tm.popupBg()).arg(tm.fg()).arg(tm.contentBg()).arg(tm.popupBorder()).arg(tm.tagBg()));
    }

private:
    // mode: 'o' = operand (auto-fill), 'p' = operator (auto-fill), 't' = text editor only
    void addTokenSection(QVBoxLayout* parent, const QString& label, std::initializer_list<const char*> tokens, const QString& color, char mode) {
        auto* row = new QWidget();
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(3);

        auto* lbl = new QLabel(label);
        lbl->setFixedWidth(36);
        lbl->setStyleSheet("color: gray; font-size: 10px;");
        layout->addWidget(lbl);

        for (auto t : tokens) {
            auto* btn = new QPushButton(t);
            btn->setFont(g_monoFont);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(QString(
                "QPushButton { background: transparent; color: %1; border: 1px solid %1; "
                "border-radius: 10px; padding: 1px 5px; font-size: 11px; }"
                "QPushButton:hover { background: %1; color: white; }"
            ).arg(color));
            connect(btn, &QPushButton::clicked, this, [this, t, mode]() {
                if (mode == 'o') {
                    m_builder->fillFirstEmptyOperand(t);
                } else if (mode == 'p') {
                    m_builder->fillFirstEmptyOp(t);
                } else {
                    m_editor->insertPlainText(QString(t) + " ");
                    m_editor->setFocus();
                }
            });
            layout->addWidget(btn);
        }
        layout->addStretch();
        parent->addWidget(row);
    }

    QString m_address;
    VisualExpressionBuilder* m_builder;
    QTextEdit* m_editor;
    QLabel* m_validationLabel;
    QComboBox* m_hitMode;
    bool m_updatingFromBuilder;
    QSpinBox* m_hitValue;
    bool m_valid = true;
};

//=============================================================================
// Base layout with hover support
//=============================================================================
class BaseBreakpointLayout : public QGroupBox {
    Q_OBJECT
public:
    BaseBreakpointLayout(const QString& title, QWidget* parent = nullptr)
        : QGroupBox(title, parent) {
        m_hoverTimer.setSingleShot(true);
        connect(&m_hoverTimer, &QTimer::timeout, this, &BaseBreakpointLayout::showPopup);
    }

protected:
    void startHover(int index, const QPoint& globalPos) {
        m_hoverTimer.stop();
        BreakpointPopup::instance().hide();
        if (index >= 0 && index < g_numBreakpoints) {
            m_hoverIndex = index;
            m_hoverPos = globalPos + QPoint(15, 15);
            m_hoverTimer.start(400);
        }
    }

    void stopHover() {
        m_hoverTimer.stop();
        BreakpointPopup::instance().hide();
        m_hoverIndex = -1;
    }

    void showPopup() {
        if (m_hoverIndex >= 0 && m_hoverIndex < g_numBreakpoints) {
            BreakpointPopup::instance().showAt(g_breakpoints[m_hoverIndex], m_hoverPos);
        }
    }

    QTimer m_hoverTimer;
    int m_hoverIndex = -1;
    QPoint m_hoverPos;
};

//=============================================================================
// Layout 1: Table with checkboxes
//=============================================================================
class Layout1 : public BaseBreakpointLayout {
    Q_OBJECT
public:
    Layout1(QWidget* parent = nullptr) : BaseBreakpointLayout("Layout 1: Classic Table", parent) {
        auto* layout = new QVBoxLayout(this);

        m_table = new QTableWidget(g_numBreakpoints, 7);
        m_table->setHorizontalHeaderLabels({"On", "F", "R", "W", "Address", "Condition", "Hits"});
        m_table->horizontalHeaderItem(0)->setToolTip("Enabled");
        m_table->horizontalHeaderItem(1)->setToolTip("Break on Fetch (execute)");
        m_table->horizontalHeaderItem(2)->setToolTip("Break on Read");
        m_table->horizontalHeaderItem(3)->setToolTip("Break on Write");
        m_table->setFont(g_monoFont);
        m_table->verticalHeader()->hide();
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setMouseTracking(true);
        m_table->viewport()->installEventFilter(this);

        for (int i = 0; i < g_numBreakpoints; i++) {
            auto& bp = g_breakpoints[i];

            auto* onCb = new QCheckBox();
            onCb->setChecked(bp.enabled);
            m_table->setCellWidget(i, 0, onCb);

            auto* fCb = new QCheckBox();
            fCb->setChecked(bp.onFetch);
            m_table->setCellWidget(i, 1, fCb);

            auto* rCb = new QCheckBox();
            rCb->setChecked(bp.onRead);
            m_table->setCellWidget(i, 2, rCb);

            auto* wCb = new QCheckBox();
            wCb->setChecked(bp.onWrite);
            m_table->setCellWidget(i, 3, wCb);

            m_table->setItem(i, 4, new QTableWidgetItem(QString("$%1").arg(bp.address, 4, 16, QChar('0')).toUpper()));
            m_table->setItem(i, 5, new QTableWidgetItem(bp.condition));
            m_table->setItem(i, 6, new QTableWidgetItem(QString::number(bp.hitCount)));
        }

        m_table->resizeColumnsToContents();
        layout->addWidget(m_table);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (obj == m_table->viewport()) {
            if (event->type() == QEvent::MouseMove) {
                auto* me = static_cast<QMouseEvent*>(event);
                int row = m_table->rowAt(me->pos().y());
                startHover(row, me->globalPosition().toPoint());
            } else if (event->type() == QEvent::Leave) {
                stopHover();
            }
        }
        return BaseBreakpointLayout::eventFilter(obj, event);
    }

private:
    QTableWidget* m_table;
};

//=============================================================================
// Layout 2: Compact list with icons
//=============================================================================
class Layout2 : public BaseBreakpointLayout {
    Q_OBJECT
public:
    Layout2(QWidget* parent = nullptr) : BaseBreakpointLayout("Layout 2: Compact Icons", parent) {
        auto* layout = new QVBoxLayout(this);

        for (int i = 0; i < g_numBreakpoints; i++) {
            auto* row = new QWidget();
            row->setProperty("bpIndex", i);
            row->installEventFilter(this);
            row->setMouseTracking(true);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 2, 4, 2);
            rowLayout->setSpacing(4);

            auto& bp = g_breakpoints[i];
            auto& tm = ThemeManager::instance();

            auto* onCb = new QCheckBox();
            onCb->setChecked(bp.enabled);
            rowLayout->addWidget(onCb);

            // Type indicators
            auto* types = new QLabel();
            QString typeStr;
            if (bp.onFetch) typeStr += QString("<span style='color:%1'>F</span>").arg(tm.fetchColor());
            if (bp.onRead) typeStr += QString("<span style='color:%1'>R</span>").arg(tm.readColor());
            if (bp.onWrite) typeStr += QString("<span style='color:%1'>W</span>").arg(tm.writeColor());
            types->setText(typeStr);
            types->setFont(g_monoFont);
            rowLayout->addWidget(types);

            auto* addr = new QLabel(QString("$%1").arg(bp.address, 4, 16, QChar('0')).toUpper());
            addr->setFont(g_monoFont);
            addr->setStyleSheet("font-weight: bold;");
            rowLayout->addWidget(addr);

            if (!bp.condition.isEmpty()) {
                auto* cond = new QLabel(bp.condition);
                cond->setFont(g_monoFont);
                cond->setStyleSheet(QString("color: %1;").arg(tm.condColor()));
                rowLayout->addWidget(cond);
            }

            rowLayout->addStretch();

            if (bp.hitCount > 0) {
                auto* hits = new QLabel(QString("%1").arg(bp.hitCount));
                hits->setFont(g_monoFont);
                hits->setStyleSheet(QString("color: %1;").arg(tm.hitColor()));
                rowLayout->addWidget(hits);
            }

            layout->addWidget(row);
        }
        layout->addStretch();
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        auto* w = qobject_cast<QWidget*>(obj);
        if (w && w->property("bpIndex").isValid()) {
            int idx = w->property("bpIndex").toInt();
            if (event->type() == QEvent::Enter) {
                startHover(idx, QCursor::pos());
            } else if (event->type() == QEvent::Leave) {
                stopHover();
            }
        }
        return BaseBreakpointLayout::eventFilter(obj, event);
    }
};

//=============================================================================
// Layout 3: Card style
//=============================================================================
class Layout3 : public BaseBreakpointLayout {
    Q_OBJECT
public:
    Layout3(QWidget* parent = nullptr) : BaseBreakpointLayout("Layout 3: Cards", parent) {
        auto* mainLayout = new QVBoxLayout(this);
        auto* grid = new QGridLayout();
        grid->setSpacing(6);

        for (int i = 0; i < g_numBreakpoints; i++) {
            auto& bp = g_breakpoints[i];
            auto& tm = ThemeManager::instance();

            auto* card = new QFrame();
            card->setProperty("bpIndex", i);
            card->installEventFilter(this);
            card->setMouseTracking(true);
            card->setFrameStyle(QFrame::NoFrame);
            card->setMinimumSize(150, 60);

            auto* cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(6, 4, 6, 4);
            cardLayout->setSpacing(2);

            // Address + enable
            auto* headerRow = new QHBoxLayout();
            auto* onCb = new QCheckBox();
            onCb->setChecked(bp.enabled);
            headerRow->addWidget(onCb);
            auto* addr = new QLabel(QString("$%1").arg(bp.address, 4, 16, QChar('0')).toUpper());
            addr->setFont(g_monoFont);
            headerRow->addWidget(addr);
            headerRow->addStretch();
            cardLayout->addLayout(headerRow);

            // Type tags
            auto* typesRow = new QHBoxLayout();
            QString types;
            if (bp.onFetch) types += "F ";
            if (bp.onRead) types += "R ";
            if (bp.onWrite) types += "W";
            auto* typesLabel = new QLabel(types.trimmed());
            typesLabel->setFont(g_monoFont);
            typesLabel->setStyleSheet(QString("color: %1;").arg(tm.fetchColor()));
            typesRow->addWidget(typesLabel);
            typesRow->addStretch();
            cardLayout->addLayout(typesRow);

            m_cards.append(card);
            grid->addWidget(card, i / 3, i % 3);
        }
        grid->setRowStretch(3, 1);
        mainLayout->addLayout(grid);
        mainLayout->addStretch();

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Layout3::applyTheme);
        applyTheme();
    }

    void applyTheme() {
        auto& tm = ThemeManager::instance();
        for (auto* card : m_cards) {
            card->setStyleSheet(QString("background: %1; border: 1px solid %2; border-radius: 4px;")
                .arg(tm.contentBg()).arg(tm.popupBorder()));
        }
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        auto* w = qobject_cast<QWidget*>(obj);
        if (w && w->property("bpIndex").isValid()) {
            int idx = w->property("bpIndex").toInt();
            if (event->type() == QEvent::Enter) {
                startHover(idx, QCursor::pos());
            } else if (event->type() == QEvent::Leave) {
                stopHover();
            }
        }
        return BaseBreakpointLayout::eventFilter(obj, event);
    }

private:
    QVector<QFrame*> m_cards;
};

//=============================================================================
// Layout 4: Tree view with grouping by type
//=============================================================================
class Layout4 : public BaseBreakpointLayout {
    Q_OBJECT
public:
    Layout4(QWidget* parent = nullptr) : BaseBreakpointLayout("Layout 4: Grouped Tree", parent) {
        auto* layout = new QVBoxLayout(this);

        m_tree = new QTreeWidget();
        m_tree->setHeaderLabels({"Breakpoints", "Condition", "Hits"});
        m_tree->setFont(g_monoFont);
        m_tree->setMouseTracking(true);
        m_tree->viewport()->installEventFilter(this);

        auto* fetchGroup = new QTreeWidgetItem(m_tree, {"Fetch Breakpoints"});
        auto* readGroup = new QTreeWidgetItem(m_tree, {"Read Breakpoints"});
        auto* writeGroup = new QTreeWidgetItem(m_tree, {"Write Breakpoints"});

        for (int i = 0; i < g_numBreakpoints; i++) {
            auto& bp = g_breakpoints[i];
            QString addr = QString("$%1").arg(bp.address, 4, 16, QChar('0')).toUpper();

            QTreeWidgetItem* item = nullptr;
            if (bp.onFetch) {
                item = new QTreeWidgetItem(fetchGroup, {addr, bp.condition, QString::number(bp.hitCount)});
            } else if (bp.onRead) {
                item = new QTreeWidgetItem(readGroup, {addr, bp.condition, QString::number(bp.hitCount)});
            } else if (bp.onWrite) {
                item = new QTreeWidgetItem(writeGroup, {addr, bp.condition, QString::number(bp.hitCount)});
            }
            if (item) {
                item->setData(0, Qt::UserRole, i);
                item->setCheckState(0, bp.enabled ? Qt::Checked : Qt::Unchecked);
            }
        }

        m_tree->expandAll();
        layout->addWidget(m_tree);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (obj == m_tree->viewport()) {
            if (event->type() == QEvent::MouseMove) {
                auto* me = static_cast<QMouseEvent*>(event);
                auto* item = m_tree->itemAt(me->pos());
                if (item && item->data(0, Qt::UserRole).isValid()) {
                    int idx = item->data(0, Qt::UserRole).toInt();
                    startHover(idx, me->globalPosition().toPoint());
                } else {
                    stopHover();
                }
            } else if (event->type() == QEvent::Leave) {
                stopHover();
            }
        }
        return BaseBreakpointLayout::eventFilter(obj, event);
    }

private:
    QTreeWidget* m_tree;
};

//=============================================================================
// Layout 5: Minimal list
//=============================================================================
class Layout5 : public BaseBreakpointLayout {
    Q_OBJECT
public:
    Layout5(QWidget* parent = nullptr) : BaseBreakpointLayout("Layout 5: Minimal List", parent) {
        auto* layout = new QVBoxLayout(this);

        m_list = new QListWidget();
        m_list->setFont(g_monoFont);
        m_list->setMouseTracking(true);
        m_list->viewport()->installEventFilter(this);

        auto& tm = ThemeManager::instance();
        for (int i = 0; i < g_numBreakpoints; i++) {
            auto& bp = g_breakpoints[i];
            QString text = QString("%1 $%2")
                .arg(bp.enabled ? "●" : "○")
                .arg(bp.address, 4, 16, QChar('0')).toUpper();
            if (!bp.label.isEmpty()) {
                text += QString(" (%1)").arg(bp.label);
            }
            auto* item = new QListWidgetItem(text);
            item->setData(Qt::UserRole, i);
            if (!bp.enabled) {
                item->setForeground(QColor(tm.disabledColor()));
            }
            m_list->addItem(item);
        }

        layout->addWidget(m_list);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (obj == m_list->viewport()) {
            if (event->type() == QEvent::MouseMove) {
                auto* me = static_cast<QMouseEvent*>(event);
                auto* item = m_list->itemAt(me->pos());
                if (item) {
                    int idx = item->data(Qt::UserRole).toInt();
                    startHover(idx, me->globalPosition().toPoint());
                } else {
                    stopHover();
                }
            } else if (event->type() == QEvent::Leave) {
                stopHover();
            }
        }
        return BaseBreakpointLayout::eventFilter(obj, event);
    }

private:
    QListWidget* m_list;
};

//=============================================================================
// Layout 6: Vertical detail cards
//=============================================================================
class Layout6 : public BaseBreakpointLayout {
    Q_OBJECT
public:
    Layout6(QWidget* parent = nullptr) : BaseBreakpointLayout("Layout 6: Detail Cards", parent) {
        auto* layout = new QVBoxLayout(this);
        auto* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        auto* content = new QWidget();
        auto* contentLayout = new QVBoxLayout(content);

        for (int i = 0; i < g_numBreakpoints; i++) {
            auto& bp = g_breakpoints[i];
            auto& tm = ThemeManager::instance();

            auto* card = new QFrame();
            card->setProperty("bpIndex", i);
            card->installEventFilter(this);
            card->setMouseTracking(true);
            card->setStyleSheet(QString("background: %1; border-radius: 6px;").arg(tm.contentBg()));

            auto* cardLayout = new QHBoxLayout(card);
            cardLayout->setContentsMargins(10, 8, 10, 8);

            auto* onCb = new QCheckBox();
            onCb->setChecked(bp.enabled);
            cardLayout->addWidget(onCb);

            auto* infoLayout = new QVBoxLayout();
            auto* addrRow = new QHBoxLayout();
            auto* addr = new QLabel(QString("$%1").arg(bp.address, 4, 16, QChar('0')).toUpper());
            addr->setFont(g_monoFont);
            addr->setStyleSheet("font-weight: bold;");
            addrRow->addWidget(addr);

            if (!bp.label.isEmpty()) {
                auto* lbl = new QLabel(bp.label);
                lbl->setStyleSheet(QString("color: %1;").arg(tm.hintColor()));
                addrRow->addWidget(lbl);
            }
            addrRow->addStretch();
            infoLayout->addLayout(addrRow);

            auto* detailRow = new QHBoxLayout();
            QString types;
            if (bp.onFetch) types += "Fetch ";
            if (bp.onRead) types += "Read ";
            if (bp.onWrite) types += "Write";
            auto* typesLbl = new QLabel(types.trimmed());
            typesLbl->setFont(g_monoFont);
            detailRow->addWidget(typesLbl);

            if (!bp.condition.isEmpty()) {
                auto* cond = new QLabel(QString("if %1").arg(bp.condition));
                cond->setStyleSheet(QString("color: %1;").arg(tm.condColor()));
                detailRow->addWidget(cond);
            }
            detailRow->addStretch();
            infoLayout->addLayout(detailRow);

            cardLayout->addLayout(infoLayout, 1);

            auto* hits = new QLabel(QString("%1").arg(bp.hitCount));
            hits->setFont(g_monoFont);
            hits->setStyleSheet(QString("color: %1; font-weight: bold;").arg(tm.hitColor()));
            cardLayout->addWidget(hits);

            contentLayout->addWidget(card);
        }
        contentLayout->addStretch();
        scroll->setWidget(content);
        layout->addWidget(scroll);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        auto* w = qobject_cast<QWidget*>(obj);
        if (w && w->property("bpIndex").isValid()) {
            int idx = w->property("bpIndex").toInt();
            if (event->type() == QEvent::Enter) {
                startHover(idx, QCursor::pos());
            } else if (event->type() == QEvent::Leave) {
                stopHover();
            }
        }
        return BaseBreakpointLayout::eventFilter(obj, event);
    }
};

//=============================================================================
// Layout 7: Address bar style
//=============================================================================
class Layout7 : public BaseBreakpointLayout {
    Q_OBJECT
public:
    Layout7(QWidget* parent = nullptr) : BaseBreakpointLayout("Layout 7: Address Bar", parent) {
        auto* layout = new QVBoxLayout(this);

        auto* barLayout = new QHBoxLayout();
        barLayout->setSpacing(2);

        for (int i = 0; i < g_numBreakpoints; i++) {
            auto& bp = g_breakpoints[i];
            auto& tm = ThemeManager::instance();

            auto* btn = new QPushButton(QString("$%1").arg(bp.address, 4, 16, QChar('0')).toUpper());
            btn->setProperty("bpIndex", i);
            btn->setFont(g_monoFont);
            btn->setCheckable(true);
            btn->setChecked(bp.enabled);
            btn->installEventFilter(this);

            QString bgColor = bp.enabled ? tm.activeColor() : tm.disabledColor();
            btn->setStyleSheet(QString(
                "QPushButton { background: %1; color: white; border: none; border-radius: 4px; padding: 4px 8px; }"
                "QPushButton:checked { background: %2; }"
            ).arg(tm.disabledColor()).arg(tm.activeColor()));

            barLayout->addWidget(btn);
        }
        barLayout->addStretch();
        layout->addLayout(barLayout);
        layout->addStretch();
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        auto* btn = qobject_cast<QPushButton*>(obj);
        if (btn && btn->property("bpIndex").isValid()) {
            int idx = btn->property("bpIndex").toInt();
            if (event->type() == QEvent::Enter) {
                startHover(idx, QCursor::pos());
            } else if (event->type() == QEvent::Leave) {
                stopHover();
            }
        }
        return BaseBreakpointLayout::eventFilter(obj, event);
    }
};

//=============================================================================
// Layout 8: Dense matrix
//=============================================================================
class Layout8 : public BaseBreakpointLayout {
    Q_OBJECT
public:
    Layout8(QWidget* parent = nullptr) : BaseBreakpointLayout("Layout 8: Dense Matrix", parent) {
        auto* mainLayout = new QVBoxLayout(this);
        auto* grid = new QGridLayout();
        grid->setSpacing(1);
        grid->setContentsMargins(0, 0, 0, 0);

        // Header with tooltips
        auto addHeader = [&](int col, const QString& text, const QString& tooltip) {
            auto* lbl = new QLabel(text);
            lbl->setToolTip(tooltip);
            grid->addWidget(lbl, 0, col);
        };
        addHeader(0, "#", "Breakpoint number");
        addHeader(1, "On", "Enabled");
        addHeader(2, "F", "Break on Fetch (execute)");
        addHeader(3, "R", "Break on Read");
        addHeader(4, "W", "Break on Write");
        addHeader(5, "Addr", "Address");
        addHeader(6, "Cnt", "Hit count");

        for (int i = 0; i < g_numBreakpoints; i++) {
            auto& bp = g_breakpoints[i];

            auto* row = new QWidget();
            row->setProperty("bpIndex", i);
            row->installEventFilter(this);
            row->setMouseTracking(true);

            auto* num = new QLabel(QString("%1").arg(i + 1));
            num->setFont(g_monoFont);
            grid->addWidget(num, i + 1, 0);

            auto* on = new QCheckBox();
            on->setChecked(bp.enabled);
            grid->addWidget(on, i + 1, 1);

            auto* f = new QCheckBox();
            f->setChecked(bp.onFetch);
            grid->addWidget(f, i + 1, 2);

            auto* r = new QCheckBox();
            r->setChecked(bp.onRead);
            grid->addWidget(r, i + 1, 3);

            auto* w = new QCheckBox();
            w->setChecked(bp.onWrite);
            grid->addWidget(w, i + 1, 4);

            auto* addr = new QLabel(QString("$%1").arg(bp.address, 4, 16, QChar('0')).toUpper());
            addr->setFont(g_monoFont);
            addr->setProperty("bpIndex", i);
            addr->installEventFilter(this);
            addr->setMouseTracking(true);
            grid->addWidget(addr, i + 1, 5);

            auto* cnt = new QLabel(QString("%1").arg(bp.hitCount));
            cnt->setFont(g_monoFont);
            grid->addWidget(cnt, i + 1, 6);
        }
        mainLayout->addLayout(grid);
        mainLayout->addStretch();
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        auto* w = qobject_cast<QWidget*>(obj);
        if (w && w->property("bpIndex").isValid()) {
            int idx = w->property("bpIndex").toInt();
            if (event->type() == QEvent::Enter) {
                startHover(idx, QCursor::pos());
            } else if (event->type() == QEvent::Leave) {
                stopHover();
            }
        }
        return BaseBreakpointLayout::eventFilter(obj, event);
    }
};

//=============================================================================
// Layout 9: Xpeccy-style horizontal
//=============================================================================
class Layout9 : public BaseBreakpointLayout {
    Q_OBJECT
public:
    Layout9(QWidget* parent = nullptr) : BaseBreakpointLayout("Layout 9: Xpeccy Style", parent) {
        auto* layout = new QVBoxLayout(this);

        m_table = new QTableWidget(g_numBreakpoints, 7);
        m_table->setHorizontalHeaderLabels({"On", "F", "R", "W", "Addr", "Cond", "Cnt"});
        m_table->horizontalHeaderItem(0)->setToolTip("Enabled");
        m_table->horizontalHeaderItem(1)->setToolTip("Break on Fetch (execute)");
        m_table->horizontalHeaderItem(2)->setToolTip("Break on Read");
        m_table->horizontalHeaderItem(3)->setToolTip("Break on Write");
        m_table->horizontalHeaderItem(4)->setToolTip("Address");
        m_table->horizontalHeaderItem(5)->setToolTip("Condition expression");
        m_table->horizontalHeaderItem(6)->setToolTip("Hit count");
        m_table->setFont(g_monoFont);
        m_table->verticalHeader()->hide();
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setAlternatingRowColors(true);
        m_table->setMouseTracking(true);
        m_table->viewport()->installEventFilter(this);

        auto& tm = ThemeManager::instance();
        for (int i = 0; i < g_numBreakpoints; i++) {
            auto& bp = g_breakpoints[i];

            auto* onCb = new QCheckBox();
            onCb->setChecked(bp.enabled);
            m_table->setCellWidget(i, 0, onCb);

            auto* fCb = new QCheckBox();
            fCb->setChecked(bp.onFetch);
            m_table->setCellWidget(i, 1, fCb);

            auto* rCb = new QCheckBox();
            rCb->setChecked(bp.onRead);
            m_table->setCellWidget(i, 2, rCb);

            auto* wCb = new QCheckBox();
            wCb->setChecked(bp.onWrite);
            m_table->setCellWidget(i, 3, wCb);

            auto* addr = new QTableWidgetItem(QString("$%1").arg(bp.address, 4, 16, QChar('0')).toUpper());
            addr->setFont(g_monoFont);
            m_table->setItem(i, 4, addr);

            // Condition cell with edit button
            auto* condWidget = new QWidget();
            auto* condLayout = new QHBoxLayout(condWidget);
            condLayout->setContentsMargins(2, 0, 2, 0);
            condLayout->setSpacing(2);
            auto* condLabel = new QLabel(bp.condition.isEmpty() ? "-" : bp.condition);
            condLabel->setFont(g_monoFont);
            condLabel->setStyleSheet(QString("color: %1;").arg(tm.condColor()));
            condLayout->addWidget(condLabel, 1);
            auto* editBtn = new QPushButton("...");
            editBtn->setFixedSize(20, 18);
            editBtn->setToolTip("Edit condition");
            connect(editBtn, &QPushButton::clicked, this, [this, i, condLabel]() {
                QString addr = QString("$%1").arg(g_breakpoints[i].address, 4, 16, QChar('0')).toUpper();
                ConditionEditorDialog dlg(addr, g_breakpoints[i].condition, this);
                if (dlg.exec() == QDialog::Accepted) {
                    g_breakpoints[i].condition = dlg.condition();
                    condLabel->setText(dlg.condition().isEmpty() ? "-" : dlg.condition());
                }
            });
            condLayout->addWidget(editBtn);
            m_table->setCellWidget(i, 5, condWidget);

            auto* cnt = new QTableWidgetItem(QString::number(bp.hitCount));
            cnt->setForeground(QColor(tm.hitColor()));
            m_table->setItem(i, 6, cnt);
        }

        m_table->resizeColumnsToContents();
        m_table->setColumnWidth(5, 150);
        layout->addWidget(m_table);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (obj == m_table->viewport()) {
            if (event->type() == QEvent::MouseMove) {
                auto* me = static_cast<QMouseEvent*>(event);
                int row = m_table->rowAt(me->pos().y());
                startHover(row, me->globalPosition().toPoint());
            } else if (event->type() == QEvent::Leave) {
                stopHover();
            }
        }
        return BaseBreakpointLayout::eventFilter(obj, event);
    }

private:
    QTableWidget* m_table;
};

//=============================================================================
// Main window
//=============================================================================
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow() {
        setWindowTitle("Breakpoints Widget POC");
        initMonoFont();

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
            applyTheme(id == 1);
        });
        themeRow->addWidget(lightRadio);
        themeRow->addWidget(darkRadio);
        themeRow->addStretch();
        mainLayout->addLayout(themeRow);

        // Tabs for layouts
        auto* tabs = new QTabWidget();
        tabs->addTab(new Layout1(), "1: Table");
        tabs->addTab(new Layout2(), "2: Icons");
        tabs->addTab(new Layout3(), "3: Cards");
        tabs->addTab(new Layout4(), "4: Tree");
        tabs->addTab(new Layout5(), "5: Minimal");
        tabs->addTab(new Layout6(), "6: Detail");
        tabs->addTab(new Layout7(), "7: Address");
        tabs->addTab(new Layout8(), "8: Matrix");
        tabs->addTab(new Layout9(), "9: Xpeccy");
        mainLayout->addWidget(tabs);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &MainWindow::applyTheme);
        applyTheme(false);
    }

private:
    void applyTheme(bool) {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("QWidget { background: %1; color: %2; }").arg(tm.bg()).arg(tm.fg()));
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    MainWindow w;
    w.resize(650, 450);
    w.setMinimumSize(600, 350);
    w.setMaximumSize(800, 600);
    w.show();

    return app.exec();
}

#include "main.moc"
