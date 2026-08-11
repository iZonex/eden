// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>
#include <QFont>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QString>
#include <Qt>

class DeckLibraryStats;

/// Marks the trailing round "All Software" cell in the game rail (shared by the page's rail model and
/// the delegate). A high UserRole the game-list model never returns, so real game rows read false.
inline constexpr int DeckAllSoftwareRole = Qt::UserRole + 777;
/// Group folder tile: value is the group index (>= 0). Real game rows return an invalid variant.
inline constexpr int DeckGroupRole = Qt::UserRole + 778;
/// The trailing "＋ New Group" tile (bool true).
inline constexpr int DeckNewGroupRole = Qt::UserRole + 779;
/// True when a game tile should show an "in this group" check (used in the add-to-group picker).
inline constexpr int DeckGroupMemberRole = Qt::UserRole + 780;
/// An empty home-row slot (bool true): the pale vacant seats the console pads its top row out with
/// when the library is shorter than the screen. The cursor lands on them and they do nothing.
inline constexpr int DeckPlaceholderRole = Qt::UserRole + 781;

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

    /// How far the selection has settled onto the current tile, 0 (just arrived) to 1 (fully
    /// focused). The tile grows, its white matte widens and its ring fades in along this, so moving
    /// the cursor is a movement rather than a jump between two static states.
    void SetFocusProgress(qreal t) {
        focus_t = t;
    }

    /// How far the press on the current cell has played out, 0 (at rest) to 1 (done). Each kind of
    /// cell answers it differently: box art dips and rebounds, the round Show More button paints its
    /// four squares in one after another.
    void SetPressProgress(qreal t) {
        press_t = t;
    }

    /// Whether the rail is the focused zone. When false (focus is on the dock or avatar) the selected
    /// tile drops its selection entirely, exactly as the console does — no ghost marker of where the
    /// cursor will come back to, because a second highlight on screen reads as two live cursors.
    void SetRailActive(bool active) {
        rail_active = active;
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

    /// Program id of the title currently suspended to the HOME menu (0 = none). Its tile wears the
    /// console's "Playing" pill — the active user's avatar beside the word — so it is obvious which
    /// game is still in memory and will resume rather than boot.
    void SetPlayingProgramId(quint64 id) {
        playing_id = id;
    }

    /// The round avatar shown inside that "Playing" pill (the user who is playing).
    void SetPlayingAvatar(QPixmap avatar) {
        playing_avatar = std::move(avatar);
    }

    /// Library history, so a title added recently and never opened can wear a "NEW" badge — the one
    /// piece of metadata the console does put on a tile.
    void SetStats(const DeckLibraryStats* s) {
        stats = s;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    /// Geometry and text of the trailing round "Show More" button, shared by paint and sizeHint so
    /// the cell it is measured into is the cell it is drawn in.
    int RoundButtonDiameter() const;
    static QFont CaptionFont(const QFont& base);
    static int CaptionWidth(const QFont& base);
    static QString Caption();

    int phase = 0;
    qreal focus_t = 1.0; ///< 0..1 settle of the selection onto the current tile
    qreal press_t = 0.0; ///< 0..1 run of the press on the current cell; 0 = at rest
    bool rail_active = true; ///< false → draw no selection at all (focus is on the dock/avatar)
    int lead_indent = 0;
    quint64 playing_id = 0;
    QPixmap playing_avatar;
    const DeckLibraryStats* stats = nullptr;
    int card_w = 0; ///< 0 = use DeckTheme::kGridCardWidth
    int card_h = 0; ///< 0 = use DeckTheme::kGridCardHeight
};
