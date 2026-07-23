// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPixmap>
#include <QString>

#include "common/common_types.h"
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
                           QWidget* parent = nullptr);
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

signals:
    /// A on a game tile — boot it straight into the game (the standard console behaviour).
    void GamePlayRequested(QString path, u64 program_id);
    /// B on a game tile — open its options page (details, favorite, remove update/DLC, delete).
    void GameActivated(QString path, u64 program_id, QString title, QPixmap art, QString meta,
                       bool favorited);
    void FavoriteToggled(u64 program_id);
    void OpenControllers();
    void OpenUsers();
    void OpenSettings();
    void ExitRequested();

private:
    enum class Zone { Rail, Dock, Avatar };
    // Users is not here — the avatar opens the Users page.
    enum DockItem { DockControllers = 0, DockSettings = 1, DockPower = 2, DockCount = 3 };

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
    QSortFilterProxyModel* filter = nullptr; ///< Shows only real games (see LibraryFilter).
    QListView* rail = nullptr;
    class DeckGameDelegate* delegate = nullptr;
    QWidget* placeholder = nullptr;
    DockBar* dock = nullptr;
    QLabel* clock = nullptr;
    QLabel* battery = nullptr;
    class AvatarBadge* avatar = nullptr; ///< active user's profile picture (top-left, focusable)
    QLabel* game_title = nullptr; ///< the selected game's name, above the rail
    QTimer* clock_timer = nullptr;
    QTimer* shimmer_timer = nullptr; ///< advances the selection-shimmer animation
    int phase = 0;                   ///< shimmer animation phase (0-359)

    Zone zone = Zone::Rail;
    bool grid_mode = false; ///< "See all": rail reflowed into a full wrapping grid of every game
    bool launched = false; ///< A game boot was requested; blocks a double-launch until we return.
};
