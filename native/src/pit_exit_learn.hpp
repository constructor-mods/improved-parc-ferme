// SPDX-License-Identifier: MIT
// Following the cars that drive out of the pits, and the pit exit of the track each session.
#pragma once
#include "pit_exit.hpp"

namespace improved_parc_ferme {

// A car's pit status changed (from the game's own pit status hook). No game calls in here.
inline void pit_status_changed(int car, uint32_t status)
{
    std::lock_guard<std::mutex> lock(g_exit_mutex);
    uint32_t before = g_pit_status[car];
    g_pit_status[car] = status;
    if (status == kBackOnTrack && before == kLeavingPitLane) {
        g_just_exited.push_back(car);
    } else if (status != kLeavingPitLane) {
        g_pit_lane_path.erase(car);
    }
}

// In practice and qualifying, every frame: follows the cars driving down the pit lane and keeps where the end of the
// pit lane is.
inline void learn_pit_exit(void *pit_lane)
{
    std::lock_guard<std::mutex> lock(g_exit_mutex);
    auto now = ExitClock::now();
    for (const auto &[car, status] : g_pit_status) {
        if (status != kLeavingPitLane) {
            continue;
        }
        float *matrix = car_matrix(car_handle(car));
        if (!matrix) {
            continue;
        }
        std::deque<Sample> &path = g_pit_lane_path[car];
        path.push_back({now, copy_of(matrix), speed_limit_of(pit_lane, car)});
        if (path.size() > kMostSamples) {
            path.pop_front();
        }
    }
    for (int car : g_just_exited) {
        auto path = g_pit_lane_path.find(car);
        float *matrix = car_matrix(car_handle(car));
        if (path == g_pit_lane_path.end() || path->second.empty() || !matrix) {
            continue;
        }
        std::optional<PitExit> exit = pit_exit_from_path(path->second);
        g_pit_lane_path.erase(path);
        // A car's own drive out is kept over the addon's list.
        if (!exit || (g_pit_exit && !g_pit_exit_built_in)) {
            continue;
        }
        g_pit_exit_built_in = false;
        swofty::log("[improved-parc-ferme] pit exit found on %s: wait at %.1f %.1f %.1f, exit at %.1f %.1f",
                    g_track.c_str(), exit->spot[kPositionX], exit->spot[kPositionY], exit->spot[kPositionZ],
                    exit->exit_x, exit->exit_z);
        save_pit_exit(g_track, *exit);
        g_pit_exit = exit;
    }
    g_just_exited.clear();
}

// A session starts on a track: its pit exit, if a car has driven out of the pits there before.
inline void at_track(const std::string &track)
{
    std::lock_guard<std::mutex> lock(g_exit_mutex);
    g_pit_lane_path.clear();
    g_pit_status.clear();
    g_just_exited.clear();
    g_held.reset();
    if (track == g_track && g_pit_exit) {
        return;
    }
    g_track = track;
    g_pit_exit.reset();
    g_pit_exit_built_in = false;
    PitExit exit;
    if (track.empty()) {
        return;
    }
    if (load_pit_exit(track, exit)) {
        g_pit_exit = exit;
        swofty::log("[improved-parc-ferme] pit exit of %s known from an earlier visit", track.c_str());
        return;
    }
    const char *known = known_pit_exit(track);
    if (known && pit_exit_from(known, exit)) {
        g_pit_exit = exit;
        g_pit_exit_built_in = true;
        swofty::log("[improved-parc-ferme] pit exit of %s known from the addon", track.c_str());
    }
}

inline bool pit_exit_known()
{
    std::lock_guard<std::mutex> lock(g_exit_mutex);
    return g_pit_exit.has_value();
}

} // namespace improved_parc_ferme
