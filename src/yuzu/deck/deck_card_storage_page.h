// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QWidget>

#include "yuzu/deck/deck_page.h"

class QLabel;
class QListView;
class QStandardItemModel;
class QThread;

/**
 * Console-mode "Card Storage": the compressed titles sitting in the game folders, shown as cards
 * that are not in a slot yet.
 *
 * A compressed dump is half the size of the one the emulator can read, which is worth having for a
 * library that outgrows the drive -- but nothing can run from it. Inserting a card unpacks it in
 * place, next to where it already lives, so the library keeps its one shape instead of gaining a
 * second place where titles hide.
 */
class DeckCardStoragePage : public DeckPage {
    Q_OBJECT

public:
    explicit DeckCardStoragePage(QWidget* parent = nullptr);
    ~DeckCardStoragePage() override;

    bool OnNavigate(Qt::Key key) override;
    bool OnAccept() override;
    bool OnBack() override;
    std::vector<DeckHint> Hints() const override;
    void OnActivated() override;
    void ApplyTheme() override;

signals:
    /// A card finished going in, so the library has a title it did not have a moment ago.
    void LibraryChanged();

private:
    void Reload();
    void InsertSelected();
    void OnFinished(bool ok, QString message);
    int Columns() const;
    void UpdateSummary();

    QLabel* title = nullptr;
    QLabel* summary = nullptr; ///< "N cards | X GB" at the top right
    QLabel* placeholder = nullptr;
    QLabel* status = nullptr; ///< progress and outcome, under the grid
    QListView* grid = nullptr;
    QStandardItemModel* model = nullptr;
    QThread* worker = nullptr; ///< the conversion, kept off the UI thread
    bool busy = false;
};
