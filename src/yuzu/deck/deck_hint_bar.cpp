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

/// The play-mode indicator, bottom-left — the console's own outline, exactly as the HOME screen
/// shows it. Extra pads only earn their own badge once a SECOND one is connected: on a Deck playing
/// solo (the overwhelmingly common case) the corner is the bare console glyph like the reference,
/// and the per-player badges appear only when the count is something the user might want to check.
///
/// It counts the emulated controllers that are actually connected with a real gamepad binding — NOT
/// raw SDL devices — because Steam Input on the Deck exposes the built-in as an extra phantom pad,
/// so a device count would read "2" for a single player. Plain QWidget (no signals) so it needs no
/// MOC; polls on a timer.
class DeckPlayModeIndicator : public QWidget {
public:
    explicit DeckPlayModeIndicator(Core::HID::HIDCore& hid_core_, QWidget* parent = nullptr)
        : QWidget(parent), hid_core{hid_core_} {
        setFixedHeight(DeckTheme::kHintBarHeight);
        setMinimumWidth(48);
        Retint();
        timer = new QTimer(this);
        timer->setInterval(800);
        connect(timer, &QTimer::timeout, this, [this] { Refresh(); });
        timer->start();
        Refresh();
    }

protected:
    static constexpr int kBadge = 34;
    static constexpr int kGap = 8;
    static constexpr int kConsole = 30; ///< the handheld glyph itself

    /// Re-render the glyphs in the current theme's ink. They are tinted at build time, so without
    /// this a theme flip leaves dark icons on the dark ground (and vice versa).
    void Retint() {
        tinted_light = DeckTheme::IsLightMode();
        glyph = DeckTheme::Icon(QStringLiteral("controllers"), kBadge - 12, DeckTheme::kText);
        console = DeckTheme::Icon(QStringLiteral("console"), kConsole, DeckTheme::kText);
    }

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
            // One badge per pad, but only once there are at least two of them (see the class note).
            const int badges = count >= 2 ? count : 0;
            setFixedWidth(qMax(48, 24 + kConsole + badges * (kBadge + kGap)));
            update();
        }
    }

    void paintEvent(QPaintEvent*) override {
        if (tinted_light != DeckTheme::IsLightMode()) {
            Retint();
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        // No background of our own — the glyph sits flat on the page (autoFillBackground handles it).
        qreal x = 24;
        // The console itself, always: this corner says what you are playing ON, not how many pads
        // happen to be paired.
        p.drawPixmap(QPointF(x, (height() - kConsole) / 2.0), console);
        x += kConsole + kGap;
        if (count < 2) {
            return;
        }
        const qreal y = (height() - kBadge) / 2.0;
        for (int i = 0; i < count; ++i) {
            const QRectF badge(x, y, kBadge, kBadge);
            // A plain controller glyph per connected player — no coloured square behind it, like the
            // console's clean bottom status cluster.
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
    QPixmap console;
    bool tinted_light = false; ///< theme the glyphs above were rendered for
    int count = -1;
};

DeckHintBar::DeckHintBar(Core::HID::HIDCore& hid_core, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("DeckHintBar"));
    setFixedHeight(DeckTheme::kHintBarHeight);

    row = new QHBoxLayout(this);
    row->setContentsMargins(16, 0, 24, 0);
    row->setSpacing(22);

    play_mode = new DeckPlayModeIndicator(hid_core, this);
    row->addWidget(play_mode);
    row->addStretch();
}

DeckHintBar::~DeckHintBar() = default;

void DeckHintBar::paintEvent(QPaintEvent*) {
    // Paint the page-coloured ground explicitly so the bar never inherits a stale (light) palette —
    // the hints sit flat on the page. PaintGround also carries the bottom of the pearlescent wash,
    // which is strongest exactly here, at the foot of the screen.
    QPainter p(this);
    DeckTheme::PaintGround(p, *this);
}

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

        // A dim hint is one the console still lists but cannot act on here — A while the cursor sits
        // on an empty home slot. It fades rather than disappearing, so the row of hints doesn't
        // reflow every time the cursor crosses a gap in the library.
        const qreal ink = hint.dim ? 0.38 : 1.0;
        auto* glyph = new QLabel(cell);
        glyph->setPixmap(DeckTheme::ButtonGlyph(hint.glyph, glyph_px, ink));
        glyph->setFixedSize(glyph_px, glyph_px);
        cell_row->addWidget(glyph);

        // Blend towards the ground rather than emitting an alpha colour: Qt's stylesheet parser is
        // fussy about the alpha argument's units, and the result here is identical because the bar's
        // ground is exactly what a translucent ink would show through to.
        const QColor fg = DeckTheme::kText;
        const QColor bg = DeckTheme::kBackground;
        const QColor faded(qRound(bg.red() + (fg.red() - bg.red()) * ink),
                           qRound(bg.green() + (fg.green() - bg.green()) * ink),
                           qRound(bg.blue() + (fg.blue() - bg.blue()) * ink));
        // Weight pinned to regular so the row cannot inherit a heavier face from the shell stylesheet.
        auto* text = new QLabel(hint.action, cell);
        text->setStyleSheet(QStringLiteral("font-size: 23px; font-weight: 400; color: %1;")
                                .arg(faded.name()));
        cell_row->addWidget(text);

        row->addWidget(cell);
    }
}
