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

signals:
    void Accepted(QString text);
    void Cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString title;
    QString text;
    int row = 0;
    int col = 0;
    std::vector<QString> rows; ///< character rows; the last row is the [space | ⌫ | ✓] control row
};
