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
#include <QMenu>
#include <QMouseEvent>
#include <QTimer>
#include <QTableWidget>
#include <QHeaderView>
#include <QFontDatabase>
#include <QScreen>
#include <QPainter>
#include <QDialog>
#include <QScrollArea>

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

    // Bank colors
    QString romColor() const { return m_isDark ? "#CE9178" : "#A31515"; }
    QString ramColor() const { return m_isDark ? "#4EC9B0" : "#267F99"; }
    QString screenColor() const { return m_isDark ? "#DCDCAA" : "#795E26"; }
    QString lockedColor() const { return m_isDark ? "#808080" : "#6E6E6E"; }

signals:
    void themeChanged(bool isDark);

private:
    ThemeManager() : m_isDark(false) {}
    bool m_isDark = false;
};

//=============================================================================
// Memory model definitions
//=============================================================================
enum class BankType { ROM, RAM, Screen, Locked };

struct BankInfo {
    BankType type;
    int bankNum;          // Logical bank number (for display)
    int physicalPage;     // Physical 16KB page (0-255 for up to 4MB)
    QString name;
    QString description;
    bool isContended;
    bool isReadOnly;
};

struct SlotMapping {
    uint16_t startAddr;
    uint16_t endAddr;
    BankInfo bank;
};

struct MemoryModel {
    QString name;
    QString description;
    int totalRamKB;
    int romPages;
    int ramPages;
    SlotMapping memSlots[4];
};

// Current state
static int g_currentModel = 1;  // 128K
static int g_currentPage = 0;
static int g_currentRom = 0;
static bool g_screenInSlot1 = false;
static bool g_allRamMode = false;

static MemoryModel g_models[] = {
    {
        "ZX Spectrum 48K", "Original 48K model",
        48, 1, 3,
        {
            {0x0000, 0x3FFF, {BankType::ROM, 0, 0, "ROM 0", "48K BASIC ROM", false, true}},
            {0x4000, 0x7FFF, {BankType::Screen, 5, 5, "RAM 5", "Screen memory (contended)", true, false}},
            {0x8000, 0xBFFF, {BankType::RAM, 2, 2, "RAM 2", "Contended RAM", true, false}},
            {0xC000, 0xFFFF, {BankType::RAM, 0, 0, "RAM 0", "Upper RAM", false, false}},
        }
    },
    {
        "ZX Spectrum 128K", "128K with memory paging",
        128, 2, 8,
        {
            {0x0000, 0x3FFF, {BankType::ROM, 0, 0, "ROM 0", "128K Editor ROM", false, true}},
            {0x4000, 0x7FFF, {BankType::Screen, 5, 5, "RAM 5", "Screen memory (contended)", true, false}},
            {0x8000, 0xBFFF, {BankType::RAM, 2, 2, "RAM 2", "Contended RAM", true, false}},
            {0xC000, 0xFFFF, {BankType::RAM, 0, 0, "RAM 0", "Pageable bank (port 7FFD)", false, false}},
        }
    },
    {
        "Pentagon 512K", "Pentagon with extended RAM",
        512, 2, 32,
        {
            {0x0000, 0x3FFF, {BankType::ROM, 0, 0, "ROM 0", "Pentagon ROM", false, true}},
            {0x4000, 0x7FFF, {BankType::RAM, 5, 5, "RAM 5", "Screen memory", false, false}},
            {0x8000, 0xBFFF, {BankType::RAM, 2, 2, "RAM 2", "Middle RAM", false, false}},
            {0xC000, 0xFFFF, {BankType::RAM, 0, 0, "RAM 0", "Pageable (ports 7FFD+EFF7)", false, false}},
        }
    },
    {
        "Scorpion 256K", "Scorpion with profROM",
        256, 4, 16,
        {
            {0x0000, 0x3FFF, {BankType::ROM, 0, 0, "ROM 0", "Service ROM", false, true}},
            {0x4000, 0x7FFF, {BankType::RAM, 5, 5, "RAM 5", "Screen memory", false, false}},
            {0x8000, 0xBFFF, {BankType::RAM, 2, 2, "RAM 2", "Middle RAM", false, false}},
            {0xC000, 0xFFFF, {BankType::RAM, 0, 0, "RAM 0", "Pageable bank", false, false}},
        }
    },
    {
        "ATM Turbo 2+ (4MB)", "ATM with max RAM",
        4096, 4, 256,
        {
            {0x0000, 0x3FFF, {BankType::ROM, 0, 0, "ROM 0", "ATM System ROM", false, true}},
            {0x4000, 0x7FFF, {BankType::RAM, 5, 5, "RAM 5", "Screen memory", false, false}},
            {0x8000, 0xBFFF, {BankType::RAM, 2, 2, "RAM 2", "Middle RAM", false, false}},
            {0xC000, 0xFFFF, {BankType::RAM, 0, 0, "RAM 0", "Pageable (0-255)", false, false}},
        }
    },
};

static const int g_numModels = sizeof(g_models) / sizeof(g_models[0]);

//=============================================================================
// Popup for bank details
//=============================================================================
class BankPopup : public QFrame {
    Q_OBJECT
public:
    BankPopup(QWidget* parent = nullptr) : QFrame(parent, Qt::ToolTip | Qt::FramelessWindowHint) {
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

        // Address range
        m_addrLabel = new QLabel();
        m_addrLabel->setFont(g_monoFont);
        layout->addWidget(m_addrLabel);

        // Details box
        m_detailsBox = new QFrame();
        m_detailsBox->setFrameStyle(QFrame::NoFrame);
        auto* detailsLayout = new QVBoxLayout(m_detailsBox);
        detailsLayout->setContentsMargins(8, 6, 8, 6);
        detailsLayout->setSpacing(2);
        m_details = new QLabel();
        m_details->setFont(g_monoFont);
        detailsLayout->addWidget(m_details);
        layout->addWidget(m_detailsBox);

        // Hint
        m_hint = new QLabel("Click → show in memory | Esc → close");
        layout->addWidget(m_hint);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &BankPopup::applyTheme);
        applyTheme(ThemeManager::instance().isDark());
    }

    void applyTheme(bool) {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString(
            "BankPopup { background: %1; border: 1px solid %2; border-radius: 8px; }"
        ).arg(tm.popupBg()).arg(tm.popupBorder()));

        m_typeTag->setStyleSheet(QString(
            "background: %1; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tm.tagBg()));

        m_detailsBox->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(tm.contentBg()));
        m_hint->setStyleSheet(QString("color: %1; padding-top: 4px;").arg(tm.hintColor()));
    }

    void showAt(const SlotMapping& slot, const QPoint& pos) {
        auto& tm = ThemeManager::instance();
        auto& model = g_models[g_currentModel];

        // Determine slot index (0-3)
        int slotIdx = slot.startAddr / 0x4000;

        m_titleLabel->setText(slot.bank.name);
        QString typeColor;
        QString typeName;
        switch (slot.bank.type) {
            case BankType::ROM:
                typeName = "ROM";
                typeColor = tm.romColor();
                break;
            case BankType::RAM:
            case BankType::Screen:
                typeName = slot.bank.type == BankType::Screen ? "SCREEN" : "RAM";
                typeColor = slot.bank.type == BankType::Screen ? tm.screenColor() : tm.ramColor();
                break;
            case BankType::Locked:
                typeName = "LOCKED";
                typeColor = tm.lockedColor();
                break;
        }
        m_titleLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(typeColor));
        m_typeTag->setText(typeName);
        m_typeTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tm.tagBg()).arg(typeColor));

        m_addrLabel->setText(QString("Z80 Slot %1").arg(slotIdx));

        // Calculate physical address from page number
        uint32_t physBase = slot.bank.physicalPage * 0x4000;

        QString details;
        details += QString("Logical: $%1 - $%2\n")
            .arg(slot.startAddr, 4, 16, QChar('0')).toUpper()
            .arg(slot.endAddr, 4, 16, QChar('0')).toUpper();

        if (slot.bank.type == BankType::ROM) {
            details += QString("Page:    ROM %1 of %2\n")
                .arg(slot.bank.physicalPage)
                .arg(model.romPages);
            details += QString("Offset:  $%1\n")
                .arg(physBase, 6, 16, QChar('0')).toUpper();
        } else {
            details += QString("Page:    RAM %1 of %2\n")
                .arg(slot.bank.physicalPage)
                .arg(model.ramPages);
            details += QString("Offset:  $%1 - $%2\n")
                .arg(physBase, 6, 16, QChar('0')).toUpper()
                .arg(physBase + 0x3FFF, 6, 16, QChar('0')).toUpper();
        }

        QStringList attrs;
        if (slot.bank.isReadOnly) attrs << "Read-Only";
        if (slot.bank.isContended) attrs << "Contended";
        if (slot.bank.type == BankType::Screen) attrs << "Display";
        if (!attrs.isEmpty()) {
            details += QString("Flags:   %1").arg(attrs.join(", "));
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

protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) hide();
    }

private:
    QLabel* m_titleLabel;
    QLabel* m_typeTag;
    QLabel* m_addrLabel;
    QFrame* m_detailsBox;
    QLabel* m_details;
    QLabel* m_hint;
};

//=============================================================================
// Clickable slot widget
//=============================================================================
class ClickableSlot : public QFrame {
    Q_OBJECT
public:
    ClickableSlot(int slotIndex, QWidget* parent = nullptr)
        : QFrame(parent), m_slotIndex(slotIndex) {
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setFrameStyle(QFrame::Box);
        setMinimumHeight(50);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 4, 8, 4);
        layout->setSpacing(2);

        m_addrLabel = new QLabel();
        m_addrLabel->setFont(g_monoFont);
        m_addrLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_addrLabel);

        m_bankLabel = new QLabel();
        m_bankLabel->setFont(g_monoFont);
        m_bankLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_bankLabel);

        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setSingleShot(true);
        connect(m_hoverTimer, &QTimer::timeout, this, [this]() {
            emit showPopup(m_slotIndex, m_lastMousePos);
        });
    }

    void setMapping(const SlotMapping& mapping) {
        m_mapping = mapping;

        m_addrLabel->setText(QString("$%1-$%2")
            .arg(mapping.startAddr, 4, 16, QChar('0')).toUpper()
            .arg(mapping.endAddr, 4, 16, QChar('0')).toUpper());

        m_bankLabel->setText(mapping.bank.name);

        applyStyle();
    }

    const SlotMapping& mapping() const { return m_mapping; }

    void applyStyle() {
        auto& tm = ThemeManager::instance();
        QString bgColor, fgColor, borderColor;

        switch (m_mapping.bank.type) {
            case BankType::ROM:
                bgColor = tm.isDark() ? "#3D2626" : "#FFEBEE";
                fgColor = tm.romColor();
                borderColor = tm.romColor();
                break;
            case BankType::Screen:
                bgColor = tm.isDark() ? "#3D3D26" : "#FFF8E1";
                fgColor = tm.screenColor();
                borderColor = tm.screenColor();
                break;
            case BankType::RAM:
                bgColor = tm.isDark() ? "#263D3D" : "#E0F7FA";
                fgColor = tm.ramColor();
                borderColor = tm.ramColor();
                break;
            case BankType::Locked:
                bgColor = tm.isDark() ? "#2D2D2D" : "#EEEEEE";
                fgColor = tm.lockedColor();
                borderColor = tm.lockedColor();
                break;
        }

        setStyleSheet(QString(
            "ClickableSlot { background: %1; border: 2px solid %2; border-radius: 6px; }"
            "ClickableSlot:hover { border-width: 3px; }"
        ).arg(bgColor).arg(borderColor));

        m_addrLabel->setStyleSheet(QString("color: %1;").arg(tm.isDark() ? "#AAAAAA" : "#666666"));
        m_bankLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(fgColor));
    }

signals:
    void showPopup(int slotIndex, QPoint pos);
    void hidePopup();
    void clicked(int slotIndex);

protected:
    void enterEvent(QEnterEvent* e) override {
        m_lastMousePos = mapToGlobal(e->position().toPoint());
        emit hidePopup();  // Hide any existing popup first
        m_hoverTimer->start(400);
    }

    void leaveEvent(QEvent*) override {
        m_hoverTimer->stop();
        emit hidePopup();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        m_lastMousePos = e->globalPosition().toPoint();
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            emit clicked(m_slotIndex);
        }
    }

private:
    int m_slotIndex;
    SlotMapping m_mapping;
    QLabel* m_addrLabel;
    QLabel* m_bankLabel;
    QTimer* m_hoverTimer;
    QPoint m_lastMousePos;
};

//=============================================================================
// Layout 1: Vertical stack (classic view)
//=============================================================================
class Layout1 : public QGroupBox {
    Q_OBJECT
public:
    Layout1(BankPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 1: Vertical stack", parent), m_popup(popup) {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(4);

        for (int i = 3; i >= 0; i--) {  // Top to bottom: high to low addresses
            auto* slot = new ClickableSlot(i);
            m_slots[i] = slot;
            layout->addWidget(slot);
            connect(slot, &ClickableSlot::showPopup, this, &Layout1::onShowPopup);
            connect(slot, &ClickableSlot::hidePopup, m_popup, &QWidget::hide);
        }
    }

    void updateMapping() {
        auto& model = g_models[g_currentModel];
        for (int i = 0; i < 4; i++) {
            m_slots[i]->setMapping(model.memSlots[i]);
        }
    }

private slots:
    void onShowPopup(int idx, QPoint pos) {
        m_popup->showAt(m_slots[idx]->mapping(), pos);
    }

private:
    ClickableSlot* m_slots[4];
    BankPopup* m_popup;
};

//=============================================================================
// Layout 2: Horizontal bar
//=============================================================================
class Layout2 : public QGroupBox {
    Q_OBJECT
public:
    Layout2(BankPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 2: Horizontal bar", parent), m_popup(popup) {
        auto* layout = new QHBoxLayout(this);
        layout->setSpacing(4);

        for (int i = 0; i < 4; i++) {
            auto* slot = new ClickableSlot(i);
            slot->setMinimumWidth(100);
            m_slots[i] = slot;
            layout->addWidget(slot);
            connect(slot, &ClickableSlot::showPopup, this, &Layout2::onShowPopup);
            connect(slot, &ClickableSlot::hidePopup, m_popup, &QWidget::hide);
        }
    }

    void updateMapping() {
        auto& model = g_models[g_currentModel];
        for (int i = 0; i < 4; i++) {
            m_slots[i]->setMapping(model.memSlots[i]);
        }
    }

private slots:
    void onShowPopup(int idx, QPoint pos) {
        m_popup->showAt(m_slots[idx]->mapping(), pos);
    }

private:
    ClickableSlot* m_slots[4];
    BankPopup* m_popup;
};

//=============================================================================
// Simple popup for physical page info
//=============================================================================
class PagePopup : public QFrame {
    Q_OBJECT
public:
    static PagePopup& instance() {
        static PagePopup popup;
        return popup;
    }

    void showAt(BankType type, int pageNum, const QPoint& pos) {
        auto& tm = ThemeManager::instance();

        uint32_t offset = pageNum * 0x4000;
        QString typeStr = (type == BankType::ROM) ? "ROM" : "RAM";
        QString color = (type == BankType::ROM) ? tm.romColor() : tm.ramColor();

        // Title with type tag
        m_titleLabel->setText(QString("%1 Page %2").arg(typeStr).arg(pageNum));
        m_titleLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(color));

        QString tagBg = tm.isDark() ? "#3C3C3C" : "#E8E8E8";
        m_typeTag->setText(typeStr);
        m_typeTag->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 8px; border-radius: 10px; font-weight: bold;"
        ).arg(tagBg).arg(color));

        // Details
        m_details->setText(QString(
            "Offset: $%1\n"
            "Size:   16 KB"
        ).arg(offset, 6, 16, QChar('0')).toUpper());

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
    PagePopup() : QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint) {
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

        // Details in subtle box
        m_detailsBox = new QFrame();
        m_detailsBox->setFrameStyle(QFrame::NoFrame);
        auto* detailsLayout = new QVBoxLayout(m_detailsBox);
        detailsLayout->setContentsMargins(8, 6, 8, 6);
        m_details = new QLabel();
        m_details->setFont(g_monoFont);
        detailsLayout->addWidget(m_details);
        layout->addWidget(m_detailsBox);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &PagePopup::applyTheme);
        applyTheme();
    }

    void applyTheme() {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString(
            "PagePopup { background: %1; border: 1px solid %2; border-radius: 8px; }"
        ).arg(tm.popupBg()).arg(tm.popupBorder()));
        m_detailsBox->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(tm.contentBg()));
    }

    QLabel* m_titleLabel;
    QLabel* m_typeTag;
    QFrame* m_detailsBox;
    QLabel* m_details;
};

//=============================================================================
// Layout 3: Grid with bank pool
//=============================================================================
class BankChip : public QLabel {
    Q_OBJECT
public:
    BankChip(BankType type, int num, QWidget* parent = nullptr)
        : QLabel(parent), m_type(type), m_num(num), m_mapped(false) {
        setFont(g_monoFont);
        setAlignment(Qt::AlignCenter);
        setFixedSize(50, 30);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setText(type == BankType::ROM ? QString("R%1").arg(num) : QString("P%1").arg(num));
        applyStyle();

        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setSingleShot(true);
        connect(m_hoverTimer, &QTimer::timeout, this, [this]() {
            PagePopup::instance().showAt(m_type, m_num, m_lastMousePos + QPoint(15, 15));
        });
    }

    void setMapped(bool mapped) {
        m_mapped = mapped;
        applyStyle();
    }

    void applyStyle() {
        auto& tm = ThemeManager::instance();
        QString bg, fg, border;

        if (m_type == BankType::ROM) {
            fg = tm.romColor();
            bg = m_mapped ? (tm.isDark() ? "#3D2626" : "#FFCDD2") : (tm.isDark() ? "#2D2D2D" : "#FAFAFA");
            border = m_mapped ? tm.romColor() : (tm.isDark() ? "#444" : "#CCC");
        } else {
            fg = tm.ramColor();
            bg = m_mapped ? (tm.isDark() ? "#263D3D" : "#B2EBF2") : (tm.isDark() ? "#2D2D2D" : "#FAFAFA");
            border = m_mapped ? tm.ramColor() : (tm.isDark() ? "#444" : "#CCC");
        }

        setStyleSheet(QString(
            "background: %1; color: %2; border: 2px solid %3; border-radius: 4px; font-weight: %4;"
        ).arg(bg).arg(fg).arg(border).arg(m_mapped ? "bold" : "normal"));
    }

protected:
    void enterEvent(QEnterEvent* e) override {
        m_lastMousePos = mapToGlobal(e->position().toPoint());
        PagePopup::instance().hide();
        m_hoverTimer->start(400);
    }

    void leaveEvent(QEvent*) override {
        m_hoverTimer->stop();
        PagePopup::instance().hide();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        m_lastMousePos = e->globalPosition().toPoint();
    }

private:
    BankType m_type;
    int m_num;
    bool m_mapped;
    QTimer* m_hoverTimer;
    QPoint m_lastMousePos;
};

class Layout3 : public QGroupBox {
    Q_OBJECT
public:
    Layout3(BankPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 3: Bank pool + slots", parent), m_popup(popup) {
        auto* mainLayout = new QVBoxLayout(this);

        // Bank pool
        m_poolBox = new QGroupBox("Physical Banks");
        m_poolLayout = new QGridLayout(m_poolBox);
        m_poolLayout->setSpacing(4);
        mainLayout->addWidget(m_poolBox);

        // Slots
        auto* slotsLayout = new QHBoxLayout();
        for (int i = 0; i < 4; i++) {
            auto* slot = new ClickableSlot(i);
            m_slots[i] = slot;
            slotsLayout->addWidget(slot);
            connect(slot, &ClickableSlot::showPopup, this, &Layout3::onShowPopup);
            connect(slot, &ClickableSlot::hidePopup, m_popup, &QWidget::hide);
        }
        mainLayout->addLayout(slotsLayout);
    }

    void updateMapping() {
        auto& model = g_models[g_currentModel];

        // Clear existing chips
        qDeleteAll(m_romChips);
        qDeleteAll(m_ramChips);
        m_romChips.clear();
        m_ramChips.clear();
        if (m_moreLabel) { delete m_moreLabel; m_moreLabel = nullptr; }

        // Remove all items from grid
        QLayoutItem* item;
        while ((item = m_poolLayout->takeAt(0)) != nullptr) {
            delete item;
        }

        const int CHIPS_PER_ROW = 8;
        int col = 0, row = 0;

        // Create ROM chips on first row(s)
        if (model.romPages > 0) {
            auto* romLabel = new QLabel("ROM:");
            romLabel->setStyleSheet("color: #888; font-weight: bold;");
            m_poolLayout->addWidget(romLabel, row, col++);

            for (int i = 0; i < model.romPages; i++) {
                auto* chip = new BankChip(BankType::ROM, i);
                m_romChips.append(chip);
                m_poolLayout->addWidget(chip, row, col++);
                if (col >= CHIPS_PER_ROW) { col = 0; row++; }
            }
        }

        // Start RAM on new row
        if (model.ramPages > 0) {
            if (model.romPages > 0) { row++; }
            col = 0;

            auto* ramLabel = new QLabel("RAM:");
            ramLabel->setStyleSheet("color: #888; font-weight: bold;");
            m_poolLayout->addWidget(ramLabel, row, col++);

            // Create RAM chips (limit display to 32, show "..." for more)
            int displayPages = qMin(model.ramPages, 32);
            for (int i = 0; i < displayPages; i++) {
                auto* chip = new BankChip(BankType::RAM, i);
                m_ramChips.append(chip);
                m_poolLayout->addWidget(chip, row, col++);
                if (col >= CHIPS_PER_ROW) { col = 1; row++; }  // col=1 to skip label column
            }

            if (model.ramPages > 32) {
                m_moreLabel = new QPushButton(QString("+%1").arg(model.ramPages - 32));
                m_moreLabel->setStyleSheet("color: #2196F3; background: transparent; border: none; font-weight: bold; text-decoration: underline;");
                m_moreLabel->setCursor(Qt::PointingHandCursor);
                connect(m_moreLabel, &QPushButton::clicked, this, &Layout3::showAllPagesDialog);
                m_poolLayout->addWidget(m_moreLabel, row, col);
            }
        }

        // Update slots and mark mapped banks
        for (int i = 0; i < 4; i++) {
            m_slots[i]->setMapping(model.memSlots[i]);

            auto& bank = model.memSlots[i].bank;
            if (bank.type == BankType::ROM && bank.bankNum < m_romChips.size()) {
                m_romChips[bank.bankNum]->setMapped(true);
            } else if (bank.type != BankType::ROM && bank.bankNum < m_ramChips.size()) {
                m_ramChips[bank.bankNum]->setMapped(true);
            }
        }
    }

private slots:
    void onShowPopup(int idx, QPoint pos) {
        m_popup->showAt(m_slots[idx]->mapping(), pos);
    }

    void showAllPagesDialog() {
        auto& model = g_models[g_currentModel];

        QDialog dialog(this);
        dialog.setWindowTitle(QString("%1 - All %2 RAM Pages").arg(model.name).arg(model.ramPages));
        dialog.setModal(true);

        auto* layout = new QVBoxLayout(&dialog);

        // Use scroll area for many pages
        auto* scrollArea = new QScrollArea();
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        auto* container = new QWidget();
        auto* grid = new QGridLayout(container);
        grid->setSpacing(3);

        const int COLS = 16;
        for (int i = 0; i < model.ramPages; i++) {
            // Check if this page is currently mapped
            bool mapped = false;
            for (int s = 0; s < 4; s++) {
                if (model.memSlots[s].bank.type != BankType::ROM &&
                    model.memSlots[s].bank.physicalPage == i) {
                    mapped = true;
                    break;
                }
            }

            auto* chip = new BankChip(BankType::RAM, i);
            chip->setFixedSize(40, 26);
            chip->setMapped(mapped);
            grid->addWidget(chip, i / COLS, i % COLS);
        }

        scrollArea->setWidget(container);
        layout->addWidget(scrollArea);

        auto* closeBtn = new QPushButton("Close");
        connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
        layout->addWidget(closeBtn);

        dialog.resize(700, 500);
        dialog.exec();
    }

private:
    ClickableSlot* m_slots[4];
    QList<BankChip*> m_romChips;
    QList<BankChip*> m_ramChips;
    QGroupBox* m_poolBox;
    QGridLayout* m_poolLayout;
    QPushButton* m_moreLabel = nullptr;
    BankPopup* m_popup;
};

//=============================================================================
// Layout 4: Table view
//=============================================================================
class Layout4 : public QGroupBox {
    Q_OBJECT
public:
    Layout4(BankPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 4: Table view", parent), m_popup(popup) {
        auto* layout = new QVBoxLayout(this);

        m_table = new QTableWidget(4, 4);
        m_table->setHorizontalHeaderLabels({"Slot", "Address", "Bank", "Type"});
        m_table->verticalHeader()->hide();
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->setMaximumHeight(160);
        m_table->setFont(g_monoFont);

        connect(m_table, &QTableWidget::cellEntered, this, &Layout4::onCellEntered);
        m_table->setMouseTracking(true);

        layout->addWidget(m_table);

        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setSingleShot(true);
        connect(m_hoverTimer, &QTimer::timeout, this, &Layout4::showHoverPopup);
    }

    void updateMapping() {
        auto& model = g_models[g_currentModel];
        auto& tm = ThemeManager::instance();

        for (int i = 0; i < 4; i++) {
            auto& slot = model.memSlots[i];

            m_table->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
            m_table->setItem(i, 1, new QTableWidgetItem(
                QString("$%1-$%2").arg(slot.startAddr, 4, 16, QChar('0')).toUpper()
                                  .arg(slot.endAddr, 4, 16, QChar('0')).toUpper()));
            m_table->setItem(i, 2, new QTableWidgetItem(slot.bank.name));

            QString typeStr;
            QColor typeColor;
            switch (slot.bank.type) {
                case BankType::ROM: typeStr = "ROM"; typeColor = QColor(tm.romColor()); break;
                case BankType::Screen: typeStr = "SCREEN"; typeColor = QColor(tm.screenColor()); break;
                case BankType::RAM: typeStr = "RAM"; typeColor = QColor(tm.ramColor()); break;
                case BankType::Locked: typeStr = "LOCKED"; typeColor = QColor(tm.lockedColor()); break;
            }
            auto* typeItem = new QTableWidgetItem(typeStr);
            typeItem->setForeground(typeColor);
            m_table->setItem(i, 3, typeItem);
        }
    }

private slots:
    void onCellEntered(int row, int) {
        m_hoverRow = row;
        m_hoverTimer->start(400);
    }

    void showHoverPopup() {
        if (m_hoverRow >= 0 && m_hoverRow < 4) {
            auto& model = g_models[g_currentModel];
            QPoint pos = QCursor::pos() + QPoint(15, 15);
            m_popup->showAt(model.memSlots[m_hoverRow], pos);
        }
    }

private:
    QTableWidget* m_table;
    BankPopup* m_popup;
    QTimer* m_hoverTimer;
    int m_hoverRow = -1;
};

//=============================================================================
// Layout 5: Compact inline
//=============================================================================
class Layout5 : public QGroupBox {
    Q_OBJECT
public:
    Layout5(BankPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 5: Compact inline", parent), m_popup(popup) {
        auto* layout = new QHBoxLayout(this);
        layout->setSpacing(2);

        const char* slotNames[] = {"0:", "1:", "2:", "3:"};
        for (int i = 0; i < 4; i++) {
            auto* lbl = new QLabel(slotNames[i]);
            lbl->setFont(g_monoFont);
            lbl->setStyleSheet("color: #888;");
            layout->addWidget(lbl);

            m_labels[i] = new QLabel();
            m_labels[i]->setFont(g_monoFont);
            m_labels[i]->setCursor(Qt::PointingHandCursor);
            m_labels[i]->setMouseTracking(true);
            m_labels[i]->installEventFilter(this);
            layout->addWidget(m_labels[i]);

            if (i < 3) {
                auto* sep = new QLabel("|");
                sep->setStyleSheet("color: #888;");
                layout->addWidget(sep);
            }
        }
        layout->addStretch();

        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setSingleShot(true);
        connect(m_hoverTimer, &QTimer::timeout, this, &Layout5::showHoverPopup);
    }

    void updateMapping() {
        auto& model = g_models[g_currentModel];
        auto& tm = ThemeManager::instance();

        for (int i = 0; i < 4; i++) {
            auto& slot = model.memSlots[i];
            m_labels[i]->setText(slot.bank.name);

            QString color;
            switch (slot.bank.type) {
                case BankType::ROM: color = tm.romColor(); break;
                case BankType::Screen: color = tm.screenColor(); break;
                case BankType::RAM: color = tm.ramColor(); break;
                case BankType::Locked: color = tm.lockedColor(); break;
            }
            m_labels[i]->setStyleSheet(QString("color: %1; font-weight: bold;").arg(color));
        }
    }

protected:
    bool eventFilter(QObject* obj, QEvent* e) override {
        for (int i = 0; i < 4; i++) {
            if (obj == m_labels[i]) {
                if (e->type() == QEvent::Enter) {
                    m_hoverIdx = i;
                    m_hoverTimer->start(400);
                } else if (e->type() == QEvent::Leave) {
                    m_hoverTimer->stop();
                }
                break;
            }
        }
        return false;
    }

private slots:
    void showHoverPopup() {
        if (m_hoverIdx >= 0) {
            auto& model = g_models[g_currentModel];
            m_popup->showAt(model.memSlots[m_hoverIdx], QCursor::pos() + QPoint(15, 15));
        }
    }

private:
    QLabel* m_labels[4];
    BankPopup* m_popup;
    QTimer* m_hoverTimer;
    int m_hoverIdx = -1;
};

//=============================================================================
// Layout 6: Visual memory map with proportions
//=============================================================================
class MemoryBar : public QWidget {
    Q_OBJECT
public:
    MemoryBar(BankPopup* popup, QWidget* parent = nullptr) : QWidget(parent), m_popup(popup) {
        setMinimumHeight(60);
        setMouseTracking(true);

        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setSingleShot(true);
        connect(m_hoverTimer, &QTimer::timeout, this, [this]() {
            if (m_hoverSlot >= 0) {
                auto& model = g_models[g_currentModel];
                m_popup->showAt(model.memSlots[m_hoverSlot], m_lastMousePos + QPoint(15, 15));
            }
        });
    }

    void updateMapping() { update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        auto& model = g_models[g_currentModel];
        auto& tm = ThemeManager::instance();

        int slotWidth = width() / 4;
        int h = height() - 20;

        for (int i = 0; i < 4; i++) {
            QRect r(i * slotWidth + 2, 10, slotWidth - 4, h);

            QColor fillColor, borderColor;
            switch (model.memSlots[i].bank.type) {
                case BankType::ROM:
                    fillColor = tm.isDark() ? QColor("#3D2626") : QColor("#FFEBEE");
                    borderColor = QColor(tm.romColor());
                    break;
                case BankType::Screen:
                    fillColor = tm.isDark() ? QColor("#3D3D26") : QColor("#FFF8E1");
                    borderColor = QColor(tm.screenColor());
                    break;
                case BankType::RAM:
                    fillColor = tm.isDark() ? QColor("#263D3D") : QColor("#E0F7FA");
                    borderColor = QColor(tm.ramColor());
                    break;
                default:
                    fillColor = tm.isDark() ? QColor("#2D2D2D") : QColor("#EEEEEE");
                    borderColor = QColor(tm.lockedColor());
            }

            p.setBrush(fillColor);
            int borderWidth = (i == m_hoverSlot) ? 3 : 2;
            p.setPen(QPen(borderColor, borderWidth));
            p.drawRoundedRect(r, 4, 4);

            // Labels
            p.setFont(g_monoFont);
            p.setPen(borderColor);
            p.drawText(r, Qt::AlignCenter, model.memSlots[i].bank.name);

            // Address at bottom
            p.setPen(tm.isDark() ? QColor("#888") : QColor("#666"));
            QFont small = g_monoFont;
            small.setPointSize(9);
            p.setFont(small);
            p.drawText(r.adjusted(0, 0, 0, -5), Qt::AlignBottom | Qt::AlignHCenter,
                QString("$%1").arg(model.memSlots[i].startAddr, 4, 16, QChar('0')).toUpper());
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        int slotWidth = width() / 4;
        int newSlot = qBound(0, (int)(e->position().x() / slotWidth), 3);
        m_lastMousePos = e->globalPosition().toPoint();

        if (newSlot != m_hoverSlot) {
            m_hoverSlot = newSlot;
            m_popup->hide();
            m_hoverTimer->start(400);
            update();
        }
    }

    void leaveEvent(QEvent*) override {
        m_hoverSlot = -1;
        m_hoverTimer->stop();
        m_popup->hide();
        update();
    }

private:
    BankPopup* m_popup;
    QTimer* m_hoverTimer;
    int m_hoverSlot = -1;
    QPoint m_lastMousePos;
};

class Layout6 : public QGroupBox {
    Q_OBJECT
public:
    Layout6(BankPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 6: Visual bar", parent) {
        auto* layout = new QVBoxLayout(this);
        m_bar = new MemoryBar(popup);
        layout->addWidget(m_bar);
    }

    void updateMapping() { m_bar->updateMapping(); }

private:
    MemoryBar* m_bar;
};

//=============================================================================
// Layout 7: Paging controls
//=============================================================================
class Layout7 : public QGroupBox {
    Q_OBJECT
public:
    Layout7(BankPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 7: With paging controls", parent), m_popup(popup) {
        auto* mainLayout = new QVBoxLayout(this);

        // Paging controls
        auto* ctrlRow = new QHBoxLayout();
        ctrlRow->addWidget(new QLabel("Page in C000:"));
        m_pageCombo = new QComboBox();
        for (int i = 0; i < 8; i++) {
            m_pageCombo->addItem(QString("RAM %1").arg(i));
        }
        connect(m_pageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &Layout7::onPageChanged);
        ctrlRow->addWidget(m_pageCombo);

        ctrlRow->addSpacing(20);
        ctrlRow->addWidget(new QLabel("ROM:"));
        m_romCombo = new QComboBox();
        m_romCombo->addItem("ROM 0 (128K)");
        m_romCombo->addItem("ROM 1 (48K)");
        connect(m_romCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &Layout7::onRomChanged);
        ctrlRow->addWidget(m_romCombo);
        ctrlRow->addStretch();

        mainLayout->addLayout(ctrlRow);

        // Slots
        auto* slotsLayout = new QHBoxLayout();
        for (int i = 0; i < 4; i++) {
            auto* slot = new ClickableSlot(i);
            m_slots[i] = slot;
            slotsLayout->addWidget(slot);
            connect(slot, &ClickableSlot::showPopup, this, &Layout7::onShowPopup);
            connect(slot, &ClickableSlot::hidePopup, m_popup, &QWidget::hide);
        }
        mainLayout->addLayout(slotsLayout);
    }

    void updateMapping() {
        auto& model = g_models[g_currentModel];
        for (int i = 0; i < 4; i++) {
            SlotMapping slot = model.memSlots[i];

            // Apply current paging selections
            if (i == 0 && slot.bank.type == BankType::ROM) {
                slot.bank.bankNum = m_romCombo->currentIndex();
                slot.bank.name = QString("ROM %1").arg(slot.bank.bankNum);
            }
            if (i == 3) {
                slot.bank.bankNum = m_pageCombo->currentIndex();
                slot.bank.name = QString("RAM %1").arg(slot.bank.bankNum);
            }

            m_slots[i]->setMapping(slot);
        }
    }

signals:
    void mappingChanged();

private slots:
    void onShowPopup(int idx, QPoint pos) {
        m_popup->showAt(m_slots[idx]->mapping(), pos);
    }

    void onPageChanged(int) { updateMapping(); emit mappingChanged(); }
    void onRomChanged(int) { updateMapping(); emit mappingChanged(); }

private:
    ClickableSlot* m_slots[4];
    QComboBox* m_pageCombo;
    QComboBox* m_romCombo;
    BankPopup* m_popup;
};

//=============================================================================
// Layout 8: Address breakdown
//=============================================================================
class Layout8 : public QGroupBox {
    Q_OBJECT
public:
    Layout8(BankPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 8: Address breakdown", parent), m_popup(popup) {
        auto* layout = new QGridLayout(this);
        layout->setSpacing(4);

        // Headers
        QStringList headers = {"Slot", "Logical", "Physical", "Bank", "Status"};
        for (int i = 0; i < headers.size(); i++) {
            auto* h = new QLabel(headers[i]);
            h->setFont(g_monoFont);
            h->setStyleSheet("font-weight: bold; color: #888;");
            layout->addWidget(h, 0, i);
        }

        // Rows
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 5; j++) {
                m_cells[i][j] = new QLabel();
                m_cells[i][j]->setFont(g_monoFont);
                m_cells[i][j]->setCursor(Qt::PointingHandCursor);
                m_cells[i][j]->installEventFilter(this);
                layout->addWidget(m_cells[i][j], i + 1, j);
            }
        }

        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setSingleShot(true);
        connect(m_hoverTimer, &QTimer::timeout, this, &Layout8::showHoverPopup);
    }

    void updateMapping() {
        auto& model = g_models[g_currentModel];
        auto& tm = ThemeManager::instance();

        for (int i = 0; i < 4; i++) {
            auto& slot = model.memSlots[i];

            m_cells[i][0]->setText(QString::number(i));
            m_cells[i][1]->setText(QString("$%1-$%2")
                .arg(slot.startAddr, 4, 16, QChar('0')).toUpper()
                .arg(slot.endAddr, 4, 16, QChar('0')).toUpper());
            m_cells[i][2]->setText(QString("Page %1").arg(slot.bank.physicalPage));
            m_cells[i][3]->setText(slot.bank.name);

            QString status = slot.bank.isContended ? "⚠ Contended" : "Normal";
            m_cells[i][4]->setText(status);
            m_cells[i][4]->setStyleSheet(slot.bank.isContended ?
                QString("color: %1;").arg(tm.screenColor()) : "");

            QString color;
            switch (slot.bank.type) {
                case BankType::ROM: color = tm.romColor(); break;
                case BankType::Screen: color = tm.screenColor(); break;
                case BankType::RAM: color = tm.ramColor(); break;
                default: color = tm.lockedColor();
            }
            m_cells[i][3]->setStyleSheet(QString("color: %1; font-weight: bold;").arg(color));
        }
    }

protected:
    bool eventFilter(QObject* obj, QEvent* e) override {
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 5; j++) {
                if (obj == m_cells[i][j]) {
                    if (e->type() == QEvent::Enter) {
                        m_hoverRow = i;
                        m_hoverTimer->start(400);
                    } else if (e->type() == QEvent::Leave) {
                        m_hoverTimer->stop();
                    }
                    return false;
                }
            }
        }
        return false;
    }

private slots:
    void showHoverPopup() {
        if (m_hoverRow >= 0) {
            auto& model = g_models[g_currentModel];
            m_popup->showAt(model.memSlots[m_hoverRow], QCursor::pos() + QPoint(15, 15));
        }
    }

private:
    QLabel* m_cells[4][5];
    BankPopup* m_popup;
    QTimer* m_hoverTimer;
    int m_hoverRow = -1;
};

//=============================================================================
// Layout 9: Vertical visual stack (like Layout 6 but vertical)
//=============================================================================
class VerticalMemoryBar : public QWidget {
    Q_OBJECT
public:
    VerticalMemoryBar(BankPopup* popup, QWidget* parent = nullptr) : QWidget(parent), m_popup(popup) {
        setMinimumWidth(120);
        setMinimumHeight(200);
        setMouseTracking(true);

        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setSingleShot(true);
        connect(m_hoverTimer, &QTimer::timeout, this, [this]() {
            if (m_hoverSlot >= 0) {
                auto& model = g_models[g_currentModel];
                m_popup->showAt(model.memSlots[m_hoverSlot], m_lastMousePos + QPoint(15, 15));
            }
        });
    }

    void updateMapping() { update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        auto& model = g_models[g_currentModel];
        auto& tm = ThemeManager::instance();

        int slotHeight = height() / 4;
        int w = width() - 10;

        // Draw from top (high addresses) to bottom (low addresses)
        for (int i = 0; i < 4; i++) {
            int slotIdx = 3 - i;  // Reverse order: FFFF at top, 0000 at bottom
            QRect r(5, i * slotHeight + 2, w, slotHeight - 4);

            QColor fillColor, borderColor;
            switch (model.memSlots[slotIdx].bank.type) {
                case BankType::ROM:
                    fillColor = tm.isDark() ? QColor("#3D2626") : QColor("#FFEBEE");
                    borderColor = QColor(tm.romColor());
                    break;
                case BankType::Screen:
                    fillColor = tm.isDark() ? QColor("#3D3D26") : QColor("#FFF8E1");
                    borderColor = QColor(tm.screenColor());
                    break;
                case BankType::RAM:
                    fillColor = tm.isDark() ? QColor("#263D3D") : QColor("#E0F7FA");
                    borderColor = QColor(tm.ramColor());
                    break;
                default:
                    fillColor = tm.isDark() ? QColor("#2D2D2D") : QColor("#EEEEEE");
                    borderColor = QColor(tm.lockedColor());
            }

            p.setBrush(fillColor);
            int borderWidth = (slotIdx == m_hoverSlot) ? 3 : 2;
            p.setPen(QPen(borderColor, borderWidth));
            p.drawRoundedRect(r, 6, 6);

            // Address range on left
            p.setFont(g_monoFont);
            p.setPen(tm.isDark() ? QColor("#888") : QColor("#666"));
            QFont small = g_monoFont;
            small.setPointSize(9);
            p.setFont(small);
            QString addrRange = QString("$%1\n$%2")
                .arg(model.memSlots[slotIdx].endAddr, 4, 16, QChar('0')).toUpper()
                .arg(model.memSlots[slotIdx].startAddr, 4, 16, QChar('0')).toUpper();

            // Bank name in center
            p.setFont(g_monoFont);
            p.setPen(borderColor);
            p.drawText(r, Qt::AlignCenter, model.memSlots[slotIdx].bank.name);

            // Address on left edge
            p.setPen(tm.isDark() ? QColor("#666") : QColor("#999"));
            p.setFont(small);
            p.drawText(r.adjusted(4, 2, 0, -2), Qt::AlignLeft | Qt::AlignTop,
                QString("$%1").arg(model.memSlots[slotIdx].endAddr, 4, 16, QChar('0')).toUpper());
            p.drawText(r.adjusted(4, 2, 0, -2), Qt::AlignLeft | Qt::AlignBottom,
                QString("$%1").arg(model.memSlots[slotIdx].startAddr, 4, 16, QChar('0')).toUpper());
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        int slotHeight = height() / 4;
        int visualSlot = qBound(0, (int)(e->position().y() / slotHeight), 3);
        int newSlot = 3 - visualSlot;  // Reverse mapping
        m_lastMousePos = e->globalPosition().toPoint();

        if (newSlot != m_hoverSlot) {
            m_hoverSlot = newSlot;
            m_popup->hide();
            m_hoverTimer->start(400);
            update();  // Repaint for hover border
        }
    }

    void leaveEvent(QEvent*) override {
        m_hoverSlot = -1;
        m_hoverTimer->stop();
        m_popup->hide();
        update();  // Repaint to remove hover border
    }

private:
    BankPopup* m_popup;
    QTimer* m_hoverTimer;
    int m_hoverSlot = -1;
    QPoint m_lastMousePos;
};

class Layout9 : public QGroupBox {
    Q_OBJECT
public:
    Layout9(BankPopup* popup, QWidget* parent = nullptr)
        : QGroupBox("Layout 9: Vertical stack", parent) {
        auto* layout = new QHBoxLayout(this);
        m_bar = new VerticalMemoryBar(popup);
        layout->addWidget(m_bar);
        layout->addStretch();
    }

    void updateMapping() { m_bar->updateMapping(); }

private:
    VerticalMemoryBar* m_bar;
};

//=============================================================================
// Main window
//=============================================================================
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow() {
        setWindowTitle("Memory Mapping POC - Layout Comparison");

        auto* mainLayout = new QVBoxLayout(this);

        // Top bar
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

        topBar->addWidget(new QLabel("Model:"));
        m_modelCombo = new QComboBox();
        for (int i = 0; i < g_numModels; i++) {
            m_modelCombo->addItem(g_models[i].name);
        }
        m_modelCombo->setCurrentIndex(g_currentModel);
        connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onModelChanged);
        topBar->addWidget(m_modelCombo);

        topBar->addStretch();
        mainLayout->addLayout(topBar);

        // Shared popup
        m_popup = new BankPopup(this);

        // Layouts in columns
        auto* columns = new QHBoxLayout();

        auto* col1 = new QVBoxLayout();
        m_layout1 = new Layout1(m_popup);
        m_layout2 = new Layout2(m_popup);
        m_layout3 = new Layout3(m_popup);
        col1->addWidget(m_layout1);
        col1->addWidget(m_layout2);
        col1->addWidget(m_layout3);
        col1->addStretch();
        columns->addLayout(col1);

        auto* col2 = new QVBoxLayout();
        m_layout4 = new Layout4(m_popup);
        m_layout5 = new Layout5(m_popup);
        m_layout6 = new Layout6(m_popup);
        col2->addWidget(m_layout4);
        col2->addWidget(m_layout5);
        col2->addWidget(m_layout6);
        col2->addStretch();
        columns->addLayout(col2);

        auto* col3 = new QVBoxLayout();
        m_layout7 = new Layout7(m_popup);
        m_layout8 = new Layout8(m_popup);
        m_layout9 = new Layout9(m_popup);
        col3->addWidget(m_layout7);
        col3->addWidget(m_layout8);
        col3->addWidget(m_layout9);
        col3->addStretch();
        columns->addLayout(col3);

        mainLayout->addLayout(columns);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](bool) {
            updateAll();
        });

        updateAll();
    }

private slots:
    void onModelChanged(int idx) {
        g_currentModel = idx;
        updateAll();
    }

private:
    void updateAll() {
        m_layout1->updateMapping();
        m_layout2->updateMapping();
        m_layout3->updateMapping();
        m_layout4->updateMapping();
        m_layout5->updateMapping();
        m_layout6->updateMapping();
        m_layout7->updateMapping();
        m_layout8->updateMapping();
        m_layout9->updateMapping();
    }

    void applyTheme(bool dark) {
        auto& tm = ThemeManager::instance();
        setStyleSheet(QString("background-color: %1; color: %2;").arg(tm.bg()).arg(tm.fg()));
    }

    QComboBox* m_modelCombo;
    BankPopup* m_popup;
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
    w.setFont(g_monoFont);
    w.resize(1200, 800);
    w.show();

    return app.exec();
}

#include "main.moc"
