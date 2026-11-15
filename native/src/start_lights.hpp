// SPDX-License-Identifier: MIT
// The start lights wait for the pit lane starter. Before the five lights the game counts down a short delay; while
// the car starting from the pit lane is still on its way to the pit exit line, that delay is kept from running out,
// so the lights only begin once the car stands at the line.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace improved_parc_ferme {

// The start lights' update in its table, and the delay before the lights begin (on this game build).
constexpr uintptr_t kStartLightsUpdateSlot = 0x57e2580 + 0x48;
constexpr uintptr_t kStartLightsUpdate = 0x2d8fcc0;
constexpr uintptr_t kLightsDelay = 0xdf0;
// The delay is kept at least this long while the lights wait.
constexpr float kDelayKept = 0.25f;
// The lights wait at most this long for the car (game seconds), so a race never hangs.
constexpr double kLongestLightsWait = 40.0;
using StartLightsUpdate = void *(*)(void *state, void *route, double seconds);

inline StartLightsUpdate g_start_lights_update = nullptr;
// True while the lights should wait for the pit lane starter.
inline std::atomic<bool> g_lights_wait{false};
inline std::atomic<double> g_lights_waited{0};
// The lights have begun since the race started: they are never held again.
inline std::atomic<bool> g_lights_begun{false};
inline std::atomic<bool> g_lights_delay_logged{false};

inline void *start_lights_update(void *state, void *route, double seconds)
{
    auto *delay = reinterpret_cast<float *>(reinterpret_cast<uintptr_t>(state) + kLightsDelay);
    if (!g_lights_begun.load() && swofty::mem::readable(reinterpret_cast<uintptr_t>(delay), sizeof(float))) {
        if (!g_lights_delay_logged.exchange(true)) {
            swofty::log("[improved-parc-ferme] start lights: %.2f s before the lights%s", *delay,
                        g_lights_wait.load() ? ", waiting for the pit lane starter" : "");
        }
        if (g_lights_wait.load() && seconds > 0 && seconds < 0.2) {
            double waited = g_lights_waited.load() + seconds;
            g_lights_waited = waited;
            if (waited > kLongestLightsWait) {
                g_lights_wait = false;
                swofty::log("[improved-parc-ferme] start lights: the car isn't at the line after %.0f s, lights go",
                            waited);
            } else if (*delay < kDelayKept) {
                *delay = kDelayKept;
            }
        }
        if (!g_lights_wait.load() && *delay <= 0) {
            g_lights_begun = true;
            swofty::log("[improved-parc-ferme] start lights begin (held %.1f s)", g_lights_waited.load());
        }
    }
    return g_start_lights_update(state, route, seconds);
}

// At load: the start lights' update goes through ours.
inline void follow_start_lights()
{
    uintptr_t module = swofty::api->module_base();
    auto *slot = reinterpret_cast<uintptr_t *>(module + kStartLightsUpdateSlot);
    if (!swofty::mem::readable(reinterpret_cast<uintptr_t>(slot), sizeof(uintptr_t)) ||
        *slot != module + kStartLightsUpdate) {
        swofty::log("[improved-parc-ferme] start lights not found: they don't wait for a pit lane starter");
        return;
    }
    g_start_lights_update = reinterpret_cast<StartLightsUpdate>(*slot);
    DWORD old_protect = 0;
    if (!VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &old_protect)) {
        g_start_lights_update = nullptr;
        return;
    }
    *slot = reinterpret_cast<uintptr_t>(&start_lights_update);
    VirtualProtect(slot, sizeof(uintptr_t), old_protect, &old_protect);
}

// A race start: the lights may wait again.
inline void new_lights()
{
    g_lights_begun = false;
    g_lights_delay_logged = false;
    g_lights_waited = 0;
}

inline void hold_lights()
{
    g_lights_wait = g_start_lights_update != nullptr;
}

inline void let_lights_go()
{
    g_lights_wait = false;
}

} // namespace improved_parc_ferme
