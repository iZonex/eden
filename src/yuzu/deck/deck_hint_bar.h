// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>
#include <QString>
#include <QWidget>

namespace Core::HID {
class HIDCore;
}

class QHBoxLayout;
class DeckPlayModeIndicator;

/// A single button hint: a Switch-style button glyph followed by a short action label.
///
/// Order matters: the bar lays hints out left to right in the order given, and the console always
/// puts the confirm button LAST, hard against the right edge. List A (or B, where it confirms) at
/// the end of the vector.
struct DeckHint {
    QString glyph;  ///< Button letter, e.g. "A", "B", "X", "Y", "L", "R".
    QString action; ///< What the button does here, e.g. "OK".
    bool dim = false; ///< The button is advertised but inert here (e.g. A on an empty home slot).
};

/**
 * The persistent bottom bar: the play-mode indicator on the left, and the current screen's button
 * hints on the right. Pages call SetHints() when their controls change.
 */
class DeckHintBar : public QWidget {
    Q_OBJECT

public:
    explicit DeckHintBar(Core::HID::HIDCore& hid_core, QWidget* parent = nullptr);
    ~DeckHintBar() override;

    void SetHints(const std::vector<DeckHint>& hints);

protected:
    void paintEvent(QPaintEvent* event) override; ///< paint the page-coloured ground under the hints

private:
    QHBoxLayout* row = nullptr;
    DeckPlayModeIndicator* play_mode = nullptr;
};
