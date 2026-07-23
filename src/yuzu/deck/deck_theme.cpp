// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPixmap>

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
        kAccent = QColor(0x00, 0xa8, 0xce);
        kAccentGlow = QColor(0x37, 0xd0, 0xf0);
        kAccentSoft = QColor(0xdd, 0xf3, 0xf9);
        kText = QColor(0x1f, 0x1f, 0x21);
        kTextDim = QColor(0x74, 0x76, 0x7a);
        kPlaceholder = QColor(0xdd, 0xdd, 0xdd);
        kPlaceholderBorder = QColor(0xcb, 0xcb, 0xcb);
        kToggleOff = QColor(0xc2, 0xc4, 0xc8);
    } else {
        // "Basic Black" — dark theme.
        kBackground = QColor(0x2b, 0x2b, 0x2b);
        kBar = QColor(0x22, 0x22, 0x22);
        kSurface = QColor(0x3a, 0x3a, 0x3a);
        kDivider = QColor(0x45, 0x45, 0x45);
        kAccent = QColor(0x00, 0xc3, 0xe3);
        kAccentGlow = QColor(0x2c, 0xe6, 0xff);
        kAccentSoft = QColor(0x12, 0x33, 0x3a);
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

QString StyleSheet() {
    // Scoped by object name so it only affects Big Picture widgets and never leaks into the
    // desktop UI.
    return QStringLiteral(R"(
#DeckShell {
    background-color: %1;
}
#DeckShell QWidget {
    color: %2;
    background-color: %1;
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

QPixmap ButtonGlyph(const QString& label, int diameter) {
    // Render at 2x for crisp edges on HiDPI, then tag the device pixel ratio.
    constexpr qreal scale = 2.0;
    const int px = static_cast<int>(diameter * scale);
    QPixmap pixmap(px, px);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

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
