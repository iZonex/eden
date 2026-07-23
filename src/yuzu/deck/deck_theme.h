// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

class QPixmap;

/**
 * Visual constants and helpers for the Big Picture / Steam Deck front-end. The look mirrors the
 * Nintendo Switch 2 DARK theme — a dark-grey ground, slightly lighter cards, hairline dividers, the
 * signature cyan accent for selection/toggles/values, and a cyan selection glow. Kept in one place
 * so every page shares the same palette, metrics and button glyphs.
 */
namespace DeckTheme {

// Two Nintendo-Switch-2 themes: "Basic Black" (dark) and "Basic White" (light, the console default).
// These are NOT const — SetLightMode() swaps every value so the whole console UI can flip theme live.
// Widgets read them at paint time, so a swap + re-apply (see DeckShell::ApplyTheme) restyles instantly.
inline QColor kBackground{0x2b, 0x2b, 0x2b}; // body
inline QColor kBar{0x22, 0x22, 0x22};        // top/bottom bars
inline QColor kSurface{0x3a, 0x3a, 0x3a};    // cards, panels
inline QColor kDivider{0x45, 0x45, 0x45};    // hairline dividers
inline QColor kAccent{0x00, 0xc3, 0xe3};     // Switch cyan (selection, toggles, radios, values)
inline QColor kAccentGlow{0x2c, 0xe6, 0xff};  // brighter cyan for the selection glow ring
inline QColor kAccentSoft{0x12, 0x33, 0x3a}; // focused-row / selection wash
inline QColor kText{0xf2, 0xf2, 0xf2};       // primary text
inline QColor kTextDim{0x9a, 0x9a, 0x9a};    // muted text
inline QColor kPlaceholder{0x3d, 0x3d, 0x3d}; // empty tile fill
inline QColor kPlaceholderBorder{0x4c, 0x4c, 0x4c};
inline QColor kToggleOff{0x56, 0x56, 0x56};

/// Switch the whole console palette between Basic Black (light=false) and Basic White (light=true).
/// Only mutates the colour globals; the caller re-applies the shell stylesheet/palette and repaints.
void SetLightMode(bool light);
/// True when the light ("Basic White") theme is active.
bool IsLightMode();

// Player colors for the controller slots (LED / pad tint), roughly the Switch player order.
inline const QColor kPlayer1{0xff, 0x5a, 0x3c};
inline const QColor kPlayer2{0x00, 0x9d, 0xdc};
inline const QColor kPlayer3{0x1a, 0xc6, 0x6d};
inline const QColor kPlayer4{0xf6, 0xb5, 0x00};
inline const QColor kPlayer5{0x9b, 0x5c, 0xf0};
inline const QColor kPlayer6{0xff, 0x7a, 0x2c};
inline const QColor kPlayer7{0x1f, 0xc0, 0xb0};
inline const QColor kPlayer8{0xff, 0x4c, 0x98};

/// The player tint for a given 0-based player index.
QColor PlayerColor(int index);

// Metrics (logical px; the Deck runs at 1280x800 @ 100% scale).
inline constexpr int kGridCardWidth = 272;
inline constexpr int kGridCardHeight = 272; // square box art
inline constexpr int kGridCardSpacing = 28;
inline constexpr int kGridCardMargin = 14;  // uniform margin around each tile inside its cell
                                            // (= half the gap between tiles + selection-border room)
inline constexpr int kGridLeadIndent = 82;  // empty space left of the FIRST tile (scrolls away), so
                                            // the rail starts after the avatar but reaches the edge
inline constexpr int kGridTitleHeight = 48;
inline constexpr int kHintBarHeight = 54;
inline constexpr int kHeaderHeight = 62;
inline constexpr int kCornerRadius = 10;

/// The application-wide stylesheet for Big Picture mode.
QString StyleSheet();

/// A dark QPalette for the whole Big Picture tree. Qt fills every widget's default background from
/// the palette (Window/Base) before paintEvent, so without this the standard containers stay white
/// while our painted widgets are dark — a grey/white mishmash. Apply on the shell so children inherit.
QPalette Palette();

/// Renders a Switch-style button glyph (e.g. "A", "B", "X", "Y", "L", "R"): a solid dark circle
/// with a white letter. `diameter` is the badge size in device-independent pixels.
QPixmap ButtonGlyph(const QString& label, int diameter);

/// Loads one of the bundled deck SVG icons (":/deck/<name>", white-filled) at the given pixel size,
/// HiDPI-aware, tinted to `color` (defaults to the theme text colour). `name` is an alias from
/// deck.qrc: "games", "controllers", "settings", "power", "search".
QPixmap Icon(const QString& name, int size, const QColor& color);
inline QPixmap Icon(const QString& name, int size) {
    return Icon(name, size, kText);
}

} // namespace DeckTheme
