// textscreen.cpp - text grid implementation (port of the txtscr painting core).
#include "screen/textscreen.h"

namespace dbg {

namespace {
constexpr uint8_t kFillChar = 0xB1;  // 50% checker ▒
}  // namespace

TextScreen::TextScreen(int width, int height, int frameCapacity)
    : width_(width),
      height_(height),
      frameCapacity_(frameCapacity),
      chars_(static_cast<size_t>(width) * static_cast<size_t>(height), kFillChar),
      attrs_(static_cast<size_t>(width) * static_cast<size_t>(height), kBackgr) {}

void TextScreen::Clear() {
    chars_.assign(chars_.size(), kFillChar);
    attrs_.assign(attrs_.size(), kBackgr);
    frames_.clear();
}

bool TextScreen::InBounds(int x, int y) const {
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}

void TextScreen::Tprint(int x, int y, const std::string& bytes, uint8_t attr) {
    int p = x;
    for (const char c : bytes) {
        if (InBounds(p, y)) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width_) +
                               static_cast<size_t>(p);
            chars_[idx] = static_cast<uint8_t>(static_cast<unsigned char>(c));
            attrs_[idx] = attr;
        }
        ++p;
    }
}

void TextScreen::TprintFg(int x, int y, const std::string& bytes, uint8_t ink) {
    int p = x;
    for (const char c : bytes) {
        if (InBounds(p, y)) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width_) +
                               static_cast<size_t>(p);
            chars_[idx] = static_cast<uint8_t>(static_cast<unsigned char>(c));
            attrs_[idx] = static_cast<uint8_t>((attrs_[idx] & 0xF0) | (ink & 0x0F));
        }
        ++p;
    }
}

void TextScreen::SetAttr(int x, int y, uint8_t attr) {
    if (InBounds(x, y)) {
        attrs_[static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)] =
            attr;
    }
}

void TextScreen::SetChar(int x, int y, uint8_t code, uint8_t attr) {
    if (InBounds(x, y)) {
        const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width_) +
                           static_cast<size_t>(x);
        chars_[idx] = code;
        attrs_[idx] = attr;
    }
}

uint8_t TextScreen::CharAt(int x, int y) const {
    return InBounds(x, y)
               ? chars_[static_cast<size_t>(y) * static_cast<size_t>(width_) +
                        static_cast<size_t>(x)]
               : 0;
}

uint8_t TextScreen::AttrAt(int x, int y) const {
    return InBounds(x, y)
               ? attrs_[static_cast<size_t>(y) * static_cast<size_t>(width_) +
                        static_cast<size_t>(x)]
               : kBackgr;
}

void TextScreen::FillRect(int x, int y, int w, int h, uint8_t attr) {
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            SetChar(xx, yy, 0x20, attr);
        }
    }
}

void TextScreen::Frame(int x, int y, int w, int h, uint8_t color) {
    if (static_cast<int>(frames_.size()) >= frameCapacity_) {
        frames_.erase(frames_.begin());  // original drops the oldest entry
    }
    frames_.push_back(FrameEntry{x, y, w, h, color});
}

void TextScreen::FilledFrame(int x, int y, int w, int h) {
    frames_.clear();  // §1.5: dialogs hide all panel frames
    FillRect(x, y, w, h, kFFrameInside);
    Frame(x, y, w, h, kFFrameFrame);
}

}  // namespace dbg
