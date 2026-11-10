// SPDX-License-Identifier: MIT
// The end of the pit lane is kept per track in the addon's own folder, so it's known on the next visit too, even
// when that weekend has no session that drives out of the pits before the race.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/storage.hpp>

#include <cstdio>
#include <map>
#include <sstream>
#include <string>

namespace improved_parc_ferme {

constexpr const char *kPitExitsFile = "pit_exits_2.txt";

// The name the end of the pit lane is kept under: the track's id, or its length in metres when the session doesn't
// say which track it is.
inline std::string track_name(const std::string &id)
{
    if (!id.empty()) {
        return id;
    }
    uintptr_t length_at = swofty::api->resolve("g_Game_TrackLength");
    float length = 0;
    if (!length_at || !swofty::mem::read(length_at, length) || length < 100.0f) {
        return "";
    }
    return "length_" + std::to_string(int(length + 0.5f));
}

// One line per track: the track, the 16 numbers of where to wait, then the exit point and its direction.
inline std::map<std::string, std::string> read_pit_exit_lines()
{
    std::map<std::string, std::string> lines;
    std::string text;
    if (swofty::storage::read(kPitExitsFile, text) != swofty::storage::Result::ok) {
        return lines;
    }
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        size_t space = line.find(' ');
        if (space == std::string::npos || space == 0) {
            continue;
        }
        lines[line.substr(0, space)] = line.substr(space + 1);
    }
    return lines;
}

inline void write_pit_exit_lines(const std::map<std::string, std::string> &lines)
{
    std::string text;
    for (const auto &[track, numbers] : lines) {
        text += track + " " + numbers + "\n";
    }
    swofty::storage::write(kPitExitsFile, text);
}

inline std::string numbers_of(const PitExit &exit)
{
    std::string numbers;
    char number[32];
    for (float value : exit.spot) {
        std::snprintf(number, sizeof number, "%.4f ", value);
        numbers += number;
    }
    std::snprintf(number, sizeof number, "%.4f ", exit.exit_x);
    numbers += number;
    std::snprintf(number, sizeof number, "%.4f ", exit.exit_z);
    numbers += number;
    std::snprintf(number, sizeof number, "%.6f ", exit.direction_x);
    numbers += number;
    std::snprintf(number, sizeof number, "%.6f", exit.direction_z);
    numbers += number;
    return numbers;
}

inline bool pit_exit_from(const std::string &numbers, PitExit &exit)
{
    std::istringstream input(numbers);
    for (float &value : exit.spot) {
        if (!(input >> value)) {
            return false;
        }
    }
    return bool(input >> exit.exit_x >> exit.exit_z >> exit.direction_x >> exit.direction_z);
}

inline void save_pit_exit(const std::string &track, const PitExit &exit)
{
    if (track.empty()) {
        return;
    }
    std::map<std::string, std::string> lines = read_pit_exit_lines();
    lines[track] = numbers_of(exit);
    write_pit_exit_lines(lines);
}

inline bool load_pit_exit(const std::string &track, PitExit &exit)
{
    std::map<std::string, std::string> lines = read_pit_exit_lines();
    auto line = lines.find(track);
    return line != lines.end() && pit_exit_from(line->second, exit);
}

} // namespace improved_parc_ferme
