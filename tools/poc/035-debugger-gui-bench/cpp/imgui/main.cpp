// Dear ImGui variant: SDL3 + OpenGL 3, docking branch. Views draw through ImDrawList (the way a real
// hex/disasm/timeline widget is written in ImGui), images are GL textures updated with glTexSubImage2D.

#include "../common/harness.h"
#include "../common/layout.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "../sdlcommon/sdlgl.h"

#include <thread>
#include <vector>

using namespace bench;
namespace L = bench::layout;

static ImU32 Col(uint32_t rgb, float a = 1.f)
{
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, int(a * 255));
}

static void ApplyStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 6;
    s.WindowBorderSize = 1;
    s.WindowPadding = ImVec2(L::kPad, L::kPad);
    auto c = [](uint32_t rgb) { return ImVec4(L::R(rgb), L::G(rgb), L::B(rgb), 1); };
    s.Colors[ImGuiCol_WindowBg] = c(L::kPanel);
    s.Colors[ImGuiCol_Border] = c(L::kLine);
    s.Colors[ImGuiCol_TitleBg] = c(0x1C2129);
    s.Colors[ImGuiCol_TitleBgActive] = c(0x1C2129);
    s.Colors[ImGuiCol_Text] = c(L::kTitle);
}

static bool BeginPanel(const char* title, const L::Rect& r)
{
    ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
    ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
    return ImGui::Begin(title, nullptr,
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoBringToFrontOnFocus);
}

static void DrawHex(Workload& w)
{
    BeginPanel("Memory", L::kHex);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o(L::kHex.x + L::kPad, L::kHex.y + L::kTitleH + L::kPad);
    const uint8_t* mem = w.Memory();
    const L::TextMetrics& t = L::T();
    ImFont* font = ImGui::GetFont();
    for (int r = 0; r < t.hexRows; r++)
    {
        const float y = o.y + r * t.lineH;
        dl->AddText(font, t.fontPx, ImVec2(o.x + t.hexAddrX, y), Col(L::kMuted), w.HexRowAddr(r).c_str());
        for (int c = 0; c < kHexCols; c++)
        {
            const float x = o.x + t.hexBytesX + c * t.hexByteStep;
            const bool ch = w.HexChanged(r, c);
            if (ch)
                dl->AddRectFilled(ImVec2(x - 2, y), ImVec2(x - 2 + t.hexChangedW, y + t.lineH - 1), Col(L::kChangedBg));
            dl->AddText(font, t.fontPx, ImVec2(x, y), Col(ch ? L::kAccent : L::kFg),
                        Workload::Hex2(mem[(w.HexBase() + r * kHexCols + c) & 0xFFFF]));
        }
        dl->AddText(font, t.fontPx, ImVec2(o.x + t.hexAsciiX, y), Col(L::kMuted), w.HexRowAscii(r).c_str());
    }
    ImGui::End();
}

static void DrawDisasm(Workload& w)
{
    BeginPanel("Disassembly", L::kDisasm);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o(L::kDisasm.x + L::kPad, L::kDisasm.y + L::kTitleH + L::kPad);
    const float cw = L::kDisasm.w - 2 * L::kPad;
    const L::TextMetrics& t = L::T();
    ImFont* font = ImGui::GetFont();
    for (int i = 0; i < t.disRows; i++)
    {
        const auto& l = w.Disasm()[i];
        const float y = o.y + i * t.lineH;
        if (l.isPc)
            dl->AddRectFilled(ImVec2(o.x, y), ImVec2(o.x + cw, y + t.lineH), Col(L::kPcRowBg));
        if (l.isBreakpoint)
            dl->AddCircleFilled(ImVec2(o.x + 5, y + t.lineH * 0.5f), 4, Col(L::kBreak));
        dl->AddText(font, t.fontPx, ImVec2(o.x + t.disAddrX, y), Col(L::kMuted), l.addr.c_str());
        dl->AddText(font, t.fontPx, ImVec2(o.x + t.disBytesX, y), Col(L::kMuted), l.bytes.c_str());
        dl->AddText(font, t.fontPx, ImVec2(o.x + t.disTextX, y), Col(l.isPc ? L::kAccent : L::kFg), l.text.c_str());
        if (!l.comment.empty())
            dl->AddText(font, t.fontPx, ImVec2(o.x + t.disCommentX, y), Col(L::kMuted), l.comment.c_str());
    }
    ImGui::End();
}

static void DrawTimeline(Workload& w)
{
    BeginPanel("Timeline", L::kTimeline);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int lane = 0; lane < kTimelineLanes; lane++)
    {
        const float y0 = L::kPlotY + lane * L::kLaneH, y1 = y0 + L::kLaneH - 4;
        dl->AddRectFilled(ImVec2(L::kPlotX, y0), ImVec2(L::kPlotX + kTimelineWidth, y1), Col(L::kLaneBg));
        const ImU32 c = Col(Workload::LaneColour(lane));
        for (int x = 0; x < kTimelineWidth; x++)
        {
            const float v = w.TimelineValue(lane, x);
            if (v > 0.f)
                dl->AddRectFilled(ImVec2(L::kPlotX + x, y1 - v * (y1 - y0)), ImVec2(L::kPlotX + x + 1, y1), c);
        }
        dl->AddText(ImGui::GetFont(), L::T().fontPx, ImVec2(L::kPlotX + 4, y0 + 2), Col(L::kFg), Workload::LaneName(lane));
    }
    const float ry = L::kPlotY + kTimelineLanes * L::kLaneH;
    for (const auto& t : w.TimelineTicks())
    {
        dl->AddLine(ImVec2(L::kPlotX + t.x, ry), ImVec2(L::kPlotX + t.x, ry + 4), Col(L::kMuted));
        dl->AddText(ImGui::GetFont(), L::T().fontPx, ImVec2(L::kPlotX + t.x + 2, ry + 3), Col(L::kMuted), t.label.c_str());
    }
    const float px = L::kPlotX + w.TimelinePlayheadX();
    dl->AddLine(ImVec2(px, L::kPlotY), ImVec2(px, ry), Col(L::kPlayhead), 2.f);
    ImGui::End();
}

static void DrawImages(const GLuint* tex)
{
    BeginPanel("Screen / layers", L::kImages);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o(L::kImages.x + L::kPad, L::kImages.y + L::kTitleH + L::kPad);
    for (int i = 0; i < kTexCount; i++)
    {
        const float x = o.x + (i % 2) * (L::kImgW + L::kPad), y = o.y + (i / 2) * (L::kImgH + L::kPad);
        dl->AddImage(ImTextureRef(ImTextureID(intptr_t(tex[i]))), ImVec2(x, y), ImVec2(x + L::kImgW, y + L::kImgH));
    }
    ImGui::End();
}

int main(int argc, char** argv)
{
    Harness h(ParseOptions(argc, argv, "imgui"));
    const HarnessOptions& opt = h.Options();

    sdlgl::PreInit();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    // UI scale: the window is 1600x1000 x scale points; the framebuffer is that x the display's pixel density (Retina: 2).
    // ImGui keeps its logical 1600x1000 coordinates and renders through DisplayFramebufferScale = framebuffer / logical.
    float scale = opt.scale;
    SDL_Window* win = SDL_CreateWindow("ImGui debugger bench", int(kWindowW * scale + 0.5f), int(kWindowH * scale + 0.5f),
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(0);
    sdlgl::Pixels px = sdlgl::WindowPixels(win);
    auto physW = [&] { return px.w; };
    auto physH = [&] { return px.h; };
    h.SetDisplay(SDL_GetWindowPixelDensity(win), sdlgl::RendererName(reinterpret_cast<const unsigned char* (*)(unsigned int)>(glGetString)));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.Fonts->AddFontFromFileTTF(L::kFontPath, L::kFontPx);
    ApplyStyle();
    ImGui_ImplSDL3_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    GLuint tex[kTexCount] = {};
    glGenTextures(kTexCount, tex);
    for (GLuint t : tex)
    {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTexW, kTexH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    std::vector<uint8_t> shot;
    bool quit = false;
    while (!quit)
    {
        const Phase ph = h.Update();
        if (ph == Phase::Done)
            break;

        SDL_Event e;
        if (ph == Phase::Idle)
        {
            // Event-driven idle: sleep until input or the end of the phase, redraw only on input
            if (SDL_WaitEventTimeout(&e, std::min(h.MsUntilPhaseEnd(), 1000)))
                if (e.type == SDL_EVENT_QUIT)
                    quit = true;
            continue;
        }
        while (SDL_PollEvent(&e))
        {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT)
                quit = true;
        }
        if (ph != Phase::Unthrottled)
            std::this_thread::sleep_until(h.NextDeadline());

        h.TickWorkload();
        h.FrameBegin();
        float newFont = 0, newScale = 0;
        if (h.PendingSwitch(newFont, newScale) && newScale != scale)
        {
            scale = newScale;
            SDL_SetWindowSize(win, int(kWindowW * scale + 0.5f), int(kWindowH * scale + 0.5f));
            SDL_SyncWindow(win);
            px = sdlgl::WindowPixels(win);
        }
        Workload& w = h.Data();
        if (opt.View(kViewImages))
            for (int i = 0; i < kTexCount; i++)
            {
                glBindTexture(GL_TEXTURE_2D, tex[i]);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kTexW, kTexH, GL_RGBA, GL_UNSIGNED_BYTE, w.Image(i));
            }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        io.DisplaySize = ImVec2(float(kWindowW), float(kWindowH));
        // fonts are rasterized at this density (1.92 dynamic fonts)
        io.DisplayFramebufferScale = ImVec2(float(physW()) / kWindowW, float(physH()) / kWindowH);
        ImGui::NewFrame();
        if (opt.View(kViewDisasm))
            DrawDisasm(w);
        if (opt.View(kViewHex))
            DrawHex(w);
        if (opt.View(kViewImages))
            DrawImages(tex);
        if (opt.View(kViewTimeline))
            DrawTimeline(w);
        ImGui::Render();
        h.MarkRenderStart();
        glViewport(0, 0, physW(), physH());
        glClearColor(L::R(L::kBg), L::G(L::kBg), L::B(L::kBg), 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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

    const ImDrawData* dd = ImGui::GetDrawData();
    char notes[160];
    std::snprintf(notes, sizeof notes, "last frame: %d vertices, %d indices, %d draw lists", dd ? dd->TotalVtxCount : 0,
                  dd ? dd->TotalIdxCount : 0, dd ? dd->CmdListsCount : 0);
    h.Finish(notes);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
