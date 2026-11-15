// SPDX-License-Identifier: MIT
// Temporary: logs what the game keeps about the waiting car, to see what changes when it may drive on.
#pragma once
#include "pit_hold.hpp"

#include <cstdio>
#include <string>

namespace improved_parc_ferme {

inline std::string probe_words(uintptr_t at, int count)
{
    std::string text;
    char word[16];
    for (int i = 0; i < count; i++) {
        uint32_t value = 0;
        if (!swofty::mem::read(at + uintptr_t(i) * 4, value)) {
            text += "?? ";
            continue;
        }
        std::snprintf(word, sizeof word, "%x ", value);
        text += word;
    }
    return text;
}

inline void probe_waiting_car(void *start_system, const StopPlan &plan, void *pit_lane)
{
    static HoldClock::time_point next{};
    auto now = HoldClock::now();
    if (now < next) {
        return;
    }
    next = now + std::chrono::milliseconds(500);
    auto system = reinterpret_cast<uintptr_t>(start_system);
    uintptr_t begin = 0;
    uintptr_t entry = 0;
    uintptr_t driver = 0;
    swofty::mem::read(system + 0x48, begin);
    if (begin) {
        swofty::mem::read(begin + uintptr_t(plan.number) * 8, entry);
    }
    if (entry) {
        swofty::mem::read(entry + 0x58, driver);
    }
    uintptr_t controllers = 0;
    uintptr_t sets = 0;
    uintptr_t set = 0;
    swofty::mem::read(swofty::api->module_base() + 0x6096578, controllers);
    if (controllers) {
        swofty::mem::read(controllers + 0x30, sets);
    }
    if (sets) {
        swofty::mem::read(sets + uintptr_t(plan.number) * 8, set);
    }
    uintptr_t record = 0;
    if (pit_lane) {
        swofty::mem::read(reinterpret_cast<uintptr_t>(pit_lane) + 0x68 + uintptr_t(plan.number) * 0x10, record);
    }
    swofty::log("[improved-parc-ferme] probe sys %s| entry %s| driver %s| set %s| car %s| pit %s",
                probe_words(system + 0x100, 6).c_str(), entry ? probe_words(entry + 0x10, 12).c_str() : "- ",
                driver ? probe_words(driver, 12).c_str() : "- ", set ? probe_words(set + 0x10, 10).c_str() : "- ",
                probe_words(plan.car + 0x1f0, 12).c_str(), record ? probe_words(record, 16).c_str() : "- ");
}

} // namespace improved_parc_ferme
