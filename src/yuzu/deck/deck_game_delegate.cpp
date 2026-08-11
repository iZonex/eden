// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cmath>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include "qt_common/game_list/game_list_p.h"
#include "yuzu/deck/deck_game_delegate.h"
#include "yuzu/deck/deck_library_stats.h"
#include "yuzu/deck/deck_theme.h"

DeckGameDelegate::DeckGameDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize DeckGameDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    // The cell is the box art plus a uniform margin on every side; that margin is the gap between
    // tiles AND the room the selection border needs, so spacing stays consistent everywhere. The
    // very first tile's cell is wider by lead_indent (empty space on its left) so the list starts
    // indented but still scrolls out to the screen edge.
    const int lead = index.row() == 0 ? lead_indent : 0;
    const int cw = card_w > 0 ? card_w : DeckTheme::kGridCardWidth;
    const int ch = card_h > 0 ? card_h : DeckTheme::kGridCardHeight;
    // The round Show More button is much narrower than a tile, so it gets a cell of its own width —
    // otherwise it floats in a tile-sized slot and the gaps on either side of it are visibly wider
    // than every other gap in the row, which reads as a layout fault rather than as a button.
    if (index.data(DeckAllSoftwareRole).toBool()) {
        const int d = RoundButtonDiameter();
        return {std::max(d, CaptionWidth(option.font)) + 2 * DeckTheme::kGridCardMargin + lead,
                ch + 2 * DeckTheme::kGridCardMargin};
    }
    return {cw + 2 * DeckTheme::kGridCardMargin + lead, ch + 2 * DeckTheme::kGridCardMargin};
}

int DeckGameDelegate::RoundButtonDiameter() const {
    return (card_h > 0 ? card_h : DeckTheme::kGridCardHeight) * 52 / 100;
}

QFont DeckGameDelegate::CaptionFont(const QFont& base) {
    QFont f = base;
    f.setPixelSize(20);
    f.setWeight(QFont::Normal);
    return f;
}

int DeckGameDelegate::CaptionWidth(const QFont& base) {
    return QFontMetrics(CaptionFont(base)).horizontalAdvance(Caption()) + 8;
}

QString DeckGameDelegate::Caption() {
    return tr("Show More");
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
    // fully inside the cell and never clip or touch a neighbour. The growth is scaled by focus_t so
    // the tile swells into focus over a few frames instead of snapping a size larger.
    const int grow = selected ? static_cast<int>(std::lround(DeckTheme::kFocusGrow * focus_t)) : 0;
    const int inset = DeckTheme::kGridCardMargin - grow;
    QRect box = cell.adjusted(inset, inset, -inset, -inset);
    int side = std::min(box.width(), box.height());
    // The press dip applies to whatever the cursor is on, so choosing a game feels like pushing it:
    // one dip out and back across the run of the animation.
    if (selected && press_t > 0.0) {
        const qreal scale = 1.0 - 0.06 * std::sin(press_t * 3.14159265358979);
        side = static_cast<int>(std::lround(side * scale));
    }
    QRect art_rect(box.center().x() - side / 2, box.center().y() - side / 2, side, side);

    // The trailing "Show More" cell is a round button, not box art: a grey disc with the console's
    // 2x2 mark punched THROUGH it, captioned above, and a *circular* selection ring — drawn here so
    // it never gets the rectangular tile frame that would look broken around a circle.
    if (index.data(DeckAllSoftwareRole).toBool()) {
        const bool focused = selected && rail_active;
        // Noticeably smaller than a tile and vertically centred, so it reads as a control at the end
        // of the games rather than as another piece of box art.
        const qreal d = RoundButtonDiameter();
        const QRectF circle(box.center().x() - d / 2, box.center().y() - d / 2, d, d);

        // At rest the button is as quiet as a vacant slot: the same pale seat, no frame around it and
        // no lift under it, carrying four grey OUTLINE squares. Nothing here competes with the box
        // art beside it — the button only asserts itself once the cursor is actually on it.
        painter->setPen(Qt::NoPen);
        painter->setBrush(DeckTheme::kPlaceholder);
        painter->drawEllipse(circle);

        // Small squares set a hair apart: the space between their drawn edges is exactly one outline
        // width, so the four read as one tight mark rather than as four separate boxes sharing a
        // circle. The centre distance is TWICE the stroke, not once — Qt centres a pen on the path,
        // so each square's outline already eats half a stroke of the gap from either side, and a gap
        // of one stroke leaves the four touching with no daylight at all.
        const qreal stroke = std::max(1.2, d * 0.022);
        const qreal sq = d * 0.14;
        const qreal gap = 2.0 * stroke;
        const qreal grid = sq * 2 + gap;
        const qreal ox = circle.center().x() - grid / 2;
        const qreal oy = circle.center().y() - grid / 2;
        // Clockwise from the top left, which is the order the press fills them in.
        const std::array<QPointF, 4> corners{QPointF(ox, oy), QPointF(ox + sq + gap, oy),
                                             QPointF(ox + sq + gap, oy + sq + gap),
                                             QPointF(ox, oy + sq + gap)};
        for (int i = 0; i < 4; ++i) {
            const QRectF mark(corners[i], QSizeF(sq, sq));
            const qreal round = sq * 0.28;
            // The press paints the squares in one after another — the console's acknowledgement that
            // the button was chosen, played out before the page it opens takes over the screen.
            const qreal fill = std::clamp(press_t * 4.0 - i, 0.0, 1.0);
            if (fill > 0.0) {
                painter->setPen(Qt::NoPen);
                QColor ink = DeckTheme::kTextDim;
                ink.setAlphaF(fill);
                painter->setBrush(ink);
                painter->drawRoundedRect(mark, round, round);
            }
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(DeckTheme::kTextDim, stroke));
            painter->drawRoundedRect(mark, round, round);
        }

        if (focused) {
            // The selection sits ON the circle's own edge — no white seat, no gap. Anything between
            // the ring and the button turns this into a badge floating inside a halo, which is not
            // what the console draws and is what made the button look like a mistake in the row.
            painter->setBrush(Qt::NoBrush);
            painter->setOpacity(focus_t);
            painter->setPen(
                QPen(DeckTheme::SelectionSweep(circle.center(), phase), DeckTheme::kFocusRing));
            painter->drawEllipse(circle);
            // The name appears with the selection, the same way a game's title does — it is the
            // label for what the cursor is on, not a permanent caption under a button.
            const QFont cf = CaptionFont(option.font);
            painter->setFont(cf);
            painter->setPen(DeckTheme::IsLightMode() ? QColor(0x2f, 0x6c, 0xb5)
                                                     : QColor(0x6a, 0xb4, 0xff));
            const int caption_h = QFontMetrics(cf).height();
            painter->drawText(
                QRectF(box.left() - 20, circle.top() - caption_h - 12, box.width() + 40, caption_h),
                Qt::AlignHCenter | Qt::AlignVCenter, Caption());
            painter->setOpacity(1.0);
        }
        painter->restore();
        return;
    }

    // An empty home-row slot: a pale vacant seat, lighter than the page and with no border of its
    // own. The cursor lands on these (the console lets it) and they do nothing — when focused the
    // seat brightens to white and takes the same iridescent rim as a real tile, so the cursor never
    // disappears just because it is standing on a gap.
    if (index.data(DeckPlaceholderRole).toBool()) {
        const bool focused = selected && rail_active;
        QPainterPath seat;
        seat.addRoundedRect(art_rect, radius, radius);
        painter->setPen(Qt::NoPen);
        painter->fillPath(seat, DeckTheme::kPlaceholder);
        if (focused) {
            // Brightens to the card colour along the settle, so an empty slot lights up the same way
            // a real tile does instead of flicking white the instant the cursor touches it.
            painter->setOpacity(focus_t);
            painter->fillPath(seat, DeckTheme::kSurface);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(DeckTheme::SelectionSweep(art_rect.center(), phase),
                                 DeckTheme::kFocusRing));
            painter->drawPath(seat);
            painter->setOpacity(1.0);
        }
        painter->restore();
        return;
    }

    // The console's selection frame: an iridescent border that slowly shimmers around the outer rim
    // of the tile's white matte. Shared by the box-art tiles, the empty slots, the group folder and
    // the new-group tile. `r` is the outer edge of the matte; the stroke is centred on it, so half
    // the width sits on the matte and half just outside.
    //
    // When the rail is not the focused zone this draws NOTHING: on the console, moving to the dock
    // leaves the game row completely unmarked, and any leftover outline reads as a second cursor.
    const auto draw_rect_selection = [&](const QRectF& r, int rad) {
        if (!rail_active) {
            return;
        }
        painter->setBrush(Qt::NoBrush);
        painter->setOpacity(focus_t); // fades in with the settle, rather than blinking on
        painter->setPen(QPen(DeckTheme::SelectionSweep(r.center(), phase), DeckTheme::kFocusRing));
        QPainterPath frame;
        frame.addRoundedRect(r, rad, rad);
        painter->drawPath(frame);
        painter->setOpacity(1.0);
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
    const int matte =
        (selected && rail_active)
            ? static_cast<int>(std::lround(std::max(5, art_rect.width() / 40) * focus_t))
            : 0;
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

    // A title suspended to the HOME menu (paused in memory) wears the console's "Playing" pill along
    // the bottom of its art: the playing user's round avatar, and a black lozenge with the word
    // beside it. It sits over the art rather than in a corner badge, which is what makes a running
    // game unmistakable at a glance across a row of tiles.
    if (playing_id != 0 &&
        index.data(GameListItemPath::ProgramIdRole).toULongLong() == playing_id) {
        const qreal side = art_inner.width();
        const qreal m = side * 0.045;
        const qreal face = side * 0.175;
        const qreal pill_h = side * 0.155;
        const QPointF face_c(art_inner.left() + m + face / 2, art_inner.bottom() - m - face / 2);
        // The lozenge runs from the middle of the avatar to the far margin, so the avatar overlaps
        // its left cap exactly as on the console.
        const QRectF pill(face_c.x(), face_c.y() - pill_h / 2, art_inner.right() - m - face_c.x(),
                          pill_h);
        painter->setPen(QPen(QColor(0xff, 0xff, 0xff, 230), std::max(1.0, side * 0.007)));
        painter->setBrush(QColor(0x11, 0x11, 0x13));
        painter->drawRoundedRect(pill, pill_h / 2, pill_h / 2);
        // The word, centred in the part of the lozenge the avatar does not cover.
        QFont pf = painter->font();
        pf.setPixelSize(std::max(10, static_cast<int>(side * 0.088)));
        pf.setWeight(QFont::Medium);
        painter->setFont(pf);
        painter->setPen(QColor(0xff, 0xff, 0xff));
        painter->drawText(pill.adjusted(face * 0.6, 0, -pill_h * 0.3, 0), Qt::AlignCenter,
                          tr("Playing"));
        // The avatar last, so it sits on top of the lozenge's left cap.
        const QRectF face_rect(face_c.x() - face / 2, face_c.y() - face / 2, face, face);
        if (!playing_avatar.isNull()) {
            painter->save();
            QPainterPath clip_face;
            clip_face.addEllipse(face_rect);
            painter->setClipPath(clip_face);
            painter->drawPixmap(face_rect.toRect(), playing_avatar);
            painter->restore();
        } else {
            painter->setPen(Qt::NoPen);
            painter->setBrush(DeckTheme::kSurface);
            painter->drawEllipse(face_rect);
        }
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
