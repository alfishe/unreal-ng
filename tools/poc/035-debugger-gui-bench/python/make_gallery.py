#!/usr/bin/env python3
"""Build gallery/index.html: the screenshots of every variant at every scale / font configuration, with the measured
CPU cost per frame of that configuration, and a 1:1 detail view of the same region in every variant."""
import json
import os
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."
GAL = os.path.join(ROOT, "gallery")
summary = json.load(open(os.path.join(ROOT, "results/scaling/summary.json")))

VARIANTS = [("imgui", "Dear ImGui"), ("rmlui_custom", "RmlUi · custom elements"), ("rmlui_dom", "RmlUi · DOM"),
            ("qtwidgets", "Qt Widgets"), ("qtquick", "Qt Quick")]
CONFIGS = [("scale1.0", "×1 · 12 px", 1.0), ("scale1.5", "×1,5", 1.5), ("scale2.0", "×2", 2.0),
           ("font10", "шрифт 10 px", 1.0), ("font16", "шрифт 16 px", 1.0), ("font20", "шрифт 20 px", 1.0)]

items = []
for key, name in VARIANTS:
    for cfg, label, scale in CONFIGS:
        path = f"{key}_{cfg}.png"
        if not os.path.exists(os.path.join(GAL, path)):
            continue
        s = summary.get(key, {})
        if cfg.startswith("scale"):
            ms = (s.get(cfg) or {}).get("cpu_ms")
        else:
            ms = s.get(cfg)
        items.append({"variant": key, "name": name, "cfg": cfg, "label": label, "scale": scale, "src": path,
                      "cpu_ms": None if ms is None or ms != ms else round(ms, 2)})

page = r"""<title>Debugger GUI Screenshots</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;500;600&display=swap">
<style>
/* Layout: a control strip over a grid of five screenshot cards, then a 1:1 detail strip of the same region */
:root {
  --bg: #F4F5F7; --panel: #FFFFFF; --line: #D8DCE2; --fg: #1A1D22; --muted: #5C6570; --accent: #A8620A; --chip: #EEF0F3;
  --sans: "IBM Plex Sans", system-ui, sans-serif; --mono: "IBM Plex Mono", ui-monospace, monospace;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #0E1014; --panel: #161A20; --line: #2A303A; --fg: #E6E8EB; --muted: #95A0AD; --accent: #E8A33D; --chip: #1F242D; color-scheme: dark } }
:root[data-theme="dark"] {
  --bg: #0E1014; --panel: #161A20; --line: #2A303A; --fg: #E6E8EB; --muted: #95A0AD; --accent: #E8A33D; --chip: #1F242D; color-scheme: dark }
body { background: var(--bg); color: var(--fg); font-family: var(--sans); }
.wrap { max-width: 1600px; margin: 0 auto; padding-block: 24px 48px;
  padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px));
  display: flex; flex-direction: column; gap: 20px; }
h1 { font-size: 22px; font-weight: 600; margin: 0; text-wrap: balance; }
h2 { font-size: 15px; font-weight: 600; margin: 0; }
p.lead { margin: 0; color: var(--muted); max-width: 70ch; line-height: 1.5; }
.controls { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
.controls .lbl { font: 600 11px var(--sans); letter-spacing: .06em; text-transform: uppercase; color: var(--muted); margin-right: 4px; }
.seg { display: flex; flex-wrap: wrap; gap: 4px; }
.seg button { font: 500 13px var(--sans); color: var(--fg); background: var(--chip); border: 1px solid var(--line);
  border-radius: 4px; padding: 8px 12px; min-height: 36px; cursor: pointer; }
.seg button[aria-pressed="true"] { border-color: var(--accent); color: var(--accent); }
.seg button:focus-visible, .card button:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(min(100%, 480px), 1fr)); gap: 16px; }
.card { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; overflow: hidden; display: flex; flex-direction: column; min-width: 0; }
.card header { display: flex; align-items: baseline; gap: 8px; padding: 10px 12px; border-bottom: 1px solid var(--line); }
.card header .n { font-weight: 600; }
.card header .m { margin-left: auto; font: 12px var(--mono); color: var(--muted); font-variant-numeric: tabular-nums; }
.card button { all: unset; display: block; cursor: zoom-in; }
.card img { display: block; width: 100%; height: auto; aspect-ratio: 16 / 10; object-fit: contain; background: #0E1014; }
.detail { display: grid; grid-template-columns: repeat(auto-fill, minmax(min(100%, 300px), 1fr)); gap: 12px; }
.crop { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; overflow: hidden; min-width: 0; }
.crop .t { padding: 6px 10px; font: 12px var(--sans); color: var(--muted); border-bottom: 1px solid var(--line); }
.crop .box { height: 220px; overflow: hidden; position: relative; background: #0E1014; }
.crop .box img { position: absolute; max-width: none; image-rendering: pixelated; }
dialog { border: 1px solid var(--line); background: var(--panel); color: var(--fg); padding: 0; width: min(96vw, 1700px); max-height: 92vh; border-radius: 6px; }
dialog::backdrop { background: rgb(0 0 0 / 0.7); }
dialog .bar { display: flex; align-items: center; gap: 8px; padding: 8px 12px; border-bottom: 1px solid var(--line); position: sticky; top: 0; background: var(--panel); }
dialog .bar span { font: 13px var(--sans); }
dialog .bar button { margin-left: auto; font: 500 13px var(--sans); color: var(--fg); background: var(--chip); border: 1px solid var(--line); border-radius: 4px; padding: 6px 12px; min-height: 36px; cursor: pointer; }
dialog .view { overflow: auto; max-height: calc(92vh - 56px); }
dialog .view img { max-width: none; display: block; }
.note { font: 12px var(--sans); color: var(--muted); line-height: 1.5; max-width: 90ch; }
@media (prefers-reduced-motion: reduce) { * { transition: none !important; } }
</style>

<div class="wrap">
  <h1>Скриншоты GUI отладчика</h1>
  <p class="lead">Один и тот же экран отладчика, данные и раскладка в пяти реализациях. Выберите конфигурацию; число на
    карточке — CPU-время кадра этого варианта в этой конфигурации (no-op GL-драйвер, 50 Гц). Клик по скриншоту открывает его в масштабе 100 %.</p>
  <div class="controls">
    <span class="lbl">Конфигурация</span>
    <div class="seg" id="cfg" role="group" aria-label="Конфигурация"></div>
  </div>
  <div class="grid" id="grid"></div>
  <h2>Деталь 1:1: панель памяти, одна и та же область во всех вариантах</h2>
  <div class="detail" id="detail"></div>
  <p class="note">Скриншоты отрисованы Mesa llvmpipe под Xvfb на 20-м кадре каждого прогона, поэтому данные на экране
    у вариантов немного различаются. Масштаб ×1,5 и ×2 — через родной механизм high-DPI каждого фреймворка (framebuffer scale в ImGui,
    dp ratio в RmlUi, QT_SCALE_FACTOR в Qt); размер шрифта меняет только шрифт содержимого, панели остаются прежнего размера.</p>
</div>

<dialog id="dlg" aria-label="Скриншот в масштабе 100 %">
  <div class="bar"><span id="dlgTitle"></span><button type="button" id="dlgClose">Закрыть</button></div>
  <div class="view"><img id="dlgImg" alt=""></div>
</dialog>

<script>
const ITEMS = __ITEMS__;
const CONFIGS = __CONFIGS__;
let current = CONFIGS[0][0];
try { const saved = localStorage.getItem("gallery-cfg"); if (saved && CONFIGS.some(c => c[0] === saved)) current = saved; } catch (e) {}

const cfgEl = document.getElementById("cfg"), grid = document.getElementById("grid"), detail = document.getElementById("detail");
const dlg = document.getElementById("dlg"), dlgImg = document.getElementById("dlgImg"), dlgTitle = document.getElementById("dlgTitle");
document.getElementById("dlgClose").addEventListener("click", () => dlg.close());
dlg.addEventListener("click", e => { if (e.target === dlg) dlg.close(); });

function el(tag, attrs = {}, kids = []) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) { if (k === "text") e.textContent = v; else e.setAttribute(k, v); }
  for (const k of kids) e.appendChild(k);
  return e;
}

function render() {
  cfgEl.replaceChildren(...CONFIGS.map(([key, label]) => {
    const b = el("button", { type: "button", "aria-pressed": String(key === current), text: label });
    b.addEventListener("click", () => { current = key; try { localStorage.setItem("gallery-cfg", key); } catch (e) {} render(); });
    return b;
  }));
  const shown = ITEMS.filter(i => i.cfg === current);
  grid.replaceChildren(...shown.map(i => {
    const img = el("img", { src: i.src, alt: `${i.name}, ${i.label}`, loading: "lazy" });
    const btn = el("button", { type: "button", "aria-label": `Открыть ${i.name} в масштабе 100 %` }, [img]);
    btn.addEventListener("click", () => { dlgImg.src = i.src; dlgImg.alt = `${i.name}, ${i.label}`; dlgTitle.textContent = `${i.name} · ${i.label}`; dlg.showModal(); });
    return el("article", { class: "card" }, [
      el("header", {}, [el("span", { class: "n", text: i.name }), el("span", { class: "m", text: i.cpu_ms == null ? "—" : `${i.cpu_ms.toFixed(2)} мс CPU / кадр` })]),
      btn]);
  }));
  // 1:1 crop of the memory panel: logical (456, 8), shown at the screenshot's physical scale
  detail.replaceChildren(...shown.map(i => {
    const img = el("img", { src: i.src, alt: `${i.name} memory panel, 1:1`, loading: "lazy" });
    img.style.left = `${-456 * i.scale}px`;
    img.style.top = `${-8 * i.scale}px`;
    return el("div", { class: "crop" }, [el("div", { class: "t", text: i.name }), el("div", { class: "box" }, [img])]);
  }));
}
render();
</script>
"""
page = page.replace("__ITEMS__", json.dumps(items)).replace("__CONFIGS__", json.dumps([[c, l] for c, l, _ in CONFIGS]))
open(os.path.join(GAL, "index.html"), "w").write(page)
print(f"{len(items)} screenshots in gallery/index.html")
