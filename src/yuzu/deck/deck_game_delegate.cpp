// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <QConicalGradient>
#include <QPainter>
#include <QPainterPath>

#include "qt_common/game_list/game_list_p.h"
#include "yuzu/deck/deck_game_delegate.h"
#include "yuzu/deck/deck_library_stats.h"
#include "yuzu/deck/deck_theme.h"

DeckGameDelegate::DeckGameDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize DeckGameDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const {
    // The cell is the box art plus a uniform margin on every side; that margin is the gap between
    // tiles AND the room the selection border needs, so spacing stays consistent everywhere. The
    // very first tile's cell is wider by lead_indent (empty space on its left) so the list starts
    // indented but still scrolls out to the screen edge.
    const int lead = index.row() == 0 ? lead_indent : 0;
    const int cw = card_w > 0 ? card_w : DeckTheme::kGridCardWidth;
    const int ch = card_h > 0 ? card_h : DeckTheme::kGridCardHeight;
    return {cw + 2 * DeckTheme::kGridCardMargin + lead, ch + 2 * DeckTheme::kGridCardMargin};
}

void DeckGameDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const int radius = 22;

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
    // The focused tile insets less than the rest, so it grows inside its own cell — the margin is
    // sized (kGridCardMargin > kFocusGrow + kFocusRing) so the grown tile and its ring still sit
    // fully inside the cell and never clip or touch a neighbour.
    const int inset = selected ? (DeckTheme::kGridCardMargin - DeckTheme::kFocusGrow)
                               : DeckTheme::kGridCardMargin;
    QRect box = cell.adjusted(inset, inset, -inset, -inset);
    const int side = std::min(box.width(), box.height());
    QRect art_rect(box.center().x() - side / 2, box.center().y() - side / 2, side, side);

    // The trailing "All Software" cell is a round button, not box art: a grey circle with the Switch's
    // 2x2 rounded-square mark, and a *circular* selection ring — drawn here so it never gets the
    // rectangular tile frame that would look broken around a circle.
    if (index.data(DeckAllSoftwareRole).toBool()) {
        // A round button a bit smaller than a full tile, vertically centred in the row so it reads as
        // a control at the end of the games rather than another piece of box art.
        const qreal d = side * 0.72;
        const QRectF circle(art_rect.center().x() - d / 2, art_rect.center().y() - d / 2, d, d);
        if (selected) {
            for (int s = 14; s >= 1; --s) { // soft lift, matching the tiles
                painter->setBrush(QColor(0, 0, 0, 6));
                painter->setPen(Qt::NoPen);
                painter->drawEllipse(circle.adjusted(-s, -s + 1, s, s + 2));
            }
        }
        // Flat surface circle with a grey glyph — the same language as the system dock.
        painter->setPen(Qt::NoPen);
        painter->setBrush(DeckTheme::kSurface);
        painter->drawEllipse(circle);

        // The Switch mark: a 2x2 grid of chunky rounded squares (four squares), centred.
        const qreal cell = d * 0.26;
        const qreal gap = d * 0.11;
        const qreal grid = cell * 2 + gap;
        const qreal ox = circle.center().x() - grid / 2;
        const qreal oy = circle.center().y() - grid / 2;
        painter->setBrush(DeckTheme::kTextDim);
        for (int r = 0; r < 2; ++r) {
            for (int c = 0; c < 2; ++c) {
                painter->drawRoundedRect(
                    QRectF(ox + c * (cell + gap), oy + r * (cell + gap), cell, cell), cell * 0.28,
                    cell * 0.28);
            }
        }
        if (selected) {
            // The circular counterpart of the tile selection: the same white seat the tiles get as
            // their matte, then the iridescent ring shimmering with the phase.
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(QColor(0xff, 0xff, 0xff), 9));
            painter->drawEllipse(circle.adjusted(-3, -3, 3, 3));
            QConicalGradient cg(circle.center(), static_cast<qreal>(phase));
            cg.setColorAt(0.00, QColor(0x5b, 0x8f, 0xff));
            cg.setColorAt(0.30, QColor(0xa9, 0x6c, 0xf0));
            cg.setColorAt(0.55, QColor(0xff, 0x83, 0xc0));
            cg.setColorAt(0.80, QColor(0x4f, 0xc8, 0xf0));
            cg.setColorAt(1.00, QColor(0x5b, 0x8f, 0xff));
            painter->setPen(QPen(QBrush(cg), DeckTheme::kFocusRing));
            painter->drawEllipse(circle.adjusted(-7, -7, 7, 7));
        }
        painter->restore();
        return;
    }

    // The Switch-style selection frame: an iridescent border (blue→purple→pink→cyan) that slowly
    // shimmers around the outer rim of the tile's white matte. Shared by the box-art tiles, the
    // group folder, and the new-group tile. `r` is the outer edge of the matte; the stroke is
    // centred on it, so half the width sits on the matte and half just outside.
    const auto draw_rect_selection = [&](const QRectF& r, int rad) {
        painter->setBrush(Qt::NoBrush);
        if (!rail_active) {
            // Focus is on the dock/avatar: keep a *dim* marker on the last tile so the user knows
            // where they'll return, but don't let it compete with the active zone's highlight.
            QColor dim = DeckTheme::kText;
            dim.setAlpha(70);
            painter->setPen(QPen(dim, 4));
            QPainterPath frame;
            frame.addRoundedRect(r.adjusted(-1, -1, 1, 1), rad + 1, rad + 1);
            painter->drawPath(frame);
            return;
        }
        QConicalGradient cg(r.center(), static_cast<qreal>(phase));
        cg.setColorAt(0.00, QColor(0x5b, 0x8f, 0xff));
        cg.setColorAt(0.30, QColor(0xa9, 0x6c, 0xf0));
        cg.setColorAt(0.55, QColor(0xff, 0x83, 0xc0));
        cg.setColorAt(0.80, QColor(0x4f, 0xc8, 0xf0));
        cg.setColorAt(1.00, QColor(0x5b, 0x8f, 0xff));
        painter->setPen(QPen(QBrush(cg), DeckTheme::kFocusRing));
        QPainterPath frame;
        frame.addRoundedRect(r, rad, rad);
        painter->drawPath(frame);
    };

    // Group folder tile: a rounded card with a folder glyph and the group's name.
    if (index.data(DeckGroupRole).isValid()) {
        const QRectF card = art_rect;
        painter->setPen(Qt::NoPen);
        painter->setBrush(DeckTheme::kSurface);
        painter->drawRoundedRect(card, radius, radius);
        // Folder shape.
        const qreal fw = card.width() * 0.5;
        const qreal fh = fw * 0.72;
        const QRectF folder(card.center().x() - fw / 2, card.center().y() - fh / 2 - card.height() * 0.06,
                            fw, fh);
        painter->setBrush(DeckTheme::kAccent);
        QPainterPath tab;
        tab.addRoundedRect(QRectF(folder.left(), folder.top() - fh * 0.18, fw * 0.42, fh * 0.3),
                           4, 4);
        painter->drawPath(tab);
        QPainterPath body;
        body.addRoundedRect(folder, 8, 8);
        painter->drawPath(body);
        // Name under the folder.
        QFont f = painter->font();
        f.setPixelSize(std::max(14, static_cast<int>(card.width() * 0.09)));
        painter->setFont(f);
        painter->setPen(DeckTheme::kText);
        painter->drawText(QRectF(card.left() + 6, card.bottom() - card.height() * 0.26,
                                 card.width() - 12, card.height() * 0.22),
                          Qt::AlignHCenter | Qt::AlignTop,
                          index.data(Qt::DisplayRole).toString());
        if (selected) {
            draw_rect_selection(card, radius);
        }
        painter->restore();
        return;
    }

    // "New group" tile: a dashed rounded card with a big plus.
    if (index.data(DeckNewGroupRole).toBool()) {
        const QRectF card = art_rect;
        QPen dash(DeckTheme::kTextDim, 2, Qt::DashLine);
        painter->setPen(dash);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(card, radius, radius);
        painter->setPen(QPen(DeckTheme::kTextDim, 4, Qt::SolidLine, Qt::RoundCap));
        const qreal s = card.width() * 0.16;
        painter->drawLine(QPointF(card.center().x() - s, card.center().y()),
                          QPointF(card.center().x() + s, card.center().y()));
        painter->drawLine(QPointF(card.center().x(), card.center().y() - s),
                          QPointF(card.center().x(), card.center().y() + s));
        QFont f = painter->font();
        f.setPixelSize(std::max(14, static_cast<int>(card.width() * 0.09)));
        painter->setFont(f);
        painter->drawText(QRectF(card.left() + 6, card.bottom() - card.height() * 0.26,
                                 card.width() - 12, card.height() * 0.22),
                          Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("New Group"));
        if (selected) {
            draw_rect_selection(card, radius);
        }
        painter->restore();
        return;
    }

    if (selected && rail_active) {
        // A soft drop shadow so the focused tile lifts off the page (the Switch's subtle highlight),
        // rather than a heavy coloured glow. Only while the rail is the active zone. Kept inside the
        // cell — QListView does not clip items, so a wider shadow bleeds onto the neighbouring tile
        // and is then half-overpainted by it, which reads as a dirty edge on one side only.
        const int shadow = std::max(1, inset - DeckTheme::kFocusRing);
        for (int s = shadow; s >= 1; --s) {
            QPainterPath sh;
            sh.addRoundedRect(art_rect.adjusted(-s, -s + 1, s, s + 1), radius + s, radius + s);
            painter->fillPath(sh, QColor(0, 0, 0, 7));
        }
    }

    // The Switch's selected tile sits on a white matte: the box art is inset inside a white card, so
    // a broad white band shows around the art, and the iridescent frame rims the card's outer edge
    // (art → white backing → selection frame). Non-selected tiles are just the art.
    //
    // The matte is what actually makes the focused tile read from across the room — on the console
    // it is roughly 2.5% of the tile's side, an order of magnitude more than a hairline. Scaled from
    // the art so the 272px rail tiles and the 200px All Software tiles get the same proportion.
    const int matte = (selected && rail_active) ? std::max(5, art_rect.width() / 40) : 0;
    if (matte > 0) {
        QPainterPath card;
        card.addRoundedRect(art_rect, radius, radius);
        painter->fillPath(card, QColor(0xff, 0xff, 0xff));
    }
    const QRectF art_inner = art_rect.adjusted(matte, matte, -matte, -matte);
    const int inner_radius = std::max(4, radius - matte);

    QPainterPath clip;
    clip.addRoundedRect(art_inner, inner_radius, inner_radius);
    painter->save();
    painter->setClipPath(clip);

    const QPixmap pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
    bool has_art = !pixmap.isNull();
    if (has_art) {
        // A title with no box art is given a fully-transparent default icon. Sample the centre pixel
        // (one pixel — cheap, only for the visible tiles) to detect it, so it draws the placeholder
        // instead of an empty/transparent tile.
        const QImage centre = pixmap.copy(pixmap.width() / 2, pixmap.height() / 2, 1, 1).toImage();
        if (centre.isNull() || centre.pixelColor(0, 0).alpha() < 8) {
            has_art = false;
        }
    }
    if (has_art) {
        const QPixmap scaled = pixmap.scaled(art_inner.size().toSize(), Qt::KeepAspectRatioByExpanding,
                                             Qt::SmoothTransformation);
        const int dx = (scaled.width() - static_cast<int>(art_inner.width())) / 2;
        const int dy = (scaled.height() - static_cast<int>(art_inner.height())) / 2;
        painter->drawPixmap(art_inner, scaled,
                            QRectF(dx, dy, art_inner.width(), art_inner.height()));
    } else {
        // No box art: a neutral tile with a game glyph and the title, so it reads as an intentional
        // placeholder (and stays identifiable) rather than a blank tile.
        QLinearGradient grad(art_inner.topLeft(), art_inner.bottomRight());
        grad.setColorAt(0, QColor(0xcc, 0xd1, 0xd8));
        grad.setColorAt(1, QColor(0xac, 0xb2, 0xbc));
        painter->fillRect(art_inner, grad);
        const int gsz = static_cast<int>(art_inner.width() / 2.6);
        const QPixmap glyph = DeckTheme::Icon(QStringLiteral("games"), gsz);
        painter->setOpacity(0.8);
        painter->drawPixmap(QPointF(art_inner.center().x() - gsz / 2.0,
                                    art_inner.top() + art_inner.height() * 0.22),
                            glyph);
        painter->setOpacity(1.0);
        QString name = index.data(GameListItemPath::TitleRole).toString();
        if (name.isEmpty()) {
            name = index.data(Qt::DisplayRole).toString();
        }
        QFont nf = painter->font();
        nf.setPixelSize(std::max(13, static_cast<int>(art_inner.width() * 0.078)));
        painter->setFont(nf);
        painter->setPen(QColor(0x33, 0x38, 0x40));
        const QRectF name_rect(art_inner.left() + 10, art_inner.top() + art_inner.height() * 0.62,
                               art_inner.width() - 20, art_inner.height() * 0.32);
        painter->drawText(name_rect, Qt::AlignHCenter | Qt::AlignTop,
                          painter->fontMetrics().elidedText(
                              name, Qt::ElideRight, static_cast<int>(art_inner.width() - 20)));
    }
    painter->restore();

    // Border: the focused tile gets the iridescent selection frame (over the white matte + drop
    // shadow drawn earlier) — art → white backing → frame, like the Switch.
    painter->setBrush(Qt::NoBrush);
    if (selected) {
        draw_rect_selection(art_rect, radius);
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

    // In the add-to-group picker, a member game wears a blue checkbox at the top-right of its art
    // (matching the Switch's "Select Software to Add" screen).
    if (index.data(DeckGroupMemberRole).toBool()) {
        const qreal d = art_rect.width() / 4.6;
        const QRectF badge(art_rect.right() - d - 8, art_rect.top() + 8, d, d);
        QPainterPath box;
        box.addRoundedRect(badge, badge.width() * 0.22, badge.width() * 0.22);
        painter->setPen(QPen(QColor(0xff, 0xff, 0xff, 235), 2));
        painter->setBrush(QColor(0x2f, 0x9e, 0xe0)); // Switch blue
        painter->drawPath(box);
        painter->setPen(QPen(QColor(0xff, 0xff, 0xff), badge.width() * 0.12, Qt::SolidLine,
                             Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        QPainterPath check;
        check.moveTo(badge.left() + badge.width() * 0.26, badge.top() + badge.height() * 0.52);
        check.lineTo(badge.left() + badge.width() * 0.44, badge.top() + badge.height() * 0.70);
        check.lineTo(badge.left() + badge.width() * 0.76, badge.top() + badge.height() * 0.30);
        painter->drawPath(check);
    }

    // A title added in the last week and not yet opened wears a small "NEW" flag in the top-left,
    // so a game you just copied over is findable even if the library is sorted some other way.
    if (stats != nullptr &&
        stats->IsNew(index.data(GameListItemPath::ProgramIdRole).toULongLong())) {
        QFont bf = painter->font();
        bf.setPixelSize(std::max(11, static_cast<int>(art_rect.width() * 0.072)));
        bf.setBold(true);
        painter->setFont(bf);
        const QString label = QStringLiteral("NEW");
        const int tw = QFontMetrics(bf).horizontalAdvance(label);
        const QRectF flag(art_rect.left() + 8, art_rect.top() + 8, tw + 18, bf.pixelSize() + 10);
        QPainterPath fp;
        fp.addRoundedRect(flag, flag.height() / 2.0, flag.height() / 2.0);
        painter->setPen(Qt::NoPen);
        painter->fillPath(fp, QColor(0xe8, 0x4a, 0x3f)); // the console's "new" red
        painter->setPen(QColor(0xff, 0xff, 0xff));
        painter->drawText(flag, Qt::AlignCenter, label);
    }

    // No per-tile title or favourite badge: the Switch shows only the selected game's name, above the
    // rail (the page draws it), so tiles stay clean.
    painter->restore();
}
