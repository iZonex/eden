// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <QString>

#include "yuzu/deck/deck_page.h"

namespace Core::HID {
class HIDCore;
}
namespace InputCommon {
class InputSubsystem;
}

class QTimer;
class QLabel;

/**
 * Console-mode "Controllers" screen — a live status view. Controllers connect by themselves (see
 * ReconcileSteamDeckControllers): every external pad becomes a player, the Deck's built-in only when
 * no external is present. This screen simply shows what is connected, with each player labelled by
 * its real physical pad (Xbox / Steam Deck / …) so the user can see which controller is which. There
 * is no manual "add"/"remove" — turn a pad on and it appears here.
 */
class DeckControllersPage : public DeckPage {
    Q_OBJECT

public:
    DeckControllersPage(Core::HID::HIDCore& hid_core, InputCommon::InputSubsystem& input_subsystem,
                        QWidget* parent = nullptr);
    ~DeckControllersPage() override;

    std::vector<DeckHint> Hints() const override;
    void OnActivated() override;
    void OnDeactivated();

signals:
    void SaveConfigRequested();

private:
    void Rebuild();

    static constexpr int kMaxPlayers = 8;

    Core::HID::HIDCore& hid_core;
    InputCommon::InputSubsystem& input;
    QTimer* poll_timer = nullptr;
    QLabel* title = nullptr;
    QLabel* hint = nullptr;
    QLabel* mode = nullptr;
    std::array<class PlayerCard*, kMaxPlayers> cards{};
    int connected_count = 0;
};
