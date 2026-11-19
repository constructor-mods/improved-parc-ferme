// SPDX-License-Identifier: MIT
// The start lights wait for the pit lane starter. Before the five lights the game counts down a short delay; while
// the car starting from the pit lane is still on its way to the pit exit line, that delay is kept from running out,
// so the lights only begin once the car stands at the line.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "ai_start.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace improved_parc_ferme {

// The start lights' update in its table, and the delay before the lights begin (on this game build).
constexpr uintptr_t kStartLightsUpdateSlot = 0x57e46c0 + 0x48;
constexpr uintptr_t kStartLightsUpdate = 0x2d92250;
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
// The start lights are on their way since the race started. With a formation lap the race starts twice: first for
// the formation lap (no lights), then again once the grid has formed up (the lights).
inline std::atomic<bool> g_lights_coming{false};
// The lights have begun once already in this session: a later start is a restart after a red flag.
inline std::atomic<bool> g_lights_begun_in_session{false};
// A car starts from the pit lane at these lights: they wait for it from their very first step.
inline std::atomic<bool> g_lights_wait_due{false};

inline void hold_lights();

inline void *start_lights_update(void *state, void *route, double seconds)
{
    auto *delay = reinterpret_cast<float *>(reinterpret_cast<uintptr_t>(state) + kLightsDelay);
    if (!g_lights_begun.load() && swofty::mem::readable(reinterpret_cast<uintptr_t>(delay), sizeof(float))) {
        if (!g_lights_delay_logged.exchange(true)) {
            // After a formation lap the lights begin at once, so the wait starts here, before the car is sent.
            if (g_lights_wait_due.exchange(false)) {
                hold_lights();
            }
            g_lights_coming = true;
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
            g_lights_begun_in_session = true;
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

// A race start: the lights may wait again. A car starting from the pit lane makes them wait from the start.
inline void new_lights(bool wait_due)
{
    g_lights_begun = false;
    g_lights_delay_logged = false;
    g_lights_coming = false;
    g_lights_waited = 0;
    g_lights_wait_due = wait_due;
}

// A new session: its first start is never a restart.
inline void new_session_lights()
{
    g_lights_begun_in_session = false;
    g_lights_coming = false;
    g_lights_wait_due = false;
}

// True once the start lights are on their way. Without our view of the lights, every race start counts.
inline bool lights_coming()
{
    return g_start_lights_update == nullptr || g_lights_coming.load();
}

// True when this race start is a restart after a red flag: the lights already went once in this session. Without our
// view of the lights, any start after the first counts as a restart.
inline bool restart_after_red_flag(int race_starts)
{
    if (g_start_lights_update == nullptr) {
        return race_starts > 1;
    }
    return g_lights_begun_in_session.load();
}

inline void hold_lights()
{
    g_lights_wait = g_start_lights_update != nullptr && can_skip_lights_wait();
}

inline void let_lights_go()
{
    g_lights_wait = false;
}

} // namespace improved_parc_ferme
