// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStyledItemDelegate>
#include <Qt>

/// Marks the trailing round "All Software" cell in the game rail (shared by the page's rail model and
/// the delegate). A high UserRole the game-list model never returns, so real game rows read false.
inline constexpr int DeckAllSoftwareRole = Qt::UserRole + 777;
/// Group folder tile: value is the group index (>= 0). Real game rows return an invalid variant.
inline constexpr int DeckGroupRole = Qt::UserRole + 778;
/// The trailing "＋ New Group" tile (bool true).
inline constexpr int DeckNewGroupRole = Qt::UserRole + 779;
/// True when a game tile should show an "in this group" check (used in the add-to-group picker).
inline constexpr int DeckGroupMemberRole = Qt::UserRole + 780;

/**
 * Draws a game as a large box-art card with a rounded frame, a title beneath it, and a bright
 * focus ring when selected. Sized for the Deck grid; used by DeckGamesPage's QListView in
 * IconMode.
 */
class DeckGameDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit DeckGameDelegate(QObject* parent = nullptr);

    /// Animation phase (0-359) for the shimmering selection border; advanced by the page's timer.
    void SetPhase(int p) {
        phase = p;
    }

    /// Override the box-art tile size (the home rail uses the theme default; the All Software grid
    /// uses a smaller, denser tile). 0 = fall back to the theme constant.
    void SetCardSize(int w, int h) {
        card_w = w;
        card_h = h;
    }

    /// Extra empty space reserved at the LEFT of the very first tile (its cell is that much wider,
    /// the art sits in the right part). This is the "start after the avatar" indent — but because it
    /// lives in the scrollable content, it scrolls away, so tiles reach the screen's left edge as you
    /// scroll (unlike a fixed viewport padding, which clips there). 0 in the full "See all" grid.
    void SetLeadIndent(int px) {
        lead_indent = px;
    }

    /// Program id of the title currently suspended to the HOME menu (0 = none). Its tile gets a small
    /// "paused" badge so the user can see which game will resume.
    void SetSuspendedProgramId(quint64 id) {
        suspended_id = id;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    int phase = 0;
    int lead_indent = 0;
    quint64 suspended_id = 0;
    int card_w = 0; ///< 0 = use DeckTheme::kGridCardWidth
    int card_h = 0; ///< 0 = use DeckTheme::kGridCardHeight
};
