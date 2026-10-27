#include "controllertoast.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

ControllerToast::ControllerToast(QWidget* owner)
    : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 10, 10, 10);
    auto* top = new QHBoxLayout();
    _text = new QLabel(this);
    _text->setStyleSheet("color: white;");
    top->addWidget(_text, 1);
    auto* close = new QToolButton(this);
    close->setText(QStringLiteral("×"));
    close->setAutoRaise(true);
    close->setStyleSheet("color: white;");
    top->addWidget(close, 0, Qt::AlignTop);
    root->addLayout(top);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    _keep = new QPushButton(tr("Keep"), this);
    _change = new QPushButton(tr("Change…"), this);
    _open = new QPushButton(tr("Open controllers"), this);
    _learn = new QPushButton(tr("Learn…"), this);
    _learn->setToolTip(tr("Teach the emulator this pad's controls: press each one when it is asked for"));
    buttons->addWidget(_keep);
    buttons->addWidget(_change);
    buttons->addWidget(_open);
    buttons->addWidget(_learn);
    root->addLayout(buttons);

    _hide.setSingleShot(true);
    _hide.setInterval(8000);
    connect(&_hide, &QTimer::timeout, this, &QWidget::hide);
    connect(close, &QToolButton::clicked, this, &QWidget::hide);
    connect(_keep, &QPushButton::clicked, this, [this] {
        hide();
        emit keepRequested();
    });
    connect(_learn, &QPushButton::clicked, this, [this] {
        hide();
        emit learnRequested(_device);
    });
    for (QPushButton* button : {_change, _open})
        connect(button, &QPushButton::clicked, this, [this] {
            hide();
            emit changeRequested();
        });
}

QString ControllerToast::text() const
{
    return _text->text();
}

void ControllerToast::showText(const QString& text, const QRect& area, Kind kind, const QString& device)
{
    _kind = kind;
    _device = device;
    _learn->setVisible(kind == Kind::Learn);
    _keep->setVisible(kind == Kind::AutoProfile);
    _change->setVisible(kind == Kind::AutoProfile);
    _open->setVisible(kind == Kind::Connected);
    _text->setText(QStringLiteral("▣ ") + text);
    adjustSize();
    const QSize size = sizeHint().expandedTo(QSize(360, 0)).boundedTo(QSize(std::max(200, area.width() - 40), 400));
    resize(size);
    move(area.center().x() - size.width() / 2, area.bottom() - size.height() - 24);
    show();
    raise();
    _hide.start();
}

void ControllerToast::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 30, 30, 220));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);
}
