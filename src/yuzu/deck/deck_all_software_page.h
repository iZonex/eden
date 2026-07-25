// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include "common/common_types.h"
#include "yuzu/deck/deck_groups.h"
#include "yuzu/deck/deck_page.h"

class QAbstractItemModel;
class QSortFilterProxyModel;
class QIdentityProxyModel;
class QLabel;
class QListView;
class QTimer;
class QResizeEvent;
class DeckKeyboard;

/**
 * Console-mode "All Software" — the Switch HOME full-library screen, with Groups (folders). The root
 * view is a grid of group folders, a "＋ New Group" tile, then every game. Selecting a folder enters
 * that group (its games only); inside a group you can add/remove games, rename it, or delete it. A
 * gamepad on-screen keyboard names groups. A titled header with the sort order sits on top.
 */
class DeckAllSoftwarePage : public DeckPage {
    Q_OBJECT

public:
    DeckAllSoftwarePage(QAbstractItemModel* library, QWidget* parent = nullptr);
    ~DeckAllSoftwarePage() override;

    bool OnNavigate(Qt::Key key) override;
    bool OnAccept() override;          // A — open folder / new group / launch / (add-mode) toggle member
    bool OnBack() override;            // B — up a level, or leave to home
    bool OnPrimaryAction() override;   // X — root: sort; group: add games
    bool OnSecondaryAction() override; // Y — group: rename; add-mode: nothing
    bool OnStart() override;           // + — group: delete (confirm)
    std::vector<DeckHint> Hints() const override;
    void OnActivated() override;
    void ApplyTheme() override;

signals:
    void GamePlayRequested(QString path, u64 program_id);

protected:
    void resizeEvent(QResizeEvent* event) override; // keep the keyboard overlay covering the page

private:
    enum class View { Root, Group, AddGames };

    void SetView(View v);
    void RefreshGroups(); ///< rebuild the groups model + current-group proxies after a change
    void UpdateHeader();
    void CycleSort();
    void StartCreateGroup();
    void StartRenameGroup();
    void DeleteCurrentGroup();
    void ToggleCurrentMembership();
    int Columns() const;
    u64 CurrentProgramId() const;

    DeckGroups groups;

    QAbstractItemModel* base = nullptr;     ///< the shared library (games page filter)
    QSortFilterProxyModel* sorted = nullptr; ///< user sort over base (drives all game grids)
    class GroupsModel* groups_model = nullptr;
    QAbstractItemModel* root_model = nullptr;    ///< groups_model + sorted (the root grid)
    QSortFilterProxyModel* group_filter = nullptr; ///< sorted, filtered to the current group's members
    QIdentityProxyModel* member_proxy = nullptr;  ///< sorted + a member check for the add-games picker

    QLabel* title = nullptr;
    QLabel* sort_label = nullptr;
    QLabel* selected_name = nullptr;
    QListView* grid = nullptr;
    class DeckGameDelegate* delegate = nullptr;
    DeckKeyboard* keyboard = nullptr;
    QTimer* shimmer = nullptr;

    View view = View::Root;
    int current_group = -1;
    int phase = 0;
    int sort_mode = 0;
    bool renaming = false; ///< the keyboard is naming (true) vs creating (false)
    bool confirming_delete = false;
};
