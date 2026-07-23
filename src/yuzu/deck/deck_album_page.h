// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include "yuzu/deck/deck_page.h"

class QLabel;
class QListView;
class QStandardItemModel;

/**
 * Console-mode "Album" — the Switch HOME Album: a grid of the screenshots Eden captured (from the
 * screenshots dir). Arrows move the selection, A opens the highlighted shot full-screen, B closes
 * the viewer (or leaves the Album from the grid).
 */
class DeckAlbumPage : public DeckPage {
    Q_OBJECT

public:
    explicit DeckAlbumPage(QWidget* parent = nullptr);
    ~DeckAlbumPage() override;

    bool OnNavigate(Qt::Key key) override;
    bool OnAccept() override;
    bool OnBack() override;
    std::vector<DeckHint> Hints() const override;
    void OnActivated() override;
    void ApplyTheme() override;

private:
    void Reload();
    void ShowViewer(bool on);
    int Columns() const;

    QLabel* title = nullptr;
    QLabel* placeholder = nullptr;
    QListView* grid = nullptr;
    QStandardItemModel* model = nullptr;
    QLabel* viewer = nullptr; ///< full-screen single-shot view shown on A
    bool viewing = false;
};
