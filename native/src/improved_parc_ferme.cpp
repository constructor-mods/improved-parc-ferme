// SPDX-License-Identifier: MIT
// Improved Parc Ferme: the setup rows stay open after qualifying, but changing a locked one means starting the race
// from the pit lane, like the real rule.
// Include mixin.hpp first: registry.hpp redefines SWOFTY_ADDON so the DLL exports SwoftyAddonRegister.
#include <swofty/mixin.hpp>
#include <swofty/mem.hpp>

#include <swofty/events.hpp>
#include <swofty/game.hpp>
#include <swofty/hud.hpp>
#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>

#include "parc_ferme.hpp"
#include "pit_start.hpp"
#include "sessions.hpp"
#include "setup_rows.hpp"
#include "unlock.hpp"

#include <cstdint>
#include <mutex>
#include <set>
#include <span>
#include <vector>

// The setup function is listed by address only in the headers: this gives it a type so it can be wrapped.
namespace swofty::game {
using Car_Setup_ApplyTuning_t = uint64_t(void *setup_manager, uint64_t *skip_mask, uint8_t *setup);
}

using namespace swofty;
using namespace improved_parc_ferme;

namespace {

constexpr uint32_t kOnTrack = 1;

std::set<int> g_sent_to_pit_box;

// The session's car number for a car, or -1 if it isn't on the live car list.
int car_number(uintptr_t car)
{
    for (const cars::live::Car &live : cars::live::cars()) {
        if (reinterpret_cast<uintptr_t>(live.handle()) == car) {
            return int(live.index());
        }
    }
    return -1;
}

uint32_t g_session = 0;

// A new session: qualifying (or a sprint shootout) starts parc ferme over, a race checks it until the start,
// practice forgets it.
void follow_session()
{
    uint32_t session = session_number();
    if (session == g_session || session == 0) {
        return;
    }
    g_session = session;
    g_pit_lane = nullptr;
    SessionKind kind = kind_of(session);
    if (kind == SessionKind::qualifying) {
        g_phase = Phase::qualifying;
        start_qualifying();
    } else if (kind == SessionKind::race) {
        g_phase = Phase::before_start;
    } else {
        g_phase = Phase::other;
        g_pit_lane_start.clear();
    }
    log("[improved-parc-ferme] session %u", session);
}

void frame(double, void *)
{
    std::vector<Message> messages;
    std::vector<int> to_pit_box;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        follow_session();
        messages.swap(g_messages);
        if (g_phase == Phase::racing) {
            for (int car : g_pit_lane_start) {
                if (!g_sent_to_pit_box.count(car)) {
                    g_sent_to_pit_box.insert(car);
                    to_pit_box.push_back(car);
                }
            }
        }
    }
    // Outside the lock: both call into the game.
    for (const Message &message : messages) {
        if (hud::available()) {
            hud::toast(message.text, message.detail, 5, "generic");
        }
    }
    if (!to_pit_box.empty()) {
        send_to_pit_box(to_pit_box);
        if (hud::available()) {
            hud::toast("Pit lane start", "Parc ferme: setup changed after qualifying", 5, "generic");
        }
    }
}

} // namespace

SWOFTY_WRAP(Car_Setup_ApplyTuning, uint64_t, void *setup_manager, uint64_t *skip_mask, uint8_t *setup)
{
    uintptr_t car = 0;
    Block block = Block::first;
    Values values;
    if (read_setup(setup_manager, setup, car, block, values)) {
        int number = car_number(car);
        if (number >= 0) {
            std::lock_guard<std::mutex> lock(g_mutex);
            setup_applied(number, block, values);
        }
    }
    return orig(setup_manager, skip_mask, setup);
}

SWOFTY_WRAP(Game_Pitlane_SetVehicleStatus, void, void *self, uint8_t vehicle, uint32_t status)
{
    orig(self, vehicle, status);
    g_pit_lane = self;
    if (status != kOnTrack) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_phase == Phase::qualifying) {
        close_parc_ferme(int(vehicle));
    }
}

SWOFTY_ON_LOAD
{
    keep_rows_open();
    events::on_frame(frame);
}

SWOFTY_REGISTER
{
    // A restart in qualifying builds the cars again: parc ferme starts over.
    events::on("car.built").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_phase == Phase::qualifying && !g_in_parc_ferme.empty()) {
            log("[improved-parc-ferme] qualifying restarted: parc ferme starts over");
            g_parc_ferme.clear();
            g_in_parc_ferme.clear();
            g_pit_lane_start.clear();
        }
    });
    // A restarted race fires race.start again, and the pit lane start is served again.
    events::on("race.start").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_phase = Phase::racing;
        g_race_started = Clock::now();
        g_sent_to_pit_box.clear();
    });
    events::on("session.end").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_phase == Phase::qualifying) {
            close_all();
        }
        if (g_phase == Phase::racing) {
            g_pit_lane_start.clear();
        }
        g_phase = Phase::other;
    });
}

SWOFTY_ADDON()
