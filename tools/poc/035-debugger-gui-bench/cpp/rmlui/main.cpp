// RmlUi variant: SDL3 + OpenGL 3 backend shipped with RmlUi.
//   BENCH_RML_MODE_DOM    - hex, disassembly and timeline are DOM nodes (spans / divs), updated in place each frame
//                           (only the nodes whose text, class or height changed are touched)
//   BENCH_RML_MODE_CUSTOM - the same three views are custom elements that build their own meshes (elements.cpp)
// In both modes the panels, titles and layout are RML/RCSS, and the images are a custom element with a GL texture.

#include "../common/harness.h"
#include "../common/layout.h"
#include "elements.h"

#include <RmlUi/Core.h>
#include <RmlUi_Backend.h>
#include <RmlUi_Include_GL3.h>
#include <RmlUi_Renderer_GL3.h>

#include <SDL3/SDL.h>

#include "../sdlcommon/sdlgl.h"

#include <cstdio>
#include <functional>
#include <sstream>
#include <thread>

using namespace bench;
namespace L = bench::layout;

#if defined(BENCH_RML_MODE_DOM)
static const char* kVariant = "rmlui-dom";
#else
static const char* kVariant = "rmlui-custom";
#endif

static std::string Hex6(uint32_t rgb)
{
    char b[8];
    std::snprintf(b, sizeof b, "#%06X", rgb);
    return b;
}

// Lengths are written in dp (scaled by the context's density-independent pixel ratio = the UI scale), text-relative
// positions in em (they follow the content font size, like CSS); the content font size itself is a dp value.
static std::string Dp(float v)
{
    char b[24];
    std::snprintf(b, sizeof b, "%.2fdp", v);
    return b;
}

static std::string Em(float px12)  // a length given in px at the 12 px reference font
{
    char b[24];
    std::snprintf(b, sizeof b, "%.4fem", px12 / L::kFontPx);
    return b;
}

static std::string PanelOpen(const char* id, const char* title, const L::Rect& r, float fontPx)
{
    std::ostringstream o;
    o << "<div class='panel' id='" << id << "' style='left:" << Dp(r.x) << ";top:" << Dp(r.y) << ";width:" << Dp(r.w - 2)
      << ";height:" << Dp(r.h - 2) << ";'><div class='title'>" << title << "</div><div class='content' style='width:"
      << Dp(r.w - 2 * L::kPad - 2) << ";height:" << Dp(r.h - L::kTitleH - 2 * L::kPad - 2) << ";font-size:" << Dp(fontPx) << ";'>";
    return o.str();
}

static std::string BuildDocument(const HarnessOptions& opt)
{
    const float fp = opt.fontPx;
    std::ostringstream o;
    o << "<rml><head><title>bench</title><style>\n"
      << "body { width:" << Dp(kWindowW) << "; height:" << Dp(kWindowH) << "; margin:0; background-color:" << Hex6(L::kBg)
      << "; font-family:'" << L::kFontFamily << "'; font-size:" << Dp(L::kFontPx) << "; color:" << Hex6(L::kFg) << "; }\n"
      << "div { display:block; }\n"
      << ".panel { position:absolute; background-color:" << Hex6(L::kPanel) << "; border:1dp " << Hex6(L::kLine)
      << "; border-radius:6dp; }\n"
      << ".title { height:" << Dp(L::kTitleH) << "; line-height:" << Dp(L::kTitleH) << "; padding-left:8dp; color:"
      << Hex6(L::kTitle) << "; background-color:#1C2129; border-radius:6dp 6dp 0 0; }\n"
      << ".content { position:absolute; left:" << Dp(L::kPad) << "; top:" << Dp(L::kTitleH + L::kPad) << "; overflow:hidden; }\n"
      << ".row { position:absolute; left:0; height:" << Em(L::kLineH) << "; line-height:" << Em(L::kLineH) << "; width:100%; }\n"
      << ".row span { position:absolute; top:0; white-space:pre; }\n"
      << ".mut { color:" << Hex6(L::kMuted) << "; }\n"
      << ".chg { color:" << Hex6(L::kAccent) << "; background-color:" << Hex6(L::kChangedBg) << "; }\n"
      << ".pc { background-color:" << Hex6(L::kPcRowBg) << "; }\n"
      << ".pc .op { color:" << Hex6(L::kAccent) << "; }\n"
      << ".bp { position:absolute; left:1dp; top:" << Em(4) << "; width:8dp; height:8dp; border-radius:4dp; }\n"
      << ".bp.on { background-color:" << Hex6(L::kBreak) << "; }\n"
      << ".lane { position:absolute; left:0; width:" << Dp(kTimelineWidth) << "; background-color:" << Hex6(L::kLaneBg) << "; }\n"
      << ".bar { position:absolute; bottom:0; width:1dp; }\n"
      << ".lanename { position:absolute; left:4dp; top:2dp; }\n"
      << ".tick { position:absolute; width:1dp; height:4dp; background-color:" << Hex6(L::kMuted) << "; }\n"
      << ".ticklabel { position:absolute; color:" << Hex6(L::kMuted) << "; }\n"
      << ".playhead { position:absolute; top:0; width:2dp; background-color:" << Hex6(L::kPlayhead) << "; }\n"
      << "texview, hexview, disasmview, timelineview { display:block; position:absolute; }\n"
      << "</style></head><body>\n";
    // Disassembly
    if (opt.View(kViewDisasm))
    {
        o << PanelOpen("disasm", "Disassembly", L::kDisasm, fp);
#if defined(BENCH_RML_MODE_DOM)
        for (int i = 0; i < kDisasmLines; i++)
            o << "<div class='row' id='d" << i << "' style='top:" << Em(i * L::kLineH) << ";'><div class='bp'></div>"
              << "<span class='mut' style='left:" << Em(14) << ";'></span><span class='mut' style='left:" << Em(54)
              << ";'></span><span class='op' style='left:" << Em(150) << ";'></span><span class='mut' style='left:" << Em(290)
              << ";'></span></div>";
#else
        o << "<disasmview id='disasmview' style='left:0;top:0;width:100%;height:100%;'/>";
#endif
        o << "</div></div>\n";
    }

    // Hex
    if (opt.View(kViewHex))
    {
        o << PanelOpen("hex", "Memory", L::kHex, fp);
#if defined(BENCH_RML_MODE_DOM)
        for (int r = 0; r < kHexRows; r++)
        {
            o << "<div class='row' id='h" << r << "' style='top:" << Em(r * L::kLineH) << ";'><span class='mut' style='left:0;'></span>";
            for (int c = 0; c < kHexCols; c++)
                o << "<span style='left:" << Em(48 + c * 22 - 2) << ";padding:0 " << Em(2) << ";'></span>";
            o << "<span class='mut' style='left:" << Em(48 + 16 * 22 + 12) << ";'></span></div>";
        }
#else
        o << "<hexview id='hexview' style='left:0;top:0;width:100%;height:100%;'/>";
#endif
        o << "</div></div>\n";
    }

    // Images
    if (opt.View(kViewImages))
    {
        o << PanelOpen("images", "Screen / layers", L::kImages, fp);
        for (int i = 0; i < kTexCount; i++)
            o << "<texview index='" << i << "' style='left:" << Dp((i % 2) * (L::kImgW + L::kPad)) << ";top:"
              << Dp((i / 2) * (L::kImgH + L::kPad)) << ";width:" << Dp(L::kImgW) << ";height:" << Dp(L::kImgH) << ";'/>";
        o << "</div></div>\n";
    }

    // Timeline
    if (opt.View(kViewTimeline))
    {
        o << PanelOpen("timeline", "Timeline", L::kTimeline, fp);
#if defined(BENCH_RML_MODE_DOM)
        for (int lane = 0; lane < kTimelineLanes; lane++)
        {
            o << "<div class='lane' id='lane" << lane << "' style='top:" << Dp(lane * L::kLaneH) << ";height:"
              << Dp(L::kLaneH - 4) << ";'>";
            for (int x = 0; x < kTimelineWidth; x++)
                o << "<div class='bar' style='left:" << x << "dp;height:0dp;background-color:" << Hex6(Workload::LaneColour(lane))
                  << ";'></div>";
            o << "<div class='lanename'>" << Workload::LaneName(lane) << "</div></div>";
        }
        const float ry = kTimelineLanes * L::kLaneH;
        for (int i = 0; i < kTimelineTicks; i++)
            o << "<div class='tick' id='tick" << i << "' style='top:" << Dp(ry) << ";'></div><div class='ticklabel' id='tl" << i
              << "' style='top:" << Dp(ry + 3) << ";'></div>";
        o << "<div class='playhead' id='playhead' style='height:" << Dp(ry) << ";'></div>";
#else
        o << "<timelineview id='timelineview' style='left:0;top:0;width:100%;height:100%;'/>";
#endif
        o << "</div></div>\n";
    }
    o << "</body></rml>";
    return o.str();
}

/// Runtime content font change: one property per panel; DOM rows and columns follow through em units
static void ApplyContentFont(Rml::ElementDocument* doc, float px)
{
    Rml::ElementList list;
    doc->GetElementsByClassName(list, "content");
    for (Rml::Element* e : list)
        e->SetProperty(Rml::PropertyId::FontSize, Rml::Property(px, Rml::Unit::DP));
}

#if defined(BENCH_RML_MODE_DOM)
// The text node of a span; updates go through ElementText::SetText (no RML parsing, no escaping of '{{' / '<')
static Rml::ElementText* TextOf(Rml::Element* e)
{
    if (e->GetNumChildren() == 0)
        e->AppendChild(e->GetOwnerDocument()->CreateTextNode(" "));
    auto* t = rmlui_dynamic_cast<Rml::ElementText*>(e->GetChild(0));
    RMLUI_ASSERT(t);
    return t;
}

// Cached DOM state, so a frame only touches the nodes that change (what a reactive binding would do)
struct DomCache
{
    Rml::Element* hexCells[kHexRows][kHexCols] = {};
    Rml::ElementText* hexCellText[kHexRows][kHexCols] = {};
    Rml::ElementText* hexAddr[kHexRows] = {};
    Rml::ElementText* hexAscii[kHexRows] = {};
    uint8_t hexValue[kHexRows][kHexCols] = {};
    bool hexChanged[kHexRows][kHexCols] = {};
    std::string hexAsciiText[kHexRows];
    bool hexInit = false;

    Rml::Element* disRow[kDisasmLines] = {};
    Rml::ElementText* disSpan[kDisasmLines][4] = {};
    Rml::Element* disBp[kDisasmLines] = {};
    std::string disText[kDisasmLines][4];

    std::vector<Rml::Element*> bars[kTimelineLanes];
    std::vector<int> barH[kTimelineLanes];
    Rml::Element* tick[kTimelineTicks] = {};
    Rml::ElementText* tickLabel[kTimelineTicks] = {};
    Rml::Element* tickLabelBox[kTimelineTicks] = {};
    std::string tickText[kTimelineTicks];
    Rml::Element* playhead = nullptr;

    unsigned views = 0;

    /// Rows that do not fit the panel at the current font are display:none and are not updated
    void ApplyRows()
    {
        if (views & kViewHex)
            for (int r = 0; r < kHexRows; r++)
                hexAddr[r]->GetParentNode()->GetParentNode()->SetProperty(Rml::PropertyId::Display,
                    Rml::Property(r < L::T().hexRows ? Rml::Style::Display::Block : Rml::Style::Display::None));
        if (views & kViewDisasm)
            for (int i = 0; i < kDisasmLines; i++)
                disRow[i]->SetProperty(Rml::PropertyId::Display,
                    Rml::Property(i < L::T().disRows ? Rml::Style::Display::Block : Rml::Style::Display::None));
    }

    void Bind(Rml::ElementDocument* doc, unsigned v)
    {
        views = v;
        if (views & kViewHex)
        for (int r = 0; r < kHexRows; r++)
        {
            Rml::Element* row = doc->GetElementById("h" + std::to_string(r));
            hexAddr[r] = TextOf(row->GetChild(0));
            for (int c = 0; c < kHexCols; c++)
            {
                hexCells[r][c] = row->GetChild(1 + c);
                hexCellText[r][c] = TextOf(hexCells[r][c]);
            }
            hexAscii[r] = TextOf(row->GetChild(1 + kHexCols));
        }
        if (views & kViewDisasm)
        for (int i = 0; i < kDisasmLines; i++)
        {
            disRow[i] = doc->GetElementById("d" + std::to_string(i));
            disBp[i] = disRow[i]->GetChild(0);
            for (int k = 0; k < 4; k++)
                disSpan[i][k] = TextOf(disRow[i]->GetChild(1 + k));
        }
        if (!(views & kViewTimeline))
            return;
        for (int lane = 0; lane < kTimelineLanes; lane++)
        {
            Rml::Element* l = doc->GetElementById("lane" + std::to_string(lane));
            for (int x = 0; x < kTimelineWidth; x++)
                bars[lane].push_back(l->GetChild(x));
            barH[lane].assign(kTimelineWidth, 0);
        }
        for (int i = 0; i < kTimelineTicks; i++)
        {
            tick[i] = doc->GetElementById("tick" + std::to_string(i));
            tickLabelBox[i] = doc->GetElementById("tl" + std::to_string(i));
            tickLabel[i] = TextOf(tickLabelBox[i]);
        }
        playhead = doc->GetElementById("playhead");
    }

    void Update(const Workload& w)
    {
        const uint8_t* mem = w.Memory();
        if (views & kViewHex)
        for (int r = 0; r < L::T().hexRows; r++)
        {
            if (!hexInit)
                hexAddr[r]->SetText(w.HexRowAddr(r));
            for (int c = 0; c < kHexCols; c++)
            {
                const uint8_t v = mem[(w.HexBase() + r * kHexCols + c) & 0xFFFF];
                const bool ch = w.HexChanged(r, c);
                if (!hexInit || v != hexValue[r][c])
                {
                    hexCellText[r][c]->SetText(Rml::String(Workload::Hex2(v), 2));
                    hexValue[r][c] = v;
                }
                if (!hexInit || ch != hexChanged[r][c])
                {
                    hexCells[r][c]->SetClass("chg", ch);
                    hexChanged[r][c] = ch;
                }
            }
            if (w.HexRowAscii(r) != hexAsciiText[r])
            {
                hexAsciiText[r] = w.HexRowAscii(r);
                hexAscii[r]->SetText(hexAsciiText[r]);
            }
        }
        hexInit = true;

        int i = 0;
        if (views & kViewDisasm)
        for (const auto& l : w.Disasm())
        {
            if (i >= L::T().disRows)
                break;
            const std::string* parts[4] = {&l.addr, &l.bytes, &l.text, &l.comment};
            for (int k = 0; k < 4; k++)
                if (*parts[k] != disText[i][k])
                {
                    disText[i][k] = *parts[k];
                    disSpan[i][k]->SetText(disText[i][k]);
                }
            disRow[i]->SetClass("pc", l.isPc);
            disBp[i]->SetClass("on", l.isBreakpoint);
            i++;
        }

        if (!(views & kViewTimeline))
            return;
        const float laneInner = L::kLaneH - 4;
        for (int lane = 0; lane < kTimelineLanes; lane++)
            for (int x = 0; x < kTimelineWidth; x++)
            {
                const int hpx = int(w.TimelineValue(lane, x) * laneInner + 0.5f);
                if (hpx != barH[lane][x])
                {
                    barH[lane][x] = hpx;
                    bars[lane][x]->SetProperty(Rml::PropertyId::Height, Rml::Property(float(hpx), Rml::Unit::DP));
                }
            }
        for (int t = 0; t < kTimelineTicks; t++)
        {
            const auto& tk = w.TimelineTicks()[t];
            tick[t]->SetProperty(Rml::PropertyId::Left, Rml::Property(tk.x, Rml::Unit::DP));
            tickLabelBox[t]->SetProperty(Rml::PropertyId::Left, Rml::Property(tk.x + 2, Rml::Unit::DP));
            if (tk.label != tickText[t])
            {
                tickText[t] = tk.label;
                tickLabel[t]->SetText(tk.label);
            }
        }
        playhead->SetProperty(Rml::PropertyId::Left, Rml::Property(w.TimelinePlayheadX() - 1, Rml::Unit::DP));
    }
};
#endif

int main(int argc, char** argv)
{
    Harness h(ParseOptions(argc, argv, kVariant));
    const HarnessOptions& opt = h.Options();

    // UI scale: the window is 1600x1000 x scale points (the backend creates it with high pixel density); the context
    // works in framebuffer pixels, its dp ratio = framebuffer / logical (UI scale x the display's pixel density)
    float scale = opt.scale;
    sdlgl::PreInit();
    if (!Backend::Initialize("RmlUi debugger bench", int(kWindowW * scale + 0.5f), int(kWindowH * scale + 0.5f), false))
        return 1;
    SDL_GL_SetSwapInterval(0);
    sdlgl::Pixels px = sdlgl::WindowPixels(SDL_GL_GetCurrentWindow());
    auto physW = [&] { return px.w; };
    auto physH = [&] { return px.h; };
    auto dpRatio = [&] { return float(px.w) / kWindowW; };
    h.SetDisplay(SDL_GetWindowPixelDensity(SDL_GL_GetCurrentWindow()),
                 sdlgl::RendererName(reinterpret_cast<const unsigned char* (*)(unsigned int)>(glGetString)));
    Rml::SetSystemInterface(Backend::GetSystemInterface());
    Rml::SetRenderInterface(Backend::GetRenderInterface());
    Rml::Initialise();
    rml::RegisterElements();
    Rml::LoadFontFace(L::kFontPath);
    Rml::Context* ctx = Rml::CreateContext("main", Rml::Vector2i(physW(), physH()));
    ctx->SetDensityIndependentPixelRatio(dpRatio());
    static_cast<RenderInterface_GL3*>(Backend::GetRenderInterface())->SetViewport(physW(), physH());

    GLuint tex[kTexCount] = {};
    glGenTextures(kTexCount, tex);
    for (GLuint t : tex)
    {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTexW, kTexH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    uint32_t texNames[kTexCount];
    for (int i = 0; i < kTexCount; i++)
        texNames[i] = tex[i];
    rml::TexView::SetTextures(texNames);
    rml::ImmediateView::SetWorkload(&h.Data());

    Rml::ElementDocument* doc = ctx->LoadDocumentFromMemory(BuildDocument(opt));
    doc->Show();

#if defined(BENCH_RML_MODE_DOM)
    DomCache dom;
    dom.Bind(doc, opt.views);
    dom.ApplyRows();
#else
    rml::ImmediateView* views[3] = {
        static_cast<rml::ImmediateView*>(doc->GetElementById("hexview")),
        static_cast<rml::ImmediateView*>(doc->GetElementById("disasmview")),
        static_cast<rml::ImmediateView*>(doc->GetElementById("timelineview"))};
#endif

    auto* gl3 = static_cast<RenderInterface_GL3*>(Backend::GetRenderInterface());
    SDL_Window* win = SDL_GL_GetCurrentWindow();
    std::vector<uint8_t> shot;
    bool running = true;
    while (running)
    {
        const Phase ph = h.Update();
        if (ph == Phase::Done)
            break;
        if (ph == Phase::Idle)
        {
            // Event-driven idle: block until input or the end of the phase; nothing to redraw without input
            SDL_WaitEventTimeout(nullptr, std::min(h.MsUntilPhaseEnd(), 1000));
            running = Backend::ProcessEvents(ctx, nullptr, false);
            continue;
        }
        running = Backend::ProcessEvents(ctx, nullptr, false);
        if (ph != Phase::Unthrottled)
            std::this_thread::sleep_until(h.NextDeadline());

        h.TickWorkload();
        h.FrameBegin();
        float newFont = 0, newScale = 0;
        if (h.PendingSwitch(newFont, newScale))
        {
            if (newScale != scale)
            {
                scale = newScale;
                SDL_SetWindowSize(win, int(kWindowW * scale + 0.5f), int(kWindowH * scale + 0.5f));
                SDL_SyncWindow(win);
                px = sdlgl::WindowPixels(win);
                ctx->SetDimensions(Rml::Vector2i(physW(), physH()));
                ctx->SetDensityIndependentPixelRatio(dpRatio());
            }
            else
            {
                ApplyContentFont(doc, newFont);
#if defined(BENCH_RML_MODE_DOM)
                dom.ApplyRows();
#endif
            }
        }
        const Workload& w = h.Data();
        {
            if (opt.View(kViewImages))
            for (int i = 0; i < kTexCount; i++)
            {
                glBindTexture(GL_TEXTURE_2D, tex[i]);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kTexW, kTexH, GL_RGBA, GL_UNSIGNED_BYTE, w.Image(i));
            }
#if defined(BENCH_RML_MODE_DOM)
            dom.Update(w);
#else
            for (auto* v : views)
                if (v)
                    v->Invalidate();
#endif
        }
        ctx->Update();
        h.MarkRenderStart();
        gl3->SetViewport(physW(), physH());  // the backend's resize handler may have set it in points
        Backend::BeginFrame();
        ctx->Render();
        gl3->EndFrame();
        if (h.WantScreenshot())
        {
            shot.resize(size_t(physW()) * physH() * 4);
            glReadPixels(0, 0, physW(), physH(), GL_RGBA, GL_UNSIGNED_BYTE, shot.data());
            h.SubmitScreenshot(shot.data(), physW(), physH(), physW() * 4, true);
        }
        SDL_GL_SwapWindow(win);
        glFinish();
        h.FrameEnd();
    }

    std::function<int(Rml::Element*)> count = [&](Rml::Element* e) {
        int n = 1;
        for (int i = 0; i < e->GetNumChildren(); i++)
            n += count(e->GetChild(i));
        return n;
    };
    char notes[128];
    std::snprintf(notes, sizeof notes, "elements in document: %d", count(doc));
    h.Finish(notes);

    doc->Close();
    Rml::Shutdown();
    Backend::Shutdown();
    return 0;
}
