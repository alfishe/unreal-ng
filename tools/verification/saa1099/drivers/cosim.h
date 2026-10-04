// cosim.h - shared part of the SAA1099 co-simulation drivers: the stimulus format,
// and the event dump every driver writes so compare.py can line references up.
//
// Stimulus (.saa, text): one write per line, "<chip clock> A|D <value>", the value
// decimal or hex (#xx / 0xNN); "<chip clock> END" closes the stream; ';' starts
// a comment. Clocks are absolute chip clocks
// (8 MHz) and non-decreasing.
//
// Dump (text, one event per line, written when the state changes):
//   T <clock> <gen> <level>              tone generator output
//   N <clock> <gen> <bit> <raw>          noise shift: output bit, raw register
//   E <clock> <gen> <on> <left> <right>  envelope generator level (on = enabled)
//   O <clock> <left> <right>             summed output, units of our model (0..720)
// <clock> is the chip clock after which the state holds (state after N clocks).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace saacosim
{

struct Write
{
    uint64_t clock;
    bool address;
    uint8_t value;
};

struct Stimulus
{
    std::vector<Write> writes;
    uint64_t end = 0;
};

inline bool ParseValue(const char* s, unsigned& out)
{
    char* e = nullptr;
    if (s[0] == '#')
        out = static_cast<unsigned>(strtoul(s + 1, &e, 16));
    else
        out = static_cast<unsigned>(strtoul(s, &e, 0));
    return e && e != s;
}

inline Stimulus LoadStimulus(const char* path)
{
    Stimulus st;
    FILE* f = fopen(path, "r");
    if (!f)
    {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    char line[512];
    int lineNo = 0;
    while (fgets(line, sizeof line, f))
    {
        lineNo++;
        if (char* comment = strchr(line, ';'))
            *comment = 0;
        char clockText[64] = {}, kind[16] = {}, value[64] = {};
        const int n = sscanf(line, "%63s %15s %63s", clockText, kind, value);
        if (n <= 0)
            continue;
        unsigned long long clock = strtoull(clockText, nullptr, 0);
        if (n >= 2 && strcmp(kind, "END") == 0)
        {
            st.end = clock;
            continue;
        }
        unsigned v = 0;
        if (n < 3 || (strcmp(kind, "A") != 0 && strcmp(kind, "D") != 0) || !ParseValue(value, v))
        {
            fprintf(stderr, "%s:%d: bad line\n", path, lineNo);
            exit(2);
        }
        if (!st.writes.empty() && clock < st.writes.back().clock)
        {
            fprintf(stderr, "%s:%d: clocks must not decrease\n", path, lineNo);
            exit(2);
        }
        st.writes.push_back({clock, kind[0] == 'A', static_cast<uint8_t>(v)});
    }
    fclose(f);
    if (st.end == 0)
        st.end = st.writes.empty() ? 0 : st.writes.back().clock + 1;
    return st;
}

// Generator state a driver can read from its reference after each clock
struct GenState
{
    uint8_t tone[6] = {};
    uint8_t noiseBit[2] = {};
    uint32_t noiseRaw[2] = {};
    uint8_t envOn[2] = {};
    uint8_t envLeft[2] = {};
    uint8_t envRight[2] = {};
    int32_t outLeft = 0;
    int32_t outRight = 0;
};

class EventLog
{
public:
    explicit EventLog(const char* path)
    {
        _f = (path && strcmp(path, "-") != 0) ? fopen(path, "w") : stdout;
        if (!_f)
        {
            fprintf(stderr, "cannot write %s\n", path);
            exit(2);
        }
    }
    ~EventLog()
    {
        if (_f && _f != stdout)
            fclose(_f);
    }

    // First call prints the full state at `clock`, later calls the changes
    void Sample(uint64_t clock, const GenState& s)
    {
        const unsigned long long c = clock;
        for (int i = 0; i < 6; i++)
            if (_first || s.tone[i] != _prev.tone[i])
                fprintf(_f, "T %llu %d %u\n", c, i, s.tone[i]);
        for (int n = 0; n < 2; n++)
            if (_first || s.noiseRaw[n] != _prev.noiseRaw[n])
                fprintf(_f, "N %llu %d %u %u\n", c, n, s.noiseBit[n], s.noiseRaw[n]);
        for (int e = 0; e < 2; e++)
            if (_first || s.envOn[e] != _prev.envOn[e] || s.envLeft[e] != _prev.envLeft[e] ||
                s.envRight[e] != _prev.envRight[e])
                fprintf(_f, "E %llu %d %u %u %u\n", c, e, s.envOn[e], s.envLeft[e], s.envRight[e]);
        if (_first || s.outLeft != _prev.outLeft || s.outRight != _prev.outRight)
            fprintf(_f, "O %llu %d %d\n", c, s.outLeft, s.outRight);
        _prev = s;
        _first = false;
    }

private:
    FILE* _f = nullptr;
    GenState _prev;
    bool _first = true;
};

// Drives a reference clock by clock: Apply(write) for every write at the current
// clock, then Tick() one chip clock, then Read() the state
template <typename Ref>
void RunPerClock(Ref& ref, const Stimulus& st, EventLog& log)
{
    size_t w = 0;
    GenState s;
    ref.Read(s);
    log.Sample(0, s);
    for (uint64_t c = 0; c < st.end; c++)
    {
        while (w < st.writes.size() && st.writes[w].clock == c)
            ref.Apply(st.writes[w++]);
        ref.Tick();
        ref.Read(s);
        log.Sample(c + 1, s);
    }
}

} // namespace saacosim
