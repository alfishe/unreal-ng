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
#include <QFrame>
#include <QMenu>
#include <QAction>
#include <QToolTip>
#include <QMouseEvent>
#include <QTimer>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollArea>
#include <QDialog>
#include <QTextEdit>
#include <QPoint>
#include <QClipboard>
#include <QScreen>
#include <QFontDatabase>

// Global monospace font
static QFont g_monoFont;

void initMonoFont() {
    g_monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    g_monoFont.setPointSize(12);
}

// Simulated stack data
struct StackEntry {
    uint16_t value;
    bool looksLikeCode;  // Heuristic: is this a valid code address?
    QString label;       // Optional label if known
};

static uint16_t g_sp = 0xFF00;
static StackEntry g_stack[16] = {
    {0x8003, true, "main_loop"},
    {0x5C00, false, ""},
    {0x0000, false, ""},
    {0x1234, false, ""},
    {0x9000, true, "isr_handler"},
    {0xABCD, false, ""},
    {0x0038, true, "im1_vector"},
    {0x0000, false, ""},
    {0xFFFF, false, ""},
    {0x8100, true, "draw_sprite"},
    {0x5B00, false, "SYSVAR"},
    {0x0000, false, ""},
    {0x0000, false, ""},
    {0x0000, false, ""},
    {0x0000, false, ""},
    {0x0000, false, ""},
};

//=============================================================================
// Clickable label with hover and context menu support
//=============================================================================
class ClickableStackValue : public QLabel {
    Q_OBJECT
public:
    ClickableStackValue(int index, QWidget* parent = nullptr)
        : QLabel(parent), m_index(index), m_isCodeAddress(false) {
        setCursor(Qt::PointingHandCursor);
        setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setFont(g_monoFont);
        setMouseTracking(true);
        setContextMenuPolicy(Qt::CustomContextMenu);
        connect(this, &QWidget::customContextMenuRequested, this, &ClickableStackValue::showContextMenu);

        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setSingleShot(true);
        connect(m_hoverTimer, &QTimer::timeout, this, [this]() {
            emit showDisassemblyPopup(m_value, m_lastMousePos + QPoint(15, 15));
        });
    }

    void setValue(uint16_t value, bool isCode, const QString& label) {
        m_value = value;
        m_isCodeAddress = isCode;
        m_label = label;

        QString txt = QString("%1").arg(value, 4, 16, QChar('0')).toUpper();
        setText(txt);

        if (isCode) {
            setStyleSheet("color: #2196F3; font-weight: bold;");
        } else {
            setStyleSheet("");
        }
    }

signals:
    void clicked(int index, uint16_t value);
    void jumpToDisassembly(uint16_t addr);
    void jumpToMemory(uint16_t addr);
    void showDisassemblyPopup(uint16_t addr, QPoint pos);

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            emit clicked(m_index, m_value);
        }
    }

    void mouseDoubleClickEvent(QMouseEvent*) override {
        emit jumpToDisassembly(m_value);
    }

    void enterEvent(QEnterEvent* e) override {
        m_lastMousePos = mapToGlobal(e->position().toPoint());
        m_hoverTimer->start(400);
    }

    void leaveEvent(QEvent*) override {
        m_hoverTimer->stop();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        m_lastMousePos = e->globalPosition().toPoint();
        QLabel::mouseMoveEvent(e);
    }

private slots:
    void showContextMenu(const QPoint& pos) {
        QMenu menu(this);

        auto* jumpDasm = menu.addAction("Jump to in Disassembly");
        auto* jumpMem = menu.addAction("Show in Memory View");
        menu.addSeparator();
        auto* preview = menu.addAction("Preview Disassembly...");
        menu.addSeparator();
        auto* copyAddr = menu.addAction("Copy Address");
        auto* copyValue = menu.addAction("Copy Value");

        connect(jumpDasm, &QAction::triggered, [this]() { emit jumpToDisassembly(m_value); });
        connect(jumpMem, &QAction::triggered, [this]() { emit jumpToMemory(m_value); });
        connect(preview, &QAction::triggered, [this]() {
            emit showDisassemblyPopup(m_value, mapToGlobal(QPoint(width(), 0)));
        });
        connect(copyAddr, &QAction::triggered, [this]() {
            QApplication::clipboard()->setText(QString("$%1").arg(m_value, 4, 16, QChar('0')).toUpper());
        });
        connect(copyValue, &QAction::triggered, [this]() {
            QApplication::clipboard()->setText(QString::number(m_value));
        });

        menu.exec(mapToGlobal(pos));
    }

private:
    int m_index;
    uint16_t m_value = 0;
    bool m_isCodeAddress = false;
    QString m_label;
    QTimer* m_hoverTimer;
    QPoint m_lastMousePos;
};

//=============================================================================
// Memory region identification
//=============================================================================
struct MemoryRegionInfo {
    const char* name;
    const char* description;
    bool isCode;
    bool isSystemVar;
};

MemoryRegionInfo identifyRegion(uint16_t addr) {
    // ZX Spectrum 48K memory map
    if (addr < 0x4000) {
        return {"ROM", "48K BASIC ROM", true, false};
    } else if (addr >= 0x4000 && addr < 0x5800) {
        return {"Screen", "Display file (bitmap)", false, false};
    } else if (addr >= 0x5800 && addr < 0x5B00) {
        return {"Attributes", "Screen attributes (colors)", false, false};
    } else if (addr >= 0x5B00 && addr < 0x5C00) {
        return {"Printer Buffer", "Printer buffer / spare", false, false};
    } else if (addr >= 0x5C00 && addr < 0x5E00) {
        return {"System Variables", "BASIC system variables", false, true};
    } else if (addr >= 0x5E00 && addr < 0x6000) {
        return {"Reserved", "Reserved / microdrive maps", false, false};
    } else if (addr >= 0x6000 && addr < 0x8000) {
        return {"BASIC Area", "BASIC program / variables", false, false};
    } else if (addr >= 0x8000 && addr < 0xC000) {
        return {"User Code", "Typically machine code", true, false};
    } else {
        return {"Upper RAM", "RAM bank (128K) / User area", true, false};
    }
}

// System variable names (subset)
QString getSystemVarName(uint16_t addr) {
    static const struct { uint16_t addr; const char* name; const char* desc; } sysvars[] = {
        {0x5C00, "KSTATE", "Keyboard state"},
        {0x5C08, "LAST_K", "Last key pressed"},
        {0x5C09, "REPDEL", "Key repeat delay"},
        {0x5C0A, "REPPER", "Key repeat period"},
        {0x5C0B, "DEFADD", "DEF FN address"},
        {0x5C36, "CHARS", "Character set address"},
        {0x5C3A, "ERR_NR", "Error number"},
        {0x5C3B, "FLAGS", "System flags"},
        {0x5C3D, "ERR_SP", "Error stack pointer"},
        {0x5C4B, "DEST", "Calculator destination"},
        {0x5C4D, "CHANS", "Channel info address"},
        {0x5C51, "CURCHL", "Current channel"},
        {0x5C53, "PROG", "BASIC program start"},
        {0x5C57, "NXTLIN", "Next BASIC line"},
        {0x5C59, "DATADD", "DATA address"},
        {0x5C5B, "E_LINE", "Edit line address"},
        {0x5C5D, "K_CUR", "Cursor address"},
        {0x5C61, "WORKSP", "Workspace address"},
        {0x5C63, "STKBOT", "Calculator stack bottom"},
        {0x5C65, "STKEND", "Calculator stack end"},
        {0x5C8D, "ATTR_P", "Permanent attribute"},
        {0x5C8F, "ATTR_T", "Temporary attribute"},
        {0x5CB2, "RAMTOP", "Top of RAM"},
        {0x5CB4, "P_RAMT", "Physical RAM top"},
    };
    for (const auto& sv : sysvars) {
        if (addr >= sv.addr && addr < sv.addr + 2) {
            return QString("%1: %2").arg(sv.name).arg(sv.desc);
        }
    }
    return "";
}

//=============================================================================
// Theme manager
//=============================================================================
class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance() {
        static ThemeManager inst;
        return inst;
    }

    bool isDark() const { return m_isDark; }

    void setDark(bool dark) {
        if (m_isDark != dark) {
            m_isDark = dark;
            emit themeChanged(dark);
        }
    }

    // Color getters
    QString popupBg() const { return m_isDark ? "#2D2D2D" : "#FFFFFF"; }
    QString popupFg() const { return m_isDark ? "#E0E0E0" : "#333333"; }
    QString popupBorder() const { return m_isDark ? "#555555" : "#CCCCCC"; }
    QString addrColor() const { return m_isDark ? "#4FC3F7" : "#0277BD"; }
    QString regionColor() const { return m_isDark ? "#FFA726" : "#E65100"; }
    QString explainColor() const { return m_isDark ? "#81C784" : "#2E7D32"; }
    QString hintColor() const { return m_isDark ? "#666666" : "#999999"; }
    QString codeColor() const { return m_isDark ? "#81C784" : "#388E3C"; }
    QString dataColor() const { return m_isDark ? "#4FC3F7" : "#1976D2"; }
    QString dimColor() const { return m_isDark ? "#888888" : "#777777"; }
    QString sepColor() const { return m_isDark ? "#555555" : "#DDDDDD"; }

signals:
    void themeChanged(bool isDark);

private:
    ThemeManager() : m_isDark(false) {}
    bool m_isDark = false;
};

//=============================================================================
// Smart content popup - shows disassembly OR hex dump based on content
//=============================================================================
class ContentPopup : public QFrame {
    Q_OBJECT
public:
    ContentPopup(QWidget* parent = nullptr) : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint) {
        setFrameStyle(QFrame::NoFrame);
        setAttribute(Qt::WA_TranslucentBackground, false);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(6);

        // Header row: address + region tag
        auto* headerRow = new QHBoxLayout();
        headerRow->setSpacing(10);
        m_addrLabel = new QLabel();
        m_addrLabel->setFont(g_monoFont);
        headerRow->addWidget(m_addrLabel);
        m_regionTag = new QLabel();
        headerRow->addWidget(m_regionTag);
        headerRow->addStretch();
        layout->addLayout(headerRow);

        // Explanation (system var name, etc.)
        m_explainLabel = new QLabel();
        m_explainLabel->setFont(g_monoFont);
        layout->addWidget(m_explainLabel);

        // Content (disasm or hex) - in a styled container
        m_contentBox = new QFrame();
        m_contentBox->setFrameStyle(QFrame::NoFrame);
        auto* contentLayout = new QVBoxLayout(m_contentBox);
        contentLayout->setContentsMargins(8, 6, 8, 6);
        contentLayout->setSpacing(0);
        m_content = new QLabel();
        m_content->setFont(g_monoFont);
        m_content->setTextFormat(Qt::RichText);
        contentLayout->addWidget(m_content);
        layout->addWidget(m_contentBox);

        // Hint row
        m_hint = new QLabel();
        layout->addWidget(m_hint);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &ContentPopup::applyTheme);
        applyTheme(ThemeManager::instance().isDark());
    }

    void applyTheme(bool) {
        auto& tm = ThemeManager::instance();

        // Main popup - rounded corners, subtle shadow effect via border
        QString bgColor = tm.isDark() ? "#252526" : "#FAFAFA";
        QString borderColor = tm.isDark() ? "#3C3C3C" : "#E0E0E0";
        setStyleSheet(QString(
            "ContentPopup { background: %1; border: 1px solid %2; border-radius: 8px; }"
        ).arg(bgColor).arg(borderColor));

        m_addrLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(tm.addrColor()));

        // Region tag - pill style
        QString tagBg = tm.isDark() ? "#3C3C3C" : "#E8E8E8";
        QString tagFg = tm.isDark() ? "#FFA726" : "#E65100";
        m_regionTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tagBg).arg(tagFg));

        m_explainLabel->setStyleSheet(QString("color: %1; font-style: italic; padding-left: 2px;")
            .arg(tm.explainColor()));

        // Content box - subtle background
        QString contentBg = tm.isDark() ? "#1E1E1E" : "#F5F5F5";
        m_contentBox->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(contentBg));

        m_hint->setStyleSheet(QString("color: %1; padding-top: 4px;").arg(tm.hintColor()));
    }

    void showAt(uint16_t addr, bool isCode, const QPoint& pos) {
        m_addr = addr;

        // Address header
        m_addrLabel->setText(QString("$%1").arg(addr, 4, 16, QChar('0')).toUpper());

        // Region tag
        auto region = identifyRegion(addr);
        m_regionTag->setText(region.name);
        m_regionTag->setToolTip(region.description);

        // Explanation
        if (region.isSystemVar) {
            QString sysvar = getSystemVarName(addr);
            m_explainLabel->setText(sysvar.isEmpty() ? "System variable" : sysvar);
            m_explainLabel->show();
        } else {
            m_explainLabel->hide();
        }

        // Content based on type
        QString content;
        if (isCode || region.isCode) {
            content = generateDisassembly(addr);
            m_hint->setText("Click → disassembly  |  Esc → close");
        } else {
            content = generateHexDump(addr, region);
            m_hint->setText("Click → memory view  |  Esc → close");
        }
        m_content->setText(content);

        adjustSize();

        // Position popup, keep on screen
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

signals:
    void jumpRequested(uint16_t addr);

protected:
    void mousePressEvent(QMouseEvent*) override {
        emit jumpRequested(m_addr);
        hide();
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) hide();
    }

private:
    QString generateDisassembly(uint16_t addr) {
        auto& tm = ThemeManager::instance();
        QString html = "<pre style='margin:0;'>";

        struct { int len; const char* bytes; const char* mnem; const char* args; } instrs[] = {
            {1, "F5", "PUSH", "AF"},
            {1, "C5", "PUSH", "BC"},
            {1, "D5", "PUSH", "DE"},
            {1, "E5", "PUSH", "HL"},
            {3, "3A 00 5C", "LD", "A,(5C00)"},
            {2, "FE 01", "CP", "01"},
            {2, "20 F7", "JR", "NZ,$%1"},
            {1, "C9", "RET", ""},
        };

        uint16_t pc = addr;
        for (int i = 0; i < 8 && i < (int)(sizeof(instrs)/sizeof(instrs[0])); i++) {
            QString args = instrs[i].args;
            if (args.contains("%1")) {
                args = args.arg(pc + instrs[i].len + (int8_t)(-9), 4, 16, QChar('0')).toUpper();
            }

            html += QString("<span style='color:%1;'>%2</span>  ")
                .arg(tm.dimColor())
                .arg(pc, 4, 16, QChar('0')).toUpper();
            html += QString("<span style='color:%1;'>%-10s</span>")
                .arg(tm.dimColor())
                .arg(instrs[i].bytes);
            html += QString("<span style='color:%1;'>%-5s</span>")
                .arg(tm.codeColor())
                .arg(instrs[i].mnem);
            html += QString("<span style='color:%1;'>%2</span>\n")
                .arg(tm.dataColor())
                .arg(args);

            pc += instrs[i].len;
        }
        html += "</pre>";
        return html;
    }

    QString generateHexDump(uint16_t addr, const MemoryRegionInfo& region) {
        auto& tm = ThemeManager::instance();
        QString html = "<pre style='margin:0;'>";

        // Simulated memory content
        uint8_t fakeMem[64];
        srand(addr);  // Deterministic "random" based on address
        for (int i = 0; i < 64; i++) {
            if (region.isSystemVar) {
                fakeMem[i] = (addr + i) & 0xFF;
            } else if (QString(region.name) == "Screen") {
                fakeMem[i] = rand() & 0xFF;
            } else if (QString(region.name) == "Attributes") {
                fakeMem[i] = 0x38 + (rand() & 0x07);
            } else {
                fakeMem[i] = rand() & 0xFF;
            }
        }

        // 4 rows of 16 bytes
        for (int row = 0; row < 4; row++) {
            uint16_t rowAddr = addr + row * 16;
            html += QString("<span style='color:%1;'>%2</span>  ")
                .arg(tm.dimColor())
                .arg(rowAddr, 4, 16, QChar('0')).toUpper();

            // Hex bytes
            for (int col = 0; col < 16; col++) {
                uint8_t b = fakeMem[row * 16 + col];
                html += QString("<span style='color:%1;'>%2</span>")
                    .arg(tm.popupFg())
                    .arg(b, 2, 16, QChar('0')).toUpper();
                html += (col == 7) ? "  " : " ";
            }

            html += QString(" <span style='color:%1;'>").arg(tm.dimColor());
            // ASCII
            for (int col = 0; col < 16; col++) {
                uint8_t b = fakeMem[row * 16 + col];
                char c = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
                html += c;
            }
            html += "</span>\n";
        }
        html += "</pre>";

        // Add interpretation for special regions
        if (QString(region.name) == "Screen") {
            html += QString("<p style='color:%1; font-size:10px; margin:4px 0 0 0;'>"
                    "Screen bitmap: 256x192 pixels, 1 bit per pixel</p>").arg(tm.regionColor());
        } else if (QString(region.name) == "Attributes") {
            html += QString("<p style='color:%1; font-size:10px; margin:4px 0 0 0;'>"
                    "Attributes: INK(0-2), PAPER(3-5), BRIGHT(6), FLASH(7)</p>").arg(tm.regionColor());
        }

        return html;
    }

    uint16_t m_addr = 0;
    QLabel* m_addrLabel;
    QLabel* m_regionTag;
    QLabel* m_explainLabel;
    QFrame* m_contentBox;
    QLabel* m_content;
    QLabel* m_hint;
};

// Backwards compatibility alias
using DisassemblyPopup = ContentPopup;

//=============================================================================
// Layout 1: Simple 4-entry (current unreal-qt style)
//=============================================================================
class StackLayout1 : public QGroupBox {
    Q_OBJECT
public:
    StackLayout1(ContentPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 1: Simple 4-entry", parent), m_popup(popup) {
        auto* layout = new QGridLayout(this);
        layout->setSpacing(2);

        const char* labels[] = {"+0:", "+2:", "+4:", "+6:"};
        for (int i = 0; i < 4; i++) {
            auto* lbl = new QLabel(labels[i]);
            lbl->setStyleSheet("color: #888;");
            layout->addWidget(lbl, i, 0);

            auto* val = new ClickableStackValue(i);
            m_values[i] = val;
            layout->addWidget(val, i, 1);

            connect(val, &ClickableStackValue::clicked, this, &StackLayout1::onValueClicked);
            connect(val, &ClickableStackValue::showDisassemblyPopup, this, [this, i](uint16_t addr, QPoint pos) {
                m_popup->showAt(addr, g_stack[i].looksLikeCode, pos);
            });
        }
    }

    void updateStack() {
        for (int i = 0; i < 4; i++) {
            m_values[i]->setValue(g_stack[i].value, g_stack[i].looksLikeCode, g_stack[i].label);
        }
    }

signals:
    void valueClicked(int index, uint16_t value);

private slots:
    void onValueClicked(int index, uint16_t value) {
        emit valueClicked(index, value);
    }

private:
    ClickableStackValue* m_values[4] = {};
    ContentPopup* m_popup;
};

//=============================================================================
// Layout 2: Extended 9-entry with SP-2 (proposed)
//=============================================================================
class StackLayout2 : public QGroupBox {
    Q_OBJECT
public:
    StackLayout2(ContentPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 2: Extended 9-entry + SP-2", parent), m_popup(popup) {
        auto* layout = new QGridLayout(this);
        layout->setSpacing(1);

        const char* labels[] = {"-2:", "+0:", "+2:", "+4:", "+6:", "+8:", "+A:", "+C:", "+E:"};
        for (int i = 0; i < 9; i++) {
            auto* lbl = new QLabel(labels[i]);
            lbl->setStyleSheet("color: #888; ");
            if (i == 0) lbl->setStyleSheet("color: #FF9800; ");
            layout->addWidget(lbl, i, 0);

            auto* val = new ClickableStackValue(i - 1);
            val->setStyleSheet(val->styleSheet() + "");
            m_values[i] = val;
            layout->addWidget(val, i, 1);

            connect(val, &ClickableStackValue::clicked, this, &StackLayout2::onValueClicked);
            int stackIdx = (i == 0) ? -1 : i - 1;
            connect(val, &ClickableStackValue::showDisassemblyPopup, this, [this, stackIdx](uint16_t addr, QPoint pos) {
                bool isCode = (stackIdx >= 0) ? g_stack[stackIdx].looksLikeCode : false;
                m_popup->showAt(addr, isCode, pos);
            });
        }

        m_values[0]->setStyleSheet("padding: 2px 4px;  background: #FFF3E0;");
    }

    void updateStack() {
        m_values[0]->setValue(0xBEEF, false, "");
        for (int i = 1; i < 9; i++) {
            m_values[i]->setValue(g_stack[i-1].value, g_stack[i-1].looksLikeCode, g_stack[i-1].label);
        }
    }

signals:
    void valueClicked(int index, uint16_t value);

private slots:
    void onValueClicked(int index, uint16_t value) {
        emit valueClicked(index, value);
    }

private:
    ClickableStackValue* m_values[9] = {};
    ContentPopup* m_popup;
};

//=============================================================================
// Layout 3: Table-based with headers
//=============================================================================
class StackLayout3 : public QGroupBox {
    Q_OBJECT
public:
    StackLayout3(ContentPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 3: Table with headers", parent), m_popup(popup) {
        auto* layout = new QVBoxLayout(this);

        m_table = new QTableWidget(8, 3);
        m_table->setHorizontalHeaderLabels({"Offset", "Value", "Label"});
        m_table->verticalHeader()->hide();
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->setColumnWidth(0, 50);
        m_table->setColumnWidth(1, 60);
        m_table->setMaximumHeight(200);
        m_table->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(m_table, &QTableWidget::customContextMenuRequested, this, &StackLayout3::showContextMenu);
        connect(m_table, &QTableWidget::cellDoubleClicked, this, &StackLayout3::onCellDoubleClicked);

        layout->addWidget(m_table);
    }

    void updateStack() {
        const char* offsets[] = {"+0", "+2", "+4", "+6", "+8", "+A", "+C", "+E"};
        for (int i = 0; i < 8; i++) {
            m_table->setItem(i, 0, new QTableWidgetItem(offsets[i]));

            auto* valItem = new QTableWidgetItem(QString("%1").arg(g_stack[i].value, 4, 16, QChar('0')).toUpper());
            if (g_stack[i].looksLikeCode) {
                valItem->setForeground(QColor("#2196F3"));
                QFont f = valItem->font();
                f.setBold(true);
                valItem->setFont(f);
            }
            m_table->setItem(i, 1, valItem);

            m_table->setItem(i, 2, new QTableWidgetItem(g_stack[i].label));
        }
    }

signals:
    void jumpToDisassembly(uint16_t addr);

private slots:
    void showContextMenu(const QPoint& pos) {
        int row = m_table->rowAt(pos.y());
        if (row < 0) return;

        uint16_t value = g_stack[row].value;
        bool isCode = g_stack[row].looksLikeCode;

        QMenu menu(this);
        auto* preview = menu.addAction("Preview...");
        menu.addSeparator();
        auto* jump = menu.addAction("Jump to Disassembly");
        auto* mem = menu.addAction("Show in Memory");

        connect(preview, &QAction::triggered, [this, value, isCode, pos]() {
            m_popup->showAt(value, isCode, m_table->viewport()->mapToGlobal(pos));
        });
        connect(jump, &QAction::triggered, [this, value]() { emit jumpToDisassembly(value); });

        menu.exec(m_table->viewport()->mapToGlobal(pos));
    }

    void onCellDoubleClicked(int row, int) {
        emit jumpToDisassembly(g_stack[row].value);
    }

private:
    QTableWidget* m_table;
    ContentPopup* m_popup;
};

//=============================================================================
// Layout 4: Horizontal compact
//=============================================================================
class StackLayout4 : public QGroupBox {
    Q_OBJECT
public:
    StackLayout4(ContentPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 4: Horizontal compact", parent), m_popup(popup) {
        auto* layout = new QHBoxLayout(this);
        layout->setSpacing(4);

        auto* spLabel = new QLabel("SP→");
        spLabel->setStyleSheet("font-weight: bold;");
        layout->addWidget(spLabel);

        for (int i = 0; i < 6; i++) {
            auto* val = new ClickableStackValue(i);
            val->setFixedWidth(45);
            m_values[i] = val;
            layout->addWidget(val);

            connect(val, &ClickableStackValue::showDisassemblyPopup, this, [this, i](uint16_t addr, QPoint pos) {
                m_popup->showAt(addr, g_stack[i].looksLikeCode, pos);
            });

            if (i < 5) {
                auto* sep = new QLabel("|");
                sep->setStyleSheet("color: #CCC;");
                layout->addWidget(sep);
            }
        }

        auto* more = new QLabel("...");
        more->setStyleSheet("color: #888;");
        layout->addWidget(more);
        layout->addStretch();
    }

    void updateStack() {
        for (int i = 0; i < 6; i++) {
            m_values[i]->setValue(g_stack[i].value, g_stack[i].looksLikeCode, g_stack[i].label);
        }
    }

private:
    ClickableStackValue* m_values[6] = {};
    ContentPopup* m_popup;
};

//=============================================================================
// Layout 5: With inline disassembly preview on hover
//=============================================================================
class StackLayout5 : public QGroupBox {
    Q_OBJECT
public:
    StackLayout5(ContentPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 5: Hover preview (hold 1s)", parent), m_popup(popup) {
        auto* layout = new QGridLayout(this);
        layout->setSpacing(2);

        const char* labels[] = {"+0:", "+2:", "+4:", "+6:", "+8:", "+A:"};
        for (int i = 0; i < 6; i++) {
            auto* lbl = new QLabel(labels[i]);
            lbl->setStyleSheet("color: #888;");
            layout->addWidget(lbl, i, 0);

            auto* val = new ClickableStackValue(i);
            m_values[i] = val;
            layout->addWidget(val, i, 1);

            val->installEventFilter(this);

            connect(val, &ClickableStackValue::showDisassemblyPopup, this, [this, i](uint16_t addr, QPoint pos) {
                m_popup->showAt(addr, g_stack[i].looksLikeCode, pos);
            });
        }

        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setSingleShot(true);
        connect(m_hoverTimer, &QTimer::timeout, this, &StackLayout5::showHoverPopup);
    }

    void updateStack() {
        for (int i = 0; i < 6; i++) {
            m_values[i]->setValue(g_stack[i].value, g_stack[i].looksLikeCode, g_stack[i].label);
        }
    }

protected:
    bool eventFilter(QObject* obj, QEvent* e) override {
        auto* val = qobject_cast<ClickableStackValue*>(obj);
        if (!val) return false;

        if (e->type() == QEvent::Enter) {
            m_hoverWidget = val;
            m_hoverTimer->start(1000);
        } else if (e->type() == QEvent::Leave) {
            m_hoverTimer->stop();
            m_hoverWidget = nullptr;
        }
        return false;
    }

private slots:
    void showHoverPopup() {
        if (m_hoverWidget) {
            for (int i = 0; i < 6; i++) {
                if (m_values[i] == m_hoverWidget) {
                    m_popup->showAt(g_stack[i].value, g_stack[i].looksLikeCode,
                        m_hoverWidget->mapToGlobal(QPoint(m_hoverWidget->width() + 5, 0)));
                    break;
                }
            }
        }
    }

private:
    ClickableStackValue* m_values[6] = {};
    ContentPopup* m_popup;
    QTimer* m_hoverTimer;
    ClickableStackValue* m_hoverWidget = nullptr;
};

//=============================================================================
// Layout 6: Collapsible with call stack analysis
//=============================================================================
class StackLayout6 : public QGroupBox {
    Q_OBJECT
public:
    StackLayout6(ContentPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 6: Call stack analysis", parent), m_popup(popup) {
        auto* layout = new QVBoxLayout(this);

        // Call stack (filtered to code addresses only)
        auto* callLabel = new QLabel("Call Stack:");
        callLabel->setStyleSheet("font-weight: bold;");
        layout->addWidget(callLabel);

        m_callList = new QVBoxLayout();
        m_callList->setSpacing(2);
        layout->addLayout(m_callList);

        // Raw stack toggle
        m_rawToggle = new QPushButton("Show Raw Stack ▼");
        m_rawToggle->setFlat(true);
        m_rawToggle->setStyleSheet("text-align: left; color: #666;");
        connect(m_rawToggle, &QPushButton::clicked, this, &StackLayout6::toggleRaw);
        layout->addWidget(m_rawToggle);

        // Raw stack (hidden by default)
        m_rawWidget = new QWidget();
        m_rawLayout = new QGridLayout(m_rawWidget);
        m_rawLayout->setSpacing(1);
        m_rawWidget->hide();
        layout->addWidget(m_rawWidget);

        layout->addStretch();

        // Hover timer for popup
        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setSingleShot(true);
        connect(m_hoverTimer, &QTimer::timeout, this, &StackLayout6::showHoverPopup);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* e) override {
        auto* lbl = qobject_cast<QLabel*>(obj);
        if (!lbl) return false;

        if (e->type() == QEvent::Enter) {
            m_hoverWidget = lbl;
            m_hoverTimer->start(1000);
        } else if (e->type() == QEvent::Leave) {
            m_hoverTimer->stop();
            m_hoverWidget = nullptr;
        }
        return false;
    }

private slots:
    void showHoverPopup() {
        if (m_hoverWidget) {
            int idx = m_hoverWidget->property("stackIndex").toInt();
            m_popup->showAt(g_stack[idx].value, g_stack[idx].looksLikeCode,
                m_hoverWidget->mapToGlobal(QPoint(m_hoverWidget->width() + 5, 0)));
        }
    }

public:
    void updateStack() {
        // Clear call list
        QLayoutItem* item;
        while ((item = m_callList->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }

        // Build call stack (code addresses only)
        int frame = 0;
        for (int i = 0; i < 8; i++) {
            if (g_stack[i].looksLikeCode) {
                QString text = QString("#%1: $%2").arg(frame).arg(g_stack[i].value, 4, 16, QChar('0')).toUpper();
                if (!g_stack[i].label.isEmpty()) {
                    text += " (" + g_stack[i].label + ")";
                }
                auto* lbl = new QLabel(text);
                lbl->setStyleSheet("color: #2196F3; padding: 2px;");
                lbl->setCursor(Qt::PointingHandCursor);
                lbl->setProperty("stackIndex", i);
                lbl->installEventFilter(this);
                m_callList->addWidget(lbl);
                frame++;
            }
        }

        if (frame == 0) {
            auto* empty = new QLabel("(no return addresses found)");
            empty->setStyleSheet("color: #888; font-style: italic;");
            m_callList->addWidget(empty);
        }

        // Update raw stack
        // Clear existing
        while ((item = m_rawLayout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }

        const char* offsets[] = {"+0", "+2", "+4", "+6", "+8", "+A", "+C", "+E"};
        for (int i = 0; i < 8; i++) {
            auto* off = new QLabel(offsets[i]);
            off->setStyleSheet("color: #888; ");
            m_rawLayout->addWidget(off, i, 0);

            auto* val = new QLabel(QString("%1").arg(g_stack[i].value, 4, 16, QChar('0')).toUpper());
            val->setStyleSheet("");
            val->setCursor(Qt::PointingHandCursor);
            val->setProperty("stackIndex", i);
            val->installEventFilter(this);
            m_rawLayout->addWidget(val, i, 1);
        }
    }

    void toggleRaw() {
        m_rawVisible = !m_rawVisible;
        m_rawWidget->setVisible(m_rawVisible);
        m_rawToggle->setText(m_rawVisible ? "Hide Raw Stack ▲" : "Show Raw Stack ▼");
    }

private:
    QVBoxLayout* m_callList;
    QPushButton* m_rawToggle;
    QWidget* m_rawWidget;
    QGridLayout* m_rawLayout;
    ContentPopup* m_popup;
    QTimer* m_hoverTimer;
    QLabel* m_hoverWidget = nullptr;
    bool m_rawVisible = false;
};

//=============================================================================
// Layout 7: Call stack vs Push-fill with mode selector
//=============================================================================
class StackLayout7 : public QGroupBox {
    Q_OBJECT
public:
    StackLayout7(ContentPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 7: Stack pattern view", parent), m_popup(popup) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(4);

        // Mode selector
        auto* modeRow = new QHBoxLayout();
        m_radioCall = new QRadioButton("CALL stack");
        m_radioPush = new QRadioButton("PUSH-fill");
        m_radioCall->setChecked(true);
        connect(m_radioCall, &QRadioButton::toggled, this, [this](bool) { updateStack(); });
        connect(m_radioPush, &QRadioButton::toggled, this, [this](bool) { updateStack(); });
        modeRow->addWidget(m_radioCall);
        modeRow->addWidget(m_radioPush);
        modeRow->addStretch();
        layout->addLayout(modeRow);

        // Description
        m_descLabel = new QLabel();
        m_descLabel->setWordWrap(true);
        m_descLabel->setStyleSheet(" padding: 4px; border-radius: 3px;");
        layout->addWidget(m_descLabel);

        // Stack entries with execution order
        auto* grid = new QGridLayout();
        grid->setSpacing(2);

        for (int i = 0; i < 6; i++) {
            m_orderLabels[i] = new QLabel();
            m_orderLabels[i]->setFixedWidth(24);
            m_orderLabels[i]->setAlignment(Qt::AlignCenter);
            grid->addWidget(m_orderLabels[i], i, 0);

            auto* val = new ClickableStackValue(i);
            m_values[i] = val;
            grid->addWidget(val, i, 1);

            connect(val, &ClickableStackValue::showDisassemblyPopup, this, [this, i](uint16_t addr, QPoint pos) {
                m_popup->showAt(addr, g_stack[i].looksLikeCode, pos);
            });

            m_hintLabels[i] = new QLabel();
            m_hintLabels[i]->setStyleSheet("");
            grid->addWidget(m_hintLabels[i], i, 2);
        }
        layout->addLayout(grid);
    }

    void updateStack() {
        bool isPushFill = m_radioPush->isChecked();

        if (isPushFill) {
            m_descLabel->setText("RET chain: each RET pops & jumps to next address");
            m_descLabel->setStyleSheet(" padding: 4px; border-radius: 3px; "
                "background: #FCE4EC; color: #880E4F;");
        } else {
            m_descLabel->setText("Normal calls: return addresses + saved regs/data");
            m_descLabel->setStyleSheet(" padding: 4px; border-radius: 3px; "
                "background: #E8F5E9; color: #1B5E20;");
        }

        int execOrder = 1;
        for (int i = 0; i < 6; i++) {
            m_values[i]->setValue(g_stack[i].value, g_stack[i].looksLikeCode, g_stack[i].label);

            if (isPushFill && g_stack[i].looksLikeCode) {
                m_orderLabels[i]->setText(QString("#%1").arg(execOrder++));
                m_orderLabels[i]->setStyleSheet("font-weight: bold; color: #E91E63;");
                m_hintLabels[i]->setText("→ RET here");
                m_hintLabels[i]->setStyleSheet(" color: #E91E63;");
            } else if (!isPushFill && g_stack[i].looksLikeCode) {
                m_orderLabels[i]->setText("◀");
                m_orderLabels[i]->setStyleSheet("font-weight: bold; color: #4CAF50;");
                m_hintLabels[i]->setText("ret addr");
                m_hintLabels[i]->setStyleSheet(" color: #4CAF50;");
            } else {
                m_orderLabels[i]->setText("");
                m_hintLabels[i]->setText(isPushFill ? "" : "data");
                m_hintLabels[i]->setStyleSheet(" color: #999;");
            }
        }
    }

private:
    ClickableStackValue* m_values[6] = {};
    QLabel* m_orderLabels[6] = {};
    QLabel* m_hintLabels[6] = {};
    QRadioButton* m_radioCall;
    QRadioButton* m_radioPush;
    QLabel* m_descLabel;
    ContentPopup* m_popup;
};

//=============================================================================
// Layout 8: Inline annotations (register hints)
//=============================================================================
class StackLayout8 : public QGroupBox {
    Q_OBJECT
public:
    StackLayout8(ContentPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 8: With annotations", parent), m_popup(popup) {
        auto* grid = new QGridLayout(this);
        grid->setSpacing(4);
        grid->setColumnMinimumWidth(0, 35);  // offset column
        grid->setColumnMinimumWidth(1, 55);  // value column
        grid->setColumnStretch(2, 1);        // annotation stretches

        for (int i = 0; i < 6; i++) {
            auto* idx = new QLabel(QString("+%1:").arg(i * 2));
            idx->setFont(g_monoFont);
            idx->setStyleSheet("color: #666;");
            idx->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            grid->addWidget(idx, i, 0);

            auto* val = new ClickableStackValue(i);
            m_values[i] = val;
            grid->addWidget(val, i, 1);

            connect(val, &ClickableStackValue::showDisassemblyPopup, this, [this, i](uint16_t addr, QPoint pos) {
                m_popup->showAt(addr, g_stack[i].looksLikeCode, pos);
            });

            m_annotations[i] = new QLabel();
            m_annotations[i]->setFont(g_monoFont);
            m_annotations[i]->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            grid->addWidget(m_annotations[i], i, 2);
        }
    }

    void updateStack() {
        for (int i = 0; i < 6; i++) {
            m_values[i]->setValue(g_stack[i].value, g_stack[i].looksLikeCode, g_stack[i].label);

            QString ann;
            QString style = "font-weight: bold; ";

            if (!g_stack[i].label.isEmpty()) {
                ann = g_stack[i].label;
                style += "color: #1565C0;";
            } else if (g_stack[i].looksLikeCode) {
                ann = "← return addr";
                style += "color: #2E7D32;";
            } else if (i == 1) {
                ann = "saved BC?";
                style += "color: #F57C00;";
            } else if (i == 2) {
                ann = "saved DE?";
                style += "color: #F57C00;";
            } else if (i == 3) {
                ann = "saved HL?";
                style += "color: #F57C00;";
            } else {
                ann = "";
            }

            m_annotations[i]->setText(ann);
            m_annotations[i]->setStyleSheet(style);
        }
    }

private:
    ClickableStackValue* m_values[6] = {};
    QLabel* m_annotations[6] = {};
    ContentPopup* m_popup;
};

//=============================================================================
// Layout 9: Minimal inline (single row)
//=============================================================================
class StackLayout9 : public QGroupBox {
    Q_OBJECT
public:
    StackLayout9(ContentPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 9: Single row minimal", parent), m_popup(popup) {
        auto* layout = new QHBoxLayout(this);
        layout->setSpacing(2);

        auto* spLbl = new QLabel("SP:");
        spLbl->setStyleSheet("font-weight: bold;");
        layout->addWidget(spLbl);

        for (int i = 0; i < 4; i++) {
            auto* val = new ClickableStackValue(i);
            val->setFixedWidth(40);
            m_values[i] = val;
            layout->addWidget(val);

            connect(val, &ClickableStackValue::showDisassemblyPopup, this, [this, i](uint16_t addr, QPoint pos) {
                m_popup->showAt(addr, g_stack[i].looksLikeCode, pos);
            });
        }

        auto* more = new QLabel("...");
        more->setStyleSheet("color: #888;");
        layout->addWidget(more);
        layout->addStretch();
    }

    void updateStack() {
        for (int i = 0; i < 4; i++) {
            m_values[i]->setValue(g_stack[i].value, g_stack[i].looksLikeCode, g_stack[i].label);
        }
    }

private:
    ClickableStackValue* m_values[4] = {};
    ContentPopup* m_popup;
};

//=============================================================================
// Main window
//=============================================================================
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow() {
        setWindowTitle("Stack Widget POC - Layout Comparison");

        auto* mainLayout = new QHBoxLayout(this);

        // Shared popup for all layouts
        m_popup = new ContentPopup(this);

        // Left column
        auto* leftCol = new QVBoxLayout();

        // Theme radio buttons and SP display
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
            applyTheme(id == 1);
        });
        topBar->addWidget(lightRadio);
        topBar->addWidget(darkRadio);

        topBar->addWidget(new QLabel("SP:"));
        m_spValue = new ClickableStackValue(-1);
        m_spValue->setValue(g_sp, false, "Stack Pointer");
        connect(m_spValue, &ClickableStackValue::showDisassemblyPopup, this, [this](uint16_t addr, QPoint pos) {
            m_popup->showAt(addr, false, pos);
        });
        topBar->addWidget(m_spValue);

        auto* randomBtn = new QPushButton("Randomize");
        connect(randomBtn, &QPushButton::clicked, this, &MainWindow::randomizeStack);
        topBar->addWidget(randomBtn);

        topBar->addStretch();
        leftCol->addLayout(topBar);

        m_layout1 = new StackLayout1(m_popup);
        m_layout2 = new StackLayout2(m_popup);
        m_layout3 = new StackLayout3(m_popup);

        leftCol->addWidget(m_layout1);
        leftCol->addWidget(m_layout2);
        leftCol->addWidget(m_layout3);
        leftCol->addStretch();

        mainLayout->addLayout(leftCol);

        // Right column
        auto* rightCol = new QVBoxLayout();

        m_layout4 = new StackLayout4(m_popup);
        m_layout5 = new StackLayout5(m_popup);
        m_layout6 = new StackLayout6(m_popup);

        rightCol->addWidget(m_layout4);
        rightCol->addWidget(m_layout5);
        rightCol->addWidget(m_layout6);
        rightCol->addStretch();

        mainLayout->addLayout(rightCol);

        // Third column with more layouts
        auto* thirdCol = new QVBoxLayout();

        m_layout7 = new StackLayout7(m_popup);
        m_layout8 = new StackLayout8(m_popup);
        m_layout9 = new StackLayout9(m_popup);

        thirdCol->addWidget(m_layout7);
        thirdCol->addWidget(m_layout8);
        thirdCol->addWidget(m_layout9);
        thirdCol->addStretch();

        mainLayout->addLayout(thirdCol);

        updateAll();
    }

private slots:
    void randomizeStack() {
        g_sp = 0xFF00 - (rand() % 256) * 2;

        // 50% chance of push-fill pattern
        bool pushFillMode = (rand() % 2 == 0);

        if (pushFillMode) {
            // Push-fill: consecutive code addresses for RET chain
            uint16_t baseAddr = 0x8000 + (rand() % 0x2000);
            const char* labels[] = {"draw_line", "draw_pixel", "next_row", "clear_attr", "update_score", "play_sfx"};
            for (int i = 0; i < 6; i++) {
                g_stack[i].value = baseAddr + i * 0x100 + (rand() % 0x80);
                g_stack[i].looksLikeCode = true;
                g_stack[i].label = labels[i];
            }
            for (int i = 6; i < 16; i++) {
                g_stack[i].value = rand() & 0xFFFF;
                g_stack[i].looksLikeCode = false;
                g_stack[i].label = "";
            }
        } else {
            // Regular call stack
            for (int i = 0; i < 16; i++) {
                g_stack[i].value = rand() & 0xFFFF;
                g_stack[i].looksLikeCode = (g_stack[i].value >= 0x4000 && g_stack[i].value < 0xC000 && (rand() % 3 == 0));
                g_stack[i].label = "";
            }
            if (g_stack[0].looksLikeCode) g_stack[0].label = "caller";
            if (g_stack[2].looksLikeCode) g_stack[2].label = "main";
        }

        updateAll();
    }

private:
    void updateAll() {
        m_spValue->setValue(g_sp, false, "Stack Pointer");
        m_layout1->updateStack();
        m_layout2->updateStack();
        m_layout3->updateStack();
        m_layout4->updateStack();
        m_layout5->updateStack();
        m_layout6->updateStack();
        m_layout7->updateStack();
        m_layout8->updateStack();
        m_layout9->updateStack();
    }

    void applyTheme(bool dark) {
        QString windowStyle = dark
            ? "background-color: #1E1E1E; color: #D4D4D4;"
            : "background-color: #FFFFFF; color: #1E1E1E;";
        setStyleSheet(windowStyle);
    }

    ClickableStackValue* m_spValue;
    ContentPopup* m_popup;
    StackLayout1* m_layout1;
    StackLayout2* m_layout2;
    StackLayout3* m_layout3;
    StackLayout4* m_layout4;
    StackLayout5* m_layout5;
    StackLayout6* m_layout6;
    StackLayout7* m_layout7;
    StackLayout8* m_layout8;
    StackLayout9* m_layout9;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    initMonoFont();

    MainWindow w;
    w.setFont(g_monoFont);
    w.resize(1100, 750);
    w.show();

    return app.exec();
}

#include "main.moc"
