// SPDX-License-Identifier: MIT
// The game hides the race HUD while the player's car sits in its garage. For a pit lane start from the garage the
// race is on, so the HUD stays.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <cstdint>
#include <cstring>

namespace improved_parc_ferme {

// The game's reasons to hide the HUD, one bit each, and its "show or hide the HUD now" call (on this game build).
constexpr uintptr_t kHudHideReasons = 0x604e900;
constexpr uint32_t kHiddenInGarage = 1;
constexpr uintptr_t kApplyHud = 0x2c74200;
constexpr uint8_t kApplyHudStart[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x89, 0xce, 0x83, 0x39};
using ApplyHud = void (*)(uint32_t *reasons);

// On the game thread only, never while holding g_mutex. Every frame while the car waits in its garage.
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
        swofty::log("[improved-parc-ferme] race HUD kept on in the garage");
    }
}

} // namespace improved_parc_ferme
