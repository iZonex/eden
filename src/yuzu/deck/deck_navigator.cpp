// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <QTimer>

#include <SDL3/SDL.h>

#include "common/logging.h"
#include "common/param_package.h"
#include "common/settings.h"
#include "common/settings_input.h"
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
constexpr int kInitialDelayTicks = 15; // ~240 ms before autorepeat kicks in
constexpr int kRepeatTicks = 6;        // ~100 ms between repeats at first
constexpr int kFastRepeatTicks = 3;    // ~50 ms once the hold has been sustained
constexpr int kAccelerateAfterTicks = 70;

// How often (in poll ticks, ~16 ms each) Auto re-asks SDL which pad is connected, so plugging a Pro
// Controller in mid-session flips the lettering without a restart.
constexpr int kLayoutPollTicks = 60; // ~1 s

/// USB vendor id Valve uses for the Steam Input virtual pad that fronts the Deck's built-in
/// controls. That pad delivers all four letters un-crossed (see DetectFaceLayout).
constexpr Uint16 kValveVendor = 0x28de;

/// Works out which face-button crossing the connected hardware needs. Returns nullopt when nothing
/// is enumerated, so the caller keeps whatever it had — a keyboard session has no lettering.
std::optional<DeckFaceLayout> DetectFaceLayout() {
    int count = 0;
    SDL_JoystickID* const ids = SDL_GetGamepads(&count);
    if (ids == nullptr || count == 0) {
        SDL_free(ids);
        return std::nullopt;
    }

    bool valve = false;
    bool all_nintendo = true;
    for (int i = 0; i < count; ++i) {
        if (SDL_GetGamepadVendorForID(ids[i]) == kValveVendor) {
            valve = true;
        }
        // GetReal* rather than GetGamepadTypeForID: the plain call honours the type-override hints
        // (which the emulator sets so guests see a Switch pad), and would answer "Nintendo" for
        // every device. We want the physical hardware's lettering, not what the guest is told.
        switch (SDL_GetRealGamepadTypeForID(ids[i])) {
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            break;
        default:
            all_nintendo = false;
            break;
        }
    }
    SDL_free(ids);

    // The Deck's own controls win when they are present: that is the pad in the user's hands, and a
    // Pro Controller plugged in beside it should not change what the built-in buttons do.
    //
    // Nothing is crossed on that pad. This used to answer SwapXY, on the belief that Steam Input
    // straightened A/B but left X and Y on each other's npad bit. It straightens all four: the
    // emulator's binding matches SDL's button by NAME, so the letter printed on the Deck is the
    // letter that arrives. Crossing X/Y on top of that is what put them on the wrong actions —
    // pressing Y ran the X action, while A and B, being uncrossed by this preset, stayed correct.
    // That asymmetry is the tell, and it is visible in the log: `bit 3 -> PrimaryAction` is BtnY
    // firing X's intent.
    if (valve) {
        return DeckFaceLayout::Nintendo;
    }
    return all_nintendo ? DeckFaceLayout::Nintendo : DeckFaceLayout::SwapAll;
}
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
        // Anything already down here can never fire until it is released first — which is exactly
        // how a *stuck* bit (a button bound to something that reads as permanently pressed) kills
        // one menu action while leaving the pad perfectly usable in game, where the same bit is only
        // ever read as a level. Worth a line in the log, because it is otherwise invisible.
        if (raw != 0) {
            LOG_WARNING(Input, "Deck menu: buttons already down at activation (npad raw {:#x}) — "
                               "they stay inert until released",
                        raw);
        }
        held_dir = 0;
        ticks_held = 0;
        layout_poll_ticks = 0;
        RefreshFaceLayout();
        timer->start();
    } else {
        timer->stop();
        pressed.fill(false);
        held_dir = 0;
        ticks_held = 0;
    }
}

void DeckNavigator::SetFaceLayout(DeckFaceLayout layout) {
    if (face_layout == layout) {
        return;
    }
    face_layout = layout;
    RefreshFaceLayout();
}

void DeckNavigator::RefreshFaceLayout() {
    const DeckFaceLayout was = resolved;
    if (face_layout == DeckFaceLayout::Auto) {
        if (const auto detected = DetectFaceLayout()) {
            resolved = *detected;
        }
    } else {
        resolved = face_layout;
    }
    if (resolved != was) {
        LOG_INFO(Input, "Deck menu: face buttons resolved to {}",
                 resolved == DeckFaceLayout::Nintendo  ? "nothing crossed (Steam Deck / Nintendo)"
                 : resolved == DeckFaceLayout::SwapXY  ? "X/Y crossed"
                                                       : "Xbox pad (all four crossed)");
        emit FaceLayoutChanged();
    }
}

DeckNavigator::FaceBits DeckNavigator::Face() const {
    switch (resolved) {
    case DeckFaceLayout::SwapXY:
        // A pad whose top and left buttons are printed the other way round from the npad bits they
        // send, with A/B already in place. NOT the Deck's built-in controls — those send all four
        // straight through and want Nintendo; this preset is kept for hardware that genuinely needs
        // the top pair crossed.
        return {BtnA, BtnB, BtnY, BtnX};
    case DeckFaceLayout::SwapAll:
        // A plain Xbox-lettered pad, straight through SDL. SDL binds by position and the emulator's
        // default turns those positions into the Switch layout, so every printed letter lands on
        // the OTHER bit of its pair: A(bottom)->NpadB, B(right)->NpadA, X(left)->NpadY, Y(top)->NpadX.
        return {BtnB, BtnA, BtnY, BtnX};
    case DeckFaceLayout::Nintendo:
    case DeckFaceLayout::Auto:
    default:
        // Every letter lands on its own bit. A Nintendo pad, and the Deck's built-in controls.
        return {BtnA, BtnB, BtnX, BtnY};
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
    // Count each PHYSICAL pad once. Two npads can be bound to the same device — Handheld and Player 1
    // always are on a Deck — and if their mappings ever disagree, OR-ing them turns a single press
    // into two different intents in one poll. That is not theoretical: a stale Handheld mapping with
    // A and B transposed made every menu page open on Accept and close again on Back 1 ms later, so
    // only the dock items that do not navigate appeared to work. Identity is the device a pad is
    // bound to (guid + port), taken from a representative button.
    std::array<std::pair<std::string, int>, npad_ids.size()> seen{};
    std::size_t seen_count = 0;
    for (const auto npad_id : npad_ids) {
        auto* const controller = hid_core.GetEmulatedController(npad_id);
        if (controller == nullptr || !controller->IsConnected()) {
            continue;
        }
        const auto param = controller->GetButtonParam(Settings::NativeButton::A);
        const std::pair<std::string, int> device{param.Get("guid", ""), param.Get("port", -1)};
        const bool duplicate =
            !device.first.empty() &&
            std::find(seen.begin(), seen.begin() + seen_count, device) != seen.begin() + seen_count;
        if (duplicate) {
            continue;
        }
        seen[seen_count++] = device;
        raw |= static_cast<unsigned long long>(controller->GetNpadButtons().raw);
    }
    return raw;
}

void DeckNavigator::Poll() {
    if (!active) {
        return;
    }

    // Under Auto, notice a pad being plugged in or unplugged without waiting for a restart.
    if (face_layout == DeckFaceLayout::Auto && ++layout_poll_ticks >= kLayoutPollTicks) {
        layout_poll_ticks = 0;
        RefreshFaceLayout();
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

    // --- Directional: D-pad or left stick, whichever is active, with autorepeat. The stick uses the
    // npad's pre-thresholded StickL direction bits — those are properly deadzoned and stable, unlike
    // the raw analog value which is noisy near centre. ---
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
    // Each press is logged with the npad word it came from, so "I pressed A and the wrong thing
    // happened" can be answered exactly: whether the physical button produced the bit we think it
    // does, and which intent that bit turned into.
    const auto fired = [raw](int bit, const char* intent) {
        LOG_INFO(Input, "Deck menu: bit {} -> {} (npad raw {:#x})", bit, intent, raw);
    };

    // The four face buttons are looked up by the letter PRINTED on them, so "A" in the hint bar is
    // always the button the user reaches for. See Face() for why the bits differ per pad.
    const FaceBits face = Face();
    if (edge(face.accept)) {
        fired(face.accept, "Accept");
        emit Accept();
    }
    if (!active) {
        return; // Accept may have booted a game and deactivated us
    }
    if (edge(face.back)) {
        fired(face.back, "Back");
        emit Back();
    }
    if (edge(face.primary)) {
        fired(face.primary, "PrimaryAction");
        emit PrimaryAction();
    }
    if (edge(face.secondary)) {
        fired(face.secondary, "SecondaryAction");
        emit SecondaryAction();
    }
    if (edge(BtnL)) {
        fired(BtnL, "TabPrev");
        emit TabPrev();
    }
    if (edge(BtnR)) {
        fired(BtnR, "TabNext");
        emit TabNext();
    }
    if (edge(BtnZL)) {
        fired(BtnZL, "PageUp");
        emit PageUp();
    }
    if (edge(BtnZR)) {
        fired(BtnZR, "PageDown");
        emit PageDown();
    }
    if (edge(BtnPlus)) {
        fired(BtnPlus, "StartPressed");
        emit StartPressed();
    }
    if (edge(BtnMinus)) {
        fired(BtnMinus, "SelectPressed");
        emit SelectPressed();
    }
}
