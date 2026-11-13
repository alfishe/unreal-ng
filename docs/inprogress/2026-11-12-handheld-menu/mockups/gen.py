#!/usr/bin/env python3
"""Handheld menu mock-ups: SteamOS-styled HTML pages rendered to 1280x800 PNG by headless Chrome."""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE / "out"

CSS = """
* { box-sizing: border-box; margin: 0; padding: 0; }
html, body { width: 1280px; height: 800px; overflow: hidden; background: #1a1c21; color: #dcdedf;
  font-family: -apple-system, 'SF Pro Text', 'Helvetica Neue', sans-serif; -webkit-font-smoothing: antialiased; }
.top { position: absolute; left: 360px; right: 0; top: 0; height: 60px; display: flex; align-items: center;
  padding: 0 28px 0 36px; gap: 22px; font-size: 19px; }
.top .title { font-weight: 600; color: #fff; }
.top .sub { color: #8b929a; }
.top .grow { flex: 1; }
.state { display: flex; align-items: center; gap: 8px; color: #b8bcbf; }
.dot { width: 10px; height: 10px; border-radius: 5px; background: #59bf40; }
.dot.paused { background: #e5a913; }
.side { position: absolute; left: 0; top: 0; bottom: 60px; width: 360px; background: #2b2d33; padding-top: 60px; }
.side .brand { position: absolute; top: 18px; left: 34px; font-size: 15px; letter-spacing: 2px; color: #8b929a; font-weight: 600; }
.nav { height: 63px; display: flex; align-items: center; gap: 22px; padding-left: 36px; font-size: 23px; color: #dcdedf; position: relative; }
.nav svg { width: 30px; height: 30px; stroke: #b8bcbf; fill: none; stroke-width: 2; stroke-linecap: round; stroke-linejoin: round; }
.nav.sel { background: linear-gradient(90deg, #245880 0%, #293441 100%); color: #fff; }
.nav.sel::before { content: ''; position: absolute; left: 0; top: 0; bottom: 0; width: 4px; background: #1a9fff; }
.nav.sel svg { stroke: #fff; }
.nav.dim { color: #8b929a; }
.sep { height: 1px; background: #414248; margin: 12px 36px; }
.main { position: absolute; left: 360px; right: 0; top: 60px; bottom: 60px; padding: 30px 36px 0 36px; overflow: hidden; }
h1 { font-size: 31px; font-weight: 600; color: #fff; margin-bottom: 16px; }
h2 { font-size: 21px; font-weight: 600; color: #dcdedf; margin: 22px 0 12px; }
.row { background: #23262e; min-height: 70px; display: flex; align-items: center; padding: 10px 18px; margin-bottom: 4px; gap: 16px; }
.row.sel { outline: 2px solid #1a9fff; outline-offset: -2px; background: #2d3340; }
.row .label { flex: 1; font-size: 22px; color: #dcdedf; }
.row .label small { display: block; font-size: 16px; color: #8b929a; margin-top: 3px; }
.dd { width: 375px; height: 60px; background: #44464d; display: flex; align-items: center; padding: 0 22px; font-size: 22px; color: #fff; justify-content: space-between; }
.dd::after { content: ''; border: 9px solid transparent; border-top: 10px solid #dcdedf; margin-top: 10px; }
.btn { height: 60px; background: #44464d; display: inline-flex; align-items: center; justify-content: center; padding: 0 26px; font-size: 21px; color: #fff; min-width: 150px; }
.btn.sel { background: #fff; color: #1a1c21; }
.btn.primary { background: #1a9fff; }
.btns { display: flex; gap: 10px; }
.tog { width: 64px; height: 34px; border-radius: 17px; background: #44464d; position: relative; }
.tog::after { content: ''; position: absolute; width: 26px; height: 26px; border-radius: 13px; background: #dcdedf; top: 4px; left: 4px; }
.tog.on { background: #1a9fff; } .tog.on::after { left: 34px; background: #fff; }
.slider { width: 375px; display: flex; align-items: center; gap: 16px; font-size: 20px; color: #fff; }
.slider .bar { flex: 1; height: 6px; background: #44464d; border-radius: 3px; position: relative; }
.slider .fill { position: absolute; left: 0; top: 0; bottom: 0; background: #1a9fff; border-radius: 3px; }
.slider .knob { position: absolute; top: -9px; width: 24px; height: 24px; border-radius: 12px; background: #fff; margin-left: -12px; }
.foot { position: absolute; left: 0; right: 0; bottom: 0; height: 60px; background: #0e0f12; display: flex; align-items: center; padding: 0 36px; gap: 34px; font-size: 18px; font-weight: 600; letter-spacing: 1px; color: #dcdedf; }
.foot .grow { flex: 1; }
.hint { display: flex; align-items: center; gap: 12px; }
.pill { height: 34px; min-width: 34px; border-radius: 17px; background: #dcdedf; color: #0e0f12; display: inline-flex; align-items: center; justify-content: center; font-size: 17px; font-weight: 700; padding: 0 10px; }
.pill.round { width: 34px; padding: 0; }
.zx { image-rendering: pixelated; display: block; }
.muted { color: #8b929a; }
.shade { position: absolute; inset: 0; background: rgba(14,15,18,0.72); }
.modal { position: absolute; background: #23262e; box-shadow: 0 20px 60px rgba(0,0,0,0.7); }
.opt { height: 54px; display: flex; align-items: center; padding: 0 24px; font-size: 21px; color: #dcdedf; gap: 14px; }
.opt.sel { background: #1a9fff; color: #fff; }
.opt .chk { width: 22px; color: #1a9fff; font-weight: 700; }
.opt.sel .chk { color: #fff; }
.opt .meta { margin-left: auto; color: #8b929a; font-size: 17px; }
.opt.sel .meta { color: #d6ecff; }
"""

ICONS = {
    "play": '<polygon points="9,6 24,15 9,24"/>',
    "slots": '<rect x="4" y="4" width="9" height="9"/><rect x="17" y="4" width="9" height="9"/><rect x="4" y="17" width="9" height="9"/><rect x="17" y="17" width="9" height="9"/>',
    "media": '<rect x="3" y="7" width="24" height="16" rx="2"/><circle cx="10" cy="15" r="3"/><circle cx="20" cy="15" r="3"/><line x1="10" y1="18" x2="20" y2="18"/>',
    "machine": '<rect x="7" y="7" width="16" height="16" rx="1"/><rect x="11" y="11" width="8" height="8"/><line x1="11" y1="3" x2="11" y2="7"/><line x1="19" y1="3" x2="19" y2="7"/><line x1="11" y1="23" x2="11" y2="27"/><line x1="19" y1="23" x2="19" y2="27"/><line x1="3" y1="11" x2="7" y2="11"/><line x1="3" y1="19" x2="7" y2="19"/><line x1="23" y1="11" x2="27" y2="11"/><line x1="23" y1="19" x2="27" y2="19"/>',
    "controls": '<path d="M8 9h14a5 5 0 0 1 5 5v4a4 4 0 0 1-7 2.6L18 18h-6l-2 2.6A4 4 0 0 1 3 18v-4a5 5 0 0 1 5-5z"/><line x1="9" y1="12" x2="9" y2="16"/><line x1="7" y1="14" x2="11" y2="14"/><circle cx="20" cy="13" r="0.8"/><circle cx="22" cy="16" r="0.8"/>',
    "display": '<rect x="3" y="5" width="24" height="16" rx="1"/><line x1="11" y1="25" x2="19" y2="25"/><line x1="15" y1="21" x2="15" y2="25"/>',
    "sound": '<polygon points="5,12 10,12 16,7 16,23 10,18 5,18"/><path d="M20 11a5 5 0 0 1 0 8"/><path d="M23 8a9 9 0 0 1 0 14"/>',
    "ttd": '<circle cx="16" cy="16" r="10"/><polyline points="16,10 16,16 20,18"/><polyline points="3,9 6,13 10,10"/>',
    "settings": '<circle cx="15" cy="15" r="4"/><path d="M15 3v4M15 23v4M3 15h4M23 15h4M6.5 6.5l2.8 2.8M20.7 20.7l2.8 2.8M6.5 23.5l2.8-2.8M20.7 9.3l2.8-2.8"/>',
    "quit": '<path d="M12 5H6v20h6"/><polyline points="18,9 24,15 18,21"/><line x1="11" y1="15" x2="24" y2="15"/>',
    "folder": '<path d="M3 8h9l2 3h13v13H3z"/>',
    "tape": '<rect x="3" y="7" width="24" height="16" rx="2"/><circle cx="10" cy="15" r="3"/><circle cx="20" cy="15" r="3"/>',
    "disk": '<rect x="5" y="4" width="20" height="22" rx="1"/><rect x="9" y="4" width="12" height="7"/><rect x="9" y="17" width="12" height="9"/>',
    "snap": '<rect x="4" y="8" width="22" height="16" rx="2"/><circle cx="15" cy="16" r="4"/><path d="M10 8l2-3h6l2 3"/>',
}


def icon(name, size=30, stroke="#b8bcbf"):
    return f'<svg viewBox="0 0 30 30" style="width:{size}px;height:{size}px;stroke:{stroke};fill:none;stroke-width:2;stroke-linecap:round;stroke-linejoin:round">{ICONS[name]}</svg>'


NAV = [("play", "Resume"), ("slots", "Save &amp; Load"), ("media", "Media"), ("machine", "Machine"),
       ("controls", "Controls"), ("display", "Display"), ("sound", "Sound"), ("ttd", "Time Travel"),
       None, ("settings", "Settings"), ("quit", "Quit")]


def side(selected):
    items = []
    for entry in NAV:
        if entry is None:
            items.append('<div class="sep"></div>')
            continue
        key, label = entry
        cls = "nav sel" if label.startswith(selected) else "nav"
        items.append(f'<div class="{cls}"><svg viewBox="0 0 30 30">{ICONS[key]}</svg>{label}</div>')
    return '<div class="side"><div class="brand">UNREAL NG</div>' + "".join(items) + "</div>"


def top(paused=False):
    dot = "dot paused" if paused else "dot"
    state = "Paused" if paused else "Running · 50 fps"
    return (f'<div class="top"><span class="title">Rick Dangerous</span><span class="sub">Pentagon 128K · TR-DOS</span>'
            f'<span class="grow"></span><span class="state"><span class="{dot}"></span>{state}</span>'
            f'<span class="muted">6:08 PM</span></div>')


def foot(hints):
    left = '<div class="hint"><span class="pill">☰</span>CLOSE</div><span class="grow"></span>'
    right = "".join(f'<div class="hint"><span class="pill{" round" if len(b) == 1 else ""}">{b}</span>{t}</div>' for b, t in hints)
    return f'<div class="foot">{left}{right}</div>'


def page(name, body, selected, hints, paused=True, overlay=""):
    html = (f'<!doctype html><html><head><meta charset="utf-8"><style>{CSS}</style></head><body>'
            f'{side(selected)}{top(paused)}<div class="main">{body}</div>{foot(hints)}{overlay}</body></html>')
    (OUT / f"{name}.html").write_text(html)


def row(label, control, sub="", sel=False):
    sub_html = f"<small>{sub}</small>" if sub else ""
    return f'<div class="row{" sel" if sel else ""}"><div class="label">{label}{sub_html}</div>{control}</div>'


def dd(value):
    return f'<div class="dd">{value}</div>'


def tog(on):
    return f'<div class="tog{" on" if on else ""}"></div>'


def slider(pct, text):
    return (f'<div class="slider"><div class="bar"><div class="fill" style="width:{pct}%"></div>'
            f'<div class="knob" style="left:{pct}%"></div></div><span style="width:70px;text-align:right">{text}</span></div>')


def img(name):
    return f"img/{name}"


def build():
    OUT.mkdir(exist_ok=True)
    (OUT / "img").mkdir(exist_ok=True)
    for f in (HERE / "img").iterdir():
        target = OUT / "img" / f.name
        if not target.exists():
            target.symlink_to(f)

    # 1. Resume: the paused picture, quick actions, recent files
    recent = [("tape", "Myth.tzx", "Tape · yesterday"), ("disk", "Zynaps.trd", "Disk A · 2 days ago"),
              ("snap", "Elite-docked.sna", "Snapshot · last week")]
    recent_html = "".join(
        f'<div class="row" style="min-height:58px">{icon(k, 26)}<div class="label" style="font-size:20px">{n}</div>'
        f'<span class="muted" style="font-size:17px">{d}</span></div>' for k, n, d in recent)
    body = f'''
<div style="display:flex;gap:28px">
  <div style="position:relative">
    <img class="zx" src="{img('rick-dangerous-level1-ladders.png')}" style="width:448px;height:336px">
    <div style="position:absolute;left:14px;bottom:14px;background:rgba(14,15,18,.8);padding:6px 12px;font-size:16px;color:#e5a913">⏸ PAUSED WHILE MENU IS OPEN</div>
  </div>
  <div style="flex:1;display:flex;flex-direction:column;gap:8px">
    <div class="btn sel" style="justify-content:flex-start;gap:14px">{icon('play', 24, '#1a1c21')}Resume</div>
    <div class="btn" style="justify-content:space-between">Quick Save<span class="muted" style="font-size:17px">slot 1 · L4</span></div>
    <div class="btn" style="justify-content:space-between">Quick Load<span class="muted" style="font-size:17px">slot 1 · 2 min ago</span></div>
    <div class="btn" style="justify-content:space-between">Rewind 10 s<span class="muted" style="font-size:17px">TTD on</span></div>
    <div class="btn" style="justify-content:space-between">Reset<span class="muted" style="font-size:17px">hold Ⓐ: hard</span></div>
  </div>
</div>
<h2>Recent</h2>{recent_html}'''
    page("01-resume", body, "Resume", [("A", "SELECT"), ("B", "BACK")], paused=True)

    # 2. Save & Load: nine slots with pictures
    shots = ["rick-dangerous-level1-ladders.png", "rick-dangerous-level3.png", "rick-dangerous-2-level1-section6.png",
             None, None, None, None, None, None]
    when = ["2 min ago", "Today 14:20", "Yesterday", "", "", "", "", "", ""]
    cells = []
    for i, shot in enumerate(shots):
        sel = i == 1
        border = "outline:3px solid #1a9fff;outline-offset:2px;" if sel else ""
        if shot:
            pic = f'<img class="zx" src="{img(shot)}" style="width:256px;height:192px">'
            cap = f'<div style="display:flex;justify-content:space-between;font-size:17px;margin-top:6px"><span style="color:#fff">Slot {i + 1}</span><span class="muted">{when[i]}</span></div>'
        else:
            pic = '<div style="width:256px;height:192px;background:#23262e;display:flex;align-items:center;justify-content:center;color:#5c6168;font-size:18px">Empty</div>'
            cap = f'<div style="font-size:17px;margin-top:6px;color:#8b929a">Slot {i + 1}</div>'
        cells.append(f'<div style="{border}">{pic}{cap}</div>')
    body = ('<div style="display:flex;justify-content:space-between;align-items:baseline"><h1>Save &amp; Load</h1>'
            '<span class="muted" style="font-size:17px">Rick Dangerous · per game, 9 slots</span></div>'
            '<div style="display:grid;grid-template-columns:repeat(3,256px);gap:16px 30px">' + "".join(cells) + "</div>")
    page("02-slots", body, "Save", [("A", "LOAD"), ("X", "SAVE HERE"), ("Y", "DELETE"), ("B", "BACK")], paused=True)

    # 3. Machine
    body = ("<h1>Machine</h1>"
            + row("Model", dd("Pentagon"), sel=True)
            + row("Memory", dd("128 KB"))
            + row("Disk interface", dd("TR-DOS (Beta 128)"))
            + row("Speed", slider(25, "100 %"), "Turbo with R5 while held")
            + "<h2>Reset</h2>"
            + row("Reset the machine", '<div class="btns"><div class="btn">Soft</div><div class="btn">Hard</div></div>',
                  "Hard reset clears memory")
            + row("NMI (Magic button)", '<div class="btn">Press</div>'))
    page("03-machine", body, "Machine", [("A", "SELECT"), ("B", "BACK")])

    # 4. Model picker over the machine page
    models = [("ZX Spectrum 48K", "48 KB"), ("ZX Spectrum 128K", "128 KB"), ("ZX Spectrum +2A", "128 KB"),
              ("ZX Spectrum +3", "128 KB · disk"), ("Pentagon", "128–1024 KB"), ("Scorpion ZS 256", "256 KB"),
              ("ZX-Evo (TS-Config)", "4 MB"), ("ZX Spectrum Next", "2 MB"), ("Sprinter 2000", "4 MB")]
    opts = "".join(
        f'<div class="opt{" sel" if n == "ZX Spectrum 128K" else ""}"><span class="chk">{"✓" if n == "Pentagon" else ""}</span>{n}<span class="meta">{m}</span></div>'
        for n, m in models)
    overlay = (f'<div class="shade"></div><div class="modal" style="left:390px;top:96px;width:560px;padding:18px 0">'
               f'<div style="padding:0 24px 12px;font-size:22px;font-weight:600;color:#fff">Model</div>{opts}'
               f'<div style="padding:12px 24px 0;font-size:16px;color:#e5a913">Changing the model restarts the machine. Quick save first?</div></div>')
    page("04-model-picker", body, "Machine", [("A", "SELECT"), ("B", "CANCEL")], overlay=overlay)

    # 5. Media
    blocks = [("Program: rick", "header · 17 bytes", False), ("rick", "BASIC · 312 bytes", False),
              ("Bytes: scr", "header", False), ("scr", "screen · 6912 bytes", True), ("Bytes: code", "header", False),
              ("code", "41 266 bytes", False)]
    blist = "".join(
        f'<div style="display:flex;gap:14px;padding:7px 14px;font-size:18px;{"background:#2d3340;color:#fff" if cur else ""}">'
        f'<span style="width:22px;color:#1a9fff">{"▶" if cur else ""}</span><span style="flex:1">{n}</span><span class="muted">{d}</span></div>'
        for n, d, cur in blocks)
    body = f'''<h1>Media</h1>
<div class="row" style="align-items:flex-start;padding:16px 18px">{icon('tape', 30)}
  <div style="flex:1"><div style="font-size:22px;color:#fff">Rick Dangerous.tzx</div>
    <div class="muted" style="font-size:16px;margin:3px 0 10px">Tape · block 4 of 6 · 0:41 / 3:12 · instant load on</div>
    <div style="height:6px;background:#44464d;border-radius:3px;margin-bottom:10px"><div style="width:21%;height:6px;background:#1a9fff;border-radius:3px"></div></div>
    {blist}
  </div>
  <div style="display:flex;flex-direction:column;gap:8px"><div class="btn sel" style="min-width:130px">Play</div><div class="btn" style="min-width:130px">Rewind</div><div class="btn" style="min-width:130px">Eject</div></div>
</div>
{row(icon('disk', 28) + '<span style="margin-left:14px">A: Rick Dangerous.trd</span>', '<div class="btns"><div class="btn">Change</div><div class="btn">Eject</div></div>', '')}
{row(icon('disk', 28) + '<span style="margin-left:14px" class="muted">B: empty</span>', '<div class="btn">Insert</div>')}
'''
    body = body.replace('<div class="label">' + icon('disk', 28), '<div class="label" style="display:flex;align-items:center">' + icon('disk', 28))
    page("05-media", body, "Media", [("A", "SELECT"), ("X", "OPEN FILE"), ("B", "BACK")])

    # 6. File browser (full-screen, opened from Media / Resume)
    files = [("folder", "..", "", False), ("folder", "Arcade", "214 items", False), ("folder", "Platform", "388 items", False),
             ("tape", "Myth.tzx", "tape · 48 KB", False), ("tape", "Rick Dangerous.tzx", "tape · 44 KB", False),
             ("disk", "Zynaps.trd", "disk · 640 KB", True), ("tape", "Tujad.tap", "tape · 41 KB", False),
             ("snap", "Elite-docked.sna", "snapshot · 128 KB", False), ("tape", "Earth Shaker.tzx", "tape · 39 KB", False)]
    flist = "".join(
        f'<div class="opt{" sel" if s else ""}" style="height:52px">{icon(k, 24, "#fff" if s else "#b8bcbf")}{n}<span class="meta">{d}</span></div>'
        for k, n, d, s in files)
    body = f'''<div style="display:flex;justify-content:space-between;align-items:baseline"><h1>Open file</h1>
<span class="muted" style="font-size:17px">SD card › zx › games › Platform</span></div>
<div style="display:flex;gap:28px">
  <div style="width:470px;background:#23262e;padding:6px 0">{flist}</div>
  <div style="flex:1">
    <img class="zx" src="{img('zynaps-level-02-1.png')}" style="width:384px;height:288px">
    <div style="font-size:22px;color:#fff;margin-top:12px">Zynaps</div>
    <div class="muted" style="font-size:17px;margin-top:4px">Hewson · 1987 · TR-DOS disk</div>
    <div class="muted" style="font-size:17px;margin-top:2px">Profile: Kempston, R4 = fire 2</div>
    <div class="btns" style="margin-top:16px"><div class="btn primary">Run</div><div class="btn">Insert to B:</div></div>
  </div>
</div>'''
    page("06-open-file", body, "Media", [("A", "RUN"), ("X", "INSERT ONLY"), ("Y", "FAVOURITE"), ("B", "UP")])

    # 7. Display
    body = ("<h1>Display</h1>"
            + row("Picture fit", dd("Fill height, crop border"), sel=True)
            + row("CRT effect", dd("Consumer TV"), "Scanlines, mask, bloom")
            + row("CRT strength", slider(60, "60 %"))
            + row("Flicker blending", tog(True), "Gigascreen and 25 Hz multicolour")
            + row("Panel refresh", dd("50 Hz (match the machine)"))
            + row("Show HUD", tog(True), "Tape counter, disk activity, toasts"))
    page("07-display", body, "Display", [("A", "SELECT"), ("B", "BACK")])

    # 8. Time travel: filmstrip and the timeline
    strip = ["rick-dangerous-level1-ladders.png"] * 5
    thumbs = "".join(
        f'<div style="{"outline:3px solid #1a9fff;outline-offset:2px;" if i == 3 else "opacity:.75"}"><img class="zx" src="{img(s)}" style="width:160px;height:120px">'
        f'<div class="muted" style="font-size:15px;margin-top:4px;text-align:center">{"live" if i == 4 else f"-{(4 - i) * 5} s"}</div></div>'
        for i, s in enumerate(strip))
    body = f'''<h1>Time Travel</h1>
<div style="display:flex;gap:14px;margin-bottom:22px">{thumbs}</div>
<div style="position:relative;height:40px;margin:0 6px 8px">
  <div style="position:absolute;left:0;right:0;top:17px;height:6px;background:#44464d;border-radius:3px"></div>
  <div style="position:absolute;left:0;width:82%;top:17px;height:6px;background:#1a9fff;border-radius:3px"></div>
  <div style="position:absolute;left:82%;top:8px;width:24px;height:24px;margin-left:-12px;border-radius:12px;background:#fff"></div>
  <div style="position:absolute;left:30%;top:0;font-size:14px;color:#e5a913">◆</div>
  <div style="position:absolute;left:61%;top:0;font-size:14px;color:#e5a913">◆</div>
</div>
<div style="display:flex;justify-content:space-between;font-size:16px" class="muted"><span>recording since 18:01 · 7 min</span><span>-5 s from live</span></div>
{row('Recording', tog(True), 'Rewind with L2 in game; menu shows the last 10 minutes')}
{row('Bookmarks', '<div class="btns"><div class="btn">Add here</div><div class="btn">List · 2</div></div>')}'''
    page("08-time-travel", body, "Time", [("L1", "−5 S"), ("R1", "+5 S"), ("A", "GO HERE"), ("X", "BRANCH"), ("B", "BACK")], paused=True)


def render():
    from playwright.sync_api import sync_playwright
    with sync_playwright() as pw:
        browser = pw.chromium.launch()
        page_ = browser.new_page(viewport={"width": 1280, "height": 800}, device_scale_factor=1)
        for html in sorted(OUT.glob("*.html")):
            page_.goto(html.as_uri())
            page_.wait_for_load_state("networkidle")
            page_.screenshot(path=str(html.with_suffix(".png")))
            print(html.with_suffix(".png").name)
        browser.close()


if __name__ == "__main__":
    build()
    if "--no-render" not in sys.argv:
        render()
