// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>
#include <vector>
#include <QPixmap>
#include <QString>

#include "common/common_types.h"
#include "yuzu/deck/deck_library_stats.h"
#include "yuzu/deck/deck_page.h"

namespace PlayTime {
class PlayTimeManager;
}

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
    void SetGame(const DeckGameInfo& info);

    /// Report the outcome of an action in the console's own style. The removal helpers the desktop
    /// UI uses pop their own message boxes, which land as stray little windows over a full-screen
    /// shell and cannot be answered with a controller — the shell says it itself instead.
    void ShowNotice(const QString& title, const QString& body);

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
    /// What is covering the page, if anything. Every destructive action asks first, in here, rather
    /// than through a desktop dialog.
    enum class Modal { None, DeleteGame, RemoveUpdate, RemoveDLC, Notice };

    QRectF ActionRect(int i) const;
    bool Compact() const;    ///< the page is short (TV overscan inset) — tighten the two lists
    qreal FactPitch() const; ///< row height of the fact table
    void SetCurrent(int index);
    void Activate();
    void ConfirmModal(); ///< A on a modal: carry out what it asked about, or dismiss a notice
    void DrawModal(QPainter& painter);
    /// The label/value table under the title: play time, last played, date added, size, format…
    std::vector<std::pair<QString, QString>> Facts() const;

    DeckGameInfo game;

    int current = 0;
    Modal modal = Modal::None;
    QString notice_title;
    QString notice_body;
};
