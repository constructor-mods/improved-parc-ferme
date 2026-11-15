// SPDX-License-Identifier: MIT
// For testing only: a file test_pit_lane_start.txt next to the addon's DLL, holding a car number, gives that car a
// pit lane start in every race session, even with no qualifying. The file isn't part of the addon.
#pragma once
#include <swofty/mixin.hpp>

#include <windows.h>

#include <cstdio>
#include <string>

namespace improved_parc_ferme {

inline int g_test_car = -1;

inline void read_test_switch()
{
    HMODULE module = nullptr;
    auto flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (!GetModuleHandleExA(flags, reinterpret_cast<LPCSTR>(&read_test_switch), &module)) {
        return;
    }
    char path[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }
    std::string file(path);
    size_t slash = file.find_last_of("\\/");
    if (slash == std::string::npos) {
        return;
    }
    file = file.substr(0, slash + 1) + "test_pit_lane_start.txt";
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
