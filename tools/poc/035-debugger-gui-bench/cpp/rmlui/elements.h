#pragma once

// RmlUi custom elements.
//   TexView       - one of the four 640x512 images; draws a quad with a GL texture the main loop updates (both modes)
//   HexView, DisasmView, TimelineView - "custom" mode only: the element builds its own meshes (rects + text through
//                   the font engine) when the data changes, merged into a few geometries, instead of one DOM node per cell.

#include "../common/workload.h"

#include <RmlUi/Core.h>

#include <cstdint>
#include <vector>

namespace bench::rml
{

class TexView : public Rml::Element
{
public:
    explicit TexView(const Rml::String& tag) : Rml::Element(tag) {}
    static void SetTextures(const uint32_t* glTextures);  // GL names, owned by the main loop

protected:
    void OnRender() override;
    void OnResize() override { _geometry = {}; }

private:
    Rml::Geometry _geometry;
    Rml::CallbackTexture _texture;
};

/// Base for the views that generate their own geometry
class ImmediateView : public Rml::Element
{
public:
    explicit ImmediateView(const Rml::String& tag) : Rml::Element(tag) {}
    static void SetWorkload(const Workload* w) { s_workload = w; }
    /// The main loop calls this after a workload tick: the geometry is rebuilt on the next render
    void Invalidate() { _dirty = true; }

protected:
    void OnRender() override;
    virtual void Build() = 0;

    void Rect(float x, float y, float w, float h, uint32_t rgb, float a = 1.f);
    void Text(float x, float y, Rml::StringView s, uint32_t rgb);

    static const Workload* s_workload;

private:
    struct TextBatch
    {
        Rml::Texture texture;
        Rml::Mesh mesh;
    };
    bool _dirty = true;
    Rml::Mesh _rects;
    std::vector<TextBatch> _text;
    Rml::Geometry _rectGeometry;
    std::vector<std::pair<Rml::Geometry, Rml::Texture>> _textGeometry;
    float _ascent = 0;
    float _dp = 1;
};

class HexView : public ImmediateView
{
public:
    using ImmediateView::ImmediateView;

protected:
    void Build() override;
};

class DisasmView : public ImmediateView
{
public:
    using ImmediateView::ImmediateView;

protected:
    void Build() override;
};

class TimelineView : public ImmediateView
{
public:
    using ImmediateView::ImmediateView;

protected:
    void Build() override;
};

void RegisterElements();

}  // namespace bench::rml
