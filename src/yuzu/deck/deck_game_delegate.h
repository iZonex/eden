// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStyledItemDelegate>

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

    /// Extra empty space reserved at the LEFT of the very first tile (its cell is that much wider,
    /// the art sits in the right part). This is the "start after the avatar" indent — but because it
    /// lives in the scrollable content, it scrolls away, so tiles reach the screen's left edge as you
    /// scroll (unlike a fixed viewport padding, which clips there). 0 in the full "See all" grid.
    void SetLeadIndent(int px) {
        lead_indent = px;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    int phase = 0;
    int lead_indent = 0;
};
