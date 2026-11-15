// SPDX-License-Identifier: MIT
// Before the lights the game's driver waits like the cars on the grid ("before the lights", then "at the lights"),
// which parks a pit lane starter halfway down the pit lane. For the car starting from the pit lane, both waits are
// skipped: its driver goes straight to racing, so it drives on to the pit exit line, where the hold stops it.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace improved_parc_ferme {

// The driver's "before the lights" and "at the lights" steps and the id of its "racing" step (on this game build).
constexpr uintptr_t kBeforeLightsStepSlot = 0x5638b50 + 0x18;
constexpr uintptr_t kBeforeLightsStep = 0x669140;
constexpr uintptr_t kAtLightsStepSlot = 0x5638890 + 0x18;
constexpr uintptr_t kAtLightsStep = 0x6682b0;
constexpr uintptr_t kRacingStepId = 0x604843c;
using DriverStep = uint32_t (*)(void *step, float seconds, void *driver);

inline DriverStep g_before_lights_step = nullptr;
inline DriverStep g_at_lights_step = nullptr;
// The car whose driver skips the wait, or -1.
inline std::atomic<int> g_skip_lights_wait{-1};
// Set once the skip has happened, so the frame can log it.
inline std::atomic<bool> g_lights_wait_skipped{false};

// After the game's own step: the pit lane starter's driver goes racing instead of waiting. The driver's first byte is
// its car number.
inline uint32_t skip_for_starter(uint32_t next, void *driver)
{
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

inline uint32_t before_lights_step(void *step, float seconds, void *driver)
{
    return skip_for_starter(g_before_lights_step(step, seconds, driver), driver);
}

inline uint32_t at_lights_step(void *step, float seconds, void *driver)
{
    return skip_for_starter(g_at_lights_step(step, seconds, driver), driver);
}

// Puts our step in a driver step's slot, after a check that the slot holds the game's own step.
inline bool follow_step(uintptr_t slot_offset, uintptr_t step_offset, DriverStep ours, DriverStep &game)
{
    uintptr_t module = swofty::api->module_base();
    auto *slot = reinterpret_cast<uintptr_t *>(module + slot_offset);
    if (!swofty::mem::readable(reinterpret_cast<uintptr_t>(slot), sizeof(uintptr_t)) ||
        *slot != module + step_offset) {
        return false;
    }
    game = reinterpret_cast<DriverStep>(*slot);
    DWORD old_protect = 0;
    if (!VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &old_protect)) {
        game = nullptr;
        return false;
    }
    *slot = reinterpret_cast<uintptr_t>(ours);
    VirtualProtect(slot, sizeof(uintptr_t), old_protect, &old_protect);
    return true;
}

// At load: our check runs after the game's own waiting steps.
inline void follow_lights_wait()
{
    if (!follow_step(kAtLightsStepSlot, kAtLightsStep, &at_lights_step, g_at_lights_step)) {
        swofty::log("[improved-parc-ferme] lights wait not found: a pit lane starter waits mid pit lane");
        return;
    }
    if (!follow_step(kBeforeLightsStepSlot, kBeforeLightsStep, &before_lights_step, g_before_lights_step)) {
        swofty::log("[improved-parc-ferme] wait before the lights not found: the start lights don't wait for the car");
    }
}

// True when both waits can be skipped, so the start lights may wait for the car.
inline bool can_skip_lights_wait()
{
    return g_at_lights_step != nullptr && g_before_lights_step != nullptr;
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
