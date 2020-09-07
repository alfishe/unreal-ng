#ifndef VERTICALLABEL_H
#define VERTICALLABEL_H

#include <QLabel>

class VerticalLabel : public QLabel
{
    Q_OBJECT
public:
    explicit VerticalLabel(QWidget *parent = nullptr);
    explicit VerticalLabel(const QString &text, QWidget *parent = nullptr);
    ~VerticalLabel();

protected:
    void paintEvent(QPaintEvent*);
    QSize sizeHint() const ;
    QSize minimumSizeHint() const;

};

#endif // VERTICALLABEL_H
