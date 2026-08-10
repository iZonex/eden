// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <QObject>
#include <Qt>

#include "common/settings_enums.h"

class QTimer;

namespace Core::HID {
class HIDCore;
}

// Which letters the pad prints on its face buttons; see Settings::DeckFaceLayout for what each
// value means and why the drift exists at all.
using DeckFaceLayout = Settings::DeckFaceLayout;

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

    /// Which lettering the face buttons are read as. Affects ONLY this menu navigator — the pad's
    /// bindings, and therefore input inside games, are untouched.
    void SetFaceLayout(DeckFaceLayout layout);
    DeckFaceLayout FaceLayout() const {
        return face_layout;
    }

signals:
    /// A directional intent (Up/Down/Left/Right as Qt::Key_*), including autorepeat while held.
    void Navigate(Qt::Key key);
    /// The button printed "A" — confirm / activate.
    void Accept();
    /// The button printed "B" — cancel / go back.
    void Back();
    /// The button printed "X" — primary context action (sort, add games, …).
    void PrimaryAction();
    /// The button printed "Y" — secondary context action (filter, rename, …).
    void SecondaryAction();
    /// The detected face lettering changed (a pad was plugged in or removed under Auto).
    void FaceLayoutChanged();
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

    /// The npad bits behind the four printed letters, for the lettering currently in effect.
    struct FaceBits {
        int accept;    ///< "A"
        int back;      ///< "B"
        int primary;   ///< "X"
        int secondary; ///< "Y"
    };
    FaceBits Face() const;

    /// Under Auto, asks SDL what is plugged in and updates `resolved`. Emits FaceLayoutChanged when
    /// the answer differs from what we were using.
    void RefreshFaceLayout();

    /// OR-combined raw button state of every connected controller this tick.
    unsigned long long CollectButtons() const;

    Core::HID::HIDCore& hid_core;
    QTimer* timer = nullptr;
    bool active = false;

    DeckFaceLayout face_layout = DeckFaceLayout::Auto;
    // What Auto settled on. Defaults to the Deck's own controls: this shell exists for the Deck, and
    // when no pad is enumerated at all (keyboard-driven desktop testing) the choice is moot anyway.
    DeckFaceLayout resolved = DeckFaceLayout::SwapXY;
    int layout_poll_ticks = 0;

    // Edge-detection latch: a button fires once on the transition to pressed.
    std::array<bool, NumTrackedButtons> pressed{};

    // Directional autorepeat state. `dir` holds the currently-held Qt arrow key (0 if none),
    // `ticks_held` counts poll ticks since the press so we can add an initial delay then repeat
    // with acceleration.
    int held_dir = 0;
    int ticks_held = 0;
};
