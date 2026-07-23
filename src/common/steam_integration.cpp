// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>

#include "common/logging.h"
#include "common/steam_integration.h"

#ifdef EDEN_STEAMWORKS
#include <steam/steam_api.h>
#endif

namespace Common::Steam {

#ifdef EDEN_STEAMWORKS

namespace {
bool g_initialized = false;

ControllerKind MapKind(ESteamInputType type) {
    switch (type) {
    case k_ESteamInputType_SteamController:
        return ControllerKind::SteamController;
    case k_ESteamInputType_SteamDeckController:
        return ControllerKind::SteamDeck;
    case k_ESteamInputType_XBox360Controller:
        return ControllerKind::XBox360;
    case k_ESteamInputType_XBoxOneController:
        return ControllerKind::XBoxOne;
    case k_ESteamInputType_PS3Controller:
        return ControllerKind::PlayStation3;
    case k_ESteamInputType_PS4Controller:
        return ControllerKind::PlayStation4;
    case k_ESteamInputType_PS5Controller:
        return ControllerKind::PlayStation5;
    case k_ESteamInputType_SwitchProController:
        return ControllerKind::SwitchPro;
    case k_ESteamInputType_SwitchJoyConPair:
        return ControllerKind::SwitchJoyConPair;
    case k_ESteamInputType_SwitchJoyConSingle:
        return ControllerKind::SwitchJoyConSingle;
    case k_ESteamInputType_GenericGamepad:
        return ControllerKind::Generic;
    default:
        return ControllerKind::Unknown;
    }
}
} // namespace

bool Init(const std::string& action_manifest_path) {
    if (g_initialized) {
        return true;
    }
    if (!SteamAPI_Init()) {
        // A non-Steam shortcut has no real AppId; fall back to Valve's test app (480, Spacewar) so
        // SteamAPI_Init succeeds while Steam Input's per-shortcut controller config still applies.
        ::setenv("SteamAppId", "480", 1);
        ::setenv("SteamGameId", "480", 1);
        if (!SteamAPI_Init()) {
            LOG_INFO(Common, "SteamAPI_Init unavailable (non-Steam context) — SDL input path");
            return false;
        }
    }
    if (SteamInput() == nullptr) {
        SteamAPI_Shutdown();
        return false;
    }
    // Reserved: activating the action API here (a manifest with no bundled bindings) drops Steam's
    // XInput emulation and kills all input on a non-Steam shortcut. We pass empty and stay read-only.
    if (!action_manifest_path.empty()) {
        SteamInput()->SetInputActionManifestFilePath(action_manifest_path.c_str());
    }
    if (!SteamInput()->Init(true)) { // true: we drive RunFrame() ourselves from RunCallbacks()
        LOG_WARNING(Common, "SteamInput init failed");
        SteamAPI_Shutdown();
        return false;
    }
    g_initialized = true;
    LOG_INFO(Common, "Steam Input up (read-only controller identification; input via SDL)");
    return true;
}

void Shutdown() {
    if (!g_initialized) {
        return;
    }
    SteamInput()->Shutdown();
    SteamAPI_Shutdown();
    g_initialized = false;
}

bool IsAvailable() {
    return g_initialized;
}

void RunCallbacks() {
    if (!g_initialized) {
        return;
    }
    SteamAPI_RunCallbacks();
    SteamInput()->RunFrame();
}

std::vector<SteamController> GetControllers() {
    std::vector<SteamController> out;
    if (!g_initialized) {
        return out;
    }
    InputHandle_t handles[STEAM_INPUT_MAX_COUNT];
    const int count = SteamInput()->GetConnectedControllers(handles);
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        SteamController controller;
        controller.handle = static_cast<std::uint64_t>(handles[i]);
        controller.kind = MapKind(SteamInput()->GetInputTypeForHandle(handles[i]));
        controller.steam_player_index = SteamInput()->GetGamepadIndexForController(handles[i]);
        out.push_back(controller);
    }
    return out;
}

#else // !EDEN_STEAMWORKS — built without the Steamworks SDK: no-op stubs.

bool Init(const std::string&) {
    return false;
}
void Shutdown() {}
bool IsAvailable() {
    return false;
}
void RunCallbacks() {}
std::vector<SteamController> GetControllers() {
    return {};
}

#endif

} // namespace Common::Steam
