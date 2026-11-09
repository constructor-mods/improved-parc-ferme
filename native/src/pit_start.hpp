// SPDX-License-Identifier: MIT
// The pit lane start: at the race start the car goes into its pit box, the crew lets it go and it joins the race
// from the end of the pit lane.
#pragma once
#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <cstdint>
#include <set>
#include <vector>

namespace improved_parc_ferme {

// The session's pit lane, from the game's own pit status changes.
inline std::atomic<void *> g_pit_lane{nullptr};

constexpr uintptr_t kPitEntries = 0x68;
constexpr uintptr_t kPitEntrySize = 0x10;
constexpr uintptr_t kPitStops = 0x14;
constexpr uint32_t kOnTrack = 1;

// Cars that started from the pit lane and still have that trip counted as a pit stop.
inline std::set<int> g_stop_to_take_back;

// Where the game counts a car's pit stops, or 0.
inline uintptr_t stop_count_address(void *pit_lane, int car)
{
    uintptr_t entry = 0;
    uintptr_t slot = reinterpret_cast<uintptr_t>(pit_lane) + kPitEntries + uintptr_t(car) * kPitEntrySize;
    if (!swofty::mem::read(slot, entry) || !entry) {
        return 0;
    }
    return entry + kPitStops;
}

// The game counts the trip out of the pit box as a pit stop. A pit lane start isn't one, so it comes off again
// once the car is on track.
inline void take_back_stop(void *pit_lane, int car)
{
    uintptr_t address = stop_count_address(pit_lane, car);
    uint32_t stops = 0;
    if (!address || !swofty::mem::read(address, stops) || stops == 0) {
        swofty::log("[improved-parc-ferme] car %d: no pit stop to take back", car);
        return;
    }
    *reinterpret_cast<uint32_t *>(address) = stops - 1;
    swofty::log("[improved-parc-ferme] car %d: pit lane start not counted as a stop (%u -> %u)", car, stops,
                stops - 1);
}

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
