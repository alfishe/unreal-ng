#pragma once

#include <QWidget>
#include <QIcon>
#include <QTimer>

/// One monochrome device LED in the status bar (tape, disk, HDD, sound).
/// Blinks while active; click toggles it.
class StatusIndicator : public QWidget
{
    Q_OBJECT
public:
    StatusIndicator(const QString &iconPath, const QString &name, QWidget *parent = nullptr);

    bool isActive() const { return m_active; }
    void setActive(bool on);

signals:
    void toggled(bool active);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    QSize sizeHint() const override { return QSize(26, 20); }

private:
    QIcon   m_icon;
    QString m_name;
    QTimer  m_blink;
    bool    m_active = false;
    bool    m_phase  = false;
};
