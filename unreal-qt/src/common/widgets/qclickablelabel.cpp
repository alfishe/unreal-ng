#include "qclickablelabel.h"

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

