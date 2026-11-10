// SPDX-License-Identifier: MIT
// A team's pit crew. In a race they sit on chairs at the front of the garage, which looks wrong with the car parked
// behind them, so they're hidden while the car waits there and come back when it drives out. Both cars of a team
// share one crew.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace improved_parc_ferme {

// The game's pit crews: a list of crews, one per garage, each knowing its garage's two cars (on this game build).
constexpr uintptr_t kPitStops = 0x6045ed8;
constexpr uintptr_t kPitCrews = 0x28;
constexpr uintptr_t kCrewListStart = 0x20;
constexpr uintptr_t kCrewListEnd = 0x28;
constexpr uintptr_t kCrewEntrySize = 0x10;
constexpr uintptr_t kCrewInEntry = 0x8;
constexpr uintptr_t kFirstCar = 0xc0;
constexpr uintptr_t kSecondCar = 0xc1;
constexpr int kMostCrews = 32;

// The game's own hide and show calls for a pit crew.
constexpr uintptr_t kHideCrew = 0x355140;
constexpr uintptr_t kShowCrew = 0x3b9410;
constexpr uint8_t kHideCrewStart[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48};
constexpr uint8_t kShowCrewStart[] = {0x56, 0x57, 0x48, 0x83, 0xec, 0x28, 0x80, 0xb9, 0x50, 0x04, 0x00, 0x00};
using HideCrew = void (*)(void *crew, uint8_t everything);
using ShowCrew = void (*)(void *crew);

// The crew of the car's garage, or nullptr if there's none or this game build isn't known.
inline void *crew_of(int car)
{
    uintptr_t base = swofty::api->module_base();
    if (std::memcmp(reinterpret_cast<void *>(base + kHideCrew), kHideCrewStart, sizeof kHideCrewStart) != 0 ||
        std::memcmp(reinterpret_cast<void *>(base + kShowCrew), kShowCrewStart, sizeof kShowCrewStart) != 0) {
        return nullptr;
    }
    uintptr_t pit_stops = 0;
    uintptr_t crews = 0;
    uintptr_t start = 0;
    uintptr_t end = 0;
    if (!swofty::mem::read(base + kPitStops, pit_stops) || !pit_stops) {
        return nullptr;
    }
    if (!swofty::mem::read(pit_stops + kPitCrews, crews) || !crews) {
        return nullptr;
    }
    if (!swofty::mem::read(crews + kCrewListStart, start) || !swofty::mem::read(crews + kCrewListEnd, end)) {
        return nullptr;
    }
    if (!start || end < start || (end - start) / kCrewEntrySize > kMostCrews) {
        return nullptr;
    }
    for (uintptr_t entry = start; entry < end; entry += kCrewEntrySize) {
        uintptr_t crew = 0;
        uint8_t first = 0xff;
        uint8_t second = 0xff;
        if (!swofty::mem::read(entry + kCrewInEntry, crew) || !crew) {
            continue;
        }
        swofty::mem::read(crew + kFirstCar, first);
        swofty::mem::read(crew + kSecondCar, second);
        if (first == car || second == car) {
            return reinterpret_cast<void *>(crew);
        }
    }
    return nullptr;
}

// On the game thread only, never while holding g_mutex.
inline void hide_crew(int car)
{
    void *crew = crew_of(car);
    if (!crew) {
        swofty::log("[improved-parc-ferme] car %d: no pit crew to hide", car);
        return;
    }
    reinterpret_cast<HideCrew>(swofty::api->module_base() + kHideCrew)(crew, 1);
    swofty::log("[improved-parc-ferme] car %d: pit crew hidden", car);
}

// The game's "show every pit crew", which it calls every frame a car is near the pit lane (a jump on this game
// build). While a car waits in its garage it returns at once, so the hidden crew stays hidden.
constexpr uintptr_t kShowAllCrews = 0x3c0590;
constexpr uint8_t kShowAllCrewsStart[] = {0xe9, 0x3b, 0x81, 0xa0, 0x07};
constexpr uint8_t kReturn = 0xc3;

inline std::atomic<bool> g_crews_stay_hidden{false};

inline void crews_stay_hidden(bool stay)
{
    if (g_crews_stay_hidden.exchange(stay) == stay) {
        return;
    }
    auto *code = reinterpret_cast<uint8_t *>(swofty::api->module_base() + kShowAllCrews);
    uint8_t first = kShowAllCrewsStart[0];
    if (stay) {
        if (std::memcmp(code, kShowAllCrewsStart, sizeof kShowAllCrewsStart) != 0) {
            swofty::log("[improved-parc-ferme] this game build is not supported: the pit crews stay");
            g_crews_stay_hidden = false;
            return;
        }
        first = kReturn;
    } else if (code[0] != kReturn) {
        return;
    }
    DWORD old_protect = 0;
    if (!VirtualProtect(code, 1, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return;
    }
    code[0] = first;
    VirtualProtect(code, 1, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), code, 1);
    swofty::log("[improved-parc-ferme] pit crews %s", stay ? "stay hidden" : "shown by the game again");
}

// On the game thread only, never while holding g_mutex.
inline void show_crew(int car)
{
    void *crew = crew_of(car);
    if (!crew) {
        return;
    }
    reinterpret_cast<ShowCrew>(swofty::api->module_base() + kShowCrew)(crew);
    swofty::log("[improved-parc-ferme] car %d: pit crew back", car);
}

} // namespace improved_parc_ferme
