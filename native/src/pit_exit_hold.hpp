// SPDX-License-Identifier: MIT
// The pit exit start at the race: the car goes to its garage, the game drives it down the pit lane at once, and it
// stops at the pit exit line. It waits there through the start until the field has passed the pit exit, then goes.
#pragma once
#include "ai_start.hpp"
#include "pit_exit.hpp"
#include "parked_timer.hpp"
#include "pit_hold.hpp"
#include "start_lights.hpp"
#include "start_prompt.hpp"

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
    for (const auto &[number, handle] : car_handles()) {
        float *matrix = car_matrix(handle);
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
        for (const auto &[other, handle] : car_handles()) {
            float *matrix = car_matrix(handle);
            if (other != car && matrix) {
                held.others_at_start[other] = copy_of(matrix);
            }
        }
        g_held = held;
        g_not_learned_from.insert(car);
        plan.car = car_handle(car);
        plan.direction_x = g_pit_exit->direction_x;
        plan.direction_z = g_pit_exit->direction_z;
        plan.stop_x = g_pit_exit->exit_x - plan.direction_x * kStopBeforeExit;
        plan.stop_z = g_pit_exit->exit_z - plan.direction_z * kStopBeforeExit;
    }
    swofty::log("[improved-parc-ferme] car %d starts from the pit lane: drives to the pit exit and waits", car);
    stop_car_at(plan);
    skip_lights_wait(car);
    hold_lights();
    free_from_start_prompt(car);
}

// What the frame has to do for the waiting car.
struct HoldNews {
    bool free_to_go = false;
    // The car is still in its garage at lights out: send it out again.
    int drive_out_again = -1;
    // The car waiting at the pit exit this frame.
    int waiting = -1;
};

// True when every racing car is past the pit exit. A car counts once it has moved off from where it was at lights
// out (the safety car and a car that never got away don't). Called with g_exit_mutex held; no game calls.
inline bool field_has_passed(const Held &held, std::map<int, uintptr_t> &handles, int &counted, int &behind)
{
    counted = 0;
    behind = 0;
    for (const auto &[other, start] : held.others_at_lights_out) {
        float *matrix = car_matrix(handles[other]);
        if (!matrix || distance_flat(matrix, start[kPositionX], start[kPositionZ]) < kMovedOff) {
            continue;
        }
        counted++;
        float along = (matrix[kPositionX] - g_pit_exit->exit_x) * g_pit_exit->direction_x +
                      (matrix[kPositionZ] - g_pit_exit->exit_z) * g_pit_exit->direction_z;
        if (along < kPastTheExit) {
            behind++;
        }
    }
    return behind == 0;
}

// Lights out: the game's start system says the race has started, or (when that isn't known) a car on the grid
// moves off. Called with g_exit_mutex held; no game calls.
inline bool lights_went_out(Held &held, std::map<int, uintptr_t> &handles)
{
    if (g_race_started_known.load()) {
        if (g_race_started_value.load() == 0) {
            held.saw_start_waiting = true;
            return false;
        }
        if (held.saw_start_waiting) {
            return true;
        }
    }
    for (const auto &[other, start] : held.others_at_start) {
        float *matrix = car_matrix(handles[other]);
        if (matrix && distance_flat(matrix, start[kPositionX], start[kPositionZ]) > kMovedOff) {
            return true;
        }
    }
    return false;
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
    news.waiting = held.car;
    auto now = ExitClock::now();
    std::map<int, uintptr_t> handles = car_handles();
    static ExitClock::time_point next_log{};
    if (g_lights_wait_skipped.exchange(false)) {
        swofty::log("[improved-parc-ferme] car %d drives on to the pit exit before the lights", held.car);
    }
    if (now >= next_log) {
        next_log = now + kHoldLogEvery;
        swofty::log("[improved-parc-ferme] car %d: %.0f km/h, %.1f m to the line, %s (%llu speed writes, gear %u, "
                    "clutch slipping %u)",
                    held.car, g_stop_speed.load() * 3.6f, g_stop_distance.load(),
                    g_standing.load() ? "held" : "driving", (unsigned long long)g_speed_writes.load(),
                    g_driving_gear.load(), g_clutch_slipping.load());
    }
    if (!held.lights_out) {
        if (g_standing.load() && g_lights_wait.load()) {
            let_lights_go();
            swofty::log("[improved-parc-ferme] car %d stands at the pit exit: the start lights may begin", held.car);
        }
        bool out = lights_went_out(held, handles);
        if (!out && now - held.since > kLongestBeforeLightsOut) {
            out = true;
        }
        if (!out) {
            return news;
        }
        held.lights_out = now;
        held.lights_out_seconds = g_game_seconds.load();
        for (const auto &[other, handle] : handles) {
            float *matrix = car_matrix(handle);
            if (other != held.car && matrix) {
                held.others_at_lights_out[other] = copy_of(matrix);
            }
        }
        swofty::log("[improved-parc-ferme] lights out: car %d waits for the field (%zu cars)", held.car,
                    held.others_at_lights_out.size());
        return news;
    }
    auto status = g_pit_status.find(held.car);
    if (!held.driven_out_again && status != g_pit_status.end() && status->second == kWaitingInGarage) {
        held.driven_out_again = true;
        news.drive_out_again = held.car;
    }
    double waited = g_game_seconds.load() - held.lights_out_seconds;
    if (waited < kFieldCheckAfter) {
        return news;
    }
    int counted = 0;
    int behind = 0;
    bool all_past = field_has_passed(held, handles, counted, behind);
    if (!all_past) {
        held.field_past_seconds = -1;
    } else if (held.field_past_seconds < 0) {
        held.field_past_seconds = g_game_seconds.load();
        swofty::log("[improved-parc-ferme] car %d: the field has passed the pit exit (%d cars, %.1f s after lights out)",
                    held.car, counted, waited);
    }
    bool green = false;
    if (all_past && g_standing.load()) {
        green = g_game_seconds.load() - held.field_past_seconds >= kGreenAfter;
    }
    bool too_long = waited > std::chrono::duration<double>(kLongestHold).count();
    if (green || too_long) {
        swofty::log("[improved-parc-ferme] car %d: pit exit open (%s, %d cars racing, %d behind, %.1f s after lights "
                    "out)",
                    held.car, green ? "the field has passed" : "longest wait", counted, behind, waited);
        g_held.reset();
        let_car_go();
        stop_skipping_lights_wait();
        start_prompt_as_normal();
        news.free_to_go = true;
        news.waiting = -1;
    }
    return news;
}

// A race restart, the end of the session or a new track: nobody waits any more.
inline void stop_waiting()
{
    std::lock_guard<std::mutex> lock(g_exit_mutex);
    g_held.reset();
    g_not_learned_from.clear();
    let_car_go();
    stop_skipping_lights_wait();
    let_lights_go();
    start_prompt_as_normal();
}

} // namespace improved_parc_ferme
