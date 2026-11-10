// SPDX-License-Identifier: MIT
// The pit exit start: the car waits at the end of the pit lane, like a real pit lane starter, and goes once the
// field has passed the pit exit. Where the end of the pit lane is comes from any car driving out of the pits on the
// same track, in any session, and is kept per track.
#pragma once
#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/systems/cars.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace improved_parc_ferme {

using ExitClock = std::chrono::steady_clock;

constexpr uint32_t kLeavingPitLane = 14;
constexpr uint32_t kBackOnTrack = 1;

// Where a car is in the world: 16 floats, its position in the last row.
constexpr uintptr_t kCarBody = 0x558;
constexpr uintptr_t kBodyMatrix = 0x190;
constexpr size_t kMatrixSize = 16;
constexpr size_t kPositionX = 12;
constexpr size_t kPositionY = 13;
constexpr size_t kPositionZ = 14;
using Matrix = std::array<float, kMatrixSize>;

// How far before the pit exit line the car waits, in time at pit lane speed.
constexpr auto kBeforeTheExit = std::chrono::seconds(3);
constexpr size_t kMostSamples = 2000;
// A car counts as past the pit exit this far beyond it.
constexpr float kPastTheExit = 10.0f;
// Lights out: another car has moved this far from its grid slot.
constexpr float kMovedOff = 3.0f;
// The car is put back on its spot when it has moved this far.
constexpr float kHeldWithin = 0.5f;
// The longest wait after lights out, and before it.
constexpr auto kLongestHold = std::chrono::seconds(25);
constexpr auto kLongestBeforeLightsOut = std::chrono::seconds(60);

struct PitExit {
    Matrix spot{};
    float exit_x = 0;
    float exit_z = 0;
    float direction_x = 0;
    float direction_z = 0;
};

} // namespace improved_parc_ferme

#include "pit_exit_store.hpp"

namespace improved_parc_ferme {

struct Sample {
    ExitClock::time_point when;
    Matrix matrix{};
};

// The car waiting at the pit exit.
struct Held {
    int car = -1;
    ExitClock::time_point since;
    std::optional<ExitClock::time_point> lights_out;
    std::map<int, Matrix> others_at_start;
};

inline std::mutex g_exit_mutex;
inline std::map<int, uint32_t> g_pit_status;
inline std::map<int, std::deque<Sample>> g_pit_lane_path;
inline std::vector<int> g_just_exited;
inline std::optional<PitExit> g_pit_exit;
inline std::optional<Held> g_held;
inline std::string g_track;

// Every live car's handle by its car number.
inline std::map<int, uintptr_t> car_handles()
{
    std::map<int, uintptr_t> handles;
    for (const swofty::cars::live::Car &live : swofty::cars::live::cars()) {
        handles[int(live.index())] = reinterpret_cast<uintptr_t>(live.handle());
    }
    return handles;
}

inline uintptr_t car_handle(int car)
{
    for (const swofty::cars::live::Car &live : swofty::cars::live::cars()) {
        if (int(live.index()) == car) {
            return reinterpret_cast<uintptr_t>(live.handle());
        }
    }
    return 0;
}

inline float *car_matrix(uintptr_t handle)
{
    uintptr_t body = 0;
    if (!handle || !swofty::mem::read(handle + kCarBody, body) || !body) {
        return nullptr;
    }
    auto *matrix = reinterpret_cast<float *>(body + kBodyMatrix);
    if (!swofty::mem::readable(matrix, kMatrixSize * sizeof(float))) {
        return nullptr;
    }
    return matrix;
}

inline Matrix copy_of(const float *matrix)
{
    Matrix copy{};
    std::memcpy(copy.data(), matrix, sizeof(float) * kMatrixSize);
    return copy;
}

inline float distance_flat(const float *matrix, float x, float z)
{
    float dx = matrix[kPositionX] - x;
    float dz = matrix[kPositionZ] - z;
    return std::sqrt(dx * dx + dz * dz);
}

// The pit exit from a car's drive down the pit lane. The game marks a car back on track at different places (later
// when the player drives), so the exit is where the car leaves the pit lane speed limit: it drives at a steady speed
// down the pit lane, then speeds up. The car waits a few seconds' drive before that.
inline std::optional<PitExit> pit_exit_from_path(const std::deque<Sample> &path)
{
    // A point every quarter of a second, and the speed between them.
    std::vector<const Sample *> points;
    for (const Sample &sample : path) {
        if (points.empty() || sample.when - points.back()->when >= std::chrono::milliseconds(250)) {
            points.push_back(&sample);
        }
    }
    if (points.size() < 12) {
        return std::nullopt;
    }
    std::vector<float> speeds(points.size(), 0.0f);
    for (size_t i = 1; i < points.size(); i++) {
        float seconds = std::chrono::duration<float>(points[i]->when - points[i - 1]->when).count();
        float metres = distance_flat(points[i]->matrix.data(), points[i - 1]->matrix[kPositionX],
                                     points[i - 1]->matrix[kPositionZ]);
        speeds[i] = metres / seconds;
    }
    // The pit lane speed: the middle speed of the first half of the drive.
    std::vector<float> first_half(speeds.begin() + 1, speeds.begin() + points.size() / 2);
    std::sort(first_half.begin(), first_half.end());
    float pit_lane_speed = first_half[first_half.size() / 2];
    if (pit_lane_speed < 5.0f) {
        return std::nullopt;
    }
    // The exit: the first point after the middle where the car is clearly faster than the pit lane speed.
    size_t exit_index = points.size() - 1;
    for (size_t i = points.size() / 2; i < points.size(); i++) {
        if (speeds[i] > pit_lane_speed * 1.2f) {
            exit_index = i - 1;
            break;
        }
    }
    // Where it waits: a few seconds before the exit.
    size_t spot_index = 0;
    for (size_t i = 0; i < exit_index; i++) {
        if (points[exit_index]->when - points[i]->when >= kBeforeTheExit) {
            spot_index = i;
        }
    }
    PitExit exit;
    exit.spot = points[spot_index]->matrix;
    exit.exit_x = points[exit_index]->matrix[kPositionX];
    exit.exit_z = points[exit_index]->matrix[kPositionZ];
    float dx = exit.exit_x - exit.spot[kPositionX];
    float dz = exit.exit_z - exit.spot[kPositionZ];
    float length = std::sqrt(dx * dx + dz * dz);
    // A wait spot that isn't a short way before the exit isn't trusted.
    if (length < 20.0f || length > 150.0f) {
        swofty::log("[improved-parc-ferme] pit exit not trusted: %.0f m between the wait spot and the exit", length);
        return std::nullopt;
    }
    exit.direction_x = dx / length;
    exit.direction_z = dz / length;
    return exit;
}

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
inline void learn_pit_exit()
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
        path.push_back({now, copy_of(matrix)});
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
        if (!exit || g_pit_exit) {
            continue;
        }
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
    PitExit exit;
    if (!track.empty() && load_pit_exit(track, exit)) {
        g_pit_exit = exit;
        swofty::log("[improved-parc-ferme] pit exit of %s known from an earlier visit", track.c_str());
    }
}

inline bool pit_exit_known()
{
    std::lock_guard<std::mutex> lock(g_exit_mutex);
    return g_pit_exit.has_value();
}

} // namespace improved_parc_ferme
