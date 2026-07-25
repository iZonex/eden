// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include "yuzu/deck/deck_keyboard.h"
#include "yuzu/deck/deck_theme.h"

namespace {
constexpr int kMaxLen = 32;
} // namespace

DeckKeyboard::DeckKeyboard(QWidget* parent) : QWidget(parent) {
    Rebuild();
    setVisible(false);
}

void DeckKeyboard::Rebuild() {
    using Kind = Key::Kind;
    keys.clear();

    const auto char_row = [](const QString& s) {
        std::vector<Key> v;
        for (const QChar c : s) {
            v.push_back({QString(c), QString(c), Kind::Char, 1});
        }
        return v;
    };

    if (!symbols) {
        auto r0 = char_row(QStringLiteral("1234567890-"));
        r0.push_back({QStringLiteral("⌫"), {}, Kind::Backspace, 2}); // backspace
        keys.push_back(std::move(r0));
        keys.push_back(char_row(shifted ? QStringLiteral("QWERTYUIOP")
                                        : QStringLiteral("qwertyuiop")));
        keys.push_back(char_row(shifted ? QStringLiteral("ASDFGHJKL")
                                        : QStringLiteral("asdfghjkl")));
        auto r3 = char_row(shifted ? QStringLiteral("ZXCVBNM") : QStringLiteral("zxcvbnm"));
        for (const QChar c : QStringLiteral(",.?!")) {
            r3.push_back({QString(c), QString(c), Kind::Char, 1});
        }
        keys.push_back(std::move(r3));
    } else {
        auto r0 = char_row(QStringLiteral("1234567890"));
        r0.push_back({QStringLiteral("⌫"), {}, Kind::Backspace, 2});
        keys.push_back(std::move(r0));
        keys.push_back(char_row(QStringLiteral("-/:;()$&@\"")));
        keys.push_back(char_row(QStringLiteral(".,?!'`+=")));
        keys.push_back(char_row(QStringLiteral("#*_\\|<>~")));
    }

    // Function row: Shift | #+= (or ABC) | Space | OK — the wide keys, like the reference.
    std::vector<Key> fr;
    fr.push_back({QStringLiteral("⇧"), {}, Kind::Shift, 2}); // shift
    fr.push_back({symbols ? QStringLiteral("ABC") : QStringLiteral("#+="), {}, Kind::Symbols, 2});
    fr.push_back({QStringLiteral("Space"), QStringLiteral(" "), Kind::Space, 8});
    fr.push_back({QStringLiteral("OK"), {}, Kind::Ok, 3});
    keys.push_back(std::move(fr));

    // Keep the cursor in range after a layout change.
    row = std::clamp(row, 0, static_cast<int>(keys.size()) - 1);
    col = std::clamp(col, 0, static_cast<int>(keys[row].size()) - 1);
}

void DeckKeyboard::Start(const QString& title_, const QString& initial) {
    title = title_;
    text = initial.left(kMaxLen);
    shifted = false;
    symbols = false;
    row = 1; // land on the top letter row
    col = 0;
    Rebuild();
    if (parentWidget() != nullptr) {
        setGeometry(parentWidget()->rect()); // cover the whole page, even before a resize event
    }
    setVisible(true);
    raise();
    update();
}

const DeckKeyboard::Key* DeckKeyboard::CurrentKey() const {
    if (row < 0 || row >= static_cast<int>(keys.size())) {
        return nullptr;
    }
    if (col < 0 || col >= static_cast<int>(keys[row].size())) {
        return nullptr;
    }
    return &keys[row][col];
}

void DeckKeyboard::MoveCursor(int d_row, int d_col) {
    const int n_rows = static_cast<int>(keys.size());
    if (d_row != 0) {
        // Preserve the horizontal position by weighted centre, so Up/Down lands on the key under the
        // cursor rather than snapping to the same index in a differently-sized row.
        qreal centre = 0.0, total = 0.0;
        for (int c = 0; c < static_cast<int>(keys[row].size()); ++c) {
            total += keys[row][c].weight;
        }
        qreal acc = 0.0;
        for (int c = 0; c <= col && c < static_cast<int>(keys[row].size()); ++c) {
            if (c == col) {
                centre = (acc + keys[row][c].weight / 2.0) / total;
            }
            acc += keys[row][c].weight;
        }
        row = std::clamp(row + d_row, 0, n_rows - 1);
        qreal rtotal = 0.0;
        for (const auto& k : keys[row]) {
            rtotal += k.weight;
        }
        qreal racc = 0.0;
        col = 0;
        for (int c = 0; c < static_cast<int>(keys[row].size()); ++c) {
            const qreal lo = racc / rtotal, hi = (racc + keys[row][c].weight) / rtotal;
            if (centre >= lo && centre < hi) {
                col = c;
                break;
            }
            racc += keys[row][c].weight;
            col = c;
        }
    }
    if (d_col != 0) {
        col = std::clamp(col + d_col, 0, static_cast<int>(keys[row].size()) - 1);
    }
    update();
}

void DeckKeyboard::PressKey() {
    const Key* k = CurrentKey();
    if (k == nullptr) {
        return;
    }
    switch (k->kind) {
    case Key::Kind::Char:
        if (text.size() < kMaxLen) {
            text.append(k->value);
        }
        if (shifted) { // one-shot capitalisation, like the Switch
            shifted = false;
            Rebuild();
        }
        break;
    case Key::Kind::Space:
        if (text.size() < kMaxLen) {
            text.append(QLatin1Char(' '));
        }
        break;
    case Key::Kind::Backspace:
        Backspace();
        break;
    case Key::Kind::Shift:
        shifted = !shifted;
        Rebuild();
        break;
    case Key::Kind::Symbols:
        symbols = !symbols;
        shifted = false;
        Rebuild();
        break;
    case Key::Kind::Ok:
        Accept();
        break;
    }
    update();
}

void DeckKeyboard::ToggleShift() {
    shifted = !shifted;
    Rebuild();
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
    const bool light = DeckTheme::IsLightMode();

    // Dim the page behind, then dock a full-width keyboard along the bottom (the Switch layout: the
    // prompt + text field sit on the dimmed page above, the keyboard fills the lower portion).
    p.fillRect(rect(), QColor(0, 0, 0, light ? 150 : 170));

    const int W = width(), H = height();
    const int margin = std::max(40, W / 24);

    // Prompt title.
    QFont f = font();
    f.setPixelSize(30);
    p.setFont(f);
    p.setPen(light ? QColor(0xf2, 0xf2, 0xf2) : QColor(0xf2, 0xf2, 0xf2));
    p.drawText(QRect(margin, static_cast<int>(H * 0.10), W - 2 * margin, 40), Qt::AlignLeft, title);

    // Text field: the value on a full-width underline, with an N/max counter at the right.
    const int field_y = static_cast<int>(H * 0.24);
    f.setPixelSize(34);
    p.setFont(f);
    p.setPen(QColor(0xff, 0xff, 0xff));
    p.save();
    p.setClipRect(QRect(margin, field_y - 40, W - 2 * margin, 50));
    p.drawText(QRect(margin, field_y - 40, W - 2 * margin, 50), Qt::AlignVCenter | Qt::AlignLeft,
               text + QStringLiteral("|"));
    p.restore();
    p.setPen(QColor(0xff, 0xff, 0xff, 200));
    p.drawLine(margin, field_y + 12, W - margin, field_y + 12);
    f.setPixelSize(20);
    p.setFont(f);
    p.setPen(QColor(0xff, 0xff, 0xff, 180));
    p.drawText(QRect(margin, field_y + 16, W - 2 * margin, 26), Qt::AlignRight,
               QStringLiteral("%1/%2").arg(text.size()).arg(kMaxLen));

    // Keyboard panel.
    const int panel_top = static_cast<int>(H * 0.40);
    QPainterPath panel;
    panel.addRoundedRect(QRectF(0, panel_top, W, H - panel_top), 24, 24);
    p.fillPath(panel, light ? QColor(0xe4, 0xe4, 0xe6) : QColor(0x2f, 0x2f, 0x31));

    // Key grid.
    const int pad_x = std::max(50, W / 18);
    const int grid_left = pad_x;
    const int grid_w = W - 2 * pad_x;
    const int grid_top = panel_top + 34;
    const int grid_bottom = H - std::max(60, H / 12);
    const int n_rows = static_cast<int>(keys.size());
    const int gap = 8;
    const int row_h = (grid_bottom - grid_top - (n_rows - 1) * gap) / n_rows;

    for (int r = 0; r < n_rows; ++r) {
        qreal wsum = 0.0;
        for (const auto& k : keys[r]) {
            wsum += k.weight;
        }
        const qreal unit = (grid_w - (static_cast<int>(keys[r].size()) - 1) * gap) / wsum;
        const int y = grid_top + r * (row_h + gap);
        qreal x = grid_left;
        for (int c = 0; c < static_cast<int>(keys[r].size()); ++c) {
            const Key& k = keys[r][c];
            const int kw = static_cast<int>(unit * k.weight);
            const QRectF kr(x, y, kw, row_h);
            const bool sel = (r == row && c == col);
            const bool is_ok = k.kind == Key::Kind::Ok;
            const bool active_toggle = (k.kind == Key::Kind::Shift && shifted) ||
                                       (k.kind == Key::Kind::Symbols && symbols);

            QPainterPath kp;
            kp.addRoundedRect(kr, 8, 8);
            QColor fill;
            if (is_ok) {
                fill = DeckTheme::kAccent; // OK is always the blue accent
            } else if (sel) {
                fill = DeckTheme::kAccent;
            } else if (active_toggle) {
                fill = DeckTheme::kAccentSoft;
            } else {
                fill = light ? QColor(0xff, 0xff, 0xff) : QColor(0x45, 0x45, 0x47);
            }
            p.fillPath(kp, fill);

            // Selected key gets the thin iridescent frame (matches the tile selection).
            if (sel) {
                QConicalGradient cg(kr.center(), 90);
                cg.setColorAt(0.00, QColor(0x5b, 0x8f, 0xff));
                cg.setColorAt(0.30, QColor(0xa9, 0x6c, 0xf0));
                cg.setColorAt(0.55, QColor(0xff, 0x83, 0xc0));
                cg.setColorAt(0.80, QColor(0x4f, 0xc8, 0xf0));
                cg.setColorAt(1.00, QColor(0x5b, 0x8f, 0xff));
                p.setPen(QPen(QBrush(cg), 3));
                p.setBrush(Qt::NoBrush);
                QPainterPath fr2;
                fr2.addRoundedRect(kr.adjusted(-2, -2, 2, 2), 10, 10);
                p.drawPath(fr2);
            }

            // Label.
            QColor text_color;
            if (is_ok || sel) {
                text_color = QColor(0xff, 0xff, 0xff);
            } else if (active_toggle) {
                text_color = DeckTheme::kAccent;
            } else {
                text_color = light ? QColor(0x1f, 0x1f, 0x21) : QColor(0xf2, 0xf2, 0xf2);
            }
            p.setPen(text_color);
            const bool wide_label = k.kind == Key::Kind::Space || k.kind == Key::Kind::Symbols ||
                                    is_ok;
            f.setPixelSize(wide_label ? 24 : 28);
            p.setFont(f);
            p.drawText(kr, Qt::AlignCenter, k.label);

            x += kw + gap;
        }
    }

    // Bottom control hints.
    f.setPixelSize(22);
    p.setFont(f);
    p.setPen(QColor(0xff, 0xff, 0xff, 200));
    p.drawText(QRect(margin, H - std::max(60, H / 12), W - 2 * margin, 30), Qt::AlignRight,
               QStringLiteral("Ⓡ Shift    Ⓧ Cancel    Ⓐ Select"));
}
