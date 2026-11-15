// SPDX-License-Identifier: MIT
// Before the lights the game's driver waits "at the lights" like the cars on the grid, which parks a pit lane starter
// halfway down the pit lane. For the car starting from the pit lane, that wait is skipped: its driver goes straight
// to racing, so it drives on to the pit exit line, where the hold stops it.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace improved_parc_ferme {

// The driver's "at the lights" step and the id of its "racing" step (on this game build).
constexpr uintptr_t kAtLightsStepSlot = 0x5638890 + 0x18;
constexpr uintptr_t kAtLightsStep = 0x6682b0;
constexpr uintptr_t kRacingStepId = 0x604843c;
using DriverStep = uint32_t (*)(void *step, float seconds, void *driver);

inline DriverStep g_at_lights_step = nullptr;
// The car whose driver skips the wait, or -1.
inline std::atomic<int> g_skip_lights_wait{-1};
// Set once the skip has happened, so the frame can log it.
inline std::atomic<bool> g_lights_wait_skipped{false};

// Runs for every car's driver while it waits at the lights. The driver's first byte is its car number.
inline uint32_t at_lights_step(void *step, float seconds, void *driver)
{
    uint32_t next = g_at_lights_step(step, seconds, driver);
    if (next != 0 || !driver) {
        return next;
    }
    int car = *static_cast<uint8_t *>(driver);
    if (car != g_skip_lights_wait.load()) {
        return next;
    }
    g_lights_wait_skipped = true;
    return *reinterpret_cast<uint32_t *>(swofty::api->module_base() + kRacingStepId);
}

// At load: our check runs after the game's own "at the lights" step.
inline void follow_lights_wait()
{
    uintptr_t module = swofty::api->module_base();
    auto *slot = reinterpret_cast<uintptr_t *>(module + kAtLightsStepSlot);
    if (!swofty::mem::readable(reinterpret_cast<uintptr_t>(slot), sizeof(uintptr_t)) ||
        *slot != module + kAtLightsStep) {
        swofty::log("[improved-parc-ferme] lights wait not found: a pit lane starter waits mid pit lane");
        return;
    }
    g_at_lights_step = reinterpret_cast<DriverStep>(*slot);
    DWORD old_protect = 0;
    if (!VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &old_protect)) {
        g_at_lights_step = nullptr;
        return;
    }
    *slot = reinterpret_cast<uintptr_t>(&at_lights_step);
    VirtualProtect(slot, sizeof(uintptr_t), old_protect, &old_protect);
}

inline void skip_lights_wait(int car)
{
    g_lights_wait_skipped = false;
    g_skip_lights_wait = car;
}

inline void stop_skipping_lights_wait()
{
    g_skip_lights_wait = -1;
}

} // namespace improved_parc_ferme
