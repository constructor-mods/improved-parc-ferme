// SPDX-License-Identifier: MIT
// Before the start the game holds the clutch in for every human car, ready for the launch. The car driving down the
// pit lane gets its clutch let out instead, so the engine drives the car and doesn't rev free on the limiter.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace improved_parc_ferme {

// The game's gearbox update and the one call to it (on this game build).
constexpr uintptr_t kGearBoxUpdate = 0x46a93d0;
constexpr uintptr_t kGearBoxUpdateCall = 0x46df67c;
constexpr uint8_t kGearBoxUpdateStart[] = {0x56, 0x48, 0x83, 0xec, 0x30, 0xc5, 0xf8, 0x29, 0x74, 0x24, 0x20};
using GearBoxUpdate = void (*)(void *gear_box, float step);

// The gearbox knows its car here; the car keeps its clutch here (1 = in, 0 = out).
constexpr uintptr_t kGearBoxCar = 0x8;
constexpr uintptr_t kClutchIn = 0x1c0;

inline GearBoxUpdate g_gear_box_update = nullptr;
// The car whose clutch is let out, or 0.
inline std::atomic<uintptr_t> g_clutch_out_car{0};

inline void gear_box_update(void *gear_box, float step)
{
    uintptr_t free_car = g_clutch_out_car.load();
    uintptr_t car = 0;
    if (free_car && swofty::mem::read(reinterpret_cast<uintptr_t>(gear_box) + kGearBoxCar, car) && car == free_car) {
        *reinterpret_cast<float *>(car + kClutchIn) = 0.0f;
    }
    g_gear_box_update(gear_box, step);
}

// A jump to our gearbox update, close enough to the game for its call to reach.
inline uint8_t *jump_near(uintptr_t module, void *target)
{
    constexpr uintptr_t kStep = 0x10000;
    for (uintptr_t tries = 1; tries < 0x4000; tries++) {
        void *place = VirtualAlloc(reinterpret_cast<void *>(module - tries * kStep), kStep, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_EXECUTE_READWRITE);
        if (!place) {
            continue;
        }
        auto *code = static_cast<uint8_t *>(place);
        // mov rax, target; jmp rax
        code[0] = 0x48;
        code[1] = 0xb8;
        uintptr_t address = reinterpret_cast<uintptr_t>(target);
        std::memcpy(code + 2, &address, sizeof address);
        code[10] = 0xff;
        code[11] = 0xe0;
        return code;
    }
    return nullptr;
}

// At load: the game's call to its gearbox update goes through ours.
inline void follow_gear_box()
{
    uintptr_t module = swofty::api->module_base();
    auto *update = reinterpret_cast<uint8_t *>(module + kGearBoxUpdate);
    auto *call = reinterpret_cast<uint8_t *>(module + kGearBoxUpdateCall);
    if (!swofty::mem::readable(reinterpret_cast<uintptr_t>(call), 5) ||
        std::memcmp(update, kGearBoxUpdateStart, sizeof kGearBoxUpdateStart) != 0 || call[0] != 0xe8) {
        swofty::log("[improved-parc-ferme] gearbox update not found: the clutch stays as the game has it");
        return;
    }
    int32_t offset = 0;
    std::memcpy(&offset, call + 1, sizeof offset);
    if (call + 5 + offset != update) {
        swofty::log("[improved-parc-ferme] gearbox update not found: the clutch stays as the game has it");
        return;
    }
    uint8_t *jump = jump_near(module, reinterpret_cast<void *>(&gear_box_update));
    if (!jump) {
        swofty::log("[improved-parc-ferme] no room near the game: the clutch stays as the game has it");
        return;
    }
    g_gear_box_update = reinterpret_cast<GearBoxUpdate>(update);
    int32_t new_offset = int32_t(jump - (call + 5));
    DWORD old_protect = 0;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return;
    }
    std::memcpy(call + 1, &new_offset, sizeof new_offset);
    VirtualProtect(call, 5, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
}

} // namespace improved_parc_ferme
