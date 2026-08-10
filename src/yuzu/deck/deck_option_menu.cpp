// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QPainter>
#include <QPainterPath>

#include "yuzu/deck/deck_option_menu.h"
#include "yuzu/deck/deck_theme.h"

namespace {
constexpr int kPanelW = 420;
constexpr int kRowH = 56;
constexpr int kHeaderH = 40;
constexpr int kTopPad = 96;
constexpr int kSidePad = 24;
} // namespace

DeckOptionMenu::DeckOptionMenu(QWidget* parent) : QWidget(parent) {
    // Keep the shell's "fill every widget with the window colour" pass off this one: the dim wash
    // in paintEvent only works if what is behind it still shows through.
    setProperty("deckTranslucent", true);
    setAutoFillBackground(false);
    hide();
}

void DeckOptionMenu::Open(const QString& title_, Mode mode_, std::vector<Item> items_,
                          int current_id) {
    title = title_;
    mode = mode_;
    items = std::move(items_);

    cursor = 0;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (!items[i].is_header && items[i].id == current_id) {
            cursor = i;
            break;
        }
    }
    if (!items.empty() && items[cursor].is_header) {
        cursor = FirstSelectable(cursor, 1);
    }

    if (parentWidget() != nullptr) {
        setGeometry(parentWidget()->rect());
    }
    show();
    raise();
    update();
}

void DeckOptionMenu::Close() {
    hide();
    emit Closed();
}

int DeckOptionMenu::FirstSelectable(int from, int step) const {
    const int n = static_cast<int>(items.size());
    for (int i = from; i >= 0 && i < n; i += step) {
        if (!items[i].is_header) {
            return i;
        }
    }
    return from;
}

void DeckOptionMenu::MoveCursor(int delta) {
    if (items.empty() || delta == 0) {
        return;
    }
    const int step = delta > 0 ? 1 : -1;
    int next = cursor;
    for (int moved = 0; moved < std::abs(delta); ++moved) {
        int candidate = next + step;
        // Headers are captions, not choices — step over them so the cursor never lands on one.
        while (candidate >= 0 && candidate < static_cast<int>(items.size()) &&
               items[candidate].is_header) {
            candidate += step;
        }
        if (candidate < 0 || candidate >= static_cast<int>(items.size())) {
            break;
        }
        next = candidate;
    }
    cursor = next;
    update();
}

void DeckOptionMenu::Activate() {
    if (items.empty() || cursor < 0 || cursor >= static_cast<int>(items.size())) {
        return;
    }
    const Item& item = items[cursor];
    if (item.is_header) {
        return;
    }
    if (mode == Mode::Radio) {
        for (Item& other : items) {
            other.checked = (&other == &item);
        }
        const int id = item.id;
        update();
        emit Picked(id);
        Close();
        return;
    }
    items[cursor].checked = !items[cursor].checked;
    update();
    emit Toggled(item.id, items[cursor].checked);
}

void DeckOptionMenu::SetChecked(int id, bool checked) {
    for (Item& item : items) {
        if (!item.is_header && item.id == id) {
            item.checked = checked;
        }
    }
    update();
}

void DeckOptionMenu::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Dim the page behind, so the grid reads as "still there, not in play".
    p.fillRect(rect(), QColor(0, 0, 0, 110));

    // The card comes in from the left, under the funnel and sort icons the menu was opened from.
    const QRectF panel(0, 0, kPanelW, height());
    p.fillRect(panel, DeckTheme::kSurface);

    QFont tf = font();
    tf.setPixelSize(26);
    p.setFont(tf);
    p.setPen(DeckTheme::kText);
    p.drawText(QRectF(kSidePad, 36, kPanelW - 2 * kSidePad, 34), Qt::AlignLeft | Qt::AlignVCenter,
               title);
    QColor rule = DeckTheme::kText;
    rule.setAlpha(40);
    p.setPen(QPen(rule, 1));
    p.drawLine(QPointF(kSidePad, 78), QPointF(kPanelW - kSidePad, 78));

    qreal y = kTopPad;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const Item& item = items[i];
        if (item.is_header) {
            QFont hf = font();
            hf.setPixelSize(16);
            p.setFont(hf);
            p.setPen(DeckTheme::kTextDim);
            p.drawText(QRectF(kSidePad + 4, y, kPanelW - 2 * kSidePad, kHeaderH),
                       Qt::AlignLeft | Qt::AlignBottom, item.label.toUpper());
            y += kHeaderH;
            continue;
        }

        const QRectF row(kSidePad - 8, y, kPanelW - 2 * kSidePad + 16, kRowH);
        const QRectF box = row.adjusted(0, 4, 0, -4);
        if (i == cursor) {
            QPainterPath fp;
            fp.addRoundedRect(box, 12, 12);
            p.fillPath(fp, DeckTheme::kAccentSoft);
            p.setPen(QPen(DeckTheme::kAccent, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(box, 12, 12);
        }

        QFont lf = font();
        lf.setPixelSize(20);
        p.setFont(lf);
        p.setPen(DeckTheme::kText);
        p.drawText(QRectF(row.left() + 16, row.top(), row.width() - 70, row.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(item.label, Qt::ElideRight,
                                              static_cast<int>(row.width() - 70)));

        // Radio: a filled dot on the option in effect. Check: a tick, so several can be on at once.
        const QPointF c(row.right() - 28, row.center().y());
        if (mode == Mode::Radio) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(item.checked ? DeckTheme::kAccent : DeckTheme::kToggleOff, 2));
            p.drawEllipse(c, 11, 11);
            if (item.checked) {
                p.setPen(Qt::NoPen);
                p.setBrush(DeckTheme::kAccent);
                p.drawEllipse(c, 6, 6);
            }
        } else {
            const QRectF tick(c.x() - 11, c.y() - 11, 22, 22);
            p.setBrush(item.checked ? DeckTheme::kAccent : QBrush(Qt::NoBrush));
            p.setPen(QPen(item.checked ? DeckTheme::kAccent : DeckTheme::kToggleOff, 2));
            p.drawRoundedRect(tick, 6, 6);
            if (item.checked) {
                p.setPen(QPen(DeckTheme::kSurface, 2.6, Qt::SolidLine, Qt::RoundCap,
                              Qt::RoundJoin));
                QPainterPath check;
                check.moveTo(tick.left() + tick.width() * 0.26, tick.top() + tick.height() * 0.52);
                check.lineTo(tick.left() + tick.width() * 0.44, tick.top() + tick.height() * 0.72);
                check.lineTo(tick.left() + tick.width() * 0.76, tick.top() + tick.height() * 0.30);
                p.setBrush(Qt::NoBrush);
                p.drawPath(check);
            }
        }
        y += kRowH;
    }
}
