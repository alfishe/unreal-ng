#pragma once

#include <QWidget>

class EnvelopeShapeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EnvelopeShapeWidget(QWidget* parent = nullptr);

    void setShape(int shape);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int _shape = -1;
};
