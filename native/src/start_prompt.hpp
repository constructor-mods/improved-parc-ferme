// SPDX-License-Identifier: MIT
// The clutch prompt of the race start ("hold the clutch") is for a car standing ready to start. The car the game
// drives down the pit lane before the start doesn't get it; it comes back once the car stands at the pit exit line.
#pragma once
#include "pit_hold.hpp"

#include <atomic>
#include <cstdint>

// The SDK knows the prompt check only by its address: its signature, so it can be wrapped.
namespace swofty::game {
using Game_VehicleAntiStallSystem_IsStartPromptActive_t = bool(void *self, uint8_t vehicle);
}

namespace improved_parc_ferme {

// The car driving down the pit lane before the start, or -1.
inline std::atomic<int> g_prompt_free_car{-1};
inline std::atomic<bool> g_prompt_hidden_logged{false};

// True when the game asks whether this car's start prompt shows, and it shouldn't.
inline bool hide_start_prompt(int car)
{
    if (car != g_prompt_free_car.load() || g_standing.load() || g_race_started_value.load() != 0) {
        return false;
    }
    if (!g_prompt_hidden_logged.exchange(true)) {
        swofty::log("[improved-parc-ferme] car %d: no clutch prompt on the way down the pit lane", car);
    }
    return true;
}

inline void free_from_start_prompt(int car)
{
    g_prompt_hidden_logged = false;
    g_prompt_free_car = car;
}

inline void start_prompt_as_normal()
{
    g_prompt_free_car = -1;
}

} // namespace improved_parc_ferme
