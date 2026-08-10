// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <QDateTime>
#include <QLinearGradient>
#include <QLocale>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPainterPath>

#include <fmt/format.h>

#include "frontend_common/play_time_manager.h"
#include "yuzu/deck/deck_game_detail_page.h"
#include "yuzu/deck/deck_theme.h"

DeckGameDetailPage::DeckGameDetailPage(QWidget* parent) : DeckPage(parent) {}

void DeckGameDetailPage::SetGame(const DeckGameInfo& info) {
    game = info;
    current = 0;
    confirming_delete = false;
    update();
}

namespace {
/// "today" / "yesterday" / "3 days ago" / a date, so a timestamp reads at a glance.
QString RelativeDate(s64 seconds) {
    if (seconds <= 0) {
        return QObject::tr("Never");
    }
    const QDateTime when = QDateTime::fromSecsSinceEpoch(seconds);
    const qint64 days = when.date().daysTo(QDate::currentDate());
    if (days <= 0) {
        return QObject::tr("Today");
    }
    if (days == 1) {
        return QObject::tr("Yesterday");
    }
    if (days < 30) {
        return QObject::tr("%n day(s) ago", nullptr, static_cast<int>(days));
    }
    return QLocale().toString(when.date(), QLocale::ShortFormat);
}

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

std::vector<std::pair<QString, QString>> DeckGameDetailPage::Facts() const {
    std::vector<std::pair<QString, QString>> facts;
    facts.emplace_back(tr("Play time"),
                       game.play_time_seconds > 0
                           ? QString::fromStdString(PlayTime::PlayTimeManager::GetReadablePlayTime(
                                 game.play_time_seconds))
                           : tr("Never played"));
    facts.emplace_back(tr("Last played"), RelativeDate(game.last_played));
    facts.emplace_back(tr("Added"), RelativeDate(game.first_seen));
    if (game.launches > 0) {
        facts.emplace_back(tr("Times opened"), QString::number(game.launches));
    }
    if (!game.size_text.isEmpty()) {
        facts.emplace_back(tr("Size"), game.size_text);
    }
    if (!game.file_type.isEmpty()) {
        facts.emplace_back(tr("Format"), game.file_type);
    }
    facts.emplace_back(tr("Version"),
                       game.version.isEmpty() ? QStringLiteral("1.0.0") : game.version);
    if (game.program_id != 0) {
        facts.emplace_back(tr("Title ID"),
                           QString::fromStdString(fmt::format("{:016X}", game.program_id)));
    }
    return facts;
}

bool DeckGameDetailPage::Compact() const {
    // A TV-docked shell insets the page for overscan (see DeckShell::resizeEvent), so the same
    // layout that fits the Deck's 800px panel has ~130px less to work with there. Tighten up rather
    // than letting the last two actions fall off the bottom.
    return height() < 720;
}

qreal DeckGameDetailPage::FactPitch() const {
    return Compact() ? 22.0 : 26.0;
}

QRectF DeckGameDetailPage::ActionRect(int i) const {
    const qreal x = 470;
    const qreal w = std::min<qreal>(520, width() - x - 60);
    const qreal pitch = Compact() ? 48.0 : 58.0;
    const qreal h = pitch - 6;
    // Below the fact table, and clear of the bottom: five actions at this pitch end at 700 on the
    // Deck's 800px panel. On a shorter page they slide up to whatever room is left.
    const qreal facts_bottom = 180 + 8 * FactPitch() + 16;
    const qreal y = std::max(facts_bottom, height() - 12 - ActionCount * pitch) + i * pitch;
    return {x, y, w, h};
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
        emit PlayRequested(game.path, game.program_id);
        break;
    case Favorite:
        game.favorited = !game.favorited;
        emit FavoriteToggled(game.program_id);
        update();
        break;
    case RemoveUpdate:
        emit RemoveUpdateRequested(game.program_id);
        break;
    case RemoveDLC:
        emit RemoveDLCRequested(game.program_id);
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
        emit DeleteRequested(game.path, game.program_id, game.title);
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
    const QRectF art_rect(60, 84, 360, 360);
    QPainterPath clip;
    clip.addRoundedRect(art_rect, 18, 18);
    p.save();
    p.setClipPath(clip);
    if (!game.art.isNull()) {
        const QPixmap scaled = game.art.scaled(art_rect.size().toSize(),
                                               Qt::KeepAspectRatioByExpanding,
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

    // Title.
    const qreal col_x = 470;
    const qreal col_w = width() - col_x - 60;
    QFont title_font = font();
    title_font.setPixelSize(38);
    title_font.setBold(false);
    p.setFont(title_font);
    p.setPen(DeckTheme::kText);
    p.drawText(QRectF(col_x, 84, col_w, 88), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               game.title);

    // Fact table: label on the left, value right-aligned, hairline between rows — the same shape as
    // the console's own information screens. Replaces the raw tooltip string this page used to
    // print, which said only "Play Time" and "Version" and ran off the edge on a long title.
    const auto facts = Facts();
    QFont label_font = font();
    label_font.setPixelSize(17);
    QFont value_font = font();
    value_font.setPixelSize(18);
    qreal y = 180;
    const qreal row_h = FactPitch();
    for (const auto& [label, value] : facts) {
        p.setFont(label_font);
        p.setPen(DeckTheme::kTextDim);
        p.drawText(QRectF(col_x, y, col_w * 0.55, row_h), Qt::AlignLeft | Qt::AlignVCenter, label);
        p.setFont(value_font);
        p.setPen(DeckTheme::kText);
        p.drawText(QRectF(col_x + col_w * 0.45, y, col_w * 0.55, row_h),
                   Qt::AlignRight | Qt::AlignVCenter, value);
        QColor rule = DeckTheme::kText;
        rule.setAlpha(28);
        p.setPen(QPen(rule, 1));
        p.drawLine(QPointF(col_x, y + row_h), QPointF(col_x + col_w, y + row_h));
        y += row_h;
    }

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
                   ActionLabel(i, game.favorited));
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
                   QObject::tr("Delete \"%1\"?").arg(game.title));

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
