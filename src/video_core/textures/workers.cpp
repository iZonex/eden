// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/textures/workers.h"
#include <algorithm>
#include <cstdlib>
#include "common/steam_deck.h"

namespace Tegra::Texture {

Common::ThreadWorker& GetThreadWorkers() {
    // Half the hardware threads, but capped on a handheld. These run alongside the pipeline
    // compilers, and on a four-core Deck the two pools together are enough to starve the game
    // itself — worst of all during a first boot, when both are busiest at the same moment.
    static const bool no_cap = std::getenv("EDEN_NO_HANDHELD_WORKER_CAP") != nullptr;
    static Common::ThreadWorker workers{
        (Common::IsSteamDeck() && !no_cap)
            ? std::min<std::size_t>((std::max)(std::thread::hardware_concurrency(), 2U) / 2, 2U)
            : (std::max)(std::thread::hardware_concurrency(), 2U) / 2,
        "ImageTranscode"};
    return workers;
}

} // namespace Tegra::Texture
