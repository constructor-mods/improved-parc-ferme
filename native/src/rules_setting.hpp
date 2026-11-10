// SPDX-License-Identifier: MIT
// The game's own Parc Ferme Rules setting. With it off, the player chose no parc ferme, so the addon does nothing.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <cstdint>
#include <cstring>

namespace improved_parc_ferme {

// The game's options and its "find an option by name" call (on this game build).
constexpr uintptr_t kOptionsStore = 0x6079258;
constexpr uintptr_t kOptionsMap = 0x270;
constexpr uintptr_t kOptionsStart = 0x288;
constexpr uintptr_t kOptionsEnd = 0x290;
constexpr uintptr_t kOptionEntrySize = 0x10;
constexpr uintptr_t kOptionInEntry = 0x8;
constexpr uintptr_t kOptionGet = 0x10;
constexpr uintptr_t kFindOption = 0x2b5dfd0;
constexpr uint8_t kFindOptionStart[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53};
using FindOption = uint64_t (*)(void *map, const char **name);
using GetOption = uint8_t (*)(void *option, void *context);

// True when Parc Ferme Rules are on, and also when the setting can't be read (the addon then works as always).
// On the game thread only, never while holding g_mutex.
inline bool parc_ferme_rules_on()
{
    uintptr_t base = swofty::api->module_base();
    if (std::memcmp(reinterpret_cast<void *>(base + kFindOption), kFindOptionStart, sizeof kFindOptionStart) != 0) {
        return true;
    }
    uintptr_t store = 0;
    if (!swofty::mem::read(base + kOptionsStore, store) || !store) {
        return true;
    }
    static const char *kName = "parc_ferme_rules";
    uint64_t index = reinterpret_cast<FindOption>(base + kFindOption)(reinterpret_cast<void *>(store + kOptionsMap),
                                                                        &kName);
    uintptr_t start = 0;
    uintptr_t end = 0;
    if (!swofty::mem::read(store + kOptionsStart, start) || !swofty::mem::read(store + kOptionsEnd, end)) {
        return true;
    }
    uintptr_t entry = start + index * kOptionEntrySize;
    uintptr_t option = 0;
    uintptr_t table = 0;
    uintptr_t get = 0;
    if (entry >= end || !swofty::mem::read(entry + kOptionInEntry, option) || !option) {
        return true;
    }
    if (!swofty::mem::read(option, table) || !table || !swofty::mem::read(table + kOptionGet, get) || !get) {
        return true;
    }
    // The game passes an empty 8-byte context and uses what the call returns.
    uint64_t context = 0;
    return reinterpret_cast<GetOption>(get)(reinterpret_cast<void *>(option), &context) != 0;
}

} // namespace improved_parc_ferme
