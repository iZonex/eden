// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <QConicalGradient>
#include <QPainter>
#include <QPainterPath>

#include "qt_common/game_list/game_list_p.h"
#include "yuzu/deck/deck_game_delegate.h"
#include "yuzu/deck/deck_theme.h"

DeckGameDelegate::DeckGameDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize DeckGameDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const {
    // The cell is the box art plus a uniform margin on every side; that margin is the gap between
    // tiles AND the room the selection border needs, so spacing stays consistent everywhere. The
    // very first tile's cell is wider by lead_indent (empty space on its left) so the list starts
    // indented but still scrolls out to the screen edge.
    const int lead = index.row() == 0 ? lead_indent : 0;
    return {DeckTheme::kGridCardWidth + 2 * DeckTheme::kGridCardMargin + lead,
            DeckTheme::kGridCardHeight + 2 * DeckTheme::kGridCardMargin};
}

void DeckGameDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const int radius = 18;

    // The focused tile is drawn larger and "floats" (soft shadow + glow); others sit smaller in the
    // same cell. The title anchors at a fixed y so labels line up.
    // The art is the cell inset by a uniform margin; the focused tile insets less, so it grows a
    // little (and its border lives in the surrounding margin). Kept square and centered so tiles line
    // up perfectly with even gaps — no stray inner border or lopsided padding.
    QRect cell = option.rect;
    // Reserve the leading indent on the first tile (empty space on its left; the art sits to the
    // right of it), so the list is indented at rest but scrolls all the way to the screen edge.
    if (index.row() == 0) {
        cell.setLeft(cell.left() + lead_indent);
    }
    // The focused tile insets by 8 (never less) so its selection ring stays fully inside the cell —
    // otherwise the ring is clipped at the top/edges. Non-focused tiles inset by the full margin.
    const int inset = selected ? 8 : DeckTheme::kGridCardMargin;
    QRect box = cell.adjusted(inset, inset, -inset, -inset);
    const int side = std::min(box.width(), box.height());
    QRect art_rect(box.center().x() - side / 2, box.center().y() - side / 2, side, side);

    if (selected) {
        // A soft drop shadow so the focused tile lifts off the page (the Switch's subtle highlight),
        // rather than a heavy coloured glow.
        for (int s = 10; s >= 1; --s) {
            QPainterPath sh;
            sh.addRoundedRect(art_rect.adjusted(-s, -s + 2, s, s + 4), radius + s, radius + s);
            painter->fillPath(sh, QColor(0, 0, 0, 5));
        }
    }

    QPainterPath clip;
    clip.addRoundedRect(art_rect, radius, radius);
    painter->save();
    painter->setClipPath(clip);

    const QPixmap pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
    if (!pixmap.isNull()) {
        const QPixmap scaled = pixmap.scaled(art_rect.size(), Qt::KeepAspectRatioByExpanding,
                                             Qt::SmoothTransformation);
        const int dx = (scaled.width() - art_rect.width()) / 2;
        const int dy = (scaled.height() - art_rect.height()) / 2;
        painter->drawPixmap(art_rect, scaled, QRect(dx, dy, art_rect.width(), art_rect.height()));
    } else {
        // No box art: a neutral tile with a centered game glyph, never a blank white square.
        QLinearGradient grad(art_rect.topLeft(), art_rect.bottomRight());
        grad.setColorAt(0, QColor(0xcc, 0xd1, 0xd8));
        grad.setColorAt(1, QColor(0xac, 0xb2, 0xbc));
        painter->fillRect(art_rect, grad);
        const int gsz = art_rect.width() / 2;
        const QPixmap glyph = DeckTheme::Icon(QStringLiteral("games"), gsz);
        painter->setOpacity(0.85);
        painter->drawPixmap(QPointF(art_rect.center().x() - gsz / 2.0,
                                    art_rect.center().y() - gsz / 2.0),
                            glyph);
        painter->setOpacity(1.0);
    }
    painter->restore();

    // Border: the focused tile gets a shimmering iridescent frame (a conical gradient sweeping
    // around it, rotated by the animation phase), like the Switch's selection.
    painter->setBrush(Qt::NoBrush);
    if (selected) {
        // The Switch selection: a white ring hugging the art, then a slightly wider iridescent ring
        // (blue → purple → pink → cyan) around it that shimmers by rotating with the phase.
        painter->setPen(QPen(QColor(0xff, 0xff, 0xff, 235), 3));
        QPainterPath inner;
        inner.addRoundedRect(art_rect.adjusted(-2, -2, 2, 2), radius + 2, radius + 2);
        painter->drawPath(inner);

        QConicalGradient cg(art_rect.center(), static_cast<qreal>(phase));
        cg.setColorAt(0.00, QColor(0x4f, 0x86, 0xff));
        cg.setColorAt(0.30, QColor(0xa9, 0x5c, 0xf0));
        cg.setColorAt(0.55, QColor(0xff, 0x6b, 0xb0));
        cg.setColorAt(0.80, QColor(0x37, 0xd0, 0xf0));
        cg.setColorAt(1.00, QColor(0x4f, 0x86, 0xff));
        painter->setPen(QPen(QBrush(cg), 3));
        QPainterPath outer;
        outer.addRoundedRect(art_rect.adjusted(-5, -5, 5, 5), radius + 5, radius + 5);
        painter->drawPath(outer);
    } else {
        // Barely-there edge so tiles read as cards on either theme.
        QColor edge = DeckTheme::kText;
        edge.setAlpha(20);
        painter->setPen(QPen(edge, 1));
        painter->drawPath(clip);
    }

    // A title suspended to the HOME menu (paused in memory) wears a small blue "paused" badge at the
    // top-right of its art, so the user can see at a glance which game will resume.
    if (suspended_id != 0 &&
        index.data(GameListItemPath::ProgramIdRole).toULongLong() == suspended_id) {
        const qreal d = art_rect.width() / 4.5;
        const QRectF badge(art_rect.right() - d - 10, art_rect.top() + 10, d, d);
        painter->setPen(QPen(QColor(0xff, 0xff, 0xff, 235), 2));
        painter->setBrush(QColor(0x2f, 0x9e, 0xe0)); // Switch blue
        painter->drawEllipse(badge);
        // Two pause bars.
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0xff, 0xff, 0xff));
        const qreal bw = badge.width() * 0.13;
        const qreal bh = badge.height() * 0.42;
        const qreal cx = badge.center().x();
        const qreal cy = badge.center().y();
        const qreal g = bw * 0.9;
        painter->drawRoundedRect(QRectF(cx - g - bw, cy - bh / 2, bw, bh), bw * 0.4, bw * 0.4);
        painter->drawRoundedRect(QRectF(cx + g, cy - bh / 2, bw, bh), bw * 0.4, bw * 0.4);
    }

    // No per-tile title or favourite badge: the Switch shows only the selected game's name, above the
    // rail (the page draws it), so tiles stay clean.
    painter->restore();
}
