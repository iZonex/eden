// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QPixmap>
#include <QString>

#include "common/common_types.h"
#include "common/uuid.h"
#include "yuzu/deck/deck_library_stats.h"
#include "yuzu/deck/deck_page.h"

namespace Core {
class System;
}
namespace PlayTime {
class PlayTimeManager;
}
class GameListModel;
class DockBar;
class QListView;
class QLabel;
class QTimer;
class QModelIndex;
class QSortFilterProxyModel;
class QAbstractItemModel;
class QAbstractListModel;

/**
 * The Switch-home screen: a top status strip (avatar + clock), a horizontal rail of game box art
 * (the selected tile framed), and a bottom dock of system icons (Controllers, Settings, Power).
 *
 * Two focus zones: the game rail and the dock. Up/Down move between them; Left/Right move within.
 * A launches the selected game (or opens the selected dock item); B opens the game's options page.
 */
class DeckGamesPage : public DeckPage {
    Q_OBJECT

public:
    explicit DeckGamesPage(GameListModel* model, Core::System& system,
                           const PlayTime::PlayTimeManager& play_time_manager,
                           DeckLibraryStats& stats, QWidget* parent = nullptr);
    ~DeckGamesPage() override;

    bool OnNavigate(Qt::Key key) override;
    bool OnAccept() override;
    bool OnPrimaryAction() override;   // X — toggle the "See all" full-library grid
    bool OnSecondaryAction() override; // Y — favorite
    bool OnStart() override;           // + — open the selected game's options
    bool OnBack() override;
    std::vector<DeckHint> Hints() const override;
    void OnActivated() override;
    void ApplyTheme() override;

    bool IsEmpty() const;

    /// The full, recency-sorted library (every game with box art) — shared with the All Software page.
    QAbstractItemModel* LibraryModel() const;

    /// Mark the title currently suspended to the HOME menu (0 = none) so its tile shows a paused badge.
    void SetSuspendedGame(u64 program_id);

    /// Re-run the ordering. The sort key (recent activity) lives outside the model, so a launch is
    /// invisible to the proxy's own change tracking — the shell calls this on every re-entry.
    /// Deferred to the event loop, so it is safe to call from inside a model signal.
    void Resort();

signals:
    /// A on a game tile — boot it straight into the game (the standard console behaviour).
    void GamePlayRequested(QString path, u64 program_id);
    /// B or + on a game tile — open its options page (details, favorite, remove update/DLC, delete).
    void GameActivated(DeckGameInfo info);
    void FavoriteToggled(u64 program_id);
    void OpenControllers();
    void OpenUsers(Common::UUID focus); ///< open My Page focused on the chosen user (invalid = active)
    void OpenSettings();
    void OpenAlbum();       ///< HOME dock Album — the screenshots gallery
    void OpenAllSoftware(); ///< the rail-end All Software button — the full library grid page
    void SleepRequested();  ///< HOME dock Sleep — put the Deck to sleep
    void ExitRequested();

private:
    enum class Zone { Rail, Dock, Avatar };
    // Users is not here — the avatar opens the Users page.
    enum DockItem {
        DockAlbum = 0,
        DockControllers = 1,
        DockSettings = 2,
        DockSleep = 3,
        DockPower = 4,
        DockCount = 5
    };

    void ResortNow(); ///< the body of Resort(), run once the event loop comes back round
    void SetZone(Zone zone);
    void MoveRail(int delta);
    void SetGridMode(bool on); ///< toggle the rail between a single row and a full wrapping grid
    int GridColumns() const;   ///< tiles per row in grid mode (for Up/Down)
    void UpdateGameTitle();    ///< refresh the selected game's name label above the rail
    void EmitCurrentGame();     ///< open the options page (B)
    void PlayCurrentGame();     ///< boot the game directly (A)
    void ActivateDock();
    QModelIndex CurrentGameIndex() const;

    Core::System& system;
    GameListModel* model = nullptr;
    DeckLibraryStats& stats; ///< first-seen / last-played history — the rail's ordering key
    QSortFilterProxyModel* filter = nullptr; ///< Shows only real games (see LibraryFilter).
    QSortFilterProxyModel* head = nullptr; ///< caps the home rail to the recent N (uncapped in the grid)
    QAbstractItemModel* rail_model = nullptr; ///< head + trailing "All Software" tile; the rail's model
    QAbstractListModel* all_software = nullptr; ///< the trailing tile's model (hidden in grid mode)
    QListView* rail = nullptr;
    class DeckGameDelegate* delegate = nullptr;
    QWidget* placeholder = nullptr;
    DockBar* dock = nullptr;
    class StatusCluster* status = nullptr; ///< time + wifi + battery graphic (top-right)
    class AvatarBadge* avatar = nullptr; ///< the active user's profile picture + name (top-left)
    Common::UUID active_uuid{}; ///< the active (last-opened) user — A on the avatar opens their page
    QLabel* game_title = nullptr; ///< the selected game's name, above the rail
    QTimer* clock_timer = nullptr;
    QTimer* shimmer_timer = nullptr; ///< advances the selection-shimmer animation
    int phase = 0;                   ///< shimmer animation phase (0-359)

    Zone zone = Zone::Rail;
    bool grid_mode = false; ///< "See all": rail reflowed into a full wrapping grid of every game
    bool launched = false; ///< A game boot was requested; blocks a double-launch until we return.
    bool initial_focus_pending = false; ///< snap focus to the rail once games finish loading
    bool resort_queued = false; ///< a deferred Resort() is already on the event loop
};
