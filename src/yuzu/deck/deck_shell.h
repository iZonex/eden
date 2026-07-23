// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QWidget>

#include "common/common_types.h"
#include "core/file_sys/vfs/vfs_types.h"

namespace Core {
class System;
}
namespace FileSys {
class ManualContentProvider;
}
namespace PlayTime {
class PlayTimeManager;
}
namespace InputCommon {
class InputSubsystem;
}

class DeckNavigator;
class DeckHintBar;
class DeckPage;
class DeckGamesPage;
class DeckGameDetailPage;
class DeckControllersPage;
class DeckSettingsPage;
class DeckUsersPage;
class DeckAlbumPage;
class GameListModel;
class QStackedWidget;

/**
 * Root of the Big Picture / Steam Deck front-end: a full-screen, controller- and touch-driven shell
 * that replaces the desktop menus while active, styled after the Nintendo Switch.
 *
 * The home screen is the game rail + system dock (DeckGamesPage). Selecting a dock item pushes the
 * Controllers or Settings page over it; B returns home, and B on home leaves Big Picture. A
 * DeckNavigator polls every connected controller and the shell routes intents to the visible page.
 */
class DeckShell : public QWidget {
    Q_OBJECT

public:
    explicit DeckShell(FileSys::VirtualFilesystem vfs, FileSys::ManualContentProvider* provider,
                       PlayTime::PlayTimeManager& play_time_manager, Core::System& system,
                       InputCommon::InputSubsystem& input_subsystem, QWidget* parent = nullptr);
    ~DeckShell() override;

    void Activate();
    void Deactivate();

    /// Forward the HOME-suspended title (paused in memory, 0 = none) to the home page's tile badge.
    void SetSuspendedGame(u64 program_id);

    GameListModel* Model() const {
        return model;
    }

protected:
    // Keyboard fallback (desktop testing / accessibility): routes arrows/Enter/Esc to the same
    // handlers the controller uses, so the shell is fully navigable without a gamepad.
    void keyPressEvent(QKeyEvent* event) override;
    // Keeps the UI inside a TV-safe area when docked to a large display (TVs overscan-crop the edges
    // of a full-screen image); the Deck's own screen doesn't overscan, so it gets no inset.
    void resizeEvent(QResizeEvent* event) override;

signals:
    void GameChosen(QString path, u64 program_id);
    void ExitRequested();
    void SaveConfigRequested();
    void RemoveUpdateRequested(u64 program_id);
    void RemoveDLCRequested(u64 program_id);
    void DeleteGameRequested(QString path, u64 program_id, QString title);

private:
    void ShowPage(QWidget* page);
    void GoHome();
    void UpdateHints();
    void ConnectNavigator();
    void ApplyTheme(bool light); ///< swap Basic White/Black, restyle the whole tree, and persist.

    void HandleNavigate(int key);
    void HandleAccept();
    void HandleBack();
    void HandlePrimary();
    void HandleSecondary();
    void HandlePageUp();
    void HandlePageDown();
    void HandleStart();

    DeckPage* CurrentPage() const;

    Core::System& system;

    GameListModel* model = nullptr;
    bool populated = false;

    DeckNavigator* navigator = nullptr;
    QStackedWidget* stack = nullptr;
    DeckHintBar* hint_bar = nullptr;
    class QVBoxLayout* root_layout = nullptr; ///< root layout, inset for the TV safe area on resize

    DeckGamesPage* games_page = nullptr;
    DeckGameDetailPage* detail_page = nullptr;
    DeckControllersPage* controllers_page = nullptr;
    DeckSettingsPage* settings_page = nullptr;
    DeckUsersPage* users_page = nullptr;
    DeckAlbumPage* album_page = nullptr;
};
