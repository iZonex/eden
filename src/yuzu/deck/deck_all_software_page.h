// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include "common/common_types.h"
#include "yuzu/deck/deck_page.h"

class QAbstractItemModel;
class QLabel;
class QListView;
class QTimer;

/**
 * Console-mode "All Software" — the Switch HOME "see all your software" screen: a distinct full-page
 * grid of the whole library (not the home rail reflowed). A titled header with a sort control sits on
 * top; below it, a dense scrolling grid of box art. Arrows move the selection, A launches, B returns
 * home, R cycles the sort order.
 */
class DeckAllSoftwarePage : public DeckPage {
    Q_OBJECT

public:
    DeckAllSoftwarePage(QAbstractItemModel* library, QWidget* parent = nullptr);
    ~DeckAllSoftwarePage() override;

    bool OnNavigate(Qt::Key key) override;
    bool OnAccept() override;          // A — launch the highlighted game
    bool OnBack() override;            // B — back to the home menu
    bool OnPrimaryAction() override;   // X — cycle sort order (also shown as R)
    bool OnSecondaryAction() override; // Y — cycle sort order
    std::vector<DeckHint> Hints() const override;
    void OnActivated() override;
    void ApplyTheme() override;

signals:
    void GamePlayRequested(QString path, u64 program_id);

private:
    void UpdateHeader();
    void CycleSort();
    int Columns() const;

    QAbstractItemModel* library = nullptr;
    QLabel* title = nullptr;
    QLabel* sort_label = nullptr;
    QLabel* selected_name = nullptr;
    QListView* grid = nullptr;
    class DeckGameDelegate* delegate = nullptr;
    QTimer* shimmer = nullptr;
    int phase = 0;
    int sort_mode = 0;
};
