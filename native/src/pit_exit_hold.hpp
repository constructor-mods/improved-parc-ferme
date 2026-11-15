// SPDX-License-Identifier: MIT
// The pit exit start at the race: the car goes to its garage, the game drives it down the pit lane at once, and it
// stops at the pit exit line. It waits there through the start until the field has passed the pit exit, then goes.
#pragma once
#include "pit_exit.hpp"
#include "pit_hold.hpp"

namespace improved_parc_ferme {

// Which way a car faces, in its matrix.
constexpr size_t kForwardX = 8;
constexpr size_t kForwardZ = 10;
// The pit exit points against the grid: it is from the same track driven the other way.
constexpr float kOtherWay = -0.5f;
constexpr uint32_t kWaitingInGarage = 9;
// How often the waiting car is logged.
constexpr auto kHoldLogEvery = std::chrono::seconds(1);

// True when the known pit exit points the way the cars on the grid face. A reverse layout has the same length as the
// normal one, so its pit exit can't be told apart by the track alone. Never called while holding a lock.
inline bool pit_exit_fits_the_grid()
{
    std::lock_guard<std::mutex> lock(g_exit_mutex);
    if (!g_pit_exit) {
        return false;
    }
    float along = 0;
    int counted = 0;
    for (const swofty::cars::live::Car &live : swofty::cars::live::cars()) {
        float *matrix = car_matrix(reinterpret_cast<uintptr_t>(live.handle()));
        if (!matrix) {
            continue;
        }
        along += matrix[kForwardX] * g_pit_exit->direction_x + matrix[kForwardZ] * g_pit_exit->direction_z;
        counted++;
    }
    if (counted == 0) {
        return true;
    }
    return along / float(counted) > kOtherWay;
}

// At the race start, as the car goes to its garage: it will stop at the pit exit line. Never called while holding a
// lock.
inline void start_at_pit_exit(int car)
{
    StopPlan plan;
    {
        std::lock_guard<std::mutex> lock(g_exit_mutex);
        if (!g_pit_exit) {
            return;
        }
        Held held;
        held.car = car;
        held.since = ExitClock::now();
        for (const swofty::cars::live::Car &live : swofty::cars::live::cars()) {
            int other = int(live.index());
            float *matrix = car_matrix(reinterpret_cast<uintptr_t>(live.handle()));
            if (other != car && matrix) {
                held.others_at_start[other] = copy_of(matrix);
            }
        }
        g_held = held;
        plan.car = car_handle(car);
        plan.direction_x = g_pit_exit->direction_x;
        plan.direction_z = g_pit_exit->direction_z;
        plan.stop_x = g_pit_exit->exit_x - plan.direction_x * kStopBeforeExit;
        plan.stop_z = g_pit_exit->exit_z - plan.direction_z * kStopBeforeExit;
    }
    swofty::log("[improved-parc-ferme] car %d starts from the pit lane: drives to the pit exit and waits", car);
    stop_car_at(plan);
}

// What the frame has to do for the waiting car.
struct HoldNews {
    bool free_to_go = false;
    // The car is still in its garage at lights out: send it out again.
    int drive_out_again = -1;
};

// True when every racing car is past the pit exit. Called with g_exit_mutex held; no game calls.
inline bool field_has_passed(const Held &held, std::map<int, uintptr_t> &handles)
{
    for (const auto &[other, start] : held.others_at_start) {
        // A car the game has in the pits (the safety car waits there) never passes.
        auto status = g_pit_status.find(other);
        if (status != g_pit_status.end() && status->second != kBackOnTrack) {
            continue;
        }
        float *matrix = car_matrix(handles[other]);
        if (!matrix) {
            continue;
        }
        float along = (matrix[kPositionX] - g_pit_exit->exit_x) * g_pit_exit->direction_x +
                      (matrix[kPositionZ] - g_pit_exit->exit_z) * g_pit_exit->direction_z;
        if (along < kPastTheExit) {
            return false;
        }
    }
    return true;
}

// Every frame in the race: watches the start and the field, and lets the car go once the field has passed.
// Never called while holding a lock.
inline HoldNews hold_at_pit_exit()
{
    HoldNews news;
    std::lock_guard<std::mutex> lock(g_exit_mutex);
    if (!g_held || !g_pit_exit) {
        return news;
    }
    Held &held = *g_held;
    auto now = ExitClock::now();
    std::map<int, uintptr_t> handles = car_handles();
    static ExitClock::time_point next_log{};
    if (now >= next_log) {
        next_log = now + kHoldLogEvery;
        swofty::log("[improved-parc-ferme] car %d: %.0f km/h, %.1f m to the line, %s (%llu speed writes)", held.car,
                    g_stop_speed.load() * 3.6f, g_stop_distance.load(), g_standing.load() ? "held" : "driving",
                    (unsigned long long)g_speed_writes.load());
    }
    // Lights out: the cars on the grid start to move.
    if (!held.lights_out) {
        for (const auto &[other, start] : held.others_at_start) {
            float *matrix = car_matrix(handles[other]);
            if (matrix && distance_flat(matrix, start[kPositionX], start[kPositionZ]) > kMovedOff) {
                held.lights_out = now;
                swofty::log("[improved-parc-ferme] lights out: car %d waits for the field", held.car);
                break;
            }
        }
        if (!held.lights_out && now - held.since > kLongestBeforeLightsOut) {
            held.lights_out = now;
        }
        if (held.lights_out) {
            held.lights_out_seconds = g_game_seconds.load();
        }
        return news;
    }
    auto status = g_pit_status.find(held.car);
    if (!held.driven_out_again && status != g_pit_status.end() && status->second == kWaitingInGarage) {
        held.driven_out_again = true;
        news.drive_out_again = held.car;
    }
    bool all_past = field_has_passed(held, handles);
    double waited = g_game_seconds.load() - held.lights_out_seconds;
    if (all_past || waited > std::chrono::duration<double>(kLongestHold).count()) {
        swofty::log("[improved-parc-ferme] car %d: pit exit open (%s)", held.car,
                    all_past ? "the field has passed" : "longest wait");
        g_held.reset();
        let_car_go();
        news.free_to_go = true;
    }
    return news;
}

// A race restart, the end of the session or a new track: nobody waits any more.
inline void stop_waiting()
{
    std::lock_guard<std::mutex> lock(g_exit_mutex);
    g_held.reset();
    let_car_go();
}

} // namespace improved_parc_ferme
