// SPDX-License-Identifier: MIT
// Which session the game is in, from the game's own session number.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <cstdint>

namespace improved_parc_ferme {

constexpr uintptr_t kManagerSettings = 0x1a8;
constexpr uintptr_t kSettingsSession = 0x4;

// The game's session numbers: 1-4 practice, 5-9 qualifying, 10-14 sprint shootout, 15-17 races, 0 none.
enum class SessionKind { none, practice, qualifying, race };

inline uint32_t session_number()
{
    uintptr_t slot = swofty::api->resolve("g_Game_Manager");
    uintptr_t manager = 0;
    uintptr_t settings = 0;
    uint32_t session = 0;
    if (!slot || !swofty::mem::read(slot, manager) || !manager) {
        return 0;
    }
    if (!swofty::mem::read(manager + kManagerSettings, settings) || !settings) {
        return 0;
    }
    if (!swofty::mem::read(settings + kSettingsSession, session)) {
        return 0;
    }
    return session;
}

inline SessionKind kind_of(uint32_t session)
{
    if (session >= 1 && session <= 4) {
        return SessionKind::practice;
    }
    if (session >= 5 && session <= 14) {
        return SessionKind::qualifying;
    }
    if (session >= 15 && session <= 17) {
        return SessionKind::race;
    }
    return SessionKind::none;
}

} // namespace improved_parc_ferme
