// SPDX-License-Identifier: MIT
// Quitting the game between qualifying and the race doesn't get you out of parc ferme. The addon keeps which track
// had its qualifying and who already broke parc ferme. Back in the race, the setup each car has then becomes its parc
// ferme setup. One weekend at a time: practice or a new qualifying starts it over.
#pragma once
#include "parc_ferme.hpp"

#include <swofty/storage.hpp>

#include <sstream>
#include <string>

namespace improved_parc_ferme {

constexpr const char *kParcFermeFile = "parc_ferme.txt";

// The track of this weekend, from the session start.
inline std::string g_weekend_track;

// Called while holding g_mutex. The file is ours: no game call.
inline void save_parc_ferme()
{
    g_parc_ferme_changed = false;
    if (g_weekend_track.empty()) {
        return;
    }
    std::string text = "track " + g_weekend_track + "\n";
    for (int car : g_pit_lane_start) {
        text += "start " + std::to_string(car) + "\n";
    }
    swofty::storage::write(kParcFermeFile, text);
}

// Called while holding g_mutex.
inline void forget_saved_parc_ferme()
{
    swofty::storage::write(kParcFermeFile, "");
}

// A race with no parc ferme from this launch: if qualifying at this track was before the game was closed, parc
// ferme goes on with the setups the cars have now. Called while holding g_mutex.
inline void load_parc_ferme()
{
    if (!g_parc_ferme.empty() || g_take_setup_now || g_weekend_track.empty()) {
        return;
    }
    std::string text;
    if (swofty::storage::read(kParcFermeFile, text) != swofty::storage::Result::ok) {
        return;
    }
    std::istringstream input(text);
    std::string word;
    std::string track;
    if (!(input >> word >> track) || word != "track" || track != g_weekend_track) {
        return;
    }
    int car = -1;
    while (input >> word >> car) {
        if (word == "start") {
            g_pit_lane_start.insert(car);
        }
    }
    // The setups already applied in this race are the ones the cars had when the game was closed.
    g_parc_ferme = g_last_applied;
    g_take_setup_now = true;
    log("[improved-parc-ferme] qualifying at %s was before the game was closed: parc ferme goes on, %zu pit lane "
        "starts",
        track.c_str(), g_pit_lane_start.size());
}

} // namespace improved_parc_ferme
