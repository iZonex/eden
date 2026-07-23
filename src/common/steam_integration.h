// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Common::Steam {

/// The physical controller kind Steam Input reports. Unlike the SDL/XInput view (where Steam Input
/// masks every pad as an identical virtual gamepad), this tells the Deck's built-in controller apart
/// from an external Xbox/DualSense/Switch pad — which is what our player assignment actually needs.
enum class ControllerKind {
    Unknown,
    SteamDeck,
    SteamController,
    XBox360,
    XBoxOne,
    PlayStation3,
    PlayStation4,
    PlayStation5,
    SwitchPro,
    SwitchJoyConPair,
    SwitchJoyConSingle,
    Generic,
};

/// A controller as seen natively through Steam Input: a STABLE handle (survives reconnect), its real
/// kind, and the Steam gamepad slot Valve assigned it. On the Deck's XInput emulation that slot IS the
/// SDL "…pad N" port, so it maps a real controller type onto the SDL device we read input from.
struct SteamController {
    std::uint64_t handle{};
    ControllerKind kind{ControllerKind::Unknown};
    int steam_player_index{-1}; ///< Steam gamepad slot == SDL/XInput port (-1 if not seated yet)
};

/// Bring up Steamworks + Steam Input in READ-ONLY mode for controller identification.
///
/// We deliberately do NOT activate the Steam Input action API (no action manifest): on a non-Steam
/// shortcut a manifest with no bundled bindings yields an empty config, which makes Steam drop its
/// XInput gamepad emulation and deliver no input at all. Instead Steam's default gamepad emulation
/// keeps feeding SDL (guaranteed input, zero config) and this interface is queried only for each
/// pad's real type/slot (GetControllers), so the built-in pad can be told apart from external ones.
///
/// @param action_manifest_path  Reserved; pass empty. A non-empty path would activate the action API
///                              (see above) and is intentionally unused on the shortcut input path.
/// @return false (no-op) unless built with the Steamworks SDK AND running under Steam. Safe always.
bool Init(const std::string& action_manifest_path);

/// Tear down Steam Input + Steamworks. Safe to call when not initialized.
void Shutdown();

/// True once Init() succeeded and Steam Input is usable for identification.
bool IsAvailable();

/// Pump Steam callbacks + Steam Input (RunFrame); call periodically (e.g. from the reconcile timer).
/// Required before GetControllers() returns handles.
void RunCallbacks();

/// The controllers Steam Input currently reports, with stable handles and real kinds. Empty when
/// unavailable. This is the native identity the SDL/XInput view alone cannot give (there every pad
/// looks like an identical virtual gamepad, distinguishable only by port).
std::vector<SteamController> GetControllers();

} // namespace Common::Steam
