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
#include "parc_ferme_store.hpp"
#include "pit_exit.hpp"
#include "pit_exit_hold.hpp"
#include "pit_start.hpp"
#include "race_hud.hpp"
#include "sessions.hpp"
#include "setup_rows.hpp"
#include "unlock.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <vector>

// The setup function is listed by address only in the headers: this gives it a type so it can be wrapped.
namespace swofty::game {
using Car_Setup_ApplyTuning_t = uint64_t(void *setup_manager, uint64_t *skip_mask, uint8_t *setup);
}

using namespace swofty;
using namespace improved_parc_ferme;

namespace {

// Cars sent to their garage at this race start, and when.
std::map<int, Clock::time_point> g_in_garage;
std::set<int> g_driven_out;
// Cars sent to the end of the pit lane at this race start.
std::set<int> g_at_pit_exit;

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
        forget_saved_parc_ferme();
    } else if (kind == SessionKind::race) {
        g_phase = Phase::before_start;
        load_parc_ferme();
    } else {
        g_phase = Phase::other;
        g_pit_lane_start.clear();
        if (kind == SessionKind::practice) {
            forget_saved_parc_ferme();
        }
    }
    log("[improved-parc-ferme] session %u", session);
}

void frame(double, void *)
{
    std::vector<Message> messages;
    std::vector<int> to_garage;
    std::vector<int> to_pit_exit;
    std::vector<int> to_drive_out;
    void *pit_lane = g_pit_lane;
    bool racing = false;
    bool waiting_in_garage = false;
    bool exit_known = pit_exit_known();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        follow_session();
        if (g_parc_ferme_changed) {
            save_parc_ferme();
        }
        messages.swap(g_messages);
        racing = g_phase == Phase::racing;
        if (racing && pit_lane) {
            auto now = Clock::now();
            for (int car : g_pit_lane_start) {
                if (g_in_garage.count(car) || g_at_pit_exit.count(car)) {
                    continue;
                }
                // The end of the pit lane when we know where it is on this track, else the garage.
                if (exit_known && g_at_pit_exit.empty()) {
                    g_at_pit_exit.insert(car);
                    to_pit_exit.push_back(car);
                } else {
                    g_in_garage[car] = now;
                    to_garage.push_back(car);
                }
            }
            for (const auto &[car, since] : g_in_garage) {
                if (g_driven_out.count(car)) {
                    continue;
                }
                if (now - since >= kDriveOutDelay) {
                    g_driven_out.insert(car);
                    to_drive_out.push_back(car);
                } else {
                    waiting_in_garage = true;
                }
            }
        }
    }
    // Outside the lock: all of these call into the game.
    // Every session, the race too: a car driving out of the pits shows where the end of the pit lane is.
    learn_pit_exit();
    for (const Message &message : messages) {
        if (hud::available()) {
            hud::toast(message.text, message.detail, 5, "generic");
        }
    }
    for (int car : to_pit_exit) {
        start_at_pit_exit(car);
        // In the pit lane, not stopped on the track: no yellow flag for the waiting car.
        game::Game_Pitlane_SetVehicleStatus(pit_lane, uint8_t(car), kLeavingPitLane);
        if (hud::available()) {
            hud::toast("Pit lane start", "Parc ferme: wait at the pit exit until the field has passed", 5, "generic");
        }
    }
    if (racing && hold_at_pit_exit() && hud::available()) {
        hud::toast("Pit exit open", "Go!", 4, "generic");
    }
    if (!to_garage.empty()) {
        send_to_garage(pit_lane, to_garage);
        if (hud::available()) {
            hud::toast("Pit lane start", "Parc ferme: setup changed after qualifying", 5, "generic");
        }
    }
    if (waiting_in_garage) {
        keep_race_hud_on();
    }
    if (!to_drive_out.empty()) {
        drive_out(pit_lane, to_drive_out);
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
    pit_status_changed(int(vehicle), status);
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
    // The track of the session, for the end of its pit lane.
    events::on("session.start").after([](std::span<const std::byte>, const events::Context &context) {
        std::string track;
        if (context.session()) {
            track = std::string(context.session()->track.id());
        }
        std::string name = track_name(track);
        at_track(name);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_weekend_track = name;
        if (g_phase == Phase::before_start) {
            load_parc_ferme();
        }
    });
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
        g_in_garage.clear();
        g_driven_out.clear();
        g_at_pit_exit.clear();
        crews_stay_hidden(false);
    });
    events::on("session.end").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_phase == Phase::qualifying) {
            close_all();
            save_parc_ferme();
        }
        if (g_phase == Phase::racing) {
            g_pit_lane_start.clear();
            forget_saved_parc_ferme();
        }
        g_phase = Phase::other;
        g_take_setup_now = false;
        crews_stay_hidden(false);
    });
}

SWOFTY_ADDON()
