#include "elements.h"

#include "../common/layout.h"

#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderManager.h>

namespace L = bench::layout;

namespace bench::rml
{

const Workload* ImmediateView::s_workload = nullptr;
static uint32_t s_textures[kTexCount] = {};

static Rml::ColourbPremultiplied Premul(uint32_t rgb, float a = 1.f)
{
    const auto c = [&](int shift) { return Rml::byte(((rgb >> shift) & 255) * a + 0.5f); };
    return Rml::ColourbPremultiplied(c(16), c(8), c(0), Rml::byte(a * 255 + 0.5f));
}

// --- TexView -----------------------------------------------------------------

void TexView::SetTextures(const uint32_t* glTextures)
{
    for (int i = 0; i < kTexCount; i++)
        s_textures[i] = glTextures[i];
}

void TexView::OnRender()
{
    Rml::RenderManager* rm = GetRenderManager();
    if (!rm)
        return;
    if (!_geometry)
    {
        const int index = GetAttribute<int>("index", 0);
        const uint32_t gl = s_textures[index];
        _texture = rm->MakeCallbackTexture([gl](const Rml::CallbackTextureInterface& ti) {
            ti.SetTextureHandle(Rml::TextureHandle(gl), Rml::Vector2i(kTexW, kTexH));
            return true;
        });
        Rml::Mesh mesh;
        const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Content);
        Rml::MeshUtilities::GenerateQuad(mesh, Rml::Vector2f(0, 0), size, Rml::ColourbPremultiplied(255, 255, 255, 255),
                                         Rml::Vector2f(0, 0), Rml::Vector2f(1, 1));
        _geometry = rm->MakeGeometry(std::move(mesh));
    }
    _geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Content), _texture);
}

// --- ImmediateView -------------------------------------------------------------

// Views work in logical px (common/layout.h); _dp converts to the context's physical pixels
void ImmediateView::Rect(float x, float y, float w, float h, uint32_t rgb, float a)
{
    Rml::MeshUtilities::GenerateQuad(_rects, Rml::Vector2f(x * _dp, y * _dp), Rml::Vector2f(w * _dp, h * _dp), Premul(rgb, a));
}

void ImmediateView::Text(float x, float y, Rml::StringView s, uint32_t rgb)
{
    static const Rml::String language = "en";
    Rml::TexturedMeshList list;
    Rml::GetFontEngineInterface()->GenerateString(*GetRenderManager(), GetFontFaceHandle(), 0, s,
                                                  Rml::Vector2f(x * _dp, y * _dp + _ascent), Premul(rgb), 1.f,
                                                  Rml::TextShapingContext{language}, list);
    for (auto& tm : list)
    {
        TextBatch* batch = nullptr;
        for (auto& b : _text)
            if (b.texture == tm.texture)
                batch = &b;
        if (!batch)
        {
            _text.push_back({tm.texture, {}});
            batch = &_text.back();
        }
        const int base = int(batch->mesh.vertices.size());
        batch->mesh.vertices.insert(batch->mesh.vertices.end(), tm.mesh.vertices.begin(), tm.mesh.vertices.end());
        for (int i : tm.mesh.indices)
            batch->mesh.indices.push_back(base + i);
    }
}

void ImmediateView::OnRender()
{
    Rml::RenderManager* rm = GetRenderManager();
    if (!rm || !s_workload)
        return;
    if (_dirty)
    {
        _dp = GetContext() ? GetContext()->GetDensityIndependentPixelRatio() : 1.f;
        _ascent = float(Rml::GetFontEngineInterface()->GetFontMetrics(GetFontFaceHandle()).ascent);
        _rects = {};
        _text.clear();
        Build();
        _rectGeometry = rm->MakeGeometry(std::move(_rects));
        _textGeometry.clear();
        for (auto& b : _text)
            _textGeometry.emplace_back(rm->MakeGeometry(std::move(b.mesh)), b.texture);
        _dirty = false;
    }
    const Rml::Vector2f o = GetAbsoluteOffset(Rml::BoxArea::Content);
    _rectGeometry.Render(o);
    for (auto& [g, t] : _textGeometry)
        g.Render(o, t);
}

// --- views ---------------------------------------------------------------------

void HexView::Build()
{
    const Workload& w = *s_workload;
    const uint8_t* mem = w.Memory();
    const L::TextMetrics& t = L::T();
    for (int r = 0; r < t.hexRows; r++)
    {
        const float y = r * t.lineH;
        Text(t.hexAddrX, y, w.HexRowAddr(r), L::kMuted);
        for (int c = 0; c < kHexCols; c++)
        {
            const float x = t.hexBytesX + c * t.hexByteStep;
            const bool ch = w.HexChanged(r, c);
            if (ch)
                Rect(x - 2, y, t.hexChangedW, t.lineH - 1, L::kChangedBg);
            const char* hex = Workload::Hex2(mem[(w.HexBase() + r * kHexCols + c) & 0xFFFF]);
            Text(x, y, Rml::StringView(hex, hex + 2), ch ? L::kAccent : L::kFg);
        }
        Text(t.hexAsciiX, y, w.HexRowAscii(r), L::kMuted);
    }
}

void DisasmView::Build()
{
    const Workload& w = *s_workload;
    const float cw = L::kDisasm.w - 2 * L::kPad;
    const L::TextMetrics& t = L::T();
    for (int i = 0; i < t.disRows; i++)
    {
        const auto& l = w.Disasm()[i];
        const float y = i * t.lineH;
        if (l.isPc)
            Rect(0, y, cw, t.lineH, L::kPcRowBg);
        if (l.isBreakpoint)
            Rect(1, y + t.lineH * 0.5f - 4, 8, 8, L::kBreak);
        Text(t.disAddrX, y, l.addr, L::kMuted);
        Text(t.disBytesX, y, l.bytes, L::kMuted);
        Text(t.disTextX, y, l.text, l.isPc ? L::kAccent : L::kFg);
        if (!l.comment.empty())
            Text(t.disCommentX, y, l.comment, L::kMuted);
    }
}

void TimelineView::Build()
{
    const Workload& w = *s_workload;
    for (int lane = 0; lane < kTimelineLanes; lane++)
    {
        const float y0 = lane * L::kLaneH, y1 = y0 + L::kLaneH - 4;
        Rect(0, y0, kTimelineWidth, y1 - y0, L::kLaneBg);
        const uint32_t c = Workload::LaneColour(lane);
        for (int x = 0; x < kTimelineWidth; x++)
        {
            const float v = w.TimelineValue(lane, x);
            if (v > 0.f)
                Rect(float(x), y1 - v * (y1 - y0), 1, v * (y1 - y0), c);
        }
        Text(4, y0 + 2, Rml::String(Workload::LaneName(lane)), L::kFg);
    }
    const float ry = kTimelineLanes * L::kLaneH;
    for (const auto& t : w.TimelineTicks())
    {
        Rect(t.x, ry, 1, 4, L::kMuted);
        Text(t.x + 2, ry + 3, t.label, L::kMuted);
    }
    Rect(w.TimelinePlayheadX() - 1, 0, 2, ry, L::kPlayhead);
}

void RegisterElements()
{
    static Rml::ElementInstancerGeneric<TexView> texview;
    static Rml::ElementInstancerGeneric<HexView> hexview;
    static Rml::ElementInstancerGeneric<DisasmView> disasmview;
    static Rml::ElementInstancerGeneric<TimelineView> timelineview;
    Rml::Factory::RegisterElementInstancer("texview", &texview);
    Rml::Factory::RegisterElementInstancer("hexview", &hexview);
    Rml::Factory::RegisterElementInstancer("disasmview", &disasmview);
    Rml::Factory::RegisterElementInstancer("timelineview", &timelineview);
}

}  // namespace bench::rml
