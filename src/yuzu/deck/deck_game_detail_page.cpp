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
    modal = Modal::None;
    update();
}

void DeckGameDetailPage::ShowNotice(const QString& title, const QString& body) {
    notice_title = title;
    notice_body = body;
    modal = Modal::Notice;
    emit HintsChanged();
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
    modal = Modal::None;
    current = 0;
    emit HintsChanged();
    update();
}

bool DeckGameDetailPage::OnNavigate(Qt::Key key) {
    if (modal != Modal::None) {
        return true; // swallow navigation while a modal is up
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
    // Removing an update or DLC throws away installed content, so it gets the same "are you sure"
    // the delete does. It used to fire straight off, and then a desktop message box asked instead.
    case RemoveUpdate:
        modal = Modal::RemoveUpdate;
        break;
    case RemoveDLC:
        modal = Modal::RemoveDLC;
        break;
    case Delete:
        modal = Modal::DeleteGame;
        break;
    default:
        return;
    }
    emit HintsChanged();
    update();
}

void DeckGameDetailPage::ConfirmModal() {
    const Modal was = modal;
    modal = Modal::None;
    emit HintsChanged();
    update();
    switch (was) {
    case Modal::DeleteGame:
        emit DeleteRequested(game.path, game.program_id, game.title);
        break;
    case Modal::RemoveUpdate:
        emit RemoveUpdateRequested(game.program_id);
        break;
    case Modal::RemoveDLC:
        emit RemoveDLCRequested(game.program_id);
        break;
    case Modal::Notice:
    case Modal::None:
        break; // a notice is just dismissed
    }
}

bool DeckGameDetailPage::OnAccept() {
    if (modal != Modal::None) {
        ConfirmModal();
        return true;
    }
    Activate();
    return true;
}

bool DeckGameDetailPage::OnBack() {
    if (modal != Modal::None) {
        modal = Modal::None;
        emit HintsChanged();
        update();
        return true; // consumed: just dismiss the modal
    }
    return false; // let the shell return to the library
}

void DeckGameDetailPage::mousePressEvent(QMouseEvent* event) {
    if (modal != Modal::None) {
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
        const bool sel = (i == current) && modal == Modal::None;
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

    DrawModal(p);
}

void DeckGameDetailPage::DrawModal(QPainter& p) {
    if (modal == Modal::None) {
        return;
    }

    QString heading;
    QString body;
    QString confirm_label;
    bool destructive = false;
    switch (modal) {
    case Modal::DeleteGame:
        // Elided: a long title used to run past the edge of a fixed-width box.
        heading = tr("Delete this game?");
        body = tr("\"%1\" will be erased from your Deck. This cannot be undone.").arg(game.title);
        confirm_label = tr("Delete");
        destructive = true;
        break;
    case Modal::RemoveUpdate:
        heading = tr("Remove the installed update?");
        body = tr("\"%1\" goes back to the version it shipped with. The game itself stays.")
                   .arg(game.title);
        confirm_label = tr("Remove");
        destructive = true;
        break;
    case Modal::RemoveDLC:
        heading = tr("Remove the installed DLC?");
        body = tr("Every add-on installed for \"%1\" is removed. The game itself stays.")
                   .arg(game.title);
        confirm_label = tr("Remove");
        destructive = true;
        break;
    case Modal::Notice:
        heading = notice_title;
        body = notice_body;
        confirm_label = tr("OK");
        break;
    case Modal::None:
        return;
    }

    p.fillRect(rect(), QColor(0, 0, 0, 130));

    // Size the card to its text instead of a fixed 640x240 — a long game name overflowed it, which
    // is what made the old confirmation look broken.
    const qreal w = std::min<qreal>(760, width() - 120);
    const qreal pad = 36;
    QFont hf = font();
    hf.setPixelSize(27);
    QFont bf = font();
    bf.setPixelSize(19);
    const QRectF text_w(0, 0, w - 2 * pad, 10000);
    const qreal head_h =
        QFontMetrics(hf).boundingRect(text_w.toRect(), Qt::TextWordWrap, heading).height();
    const qreal body_h =
        QFontMetrics(bf).boundingRect(text_w.toRect(), Qt::TextWordWrap, body).height();
    const qreal buttons_h = 40;
    const qreal h = pad + head_h + 16 + body_h + 28 + buttons_h + pad;
    const QRectF box((width() - w) / 2.0, std::max<qreal>(40, (height() - h) / 2.0), w, h);

    for (int s = 10; s >= 1; --s) { // the card lifts off the dimmed page
        QPainterPath sh;
        sh.addRoundedRect(box.adjusted(-s, -s + 2, s, s + 3), 18 + s, 18 + s);
        p.fillPath(sh, QColor(0, 0, 0, 8));
    }
    QPainterPath bp;
    bp.addRoundedRect(box, 18, 18);
    p.fillPath(bp, DeckTheme::kSurface);

    p.setFont(hf);
    p.setPen(destructive ? QColor(0xd9, 0x53, 0x4f) : DeckTheme::kText);
    p.drawText(QRectF(box.left() + pad, box.top() + pad, w - 2 * pad, head_h),
               Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, heading);

    p.setFont(bf);
    p.setPen(DeckTheme::kTextDim);
    p.drawText(QRectF(box.left() + pad, box.top() + pad + head_h + 16, w - 2 * pad, body_h),
               Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, body);

    // Real button glyphs, the same ones the hint bar draws, so the prompt and the bar agree.
    const qreal by = box.bottom() - pad - buttons_h / 2.0;
    qreal x = box.right() - pad;
    const auto chip = [&](const QString& glyph, const QString& label, const QColor& colour) {
        QFont lf = font();
        lf.setPixelSize(20);
        p.setFont(lf);
        const int tw = QFontMetrics(lf).horizontalAdvance(label);
        x -= tw;
        p.setPen(colour);
        p.drawText(QRectF(x, by - 14, tw, 28), Qt::AlignVCenter | Qt::AlignLeft, label);
        x -= 8 + 26;
        const QPixmap g = DeckTheme::ButtonGlyph(glyph, 26);
        p.drawPixmap(QPointF(x, by - 13), g);
        x -= 26;
    };
    chip(QStringLiteral("A"), confirm_label,
         destructive ? QColor(0xd9, 0x53, 0x4f) : DeckTheme::kText);
    if (modal != Modal::Notice) {
        chip(QStringLiteral("B"), tr("Cancel"), DeckTheme::kText);
    }
}

std::vector<DeckHint> DeckGameDetailPage::Hints() const {
    switch (modal) {
    case Modal::DeleteGame:
        return {{QStringLiteral("A"), tr("Delete")}, {QStringLiteral("B"), tr("Cancel")}};
    case Modal::RemoveUpdate:
    case Modal::RemoveDLC:
        return {{QStringLiteral("A"), tr("Remove")}, {QStringLiteral("B"), tr("Cancel")}};
    case Modal::Notice:
        return {{QStringLiteral("A"), tr("OK")}};
    case Modal::None:
        break;
    }
    return {{QStringLiteral("A"), tr("Select")}, {QStringLiteral("B"), tr("Back")}};
}
