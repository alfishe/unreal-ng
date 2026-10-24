#include "connectorview.h"

#include <QCoreApplication>

#include <algorithm>

using namespace controllers;

namespace connectorview
{

namespace
{
QString Tr(const char* text)
{
    return QCoreApplication::translate("ConnectorView", text);
}

/// The keyboard sockets' joystick by the port it presses
bool SinclairTwo(const Connector& connector)
{
    return connector.device == RetroDeviceKind::SinclairJoystick && connector.options.sinclairPort == 2;
}
}  // namespace

Style StyleFor(const Connector& connector)
{
    // Muted tones that read on a dark and a light window alike; the debug layer's amber is kept for the DEBUG tag
    switch (connector.device)
    {
        case RetroDeviceKind::KempstonJoystick:
            return {QColor(74, 134, 197), QStringLiteral("K")};
        case RetroDeviceKind::Md6Pad:
            return connector.alias == "right" || connector.kindIndex == 1 ? Style{QColor(63, 163, 155), "MD2"}
                                                                          : Style{QColor(74, 134, 197), "MD1"};
        case RetroDeviceKind::SinclairJoystick:
            return SinclairTwo(connector) ? Style{QColor(63, 163, 155), "S2"} : Style{QColor(208, 138, 60), "S1"};
        case RetroDeviceKind::CursorJoystick:
            return {QColor(106, 163, 87), QStringLiteral("CUR")};
        case RetroDeviceKind::KeysJoystick:
            return {QColor(138, 143, 152), QStringLiteral("KEY")};
        case RetroDeviceKind::FullerJoystick:
            return {QColor(192, 96, 96), QStringLiteral("F")};
        case RetroDeviceKind::None:
            break;
    }
    return {QColor(138, 143, 152), QString()};
}

QString DisplayName(const Connector& connector)
{
    if (connector.id == "kempston")
        return Tr("Kempston");
    if (connector.id == "sinclair-1")
        return Tr("Sinclair 1");
    if (connector.id == "sinclair-2")
        return Tr("Sinclair 2");
    if (connector.id == "next.left")
        return Tr("Next left");
    if (connector.id == "next.right")
        return Tr("Next right");
    if (connector.id == "fuller")
        return Tr("Fuller");
    if (connector.id.rfind("keyjoy.", 0) == 0)
        return Tr("Keys socket %1").arg(QString::fromStdString(connector.id.substr(7)));
    return QString::fromStdString(connector.id);
}

QString PortText(const Connector& connector)
{
    switch (connector.kind)
    {
        case ConnectorKind::Kempston:
            return QStringLiteral("#1F");
        case ConnectorKind::Fuller:
            return QStringLiteral("#7F");
        case ConnectorKind::NextDb9:
            return QStringLiteral("NR #05");
        case ConnectorKind::KeyboardJoystick:
            break;
    }
    switch (connector.device)
    {
        case RetroDeviceKind::SinclairJoystick:
            return SinclairTwo(connector) ? Tr("keys 1-5") : Tr("keys 6-0");
        case RetroDeviceKind::CursorJoystick:
            return Tr("keys 5-8, 0");
        case RetroDeviceKind::KeysJoystick:
            return Tr("keys");
        default:
            return QString();
    }
}

QString HeldText(RetroDeviceKind device, uint16_t mask)
{
    // Kempston order for every stick; the MD pad's PortDecoder::PadState bits after the directions
    static const char* const stick[] = {"right", "left", "down", "up", "fire", "fire 2", "fire 3", "fire 4"};
    static const char* const pad[] = {"right", "left", "down", "up", "B", "C", "A", "START", "Y", "Z", "X", "MODE"};
    const bool md = device == RetroDeviceKind::Md6Pad;
    const int bits = md ? 12 : 8;
    // Directions read better up / down / left / right first
    static const int order[] = {3, 2, 1, 0, 4, 5, 6, 7, 8, 9, 10, 11};
    QStringList held;
    for (int index : order)
        if (index < bits && (mask & (1u << index)))
            held << Tr(md ? pad[index] : stick[index]);
    return held.isEmpty() ? QStringLiteral("—") : held.join(QStringLiteral(", "));
}

QString ImageSetFor(RetroDeviceKind device)
{
    switch (device)
    {
        case RetroDeviceKind::KempstonJoystick:
        case RetroDeviceKind::FullerJoystick:
            return QStringLiteral("kempston-joystick");
        case RetroDeviceKind::SinclairJoystick:
            return QStringLiteral("sinclair-joystick");
        case RetroDeviceKind::CursorJoystick:
            return QStringLiteral("cursor-joystick");
        case RetroDeviceKind::KeysJoystick:
            return QStringLiteral("generic-db9-stick");
        case RetroDeviceKind::Md6Pad:
            return QStringLiteral("md6-pad");
        default:
            return QString();
    }
}

std::map<std::string, float> LiveOf(RetroDeviceKind device, uint16_t mask)
{
    static const char* const stick[] = {"right", "left", "down", "up", "fire", "fire2", "fire3", "fire4"};
    static const char* const pad[] = {"right", "left", "down", "up", "b", "c", "a", "start", "y", "z", "x", "mode"};
    const bool md = device == RetroDeviceKind::Md6Pad;
    std::map<std::string, float> live;
    for (int bit = 0; bit < (md ? 12 : 8); ++bit)
        if (mask & (1u << bit))
            live[md ? pad[bit] : stick[bit]] = 1.0f;
    return live;
}

std::vector<StripKey> KeyStrip(const Connector& connector)
{
    switch (connector.device)
    {
        case RetroDeviceKind::SinclairJoystick:
            if (SinclairTwo(connector))
                return {{"1", "left", kLeft}, {"2", "right", kRight}, {"3", "down", kDown}, {"4", "up", kUp},
                        {"5", "fire", kFire}};
            return {{"6", "left", kLeft}, {"7", "right", kRight}, {"8", "down", kDown}, {"9", "up", kUp},
                    {"0", "fire", kFire}};
        case RetroDeviceKind::CursorJoystick:
            return {{"5", "left", kLeft}, {"6", "down", kDown}, {"7", "up", kUp}, {"8", "right", kRight},
                    {"0", "fire", kFire}};
        default:
            return {};
    }
}

std::vector<std::pair<std::string, std::string>> SwapPorts(const std::vector<std::pair<std::string, std::string>>& pads,
                                                           const std::vector<std::string>& joysticks)
{
    std::vector<std::pair<std::string, std::string>> driving;
    for (const auto& pad : pads)
        if (!pad.second.empty())
            driving.push_back(pad);
    if (driving.size() >= 2)
    {
        if (driving[0].second == driving[1].second)
            return {};
        return {{driving[0].first, driving[1].second}, {driving[1].first, driving[0].second}};
    }
    if (driving.size() == 1 && joysticks.size() >= 2)
    {
        const auto at = std::find(joysticks.begin(), joysticks.end(), driving[0].second);
        const size_t next = at == joysticks.end() ? 0 : static_cast<size_t>(at - joysticks.begin() + 1) % joysticks.size();
        if (joysticks[next] != driving[0].second)
            return {{driving[0].first, joysticks[next]}};
    }
    return {};
}

Led DescribeLed(const std::vector<Connector>& connectors, const std::vector<uint16_t>& states,
                const std::map<std::string, QStringList>& drivers)
{
    Led led;
    QStringList lines;
    for (size_t i = 0; i < connectors.size(); ++i)
    {
        const Connector& connector = connectors[i];
        if (connector.device == RetroDeviceKind::None)
            continue;
        led.visible = true;
        const uint16_t mask = i < states.size() ? states[i] : 0;
        if (mask != 0)
            led.active = true;
        const QString port = PortText(connector);
        QString line = DisplayName(connector) + (port.isEmpty() ? QString() : QStringLiteral(" ") + port) +
                       QStringLiteral(": ") + HeldText(connector.device, mask);
        const auto driven = drivers.find(connector.id);
        if (driven != drivers.end() && !driven->second.isEmpty())
            line += QStringLiteral(" ← ") + driven->second.join(QStringLiteral(", "));
        lines << line;
    }
    led.toolTip = lines.isEmpty() ? QString() : lines.join(QStringLiteral("\n")) + QStringLiteral("\n") +
                                                    Tr("Click: Tools > Controllers");
    return led;
}

}  // namespace connectorview
