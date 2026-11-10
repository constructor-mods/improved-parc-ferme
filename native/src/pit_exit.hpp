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
#include "pit_exit_table.hpp"
#include "pit_limit.hpp"

namespace improved_parc_ferme {

struct Sample {
    ExitClock::time_point when;
    Matrix matrix{};
    SpeedLimit limit = SpeedLimit::unknown;
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
// The pit exit came from the addon's list, not from a car on this PC.
inline bool g_pit_exit_built_in = false;
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

// Where the car left the game's speed limit zone: its last point inside it. None when the game didn't say.
inline std::optional<size_t> exit_from_limit_zone(const std::vector<const Sample *> &points)
{
    std::optional<size_t> last_inside;
    for (size_t i = 0; i < points.size(); i++) {
        if (points[i]->limit == SpeedLimit::inside) {
            last_inside = i;
        }
    }
    if (!last_inside || *last_inside + 1 >= points.size()) {
        return std::nullopt;
    }
    return last_inside;
}

// Where the car left the speed limit, from its speed: it drives at a steady speed down the pit lane, then speeds up.
inline std::optional<size_t> exit_from_speed(const std::vector<const Sample *> &points)
{
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
    for (size_t i = points.size() / 2; i < points.size(); i++) {
        if (speeds[i] > pit_lane_speed * 1.2f) {
            return i - 1;
        }
    }
    return points.size() - 1;
}

// The pit exit at one point of the drive, with the spot a few seconds' drive before it to wait at.
inline std::optional<PitExit> pit_exit_at(const std::vector<const Sample *> &points, size_t exit_index)
{
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

// The pit exit from a car's drive down the pit lane. The game marks a car back on track at different places (later
// when the player drives), so the exit is where the car leaves the pit lane speed limit.
inline std::optional<PitExit> pit_exit_from_path(const std::deque<Sample> &path)
{
    // A point every quarter of a second.
    std::vector<const Sample *> points;
    for (const Sample &sample : path) {
        if (points.empty() || sample.when - points.back()->when >= std::chrono::milliseconds(250)) {
            points.push_back(&sample);
        }
    }
    if (points.size() < 12) {
        return std::nullopt;
    }
    // The game's own speed limit zone when it says, else (or when that gives no good spot) the car's speed.
    std::optional<PitExit> exit;
    std::optional<size_t> found = exit_from_limit_zone(points);
    if (found) {
        exit = pit_exit_at(points, *found);
    }
    if (!exit) {
        found = exit_from_speed(points);
        if (found) {
            exit = pit_exit_at(points, *found);
        }
    }
    return exit;
}

} // namespace improved_parc_ferme

#include "pit_exit_learn.hpp"
