// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include "yuzu/deck/deck_keyboard.h"
#include "yuzu/deck/deck_theme.h"

namespace {
// The last row holds three wide control keys. Plain ASCII labels so they render in any font (unicode
// ⌫/✓ can fall back to tofu boxes, which looked broken).
const QString kSpaceKey = QStringLiteral("Space");
const QString kBackKey = QStringLiteral("Delete");
const QString kDoneKey = QStringLiteral("Done");
constexpr int kMaxLen = 32;
} // namespace

DeckKeyboard::DeckKeyboard(QWidget* parent) : QWidget(parent) {
    rows = {
        QStringLiteral("1234567890"),
        QStringLiteral("QWERTYUIOP"),
        QStringLiteral("ASDFGHJKL"),
        QStringLiteral("ZXCVBNM"),
        QString(),  // control row, handled specially
    };
    setVisible(false);
}

void DeckKeyboard::Start(const QString& title_, const QString& initial) {
    title = title_;
    text = initial.left(kMaxLen);
    row = 1;
    col = 0;
    if (parentWidget() != nullptr) {
        setGeometry(parentWidget()->rect()); // cover the whole page, even before a resize event
    }
    setVisible(true);
    raise();
    update();
}

void DeckKeyboard::MoveCursor(int d_row, int d_col) {
    const int n_rows = static_cast<int>(rows.size());
    row = std::clamp(row + d_row, 0, n_rows - 1);
    const bool control = row == n_rows - 1;
    const int len = control ? 3 : static_cast<int>(rows[row].size());
    col = std::clamp(col + d_col, 0, len - 1);
    update();
}

void DeckKeyboard::PressKey() {
    const int n_rows = static_cast<int>(rows.size());
    if (row == n_rows - 1) {
        // Control row: Space / Backspace / Done.
        if (col == 0) {
            if (text.size() < kMaxLen) {
                text.append(QLatin1Char(' '));
            }
        } else if (col == 1) {
            Backspace();
        } else {
            Accept();
        }
    } else if (col < static_cast<int>(rows[row].size()) && text.size() < kMaxLen) {
        text.append(rows[row].at(col));
    }
    update();
}

void DeckKeyboard::Backspace() {
    if (!text.isEmpty()) {
        text.chop(1);
    }
    update();
}

void DeckKeyboard::Accept() {
    if (text.trimmed().isEmpty()) {
        return; // don't accept an empty name
    }
    setVisible(false);
    emit Accepted(text.trimmed());
}

void DeckKeyboard::Cancel() {
    setVisible(false);
    emit Cancelled();
}

void DeckKeyboard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Dim the screen behind the panel.
    p.fillRect(rect(), QColor(0, 0, 0, 130));

    // Centred panel.
    const int pw = 760;
    const int ph = 440;
    const QRect panel((width() - pw) / 2, (height() - ph) / 2, pw, ph);
    QPainterPath panel_path;
    panel_path.addRoundedRect(panel, 18, 18);
    p.fillPath(panel_path, DeckTheme::kSurface);

    QFont f = font();
    // Title.
    f.setPixelSize(24);
    p.setFont(f);
    p.setPen(DeckTheme::kTextDim);
    p.drawText(QRect(panel.left() + 30, panel.top() + 20, pw - 60, 30), Qt::AlignLeft, title);

    // Text field.
    const QRect field(panel.left() + 30, panel.top() + 58, pw - 60, 48);
    QPainterPath field_path;
    field_path.addRoundedRect(field, 8, 8);
    p.fillPath(field_path, DeckTheme::kBackground);
    f.setPixelSize(26);
    p.setFont(f);
    p.setPen(DeckTheme::kText);
    p.save();
    p.setClipRect(field.adjusted(8, 0, -8, 0)); // never let a long name spill past the field
    p.drawText(field.adjusted(14, 0, -14, 0), Qt::AlignVCenter | Qt::AlignLeft,
               text + QStringLiteral("|"));
    p.restore();

    // Keys.
    const int n_rows = static_cast<int>(rows.size());
    const int key_top = panel.top() + 128;
    const int key_h = 52;
    const int key_gap = 8;
    const int grid_left = panel.left() + 30;
    const int grid_w = pw - 60;
    const int key_w = (grid_w - 9 * key_gap) / 10; // sized to the widest (10-key) row

    f.setPixelSize(24);
    p.setFont(f);
    for (int r = 0; r < n_rows; ++r) {
        const int y = key_top + r * (key_h + key_gap);
        if (r == n_rows - 1) {
            // Control row: three wide keys spanning the grid.
            const QString labels[3] = {kSpaceKey, kBackKey, kDoneKey};
            const int widths[3] = {grid_w / 2, grid_w / 4 - key_gap, grid_w / 4 - key_gap};
            int x = grid_left;
            for (int c = 0; c < 3; ++c) {
                const QRect kr(x, y, widths[c], key_h);
                const bool sel = row == r && col == c;
                QPainterPath kp;
                kp.addRoundedRect(kr, 8, 8);
                p.fillPath(kp, sel ? DeckTheme::kAccent : DeckTheme::kBackground);
                p.setPen(sel ? DeckTheme::kSurface : DeckTheme::kText);
                p.drawText(kr, Qt::AlignCenter, labels[c]);
                x += widths[c] + key_gap;
            }
        } else {
            const QString& chars = rows[r];
            for (int c = 0; c < chars.size(); ++c) {
                const QRect kr(grid_left + c * (key_w + key_gap), y, key_w, key_h);
                const bool sel = row == r && col == c;
                QPainterPath kp;
                kp.addRoundedRect(kr, 8, 8);
                p.fillPath(kp, sel ? DeckTheme::kAccent : DeckTheme::kBackground);
                p.setPen(sel ? DeckTheme::kSurface : DeckTheme::kText);
                p.drawText(kr, Qt::AlignCenter, QString(chars.at(c)));
            }
        }
    }

    // Hint.
    f.setPixelSize(18);
    p.setFont(f);
    p.setPen(DeckTheme::kTextDim);
    p.drawText(QRect(panel.left() + 30, panel.bottom() - 34, pw - 60, 24), Qt::AlignHCenter,
               QStringLiteral("A: type    B: backspace    +: done"));
}
