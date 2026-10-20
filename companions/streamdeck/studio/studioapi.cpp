#include "studioapi.h"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QToolButton>
#include <chrono>
#include <thread>

#include "decklink.h"
#include "deckwidget.h"
#include "gui.h"
#include "mainwindow.h"
#include "recorder.h"
#include "virtualdeck.h"

using namespace sdb;
using automation::json;
using automation::Param;
using automation::Reply;
using automation::Request;

namespace
{
QString Qs(const std::string& s)
{
    return QString::fromStdString(s);
}
std::string NameOf(const Model& m)
{
    return std::string(m.name);
}
const char* SrcName(int s)
{
    static const char* const n[] = {"daemon", "viewer", "deck", "app", "info"};
    return n[s];
}
json ModelJson(const Model& m)
{
    return {
        {"name", NameOf(m)},
        {"pid", m.pid},
        {"keys", m.keys},
        {"rows", m.rows},
        {"cols", m.cols},
        {"dials", m.dials},
        {"touch_points", m.touchPoints},
        {"key_size", m.key.w},
        {"window",
         m.HasLcd() ? json{{"width", m.lcdLogicalW}, {"height", m.lcdLogicalH}, {"touch", m.lcdKind == LcdKind::Region}}
                    : json()}};
}
void SleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
}  // namespace

StudioApi::StudioApi(MainWindow* w)
    : _w(w),
      _api(
          "streamdeck-studio", "1.0",
          "Stream Deck Studio: the software Stream Deck of unreal-ng (a viewer of unreal-streamdeck: a mirror of the "
          "real deck or a deck of its own) and its scenario editor. Connection: role (mirror / own), daemon state. "
          "Device: model, settings, input (keys, dials, "
          "touch strip), screenshots (area lcd = the display only, deck = the device, window = everything; target "
          "editor for "
          "the scenario editor), protocol log, video recording. Editor: document, nodes (add / move / patch / delete), "
          "undo, run on the deck through unreal-streamdeck. Start with search_api.")
{
    AddDeviceRoutes();
    AddInputRoutes();
    AddViewRoutes();
}

StudioApi::~StudioApi()
{
    _server.Stop();
}

bool StudioApi::Start(int port, QString* error)
{
    std::string err;
    if (!_server.Start(_api, "127.0.0.1", port, &err))
    {
        if (error)
            *error = Qs(err);
        return false;
    }
    return true;
}

std::string StudioApi::Png(const QImage& img)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes.toStdString();
}

// ---------------------------------------------------------------- device
void StudioApi::AddDeviceRoutes()
{
    MainWindow* w = _w;
    _api.Add({"GET",
              "/api/v1/device",
              "The software deck: model, geometry, serial, brightness, asleep",
              "",
              {"device"},
              {},
              nullptr,
              [w](const Request&) {
                  return OnGui([w] {
                      const Model& m = w->_deck->GetModel();
                      json j = ModelJson(m);
                      j["serial"] = w->_deck->Serial().toStdString();
                      j["brightness"] = w->_deck->Brightness();
                      j["asleep"] = w->_deck->Asleep();
                      return Reply::Ok(j);
                  });
              }});
    _api.Add({"GET",
              "/api/v1/models",
              "Every model the studio can be (name, PID, keys, dials, window)",
              "",
              {"device", "model"},
              {},
              nullptr,
              [](const Request&) {
                  json a = json::array();
                  for (const auto& m : AllModels())
                      a.push_back(ModelJson(m));
                  return Reply::Ok({{"models", a}});
              }});
    _api.Add(
        {"PUT",
         "/api/v1/device/model",
         "Switch the model: an own deck of this model (client), or the deck's model (--listen)",
         "A mirror follows the real deck's model; asking for a model makes the studio an own deck of it "
         "(the same as PUT /api/v1/connection {role: own, model}).",
         {"device", "model"},
         {{"model", "body", "string", "name (plusxl plus xl classic mk2 neo mini original pedal ...) or PID", true}},
         json{{"model", "plusxl"}},
         [w](const Request& r) {
             const Model* m = FindModelByName(r.Str("model"));
             if (!m)
                 return Reply::Error(404, "unknown model '" + r.Str("model") + "'", "GET /api/v1/models");
             return OnGui([w, m] {
                 w->SelectModel(m->pid);
                 return Reply::Ok(ModelJson(w->_deck->GetModel()));
             });
         }});
    _api.Add(
        {"GET",
         "/api/v1/connection",
         "What drives the deck: the daemon (address, state, role, deck serial, model, real or own) or a --listen host",
         "",
         {"connection", "daemon", "role"},
         {},
         nullptr,
         [w](const Request&) {
             return OnGui([w] {
                 json j = w->_link->Describe();
                 const auto s = w->_deck->GetStats();
                 j["reports"] = s.reports;
                 j["images"] = s.images;
                 return Reply::Ok(j);
             });
         }});
    _api.Add(
        {"PUT",
         "/api/v1/connection",
         "Switch the role: mirror a real deck, or a deck of its own of a model (the daemon runs a scenario for it)",
         "",
         {"connection", "role", "mirror", "own"},
         {{"role", "body", "string", "mirror | own", true, {"mirror", "own"}},
          {"deck", "body", "string", "mirror: a deck serial, default first"},
          {"model", "body", "string", "own: model name or PID, default plusxl"}},
         json{{"role", "own"}, {"model", "neo"}},
         [w](const Request& r) {
             const std::string role = r.Str("role");
             if (role != "mirror" && role != "own")
                 return Reply::Error(400, "role: mirror or own");
             return OnGui([&] {
                 QString err;
                 const bool own = role == "own";
                 if (!w->SetRole(own, Qs(own ? r.Str("model", "plusxl") : r.Str("deck", "first")), &err))
                     return Reply::Error(own ? 404 : 409, err.toStdString());
                 return Reply::Ok(w->_link->Describe());
             });
         }});
    _api.Add({"GET",
              "/api/v1/settings",
              "View settings: bezel mask, visible-area guide, firmware quirks",
              "",
              {"settings"},
              {},
              nullptr,
              [w](const Request&) {
                  return OnGui([w] {
                      return Reply::Ok({{"bezel_mask", w->_mask->isChecked()},
                                        {"visible_area", w->_safe->isChecked()},
                                        {"firmware_quirks", w->_quirks->isChecked()}});
                  });
              }});
    _api.Add({"PUT",
              "/api/v1/settings",
              "Change view settings (any subset)",
              "",
              {"settings"},
              {{"bezel_mask", "body", "boolean", "hide the LCD behind the bezel"},
               {"visible_area", "body", "boolean", "outline the visible part of each key"},
               {"firmware_quirks", "body", "boolean", "reproduce measured firmware behaviour"}},
              json{{"bezel_mask", false}},
              [w](const Request& r) {
                  return OnGui([w, &r] {
                      if (r.Has("bezel_mask"))
                          w->_mask->setChecked(r.Flag("bezel_mask"));
                      if (r.Has("visible_area"))
                          w->_safe->setChecked(r.Flag("visible_area"));
                      if (r.Has("firmware_quirks"))
                          w->_quirks->setChecked(r.Flag("firmware_quirks"));
                      return Reply::Ok({{"bezel_mask", w->_mask->isChecked()},
                                        {"visible_area", w->_safe->isChecked()},
                                        {"firmware_quirks", w->_quirks->isChecked()}});
                  });
              }});
}

// ---------------------------------------------------------------- input
void StudioApi::AddInputRoutes()
{
    MainWindow* w = _w;
    auto keyIndexOf = [w](const Request& r, std::string& err) -> int {
        return OnGui([&]() -> int {
            const Model& m = w->_deck->GetModel();
            int k = -1;
            if (r.Has("index"))
                k = static_cast<int>(r.Num("index", -1));
            else if (r.Has("row") && r.Has("col"))
            {
                if (auto i = KeyIndex(m, static_cast<int>(r.Num("row")), static_cast<int>(r.Num("col"))))
                    k = *i;
            }
            if (k < 0 || k >= m.keys)
            {
                err = "no key there (" + std::to_string(m.rows) + "x" + std::to_string(m.cols) + ", index 0.." +
                      std::to_string(m.keys - 1) + ")";
                return -1;
            }
            return k;
        });
    };
    auto press = [w](int button, const std::string& action, int holdMs) {
        if (action == "down" || action == "click")
            OnGui([w, button] { w->_deck->PressButton(button, true); });
        if (action == "click")
            SleepMs(holdMs);
        if (action == "up" || action == "click")
            OnGui([w, button] { w->_deck->PressButton(button, false); });
    };
    _api.Add({"POST",
              "/api/v1/input/key",
              "Press a key like a finger: click (down, hold, up), down or up",
              "",
              {"input", "key", "press"},
              {{"index", "body", "integer", "key index (row-major)"},
               {"row", "body", "integer", "or row + col"},
               {"col", "body", "integer", ""},
               {"action", "body", "string", "click (default), down, up", false, {"click", "down", "up"}},
               {"hold_ms", "body", "integer", "click hold time, default 80 (>= 600 on a back key = home)"},
               {"source",
                "body",
                "string",
                "studio (default: sent to the daemon as the deck's input) or deck (shown as a press on the real deck, "
                "nothing sent)",
                false,
                {"studio", "deck"}}},
              json{{"row", 1}, {"col", 0}},
              [w, keyIndexOf, press](const Request& r) {
                  std::string err;
                  const int k = keyIndexOf(r, err);
                  if (k < 0)
                      return Reply::Error(400, err);
                  const std::string action = r.Str("action", "click");
                  if (r.Str("source", "studio") == "deck")
                  {  // what a press on the real deck looks like here
                      auto show = [w, k](bool down) {
                          OnGui([w, k, down] {
                              sdb::Event e;
                              e.type = down ? sdb::EventType::KeyDown : sdb::EventType::KeyUp;
                              e.index = k;
                              w->_deck->ShowRemote(e);
                          });
                      };
                      if (action != "up")
                          show(true);
                      if (action == "click")
                      {
                          SleepMs(static_cast<int>(r.Num("hold_ms", 80)));
                          show(false);
                      }
                      if (action == "up")
                          show(false);
                      return Reply::Ok({{"key", k}, {"action", action}, {"source", "deck"}});
                  }
                  press(k, action, static_cast<int>(r.Num("hold_ms", 80)));
                  SleepMs(30);  // let the host react before the caller looks
                  return Reply::Ok({{"key", k}, {"action", action}});
              }});
    _api.Add({"POST",
              "/api/v1/input/sensor",
              "Touch a Neo sensor (touch point)",
              "",
              {"input", "sensor", "neo"},
              {{"index", "body", "integer", "sensor 0..1", true},
               {"action", "body", "string", "click, down, up", false, {"click", "down", "up"}}},
              nullptr,
              [w, press](const Request& r) {
                  const auto m = OnGui(
                      [w] { return std::make_pair(w->_deck->GetModel().keys, w->_deck->GetModel().touchPoints); });
                  const int i = static_cast<int>(r.Num("index", -1));
                  if (i < 0 || i >= m.second)
                      return Reply::Error(400, "this model has " + std::to_string(m.second) + " sensors");
                  press(m.first + i, r.Str("action", "click"), static_cast<int>(r.Num("hold_ms", 80)));
                  return Reply::Ok({{"sensor", i}});
              }});
    _api.Add({"POST",
              "/api/v1/input/dial",
              "Turn or press a dial",
              "",
              {"input", "dial", "encoder", "turn"},
              {{"index", "body", "integer", "dial index", true},
               {"action",
                "body",
                "string",
                "turn (default), press (click), down, up",
                false,
                {"turn", "press", "down", "up"}},
               {"ticks", "body", "integer", "turn: detents, negative = counter-clockwise (default 1)"}},
              json{{"index", 0}, {"action", "turn"}, {"ticks", -3}},
              [w](const Request& r) {
                  const int dials = OnGui([w] { return static_cast<int>(w->_deck->GetModel().dials); });
                  const int i = static_cast<int>(r.Num("index", -1));
                  if (i < 0 || i >= dials)
                      return Reply::Error(400, "this model has " + std::to_string(dials) + " dials");
                  const std::string action = r.Str("action", "turn");
                  if (action == "turn")
                      OnGui([w, i, t = static_cast<int>(r.Num("ticks", 1))] { w->_deck->TurnDial(i, t); });
                  else
                  {
                      if (action == "press" || action == "down")
                          OnGui([w, i] { w->_deck->PressDial(i, true); });
                      if (action == "press")
                          SleepMs(static_cast<int>(r.Num("hold_ms", 80)));
                      if (action == "press" || action == "up")
                          OnGui([w, i] { w->_deck->PressDial(i, false); });
                  }
                  SleepMs(30);
                  return Reply::Ok({{"dial", i}, {"action", action}});
              }});
    _api.Add({"POST",
              "/api/v1/input/touch",
              "Touch the strip (+ / + XL): tap, press (long), flick (swipe x->x2)",
              "",
              {"input", "touch", "strip", "swipe"},
              {{"gesture", "body", "string", "tap (default), press, flick", false, {"tap", "press", "flick"}},
               {"x", "body", "integer", "window x (logical, 0 = left)", true},
               {"y", "body", "integer", "window y, default the middle"},
               {"x2", "body", "integer", "flick end x"},
               {"y2", "body", "integer", "flick end y"}},
              json{{"gesture", "tap"}, {"x", 120}},
              [w](const Request& r) {
                  return OnGui([w, &r] {
                      const Model& m = w->_deck->GetModel();
                      if (m.lcdKind != LcdKind::Region)
                          return Reply::Error(409, NameOf(m) + " has no touch strip");
                      const int x = static_cast<int>(r.Num("x")), y = static_cast<int>(r.Num("y", m.lcdLogicalH / 2));
                      const std::string g = r.Str("gesture", "tap");
                      if (g == "press")
                          w->_deck->TouchPress(x, y);
                      else if (g == "flick")
                          w->_deck->TouchFlick(x, y, static_cast<int>(r.Num("x2", x)),
                                               static_cast<int>(r.Num("y2", y)));
                      else
                          w->_deck->TouchTap(x, y);
                      return Reply::Ok({{"gesture", g}, {"x", x}, {"y", y}});
                  });
              }});
}

// ---------------------------------------------------------------- screenshots, log, recording, window
void StudioApi::AddViewRoutes()
{
    MainWindow* w = _w;
    _api.Add({"GET",
              "/api/v1/screenshot",
              "Screenshot as PNG: the display only (lcd), the device (deck) or the whole window",
              "",
              {"screenshot", "image", "capture"},
              {{"area",
                "query",
                "string",
                "lcd = the LCD as the host drew it, deck = the device as shown (default), window = the whole window",
                false,
                {"lcd", "deck", "window"}},
               {"target",
                "query",
                "string",
                "studio (default) or editor (area canvas / window)",
                false,
                {"studio", "editor"}},
               {"scale", "query", "number", "deck / lcd scale, default 1 (deck at 2 = sharp)"},
               {"path", "query", "string", "also save the PNG here"}},
              nullptr,
              [w](const Request& r) {
                  if (r.Str("target", "studio") == "editor")
                  {
                      Request e2 = r;
                      e2.path = "/api/v1/editor/screenshot";
                      if (r.Str("area", "canvas") != "window")
                          e2.query["area"] = "canvas";
                      return w->_api->GetApi().Dispatch(e2);
                  }
                  const std::string area = r.Str("area", "deck");
                  const double scale = std::clamp(r.Real("scale", 1.0), 0.25, 4.0);
                  QImage img = OnGui([w, &area, scale] {
                      if (area == "lcd")
                      {
                          const QImage& l = w->_deck->Lcd();
                          return scale == 1.0 ? l.copy()
                                              : l.scaled(l.size() * scale, Qt::KeepAspectRatio, Qt::FastTransformation);
                      }
                      if (area == "window")
                          return w->grab().toImage();
                      QImage d(w->_view->size() * scale, QImage::Format_ARGB32);
                      d.setDevicePixelRatio(scale);
                      d.fill(Qt::black);
                      w->_view->render(&d);
                      d.setDevicePixelRatio(1);
                      return d;
                  });
                  json meta{{"target", "studio"}, {"area", area}, {"width", img.width()}, {"height", img.height()}};
                  if (r.Has("path"))
                  {
                      meta["saved"] = img.save(Qs(r.Str("path")), "PNG");
                      meta["path"] = r.Str("path");
                  }
                  return Reply::Png(Png(img), meta);
              }});
    _api.Add({"GET",
              "/api/v1/page",
              "What the keys, dials and strip mean right now, as JSON (published by the daemon's scenario runtime)",
              "Cheaper and exact compared to reading a screenshot: menu path, each key's node id / title / type / "
              "action kind / tags, "
              "the back key, dial bindings. Empty keys are left out.",
              {"page", "keys", "navigation", "menu"},
              {},
              nullptr,
              [w](const Request&) {
                  return OnGui([w] {
                      if (w->_page.isEmpty())
                          return Reply::Error(404, "the daemon publishes no page (no scenario runs for this deck)",
                                              "POST /api/v1/editor/run");
                      json j = json::parse(w->_page.toStdString(), nullptr, false);
                      if (j.is_discarded())
                          return Reply::Error(500, "bad page JSON from the daemon");
                      j["published"] = w->_pageTime.toString(Qt::ISODateWithMs).toStdString();
                      return Reply::Ok(j);
                  });
              }});
    _api.Add({"GET",
              "/api/v1/log",
              "Protocol log lines (daemon frames, this window's input, the real deck's input, runtime notes, info)",
              "",
              {"log", "protocol"},
              {{"since", "query", "integer", "only lines after this seq"},
               {"source", "query", "string", "comma list: daemon,viewer,deck,app,info"},
               {"limit", "query", "integer", "newest N, default 200"}},
              nullptr,
              [w](const Request& r) {
                  const qint64 since = r.Num("since", 0);
                  const std::string src = r.Str("source");
                  const size_t limit = static_cast<size_t>(std::clamp<long long>(r.Num("limit", 200), 1, 5000));
                  return OnGui([&] {
                      json lines = json::array();
                      for (auto it = w->_logStore.rbegin(); it != w->_logStore.rend() && lines.size() < limit; ++it)
                      {
                          if (it->seq <= since)
                              break;
                          const char* s = SrcName(static_cast<int>(it->src));
                          if (!src.empty() && ("," + src + ",").find(std::string(",") + s + ",") == std::string::npos)
                              continue;
                          lines.insert(lines.begin(), json{{"seq", it->seq},
                                                           {"time", it->time.toString(Qt::ISODateWithMs).toStdString()},
                                                           {"source", s},
                                                           {"text", it->text.toStdString()}});
                      }
                      return Reply::Ok({{"lines", lines}, {"last_seq", w->_logSeq}});
                  });
              }});
    _api.Add({"DELETE", "/api/v1/log", "Clear the protocol log", "", {"log"}, {}, nullptr, [w](const Request&) {
                  return OnGui([w] {
                      w->_logStore.clear();
                      w->_log->clear();
                      return Reply::Ok();
                  });
              }});
    _api.Add({"PUT",
              "/api/v1/log/filter",
              "Which sources the protocol panel shows",
              "",
              {"log", "filter"},
              {{"daemon", "body", "boolean", ""},
               {"viewer", "body", "boolean", ""},
               {"deck", "body", "boolean", ""},
               {"app", "body", "boolean", ""},
               {"info", "body", "boolean", ""}},
              json{{"daemon", false}},
              [w](const Request& r) {
                  return OnGui([w, &r] {
                      json out = json::object();
                      for (int i = 0; i < 5; ++i)
                      {
                          if (r.Has(SrcName(i)))
                              w->_filterButtons[i]->setChecked(r.Flag(SrcName(i)));
                          out[SrcName(i)] = w->_filterButtons[i]->isChecked();
                      }
                      return Reply::Ok(out);
                  });
              }});
    _api.Add({"GET",
              "/api/v1/recording",
              "Video recording status",
              "",
              {"recording", "video"},
              {},
              nullptr,
              [w](const Request&) {
                  return OnGui([w] {
                      return Reply::Ok({{"active", w->_recorder->Active()},
                                        {"path", w->_recorder->Path().toStdString()},
                                        {"seconds", w->_recorder->Seconds()},
                                        {"frames", w->_recorder->Frames()}});
                  });
              }});
    _api.Add({"POST",
              "/api/v1/recording",
              "Start / stop recording the deck to a video (.mp4 .mov .mkv .webm .gif)",
              "",
              {"recording", "video", "record"},
              {{"action", "body", "string", "start | stop", true, {"start", "stop"}},
               {"path", "body", "string", "file for start"},
               {"fps", "body", "integer", "default 30"}},
              json{{"action", "start"}, {"path", "deck.mp4"}},
              [w](const Request& r) {
                  return OnGui([w, &r] {
                      if (r.Str("action") == "stop")
                      {
                          if (!w->_recorder->Active())
                              return Reply::Error(409, "not recording");
                          return Reply::Ok({{"summary", w->StopRecording().toStdString()}});
                      }
                      if (!r.Has("path"))
                          return Reply::Error(400, "start needs 'path'");
                      QString err;
                      if (!w->StartRecording(Qs(r.Str("path")), static_cast<int>(r.Num("fps", 30)), &err))
                          return Reply::Error(500, err.toStdString());
                      return Reply::Ok({{"recording", r.Str("path")}});
                  });
              }});
    _api.Add({"PUT",
              "/api/v1/window",
              "Window: raise, resize, protocol panel, the scenario editor (own window / docked / hidden)",
              "",
              {"window", "editor"},
              {{"raise", "body", "boolean", "bring to front"},
               {"width", "body", "integer", ""},
               {"height", "body", "integer", ""},
               {"protocol", "body", "boolean", "protocol panel visible"},
               {"editor", "body", "string", "window | dock | hidden", false, {"window", "dock", "hidden"}},
               {"editor_width", "body", "integer", "the editor's own window"},
               {"editor_height", "body", "integer", "the editor's own window"}},
              json{{"raise", true}, {"width", 1400}, {"height", 900}},
              [w](const Request& r) {
                  return OnGui([w, &r] {
                      if (r.Flag("raise"))
                      {
                          w->showNormal();
                          w->raise();
                          w->activateWindow();
                      }
                      if (r.Has("width") || r.Has("height"))
                          w->resize(static_cast<int>(r.Num("width", w->width())),
                                    static_cast<int>(r.Num("height", w->height())));
                      if (r.Has("protocol"))
                          w->_protocolDock->setVisible(r.Flag("protocol"));
                      if (r.Has("editor"))
                          w->SetEditorMode(QString::fromStdString(r.Str("editor")));
                      if ((r.Has("editor_width") || r.Has("editor_height")) && w->_editorDock->isFloating())
                          w->_editorDock->resize(static_cast<int>(r.Num("editor_width", w->_editorDock->width())),
                                                 static_cast<int>(r.Num("editor_height", w->_editorDock->height())));
                      const QString ed = !w->_editorDock->isVisible()   ? QStringLiteral("hidden")
                                         : w->_editorDock->isFloating() ? QStringLiteral("window")
                                                                        : QStringLiteral("dock");
                      return Reply::Ok({{"width", w->width()},
                                        {"height", w->height()},
                                        {"protocol", w->_protocolDock->isVisible()},
                                        {"editor", ed.toStdString()},
                                        {"editor_width", w->_editorDock->width()},
                                        {"editor_height", w->_editorDock->height()}});
                  });
              }});
}
