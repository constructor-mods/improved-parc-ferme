// SPDX-License-Identifier: MIT
// Keeps the setup rows open after qualifying. The game's own parc ferme rule greys them out in the race; this addon
// lets the player change them and sends the car to the pit lane instead, like the real rule.
#pragma once
#include <swofty/mixin.hpp>

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace improved_parc_ferme {

using swofty::log;

// Where the game decides between "lock the rows" and "clear the lock", on this game build.
constexpr uintptr_t kLockChoice = 0xc0ac05;
constexpr uint8_t kLockChoiceBytes[] = {0x80, 0x7a, 0x14, 0x00, 0x0f, 0x84, 0x99, 0x00, 0x00, 0x00};
// Always take the "clear the lock" way.
constexpr uint8_t kAlwaysClear[] = {0xe9, 0x9a, 0x00, 0x00, 0x00, 0x90};

// True when the rows stay open. Does nothing on a game build it doesn't know.
inline bool keep_rows_open()
{
    auto *choice = reinterpret_cast<uint8_t *>(swofty::api->module_base() + kLockChoice);
    if (std::memcmp(choice, kLockChoiceBytes, sizeof kLockChoiceBytes) != 0) {
        log("[improved-parc-ferme] this game build is not supported: the setup rows stay locked in the race");
        return false;
    }
    uint8_t *jump = choice + 4;
    DWORD old_protect = 0;
    if (!VirtualProtect(jump, sizeof kAlwaysClear, PAGE_EXECUTE_READWRITE, &old_protect)) {
        log("[improved-parc-ferme] can't change the game code: the setup rows stay locked in the race");
        return false;
    }
    std::memcpy(jump, kAlwaysClear, sizeof kAlwaysClear);
    VirtualProtect(jump, sizeof kAlwaysClear, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), jump, sizeof kAlwaysClear);
    log("[improved-parc-ferme] setup rows stay open after qualifying");
    return true;
}

} // namespace improved_parc_ferme
