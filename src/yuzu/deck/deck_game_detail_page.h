// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>
#include <QPixmap>
#include <QString>

#include "common/common_types.h"
#include "yuzu/deck/deck_page.h"

/**
 * The game detail screen, opened from the library (A on a tile). Shows the box art, title and
 * metadata, plus a vertical list of actions — Play, Favorite, Remove Update, Remove DLC and Delete
 * — that replace the desktop right-click menu. Fully gamepad/touch driven; Delete asks for an
 * in-place confirmation so nothing destructive happens on a single press.
 */
class DeckGameDetailPage : public DeckPage {
    Q_OBJECT

public:
    enum Action { Play, Favorite, RemoveUpdate, RemoveDLC, Delete, ActionCount };

    explicit DeckGameDetailPage(QWidget* parent = nullptr);

    /// Populate the page for a game before it is shown.
    void SetGame(const QString& path, u64 program_id, const QString& title, const QPixmap& art,
                 const QString& meta, bool favorited);

    bool OnNavigate(Qt::Key key) override;
    bool OnAccept() override;
    bool OnBack() override;
    std::vector<DeckHint> Hints() const override;
    void OnActivated() override;

signals:
    void PlayRequested(QString path, u64 program_id);
    void FavoriteToggled(u64 program_id);
    void RemoveUpdateRequested(u64 program_id);
    void RemoveDLCRequested(u64 program_id);
    void DeleteRequested(QString path, u64 program_id, QString title);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QRectF ActionRect(int i) const;
    void SetCurrent(int index);
    void Activate();

    QString path;
    u64 program_id = 0;
    QString title;
    QString meta;
    QPixmap art;
    bool favorited = false;

    int current = 0;
    bool confirming_delete = false;
};
