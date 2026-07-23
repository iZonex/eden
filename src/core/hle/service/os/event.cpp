// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_event.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"

namespace Service {

Event::Event(KernelHelpers::ServiceContext& ctx_)
    : ctx{ctx_}
{
    m_event = ctx.CreateEvent("Event");
}

Event::~Event() {
    // m_event is null when the kernel event budget was exhausted at construction. Guard every use so
    // exhaustion degrades gracefully (a service returns a null handle) instead of dereferencing null
    // — the outgoing-IPC layer already skips a null handle, so this turns a hard crash into a soft
    // failure. See ServiceContext::CreateEvent.
    if (m_event != nullptr) {
        m_event->GetReadableEvent().Close(ctx.kernel);
        m_event->Close(ctx.kernel);
    }
}

void Event::Signal(Kernel::KernelCore& kernel) noexcept {
    if (m_event != nullptr) {
        m_event->Signal(kernel);
    }
}

void Event::Clear(Kernel::KernelCore& kernel) noexcept {
    if (m_event != nullptr) {
        m_event->Clear(kernel);
    }
}

Kernel::KReadableEvent* Event::GetHandle() noexcept {
    // Return a real null (not `&null->readable_event`, which would be a non-null garbage pointer that
    // crashes in KHandleTable::Add) so the IPC handle-writer's `if (object)` guard skips it.
    if (m_event == nullptr) {
        return nullptr;
    }
    return std::addressof(m_event->GetReadableEvent());
}

} // namespace Service
