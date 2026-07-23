// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <string>
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
    controller.EnableConfiguration();
    auto button_mapping = input_subsystem.GetButtonMappingForDevice(device);
    // Map by the pad's PRINTED labels (Xbox / Steam Deck layout), not by physical position. The
    // default mapping is positional (Switch A = the east button), but on an Xbox/Deck pad the east
    // button is labelled B — so "A" would fire Switch B. Swap A<->B and X<->Y so the button the user
    // sees as A is Switch A, B is B, etc. (Steam Deck's built-in pad uses the Xbox layout too.)
    if (button_mapping.contains(Settings::NativeButton::A) &&
        button_mapping.contains(Settings::NativeButton::B)) {
        std::swap(button_mapping[Settings::NativeButton::A],
                  button_mapping[Settings::NativeButton::B]);
    }
    if (button_mapping.contains(Settings::NativeButton::X) &&
        button_mapping.contains(Settings::NativeButton::Y)) {
        std::swap(button_mapping[Settings::NativeButton::X],
                  button_mapping[Settings::NativeButton::Y]);
    }
    for (const auto& [index, param] : button_mapping) {
        controller.SetButtonParam(index, param);
    }
    for (const auto& [index, param] : input_subsystem.GetAnalogMappingForDevice(device)) {
        controller.SetStickParam(index, param);
    }
    for (const auto& [index, param] : input_subsystem.GetMotionMappingForDevice(device)) {
        controller.SetMotionParam(index, param);
    }
    controller.DisableConfiguration();
    controller.SaveCurrentConfig();
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
    if (signature != last_present_signature) {
        last_present_signature = signature;
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
            if (!controller->IsConnected() || !BindingMatchesDevice(*controller, device)) {
                AssignDevice(input_subsystem, *controller, device);
                ++changed;
                LOG_INFO(Input, "Steam Deck: Player {} = '{}' (guid {} port {})", i + 1,
                         device.Get("display", "?"), device.Get("guid", "?"),
                         device.Get("port", "?"));
            }
        } else if (controller->IsConnected() || HasControllerBinding(*controller)) {
            controller->Disconnect();
            if (i < players.size()) {
                players[i].connected = false; // persist so a game boot won't reconnect it
            }
            ++changed;
        }
    }
    return changed;
}

bool ShouldExitGameOnHotkeyHold(Core::HID::HIDCore& hid_core) {
    if (!Common::IsSteamDeck()) {
        return false;
    }

    // Reconcile is polled at ~2 Hz, so requiring the gesture held across this many samples
    // means it was held for roughly a second — deliberate enough to avoid an accidental exit.
    constexpr int hold_threshold = 3;
    static int hold_ticks = 0;

    // Exit gesture: Minus+Plus (Select+Start) held together — present on Xbox pads, the
    // Deck's built-in controls and Switch pads alike. A held Home button also works where
    // present. (NpadButtonState layout: plus=bit 10, minus=bit 11.)
    const auto exit_gesture_held = [](const Core::HID::EmulatedController& controller) {
        const auto npad_raw = static_cast<unsigned long long>(controller.GetNpadButtons().raw);
        const bool select_start = (npad_raw & (1ULL << 10)) != 0 && (npad_raw & (1ULL << 11)) != 0;
        const bool home = controller.GetHomeButtons().raw != 0;
        return select_start || home;
    };

    bool held = false;
    for (const auto npad_id : {Core::HID::NpadIdType::Handheld, Core::HID::NpadIdType::Player1}) {
        const auto* const controller = hid_core.GetEmulatedController(npad_id);
        if (controller != nullptr && controller->IsConnected() && exit_gesture_held(*controller)) {
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
        LOG_INFO(Input, "Steam Deck: exit gesture (Select+Start) held — requesting game exit");
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
        Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "deck_defaults_applied_v2";
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
