// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>
#include <QString>

#include "common/uuid.h"
#include "yuzu/deck/deck_page.h"

namespace Core {
class System;
}

class QLabel;

/**
 * Console-mode "Users" screen — the Switch multi-user manager, laid out like the reference: a left
 * sidebar listing every profile (avatar + name, the active one badged) and a right pane showing the
 * selected profile (large avatar, name, active state). A makes the selected profile active, X adds a
 * new one. Each profile keeps its own save data. Rename/delete stays in the desktop settings for now.
 */
class DeckUsersPage : public DeckPage {
    Q_OBJECT

public:
    DeckUsersPage(Core::System& system, QWidget* parent = nullptr);
    ~DeckUsersPage() override;

    bool OnNavigate(Qt::Key key) override;
    bool OnAccept() override;        // A — make the selected profile active
    bool OnPrimaryAction() override; // ＋ / X — create a new profile
    std::vector<DeckHint> Hints() const override;
    void OnActivated() override;

signals:
    void SaveConfigRequested();

private:
    void Rebuild();
    void UpdateDetail();
    void SetSelected(int index);
    void CreateUser();

    Core::System& system;
    QLabel* title = nullptr;
    QLabel* hint = nullptr;
    QLabel* profile_avatar = nullptr;
    QLabel* profile_name = nullptr;
    QLabel* profile_status = nullptr;
    class UserSidebar* sidebar = nullptr;
    std::vector<Common::UUID> users; ///< the valid profile UUIDs, in sidebar order
    int selected = 0;
};
