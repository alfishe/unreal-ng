// What a deck shows now, as the reports that drew it: a viewer that joins late gets these replayed
// (device-wire.md §5) instead of waiting for the runtime to redraw.
//
// Output reports are grouped into image chains (a key, the full screen, the strip window, an LCD region), and a
// chain replaces the chains it covers. Colour fills are pictures too (they cover a key or everything). Brightness,
// sleep, background and touch LEDs are kept as the last value of each. Not thread-safe: one owner.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sdb/models.h"

namespace sdb
{

class PictureCache
{
public:
    struct Entry
    {
        std::uint8_t wireType = 0;  // wire::Output or wire::SetFeature
        std::string payload;
    };

    explicit PictureCache(const Model& model) : _model(&model) {}

    void Output(std::span<const std::uint8_t> report);
    void Feature(std::span<const std::uint8_t> report);
    void Clear();

    // State features first, then the pictures in the order they were drawn.
    [[nodiscard]] std::vector<Entry> Replay() const;
    [[nodiscard]] std::size_t PictureCount() const
    {
        return _pictures.size();
    }

private:
    enum class Kind : std::uint8_t
    {
        Key,         // one key
        Region,      // an LCD rectangle (device space)
        Window,      // the whole strip window (0x0B)
        FullScreen,  // the whole device, keys and strip (0x08)
        KeyColor,    // feature 03 06 on a key
        ScreenColor  // feature 03 05: everything
    };
    struct Picture
    {
        Kind kind = Kind::Key;
        int key = -1;
        int x = 0, y = 0, w = 0, h = 0;
        std::vector<Entry> reports;
    };
    struct Pending
    {
        Picture picture;
        int nextPage = 0;
    };

    void Commit(Picture picture);
    [[nodiscard]] static bool Covers(const Picture& by, const Picture& old);

    const Model* _model;
    std::map<std::string, Pending> _pending;  // chains being sent, by slot
    std::vector<Picture> _pictures;
    std::map<std::string, std::string> _state;  // feature key -> the last report
};

}  // namespace sdb
