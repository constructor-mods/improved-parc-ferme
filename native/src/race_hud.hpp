// SPDX-License-Identifier: MIT
// The game hides the race HUD while the player's car sits in its garage. For a pit lane start from the garage the
// race is on, so the HUD stays.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace improved_parc_ferme {

// The game's rule for when the HUD may show: a list of pit states it shows in, and the garage isn't one of them (on
// this game build). While a car waits in its garage at the race start, the garage is added to that list, so the game
// never hides the HUD.
constexpr uintptr_t kHudPitStates = 0x2d66129;
constexpr uint8_t kHudPitStatesStart[] = {0xb9, 0xfe, 0xe1, 0x01, 0x00};
constexpr uintptr_t kHudPitStatesByte = 2;
constexpr uint8_t kWithoutGarage = 0xe1;
constexpr uint8_t kWithGarage = 0xe3;

inline std::atomic<bool> g_hud_stays_in_garage{false};

inline void hud_stays_in_garage(bool stay)
{
    if (g_hud_stays_in_garage.exchange(stay) == stay) {
        return;
    }
    auto *code = reinterpret_cast<uint8_t *>(swofty::api->module_base() + kHudPitStates);
    uint8_t value = kWithoutGarage;
    if (stay) {
        if (std::memcmp(code, kHudPitStatesStart, sizeof kHudPitStatesStart) != 0) {
            swofty::log("[improved-parc-ferme] this game build is not supported: the HUD may hide in the garage");
            g_hud_stays_in_garage = false;
            return;
        }
        value = kWithGarage;
    } else if (code[kHudPitStatesByte] != kWithGarage) {
        return;
    }
    DWORD old_protect = 0;
    if (!VirtualProtect(code, sizeof kHudPitStatesStart, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return;
    }
    code[kHudPitStatesByte] = value;
    VirtualProtect(code, sizeof kHudPitStatesStart, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), code, sizeof kHudPitStatesStart);
    swofty::log("[improved-parc-ferme] race HUD %s", stay ? "stays on in the garage" : "follows the game again");
}

// The game's reasons to hide the HUD, one bit each, and its "show or hide the HUD now" call (on this game build).
constexpr uintptr_t kHudHideReasons = 0x604e900;
constexpr uint32_t kHiddenInGarage = 1;
constexpr uintptr_t kApplyHud = 0x2c74200;
constexpr uint8_t kApplyHudStart[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x89, 0xce, 0x83, 0x39};
using ApplyHud = void (*)(uint32_t *reasons);

// In case the HUD was already hidden: on the game thread only, never while holding g_mutex. Every frame while the
// car waits in its garage.
inline void keep_race_hud_on()
{
    uintptr_t base = swofty::api->module_base();
    if (std::memcmp(reinterpret_cast<void *>(base + kApplyHud), kApplyHudStart, sizeof kApplyHudStart) != 0) {
        return;
    }
    auto *reasons = reinterpret_cast<uint32_t *>(base + kHudHideReasons);
    if (!(*reasons & kHiddenInGarage)) {
        return;
    }
    *reasons &= ~kHiddenInGarage;
    reinterpret_cast<ApplyHud>(base + kApplyHud)(reasons);
    static bool logged = false;
    if (!logged) {
        logged = true;
        swofty::log("[improved-parc-ferme] race HUD switched back on in the garage");
    }
}

} // namespace improved_parc_ferme
