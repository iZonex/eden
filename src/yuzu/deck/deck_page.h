// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>
#include <QWidget>
#include <Qt>

#include "yuzu/deck/deck_hint_bar.h"

/**
 * Base class for a full-screen page inside the Big Picture shell. The shell forwards controller
 * intents to the active page through these handlers and reads Hints() to populate the bottom bar.
 *
 * Handlers that return true consume the intent; returning false lets the shell apply its default
 * (e.g. Back() on the root page asks the shell to leave Big Picture, TabPrev/TabNext switch tabs).
 */
class DeckPage : public QWidget {
    Q_OBJECT

public:
    explicit DeckPage(QWidget* parent = nullptr) : QWidget(parent) {}
    ~DeckPage() override = default;

    /// Directional intent (Qt arrow key). Default: unhandled, shell may act on it.
    virtual bool OnNavigate(Qt::Key /*key*/) {
        return false;
    }
    virtual bool OnAccept() {
        return false;
    }
    virtual bool OnBack() {
        return false;
    }
    virtual bool OnPrimaryAction() {
        return false;
    }
    virtual bool OnSecondaryAction() {
        return false;
    }
    virtual bool OnPageUp() {
        return false;
    }
    virtual bool OnPageDown() {
        return false;
    }
    virtual bool OnStart() {
        return false;
    }
    virtual bool OnSelect() {
        return false;
    }

    /// Button hints for this page, shown in the bottom bar. Called whenever the page becomes
    /// active or emits HintsChanged().
    virtual std::vector<DeckHint> Hints() const {
        return {};
    }

    /// Called by the shell right after this page becomes the visible one, so it can take focus.
    virtual void OnActivated() {}

    /// Called by the shell when the console theme changes, so the page can re-apply any stylesheets
    /// that baked in colour values (custom-painted widgets read DeckTheme live and just repaint).
    virtual void ApplyTheme() {}

signals:
    /// Ask the shell to refresh the hint bar from Hints().
    void HintsChanged();
};
