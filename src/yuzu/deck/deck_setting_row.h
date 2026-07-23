// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <QFrame>
#include <QString>

namespace ConfigurationShared {
class Widget;
}

/**
 * One Switch-style settings row. It paints itself — the setting name on the left, its value (text
 * with ‹ › arrows, or a toggle pill) on the right, a thin divider below, and a bright cursor fill
 * when focused. The actual value lives in a hidden ConfigurationShared::Widget (built from the real
 * settings system); this row drives that control on Left/Right/A and reads it back to paint. The
 * control's apply closure (registered with the page's apply list) writes the setting on save.
 */
class DeckSettingRow : public QFrame {
    Q_OBJECT

public:
    DeckSettingRow(ConfigurationShared::Widget* source, QString label, QString description,
                   QWidget* parent = nullptr);

    void SetFocused(bool focused);
    void Adjust(int delta); ///< Left/Right: step the value.
    void Activate();        ///< A: toggle a bool / cycle an enum.

    /// Called when the row is tapped (touch or mouse).
    std::function<void()> on_tapped;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    bool IsToggle() const;
    bool ToggleOn() const;
    QString ValueText() const;

    ConfigurationShared::Widget* source;
    QString label;
    QString description; ///< short info line shown under the name (Switch-style), if any
    bool focused = false;
};
