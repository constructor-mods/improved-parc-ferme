// SPDX-License-Identifier: MIT
// The cars in the session, by their car number. The launcher's live car list comes first; on a game build the
// launcher doesn't list cars for yet, the addon reads the game's own car table.
#pragma once
#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/systems/cars.hpp>

#include <cstdint>
#include <map>

namespace improved_parc_ferme {

constexpr uint32_t kMostListedCars = 22;
// Each car keeps its own car number here.
constexpr uintptr_t kCarNumber = 0xe8;

inline std::map<int, uintptr_t> cars_from_the_game()
{
    std::map<int, uintptr_t> handles;
    static uintptr_t table = swofty::api->resolve("g_Sess_VehicleTable");
    static uintptr_t count_at = swofty::api->resolve("g_Sess_VehicleCount");
    uint32_t count = 0;
    if (!table || !count_at || !swofty::mem::read(count_at, count)) {
        return handles;
    }
    if (count > kMostListedCars) {
        count = kMostListedCars;
    }
    for (uint32_t index = 0; index < count; index++) {
        uintptr_t car = 0;
        uint8_t number = 0;
        if (!swofty::mem::read(table + index * sizeof(uintptr_t), car) || !car) {
            continue;
        }
        // Only a car that says it is this number counts (a half-built car doesn't yet).
        if (!swofty::mem::read(car + kCarNumber, number) || number != index) {
            continue;
        }
        handles[int(index)] = car;
    }
    return handles;
}

// Every car's handle by its car number.
inline std::map<int, uintptr_t> car_handles()
{
    std::map<int, uintptr_t> handles;
    for (const swofty::cars::live::Car &live : swofty::cars::live::cars()) {
        handles[int(live.index())] = reinterpret_cast<uintptr_t>(live.handle());
    }
    if (handles.empty()) {
        handles = cars_from_the_game();
    }
    return handles;
}

inline uintptr_t car_handle(int car)
{
    std::map<int, uintptr_t> handles = car_handles();
    auto found = handles.find(car);
    if (found == handles.end()) {
        return 0;
    }
    return found->second;
}

} // namespace improved_parc_ferme
