#include "qclickablelabel.h"

QClickableLabel::QClickableLabel(QWidget* parent, Qt::WindowFlags f) : QLabel(parent)
{
}

QClickableLabel::~QClickableLabel()
{
}

void QClickableLabel::mousePressEvent(QMouseEvent* event)
{
    emit clicked();
}

void QClickableLabel::mouseReleaseEvent(QMouseEvent *event)
{
    emit released();
}

void QClickableLabel::mouseDoubleClickEvent(QMouseEvent *event)
{
    emit doubleClicked();
}

