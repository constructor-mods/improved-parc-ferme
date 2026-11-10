// SPDX-License-Identifier: MIT
// Parc ferme: the setup each car has when it first leaves the pits in qualifying, and the cars that changed a
// locked row after that. Those cars start the next race from the pit lane.
#pragma once
#include "setup_rows.hpp"

#include <swofty/mixin.hpp>

#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace improved_parc_ferme {

using swofty::log;
using Clock = std::chrono::steady_clock;

enum class Phase { other, qualifying, before_start, racing };

// How long after the race start a late setup change still counts (the game applies the grid menu's changes when
// the player leaves it).
constexpr auto kLateChange = std::chrono::seconds(10);

// One setup set of one car.
using SetupKey = std::pair<int, Block>;

// Something to tell the player, shown outside the lock.
struct Message {
    std::string text;
    std::string detail;
};

inline std::mutex g_mutex;
inline Phase g_phase = Phase::other;
inline Clock::time_point g_race_started;
inline std::map<SetupKey, Values> g_last_applied;
inline std::map<SetupKey, Values> g_parc_ferme;
inline std::set<int> g_in_parc_ferme;
inline std::set<int> g_pit_lane_start;
inline std::vector<Message> g_messages;
// Set when a car broke parc ferme and it should be saved.
inline bool g_parc_ferme_changed = false;
// Back in a race after the game was closed since qualifying: the setup each car has now is its parc ferme setup.
inline bool g_take_setup_now = false;

// A new qualifying: every car is out of parc ferme until it leaves the pits.
inline void start_qualifying()
{
    g_parc_ferme.clear();
    g_in_parc_ferme.clear();
    g_pit_lane_start.clear();
    g_take_setup_now = false;
}

// The car's setup goes into parc ferme as it is now.
inline void close_parc_ferme(int car)
{
    if (g_in_parc_ferme.count(car)) {
        return;
    }
    g_in_parc_ferme.insert(car);
    for (const auto &[key, values] : g_last_applied) {
        if (key.first == car) {
            g_parc_ferme[key] = values;
        }
    }
    log("[improved-parc-ferme] car %d in parc ferme", car);
}

// Qualifying ends: a car that never left the pits is in parc ferme too.
inline void close_all()
{
    std::set<int> cars;
    for (const auto &[key, values] : g_last_applied) {
        cars.insert(key.first);
    }
    for (int car : cars) {
        close_parc_ferme(car);
    }
}

// Whether a setup change now can still break parc ferme.
inline bool checking()
{
    if (g_phase == Phase::qualifying || g_phase == Phase::before_start) {
        return true;
    }
    return g_phase == Phase::racing && Clock::now() - g_race_started < kLateChange;
}

// The game applies a setup to a car: keeps it, and checks it against parc ferme.
inline void setup_applied(int car, Block block, const Values &values)
{
    SetupKey key{car, block};
    g_last_applied[key] = values;
    if (g_take_setup_now && !g_parc_ferme.count(key)) {
        g_parc_ferme[key] = values;
        return;
    }
    if (!checking() || g_pit_lane_start.count(car)) {
        return;
    }
    auto kept = g_parc_ferme.find(key);
    if (kept == g_parc_ferme.end()) {
        return;
    }
    std::string rows = changed_rows(kept->second, values);
    if (rows.empty()) {
        return;
    }
    g_pit_lane_start.insert(car);
    g_parc_ferme_changed = true;
    log("[improved-parc-ferme] car %d broke parc ferme (%s): pit lane start", car, rows.c_str());
    g_messages.push_back({"Parc ferme broken", "Changed " + rows + ": you start from the pit lane"});
}

} // namespace improved_parc_ferme
