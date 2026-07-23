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
class DeckControllerIndicator;

/// A single button hint: a Switch-style button glyph followed by a short action label.
struct DeckHint {
    QString glyph;  ///< Button letter, e.g. "A", "B", "X", "Y", "L", "R".
    QString action; ///< What the button does here, e.g. "Play".
};

/**
 * The persistent bottom bar: a live indicator of the connected controllers on the left, and the
 * current screen's button hints on the right. Pages call SetHints() when their controls change.
 */
class DeckHintBar : public QWidget {
    Q_OBJECT

public:
    explicit DeckHintBar(Core::HID::HIDCore& hid_core, QWidget* parent = nullptr);
    ~DeckHintBar() override;

    void SetHints(const std::vector<DeckHint>& hints);

private:
    QHBoxLayout* row = nullptr;
    DeckControllerIndicator* controllers = nullptr;
};
