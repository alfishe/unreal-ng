#pragma once

#include <cstdio>

template<typename T>
class FilterDC
{
public:
    static constexpr size_t DC_FILTER_BUFFER_SIZE = 1024;

protected:
    double _sum;
    size_t _index;
    double _delayBuffer[DC_FILTER_BUFFER_SIZE] = {0.0 };

public:
    FilterDC() : _sum(0), _index(0) {};
    virtual ~FilterDC() = default;

public:
    double filter(T sample)
    {
        // Remove the oldest sample amplitude from delay buffer and add new one
        _sum += -_delayBuffer[_index] + sample;
        _delayBuffer[_index] = sample;

        // Same as _index = (_index + 1) % DC_FILTER_SIZE, but faster
        _index = (_index + 1) & (DC_FILTER_BUFFER_SIZE - 1);

        T result = (T)(sample - (_sum / DC_FILTER_BUFFER_SIZE));

        return result;
    }

protected:
    double xm1;
    double ym1;
public:
    T filter2(T sample)
    {
        double value = sample - xm1 + 0.995 * ym1;
        xm1 = sample;
        ym1 = value;

        T result = (T)value;

        return result;
    }
};

//
// Code Under Test (CUT) wrappers to allow access protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

// Expose all internal classes to public
class FilterDCCUT : public FilterDC
{
public:
    FilterDCCUT() : FilterDC() {};

    using FilterDC::_sum;
    using FilterDC::_delayBuffer;
};


#endif // _CODE_UNDER_TEST