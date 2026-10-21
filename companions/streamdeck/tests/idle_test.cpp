// The data directory ($data: where it is looked for, in which order, how paths resolve), picture references with a
// region (#xywh=), and the idle scenario: valid for every model, every picture it names exists, its strip and its
// logo keys are drawn (not black). Uses the repository's data/streamdeck/themes/unrealng; no hardware.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "check.h"
#include "sdb/daemondecks.h"
#include "sdb/datadir.h"
#include "sdb/idlescenario.h"
#include "sdb/image.h"
#include "sdb/navigator.h"
#include "sdb/scenariorender.h"

using namespace sdb;
using namespace sdb::scn;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
void Dir(const fs::path& p)
{
    fs::create_directories(p);
}

// pixels clearly brighter than the strip's own background (8, 8, 14)
double LitShare(const Rgb& im)
{
    if (im.Empty())
        return 0;
    size_t lit = 0;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x)
        {
            const auto* p = im.At(x, y);
            lit += std::max({p[0], p[1], p[2]}) > 60;
        }
    return static_cast<double>(lit) / (static_cast<double>(im.w) * im.h);
}

void TestLookup(const fs::path& t)
{
    // macOS: the daemon inside UnrealNG.app, the studio nested in it (Contents/Applications)
    const fs::path app = t / "dmg" / "UnrealNG.app";
    const fs::path studioExe = app / "Contents/Applications/Stream Deck Studio.app/Contents/MacOS";
    const fs::path daemonExe = app / "Contents/MacOS";
    Dir(studioExe);
    Dir(daemonExe);
    const fs::path src = t / "src" / "data";
    auto c = data::Candidates(studioExe, "", src);
    CHECK(c.size() > 5);
    CHECK(c[0] == (app / "Contents/Applications/Stream Deck Studio.app/Contents/Resources").lexically_normal());
    CHECK(c[1] == (app / "Contents/Resources").lexically_normal());
    CHECK(c[2] == studioExe.lexically_normal());
    CHECK(c[3] == (t / "dmg").lexically_normal());  // beside the outermost bundle (a build tree's bin/)
    CHECK(c.back() == src.lexically_normal());

    // nothing has streamdeck/ yet: not found; the source tree is the last resort
    CHECK(data::Find(data::Candidates(daemonExe, "", src)).empty());
    Dir(src / "streamdeck");
    CHECK(data::Find(data::Candidates(daemonExe, "", src)) == src.lexically_normal());
    // the bundle's Resources win over the source tree, for the daemon and the nested studio alike
    Dir(app / "Contents/Resources/streamdeck");
    CHECK(data::Find(data::Candidates(daemonExe, "", src)) == (app / "Contents/Resources").lexically_normal());
    CHECK(data::Find(data::Candidates(studioExe, "", src)) == (app / "Contents/Resources").lexically_normal());
    // the environment wins over everything, when it names a data directory; one without streamdeck/ is passed over
    const fs::path envDir = t / "env";
    Dir(envDir);
    CHECK(data::Find(data::Candidates(daemonExe, envDir.string(), src)) ==
          (app / "Contents/Resources").lexically_normal());
    Dir(envDir / "streamdeck");
    CHECK(data::Find(data::Candidates(daemonExe, envDir.string(), src)) == envDir.lexically_normal());

    // Windows ZIP / Linux tarball / DEB (lib/unreal-ng): beside the executable
    const fs::path flat = t / "usr" / "lib" / "unreal-ng";
    Dir(flat / "streamdeck");
    CHECK(data::Find(data::Candidates(flat, "", src)) == flat.lexically_normal());
    // an FHS install: ../share/unreal-ng
    const fs::path fhsBin = t / "opt" / "bin";
    Dir(fhsBin);
    Dir(t / "opt" / "share" / "unreal-ng" / "streamdeck");
    CHECK(data::Find(data::Candidates(fhsBin, "", src)) == (t / "opt" / "share" / "unreal-ng").lexically_normal());
    // a build inside the source tree: repo/build/bin -> repo/data
    const fs::path devBin = t / "repo" / "cmake-build" / "bin";
    Dir(devBin);
    Dir(t / "repo" / "data" / "streamdeck");
    CHECK(data::Find(data::Candidates(devBin, "", src)) == (t / "repo" / "data").lexically_normal());
    // ... unless the build copied it beside the binaries (cmake/RuntimeData.cmake)
    Dir(devBin / "streamdeck");
    CHECK(data::Find(data::Candidates(devBin, "", src)) == devBin.lexically_normal());

    // $data / $icons
    data::SetRoot(t / "root");
    CHECK(data::Resolve("$data/streamdeck/a.png") == (t / "root" / "streamdeck" / "a.png").generic_string());
    CHECK(data::Resolve("$icons/run.svg") == (t / "root" / "icons" / "run.svg").generic_string());
    CHECK(data::Resolve("$database/x.png") == "$database/x.png");
    CHECK(data::Resolve("pictures/a.png") == "pictures/a.png");
    CHECK(data::Resolve("/abs/a.png") == "/abs/a.png");
    data::SetRoot({});
    // this binary, from the build tree: the repository's data (or the build's copy of it)
    CHECK(fs::exists(data::Root() / idle::kThemeDir / "theme.json"));
}

void TestImageRefs(const fs::path& t)
{
    const auto bad = {"a.png#xywh=1,2,3", "a.png#xywh=1,2,0,4", "a.png#xywh=a,b,c,d", "a.png#xywh="};
    for (const char* r : bad)
        CHECK(!ParseImageRef(r));
    auto r = ParseImageRef("$data/x/a b.png#xywh=pixel:-4,5,16,8");
    CHECK(r && r->file == "$data/x/a b.png" && r->region && r->x == -4 && r->y == 5 && r->w == 16 && r->h == 8);
    r = ParseImageRef("plain.png");
    CHECK(r && r->file == "plain.png" && !r->region);

    // a 4x2 picture: left half red, right half green; a region past its left edge comes out black there
    Rgb im(4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            im.At(x, y)[x < 2 ? 0 : 1] = 255;
    const fs::path file = t / "rg.bmp";
    const auto bmp = EncodeBmp(im);
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bmp.data()),
                                                static_cast<std::streamsize>(bmp.size()));
    auto cut = LoadImageRef(file.string() + "#xywh=-1,0,3,2");
    CHECK(cut && cut->w == 3 && cut->h == 2);
    if (cut)
    {
        CHECK(cut->At(0, 0)[0] == 0 && cut->At(0, 0)[1] == 0);  // outside: black
        CHECK(cut->At(1, 1)[0] == 255);                         // x 0: red
        CHECK(cut->At(2, 0)[0] == 255);                         // x 1: red
    }
    cut = LoadImageRef(file.string() + "#xywh=2,0,2,2");
    CHECK(cut && cut->At(0, 0)[1] == 255 && cut->At(0, 0)[0] == 0);
    // $data in front of a region
    data::SetRoot(t);
    CHECK(LoadImageRef("$data/rg.bmp#xywh=0,0,1,1").has_value());
    data::SetRoot({});
}

// every picture a scenario node names, and its strip
void Pictures(const Node& n, std::set<std::string>& out)
{
    if (!n.face.image.empty())
        out.insert(n.face.image);
    if (!n.strip.image.empty())
        out.insert(n.strip.image);
    for (const auto& c : n.items)
        Pictures(c, out);
}

void TestIdleModels()
{
    std::set<std::string> files;  // each file loaded once (the regions share them)
    for (const Model& m : AllModels())
    {
        const json doc = DaemonDecks::IdleDocument(&m);
        std::vector<Issue> issues;
        auto s = Parse(doc, issues);
        CHECK(s.has_value());
        if (!s)
        {
            for (const auto& i : issues)
                std::printf("  %s: %s: %s\n", std::string(m.name).c_str(), i.node.c_str(), i.message.c_str());
            continue;
        }
        CHECK(s->model == &m);
        CHECK(s->root.title == idle::kTitle);
        std::set<std::string> refs;
        Pictures(s->root, refs);
        for (const auto& ref : refs)
        {
            CHECK(ref.find("ttd") == std::string::npos);  // the timeline is a strip readout, never keys
            CHECK(ref.rfind("$data/streamdeck/themes/unrealng/", 0) == 0);
            const auto r = ParseImageRef(ref);
            CHECK(r && fs::exists(data::Resolve(r->file)));
            if (r)
                files.insert(r->file);
        }
        // pictures only: no key does anything
        bool inert = true;
        for (const auto& c : s->root.items)
            inert &= !c.IsMenu() && c.action.kind == "multi" && c.action.spec.value("steps", json::array()).empty();
        CHECK(inert);
        const size_t n = s->root.items.size();
        if (m.HasLcd())
            CHECK(s->root.strip.content == "image" && !s->root.strip.image.empty());
        if (!m.HasKeyScreens())
            CHECK_EQ(n, 0u);
        else if (m.pid == 0x00C6)  // the Plus XL: the theme's 27 icons at their keys, row 3 blank
            CHECK_EQ(n, 27u);
        else
            CHECK_EQ(n, static_cast<size_t>(m.keys));  // every key shows something
        if (m.HasKeyScreens() && !m.HasLcd())
            CHECK(std::any_of(s->root.items.begin(), s->root.items.end(),
                              [](const Node& c) { return c.id.rfind("logo-", 0) == 0; }));
    }
    for (const auto& f : files)
        CHECK(LoadImage(data::Resolve(f)).has_value());

    // without a data directory: a drawn title, still valid on every model
    for (const Model& m : AllModels())
    {
        std::vector<Issue> issues;
        auto s = Parse(idle::Document(m, {}), issues);
        CHECK(s.has_value());
        if (s && m.HasLcd())
            CHECK(s->root.strip.content == "text" && s->root.strip.text == idle::kTitle);
        else if (s && m.HasKeyScreens())
            CHECK(s->root.items.size() == 1 && s->root.items[0].face.label == idle::kTitle);
    }
}

void TestIdleRender()
{
    // the Plus XL's strip: the banner edge to edge (the ZX stripes at both ends), not black
    const Model& xl = *FindModelByName("plusxl");
    const Scenario s = DaemonDecks::Idle(&xl);
    Navigator nav(s);
    const Page page = Layout(s, nav, xl);
    const Rgb strip = RenderStrip(page, xl.lcdLogicalW, xl.lcdLogicalH, xl.dialTileW);
    CHECK(LitShare(strip) > 0.3);
    CHECK(std::max({strip.At(2, 50)[0], strip.At(2, 50)[1], strip.At(2, 50)[2]}) > 200);
    CHECK(std::max({strip.At(1197, 50)[0], strip.At(1197, 50)[1], strip.At(1197, 50)[2]}) > 200);
    // its keys: an icon; the bottom row blank (no timeline on keys)
    const int face = xl.key.w;
    CHECK(page.keys[0] && LitShare(RenderFace(page.keys[0]->face, "", false, false, 0, face)) > 0.05);
    bool bottomBlank = true;
    for (int k = 27; k < xl.keys; ++k)
        bottomBlank &= page.keys[static_cast<size_t>(k)] == nullptr;
    CHECK(bottomBlank);

    // the Neo's window (the logo: the whole banner would be a thin band there) and the Plus's strip (the banner)
    for (const char* name : {"neo", "plus"})
    {
        const Model& m = *FindModelByName(name);
        const Scenario sm = DaemonDecks::Idle(&m);
        Navigator n(sm);
        CHECK(LitShare(RenderStrip(Layout(sm, n, m), m.lcdLogicalW, m.lcdLogicalH, m.dialTileW)) > 0.1);
        CHECK(sm.root.strip.image.find("logo.png") == std::string::npos ? m.pid != 0x009A : m.pid == 0x009A);
    }
    // a deck without a strip: the logo across its middle row
    const Model& mk2 = *FindModelByName("mk2");
    const Scenario sk = DaemonDecks::Idle(&mk2);
    Navigator nk(sk);
    const Page pk = Layout(sk, nk, mk2);
    const Node* mid = pk.keys[static_cast<size_t>(mk2.cols + mk2.cols / 2)];
    CHECK(mid && mid->id.rfind("logo-", 0) == 0);
    if (mid)
        CHECK(LitShare(RenderFace(mid->face, "", false, false, 0, mk2.key.w)) > 0.05);

    // the drawn title (no data directory) is not black either
    std::vector<Issue> issues;
    const auto drawn = Parse(idle::Document(xl, {}), issues);
    CHECK(drawn.has_value());
    if (drawn)
    {
        Navigator nd(*drawn);
        CHECK(LitShare(RenderStrip(Layout(*drawn, nd, xl), xl.lcdLogicalW, xl.lcdLogicalH, xl.dialTileW)) > 0.02);
    }
}
}  // namespace

void TestIdle()
{
    std::printf("idle scenario, data directory\n");
    const fs::path t = fs::temp_directory_path() / "sdb-idle-test";
    std::error_code ec;
    fs::remove_all(t, ec);
    fs::create_directories(t);
    TestLookup(t);
    TestImageRefs(t);
    TestIdleModels();
    TestIdleRender();
    fs::remove_all(t, ec);
}
