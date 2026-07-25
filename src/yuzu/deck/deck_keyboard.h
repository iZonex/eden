// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QString>
#include <QWidget>

/**
 * A small on-screen keyboard driven entirely by the gamepad, for the console shell's text entry
 * (naming Groups, etc.). Arrows move the key cursor, A types the highlighted key, B backspaces, +
 * (Start) confirms. It draws itself as a centred panel over a dimmed backdrop; the owning page shows
 * it and forwards navigation while it is up.
 */
class DeckKeyboard : public QWidget {
    Q_OBJECT

public:
    explicit DeckKeyboard(QWidget* parent = nullptr);

    void Start(const QString& title, const QString& initial);
    QString Text() const {
        return text;
    }

    void MoveCursor(int d_row, int d_col);
    void PressKey();  ///< type the highlighted key (or run its action)
    void Backspace(); ///< delete the last character
    void Accept();    ///< confirm the current text
    void Cancel();    ///< dismiss without confirming
    void ToggleShift(); ///< Shift (stick-click / L) — one-shot capitalisation

signals:
    void Accepted(QString text);
    void Cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// One key in the on-screen layout. Wide keys (Space, ⌫, OK, Shift) carry a weight > 1.
    struct Key {
        enum class Kind { Char, Backspace, Shift, Symbols, Space, Ok };
        QString label;        ///< glyph/label drawn on the key
        QString value;        ///< inserted text (Char keys only)
        Kind kind = Kind::Char;
        int weight = 1;       ///< relative width within the row
    };

    void Rebuild();           ///< regenerate `keys` for the current shift/symbols state
    const Key* CurrentKey() const;

    QString title;
    QString text;
    int row = 0;
    int col = 0;
    bool shifted = false;     ///< letters typed uppercase until the next character
    bool symbols = false;     ///< symbol/number layout instead of letters
    std::vector<std::vector<Key>> keys; ///< current layout (rebuilt on shift/symbol changes)
};
