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
class GroupsModel;
class AllSoftHeader;

/**
 * Console-mode "All Software" — the Switch HOME full-library screen. Two tabs, switched with L/R:
 * "Software" (the whole library grid) and "Groups" (folders). A folder opens to its games; inside a
 * group you can add/remove games, rename, or delete. A gamepad keyboard names groups. The header
 * carries the tabs, the filter/sort icons, and the current sort; the selected game's name floats in
 * a pill below its tile.
 */
class DeckAllSoftwarePage : public DeckPage {
    Q_OBJECT

public:
    DeckAllSoftwarePage(QAbstractItemModel* library, QWidget* parent = nullptr);
    ~DeckAllSoftwarePage() override;

    bool OnNavigate(Qt::Key key) override;
    bool OnAccept() override;
    bool OnBack() override;
    bool OnPrimaryAction() override;   // X — sort (Software) / add games (group)
    bool OnSecondaryAction() override; // Y — rename group
    bool OnStart() override;           // + — delete group (confirm)
    bool OnPageUp() override;           // L — Software tab
    bool OnPageDown() override;         // R — Groups tab
    std::vector<DeckHint> Hints() const override;
    void OnActivated() override;
    void ApplyTheme() override;

signals:
    void GamePlayRequested(QString path, u64 program_id);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class View { Software, Groups, GroupDetail, AddGames };

    void SetView(View v);
    void RefreshGroups();
    void UpdateHeader();
    void CycleSort();
    void StartCreateGroup();
    void StartRenameGroup();
    void DeleteCurrentGroup();
    void ToggleCurrentMembership();
    void PositionNamePill();
    int Columns() const;
    u64 CurrentProgramId() const;

    DeckGroups groups;

    QAbstractItemModel* base = nullptr;
    QSortFilterProxyModel* sorted = nullptr;
    GroupsModel* groups_model = nullptr;
    QSortFilterProxyModel* group_filter = nullptr;
    QIdentityProxyModel* member_proxy = nullptr;

    AllSoftHeader* header = nullptr;
    QLabel* name_pill = nullptr;
    QListView* grid = nullptr;
    class DeckGameDelegate* delegate = nullptr;
    DeckKeyboard* keyboard = nullptr;
    QTimer* shimmer = nullptr;

    View view = View::Software;
    int current_group = -1;
    int phase = 0;
    int sort_mode = 0;
    bool renaming = false;
    bool confirming_delete = false;
};
