// SPDX-License-Identifier: MIT
// For testing only: a file test_pit_lane_start.txt in the installed addon's native folder, holding a car number,
// gives that car a pit lane start in every race session, even with no qualifying. The file isn't part of the addon.
#pragma once
#include <swofty/mixin.hpp>

#include <windows.h>

#include <cstdio>
#include <string>

namespace improved_parc_ferme {

inline int g_test_car = -1;

inline void read_test_switch()
{
    // The game runs a copy of the DLL, so the file is looked for in the installed addon's folder.
    char folder[MAX_PATH] = {};
    DWORD length = GetEnvironmentVariableA("LOCALAPPDATA", folder, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }
    std::string file = std::string(folder) + "/Swofty/addons/msyeett.improved-parc-ferme/native/test_pit_lane_start.txt";
    FILE *input = std::fopen(file.c_str(), "r");
    if (!input) {
        return;
    }
    int car = -1;
    if (std::fscanf(input, "%d", &car) == 1 && car >= 0) {
        g_test_car = car;
        swofty::log("[improved-parc-ferme] test switch: car %d starts every race from the pit lane", car);
    }
    std::fclose(input);
}

} // namespace improved_parc_ferme
