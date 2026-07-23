// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPainterPath>

#include "yuzu/deck/deck_game_detail_page.h"
#include "yuzu/deck/deck_theme.h"

DeckGameDetailPage::DeckGameDetailPage(QWidget* parent) : DeckPage(parent) {}

void DeckGameDetailPage::SetGame(const QString& path_, u64 program_id_, const QString& title_,
                                 const QPixmap& art_, const QString& meta_, bool favorited_) {
    path = path_;
    program_id = program_id_;
    title = title_;
    art = art_;
    meta = meta_;
    favorited = favorited_;
    current = 0;
    confirming_delete = false;
    update();
}

namespace {
QString ActionLabel(int i, bool favorited) {
    switch (i) {
    case DeckGameDetailPage::Play:
        return QObject::tr("Play");
    case DeckGameDetailPage::Favorite:
        return favorited ? QObject::tr("Remove from favorites") : QObject::tr("Add to favorites");
    case DeckGameDetailPage::RemoveUpdate:
        return QObject::tr("Remove update");
    case DeckGameDetailPage::RemoveDLC:
        return QObject::tr("Remove DLC");
    case DeckGameDetailPage::Delete:
        return QObject::tr("Delete game");
    default:
        return {};
    }
}
} // namespace

QRectF DeckGameDetailPage::ActionRect(int i) const {
    const qreal x = 470;
    const qreal w = std::min<qreal>(520, width() - x - 60);
    const qreal y = 250 + i * 66;
    return {x, y, w, 58};
}

void DeckGameDetailPage::SetCurrent(int index) {
    current = std::clamp(index, 0, static_cast<int>(ActionCount) - 1);
    update();
}

void DeckGameDetailPage::OnActivated() {
    confirming_delete = false;
    current = 0;
    emit HintsChanged();
    update();
}

bool DeckGameDetailPage::OnNavigate(Qt::Key key) {
    if (confirming_delete) {
        return true; // swallow navigation while the confirm is up
    }
    if (key == Qt::Key_Up) {
        SetCurrent(current - 1);
        return true;
    }
    if (key == Qt::Key_Down) {
        SetCurrent(current + 1);
        return true;
    }
    return true; // detail page consumes left/right too (no horizontal nav)
}

void DeckGameDetailPage::Activate() {
    switch (current) {
    case Play:
        emit PlayRequested(path, program_id);
        break;
    case Favorite:
        favorited = !favorited;
        emit FavoriteToggled(program_id);
        update();
        break;
    case RemoveUpdate:
        emit RemoveUpdateRequested(program_id);
        break;
    case RemoveDLC:
        emit RemoveDLCRequested(program_id);
        break;
    case Delete:
        confirming_delete = true;
        emit HintsChanged();
        update();
        break;
    default:
        break;
    }
}

bool DeckGameDetailPage::OnAccept() {
    if (confirming_delete) {
        confirming_delete = false;
        emit HintsChanged();
        emit DeleteRequested(path, program_id, title);
        return true;
    }
    Activate();
    return true;
}

bool DeckGameDetailPage::OnBack() {
    if (confirming_delete) {
        confirming_delete = false;
        emit HintsChanged();
        update();
        return true; // consumed: just cancel the confirm
    }
    return false; // let the shell return to the library
}

void DeckGameDetailPage::mousePressEvent(QMouseEvent* event) {
    if (confirming_delete) {
        return;
    }
    for (int i = 0; i < ActionCount; ++i) {
        if (ActionRect(i).contains(event->position())) {
            current = i;
            Activate();
            return;
        }
    }
}

void DeckGameDetailPage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.fillRect(rect(), DeckTheme::kBackground);

    // Box art.
    const QRectF art_rect(60, 100, 360, 360);
    QPainterPath clip;
    clip.addRoundedRect(art_rect, 18, 18);
    p.save();
    p.setClipPath(clip);
    if (!art.isNull()) {
        const QPixmap scaled = art.scaled(art_rect.size().toSize(), Qt::KeepAspectRatioByExpanding,
                                          Qt::SmoothTransformation);
        p.drawPixmap(art_rect.topLeft() -
                         QPointF((scaled.width() - art_rect.width()) / 2.0,
                                 (scaled.height() - art_rect.height()) / 2.0),
                     scaled);
    } else {
        QLinearGradient g(art_rect.topLeft(), art_rect.bottomRight());
        g.setColorAt(0, QColor(0xcc, 0xd1, 0xd8));
        g.setColorAt(1, QColor(0xac, 0xb2, 0xbc));
        p.fillRect(art_rect, g);
        const int gs = 130;
        p.setOpacity(0.85);
        p.drawPixmap(QPointF(art_rect.center().x() - gs / 2.0, art_rect.center().y() - gs / 2.0),
                     DeckTheme::Icon(QStringLiteral("games"), gs));
        p.setOpacity(1.0);
    }
    p.restore();
    p.setPen(QPen(QColor(0, 0, 0, 20), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(clip);

    // Title + meta.
    QFont title_font = font();
    title_font.setPixelSize(38);
    title_font.setBold(false);
    p.setFont(title_font);
    p.setPen(DeckTheme::kText);
    p.drawText(QRectF(470, 108, width() - 470 - 60, 96),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, title);

    QFont meta_font = font();
    meta_font.setPixelSize(18);
    p.setFont(meta_font);
    p.setPen(DeckTheme::kTextDim);
    p.drawText(QRectF(470, 205, width() - 470 - 60, 40), Qt::AlignLeft, meta);

    // Action list.
    for (int i = 0; i < ActionCount; ++i) {
        const QRectF r = ActionRect(i);
        const bool sel = (i == current) && !confirming_delete;
        if (sel) {
            // Cyan glowing rounded border, matching the Switch selection used across the console UI.
            for (int s = 6; s >= 1; --s) {
                QPainterPath glow;
                glow.addRoundedRect(r.adjusted(-s, -s, s, s), 10 + s, 10 + s);
                QColor c = DeckTheme::kAccentGlow;
                c.setAlpha(6 + (6 - s) * 7);
                p.fillPath(glow, c);
            }
            QPainterPath fill;
            fill.addRoundedRect(r, 10, 10);
            p.fillPath(fill, DeckTheme::kAccentSoft);
            p.setPen(QPen(DeckTheme::kAccent, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, 10, 10);
        }
        QFont af = font();
        af.setPixelSize(22);
        af.setBold(sel);
        p.setFont(af);
        const bool destructive = (i == Delete);
        p.setPen(destructive ? QColor(0xd9, 0x53, 0x4f) : DeckTheme::kText);
        p.drawText(r.adjusted(22, 0, -16, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   ActionLabel(i, favorited));
    }

    // Delete confirmation overlay.
    if (confirming_delete) {
        p.fillRect(rect(), QColor(0, 0, 0, 110));
        const QRectF box((width() - 640) / 2.0, (height() - 240) / 2.0, 640, 240);
        QPainterPath bp;
        bp.addRoundedRect(box, 16, 16);
        p.fillPath(bp, DeckTheme::kSurface);

        QFont hf = font();
        hf.setPixelSize(26);
        hf.setBold(false);
        p.setFont(hf);
        p.setPen(DeckTheme::kText);
        p.drawText(box.adjusted(40, 40, -40, 0), Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
                   QObject::tr("Delete \"%1\"?").arg(title));

        QFont bf = font();
        bf.setPixelSize(18);
        p.setFont(bf);
        p.setPen(DeckTheme::kTextDim);
        p.drawText(box.adjusted(40, 110, -40, -70),
                   Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
                   QObject::tr("This permanently deletes the game file from your Deck. This cannot "
                               "be undone."));

        QFont pf = font();
        pf.setPixelSize(19);
        pf.setBold(false);
        p.setFont(pf);
        p.setPen(QColor(0xd9, 0x53, 0x4f));
        p.drawText(box.adjusted(0, 0, -40, -24), Qt::AlignBottom | Qt::AlignRight,
                   QObject::tr("A  Delete"));
        p.setPen(DeckTheme::kText);
        p.drawText(box.adjusted(40, 0, 0, -24), Qt::AlignBottom | Qt::AlignLeft,
                   QObject::tr("B  Cancel"));
    }
}

std::vector<DeckHint> DeckGameDetailPage::Hints() const {
    if (confirming_delete) {
        return {{QStringLiteral("A"), tr("Delete")}, {QStringLiteral("B"), tr("Cancel")}};
    }
    return {{QStringLiteral("A"), tr("Select")}, {QStringLiteral("B"), tr("Back")}};
}
