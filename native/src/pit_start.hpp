// SPDX-License-Identifier: MIT
// The pit lane start: at the race start the car goes into its pit box, the crew lets it go and it joins the race
// from the end of the pit lane.
#pragma once
#include <swofty/game.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <cstdint>
#include <vector>

namespace improved_parc_ferme {

// The session's pit lane, from the game's own pit status changes.
inline std::atomic<void *> g_pit_lane{nullptr};

// Never called while holding g_mutex: the game applies the car's setup inside this call.
inline void send_to_pit_box(const std::vector<int> &cars)
{
    void *pit_lane = g_pit_lane;
    if (!pit_lane) {
        swofty::log("[improved-parc-ferme] no pit lane known: pit lane start skipped");
        return;
    }
    for (int car : cars) {
        swofty::log("[improved-parc-ferme] car %d starts from the pit lane", car);
        swofty::game::Game_PitLane_PutVehicleInPitBox(pit_lane, uint8_t(car));
    }
}

} // namespace improved_parc_ferme
