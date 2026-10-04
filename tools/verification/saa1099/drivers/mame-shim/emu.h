// emu.h - the few MAME device-framework names refs/mame/saa1099.cpp uses, enough to
// compile and drive the device standalone. Not MAME code: an interface stand-in.
#pragma once

#include <cstdint>
#include <cstdio>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using s32 = int32_t;
using offs_t = uint32_t;
using device_type = int;

template <typename T>
constexpr int BIT(T x, int n)
{
    return static_cast<int>((x >> n) & 1);
}

#define ATTR_COLD
#define NAME(x) x
#define STRUCT_MEMBER(a, b) 0
#define DECLARE_DEVICE_TYPE(Type, Class) extern const device_type Type;
#define DEFINE_DEVICE_TYPE(Type, Class, Short, Long) const device_type Type = 0;

struct machine_config
{
};

struct running_machine
{
    const char* describe_context() const { return "cosim"; }
};

class sound_stream
{
public:
    int samples() const { return _samples; }
    void put_int(int channel, int index, int value, int scale)
    {
        (void)scale;
        if (index >= 0 && index < kMax)
            _out[channel & 1][index] = value;
    }
    void update() {}
    void set_sample_rate(int) {}
    void Begin(int samples)
    {
        _samples = samples;
        for (int i = 0; i < samples && i < kMax; i++)
            _out[0][i] = _out[1][i] = 0;
    }
    int Out(int channel, int index) const { return _out[channel & 1][index]; }

private:
    static constexpr int kMax = 16;
    int _samples = 0;
    int _out[2][kMax] = {};
};

class device_t
{
public:
    device_t(const machine_config&, device_type, const char*, device_t*, u32 clock) : _clock(clock) {}
    virtual ~device_t() = default;
    u32 clock() const { return _clock; }
    const running_machine& machine() const { return _machine; }
    template <typename... Args>
    void logerror(const char*, Args...) const
    {
    }
    template <typename T>
    void save_item(T&&, const char* = nullptr)
    {
    }
    template <typename T>
    void save_item(T&&, int)
    {
    }

protected:
    virtual void device_start() {}
    virtual void device_clock_changed() {}

private:
    u32 _clock;
    running_machine _machine;
};

class device_sound_interface
{
public:
    device_sound_interface(const machine_config&, device_t&) {}
    virtual ~device_sound_interface() = default;
    sound_stream* stream_alloc(int, int, int) { return &_stream; }
    sound_stream& Stream() { return _stream; }

protected:
    virtual void sound_stream_update(sound_stream& stream) = 0;

private:
    sound_stream _stream;
};
