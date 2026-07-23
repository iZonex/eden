// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QKeyEvent>
#include <QResizeEvent>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "common/settings.h"
#include "core/core.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/model.h"
#include "yuzu/deck/deck_controllers_page.h"
#include "yuzu/deck/deck_game_detail_page.h"
#include "yuzu/deck/deck_games_page.h"
#include "yuzu/deck/deck_hint_bar.h"
#include "yuzu/deck/deck_navigator.h"
#include "yuzu/deck/deck_settings_page.h"
#include "yuzu/deck/deck_shell.h"
#include "yuzu/deck/deck_theme.h"
#include "yuzu/deck/deck_users_page.h"

namespace {
/// Force every widget in a subtree to actually paint the dark palette background.
///
/// This is the belt-and-suspenders that kills the "white patches" for good. Qt does NOT fill a
/// widget's palette Window/Base colour on its own — a plain QWidget only looks dark because a
/// stylesheet gives it a background, and a widget with a custom paintEvent won't draw the
/// stylesheet background at all unless it fills it by hand. Any widget that slips through either
/// net renders with an uninitialised (white) ground. Turning on autoFillBackground makes Qt paint
/// the (dark) palette colour before every paintEvent, so nothing can leak white regardless of
/// stylesheet quirks or a custom painter that forgot to fill.
void ForceDarkBackground(QWidget* root) {
    root->setAutoFillBackground(true);
    for (QWidget* child : root->findChildren<QWidget*>()) {
        child->setAutoFillBackground(true);
    }
}
} // namespace

DeckShell::DeckShell(FileSys::VirtualFilesystem vfs, FileSys::ManualContentProvider* provider,
                     PlayTime::PlayTimeManager& play_time_manager, Core::System& system_,
                     InputCommon::InputSubsystem& input_subsystem, QWidget* parent)
    : QWidget(parent), system{system_} {
    setObjectName(QStringLiteral("DeckShell"));
    // Apply the saved theme (default: Basic White — the Switch 2 default) BEFORE building the palette
    // and stylesheet, so every widget bakes the right colours from the start.
    {
        QSettings settings(QStringLiteral("Eden"), QStringLiteral("deck"));
        DeckTheme::SetLightMode(settings.value(QStringLiteral("light_theme"), true).toBool());
    }
    setPalette(DeckTheme::Palette());
    setStyleSheet(DeckTheme::StyleSheet());
    setFocusPolicy(Qt::StrongFocus);

    model = new GameListModel(std::move(vfs), provider, play_time_manager, system, this);
    model->SetFlat(true);
    connect(model, &GameListModel::SaveConfig, this, &DeckShell::SaveConfigRequested);
    connect(model, &GameListModel::PopulatingCompleted, this, [this](const QStringList&) {
        if (stack->currentWidget() == games_page) {
            games_page->OnActivated();
        }
    });

    root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    stack = new QStackedWidget(this);
    games_page = new DeckGamesPage(model, system, play_time_manager, stack);
    detail_page = new DeckGameDetailPage(stack);
    controllers_page = new DeckControllersPage(system.HIDCore(), input_subsystem, stack);
    settings_page = new DeckSettingsPage(system, stack);
    users_page = new DeckUsersPage(system, stack);
    stack->addWidget(games_page);
    stack->addWidget(detail_page);
    stack->addWidget(controllers_page);
    stack->addWidget(settings_page);
    stack->addWidget(users_page);
    root_layout->addWidget(stack, 1);

    hint_bar = new DeckHintBar(system.HIDCore(), this);
    root_layout->addWidget(hint_bar);

    connect(games_page, &DeckGamesPage::GamePlayRequested, this,
            [this](QString path, u64 program_id) {
                settings_page->Apply();
                emit GameChosen(std::move(path), program_id);
            });
    connect(games_page, &DeckGamesPage::GameActivated, this,
            [this](QString path, u64 program_id, QString title, QPixmap art, QString meta, bool fav) {
                detail_page->SetGame(path, program_id, title, art, meta, fav);
                ShowPage(detail_page);
            });
    connect(games_page, &DeckGamesPage::FavoriteToggled, this, [this](u64 program_id) {
        model->ToggleFavorite(program_id);
        emit SaveConfigRequested();
    });

    connect(detail_page, &DeckGameDetailPage::PlayRequested, this,
            [this](QString path, u64 program_id) {
                settings_page->Apply();
                emit GameChosen(std::move(path), program_id);
            });
    connect(detail_page, &DeckGameDetailPage::FavoriteToggled, this, [this](u64 program_id) {
        model->ToggleFavorite(program_id);
        emit SaveConfigRequested();
    });
    connect(detail_page, &DeckGameDetailPage::RemoveUpdateRequested, this,
            &DeckShell::RemoveUpdateRequested);
    connect(detail_page, &DeckGameDetailPage::RemoveDLCRequested, this,
            &DeckShell::RemoveDLCRequested);
    connect(detail_page, &DeckGameDetailPage::DeleteRequested, this,
            [this](QString path, u64 program_id, QString title) {
                emit DeleteGameRequested(std::move(path), program_id, std::move(title));
                model->RefreshGameDirectory(); // the game is gone — re-scan and return home
                GoHome();
            });
    connect(detail_page, &DeckPage::HintsChanged, this, &DeckShell::UpdateHints);
    connect(games_page, &DeckGamesPage::OpenControllers, this,
            [this] { ShowPage(controllers_page); });
    connect(games_page, &DeckGamesPage::OpenUsers, this, [this](Common::UUID focus) {
        users_page->FocusUser(focus);
        ShowPage(users_page);
    });
    connect(games_page, &DeckGamesPage::OpenSettings, this, [this] { ShowPage(settings_page); });
    connect(games_page, &DeckGamesPage::ExitRequested, this, &DeckShell::ExitRequested);

    connect(settings_page, &DeckSettingsPage::SaveConfigRequested, this,
            &DeckShell::SaveConfigRequested);
    connect(settings_page, &DeckSettingsPage::ThemeChangeRequested, this, &DeckShell::ApplyTheme);
    connect(users_page, &DeckUsersPage::SaveConfigRequested, this,
            &DeckShell::SaveConfigRequested);
    connect(users_page, &DeckPage::HintsChanged, this, &DeckShell::UpdateHints);

    connect(controllers_page, &DeckControllersPage::SaveConfigRequested, this,
            &DeckShell::SaveConfigRequested);

    connect(games_page, &DeckPage::HintsChanged, this, &DeckShell::UpdateHints);
    connect(controllers_page, &DeckPage::HintsChanged, this, &DeckShell::UpdateHints);
    connect(settings_page, &DeckPage::HintsChanged, this, &DeckShell::UpdateHints);

    navigator = new DeckNavigator(system.HIDCore(), this);
    ConnectNavigator();

    stack->setCurrentWidget(games_page);

    // Guarantee a dark ground under every widget now that the whole tree exists (see helper).
    ForceDarkBackground(this);
}

DeckShell::~DeckShell() = default;

void DeckShell::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // TVs overscan-crop the edges of a full-screen image, so on a large (docked-TV) display keep the
    // whole UI inside a safe area. The Deck's own 1280-wide panel doesn't overscan → no inset there.
    const int w = width();
    const int h = height();
    const bool tv = w > 1600; // the Deck panel is 1280 wide; a docked TV is far wider
    const int mx = tv ? w * 35 / 1000 : 0; // ~3.5% safe area on a TV
    const int my = tv ? h * 35 / 1000 : 0;
    if (root_layout != nullptr) {
        root_layout->setContentsMargins(mx, my, mx, my);
    }
}

DeckPage* DeckShell::CurrentPage() const {
    return qobject_cast<DeckPage*>(stack->currentWidget());
}

void DeckShell::ShowPage(QWidget* page) {
    // Tidy up the page we are leaving.
    if (stack->currentWidget() == settings_page && page != settings_page) {
        settings_page->Apply();
    }
    if (stack->currentWidget() == controllers_page && page != controllers_page) {
        controllers_page->OnDeactivated();
    }

    stack->setCurrentWidget(page);
    if (auto* deck_page = CurrentPage()) {
        deck_page->OnActivated();
    }
    UpdateHints();
}

void DeckShell::GoHome() {
    ShowPage(games_page);
}

void DeckShell::UpdateHints() {
    if (auto* page = CurrentPage()) {
        hint_bar->SetHints(page->Hints());
        // SetHints rebuilds cells; keep them on the dark ground too.
        ForceDarkBackground(hint_bar);
    }
}

void DeckShell::ApplyTheme(bool light) {
    if (DeckTheme::IsLightMode() == light) {
        return;
    }
    DeckTheme::SetLightMode(light);
    QSettings settings(QStringLiteral("Eden"), QStringLiteral("deck"));
    settings.setValue(QStringLiteral("light_theme"), light);
    // Re-apply the palette + stylesheet (they bake the colour values) and re-assert the dark/light
    // ground under every widget, then repaint the whole tree. Custom-painted widgets read the colour
    // globals live, so they flip immediately; a few labels with baked-in colour stylesheets settle
    // fully on the next entry into Big Picture.
    setPalette(DeckTheme::Palette());
    setStyleSheet(DeckTheme::StyleSheet());
    ForceDarkBackground(this);
    // Let each page re-apply its baked-colour stylesheets (labels, sidebar), then repaint the tree.
    for (int i = 0; i < stack->count(); ++i) {
        if (auto* page = qobject_cast<DeckPage*>(stack->widget(i))) {
            page->ApplyTheme();
        }
    }
    for (QWidget* w : findChildren<QWidget*>()) {
        w->update();
    }
    update();
}

void DeckShell::Activate() {
    if (!populated) {
        populated = true;
        // Larger box art than the desktop default, applied only when the console is actually used.
        if (UISettings::values.game_icon_size.GetValue() < 256) {
            UISettings::values.game_icon_size.SetValue(256);
        }
        if (!UISettings::values.game_dirs.empty()) {
            model->PopulateAsync(UISettings::values.game_dirs);
        }
    }
    if (auto* page = CurrentPage()) {
        page->OnActivated();
    }
    UpdateHints();
    navigator->SetActive(true);
    setFocus();
}

void DeckShell::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
        HandleNavigate(event->key());
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        HandleAccept();
        break;
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        HandleBack();
        break;
    case Qt::Key_PageUp:
        HandlePageUp();
        break;
    case Qt::Key_PageDown:
        HandlePageDown();
        break;
    case Qt::Key_F:
        HandleSecondary(); // favorite
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void DeckShell::Deactivate() {
    navigator->SetActive(false);
    controllers_page->OnDeactivated();
    if (stack->currentWidget() == settings_page) {
        settings_page->Apply();
    }
}

void DeckShell::ConnectNavigator() {
    connect(navigator, &DeckNavigator::Navigate, this,
            [this](Qt::Key key) { HandleNavigate(key); });
    connect(navigator, &DeckNavigator::Accept, this, &DeckShell::HandleAccept);
    connect(navigator, &DeckNavigator::Back, this, &DeckShell::HandleBack);
    connect(navigator, &DeckNavigator::PrimaryAction, this, &DeckShell::HandlePrimary);
    connect(navigator, &DeckNavigator::SecondaryAction, this, &DeckShell::HandleSecondary);
    connect(navigator, &DeckNavigator::PageUp, this, &DeckShell::HandlePageUp);
    connect(navigator, &DeckNavigator::PageDown, this, &DeckShell::HandlePageDown);
    connect(navigator, &DeckNavigator::StartPressed, this, &DeckShell::HandleStart);
}

void DeckShell::HandleStart() {
    if (auto* page = CurrentPage()) {
        page->OnStart();
    }
}

void DeckShell::HandleNavigate(int key) {
    if (auto* page = CurrentPage()) {
        page->OnNavigate(static_cast<Qt::Key>(key));
    }
}

void DeckShell::HandleAccept() {
    if (auto* page = CurrentPage()) {
        page->OnAccept();
    }
}

void DeckShell::HandleBack() {
    if (auto* page = CurrentPage(); page && page->OnBack()) {
        return;
    }
    // Not consumed by the page: from Controllers/Settings, return to the home screen.
    if (stack->currentWidget() != games_page) {
        GoHome();
    }
}

void DeckShell::HandlePrimary() {
    if (auto* page = CurrentPage()) {
        page->OnPrimaryAction();
    }
}

void DeckShell::HandleSecondary() {
    if (auto* page = CurrentPage()) {
        page->OnSecondaryAction();
    }
}

void DeckShell::HandlePageUp() {
    if (auto* page = CurrentPage()) {
        page->OnPageUp();
    }
}

void DeckShell::HandlePageDown() {
    if (auto* page = CurrentPage()) {
        page->OnPageDown();
    }
}
