// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <QConicalGradient>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QRadialGradient>
#include <QWidget>

#include "yuzu/deck/deck_theme.h"

namespace DeckTheme {

namespace {
bool g_light = false;
} // namespace

void SetLightMode(bool light) {
    g_light = light;
    if (light) {
        // "Basic White" — the Switch 2 default: near-white body, white cards, dark text.
        kBackground = QColor(0xeb, 0xeb, 0xeb);
        kBar = QColor(0xf6, 0xf6, 0xf6);
        kSurface = QColor(0xff, 0xff, 0xff);
        kDivider = QColor(0xd6, 0xd6, 0xd6);
        kAccent = QColor(0x2f, 0x6c, 0xb5); // Switch royal blue, sampled from the reference HOME/Settings
        kAccentGlow = QColor(0x6f, 0x9f, 0xd8);
        kAccentSoft = QColor(0xe2, 0xec, 0xf8);
        kText = QColor(0x1f, 0x1f, 0x21);
        kTextDim = QColor(0x74, 0x76, 0x7a);
        // Lighter than the ground, and borderless on the home row: on the console an empty slot is a
        // pale seat, not a grey hole. Darker-than-the-page slots (which is what these used to be) read
        // as punched-out gaps and made a short library look broken rather than merely short.
        kPlaceholder = QColor(0xf5, 0xf5, 0xf5);
        kPlaceholderBorder = QColor(0xd2, 0xd2, 0xd2);
        kToggleOff = QColor(0xc2, 0xc4, 0xc8);
    } else {
        // "Basic Black" — dark theme.
        kBackground = QColor(0x2b, 0x2b, 0x2b);
        kBar = QColor(0x22, 0x22, 0x22);
        kSurface = QColor(0x3a, 0x3a, 0x3a);
        kDivider = QColor(0x45, 0x45, 0x45);
        kAccent = QColor(0x6a, 0xb4, 0xff); // Switch royal blue (lighter for the dark theme)
        kAccentGlow = QColor(0x9f, 0xc8, 0xff);
        kAccentSoft = QColor(0x16, 0x30, 0x4f);
        kText = QColor(0xf2, 0xf2, 0xf2);
        kTextDim = QColor(0x9a, 0x9a, 0x9a);
        kPlaceholder = QColor(0x3d, 0x3d, 0x3d);
        kPlaceholderBorder = QColor(0x4c, 0x4c, 0x4c);
        kToggleOff = QColor(0x56, 0x56, 0x56);
    }
}

bool IsLightMode() {
    return g_light;
}

QColor PlayerColor(int index) {
    static const std::array<QColor, 8> colors{kPlayer1, kPlayer2, kPlayer3, kPlayer4,
                                              kPlayer5, kPlayer6, kPlayer7, kPlayer8};
    if (index < 0) {
        index = 0;
    }
    return colors[index % static_cast<int>(colors.size())];
}

QBrush SelectionSweep(const QPointF& center, int phase) {
    QConicalGradient sweep(center, static_cast<qreal>(phase));
    sweep.setColorAt(0.00, QColor(0x5b, 0x8f, 0xff));
    sweep.setColorAt(0.30, QColor(0xa9, 0x6c, 0xf0));
    sweep.setColorAt(0.55, QColor(0xff, 0x83, 0xc0));
    sweep.setColorAt(0.80, QColor(0x4f, 0xc8, 0xf0));
    sweep.setColorAt(1.00, QColor(0x5b, 0x8f, 0xff));
    return QBrush(sweep);
}

void PaintGround(QPainter& painter, const QWidget& widget) {
    painter.fillRect(widget.rect(), kBackground);
    if (!g_light) {
        return; // Basic Black is a flat ground on the console too
    }
    const QWidget* const top = widget.window();
    if (top == nullptr || top->width() <= 0 || top->height() <= 0) {
        return;
    }
    // Where the top-level's origin falls in OUR coordinates, so the wash below is laid out once for
    // the whole page and every widget simply draws the part of it that overlaps its own rect.
    const QPointF origin = -QPointF(widget.mapTo(top, QPoint(0, 0)));
    const qreal w = top->width();
    const qreal h = top->height();

    painter.save();
    painter.setPen(Qt::NoPen);
    // A gentle lift toward the foot of the page.
    QLinearGradient lift(origin + QPointF(0, h * 0.45), origin + QPointF(0, h));
    lift.setColorAt(0.0, QColor(0xff, 0xff, 0xff, 0));
    lift.setColorAt(1.0, QColor(0xff, 0xff, 0xff, 80));
    painter.fillRect(widget.rect(), lift);
    // Two faint spectral pools just past the bottom corners — the pearlescent sheen of Basic White.
    // Anchored below the screen edge so only their soft outer falloff is ever on screen.
    const auto pool = [&](qreal fx, qreal radius, const QColor& tint) {
        QRadialGradient g(origin + QPointF(w * fx, h * 1.02), w * radius);
        QColor edge = tint;
        edge.setAlpha(0);
        g.setColorAt(0.0, tint);
        g.setColorAt(1.0, edge);
        painter.fillRect(widget.rect(), g);
    };
    pool(0.20, 0.52, QColor(0xc9, 0x9a, 0xe8, 34)); // violet, left
    pool(0.82, 0.48, QColor(0x8f, 0xd4, 0xf0, 30)); // cyan, right
    painter.restore();
}

QString StyleSheet() {
    // Scoped by object name so it only affects Big Picture widgets and never leaks into the
    // desktop UI.
    //
    // The font family is named here rather than inherited: main.cpp sets the application font to
    // "MS Shell Dlg 2", a Windows alias that resolves on neither SteamOS nor macOS, so every label
    // in the shell was rendering in whatever the substitution table happened to pick — which came
    // out visibly heavier than the console's own book-weight UI text. Naming a real family (and the
    // weight with it) is what actually fixes that; asking for weight 400 alone cannot, because the
    // substituted face was the wrong one to begin with.
    return QStringLiteral(R"(
#DeckShell {
    background-color: %1;
}
#DeckShell QWidget {
    color: %2;
    background-color: %1;
    font-family: "Noto Sans", "SF Pro Text", "Helvetica Neue", "DejaVu Sans", sans-serif;
    font-weight: 400;
    font-size: 18px;
}
#DeckShell QScrollArea, #DeckShell QAbstractScrollArea, #DeckShell QStackedWidget {
    background-color: %1;
    border: none;
}
#DeckHeader {
    background-color: %3;
    border-bottom: 1px solid %4;
}
#DeckTab {
    color: %5;
    font-size: 22px;
    font-weight: 500;
    padding: 8px 24px;
    border: none;
    background: transparent;
}
#DeckTab[selected="true"] {
    color: %2;
    border-bottom: 3px solid %6;
}
#DeckHintBar {
    background-color: %1;
    border: none;
}
QScrollBar:vertical {
    background: transparent;
    width: 8px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: %4;
    border-radius: 4px;
    min-height: 40px;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
)")
        .arg(kBackground.name(), kText.name(), kBar.name(), kDivider.name(), kTextDim.name(),
             kAccent.name());
}

QPalette Palette() {
    QPalette p;
    p.setColor(QPalette::Window, kBackground);
    p.setColor(QPalette::Base, kBackground);
    p.setColor(QPalette::AlternateBase, kSurface);
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::Button, kSurface);
    p.setColor(QPalette::ButtonText, kText);
    p.setColor(QPalette::ToolTipBase, kSurface);
    p.setColor(QPalette::ToolTipText, kText);
    p.setColor(QPalette::PlaceholderText, kTextDim);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, kBackground);
    // Muted variants for disabled widgets so nothing renders as black-on-black.
    p.setColor(QPalette::Disabled, QPalette::WindowText, kTextDim);
    p.setColor(QPalette::Disabled, QPalette::Text, kTextDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, kTextDim);
    return p;
}

QPixmap ButtonGlyph(const QString& label, int diameter, qreal opacity) {
    // Render at 2x for crisp edges on HiDPI, then tag the device pixel ratio.
    constexpr qreal scale = 2.0;
    const int px = static_cast<int>(diameter * scale);
    QPixmap pixmap(px, px);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(std::clamp(opacity, 0.0, 1.0));

    // Switch-style button glyph: a filled circle with a contrasting letter. On the dark theme the
    // Switch uses a light circle + dark letter; on the light theme, a dark circle + light letter.
    const bool light = IsLightMode();
    painter.setPen(Qt::NoPen);
    painter.setBrush(light ? QColor(0x3a, 0x3a, 0x3c) : QColor(0xd0, 0xd0, 0xd0));
    painter.drawEllipse(0, 0, px, px);

    QFont font;
    font.setPixelSize(static_cast<int>(px * 0.56));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(light ? QColor(0xf5, 0xf5, 0xf5) : QColor(0x22, 0x22, 0x22));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, label);
    painter.end();

    pixmap.setDevicePixelRatio(scale);
    return pixmap;
}

QPixmap Icon(const QString& name, int size, const QColor& color) {
    // Rasterise the SVG at the exact device resolution with QImageReader (the qrc aliases have no
    // ".svg" extension, so a plain QIcon renders them at the 24px viewBox and upscales — that's the
    // blurry, "raster" look). setScaledSize on the SVG image reader renders crisp at any size. Render
    // at 2x for supersampled edges, tag the DPR so callers draw at the logical `size`.
    constexpr qreal dpr = 2.0;
    const int px = std::max(1, static_cast<int>(size * dpr));
    QImageReader reader(QStringLiteral(":/deck/%1").arg(name));
    reader.setFormat("svg");
    reader.setScaledSize(QSize(px, px));
    QImage img = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // The bundled SVGs are white-filled; tint them to `color` (keeps the anti-aliased edges).
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(img.rect(), color);
    p.end();
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

} // namespace DeckTheme
