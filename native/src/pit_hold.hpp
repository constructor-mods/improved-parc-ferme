// SPDX-License-Identifier: MIT
// Stopping a car at the end of the pit lane and keeping it there. The car's speed is turned down a little every
// physics step as it comes up to the line, the way the game's own pit limiter slows a car, until it stands; then its
// speed is kept at zero until it is let go. This runs right after the game's start system each step.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>

namespace improved_parc_ferme {

using HoldClock = std::chrono::steady_clock;

// Where the car should stop, and which way the pit lane runs there.
struct StopPlan {
    uintptr_t car = 0;
    float stop_x = 0;
    float stop_z = 0;
    float direction_x = 0;
    float direction_z = 0;
};

// The game's start system update, in its table (on this game build).
constexpr uintptr_t kStartUpdateSlot = 0x57f8b28;
constexpr uintptr_t kStartUpdate = 0x2f4a070;
using StartUpdate = void (*)(void *self, void *context);

// The start system's own "the race has started" value: 0 until lights out.
constexpr uintptr_t kRaceStarted = 0x100;

// The start system's list of cars: each entry's car, and the driver it holds still until lights out.
constexpr uintptr_t kStartEntriesBegin = 0x48;
constexpr uintptr_t kStartEntriesEnd = 0x50;
constexpr uintptr_t kEntryCar = 0x50;
constexpr uintptr_t kEntryDriver = 0x58;
constexpr uintptr_t kDriverFlags = 0x1c;
constexpr uint32_t kHeldForTheStart = 0x20;

// The start lock the game puts on each car before the lights; cleared when the car is let go.
constexpr uintptr_t kLockBits = 0x1fc;
constexpr uint32_t kStartLock = 2;

// The car's physics body and its speed.
constexpr uintptr_t kHoldCarBody = 0x558;
constexpr uintptr_t kHoldBodyMatrix = 0x190;
constexpr uintptr_t kChassisBody = 0x340;
constexpr uintptr_t kBodyState = 0x18;
constexpr uintptr_t kLinearVelocity = 0x210;
constexpr uintptr_t kAngularVelocity = 0x220;
constexpr uintptr_t kSetAngularVelocity = 0x40c2080;
constexpr uintptr_t kSetLinearVelocity = 0x40c2290;
constexpr uint8_t kSetVelocityStart[] = {0x56, 0x48, 0x83, 0xec, 0x70, 0xc5, 0x78, 0x29, 0x54, 0x24, 0x60, 0xc5};
using SetVelocity = void (*)(void *body, const float *velocity);

// Slowing starts this far before the stop.
constexpr float kSlowFrom = 40.0f;
// How hard the car slows on the way in (m/s each second): a gentle stop.
constexpr float kBraking = 5.0f;
// How quickly the speed is turned down while the car is faster than it should be (each second).
constexpr float kEaseOff = 3.0f;
// The car counts as at the line this close to it, or standing below this speed near it.
constexpr float kAtTheLine = 0.5f;
constexpr float kNearTheLine = 3.0f;
constexpr float kStanding = 0.5f;
// Speed is measured over this long.
constexpr auto kSpeedWindow = std::chrono::milliseconds(150);

inline StartUpdate g_start_update = nullptr;
// Shared between the frame and the start system's update. Never held around a game call.
inline std::mutex g_stop_mutex;
inline std::optional<StopPlan> g_stop_plan;
inline std::atomic<bool> g_standing{false};
inline std::atomic<uintptr_t> g_let_go{0};
// Seconds the game has run (pauses don't count), for the longest wait.
inline std::atomic<double> g_game_seconds{0};
// The start system's "the race has started" value, last step.
inline std::atomic<uint32_t> g_race_started_value{0};
inline std::atomic<bool> g_race_started_known{false};
// The last speed and distance to the stop, for the log.
inline std::atomic<float> g_stop_speed{0};
inline std::atomic<float> g_stop_distance{0};
inline std::atomic<uint64_t> g_speed_writes{0};

struct SpeedSample {
    HoldClock::time_point when;
    float x = 0;
    float z = 0;
};

inline uintptr_t physics_body(uintptr_t car)
{
    uintptr_t chassis = 0;
    uintptr_t body = 0;
    uintptr_t state = 0;
    if (!car || !swofty::mem::read(car + kHoldCarBody, chassis) || !chassis) {
        return 0;
    }
    if (!swofty::mem::read(chassis + kChassisBody, body) || !body) {
        return 0;
    }
    if (!swofty::mem::read(body + kBodyState, state) || !state) {
        return 0;
    }
    if (!swofty::mem::readable(state + kLinearVelocity, 0x20)) {
        return 0;
    }
    return body;
}

// Multiplies the car's speed (and its turning) by a factor: 0 stops it. Calls into the game.
inline void scale_speed(uintptr_t car, float factor)
{
    uintptr_t body = physics_body(car);
    if (!body) {
        return;
    }
    uintptr_t module = swofty::api->module_base();
    auto *set_linear = reinterpret_cast<uint8_t *>(module + kSetLinearVelocity);
    auto *set_angular = reinterpret_cast<uint8_t *>(module + kSetAngularVelocity);
    if (std::memcmp(set_linear, kSetVelocityStart, sizeof kSetVelocityStart) != 0) {
        return;
    }
    if (std::memcmp(set_angular, kSetVelocityStart, sizeof kSetVelocityStart) != 0) {
        return;
    }
    uintptr_t state = *reinterpret_cast<uintptr_t *>(body + kBodyState);
    alignas(16) float linear[4];
    alignas(16) float angular[4];
    std::memcpy(linear, reinterpret_cast<void *>(state + kLinearVelocity), sizeof linear);
    std::memcpy(angular, reinterpret_cast<void *>(state + kAngularVelocity), sizeof angular);
    for (int i = 0; i < 3; i++) {
        linear[i] *= factor;
        angular[i] *= factor;
    }
    reinterpret_cast<SetVelocity>(set_angular)(reinterpret_cast<void *>(body), angular);
    reinterpret_cast<SetVelocity>(set_linear)(reinterpret_cast<void *>(body), linear);
    g_speed_writes++;
}

// The car's speed in m/s from where it has been over the last moment.
inline float measured_speed(std::deque<SpeedSample> &samples, HoldClock::time_point now, float x, float z)
{
    samples.push_back({now, x, z});
    while (samples.size() > 2 && now - samples[1].when >= kSpeedWindow) {
        samples.pop_front();
    }
    const SpeedSample &first = samples.front();
    double seconds = std::chrono::duration<double>(now - first.when).count();
    if (seconds < 0.03) {
        return -1;
    }
    return float(std::hypot(x - first.x, z - first.z) / seconds);
}

// Before the lights the game holds every car still, also one in the pit lane: this car may drive on to the pit exit.
// Memory only, no game calls.
inline void let_drive_before_the_start(void *start_system, uintptr_t car)
{
    auto system = reinterpret_cast<uintptr_t>(start_system);
    uintptr_t begin = 0;
    uintptr_t end = 0;
    if (!swofty::mem::read(system + kStartEntriesBegin, begin)) {
        return;
    }
    if (!swofty::mem::read(system + kStartEntriesEnd, end)) {
        return;
    }
    if (end < begin) {
        return;
    }
    for (uintptr_t at = begin; at < end; at += sizeof(uintptr_t)) {
        uintptr_t entry = 0;
        uintptr_t entry_car = 0;
        uintptr_t driver = 0;
        uint32_t flags = 0;
        if (!swofty::mem::read(at, entry) || !entry || !swofty::mem::read(entry + kEntryCar, entry_car)) {
            continue;
        }
        if (entry_car != car || !swofty::mem::read(entry + kEntryDriver, driver) || !driver) {
            continue;
        }
        if (!swofty::mem::read(driver + kDriverFlags, flags) || !(flags & kHeldForTheStart)) {
            continue;
        }
        *reinterpret_cast<uint32_t *>(driver + kDriverFlags) = flags & ~kHeldForTheStart;
        static bool logged = false;
        if (!logged) {
            logged = true;
            swofty::log("[improved-parc-ferme] the waiting car may drive on before the start");
        }
    }
}

// One physics step for the car being stopped. Calls into the game, never while holding a lock.
inline void stop_step(const StopPlan &plan, float step)
{
    static std::deque<SpeedSample> samples;
    static uintptr_t followed = 0;
    if (followed != plan.car) {
        followed = plan.car;
        samples.clear();
    }
    uintptr_t body = 0;
    if (!swofty::mem::read(plan.car + kHoldCarBody, body) || !body) {
        return;
    }
    auto *matrix = reinterpret_cast<float *>(body + kHoldBodyMatrix);
    if (!swofty::mem::readable(matrix, 16 * sizeof(float))) {
        return;
    }
    float x = matrix[12];
    float z = matrix[14];
    float to_go = (plan.stop_x - x) * plan.direction_x + (plan.stop_z - z) * plan.direction_z;
    float speed = measured_speed(samples, HoldClock::now(), x, z);
    g_stop_distance = to_go;
    if (speed >= 0) {
        g_stop_speed = speed;
    }
    if (!g_standing.load()) {
        bool at_line = to_go <= kAtTheLine;
        bool standing_near = speed >= 0 && speed < kStanding && to_go < kNearTheLine;
        if (at_line || standing_near) {
            g_standing = true;
            swofty::log("[improved-parc-ferme] stopped at the pit exit (%.1f m from the line, %.1f km/h)", to_go,
                        speed * 3.6f);
        } else if (to_go < kSlowFrom && speed >= 0) {
            float allowed = std::sqrt(2.0f * kBraking * std::max(to_go, 0.0f));
            if (speed > allowed) {
                scale_speed(plan.car, std::max(0.0f, 1.0f - kEaseOff * step));
            }
        }
    }
    if (g_standing.load()) {
        scale_speed(plan.car, 0.0f);
    }
}

inline void start_update(void *self, void *context)
{
    g_start_update(self, context);
    uint32_t started = 0;
    if (swofty::mem::read(reinterpret_cast<uintptr_t>(self) + kRaceStarted, started)) {
        uint32_t before = g_race_started_value.exchange(started);
        if ((before == 0) != (started == 0) || !g_race_started_known.load()) {
            swofty::log("[improved-parc-ferme] start system: race started value %#x", started);
        }
        g_race_started_known = true;
    }
    static HoldClock::time_point last{};
    auto now = HoldClock::now();
    double step = std::chrono::duration<double>(now - last).count();
    last = now;
    // A long gap is a pause or a load: not counted.
    if (step <= 0 || step > 0.2) {
        return;
    }
    g_game_seconds = g_game_seconds.load() + step;
    uintptr_t let_go = g_let_go.exchange(0);
    if (let_go && swofty::mem::readable(let_go + kLockBits, sizeof(uint32_t))) {
        *reinterpret_cast<uint32_t *>(let_go + kLockBits) &= ~kStartLock;
    }
    std::optional<StopPlan> plan;
    {
        std::lock_guard<std::mutex> lock(g_stop_mutex);
        plan = g_stop_plan;
    }
    if (plan) {
        if (g_race_started_known.load() && g_race_started_value.load() == 0) {
            let_drive_before_the_start(self, plan->car);
        }
        stop_step(*plan, float(step));
    }
}

// At load: runs our step right after the game's start system.
inline void follow_start_system()
{
    uintptr_t module = swofty::api->module_base();
    auto *slot = reinterpret_cast<uintptr_t *>(module + kStartUpdateSlot);
    if (!swofty::mem::readable(reinterpret_cast<uintptr_t>(slot), sizeof(uintptr_t)) ||
        *slot != module + kStartUpdate) {
        swofty::log("[improved-parc-ferme] this game build is not supported: pit lane starts from the garage");
        return;
    }
    g_start_update = reinterpret_cast<StartUpdate>(*slot);
    DWORD old_protect = 0;
    if (!VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &old_protect)) {
        g_start_update = nullptr;
        return;
    }
    *slot = reinterpret_cast<uintptr_t>(&start_update);
    VirtualProtect(slot, sizeof(uintptr_t), old_protect, &old_protect);
}

inline bool can_stop_cars()
{
    return g_start_update != nullptr;
}

inline void stop_car_at(const StopPlan &plan)
{
    std::lock_guard<std::mutex> lock(g_stop_mutex);
    g_stop_plan = plan;
    g_standing = false;
}

// The car may go: no more speed writes, and the start lock is taken off.
inline void let_car_go()
{
    std::lock_guard<std::mutex> lock(g_stop_mutex);
    if (g_stop_plan) {
        g_let_go = g_stop_plan->car;
    }
    g_stop_plan.reset();
    g_standing = false;
}

} // namespace improved_parc_ferme
