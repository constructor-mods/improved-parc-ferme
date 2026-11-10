// SPDX-License-Identifier: MIT
// The pit lane start: at the race start the car goes to its garage, like in practice. Once the field has gone past
// the pit exit, the crew unplugs it and it drives out by itself to join the race from the end of the pit lane.
#pragma once
#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "pit_crew.hpp"
#include "race_hud.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

namespace improved_parc_ferme {

// The session's pit lane, from the game's own pit status changes.
inline std::atomic<void *> g_pit_lane{nullptr};

constexpr uint32_t kInGarage = 9;
constexpr uint32_t kOnTrack = 1;

// How long the car waits in its garage after the lights: by then the field is past the pit exit.
constexpr auto kDriveOutDelay = std::chrono::seconds(15);

// The game's "car wants to leave its garage" call, the one Go to Track uses (a jump on this game build).
constexpr uintptr_t kWantsToLeaveGarage = 0x34d1350;
constexpr uint8_t kJump = 0xe9;
using WantsToLeaveGarage = void (*)(void *pit_lane, uint8_t car, uint32_t flag);

// The game's own "move the car into its garage".
constexpr uintptr_t kTeleportToGarage = 0x34d17f0;
constexpr uint8_t kTeleportToGarageStart[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48};
using TeleportToGarage = bool (*)(void *pit_lane, uint8_t car);

// Never called while holding g_mutex: the game calls back into the addon's own hooks.
inline void send_to_garage(void *pit_lane, const std::vector<int> &cars)
{
    auto *call = reinterpret_cast<uint8_t *>(swofty::api->module_base() + kTeleportToGarage);
    bool known = std::memcmp(call, kTeleportToGarageStart, sizeof kTeleportToGarageStart) == 0;
    for (int car : cars) {
        swofty::log("[improved-parc-ferme] car %d starts from the pit lane: to its garage", car);
        // The race is on: the HUD stays until the end of the race, also in the garage.
        hud_stays_in_garage(true);
        if (known) {
            reinterpret_cast<TeleportToGarage>(call)(pit_lane, uint8_t(car));
            crews_stay_hidden(true);
            hide_crew(car);
        } else {
            // Unknown game build: the car still goes to its garage.
            swofty::game::Game_Pitlane_SetVehicleStatus(pit_lane, uint8_t(car), kInGarage);
        }
    }
}

// Never called while holding g_mutex.
inline void drive_out(void *pit_lane, const std::vector<int> &cars)
{
    auto *call = reinterpret_cast<uint8_t *>(swofty::api->module_base() + kWantsToLeaveGarage);
    if (*call != kJump) {
        swofty::log("[improved-parc-ferme] this game build is not supported: the car stays in its garage");
        return;
    }
    for (int car : cars) {
        swofty::log("[improved-parc-ferme] car %d drives out of its garage", car);
        crews_stay_hidden(false);
        show_crew(car);
        reinterpret_cast<WantsToLeaveGarage>(call)(pit_lane, uint8_t(car), 0);
    }
}

} // namespace improved_parc_ferme
