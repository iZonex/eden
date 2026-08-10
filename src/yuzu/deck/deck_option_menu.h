// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QString>
#include <QWidget>

/**
 * The console's sort / filter panel: a dimmed page with a card sliding in from the left, a title,
 * and a list of options with a mark on the ones in effect.
 *
 * Radio mode picks one option and closes; check mode toggles options and stays open, which is what
 * a filter needs — you usually set two or three at once. Purely gamepad-driven: the owning page
 * forwards navigation while the menu is visible, the same way it already does for the keyboard.
 */
class DeckOptionMenu : public QWidget {
    Q_OBJECT

public:
    struct Item {
        QString label;
        int id = 0;
        bool checked = false;
        bool is_header = false; ///< a non-selectable group caption ("Groups", "Format")
    };

    enum class Mode { Radio, Check };

    explicit DeckOptionMenu(QWidget* parent = nullptr);

    void Open(const QString& title, Mode mode, std::vector<Item> items, int current_id);
    void Close();

    void MoveCursor(int delta);
    void Activate(); ///< A on the focused row

    /// Refresh the marks without moving the cursor (a filter toggle changes what is checked).
    void SetChecked(int id, bool checked);

signals:
    void Picked(int id);              ///< radio mode: an option was chosen (menu then closes)
    void Toggled(int id, bool on);    ///< check mode: an option flipped
    void Closed();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int FirstSelectable(int from, int step) const;

    std::vector<Item> items;
    QString title;
    Mode mode = Mode::Radio;
    int cursor = 0;
};
