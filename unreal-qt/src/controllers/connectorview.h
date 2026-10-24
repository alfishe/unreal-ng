#pragma once

/// @file connectorview.h
/// @brief How the Qt client names and draws a machine connector (docs/inprogress/2026-10-17-controller-manager/ui-ux.md
/// §2.3, §6, §8): its display name and port, the colour and the short tag of its cords and callouts (Kempston blue,
/// Sinclair 1 orange, keyboard grey; colour is never the only signal, each has a tag), the controls it holds in words,
/// and the status-bar connector LED built from all of it. Only the core's connector types: no Qt widgets, no host
/// library, so the status bar and the tests use it as the dialog does.

#include <QColor>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "emulator/io/controllers/controllermanager.h"

namespace connectorview
{

struct Style
{
    QColor colour;
    QString tag;  ///< K, S1, S2, CUR, KEY, F, MD1, MD2
};

/// The colour and tag of what the connector holds (an empty connector: grey, its kind's tag)
Style StyleFor(const controllers::Connector& connector);
/// "Kempston", "Sinclair 1", "Next left", "Keys joystick 1", "Fuller"
QString DisplayName(const controllers::Connector& connector);
/// What the guest reads it through: "#1F", "#7F", "keys 6-0", "keys 1-5", "keys 5-8, 0", "NR #05"; "" when none
QString PortText(const controllers::Connector& connector);
/// The held controls in words ("up, fire"; an MD pad: "up, B, START"); "—" when nothing is held
QString HeldText(controllers::RetroDeviceKind device, uint16_t mask);

/// The image set of a retro device's picture (device-database.md §10): kempston-joystick, sinclair-joystick,
/// cursor-joystick, md6-pad; "" for an empty connector
QString ImageSetFor(controllers::RetroDeviceKind device);
/// The held bits as the picture's live values by retro control (`up`, `fire`; an MD pad's `a`, `start`, `mode`)
std::map<std::string, float> LiveOf(controllers::RetroDeviceKind device, uint16_t mask);

/// One key of a keyboard joystick's strip (ui-ux.md §4): the ZX key it presses and the control bit
struct StripKey
{
    QString key;  ///< "6"
    QString control;  ///< "left"
    uint16_t bit = 0;
};
/// The five keys a Sinclair or Cursor joystick presses, in key order; empty for every other device
std::vector<StripKey> KeyStrip(const controllers::Connector& connector);

/// Machine > Controllers > Swap joystick ports (ui-ux.md §8): @p pads the player pads with the connector each drives
/// ("" none), @p joysticks the connectors holding a joystick or pad, in machine order. Two pads or more: the first
/// two trade connectors; one pad: it moves on to the next joystick connector. Answers the new connector per pad
/// that changes; empty when there is nothing to swap
std::vector<std::pair<std::string, std::string>> SwapPorts(const std::vector<std::pair<std::string, std::string>>& pads,
                                                           const std::vector<std::string>& joysticks);

/// The status-bar connector LED (ui-ux.md §8)
struct Led
{
    bool visible = false;  ///< some connector holds a retro device
    bool active = false;   ///< some plugged retro device is away from rest
    QString toolTip;       ///< per connector what is held and which host devices drive it
};

/// @p states: the held controls per connector (ControllerManager::State), in list order; @p drivers: per connector
/// id the host devices that drive it ("Xbox Elite 2")
Led DescribeLed(const std::vector<controllers::Connector>& connectors, const std::vector<uint16_t>& states,
                const std::map<std::string, QStringList>& drivers);

}  // namespace connectorview
