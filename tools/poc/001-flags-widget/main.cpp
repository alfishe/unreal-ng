#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QToolTip>
#include <QStyle>
#include <QPainter>
#include <QMouseEvent>

// Flag definitions
struct FlagDef {
    char letter;
    const char* name;
    const char* tooltip;
    int bit;
};

static const FlagDef FLAGS[] = {
    {'S', "Sign",      "Sign flag (bit 7) - Set if result is negative", 7},
    {'Z', "Zero",      "Zero flag (bit 6) - Set if result is zero", 6},
    {'-', nullptr,     nullptr, 5},  // Unused bit 5
    {'H', "Half",      "Half-carry flag (bit 4) - BCD half-carry", 4},
    {'-', nullptr,     nullptr, 3},  // Unused bit 3
    {'P', "Parity/V",  "Parity/Overflow flag (bit 2)", 2},
    {'N', "Subtract",  "Subtract flag (bit 1) - Set after subtraction", 1},
    {'C', "Carry",     "Carry flag (bit 0) - Set on carry/borrow", 0},
};

//=============================================================================
// Layout 1: Checkboxes with labels above
//=============================================================================
class FlagsLayout1 : public QGroupBox {
    Q_OBJECT
public:
    FlagsLayout1(QWidget* parent = nullptr) : QGroupBox("Layout 1: Checkboxes + Labels", parent) {
        auto* layout = new QGridLayout(this);
        layout->setSpacing(2);

        for (int i = 0; i < 8; i++) {
            const auto& f = FLAGS[i];

            auto* label = new QLabel(QString(f.letter));
            label->setAlignment(Qt::AlignCenter);
            label->setStyleSheet("font-weight: bold; font-family: monospace;");
            layout->addWidget(label, 0, i);

            if (f.name) {
                auto* cb = new QCheckBox();
                cb->setToolTip(f.tooltip);
                cb->setChecked(i == 0 || i == 7);  // S and C set for demo
                connect(cb, &QCheckBox::toggled, this, [this, i](bool checked) {
                    emit flagToggled(FLAGS[i].bit, checked);
                });
                m_checkboxes[i] = cb;
                layout->addWidget(cb, 1, i, Qt::AlignCenter);
            } else {
                auto* dash = new QLabel("-");
                dash->setAlignment(Qt::AlignCenter);
                dash->setStyleSheet("color: gray;");
                layout->addWidget(dash, 1, i, Qt::AlignCenter);
                m_checkboxes[i] = nullptr;
            }
        }
    }

    void setFlags(uint8_t f) {
        for (int i = 0; i < 8; i++) {
            if (m_checkboxes[i]) {
                m_checkboxes[i]->blockSignals(true);
                m_checkboxes[i]->setChecked(f & (1 << FLAGS[i].bit));
                m_checkboxes[i]->blockSignals(false);
            }
        }
    }

signals:
    void flagToggled(int bit, bool value);

private:
    QCheckBox* m_checkboxes[8] = {};
};

//=============================================================================
// Layout 2: Compact toggle buttons (circular indicators)
//=============================================================================
class ToggleIndicator : public QWidget {
    Q_OBJECT
public:
    ToggleIndicator(char letter, const char* tooltip, QWidget* parent = nullptr)
        : QWidget(parent), m_letter(letter), m_checked(false), m_enabled(tooltip != nullptr) {
        setFixedSize(24, 24);
        setCursor(m_enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (tooltip) setToolTip(tooltip);
    }

    void setChecked(bool checked) {
        if (m_checked != checked) {
            m_checked = checked;
            update();
        }
    }

    bool isChecked() const { return m_checked; }

signals:
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRect r = rect().adjusted(2, 2, -2, -2);

        if (!m_enabled) {
            p.setPen(Qt::gray);
            p.drawText(rect(), Qt::AlignCenter, QString(m_letter));
            return;
        }

        // Draw circle
        if (m_checked) {
            p.setBrush(QColor("#4CAF50"));  // Green when set
            p.setPen(QColor("#388E3C"));
        } else {
            p.setBrush(QColor("#E0E0E0"));  // Gray when clear
            p.setPen(QColor("#9E9E9E"));
        }
        p.drawEllipse(r);

        // Draw letter
        p.setPen(m_checked ? Qt::white : Qt::darkGray);
        QFont f = font();
        f.setBold(true);
        f.setPointSize(10);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, QString(m_letter));
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (m_enabled && e->button() == Qt::LeftButton) {
            m_checked = !m_checked;
            update();
            emit toggled(m_checked);
        }
    }

private:
    char m_letter;
    bool m_checked;
    bool m_enabled;
};

class FlagsLayout2 : public QGroupBox {
    Q_OBJECT
public:
    FlagsLayout2(QWidget* parent = nullptr) : QGroupBox("Layout 2: Circular Indicators", parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setSpacing(4);

        auto* label = new QLabel("Flags:");
        layout->addWidget(label);

        for (int i = 0; i < 8; i++) {
            const auto& f = FLAGS[i];
            auto* ind = new ToggleIndicator(f.letter, f.tooltip);
            ind->setChecked(i == 0 || i == 7);
            connect(ind, &ToggleIndicator::toggled, this, [this, i](bool checked) {
                emit flagToggled(FLAGS[i].bit, checked);
            });
            m_indicators[i] = ind;
            layout->addWidget(ind);
        }

        layout->addStretch();
    }

    void setFlags(uint8_t f) {
        for (int i = 0; i < 8; i++) {
            m_indicators[i]->setChecked(f & (1 << FLAGS[i].bit));
        }
    }

signals:
    void flagToggled(int bit, bool value);

private:
    ToggleIndicator* m_indicators[8] = {};
};

//=============================================================================
// Layout 3: Toggle buttons with text
//=============================================================================
class FlagsLayout3 : public QGroupBox {
    Q_OBJECT
public:
    FlagsLayout3(QWidget* parent = nullptr) : QGroupBox("Layout 3: Toggle Buttons", parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setSpacing(2);

        for (int i = 0; i < 8; i++) {
            const auto& f = FLAGS[i];

            if (f.name) {
                auto* btn = new QPushButton(QString(f.letter));
                btn->setCheckable(true);
                btn->setChecked(i == 0 || i == 7);
                btn->setFixedSize(28, 28);
                btn->setToolTip(f.tooltip);
                btn->setStyleSheet(
                    "QPushButton { font-weight: bold; font-family: monospace; }"
                    "QPushButton:checked { background-color: #4CAF50; color: white; }"
                );
                connect(btn, &QPushButton::toggled, this, [this, i](bool checked) {
                    emit flagToggled(FLAGS[i].bit, checked);
                });
                m_buttons[i] = btn;
                layout->addWidget(btn);
            } else {
                auto* dash = new QLabel("-");
                dash->setFixedSize(28, 28);
                dash->setAlignment(Qt::AlignCenter);
                dash->setStyleSheet("color: gray; font-family: monospace;");
                layout->addWidget(dash);
                m_buttons[i] = nullptr;
            }
        }

        layout->addStretch();
    }

    void setFlags(uint8_t f) {
        for (int i = 0; i < 8; i++) {
            if (m_buttons[i]) {
                m_buttons[i]->blockSignals(true);
                m_buttons[i]->setChecked(f & (1 << FLAGS[i].bit));
                m_buttons[i]->blockSignals(false);
            }
        }
    }

signals:
    void flagToggled(int bit, bool value);

private:
    QPushButton* m_buttons[8] = {};
};

//=============================================================================
// Layout 4: Vertical compact (for narrow panels)
//=============================================================================
class FlagsLayout4 : public QGroupBox {
    Q_OBJECT
public:
    FlagsLayout4(QWidget* parent = nullptr) : QGroupBox("Layout 4: Vertical Compact", parent) {
        auto* layout = new QGridLayout(this);
        layout->setSpacing(1);

        // Two columns: 4 flags each
        int col = 0, row = 0;
        for (int i = 0; i < 8; i++) {
            const auto& f = FLAGS[i];
            if (!f.name) continue;

            auto* cb = new QCheckBox(QString(f.letter));
            cb->setToolTip(f.tooltip);
            cb->setChecked(i == 0 || i == 7);
            cb->setStyleSheet("font-family: monospace; font-weight: bold;");
            connect(cb, &QCheckBox::toggled, this, [this, i](bool checked) {
                emit flagToggled(FLAGS[i].bit, checked);
            });
            m_checkboxes[i] = cb;
            layout->addWidget(cb, row, col);

            row++;
            if (row >= 3) {
                row = 0;
                col++;
            }
        }
    }

    void setFlags(uint8_t f) {
        for (int i = 0; i < 8; i++) {
            if (m_checkboxes[i]) {
                m_checkboxes[i]->blockSignals(true);
                m_checkboxes[i]->setChecked(f & (1 << FLAGS[i].bit));
                m_checkboxes[i]->blockSignals(false);
            }
        }
    }

signals:
    void flagToggled(int bit, bool value);

private:
    QCheckBox* m_checkboxes[8] = {};
};

//=============================================================================
// Layout 5: LED-style indicators with separate labels
//=============================================================================
class LedIndicator : public QWidget {
    Q_OBJECT
public:
    LedIndicator(QWidget* parent = nullptr) : QWidget(parent), m_on(false) {
        setFixedSize(12, 12);
    }

    void setOn(bool on) {
        if (m_on != on) {
            m_on = on;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRect r = rect().adjusted(1, 1, -1, -1);

        if (m_on) {
            // Glowing green LED
            QRadialGradient g(rect().center(), 6);
            g.setColorAt(0, QColor("#7FFF7F"));
            g.setColorAt(0.5, QColor("#00CC00"));
            g.setColorAt(1, QColor("#006600"));
            p.setBrush(g);
            p.setPen(QColor("#004400"));
        } else {
            // Dark LED
            p.setBrush(QColor("#333333"));
            p.setPen(QColor("#222222"));
        }
        p.drawEllipse(r);
    }

private:
    bool m_on;
};

class FlagsLayout5 : public QGroupBox {
    Q_OBJECT
public:
    FlagsLayout5(QWidget* parent = nullptr) : QGroupBox("Layout 5: LED Indicators (read-only style)", parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setSpacing(8);

        for (int i = 0; i < 8; i++) {
            const auto& f = FLAGS[i];

            auto* col = new QVBoxLayout();
            col->setSpacing(2);

            auto* label = new QLabel(QString(f.letter));
            label->setAlignment(Qt::AlignCenter);
            label->setStyleSheet("font-weight: bold; font-family: monospace; font-size: 11px;");
            col->addWidget(label, 0, Qt::AlignCenter);

            if (f.name) {
                auto* led = new LedIndicator();
                led->setOn(i == 0 || i == 7);
                led->setToolTip(f.tooltip);
                m_leds[i] = led;
                col->addWidget(led, 0, Qt::AlignCenter);
            } else {
                auto* spacer = new QWidget();
                spacer->setFixedSize(12, 12);
                col->addWidget(spacer, 0, Qt::AlignCenter);
                m_leds[i] = nullptr;
            }

            layout->addLayout(col);
        }

        layout->addStretch();
    }

    void setFlags(uint8_t f) {
        for (int i = 0; i < 8; i++) {
            if (m_leds[i]) {
                m_leds[i]->setOn(f & (1 << FLAGS[i].bit));
            }
        }
    }

private:
    LedIndicator* m_leds[8] = {};
};

//=============================================================================
// Main window
//=============================================================================
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow() {
        setWindowTitle("Flags Widget POC - Layout Comparison");

        auto* mainLayout = new QVBoxLayout(this);

        // Current value display
        auto* valueLayout = new QHBoxLayout();
        valueLayout->addWidget(new QLabel("F Register:"));
        m_valueLabel = new QLabel("0x81 (10000001)");
        m_valueLabel->setStyleSheet("font-family: monospace; font-weight: bold;");
        valueLayout->addWidget(m_valueLabel);
        valueLayout->addStretch();
        mainLayout->addLayout(valueLayout);

        // Separator
        auto* sep = new QFrame();
        sep->setFrameShape(QFrame::HLine);
        mainLayout->addWidget(sep);

        // Layout variants
        m_layout1 = new FlagsLayout1();
        m_layout2 = new FlagsLayout2();
        m_layout3 = new FlagsLayout3();
        m_layout4 = new FlagsLayout4();
        m_layout5 = new FlagsLayout5();

        mainLayout->addWidget(m_layout1);
        mainLayout->addWidget(m_layout2);
        mainLayout->addWidget(m_layout3);
        mainLayout->addWidget(m_layout4);
        mainLayout->addWidget(m_layout5);

        // Connect all layouts to sync
        connect(m_layout1, &FlagsLayout1::flagToggled, this, &MainWindow::onFlagToggled);
        connect(m_layout2, &FlagsLayout2::flagToggled, this, &MainWindow::onFlagToggled);
        connect(m_layout3, &FlagsLayout3::flagToggled, this, &MainWindow::onFlagToggled);

        // Test buttons
        auto* btnLayout = new QHBoxLayout();
        auto* setAllBtn = new QPushButton("Set All (0xFF)");
        auto* clearAllBtn = new QPushButton("Clear All (0x00)");
        auto* randomBtn = new QPushButton("Random");

        connect(setAllBtn, &QPushButton::clicked, this, [this]() { setFlags(0xFF); });
        connect(clearAllBtn, &QPushButton::clicked, this, [this]() { setFlags(0x00); });
        connect(randomBtn, &QPushButton::clicked, this, [this]() { setFlags(rand() & 0xFF); });

        btnLayout->addWidget(setAllBtn);
        btnLayout->addWidget(clearAllBtn);
        btnLayout->addWidget(randomBtn);
        btnLayout->addStretch();
        mainLayout->addLayout(btnLayout);

        mainLayout->addStretch();

        m_flags = 0x81;  // S and C set
    }

private slots:
    void onFlagToggled(int bit, bool value) {
        if (value)
            m_flags |= (1 << bit);
        else
            m_flags &= ~(1 << bit);

        updateAll();
    }

private:
    void setFlags(uint8_t f) {
        m_flags = f;
        updateAll();
    }

    void updateAll() {
        m_valueLabel->setText(QString("0x%1 (%2)")
            .arg(m_flags, 2, 16, QChar('0')).toUpper()
            .arg(m_flags, 8, 2, QChar('0')));

        m_layout1->setFlags(m_flags);
        m_layout2->setFlags(m_flags);
        m_layout3->setFlags(m_flags);
        m_layout4->setFlags(m_flags);
        m_layout5->setFlags(m_flags);
    }

    uint8_t m_flags = 0;
    QLabel* m_valueLabel;
    FlagsLayout1* m_layout1;
    FlagsLayout2* m_layout2;
    FlagsLayout3* m_layout3;
    FlagsLayout4* m_layout4;
    FlagsLayout5* m_layout5;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    MainWindow w;
    w.resize(500, 600);
    w.show();

    return app.exec();
}

#include "main.moc"
