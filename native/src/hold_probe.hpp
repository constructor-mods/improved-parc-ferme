// SPDX-License-Identifier: MIT
// Temporary: logs what the game keeps about the waiting car, to see what changes when it may drive on.
#pragma once
#include "pit_hold.hpp"

#include <cstdio>
#include <string>
#include <vector>

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

namespace improved_parc_ferme {

struct ProbeRegion {
    const char *name;
    uintptr_t at;
    size_t size;
};

inline std::vector<uint32_t> probe_snapshot(uintptr_t at, size_t size)
{
    std::vector<uint32_t> words(size / 4, 0xdeadbeef);
    for (size_t i = 0; i < words.size(); i++) {
        swofty::mem::read(at + i * 4, words[i]);
    }
    return words;
}

// Every frame while a car waits: what in the pit lane changes at lights out.
inline void probe_diff_at_lights_out(void *pit_lane, int car)
{
    static std::vector<uint32_t> before_manager;
    static std::vector<uint32_t> before_state;
    static int phase = 0;
    static HoldClock::time_point lights_out{};
    if (!pit_lane || car < 0) {
        return;
    }
    uintptr_t manager = 0;
    uintptr_t ai = 0;
    uintptr_t states = 0;
    uintptr_t list = 0;
    uintptr_t state = 0;
    swofty::mem::read(reinterpret_cast<uintptr_t>(pit_lane) + 0x48, manager);
    if (manager) {
        swofty::mem::read(manager + 0xb08, ai);
    }
    if (ai) {
        swofty::mem::read(ai + 0x38, states);
    }
    if (states) {
        swofty::mem::read(states + 0xc8, list);
    }
    if (list) {
        swofty::mem::read(list + uintptr_t(car) * 8, state);
    }
    if (!manager) {
        return;
    }
    bool started = g_race_started_value.load() != 0;
    if (phase == 0 && !started) {
        before_manager = probe_snapshot(manager, 0xc40);
        if (state) {
            before_state = probe_snapshot(state, 0x200);
        }
        return;
    }
    if (phase == 0 && started) {
        phase = 1;
        lights_out = HoldClock::now();
        return;
    }
    if (phase != 1 || HoldClock::now() - lights_out < std::chrono::milliseconds(300)) {
        return;
    }
    phase = 2;
    std::vector<uint32_t> after_manager = probe_snapshot(manager, 0xc40);
    int logged = 0;
    for (size_t i = 0; i < after_manager.size() && i < before_manager.size() && logged < 60; i++) {
        if (after_manager[i] != before_manager[i]) {
            swofty::log("[improved-parc-ferme] probe manager +%#zx: %x -> %x", i * 4, before_manager[i],
                        after_manager[i]);
            logged++;
        }
    }
    if (state && !before_state.empty()) {
        std::vector<uint32_t> after_state = probe_snapshot(state, 0x200);
        for (size_t i = 0; i < after_state.size() && logged < 120; i++) {
            if (after_state[i] != before_state[i]) {
                swofty::log("[improved-parc-ferme] probe car state +%#zx: %x -> %x", i * 4, before_state[i],
                            after_state[i]);
                logged++;
            }
        }
    }
    swofty::log("[improved-parc-ferme] probe diff done (manager %p, car state %p)", reinterpret_cast<void *>(manager),
                reinterpret_cast<void *>(state));
}

} // namespace improved_parc_ferme
