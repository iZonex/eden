// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

#include "common/common_types.h"
#include "yuzu/deck/deck_groups.h"
#include "yuzu/deck/deck_library_order.h"
#include "yuzu/deck/deck_library_stats.h"
#include "yuzu/deck/deck_page.h"

namespace PlayTime {
class PlayTimeManager;
}

class QAbstractItemModel;
class QSortFilterProxyModel;
class QIdentityProxyModel;
class QListView;
class QTimer;
class QResizeEvent;
class QPaintEvent;
class DeckKeyboard;
class DeckOptionMenu;
class GroupsModel;
class AllSoftHeader;
class NamePill;
class LibraryViewProxy;

/**
 * Console-mode "All Software" — the Switch HOME full-library screen. Two tabs, switched with L/R:
 * "Software" (the whole library grid) and "Groups" (folders). A folder opens to its games; inside a
 * group you can add/remove games, rename, or delete. A gamepad keyboard names groups.
 *
 * The filter and sort icons sit in a column to the left of the grid and are focusable, like the
 * console: steer left off the first tile to reach them and press A to open the matching menu. The
 * header carries the tabs and the sort in effect; the selected game's name floats in a bubble under
 * its tile, and + opens that game's options page.
 */
class DeckAllSoftwarePage : public DeckPage {
    Q_OBJECT

public:
    DeckAllSoftwarePage(QAbstractItemModel* library, DeckLibraryStats& stats,
                        const PlayTime::PlayTimeManager& play_time, QWidget* parent = nullptr);
    ~DeckAllSoftwarePage() override;

    bool OnNavigate(Qt::Key key) override;
    bool OnAccept() override;
    bool OnBack() override;
    bool OnPrimaryAction() override;   // X — add games (group)
    bool OnSecondaryAction() override; // Y — rename group
    bool OnStart() override;           // + — the selected game's options
    bool OnSelect() override;          // - — delete group (confirm)
    bool OnPageUp() override;          // L — Software tab
    bool OnPageDown() override;        // R — Groups tab
    std::vector<DeckHint> Hints() const override;
    void OnActivated() override;
    void ApplyTheme() override;

    /// Re-run the ordering (the sort key lives outside the model, so a launch is invisible to it)
    /// and put the cursor back on the same game.
    void Resort();

    /// Ask the next OnActivated to restore this game and view instead of resetting to the top —
    /// used when coming back from the game options page.
    void RestoreOnReturn();

signals:
    void GamePlayRequested(QString path, u64 program_id);
    /// + on a game tile — open its options page (details, favorite, remove update/DLC, delete).
    void GameOptionsRequested(DeckGameInfo info);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override; // filter/sort icons in the left column

private:
    enum class View { Software, Groups, GroupDetail, AddGames };
    /// Where the cursor is: on the grid, or on the filter/sort icons beside it.
    enum class Zone { Grid, Gutter };
    enum GutterItem { GutterFilter = 0, GutterSort = 1, GutterCount = 2 };

    void SetView(View v);
    void SetZone(Zone z);
    void RefreshGroups();
    void UpdateHeader();
    void OpenSortMenu();
    void OpenFilterMenu();
    void ApplyOrdering();  ///< push the current key/filter into the proxies and re-sort
    void LoadPreferences(); ///< the sort key and filter last used, from the console's settings
    void SavePreferences() const;
    void StartCreateGroup();
    void StartRenameGroup();
    void DeleteCurrentGroup();
    void ToggleCurrentMembership();
    void PositionNamePill();
    void SelectProgram(u64 program_id); ///< put the cursor on a game, or on the first row
    int Columns() const;
    u64 CurrentProgramId() const;
    bool MenuOpen() const;

    DeckGroups groups;
    DeckLibraryStats& stats;
    const PlayTime::PlayTimeManager& play_time;

    QAbstractItemModel* base = nullptr;
    LibraryViewProxy* sorted = nullptr;      ///< the Software tab: sort key + user filter
    GroupsModel* groups_model = nullptr;
    LibraryViewProxy* group_filter = nullptr; ///< one group's games, in the same sort order
    QIdentityProxyModel* member_proxy = nullptr;

    AllSoftHeader* header = nullptr;
    NamePill* name_pill = nullptr;
    QListView* grid = nullptr;
    class DeckGameDelegate* delegate = nullptr;
    DeckKeyboard* keyboard = nullptr;
    DeckOptionMenu* menu = nullptr;
    QTimer* shimmer = nullptr;

    DeckSortKey sort_key = DeckSortKey::Recent;
    DeckFilterState filter_state;
    // The group and format lists the open filter menu's row ids refer to. Captured when the menu
    // opens so a background scan finishing mid-menu cannot renumber the rows under the cursor.
    QStringList menu_groups;
    QStringList menu_types;

    View view = View::Software;
    Zone zone = Zone::Grid;
    int gutter_item = GutterFilter;
    int current_group = -1;
    int phase = 0;
    bool renaming = false;
    bool confirming_delete = false;
    bool sort_menu_open = false; ///< which of the two menus the open panel belongs to
    u64 restore_pid = 0;         ///< game to re-select on the next activation (0 = start at the top)
    View restore_view = View::Software;
    int restore_group = -1;
    bool restore_pending = false;
};
