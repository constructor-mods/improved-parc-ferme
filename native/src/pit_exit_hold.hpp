// SPDX-License-Identifier: MIT
// The pit exit start at the race: the car goes to the end of the pit lane and is kept there after lights out until
// the field has passed the pit exit.
#pragma once
#include "pit_exit.hpp"

namespace improved_parc_ferme {

// Which way a car faces, in its matrix.
constexpr size_t kForwardX = 8;
constexpr size_t kForwardZ = 10;
// The pit exit points against the grid: it is from the same track driven the other way.
constexpr float kOtherWay = -0.5f;

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

// Never called while holding a lock: the teleport calls into the game.
inline void put_on_spot(uintptr_t handle, const Matrix &spot)
{
    float *matrix = car_matrix(handle);
    if (!matrix) {
        return;
    }
    static const char kTag[] = "ParcFermePitLaneStart";
    Matrix target = spot;
    swofty::game::Game_Teleport_Vehicle(reinterpret_cast<void *>(handle), kTag, matrix, target.data(),
                                        1.0f / 60.0f, 0x800);
}

// At the race start: the car goes to the end of the pit lane. Never called while holding a lock.
inline void start_at_pit_exit(int car)
{
    Matrix spot{};
    Held held;
    {
        std::lock_guard<std::mutex> lock(g_exit_mutex);
        if (!g_pit_exit) {
            return;
        }
        spot = g_pit_exit->spot;
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
    }
    swofty::log("[improved-parc-ferme] car %d starts from the pit lane: waits at the pit exit", car);
    put_on_spot(car_handle(car), spot);
}

// Every frame in the race: keeps the waiting car on its spot until the field has passed. True once it's free to go.
// Never called while holding a lock.
inline bool hold_at_pit_exit()
{
    int car = -1;
    Matrix spot{};
    bool put_back = false;
    bool free_to_go = false;
    {
        std::lock_guard<std::mutex> lock(g_exit_mutex);
        if (!g_held || !g_pit_exit) {
            return false;
        }
        Held &held = *g_held;
        car = held.car;
        spot = g_pit_exit->spot;
        auto now = ExitClock::now();
        std::map<int, uintptr_t> handles = car_handles();
        // Lights out: the cars on the grid start to move.
        if (!held.lights_out) {
            for (const auto &[other, start] : held.others_at_start) {
                float *matrix = car_matrix(handles[other]);
                if (matrix && distance_flat(matrix, start[kPositionX], start[kPositionZ]) > kMovedOff) {
                    held.lights_out = now;
                    swofty::log("[improved-parc-ferme] lights out: car %d waits for the field", car);
                    break;
                }
            }
            if (!held.lights_out && now - held.since > kLongestBeforeLightsOut) {
                held.lights_out = now;
            }
        }
        if (held.lights_out) {
            // Free once every other car is past the pit exit, or after the longest wait.
            bool all_past = true;
            for (const auto &[other, start] : held.others_at_start) {
                // Only the cars racing: the safety car waits in the pit lane and never passes.
                auto status = g_pit_status.find(other);
                if (status == g_pit_status.end() || status->second != kBackOnTrack) {
                    continue;
                }
                float *matrix = car_matrix(handles[other]);
                if (!matrix) {
                    continue;
                }
                float along = (matrix[kPositionX] - g_pit_exit->exit_x) * g_pit_exit->direction_x +
                              (matrix[kPositionZ] - g_pit_exit->exit_z) * g_pit_exit->direction_z;
                if (along < kPastTheExit) {
                    all_past = false;
                    break;
                }
            }
            if (all_past || now - *held.lights_out > kLongestHold) {
                swofty::log("[improved-parc-ferme] car %d: pit exit open (%s)", car,
                            all_past ? "the field has passed" : "longest wait");
                g_held.reset();
                free_to_go = true;
            } else {
                float *matrix = car_matrix(handles[car]);
                put_back = matrix && distance_flat(matrix, spot[kPositionX], spot[kPositionZ]) > kHeldWithin;
            }
        }
    }
    if (put_back) {
        put_on_spot(car_handle(car), spot);
    }
    return free_to_go;
}

} // namespace improved_parc_ferme
