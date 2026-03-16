#pragma once

#include <QWidget>
#include <vector>

class SparklineWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SparklineWidget(int capacity = 200, QWidget* parent = nullptr);

    void addValue(float v);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<float> _buffer;
    int _capacity;
    int _head = 0;
    int _count = 0;
    float _minVal = 0.0f;
    float _maxVal = 0.0f;
    bool _dirty = true;
};
