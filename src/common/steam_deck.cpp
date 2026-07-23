// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <string>

#include "common/steam_deck.h"

#ifdef __linux__
#include <fstream>
#endif

namespace Common {

namespace {

#ifdef __linux__
std::string ReadTrimmedLine(const char* path) {
    std::ifstream file(path);
    if (!file) {
        return {};
    }
    std::string contents;
    std::getline(file, contents);
    // DMI values frequently carry trailing whitespace or newlines.
    while (!contents.empty() && (contents.back() == '\n' || contents.back() == '\r' ||
                                 contents.back() == ' ' || contents.back() == '\t')) {
        contents.pop_back();
    }
    return contents;
}

bool EnvEquals(const char* name, const char* expected) {
    const char* value = ::getenv(name);
    return value != nullptr && std::string(value) == expected;
}

bool EnvContains(const char* name, const char* needle) {
    const char* value = ::getenv(name);
    return value != nullptr && std::string(value).find(needle) != std::string::npos;
}
#endif

SteamDeckModel DetectSteamDeckModel() {
#ifdef __linux__
    // The DMI product name is the most reliable signal: it is set by the firmware,
    // is present in both Gaming Mode and Desktop Mode, and is unaffected by whether
    // Steam Input is currently masking the physical controller.
    const std::string product = ReadTrimmedLine("/sys/class/dmi/id/product_name");
    if (product == "Jupiter") {
        return SteamDeckModel::LCD;
    }
    if (product == "Galileo") {
        return SteamDeckModel::OLED;
    }

    // Fallback: SteamOS Gaming Mode exports SteamDeck=1 even where the board report
    // is non-standard (e.g. some clones or future revisions).
    if (EnvEquals("SteamDeck", "1")) {
        return SteamDeckModel::Unknown;
    }
#endif
    return SteamDeckModel::None;
}

} // Anonymous namespace

SteamDeckModel GetSteamDeckModel() {
    static const SteamDeckModel model = DetectSteamDeckModel();
    return model;
}

bool IsSteamDeck() {
    return GetSteamDeckModel() != SteamDeckModel::None;
}

bool IsSteamDeckGamingMode() {
#ifdef __linux__
    if (!IsSteamDeck()) {
        return false;
    }
    // Gaming Mode runs under the gamescope compositor, which advertises itself
    // through the XDG desktop hint and its own Wayland display variable.
    return EnvContains("XDG_CURRENT_DESKTOP", "gamescope") ||
           ::getenv("GAMESCOPE_WAYLAND_DISPLAY") != nullptr;
#else
    return false;
#endif
}

} // namespace Common
