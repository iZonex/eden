// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTimer>

#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "yuzu/deck/deck_navigator.h"

namespace {
// Poll at ~60 Hz. Fast enough to feel responsive, cheap enough to run continuously.
constexpr int kPollIntervalMs = 16;

// Directional autorepeat, expressed in poll ticks (~16 ms each):
//  - first move fires immediately on press,
//  - then a deliberate initial hold delay before it starts repeating,
//  - then a base repeat interval that accelerates the longer the direction is held.
constexpr int kInitialDelayTicks = 22; // ~350 ms before autorepeat kicks in
constexpr int kRepeatTicks = 6;        // ~100 ms between repeats at first
constexpr int kFastRepeatTicks = 3;    // ~50 ms once the hold has been sustained
constexpr int kAccelerateAfterTicks = 70;
} // namespace

DeckNavigator::DeckNavigator(Core::HID::HIDCore& hid_core_, QObject* parent)
    : QObject(parent), hid_core{hid_core_} {
    timer = new QTimer(this);
    timer->setInterval(kPollIntervalMs);
    connect(timer, &QTimer::timeout, this, &DeckNavigator::Poll);
}

DeckNavigator::~DeckNavigator() = default;

void DeckNavigator::SetActive(bool active_) {
    if (active == active_) {
        return;
    }
    active = active_;
    if (active) {
        // Prime the latch with the current state so a button already down when we activate does
        // not immediately register as a fresh press.
        const auto raw = CollectButtons();
        for (int i = 0; i < NumTrackedButtons; ++i) {
            pressed[i] = (raw & (1ULL << i)) != 0;
        }
        held_dir = 0;
        ticks_held = 0;
        timer->start();
    } else {
        timer->stop();
        pressed.fill(false);
        held_dir = 0;
        ticks_held = 0;
    }
}

unsigned long long DeckNavigator::CollectButtons() const {
    unsigned long long raw = 0;
    // Aggregate every controller the front-end could plausibly expose so any pad — the Deck's
    // built-in controls, an external Pro Controller on any player slot, or the handheld pad —
    // drives the menu.
    static constexpr std::array npad_ids{
        Core::HID::NpadIdType::Handheld, Core::HID::NpadIdType::Player1,
        Core::HID::NpadIdType::Player2,  Core::HID::NpadIdType::Player3,
        Core::HID::NpadIdType::Player4,  Core::HID::NpadIdType::Player5,
        Core::HID::NpadIdType::Player6,  Core::HID::NpadIdType::Player7,
        Core::HID::NpadIdType::Player8,
    };
    for (const auto npad_id : npad_ids) {
        auto* const controller = hid_core.GetEmulatedController(npad_id);
        if (controller == nullptr || !controller->IsConnected()) {
            continue;
        }
        raw |= static_cast<unsigned long long>(controller->GetNpadButtons().raw);
    }
    return raw;
}

void DeckNavigator::Poll() {
    if (!active) {
        return;
    }

    const auto raw = CollectButtons();

    const auto is_down = [raw](int bit) { return (raw & (1ULL << bit)) != 0; };
    // Fires once, on the transition from released to pressed.
    const auto edge = [&](int bit) {
        const bool down = is_down(bit);
        const bool was = pressed[bit];
        pressed[bit] = down;
        return down && !was;
    };

    // --- Directional: D-pad or left stick, whichever is active, with autorepeat. ---
    const bool up = is_down(BtnUp) || is_down(BtnStickLUp);
    const bool down = is_down(BtnDown) || is_down(BtnStickLDown);
    const bool left = is_down(BtnLeft) || is_down(BtnStickLLeft);
    const bool right = is_down(BtnRight) || is_down(BtnStickLRight);

    // Vertical takes priority over horizontal to avoid diagonal drift on a loose stick.
    int dir = 0;
    if (up) {
        dir = Qt::Key_Up;
    } else if (down) {
        dir = Qt::Key_Down;
    } else if (left) {
        dir = Qt::Key_Left;
    } else if (right) {
        dir = Qt::Key_Right;
    }

    if (dir == 0) {
        held_dir = 0;
        ticks_held = 0;
    } else if (dir != held_dir) {
        // New direction: move immediately, then wait the initial delay before repeating.
        held_dir = dir;
        ticks_held = 0;
        emit Navigate(static_cast<Qt::Key>(dir));
    } else {
        // Same direction held: repeat after the initial delay, accelerating over time.
        ++ticks_held;
        if (ticks_held >= kInitialDelayTicks) {
            const int since = ticks_held - kInitialDelayTicks;
            const int interval = since >= kAccelerateAfterTicks ? kFastRepeatTicks : kRepeatTicks;
            if (since % interval == 0) {
                emit Navigate(static_cast<Qt::Key>(dir));
            }
        }
    }

    // A directional emit above can synchronously boot a game and deactivate us; if so, stop before
    // evaluating the face buttons against this now-stale snapshot.
    if (!active) {
        return;
    }

    // --- Face buttons and shoulders: single-shot on press. ---
    if (edge(BtnA)) {
        emit Accept();
    }
    if (!active) {
        return; // Accept may have booted a game and deactivated us
    }
    if (edge(BtnB)) {
        emit Back();
    }
    if (edge(BtnX)) {
        emit PrimaryAction();
    }
    if (edge(BtnY)) {
        emit SecondaryAction();
    }
    if (edge(BtnL)) {
        emit TabPrev();
    }
    if (edge(BtnR)) {
        emit TabNext();
    }
    if (edge(BtnZL)) {
        emit PageUp();
    }
    if (edge(BtnZR)) {
        emit PageDown();
    }
    if (edge(BtnPlus)) {
        emit StartPressed();
    }
    if (edge(BtnMinus)) {
        emit SelectPressed();
    }
}
