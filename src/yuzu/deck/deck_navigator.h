// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <QObject>
#include <Qt>

class QTimer;

namespace Core::HID {
class HIDCore;
}

/**
 * Reads every connected controller and turns it into high-level, console-style UI intents for
 * the Big Picture / Steam Deck front-end.
 *
 * Why this exists instead of ControllerNavigation: that class only listens to Player1 + Handheld,
 * fires a single event per stick threshold crossing (so holding a direction does nothing), and
 * only exposes arrows/Enter/Escape. A couch/handheld UI needs every pad to drive the cursor, an
 * autorepeat with acceleration when a direction is held, and the shoulder/trigger/face buttons
 * mapped to menu actions. This polls on the GUI thread from a QTimer (the same safe pattern
 * configure_hotkeys.cpp uses), OR-combining the state of all connected pads.
 *
 * It emits semantic signals; pages decide what each means. Directional intent is also surfaced
 * as Qt arrow keys so item views (QListView/QTreeView) keep their built-in cursor movement.
 */
class DeckNavigator : public QObject {
    Q_OBJECT

public:
    explicit DeckNavigator(Core::HID::HIDCore& hid_core, QObject* parent = nullptr);
    ~DeckNavigator() override;

    /// Starts/stops polling. While stopped, no signals are emitted and the button latch is cleared
    /// so re-enabling never fires a stale press.
    void SetActive(bool active);
    bool IsActive() const {
        return active;
    }

signals:
    /// A directional intent (Up/Down/Left/Right as Qt::Key_*), including autorepeat while held.
    void Navigate(Qt::Key key);
    /// A (South) — confirm / activate.
    void Accept();
    /// B (East) — cancel / go back.
    void Back();
    /// X (West) — primary context action (open game options, etc.).
    void PrimaryAction();
    /// Y (North) — secondary context action (toggle favorite, etc.).
    void SecondaryAction();
    /// L1 / R1 — switch between top-level sections.
    void TabPrev();
    void TabNext();
    /// L2 / R2 — coarse scroll (page up/down).
    void PageUp();
    void PageDown();
    /// Plus (Start) — open the main menu / primary button.
    void StartPressed();
    /// Minus (Select) — auxiliary (search / filter).
    void SelectPressed();

private:
    void Poll();

    // Bit indices into NpadButtonState::raw (see hid_core/hid_types.h).
    enum Button {
        BtnA = 0,
        BtnB = 1,
        BtnX = 2,
        BtnY = 3,
        BtnL = 6,
        BtnR = 7,
        BtnZL = 8,
        BtnZR = 9,
        BtnPlus = 10,
        BtnMinus = 11,
        BtnLeft = 12,
        BtnUp = 13,
        BtnRight = 14,
        BtnDown = 15,
        BtnStickLLeft = 16,
        BtnStickLUp = 17,
        BtnStickLRight = 18,
        BtnStickLDown = 19,
        NumTrackedButtons = 24,
    };

    /// OR-combined raw button state of every connected controller this tick.
    unsigned long long CollectButtons() const;

    Core::HID::HIDCore& hid_core;
    QTimer* timer = nullptr;
    bool active = false;

    // Edge-detection latch: a button fires once on the transition to pressed.
    std::array<bool, NumTrackedButtons> pressed{};

    // Directional autorepeat state. `dir` holds the currently-held Qt arrow key (0 if none),
    // `ticks_held` counts poll ticks since the press so we can add an initial delay then repeat
    // with acceleration.
    int held_dir = 0;
    int ticks_held = 0;
};
