// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/param_package.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "common/settings_input.h"
#include "common/steam_deck.h"

#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"

#include "input_common/main.h"

#include "frontend_common/deck_input.h"

namespace FrontendCommon {

namespace {

/// True for the Deck's built-in controller, by its SDL display name. Under Steam's gamepad emulation
/// the built-in is named "Steam Deck Controller" (and the older Steam Controller "Steam Controller"),
/// while external pads carry their real product name ("Xbox One Controller", "DualSense", …). The name
/// is the reliable discriminator here: Steam Input's own controller *type* cannot be used, because its
/// gamepad-slot index does NOT line up with the SDL port, so there is no way to match a Steam handle
/// back to the SDL device we actually read input from.
bool DeviceIsBuiltIn(const Common::ParamPackage& device) {
    const std::string display = device.Get("display", "");
    return display.find("Steam Deck") != std::string::npos ||
           display.find("Steam Controller") != std::string::npos;
}

/// True when SDL fully understands the device as a *gamepad* — i.e. it has an SDL_Gamepad behind it,
/// so its buttons and axes carry real, semantic bindings (this is south, that is the left trigger).
///
/// Why this matters so much: for a device SDL only knows as a bare joystick, the SDL driver's
/// GetAnalogMappingForDevice returns NOTHING (it bails out on a null SDL_Gamepad) and
/// GetButtonMappingForDevice falls back to reusing the SDL_GamepadButton *enum values* as raw
/// joystick button indices — a layout that matches no real controller. So an unrecognised pad ends
/// up with scrambled buttons and, worse, with its sticks left pointing at whatever the previous
/// device's config said. That is exactly the "with a pad everything is fine, without one the menu
/// eats A and the stick drifts" failure: the external pad is always recognised, the Deck's built-in
/// (as Steam or hidapi happens to expose it) is not always.
///
/// The empty analog mapping is the cheapest honest probe available from here, and it is the very
/// thing we need anyway. Cached per device identity — the answer cannot change for a given
/// guid+port, and this runs on a 2 Hz timer.
bool DeviceIsRecognizedGamepad(InputCommon::InputSubsystem& input_subsystem,
                               const Common::ParamPackage& device) {
    static std::unordered_map<std::string, bool> cache;
    const std::string key = device.Get("guid", "") + "/" + device.Get("port", "");
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    const bool recognized = !input_subsystem.GetAnalogMappingForDevice(device).empty();
    cache.emplace(key, recognized);
    LOG_INFO(Input, "Steam Deck: '{}' (guid {} port {}) — SDL gamepad: {}",
             device.Get("display", "?"), device.Get("guid", "?"), device.Get("port", "?"),
             recognized ? "yes" : "NO (raw joystick, using the standard layout)");
    return recognized;
}

/// Base of a raw input param: the engine + device identity every binding below shares.
Common::ParamPackage RawParam(const Common::ParamPackage& device) {
    Common::ParamPackage param;
    param.Set("engine", device.Get("engine", "sdl"));
    param.Set("port", device.Get("port", 0));
    param.Set("guid", device.Get("guid", ""));
    return param;
}

Common::ParamPackage RawButton(const Common::ParamPackage& device, int button) {
    auto param = RawParam(device);
    param.Set("button", button);
    return param;
}

Common::ParamPackage RawHat(const Common::ParamPackage& device, const std::string& direction) {
    auto param = RawParam(device);
    param.Set("hat", 0);
    param.Set("direction", direction);
    return param;
}

/// An analog trigger used as a button (ZL/ZR), pressed past the half-way point.
Common::ParamPackage RawTrigger(const Common::ParamPackage& device, int axis) {
    auto param = RawParam(device);
    param.Set("axis", axis);
    param.Set("threshold", "0.5");
    param.Set("invert", "+");
    return param;
}

/// A stick over two raw axes. Same param shape a recognised gamepad gets, with a zero centre
/// offset: an unrecognised pad gives us no trustworthy rest reading, and guessing one is what bakes
/// a permanent drift into the config.
Common::ParamPackage RawStick(const Common::ParamPackage& device, int axis_x, int axis_y) {
    auto param = RawParam(device);
    param.Set("axis_x", axis_x);
    param.Set("axis_y", axis_y);
    param.Set("offset_x", 0.0f);
    param.Set("offset_y", 0.0f);
    param.Set("invert_x", "+");
    param.Set("invert_y", "+");
    return param;
}

/// The standard layout of a modern gamepad as the Linux kernel enumerates it: BTN_SOUTH, BTN_EAST,
/// BTN_WEST, BTN_NORTH, shoulders, Select, Start, Guide, stick clicks; D-pad on hat 0; and the axes
/// in ABS order — X, Y, Z(=left trigger), RX, RY, RZ(=right trigger), so the RIGHT STICK sits on
/// axes 3/4 and the triggers on 2 and 5, not the SDL gamepad-axis numbering. (Confirmed against what
/// the SDL driver itself writes for the Deck's pad: `axis_x:3, axis_y:4` for the right stick.) Used
/// when SDL hands us no bindings of its own, in place of the upstream fallback that puts L/R on the
/// stick clicks and the D-pad on buttons that do not exist.
///
/// Mapped by PRINTED label (physical south = "A"), matching the swap applied to recognised pads
/// below, so the button the user sees as A is Switch A on every controller.
///
/// The mapping is TOTAL — every button the Switch has appears, and the ones with no counterpart here
/// are explicitly unbound. Leaving them out instead would keep whatever the config already held for
/// them, i.e. a raw button index belonging to a pad that is no longer the one being used; on a
/// matching port that stale index fires on a real, unrelated button.
///
/// Home and Screenshot are among those unbound on purpose: a wrong guess at Home is not a dead
/// button but a live one that yanks the user out of the game (this is how a plain R press once
/// dropped them to the menu), and nothing in the UI depends on either.
InputCommon::ButtonMapping StandardRawButtonMapping(const Common::ParamPackage& device) {
    namespace Btn = Settings::NativeButton;
    InputCommon::ButtonMapping mapping;
    for (int i = 0; i < Btn::NumButtons; ++i) {
        mapping.insert_or_assign(static_cast<Btn::Values>(i), Common::ParamPackage{});
    }
    mapping.insert_or_assign(Btn::A, RawButton(device, 0));      // south, labelled A
    mapping.insert_or_assign(Btn::B, RawButton(device, 1));      // east, labelled B
    mapping.insert_or_assign(Btn::X, RawButton(device, 2));      // west, labelled X
    mapping.insert_or_assign(Btn::Y, RawButton(device, 3));      // north, labelled Y
    mapping.insert_or_assign(Btn::L, RawButton(device, 4));
    mapping.insert_or_assign(Btn::R, RawButton(device, 5));
    mapping.insert_or_assign(Btn::Minus, RawButton(device, 6));  // Select / View
    mapping.insert_or_assign(Btn::Plus, RawButton(device, 7));   // Start / Menu
    mapping.insert_or_assign(Btn::LStick, RawButton(device, 9));
    mapping.insert_or_assign(Btn::RStick, RawButton(device, 10));
    mapping.insert_or_assign(Btn::ZL, RawTrigger(device, 2)); // ABS_Z
    mapping.insert_or_assign(Btn::ZR, RawTrigger(device, 5)); // ABS_RZ
    mapping.insert_or_assign(Btn::DUp, RawHat(device, "up"));
    mapping.insert_or_assign(Btn::DDown, RawHat(device, "down"));
    mapping.insert_or_assign(Btn::DLeft, RawHat(device, "left"));
    mapping.insert_or_assign(Btn::DRight, RawHat(device, "right"));
    return mapping;
}

/// Collects the real gamepads SDL exposes, excluding the keyboard/mouse pseudo-device (no guid/port)
/// and Steam Input's empty "Steam Virtual Gamepad" phantom slots. The Deck's built-in controller is
/// sorted to the end (identified by name) so external pads come first.
std::vector<Common::ParamPackage> CollectControllers(
    InputCommon::InputSubsystem& input_subsystem) {
    std::vector<Common::ParamPackage> controllers;
    for (const auto& device : input_subsystem.GetInputDevices()) {
        if (!device.Has("guid") || !device.Has("port")) {
            continue;
        }
        if (device.Get("display", "").find("Steam Virtual Gamepad") != std::string::npos) {
            continue;
        }
        controllers.push_back(device);
    }
    // De-duplicate the ONE physical Deck. Steam exposes the built-in pad twice: as its virtualised
    // gamepad (a real product name like "Xbox One Controller", a standard button layout that maps
    // correctly) AND as the raw "Steam Deck Controller" — same guid, but a different RAW button order
    // that mis-maps buttons (R landed on Home -> dropped to the menu), reads a bad stick centre
    // (drift), and doubles the player count. Since Eden reads raw joystick buttons, the raw duplicate
    // is the broken one: when a virtual device shares its guid, drop the raw "Steam Deck Controller".
    // This is conservative — it only removes a built-in device that a same-guid virtual already
    // covers, so a Deck with Steam Input off (raw only, no virtual) and genuine external pads
    // (different guids) are untouched.
    std::unordered_set<std::string> virtual_guids;
    for (const auto& d : controllers) {
        if (!DeviceIsBuiltIn(d)) {
            virtual_guids.insert(d.Get("guid", std::string{}));
        }
    }
    std::erase_if(controllers, [&](const Common::ParamPackage& d) {
        return DeviceIsBuiltIn(d) && virtual_guids.count(d.Get("guid", std::string{})) != 0;
    });

    // Second de-duplication pass, by capability rather than by name. The guid check above only
    // catches a raw duplicate that shares its guid with the virtual pad; Steam can just as well
    // hand the same physical Deck a *different* guid for each exposure, and then both copies
    // survive — with nothing but SDL's enumeration order deciding which one becomes Player 1. Pick
    // by what the device can actually do instead: when any properly recognised gamepad is present,
    // an unrecognised joystick alongside it is either that duplicate or junk, so drop it. Only when
    // nothing at all is recognised do we keep them, and map them by the standard layout above.
    const bool any_recognized =
        std::any_of(controllers.begin(), controllers.end(), [&](const Common::ParamPackage& d) {
            return DeviceIsRecognizedGamepad(input_subsystem, d);
        });
    if (any_recognized) {
        std::erase_if(controllers, [&](const Common::ParamPackage& d) {
            return !DeviceIsRecognizedGamepad(input_subsystem, d);
        });
    }

    std::stable_sort(controllers.begin(), controllers.end(),
                     [&](const Common::ParamPackage& a, const Common::ParamPackage& b) {
                         return !DeviceIsBuiltIn(a) && DeviceIsBuiltIn(b);
                     });
    return controllers;
}

/// A player counts as "bound" when a representative face button maps to a real gamepad engine (not
/// the keyboard/mouse fallbacks) — i.e. a controller was actually assigned to it.
bool HasControllerBinding(const Core::HID::EmulatedController& controller) {
    const Common::ParamPackage param = controller.GetButtonParam(Settings::NativeButton::A);
    const std::string engine = param.Get("engine", "");
    return !engine.empty() && engine != "keyboard" && engine != "mouse";
}

/// The device GUID a player is currently bound to (empty if none).
std::string BoundGuid(const Core::HID::EmulatedController& controller) {
    return controller.GetButtonParam(Settings::NativeButton::A).Get("guid", "");
}

/// The device port a player is currently bound to (-1 if none).
int BoundPort(const Core::HID::EmulatedController& controller) {
    return controller.GetButtonParam(Settings::NativeButton::A).Get("port", -1);
}

/// True when a player's binding matches a present device by its FULL identity (guid AND port).
/// Steam Input hands every virtual pad the same guid, so a guid-only check would wrongly treat a
/// stale binding as valid once the port renumbers on reconnect — leaving the pad with no input.
bool BindingMatchesDevice(const Core::HID::EmulatedController& controller,
                          const Common::ParamPackage& device) {
    return BoundGuid(controller) == device.Get("guid", "") &&
           BoundPort(controller) == device.Get("port", -2);
}

/// Applies `device`'s default mapping to `controller`, mirroring the input dialog's
/// enable/edit/disable/save sequence so persistence behaves identically.
void ApplyDefaultMapping(InputCommon::InputSubsystem& input_subsystem,
                         Core::HID::EmulatedController& controller,
                         const Common::ParamPackage& device) {
    const bool recognized = DeviceIsRecognizedGamepad(input_subsystem, device);

    controller.EnableConfiguration();

    // Only ask SDL for a mapping when it actually has one. For an unrecognised pad the driver
    // answers with the enum-index fallback (L/R on the stick clicks, Home on a shoulder, a D-pad on
    // buttons that do not exist) — worse than useless, since it looks like a valid mapping.
    auto button_mapping =
        recognized ? input_subsystem.GetButtonMappingForDevice(device) : InputCommon::ButtonMapping{};
    // Map by the pad's PRINTED labels (Xbox / Steam Deck layout), not by physical position. The
    // default mapping is positional (Switch A = the east button), but on an Xbox pad the east
    // button is labelled B — so "A" would fire Switch B. Swap A<->B and X<->Y so the button the user
    // sees as A is Switch A, B is B, etc. The standard layout below is already label-ordered, so
    // this only applies to SDL's mapping.
    //
    // Both pairs need it, including on the Deck's own controls. This once exempted the Steam virtual
    // pad from the X/Y half, on the reading that SDL already had that pair crossed for it. Measured
    // on the hardware instead of reasoned about: on the Deck's virtual pad the button PRINTED X
    // reports evdev code 0x133 and the one printed Y reports 0x134, which SDL enumerates as raw 2
    // and raw 3. SDL's default binding sends npad X to NORTH and npad Y to WEST, which resolve to
    // raw 3 and raw 2 — so without a swap npad X lands on the button printed Y and npad Y on the one
    // printed X. Exempting the pad is what crossed it.
    for (const auto& [lhs, rhs] : {std::pair{Settings::NativeButton::A, Settings::NativeButton::B},
                                   std::pair{Settings::NativeButton::X, Settings::NativeButton::Y}}) {
        if (button_mapping.contains(lhs) && button_mapping.contains(rhs)) {
            std::swap(button_mapping[lhs], button_mapping[rhs]);
        }
    }

    // Fill in the standard layout for a pad SDL gave us nothing for. Deliberately NOT applied on top
    // of a recognised pad's mapping: SDL knows that hardware better than any assumption here does,
    // and a pad that already works must not be second-guessed. (A recognised pad that still ends up
    // without a usable A is caught by the reconcile pass instead, which parks it rather than
    // re-mapping it forever.)
    if (!recognized) {
        for (const auto& [index, param] : StandardRawButtonMapping(device)) {
            button_mapping.insert_or_assign(index, param);
        }
    }
    for (const auto& [index, param] : button_mapping) {
        controller.SetButtonParam(index, param);
    }

    // Sticks. An empty analog mapping (what an unrecognised pad yields) must never be left as-is:
    // the controller would keep the *previous* device's stick params — the external pad's axes, or
    // its baked-in centre offset — which reads as a stick that drifts or does not move at all.
    auto stick_mapping = input_subsystem.GetAnalogMappingForDevice(device);
    if (stick_mapping.empty()) {
        stick_mapping.insert_or_assign(Settings::NativeAnalog::LStick, RawStick(device, 0, 1));
        stick_mapping.insert_or_assign(Settings::NativeAnalog::RStick, RawStick(device, 3, 4));
    }
    for (auto& [index, param] : stick_mapping) {
        // Never keep a captured stick centre. The SDL driver reads the LIVE axis value at the moment
        // of mapping and bakes it in as the neutral point — and a re-map fires whenever a pad's port
        // renumbers, which on the Deck means every time another controller is plugged in or pulled
        // out, quite possibly mid-game with a thumb on the stick. Anything below the driver's 0.2
        // guard then becomes permanent drift, and every further re-map bakes a fresh one on top.
        // A working pad reports a centred stick, and the 0.15 deadzone covers what is left; a
        // guessed offset can only make that worse.
        param.Set("offset_x", 0.0f);
        param.Set("offset_y", 0.0f);
        controller.SetStickParam(index, param);
    }

    for (const auto& [index, param] : input_subsystem.GetMotionMappingForDevice(device)) {
        controller.SetMotionParam(index, param);
    }
    controller.DisableConfiguration();
    controller.SaveCurrentConfig();

    // Log every face button, not just A: a report of "the wrong thing happened" has to be checkable
    // against all four at once, against the raw index each one actually ended up on.
    LOG_INFO(Input,
             "Steam Deck: mapped '{}' (both face pairs swapped to the printed labels) — A=[{}] "
             "B=[{}] X=[{}] Y=[{}] LStick=[{}]",
             device.Get("display", "?"),
             controller.GetButtonParam(Settings::NativeButton::A).Serialize(),
             controller.GetButtonParam(Settings::NativeButton::B).Serialize(),
             controller.GetButtonParam(Settings::NativeButton::X).Serialize(),
             controller.GetButtonParam(Settings::NativeButton::Y).Serialize(),
             controller.GetStickParam(Settings::NativeAnalog::LStick).Serialize());
}

/// Maps `device` onto `controller`, makes it a Pro Controller and connects it.
void AssignDevice(InputCommon::InputSubsystem& input_subsystem,
                  Core::HID::EmulatedController& controller, const Common::ParamPackage& device) {
    ApplyDefaultMapping(input_subsystem, controller, device);
    controller.SetNpadStyleIndex(Core::HID::NpadStyleIndex::Fullkey);
    controller.Connect();
}

/// Sets the emulated console's system language + region from the Deck's OS locale (the LANG/LC_ALL
/// env var, e.g. "ru_RU.UTF-8"), so the console boots in the user's own language — exactly what a
/// real Switch does from its setup locale, instead of defaulting to US English.
void ApplyDeckSystemLocale() {
    const char* env = std::getenv("LANG");
    if (env == nullptr || *env == '\0') {
        env = std::getenv("LC_ALL");
    }
    if (env == nullptr || *env == '\0') {
        return;
    }
    std::string loc = env; // "ru_RU.UTF-8"
    if (const auto cut = loc.find_first_of(".@"); cut != std::string::npos) {
        loc.resize(cut); // "ru_RU"
    }
    std::string lang = loc;
    std::string country;
    if (const auto us = loc.find('_'); us != std::string::npos) {
        lang = loc.substr(0, us);
        country = loc.substr(us + 1);
    }
    std::transform(lang.begin(), lang.end(), lang.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(country.begin(), country.end(), country.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    using L = Settings::Language;
    using R = Settings::Region;
    L language = L::EnglishAmerican;
    R region = R::Usa;
    const bool americas = country == "US" || country == "CA" || country == "MX" || country == "BR" ||
                          country == "AR" || country == "CL" || country == "CO" || country == "PE";

    if (lang == "ja") {
        language = L::Japanese;
        region = R::Japan;
    } else if (lang == "en") {
        language = country == "GB" ? L::EnglishBritish : L::EnglishAmerican;
        region = (country == "GB" || country == "IE") ? R::Europe
                 : (country == "AU" || country == "NZ") ? R::Australia
                                                        : R::Usa;
    } else if (lang == "fr") {
        language = country == "CA" ? L::FrenchCanadian : L::French;
        region = country == "CA" ? R::Usa : R::Europe;
    } else if (lang == "de") {
        language = L::German;
        region = R::Europe;
    } else if (lang == "it") {
        language = L::Italian;
        region = R::Europe;
    } else if (lang == "es") {
        language = americas ? L::SpanishLatin : L::Spanish;
        region = americas ? R::Usa : R::Europe;
    } else if (lang == "nl") {
        language = L::Dutch;
        region = R::Europe;
    } else if (lang == "pt") {
        language = country == "BR" ? L::PortugueseBrazilian : L::Portuguese;
        region = country == "BR" ? R::Usa : R::Europe;
    } else if (lang == "ru") {
        language = L::Russian;
        region = R::Europe;
    } else if (lang == "ko") {
        language = L::Korean;
        region = R::Korea;
    } else if (lang == "pl") {
        language = L::Polish;
        region = R::Europe;
    } else if (lang == "th") {
        language = L::Thai;
        region = R::Europe;
    } else if (lang == "zh") {
        const bool traditional = country == "TW" || country == "HK" || country == "MO";
        language = traditional ? L::ChineseTraditional : L::ChineseSimplified;
        region = traditional ? R::Taiwan : R::China;
    }

    Settings::values.language_index.SetValue(language);
    Settings::values.region_index.SetValue(region);
    LOG_INFO(Input, "Steam Deck: OS locale '{}' -> language index {}, region index {}", env,
             static_cast<int>(language), static_cast<int>(region));
}

} // Anonymous namespace

std::vector<std::string> GetPresentControllerNames(InputCommon::InputSubsystem& input_subsystem) {
    std::vector<std::string> names;
    for (const auto& device : CollectControllers(input_subsystem)) {
        names.push_back(device.Get("display", "?"));
    }
    return names;
}

std::string DescribePlayerController(InputCommon::InputSubsystem& input_subsystem,
                                     Core::HID::HIDCore& hid_core, std::size_t player_index) {
    const auto* const controller = hid_core.GetEmulatedControllerByIndex(player_index);
    if (controller == nullptr || !HasControllerBinding(*controller)) {
        return {};
    }
    // The SDL display name is the real, accurate label here ("Xbox One Controller", "Steam Deck
    // Controller", …). Match the bound pad by guid+port and strip SDL's trailing enumeration index
    // ("Xbox One Controller 0" -> "Xbox One Controller") for a clean name.
    const std::string guid = BoundGuid(*controller);
    const int port = BoundPort(*controller);
    for (const auto& d : input_subsystem.GetInputDevices()) {
        if (d.Get("guid", "") != guid || d.Get("port", -1) != port) {
            continue;
        }
        std::string display = d.Get("display", "");
        while (!display.empty() &&
               (std::isdigit(static_cast<unsigned char>(display.back())) || display.back() == ' ')) {
            display.pop_back();
        }
        if (!display.empty()) {
            return display;
        }
    }
    return "Controller";
}

int AutoConfigureSteamDeckControllers(InputCommon::InputSubsystem&, Core::HID::HIDCore&) {
    // Controller connection/assignment is deliberately left to Eden's default handling
    // and the Switch controller applet. The applet is the join mechanism — a player joins
    // by pressing a button on an *unassigned* controller — so pre-connecting controllers
    // here broke multiplayer join. This is now a no-op.
    return 0;
}

int ReconcileSteamDeckControllers(InputCommon::InputSubsystem& input_subsystem,
                                  Core::HID::HIDCore& hid_core) {
    if (!Common::IsSteamDeck()) {
        return 0;
    }
    constexpr std::size_t max_players = 8;


    // Auto-detect model: controllers connect by themselves. Input flows through Steam's gamepad
    // emulation into SDL (we keep Steam Input read-only and never activate the action API, which
    // would kill input on a non-Steam shortcut). Every external pad present becomes a player, in
    // order (P1, P2, …); the Deck's built-in is a player only when there is no external, so a docked
    // Deck with one pad shows exactly one player (Xbox), never a phantom second. Which pad plays is
    // then up to the game — the user just sees what is connected. No manual "add a player" step.

    const auto present = CollectControllers(input_subsystem); // external-first, built-in sorted last

    // Log the present set whenever it changes, so controller problems are diagnosable from the log.
    static std::string last_present_signature;
    std::string signature;
    for (const auto& d : present) {
        signature +=
            d.Get("display", "?") + "#" + d.Get("guid", "?") + "/" + d.Get("port", "?") + "; ";
    }
    // Devices that would not take a mapping. Without this the slot below never matches its device,
    // so every tick re-runs the full mapping: a 2 Hz storm of re-binds that also re-snapshots the
    // stick centre each time, which is drift by construction. Forget the list whenever the set of
    // present devices changes, so a re-plug always gets a fresh try.
    static std::unordered_set<std::string> unmappable;

    // Any change to the set of present devices re-maps every player, rather than trusting the
    // binding already in the config. On the Deck guid+port is NOT a stable identity: Steam re-exposes
    // the one built-in pad under different faces — "Steam Deck Controller" on port 0 and "Xbox One
    // Controller" on port 1, same guid — and each face has its own raw button order. A config written
    // under one face still matches the other by guid, so the old check kept a mapping whose buttons
    // had quietly moved: A pressed nothing, while Plus/Minus happened to still line up (which is why
    // the in-game exit gesture kept working while the menu looked dead). This also covers the very
    // first tick after launch, where the signature changes from empty and everything is re-mapped
    // against whatever face SDL is showing right now. Re-mapping is cheap and idempotent now that no
    // stick centre is captured, so preferring it over a stale binding costs nothing.
    const bool devices_changed = signature != last_present_signature;
    if (devices_changed) {
        last_present_signature = signature;
        unmappable.clear();
        LOG_INFO(Input, "Steam Deck: {} controller(s) present: {}", present.size(),
                 signature.empty() ? "(none)" : signature);
    }

    // The devices that become players: every external pad (external-first). Only when no external is
    // present does the built-in step in as the lone Player 1.
    std::vector<const Common::ParamPackage*> player_devices;
    for (const auto& d : present) {
        if (!DeviceIsBuiltIn(d)) {
            player_devices.push_back(&d);
        }
    }
    if (player_devices.empty() && !present.empty()) {
        player_devices.push_back(&present.front()); // only the built-in is here — it is Player 1
    }

    // Assign player_devices[i] -> Player i, connecting it; disconnect any slot with no device. A pad
    // already correctly bound to its slot is left untouched (never disturb a working controller). We
    // never rescan the SDL subsystem here: under Steam that tears down Steam's injected virtual pads
    // and they do not come back, which killed all controllers. SDL hotplug delivers new pads on its
    // own, so we simply reflect whatever SDL currently reports.
    int changed = 0;
    auto& players = Settings::values.players.GetValue();
    for (std::size_t i = 0; i < max_players; ++i) {
        auto* const controller = hid_core.GetEmulatedControllerByIndex(i);
        if (controller == nullptr) {
            continue;
        }
        if (i < player_devices.size()) {
            const Common::ParamPackage& device = *player_devices[i];
            const std::string device_key =
                device.Get("guid", "") + "/" + device.Get("port", "");
            if (unmappable.count(device_key) != 0) {
                continue; // already tried and it did not stick; do not loop on it
            }
            if (devices_changed || !BindingMatchesDevice(*controller, device)) {
                // A genuinely new/different device (or a port renumber) — do the full default mapping.
                AssignDevice(input_subsystem, *controller, device);
                ++changed;
                if (!BindingMatchesDevice(*controller, device)) {
                    unmappable.insert(device_key);
                    LOG_ERROR(Input,
                              "Steam Deck: Player {} would not take a mapping for '{}' (guid {} "
                              "port {}) — leaving it alone",
                              i + 1, device.Get("display", "?"), device.Get("guid", "?"),
                              device.Get("port", "?"));
                    continue;
                }
                LOG_INFO(Input, "Steam Deck: Player {} = '{}' (guid {} port {})", i + 1,
                         device.Get("display", "?"), device.Get("guid", "?"),
                         device.Get("port", "?"));
            } else if (!controller->IsConnected()) {
                // Same device, only the connection dropped (e.g. the controller applet force-
                // disconnected it). Reconnect WITHOUT re-running the default mapping: re-mapping
                // re-snapshots the analog centre from the live (possibly deflected) stick position and
                // bakes that offset in as permanent drift. Keep the existing, good mapping.
                controller->Connect();
                ++changed;
                LOG_INFO(Input, "Steam Deck: Player {} reconnected (kept mapping)", i + 1);
            }
        } else if (controller->IsConnected() || HasControllerBinding(*controller)) {
            controller->Disconnect();
            if (i < players.size()) {
                players[i].connected = false; // persist so a game boot won't reconnect it
            }
            ++changed;
        }
    }

    // The Handheld npad is the same physical pad as Player 1, but it is NOT one of the player slots
    // above — so it quietly kept whatever mapping an older build left in the config. That is enough
    // to break the console UI: the menu ORs the button state of every connected controller, so a
    // stale Handheld with A and B the other way round turns one press into BOTH bits in a single
    // poll. The page opened on Accept and closed again on Back 1 ms later, which is why only the
    // dock items that do not navigate — Sleep and Power — appeared to work at all.
    //
    // Keep its MAPPING in lockstep with Player 1 — and nothing else. Its connection state belongs to
    // the controller applet, which hands Handheld the pad for games that only accept handheld play
    // and disconnects it otherwise. Disconnecting it from here as well, twice a second, would tear
    // that down half a second after the applet set it up: the game loses its controller, asks for
    // the applet again, and the two sides fight forever.
    if (auto* const handheld = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
        handheld != nullptr && !player_devices.empty()) {
        const Common::ParamPackage& device = *player_devices.front();
        if (devices_changed || !BindingMatchesDevice(*handheld, device)) {
            ApplyDefaultMapping(input_subsystem, *handheld, device);
            ++changed;
            LOG_INFO(Input, "Steam Deck: Handheld re-mapped to match Player 1");
        }
    }

    return changed;
}

bool ShouldExitGameOnHotkeyHold(Core::HID::HIDCore& hid_core) {
    if (!Common::IsSteamDeck()) {
        return false;
    }

    // Polled at ~2 Hz (500 ms); holding across this many samples ≈ 1 s — deliberate but responsive.
    constexpr int hold_threshold = 2;
    static int hold_ticks = 0;

    // Refuse to fire again for a few seconds after each suspend. Releasing the pair and re-holding
    // it re-arms the gesture within one tick, and the suspended title stays powered on — so the
    // poll keeps running and a second hold lands on top of a suspend that has not finished. That is
    // how it fired three times in ten seconds and looked like a freeze. The delay is long enough to
    // cover the suspend and far shorter than any deliberate second use.
    constexpr int cooldown_ticks = 8; // ~4 s
    static int cooldown = 0;
    if (cooldown > 0) {
        --cooldown;
        return false;
    }

    // Exit gesture: Minus+Plus (Select+Start) held together — present on Xbox pads, the Deck's
    // built-in controls and Switch pads alike. A held Home button also works where present.
    // (NpadButton: plus=bit 10, minus=bit 11.)
    const auto gesture_held = [](const Core::HID::EmulatedController& controller) {
        // Deliberate TWO-button hold only: Minus+Plus (Select+Start). A single mapped button must
        // never trigger this. On the Deck the one physical pad is exposed several times (Steam's
        // virtual Xbox pads + the raw "Steam Deck Controller"), each with a different raw button
        // layout; the old `|| home` shortcut fired whenever a single button (e.g. R on the raw pad)
        // landed on the raw index that pad reports as Home — so pressing R dropped you to the menu.
        // Requiring BOTH Plus and Minus makes it impossible for any one button to fire the gesture.
        const auto npad_raw = static_cast<unsigned long long>(controller.GetNpadButtons().raw);
        return (npad_raw & (1ULL << 10)) != 0 && (npad_raw & (1ULL << 11)) != 0; // Plus & Minus
    };

    // Scan every controller slot, not just Player 1 / Handheld — under Steam the active pad can land
    // on a different npad id, and the gesture should work from whichever one the user holds.
    static constexpr Core::HID::NpadIdType kIds[] = {
        Core::HID::NpadIdType::Handheld, Core::HID::NpadIdType::Player1,
        Core::HID::NpadIdType::Player2,  Core::HID::NpadIdType::Player3,
        Core::HID::NpadIdType::Player4,  Core::HID::NpadIdType::Player5,
        Core::HID::NpadIdType::Player6,  Core::HID::NpadIdType::Player7,
        Core::HID::NpadIdType::Player8,
    };

    bool held = false;
    for (const auto npad_id : kIds) {
        const auto* const controller = hid_core.GetEmulatedController(npad_id);
        if (controller == nullptr || !controller->IsConnected()) {
            continue;
        }
        if (gesture_held(*controller)) {
            held = true;
            break;
        }
    }

    if (!held) {
        hold_ticks = 0; // released — re-arm for the next hold
        return false;
    }
    if (hold_ticks < 0) {
        return false; // already fired for this hold; wait for release
    }
    if (++hold_ticks >= hold_threshold) {
        hold_ticks = -1;
        cooldown = cooldown_ticks;
        LOG_INFO(Input, "Steam Deck: HOME gesture (Select+Start) held — suspending to menu");
        return true;
    }
    return false;
}

bool ApplySteamDeckDefaultsOnce() {
    if (!Common::IsSteamDeck()) {
        return false;
    }

    // Apply the Deck profile only once per profile version; a versioned marker in the config dir
    // records that it ran, so the user's later tuning is preserved. Bump the suffix whenever the
    // optimal profile below changes, so it re-applies exactly once on existing installs.
    const auto marker =
        Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "deck_defaults_applied_v4";
    if (Common::FS::Exists(marker)) {
        return false;
    }

    // Both Deck models (LCD "Jupiter" / OLED "Galileo") share the specs that decide the emulator
    // config — RDNA2 8-CU iGPU on RADV/Vulkan, Zen 2 4c/8t, 16 GB unified RAM, a 1280x800 panel — so
    // one profile is correct for both. The OLED only differs where it does NOT need different config:
    // faster LPDDR5 and a cooler 6nm APU (it just runs this same profile better) and a 90 Hz panel
    // (VSync Fifo below syncs to whatever refresh the panel reports, so nothing is hardcoded to 60).
    const Common::SteamDeckModel model = Common::GetSteamDeckModel();
    LOG_INFO(Input, "Steam Deck: applying native profile on model {}",
             model == Common::SteamDeckModel::OLED   ? "OLED (Galileo)"
             : model == Common::SteamDeckModel::LCD   ? "LCD (Jupiter)"
                                                      : "Unknown");

    // Graphics — Vulkan is the only good backend on RADV; native 1x (Switch handheld resolution) with
    // FSR upscaling; async GPU + async shaders + disk/pipeline caches hide compilation stutter.
    Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Vulkan);
    Settings::values.resolution_setup.SetValue(Settings::ResolutionSetup::Res1X);
    Settings::values.scaling_filter.SetValue(Settings::ScalingFilter::Fsr);
    Settings::values.vsync_mode.SetValue(Settings::VSyncMode::Fifo);
    Settings::values.aspect_ratio.SetValue(Settings::AspectRatio::R16_9);
    Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::Low);
    Settings::values.vram_usage_mode.SetValue(Settings::VramUsageMode::Conservative);
    Settings::values.nvdec_emulation.SetValue(Settings::NvdecEmulation::Gpu);
    Settings::values.use_asynchronous_gpu_emulation.SetValue(true);
    Settings::values.use_asynchronous_shaders.SetValue(true);
    Settings::values.use_disk_shader_cache.SetValue(true);
    Settings::values.use_vulkan_driver_pipeline_cache.SetValue(true);
    Settings::values.use_reactive_flushing.SetValue(true);

    // CPU — the host is x86-64, so Dynarmic (JIT) is the only valid backend (NCE is ARM-host only);
    // Auto accuracy picks the right per-title level; multicore on; 4 GB matches the real Switch.
    Settings::values.cpu_backend.SetValue(Settings::CpuBackend::Dynarmic);
    Settings::values.cpu_accuracy.SetValue(Settings::CpuAccuracy::Auto);
    Settings::values.use_multi_core.SetValue(true);
    Settings::values.memory_layout_mode.SetValue(Settings::MemoryLayout::Memory_4Gb);

    // Audio — let it pick the working Deck backend (PipeWire via SDL/Cubeb).
    Settings::values.sink_id.SetValue(Settings::AudioEngine::Auto);

    // Input / system — Docked so external Pro Controllers and the join applet work (the built-in
    // still drives Player 1 alone); native Joy-Con support on; virtual SD on.
    Settings::values.use_docked_mode.SetValue(Settings::ConsoleMode::Docked);
    Settings::values.controller_navigation.SetValue(true);
    Settings::values.enable_joycon_driver.SetValue(true);
    Settings::values.use_virtual_sd.SetValue(true);

    // Boot the emulated console in the Deck's own language/region, like a real Switch's setup locale.
    ApplyDeckSystemLocale();

    std::ofstream marker_file{marker};
    marker_file << "1\n";

    LOG_INFO(Input, "Steam Deck: applied native Switch-on-Deck profile (first run)");
    return true;
}

} // namespace FrontendCommon
