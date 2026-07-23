// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace Common {

/// Identifies which Steam Deck model the emulator is running on, if any.
enum class SteamDeckModel {
    None,    ///< Not running on Steam Deck hardware.
    LCD,     ///< Steam Deck LCD (firmware codename "Jupiter").
    OLED,    ///< Steam Deck OLED (firmware codename "Galileo").
    Unknown, ///< Detected as a Deck, but the specific model could not be resolved.
};

/// Returns the detected Steam Deck model. The result is computed once and cached.
[[nodiscard]] SteamDeckModel GetSteamDeckModel();

/// Returns true when running on Steam Deck hardware (any model).
[[nodiscard]] bool IsSteamDeck();

/// Returns true when running inside SteamOS Gaming Mode (the gamescope session),
/// as opposed to the KDE-based Desktop Mode. Only meaningful on a Steam Deck.
[[nodiscard]] bool IsSteamDeckGamingMode();

} // namespace Common
