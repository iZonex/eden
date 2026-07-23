// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include "common/param_package.h"
#include "common/settings_input.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "yuzu/deck/deck_hint_bar.h"
#include "yuzu/deck/deck_theme.h"

/// Live badges for the connected players, bottom-left. One player-colored rounded square per active
/// player (with a gamepad glyph and its player number). It counts the emulated controllers that are
/// actually connected with a real gamepad binding — NOT raw SDL devices — because Steam Input on the
/// Deck exposes the built-in as an extra phantom pad, so a device count would read "2" for a single
/// player. Plain QWidget (no signals) so it needs no MOC; polls on a timer.
class DeckControllerIndicator : public QWidget {
public:
    explicit DeckControllerIndicator(Core::HID::HIDCore& hid_core_, QWidget* parent = nullptr)
        : QWidget(parent), hid_core{hid_core_} {
        setFixedHeight(DeckTheme::kHintBarHeight);
        setMinimumWidth(48);
        glyph = DeckTheme::Icon(QStringLiteral("controllers"), kBadge - 12);
        timer = new QTimer(this);
        timer->setInterval(800);
        connect(timer, &QTimer::timeout, this, [this] { Refresh(); });
        timer->start();
        Refresh();
    }

protected:
    static constexpr int kBadge = 34;
    static constexpr int kGap = 8;

    void Refresh() {
        int n = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            const auto* c = hid_core.GetEmulatedControllerByIndex(i);
            if (c == nullptr || !c->IsConnected()) {
                continue;
            }
            const std::string engine =
                c->GetButtonParam(Settings::NativeButton::A).Get("engine", "");
            if (engine.empty() || engine == "keyboard" || engine == "mouse") {
                continue; // not driven by a real gamepad
            }
            ++n;
        }
        if (n != count) {
            count = n;
            setFixedWidth(qMax(48, 24 + count * (kBadge + kGap)));
            update();
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        // No background of our own — the glyph sits flat on the page (autoFillBackground handles it).
        qreal x = 24;
        const qreal y = (height() - kBadge) / 2.0;
        for (int i = 0; i < count; ++i) {
            const QRectF badge(x, y, kBadge, kBadge);
            // A plain white controller glyph per connected player — no coloured square behind it,
            // like the Switch's clean bottom status cluster.
            const int g = kBadge - 12;
            p.drawPixmap(QPointF(badge.center().x() - g / 2.0, badge.top() + 1), glyph);

            // A small dim player number under the glyph, so it's clear which controller is which.
            QFont f = font();
            f.setPixelSize(11);
            f.setBold(false);
            p.setFont(f);
            p.setPen(DeckTheme::kTextDim);
            p.drawText(QRectF(badge.left(), badge.bottom() - 13, badge.width(), 12),
                       Qt::AlignHCenter, QStringLiteral("P%1").arg(i + 1));
            x += kBadge + kGap;
        }
    }

private:
    Core::HID::HIDCore& hid_core;
    QTimer* timer = nullptr;
    QPixmap glyph;
    int count = -1;
};

DeckHintBar::DeckHintBar(Core::HID::HIDCore& hid_core, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("DeckHintBar"));
    setFixedHeight(DeckTheme::kHintBarHeight);

    row = new QHBoxLayout(this);
    row->setContentsMargins(16, 0, 24, 0);
    row->setSpacing(22);

    controllers = new DeckControllerIndicator(hid_core, this);
    row->addWidget(controllers);
    row->addStretch();
}

DeckHintBar::~DeckHintBar() = default;

void DeckHintBar::SetHints(const std::vector<DeckHint>& hints) {
    // Rebuild the hint cells (everything after the controller indicator + stretch).
    while (row->count() > 2) {
        QLayoutItem* item = row->takeAt(row->count() - 1);
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    // The button glyph is no bigger than the hint text (~text cap height), not an oversized badge.
    const int glyph_px = 24;
    for (const auto& hint : hints) {
        auto* cell = new QWidget(this);
        auto* cell_row = new QHBoxLayout(cell);
        cell_row->setContentsMargins(0, 0, 0, 0);
        cell_row->setSpacing(8);

        auto* glyph = new QLabel(cell);
        glyph->setPixmap(DeckTheme::ButtonGlyph(hint.glyph, glyph_px));
        glyph->setFixedSize(glyph_px, glyph_px);
        cell_row->addWidget(glyph);

        auto* text = new QLabel(hint.action, cell);
        text->setStyleSheet(
            QStringLiteral("font-size: 23px; color: %1;").arg(DeckTheme::kText.name()));
        cell_row->addWidget(text);

        row->addWidget(cell);
    }
}
