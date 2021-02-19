#pragma once

#ifndef UNREAL_QT_QCLICKABLELABEL_H
#define UNREAL_QT_QCLICKABLELABEL_H

#include <QLabel>
#include <QWidget>
#include <Qt>

/// See: https://wiki.qt.io/Clickable_QLabel
class QClickableLabel : public QLabel
{
    Q_OBJECT

public:
    explicit QClickableLabel(QWidget* parent = Q_NULLPTR, Qt::WindowFlags f = Qt::WindowFlags());
    ~QClickableLabel();

signals:
    void clicked();
    void released();
    void doubleClicked();

protected:
    void mousePressEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent* event);
    void mouseDoubleClickEvent(QMouseEvent* event);
};


#endif //UNREAL_QT_QCLICKABLELABEL_H
