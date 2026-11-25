// SPDX-License-Identifier: MIT
// The game disqualifies a car that stands still for 20 s in the race ("parked for too long"). A car held at the pit
// exit is parked on purpose, so its countdown is kept full while it waits. Once let go it gets the full 20 s again.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <chrono>
#include <cstdint>

namespace improved_parc_ferme {

constexpr uintptr_t kManagerRacing = 0x2b8;
constexpr uintptr_t kRacingRules = 0x410;
constexpr uintptr_t kRulesParkedCheck = 0xa8;
constexpr uintptr_t kParkedTimersBegin = 0x50;
constexpr uintptr_t kParkedTimersEnd = 0x58;
constexpr uintptr_t kParkedTimerSize = 8;
constexpr float kParkedTimerFull = 20.0f;
constexpr uintptr_t kMostParkedTimers = 32;
constexpr auto kParkedLogEvery = std::chrono::seconds(2);

// Where the car's countdown is, or 0 when it can't be found. No game calls, reads only.
inline uintptr_t parked_timer_of(int car)
{
    uintptr_t slot = swofty::api->resolve("g_Game_Manager");
    uintptr_t manager = 0;
    uintptr_t racing = 0;
    uintptr_t check = 0;
    uintptr_t table = 0;
    uintptr_t begin = 0;
    uintptr_t end = 0;
    if (car < 0 || !slot || !swofty::mem::read(slot, manager) || !manager) {
        return 0;
    }
    if (!swofty::mem::read(manager + kManagerRacing, racing) || !racing) {
        return 0;
    }
    if (!swofty::mem::read(racing + kRacingRules + kRulesParkedCheck, check) || !check) {
        return 0;
    }
    // The check's own function table sits in the game's image.
    if (!swofty::mem::read(check, table) || !swofty::mem::in_image(table)) {
        return 0;
    }
    if (!swofty::mem::read(check + kParkedTimersBegin, begin) || !swofty::mem::read(check + kParkedTimersEnd, end)) {
        return 0;
    }
    if (!begin || end <= begin || (end - begin) % kParkedTimerSize != 0) {
        return 0;
    }
    uintptr_t count = (end - begin) / kParkedTimerSize;
    if (count > kMostParkedTimers || uintptr_t(car) >= count) {
        return 0;
    }
    return begin + uintptr_t(car) * kParkedTimerSize;
}

// Every frame while the car waits at the pit exit: put its countdown back to full.
inline void keep_parked_timer_full(int car)
{
    static bool told_missing = false;
    static auto next_log = std::chrono::steady_clock::time_point{};
    uintptr_t timer = parked_timer_of(car);
    if (!timer) {
        if (!told_missing) {
            told_missing = true;
            swofty::log("[improved-parc-ferme] parked timer not found: the game may disqualify a car held too long");
        }
        return;
    }
    float left = 0;
    if (!swofty::mem::read(timer, left) || left < -1.0f || left > kParkedTimerFull + 0.5f) {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    if (now >= next_log) {
        next_log = now + kParkedLogEvery;
        swofty::log("[improved-parc-ferme] car %d: parked timer %.3f s, kept full", car, left);
    }
    *reinterpret_cast<float *>(timer) = kParkedTimerFull;
}

} // namespace improved_parc_ferme
