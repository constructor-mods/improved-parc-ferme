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
#include <swofty/services.hpp>
#include <swofty/systems/cars.hpp>

#include "parc_ferme.hpp"
#include "parc_ferme_store.hpp"
#include "pit_exit.hpp"
#include "pit_exit_hold.hpp"
#include "pit_hold.hpp"
#include "pit_start.hpp"
#include "race_hud.hpp"
#include "rows_service.hpp"
#include "rules_setting.hpp"
#include "sessions.hpp"
#include "setup_rows.hpp"
#include "test_switch.hpp"
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
// Cars that drive to the end of the pit lane at this race start, and when they went to their garage.
std::map<int, Clock::time_point> g_at_pit_exit;
// A pit exit starter goes from its garage this soon.
constexpr auto kPitExitDriveOut = std::chrono::seconds(1);

// The test switch: the session started at this time and its test car isn't set up yet.
bool g_test_waiting = false;
Clock::time_point g_test_since;
constexpr auto kTestDelay = std::chrono::seconds(1);

// With the test switch: the test car gets a pit lane start in this session, as if the race had just started.
void apply_test_switch()
{
    if (g_test_car < 0 || !g_test_waiting || Clock::now() - g_test_since < kTestDelay) {
        return;
    }
    g_test_waiting = false;
    g_phase = Phase::racing;
    g_race_started = Clock::now();
    g_pit_lane_start = {g_test_car};
    g_in_garage.clear();
    g_driven_out.clear();
    g_at_pit_exit.clear();
    log("[improved-parc-ferme] test switch: car %d has a pit lane start", g_test_car);
}

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

uint64_t service_locked_rows(int car)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (car < 0 || !checking() || g_pit_lane_start.count(car)) {
        return 0;
    }
    if (!g_parc_ferme.count({car, Block::first}) && !g_parc_ferme.count({car, Block::second})) {
        return 0;
    }
    return locked_row_bits();
}

int service_pit_lane_start(int car)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_pit_lane_start.count(car) ? 1 : 0;
}

uint32_t g_session = 0;
// The session ended and no new one has been followed since.
bool g_session_ended = false;
// The game's Parc Ferme Rules setting for the session.
bool g_rules_on = true;

// A new session: qualifying (or a sprint shootout) starts parc ferme over, a race checks it until the start,
// practice forgets it.
void follow_session(uint32_t session, bool rules_on)
{
    if (session == g_session || session == 0) {
        return;
    }
    g_session = session;
    g_session_ended = false;
    g_pit_lane = nullptr;
    SessionKind kind = kind_of(session);
    // Parc Ferme Rules off in the game's settings: no parc ferme this session.
    if (!rules_on && kind != SessionKind::practice) {
        g_phase = Phase::other;
        g_pit_lane_start.clear();
        g_take_setup_now = false;
        log("[improved-parc-ferme] session %u: Parc Ferme Rules are off, nothing to do", session);
        return;
    }
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
    std::vector<int> to_exit_garage;
    bool exit_known = can_stop_cars() && pit_exit_known();
    bool exit_fits = false;
    if (exit_known) {
        exit_fits = pit_exit_fits_the_grid();
    }
    // The game's Parc Ferme Rules setting, read once per session (a game call: outside the lock).
    static uint32_t rules_session = 0;
    uint32_t session = session_number();
    if (session != rules_session && session != 0) {
        rules_session = session;
        bool rules_on = parc_ferme_rules_on();
        std::lock_guard<std::mutex> lock(g_mutex);
        g_rules_on = rules_on;
        log("[improved-parc-ferme] session %u: Parc Ferme Rules %s", session, rules_on ? "on" : "off");
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        follow_session(session, g_rules_on);
        apply_test_switch();
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
                if (exit_known && exit_fits && g_at_pit_exit.empty()) {
                    g_at_pit_exit[car] = now;
                    to_pit_exit.push_back(car);
                } else {
                    if (exit_known && !exit_fits) {
                        log("[improved-parc-ferme] the known pit exit points against the grid: garage start");
                    }
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
            for (const auto &[car, since] : g_at_pit_exit) {
                if (g_driven_out.count(car)) {
                    continue;
                }
                if (now - since >= kPitExitDriveOut) {
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
    learn_pit_exit(pit_lane);
    for (const Message &message : messages) {
        if (hud::available()) {
            hud::toast(message.text, message.detail, 5, "generic");
        }
    }
    if (!to_pit_exit.empty()) {
        for (int car : to_pit_exit) {
            start_at_pit_exit(car);
        }
        send_to_garage(pit_lane, to_pit_exit);
        if (hud::available()) {
            hud::toast("Pit lane start", "Parc ferme: wait at the pit exit until the field has passed", 5, "generic");
        }
    }
    if (racing) {
        HoldNews news = hold_at_pit_exit();
        if (news.drive_out_again >= 0) {
            log("[improved-parc-ferme] car %d still in its garage at lights out: drives out now", news.drive_out_again);
            drive_out(pit_lane, {news.drive_out_again});
        }
        if (news.free_to_go && hud::available()) {
            hud::toast("Pit exit open", "Go!", 4, "generic");
        }
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
    follow_start_system();
    follow_lights_wait();
    read_test_switch();
    events::on_frame(frame);
}

SWOFTY_REGISTER
{
    // Tell other addons which setup rows are under parc ferme.
    static ParcFermeRows rows{};
    rows.size = sizeof rows;
    rows.locked_rows = service_locked_rows;
    rows.pit_lane_start = service_pit_lane_start;
    log("[improved-parc-ferme] rows service: %s", services::name(services::publish(rows)));
    // The track of the session, for the end of its pit lane.
    events::on("session.start").after([](std::span<const std::byte>, const events::Context &context) {
        std::string track;
        if (context.session()) {
            track = std::string(context.session()->track.id());
        }
        std::string name = track_name(track);
        stop_waiting();
        at_track(name);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_weekend_track = name;
        g_test_waiting = g_test_car >= 0;
        g_test_since = Clock::now();
        // The session ended and this one has the same number (the race loaded again): follow it again.
        if (g_session_ended) {
            uint32_t again = g_session;
            g_session = 0;
            follow_session(again, g_rules_on);
        }
        if (g_phase == Phase::before_start) {
            // Parc ferme from another track's weekend doesn't count here.
            if (g_parc_ferme_track != name) {
                start_qualifying();
            }
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
        stop_waiting();
        std::lock_guard<std::mutex> lock(g_mutex);
        g_phase = Phase::racing;
        g_race_started = Clock::now();
        g_in_garage.clear();
        g_driven_out.clear();
        g_at_pit_exit.clear();
        crews_stay_hidden(false);
        hud_stays_in_garage(false);
    });
    events::on("session.end").after([](std::span<const std::byte>, const events::Context &) {
        stop_waiting();
        std::lock_guard<std::mutex> lock(g_mutex);
        g_test_waiting = false;
        if (g_phase == Phase::qualifying) {
            close_all();
            g_parc_ferme_track = g_weekend_track;
            save_parc_ferme();
        }
        // Quitting the race keeps parc ferme and the pit lane starts: loading the weekend again gives the same race.
        g_session_ended = true;
        g_phase = Phase::other;
        g_take_setup_now = false;
        crews_stay_hidden(false);
        hud_stays_in_garage(false);
    });
}

SWOFTY_ADDON()
