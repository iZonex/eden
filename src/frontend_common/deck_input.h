// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace Core::HID {
class HIDCore;
}

namespace InputCommon {
class InputSubsystem;
}

namespace FrontendCommon {

/// Display names of the real controllers currently present (external pads first, the Deck's
/// built-in last; keyboard/mouse and Steam Input phantoms excluded). For the Controllers UI.
std::vector<std::string> GetPresentControllerNames(InputCommon::InputSubsystem& input_subsystem);

/**
 * A short human label of the PHYSICAL pad driving a player slot — "Steam Deck", "Xbox Controller",
 * "PlayStation", "Switch Pro", "Joy-Con" or a plain "Controller" — resolved from the pad's real Steam
 * Input type when available (the SDL view alone cannot tell them apart), else the SDL device name.
 * This is what lets the Controllers UI show which physical controller each player actually is. Empty
 * when that player has no controller bound.
 */
std::string DescribePlayerController(InputCommon::InputSubsystem& input_subsystem,
                                     Core::HID::HIDCore& hid_core, std::size_t player_index);

/**
 * Steam Deck "console mode" auto-configuration.
 *
 * On a Steam Deck, the controller hardware is fixed and always present, so there
 * is no reason to make the user open the input dialog and map anything by hand.
 * This walks the connected controller-class devices and, for every player that
 * does not already have a real gamepad binding, applies the same default mapping
 * the input dialog's auto-map produces (buttons + sticks + motion), then connects
 * that player so the running game sees it.
 *
 * It is a no-op when not running on a Steam Deck, and it never overwrites a player
 * that already has a controller profile, so a user's manual setup is preserved.
 *
 * @return the number of players that were newly auto-configured.
 */
int AutoConfigureSteamDeckControllers(InputCommon::InputSubsystem& input_subsystem,
                                      Core::HID::HIDCore& hid_core);

/**
 * Auto-detects and connects controllers; intended for a periodic timer (menu and in-game). Every
 * external pad present becomes a player in order (P1, P2, …) with no manual step — turn one on and it
 * appears; the Deck's built-in is a player only when no external is present, so a docked Deck with one
 * pad shows exactly one player and never a phantom second. A pad already correctly bound to its slot
 * is left untouched (a working controller is never disturbed), and when a pad disappears an SDL
 * hardware rescan is forced once to re-detect pads SDL misses on reconnect (libsdl-org/SDL#8260).
 * No-op off-Deck.
 *
 * @return the number of player slots whose connection changed this call.
 */
int ReconcileSteamDeckControllers(InputCommon::InputSubsystem& input_subsystem,
                                  Core::HID::HIDCore& hid_core);

/**
 * Nintendo-Switch-style "close game": returns true once the Steam Deck user has held
 * the exit gesture long enough (about a second) to quit back to the game list. The
 * gesture is Minus+Plus (Select+Start) held together — available on Xbox pads, the
 * Deck's built-in controls and Switch pads — or a held Home button where present. A
 * short tap never triggers it. Poll from the same periodic timer as
 * ReconcileSteamDeckControllers and stop emulation when it returns true. No-op off-Deck.
 */
bool ShouldExitGameOnHotkeyHold(Core::HID::HIDCore& hid_core);

/**
 * On a Steam Deck, applies graphics/system defaults tuned for the device (Vulkan,
 * native resolution, async GPU + async shaders, disk shader cache, Normal GPU accuracy,
 * handheld mode) so the emulator runs well out of the box. Runs only once — a marker
 * file in the config directory records that it has applied, so the user's later tuning
 * is never overwritten. No-op off-Deck or after the first run.
 *
 * @return true if defaults were applied this call (the caller should then persist config).
 */
bool ApplySteamDeckDefaultsOnce();

} // namespace FrontendCommon
