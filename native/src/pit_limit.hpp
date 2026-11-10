// SPDX-License-Identifier: MIT
// The game's own pit lane speed limit zone: whether a car is in it right now. Where a car driving out of the pits
// leaves it is the end of the pit lane.
#pragma once
#include <swofty/mem.hpp>

#include <cstdint>

namespace improved_parc_ferme {

// Where the game keeps each car's pit lane state (on this game build).
constexpr uintptr_t kPitLaneMovement = 0x48;
constexpr uintptr_t kPitLaneAi = 0xb08;
constexpr uintptr_t kAiStates = 0x38;
constexpr uintptr_t kAiStateList = 0xc8;
constexpr uintptr_t kInSpeedLimit = 0x5a;
constexpr int kMostCars = 27;

enum class SpeedLimit { unknown, inside, outside };

// No game calls: memory reads only.
inline SpeedLimit speed_limit_of(void *pit_lane, int car)
{
    if (!pit_lane || car < 0 || car >= kMostCars) {
        return SpeedLimit::unknown;
    }
    uintptr_t movement = 0;
    uintptr_t ai = 0;
    uintptr_t states = 0;
    uintptr_t list = 0;
    uintptr_t state = 0;
    uint8_t inside = 0;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(pit_lane) + kPitLaneMovement, movement) || !movement) {
        return SpeedLimit::unknown;
    }
    if (!swofty::mem::read(movement + kPitLaneAi, ai) || !ai || !swofty::mem::read(ai + kAiStates, states) ||
        !states) {
        return SpeedLimit::unknown;
    }
    if (!swofty::mem::read(states + kAiStateList, list) || !list ||
        !swofty::mem::read(list + uintptr_t(car) * sizeof(uintptr_t), state) || !state) {
        return SpeedLimit::unknown;
    }
    if (!swofty::mem::read(state + kInSpeedLimit, inside) || inside > 1) {
        return SpeedLimit::unknown;
    }
    if (inside) {
        return SpeedLimit::inside;
    }
    return SpeedLimit::outside;
}

} // namespace improved_parc_ferme
