#include "platform/linuxpads.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "hid/drivers/switchpro.h"
#include "hid/hidrawnode.h"
#include "joyconpairing.h"

namespace controllers
{

namespace
{
/// linux/input-event-codes.h (UAPI: the values never change)
namespace code
{
constexpr unsigned kBtnLeft = 0x110;
constexpr unsigned kBtnRight = 0x111;
constexpr unsigned kBtnForward = 0x115;
constexpr unsigned kBtnBack = 0x116;
constexpr unsigned kBtnSouth = 0x130;
constexpr unsigned kBtnEast = 0x131;
constexpr unsigned kBtnNorth = 0x133;
constexpr unsigned kBtnWest = 0x134;
constexpr unsigned kBtnZ = 0x135;
constexpr unsigned kBtnTl = 0x136;
constexpr unsigned kBtnTr = 0x137;
constexpr unsigned kBtnTl2 = 0x138;
constexpr unsigned kBtnTr2 = 0x139;
constexpr unsigned kBtnSelect = 0x13a;
constexpr unsigned kBtnStart = 0x13b;
constexpr unsigned kBtnMode = 0x13c;
constexpr unsigned kBtnThumbl = 0x13d;
constexpr unsigned kBtnThumbr = 0x13e;
constexpr unsigned kBtnDpadUp = 0x220;
constexpr unsigned kBtnDpadDown = 0x221;
constexpr unsigned kBtnDpadLeft = 0x222;
constexpr unsigned kBtnDpadRight = 0x223;
constexpr unsigned kBtnGripl = 0x224;
constexpr unsigned kBtnGripr = 0x225;
constexpr unsigned kBtnGripl2 = 0x226;
constexpr unsigned kBtnGripr2 = 0x227;
constexpr unsigned kBtnTriggerHappy1 = 0x2c0;
constexpr unsigned kBtnTriggerHappy2 = 0x2c1;
constexpr unsigned kBtnTriggerHappy3 = 0x2c2;
constexpr unsigned kBtnTriggerHappy4 = 0x2c3;
constexpr unsigned kBtnTriggerHappy5 = 0x2c4;
constexpr unsigned kBtnTriggerHappy6 = 0x2c5;
constexpr unsigned kBtnTriggerHappy7 = 0x2c6;
constexpr unsigned kBtnTriggerHappy8 = 0x2c7;
constexpr unsigned kAbsX = 0x00;
constexpr unsigned kAbsY = 0x01;
constexpr unsigned kAbsZ = 0x02;
constexpr unsigned kAbsRx = 0x03;
constexpr unsigned kAbsRy = 0x04;
constexpr unsigned kAbsRz = 0x05;
constexpr unsigned kBtnJoystick = 0x120;
constexpr unsigned kTriggerHappyCount = 40;  // BTN_TRIGGER_HAPPY1 .. 40
constexpr unsigned kAbsGas = 0x09;
constexpr unsigned kAbsBrake = 0x0a;
constexpr uint16_t kBusBluetooth = 0x05;
constexpr uint16_t kBusVirtual = 0x06;
}  // namespace code

constexpr uint16_t kNintendo = 0x057E;
constexpr uint16_t kSony = 0x054C;

const char* StandardButton(unsigned key)
{
    using namespace code;
    switch (key)
    {
        case kBtnSouth: return "south";
        case kBtnEast: return "east";
        case kBtnNorth: return "north";
        case kBtnWest: return "west";
        case kBtnTl: return "lb";
        case kBtnTr: return "rb";
        case kBtnTl2: return "lt";
        case kBtnTr2: return "rt";
        case kBtnSelect: return "view";
        case kBtnStart: return "menu";
        case kBtnMode: return "guide";
        case kBtnThumbl: return "lstick.click";
        case kBtnThumbr: return "rstick.click";
        case kBtnDpadUp: return "dpad.up";
        case kBtnDpadDown: return "dpad.down";
        case kBtnDpadLeft: return "dpad.left";
        case kBtnDpadRight: return "dpad.right";
        // xpad's Elite 2 paddles (drivers/input/joystick/xpad.c, xpad_btn_paddles[] "paddle upper right, lower right /
        // upper left, lower left", reported from data[18] bits 0-3 and zeroed while a custom profile slot is active):
        // current kernels send BTN_GRIPR, BTN_GRIPR2, BTN_GRIPL, BTN_GRIPL2, older ones (6.1+) BTN_TRIGGER_HAPPY5-8 in
        // the same bit order. That is the order of the Bluetooth report's paddle nibble HidPad reads on macOS (0x01 P1
        // upper right .. 0x08 P4 lower left, research-host-apis.md §2.3b), so the same paddle has the same name
        case kBtnGripr:
        case kBtnTriggerHappy5: return "paddle.p1";
        case kBtnGripr2:
        case kBtnTriggerHappy6: return "paddle.p2";
        case kBtnGripl:
        case kBtnTriggerHappy7: return "paddle.p3";
        case kBtnGripl2:
        case kBtnTriggerHappy8: return "paddle.p4";
        default: return nullptr;
    }
}

/// hid-nintendo's left_joycon_button_mappings / _s_ (SL = BTN_TR, SR = BTN_TR2 off the grip) as SDL's
/// HandleMiniControllerStateL names them (switchpro.cpp kJoyConLeftBits): the arrows are the faces held sideways
const char* JoyConLeftButton(unsigned key)
{
    using namespace code;
    switch (key)
    {
        case kBtnSelect: return "menu";  // -
        case kBtnThumbl: return "lstick.click";
        case kBtnZ: return "guide";  // Capture
        case kBtnDpadLeft: return "south";
        case kBtnDpadDown: return "east";
        case kBtnDpadUp: return "west";
        case kBtnDpadRight: return "north";
        case kBtnTr2: return "rb";  // SR
        case kBtnTr: return "lb";   // SL
        case kBtnTl: return "paddle.p3";   // L
        case kBtnTl2: return "paddle.p4";  // ZL
        default: return nullptr;
    }
}

/// right_joycon_button_mappings / _s_ (SL = BTN_TL, SR = BTN_TL2) as HandleMiniControllerStateR (kJoyConRightBits)
const char* JoyConRightButton(unsigned key)
{
    using namespace code;
    switch (key)
    {
        case kBtnEast: return "south";   // A
        case kBtnNorth: return "east";   // X
        case kBtnSouth: return "west";   // B
        case kBtnWest: return "north";   // Y
        case kBtnTl2: return "rb";       // SR
        case kBtnTl: return "lb";        // SL
        case kBtnTr: return "paddle.p1";   // R
        case kBtnTr2: return "paddle.p2";  // ZR
        case kBtnStart: return "menu";     // +
        case kBtnThumbr: return "lstick.click";
        case kBtnMode: return "guide";  // Home
        default: return nullptr;
    }
}

/// The left half of a pair: HandleCombinedControllerStateL (kPairLeftBits)
const char* PairLeftButton(unsigned key)
{
    using namespace code;
    switch (key)
    {
        case kBtnSelect: return "view";
        case kBtnThumbl: return "lstick.click";
        case kBtnZ: return "share";
        case kBtnDpadUp: return "dpad.up";
        case kBtnDpadDown: return "dpad.down";
        case kBtnDpadLeft: return "dpad.left";
        case kBtnDpadRight: return "dpad.right";
        case kBtnTr2: return "paddle.p4";  // SR
        case kBtnTr: return "paddle.p3";   // SL
        case kBtnTl: return "lb";
        case kBtnTl2: return "lt";  // ZL, digital
        default: return nullptr;
    }
}

/// The right half: HandleCombinedControllerStateR (kPairRightBits)
const char* PairRightButton(unsigned key)
{
    using namespace code;
    switch (key)
    {
        case kBtnSouth: return "south";  // B
        case kBtnEast: return "east";    // A
        case kBtnWest: return "west";    // Y
        case kBtnNorth: return "north";  // X
        case kBtnTl2: return "paddle.p1";  // SR
        case kBtnTl: return "paddle.p2";   // SL
        case kBtnTr: return "rb";
        case kBtnTr2: return "rt";  // ZR, digital
        case kBtnStart: return "menu";
        case kBtnThumbr: return "rstick.click";
        case kBtnMode: return "guide";
        default: return nullptr;
    }
}

std::string ReadLine(const std::filesystem::path& file)
{
    std::ifstream in(file);
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    return line;
}

uint16_t ReadHex(const std::filesystem::path& file)
{
    const std::string text = ReadLine(file);
    return static_cast<uint16_t>(std::strtoul(text.c_str(), nullptr, 16) & 0xFFFF);
}

/// sysfs `capabilities/abs`: hex words, the most significant first; bit @p bit of the whole
bool CapabilityBit(const std::string& text, unsigned bit)
{
    std::vector<std::string> words;
    std::istringstream in(text);
    for (std::string word; in >> word;)
        words.push_back(word);
    if (words.empty())
        return false;
    // The kernel prints longs (64 bits on a 64-bit kernel, 32 on a 32-bit one); bit 3 sits in the last word either way
    const std::string& last = words.back();
    const unsigned long low = std::strtoul(last.substr(last.size() > 8 ? last.size() - 8 : 0).c_str(), nullptr, 16);
    return bit < 32 && ((low >> bit) & 1ul) != 0;
}
}  // namespace

LinuxPadKind LinuxPadKindFor(uint16_t vendor, uint16_t product, bool rightStick, bool nintendoDriver,
                             const std::string& name)
{
    if (vendor == kSony && product == 0x0DF2)
        return LinuxPadKind::DualSenseEdge;
    if (vendor != kNintendo)
        return LinuxPadKind::Standard;
    if (product == 0x2008)
        return LinuxPadKind::JoycondCombined;  // joycond's uinput device: no kernel driver, its own key set
    if (!nintendoDriver)
        return LinuxPadKind::Standard;
    switch (product)
    {
        case 0x2009: return LinuxPadKind::SwitchPro;
        case 0x2006: return LinuxPadKind::JoyConLeft;
        case 0x2007:
            return SwitchProPad::LayoutFor(vendor, product, name) == SwitchLayout::Nes ? LinuxPadKind::NsoNes
                                                                                         : LinuxPadKind::JoyConRight;
        case 0x2019: return LinuxPadKind::NsoN64;
        case 0x201E: return LinuxPadKind::NsoGenesis;
        case 0x200E: return rightStick ? LinuxPadKind::JoyConRight : LinuxPadKind::JoyConLeft;  // a grip's half
        default: return LinuxPadKind::Standard;
    }
}

bool IsJoyConKind(LinuxPadKind kind)
{
    return kind == LinuxPadKind::JoyConLeft || kind == LinuxPadKind::JoyConRight || kind == LinuxPadKind::PairLeft ||
           kind == LinuxPadKind::PairRight;
}

bool IsLeftJoyConKind(LinuxPadKind kind)
{
    return kind == LinuxPadKind::JoyConLeft || kind == LinuxPadKind::PairLeft;
}

LinuxPadKind JoyConKind(bool left, bool paired)
{
    if (paired)
        return left ? LinuxPadKind::PairLeft : LinuxPadKind::PairRight;
    return left ? LinuxPadKind::JoyConLeft : LinuxPadKind::JoyConRight;
}

const char* LinuxPadButton(LinuxPadKind kind, unsigned key, bool analogTriggers)
{
    using namespace code;
    switch (kind)
    {
        case LinuxPadKind::JoyConLeft: return JoyConLeftButton(key);
        case LinuxPadKind::JoyConRight: return JoyConRightButton(key);
        case LinuxPadKind::PairLeft: return PairLeftButton(key);
        case LinuxPadKind::PairRight: return PairRightButton(key);
        case LinuxPadKind::SwitchPro:
            if (key == kBtnZ)
                return "share";  // Capture
            break;
        case LinuxPadKind::DualSenseEdge:
            // hid-playstation dualsense_parse_report (7.2): DS_EDGE_BUTTONS_FN1 / FN2 / LEFT_PADDLE / RIGHT_PADDLE, the
            // names SonyPad gives the same bits of the full report
            switch (key)
            {
                case kBtnTriggerHappy1: return "misc.2";
                case kBtnTriggerHappy2: return "misc.3";
                case kBtnTriggerHappy3: return "paddle.p3";
                case kBtnTriggerHappy4: return "paddle.p1";
                default: break;
            }
            break;
        case LinuxPadKind::JoycondCombined:
            // joycond virt_ctlr_combined: Capture BTN_Z; the left SL / SR as BTN_TRIGGER_HAPPY1 / 2, the right SL / SR
            // as 3 / 4 (not over the serial rail); the names a pair the backend joins gives them (PairLeft / Right)
            switch (key)
            {
                case kBtnZ: return "share";
                case kBtnTriggerHappy1: return "paddle.p3";
                case kBtnTriggerHappy2: return "paddle.p4";
                case kBtnTriggerHappy3: return "paddle.p2";
                case kBtnTriggerHappy4: return "paddle.p1";
                default: break;
            }
            break;
        case LinuxPadKind::NsoNes:
            // nescon_button_mappings: A BTN_SOUTH, B BTN_EAST; the pad has A on the right (east), B on the left
            if (key == kBtnSouth)
                return "east";
            if (key == kBtnEast)
                return "south";
            break;
        case LinuxPadKind::NsoN64:
            // n64con_button_mappings: A BTN_A (south), B BTN_B (the left of the two: west), Z BTN_TL2, ZR BTN_TR2, the
            // C buttons BTN_FORWARD / BACK / LEFT / RIGHT (up, down, left, right: misc.1-4), Capture BTN_Z
            switch (key)
            {
                case kBtnEast: return "west";
                case kBtnForward: return "misc.1";
                case kBtnBack: return "misc.2";
                case kBtnLeft: return "misc.3";
                case kBtnRight: return "misc.4";
                case kBtnZ: return "share";
                default: break;
            }
            break;
        case LinuxPadKind::NsoGenesis:
            // gencon_button_mappings: A BTN_SOUTH, B BTN_EAST, C BTN_WEST (rb: the bottom row's third), Mode
            // BTN_SELECT (view), Capture BTN_Z; the six-button pad's X / Y / Z have no key there
            if (key == kBtnWest)
                return "rb";
            if (key == kBtnZ)
                return "share";
            break;
        case LinuxPadKind::Standard: break;
    }
    if (analogTriggers && (key == kBtnTl2 || key == kBtnTr2))
        return nullptr;
    return StandardButton(key);
}

LinuxPadAxis LinuxPadAxisOf(LinuxPadKind kind, unsigned axis)
{
    using namespace code;
    LinuxPadAxis out;
    switch (kind)
    {
        case LinuxPadKind::JoyConLeft:
            // HandleMiniControllerStateL: LEFTX = ~y, LEFTY = ~x of the raw stick; hid-nintendo's ABS_Y is the raw y
            // negated, its ABS_X the raw x
            if (axis == kAbsY)
                out.control = "lstick.x";
            else if (axis == kAbsX)
            {
                out.control = "lstick.y";
                out.negate = true;
            }
            return out;
        case LinuxPadKind::JoyConRight:
            // HandleMiniControllerStateR: LEFTX = y, LEFTY = x of the raw right stick (ABS_RY the raw y negated)
            if (axis == kAbsRy)
            {
                out.control = "lstick.x";
                out.negate = true;
            }
            else if (axis == kAbsRx)
                out.control = "lstick.y";
            return out;
        case LinuxPadKind::PairLeft:
            if (axis == kAbsX || axis == kAbsY)
                out.control = axis == kAbsX ? "lstick.x" : "lstick.y";
            return out;
        case LinuxPadKind::PairRight:
            if (axis == kAbsRx || axis == kAbsRy)
                out.control = axis == kAbsRx ? "rstick.x" : "rstick.y";
            return out;
        default: break;
    }
    switch (axis)
    {
        case kAbsX: out.control = "lstick.x"; break;
        case kAbsY: out.control = "lstick.y"; break;  // evdev y is + down, as the canonical one
        case kAbsRx: out.control = "rstick.x"; break;
        case kAbsRy: out.control = "rstick.y"; break;
        case kAbsZ:
            out.control = "lt";
            out.unit = true;
            break;
        case kAbsRz:
            out.control = "rt";
            out.unit = true;
            break;
        default: break;
    }
    return out;
}

/// region <hid-generic>

int LinuxGenericButtonIndex(unsigned key)
{
    using namespace code;
    if (key >= kBtnJoystick && key < kBtnJoystick + 16)
        return static_cast<int>(key - kBtnJoystick);
    if (key >= kBtnSouth && key < kBtnSouth + 16)  // BTN_GAMEPAD
        return static_cast<int>(key - kBtnSouth);
    if (key >= kBtnTriggerHappy1 && key < kBtnTriggerHappy1 + kTriggerHappyCount)
        return static_cast<int>(16 + key - kBtnTriggerHappy1);
    return -1;
}

int LinuxConsumerUsage(unsigned key)
{
    // hid-input.c, hidinput_configure_usage, HID_UP_CONSUMER: the usages a pad's map names
    switch (key)
    {
        case 158: return 0x224;  // KEY_BACK: AC Back
        case 159: return 0x225;  // KEY_FORWARD: AC Forward
        case 167: return 0x0B2;  // KEY_RECORD: Record
        case 172: return 0x223;  // KEY_HOMEPAGE: AC Home
        case 217: return 0x221;  // KEY_SEARCH: AC Search
        default: return -1;
    }
}

std::string LinuxGenericButton(const HidLayout& layout, unsigned key)
{
    const int index = LinuxGenericButtonIndex(key);
    if (index < 0)
    {
        // A consumer usage in the pad's own application collection: on the pad's node as a KEY_*, named as HidPad
        // names it (the map's `consumer`; an Xbox pad's Record is `share`)
        const int usage = LinuxConsumerUsage(key);
        if (usage < 0)
            return {};
        for (const auto& [mapped, control] : layout.consumer)
            if (mapped == usage)
                return control;
        if (layout.family == "xbox" && usage == 0x0B2)
            return "share";
        return {};
    }
    const size_t at = static_cast<size_t>(index);
    if (at < layout.buttons.size() && !layout.buttons[at].empty())
        return layout.buttons[at];
    return "button." + std::to_string(index);
}

LinuxPadAxis LinuxGenericAxis(const HidLayout& layout, unsigned axis)
{
    using namespace code;
    LinuxPadAxis out;
    switch (axis)
    {
        case kAbsX:
        case kAbsY:
            out.control = layout.dpadAxes ? (axis == kAbsX ? "dpad.x" : "dpad.y")
                                          : (axis == kAbsX ? "lstick.x" : "lstick.y");
            out.dpad = layout.dpadAxes;
            break;
        // HidPad: Z / Rz the right stick, Rx / Ry the triggers of a PlayStation layout (else extra axes)
        case kAbsZ: out.control = "rstick.x"; break;
        case kAbsRz: out.control = "rstick.y"; break;
        case kAbsRx:
        case kAbsRy:
            if (layout.rxRyTriggers)
            {
                out.control = axis == kAbsRx ? "lt" : "rt";
                out.unit = true;
            }
            break;
        case kAbsBrake:
        case kAbsGas:
            out.control = axis == kAbsBrake ? "lt" : "rt";  // the Stadia's triggers
            out.unit = true;
            break;
        default: break;
    }
    return out;
}

/// endregion </hid-generic>

/// region <Identity>

std::string LinuxIdHash(const std::string& text)
{
    uint32_t hash = 2166136261u;
    for (unsigned char c : text)
        hash = (hash ^ c) * 16777619u;
    char out[12];
    std::snprintf(out, sizeof(out), "%08x", hash);
    return out;
}

std::string LinuxDeviceId(uint16_t bus, uint16_t vendor, uint16_t product, uint16_t version, const std::string& uniq,
                          const std::string& phys, const std::string& node)
{
    char ids[24];
    std::snprintf(ids, sizeof(ids), "%04x:%04x", vendor, product);
    char ver[8];
    std::snprintf(ver, sizeof(ver), "%04x", version);
    if (bus == code::kBusBluetooth)
        return std::string("bt:") + ids + ":" + LinuxIdHash(uniq.empty() ? phys : uniq);
    const std::string prefix = bus == code::kBusVirtual ? "virtual:" : "usb:";
    if (!uniq.empty())
        return prefix + ids + ":" + ver + ":" + uniq;
    return prefix + ids + ":" + ver + "@" + LinuxIdHash(phys.empty() ? node : phys);
}

std::vector<LinuxJoyConHalf> FindJoyConHalves(const std::string& sysRoot, const std::string& devRoot,
                                              const std::function<bool(const std::string& node)>& readable)
{
    namespace fs = std::filesystem;
    std::vector<LinuxJoyConHalf> halves;
    std::error_code error;
    std::vector<fs::path> inputs;
    for (fs::directory_iterator it(fs::path(sysRoot) / "class" / "input", error), end; !error && it != end;
         it.increment(error))
        if (it->path().filename().string().rfind("input", 0) == 0)
            inputs.push_back(it->path());
    std::sort(inputs.begin(), inputs.end());
    for (const fs::path& input : inputs)
    {
        if (ReadHex(input / "id" / "vendor") != kNintendo)
            continue;
        const uint16_t product = ReadHex(input / "id" / "product");
        if (product != 0x2006 && product != 0x2007 && product != 0x200E)
            continue;
        const std::string name = ReadLine(input / "name");
        if (name.size() >= 6 && name.compare(name.size() - 6, 6, " (IMU)") == 0)
            continue;  // hid-nintendo's motion node of the same Joy-Con
        std::string event;
        for (fs::directory_iterator it(input, error), end; !error && it != end; it.increment(error))
            if (it->path().filename().string().rfind("event", 0) == 0)
                event = it->path().filename().string();
        if (event.empty())
            continue;
        const std::string node = devRoot + "/" + event;
        LinuxJoyConHalf half;
        half.left = product == 0x2006 || (product == 0x200E && !CapabilityBit(ReadLine(input / "capabilities" / "abs"),
                                                                             code::kAbsRx));
        half.id = LinuxDeviceId(ReadHex(input / "id" / "bustype"), kNintendo, product, ReadHex(input / "id" / "version"),
                                ReadLine(input / "uniq"), ReadLine(input / "phys"), node);
        half.taken = !readable(node);
        halves.push_back(half);
    }
    return halves;
}

std::string JoycondPairId(const std::vector<LinuxJoyConHalf>& halves)
{
    const LinuxJoyConHalf* left = nullptr;
    const LinuxJoyConHalf* right = nullptr;
    for (const LinuxJoyConHalf& half : halves)
    {
        if (!half.taken)
            continue;
        const LinuxJoyConHalf*& slot = half.left ? left : right;
        if (slot != nullptr)
            return {};  // two taken of one side: which two joycond joined is not visible
        slot = &half;
    }
    if (left == nullptr || right == nullptr)
        return {};
    return JoyConPairing::PairId(left->id, right->id);
}

/// endregion </Identity>

/// region <hid-steam>

std::vector<LinuxSteamHeld> FindSteamHeldPads(const std::string& sysRoot)
{
    namespace fs = std::filesystem;
    constexpr uint16_t kValve = 0x28DE;
    constexpr uint16_t kSteamDeck = 0x1205;
    std::vector<fs::path> devices;
    std::error_code error;
    for (fs::directory_iterator it(fs::path(sysRoot) / "bus" / "hid" / "drivers" / "hid-steam", error), end;
         !error && it != end; it.increment(error))
    {
        const std::string name = it->path().filename().string();
        // <bus>:<vid>:<pid>.<n>: the bound devices among the driver's own files (bind, unbind, module, ...)
        if (name.size() > 15 && name[4] == ':' && name[9] == ':' && name[14] == '.')
            devices.push_back(it->path());
    }
    std::sort(devices.begin(), devices.end());
    std::vector<LinuxSteamHeld> held;
    std::vector<std::string> receivers;
    for (const fs::path& device : devices)
    {
        const std::string dir = device.filename().string();
        const uint16_t bus = static_cast<uint16_t>(std::strtoul(dir.substr(0, 4).c_str(), nullptr, 16));
        const uint16_t vendor = static_cast<uint16_t>(std::strtoul(dir.substr(5, 4).c_str(), nullptr, 16));
        const uint16_t product = static_cast<uint16_t>(std::strtoul(dir.substr(10, 4).c_str(), nullptr, 16));
        if (vendor != kValve || fs::exists(device / "hidraw", error) || fs::exists(device / "input", error))
            continue;
        // The Steam Deck's controller: the hidraw backend reads it beside the Steam client (hidrawbackend.h)
        if (product == kSteamDeck)
            continue;
        std::string name;
        std::string phys;
        std::string uniq;
        std::ifstream uevent(device / "uevent");
        for (std::string line; std::getline(uevent, line);)
        {
            if (line.rfind("HID_NAME=", 0) == 0)
                name = line.substr(9);
            else if (line.rfind("HID_PHYS=", 0) == 0)
                phys = line.substr(9);
            else if (line.rfind("HID_UNIQ=", 0) == 0)
                uniq = line.substr(9);
        }
        // A receiver's interfaces differ by their `/inputN` only: one entry for all its slots
        const bool wireless = product == 0x1142 || product == 0x1304 || product == 0x1305;
        if (wireless)
        {
            const std::string receiver = std::to_string(product) + phys.substr(0, phys.rfind("/input"));
            if (std::find(receivers.begin(), receivers.end(), receiver) != receivers.end())
                continue;
            receivers.push_back(receiver);
            phys = phys.substr(0, phys.rfind("/input"));
        }
        held.push_back({LinuxDeviceId(bus, vendor, product, 0, uniq, phys, dir), product, name});
    }
    return held;
}

bool SteamClientRunning(const std::string& procRoot)
{
    namespace fs = std::filesystem;
    std::error_code error;
    for (fs::directory_iterator it(procRoot, error), end; !error && it != end; it.increment(error))
    {
        const std::string pid = it->path().filename().string();
        if (pid.empty() || pid.find_first_not_of("0123456789") != std::string::npos)
            continue;
        std::ifstream comm(it->path() / "comm");
        std::string name;
        if (std::getline(comm, name) && name == "steam")
            return true;
    }
    return false;
}

std::vector<LinuxHidrawDevice> FindHidrawDevices(const std::string& sysRoot, const std::string& devRoot,
                                                 uint16_t vendor, uint16_t product)
{
    namespace fs = std::filesystem;
    std::vector<std::string> nodes;
    std::error_code error;
    for (fs::directory_iterator it(fs::path(sysRoot) / "class" / "hidraw", error), end; !error && it != end;
         it.increment(error))
        if (const std::string name = it->path().filename().string(); name.rfind("hidraw", 0) == 0)
            nodes.push_back(name);
    std::sort(nodes.begin(), nodes.end(), [](const std::string& a, const std::string& b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    std::vector<LinuxHidrawDevice> found;
    for (const std::string& node : nodes)
    {
        std::ifstream in(fs::path(sysRoot) / "class" / "hidraw" / node / "device" / "uevent");
        std::stringstream text;
        text << in.rdbuf();
        HidUevent uevent;
        if (!ParseHidUevent(text.str(), uevent) || uevent.vendor != vendor || uevent.product != product)
            continue;
        const std::string path = devRoot + "/" + node;
        found.push_back(
            {LinuxDeviceId(uevent.bus, vendor, product, 0, uevent.uniq, uevent.phys, path), path, uevent.name});
    }
    return found;
}

std::string SteamHeldNotice()
{
    return "The Steam client has this controller: while Steam runs, the kernel's hid-steam hands the pad to Steam and "
           "offers no input device of its own. Quit Steam to use it here, or use the virtual pad Steam Input makes "
           "for it (Steam Virtual Gamepad).";
}

/// endregion </hid-steam>

}  // namespace controllers
