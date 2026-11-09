// SPDX-License-Identifier: MIT
// The car setup the game applies to a car, and the rows parc ferme rules lock.
#pragma once
#include <swofty/mem.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace improved_parc_ferme {

constexpr uintptr_t kSetupCar = 0x28;
constexpr uintptr_t kFirstBlock = 0x08;
constexpr uintptr_t kSecondBlock = 0xb0;
constexpr uintptr_t kBlockChoice = 0x158;
constexpr size_t kBlockSize = 0xa8;

// The game keeps two sets of values for a car and applies one or the other.
enum class Block { first, second };

struct Values {
    std::array<uint8_t, kBlockSize> bytes{};
};

// A row the rules lock after qualifying: where it sits in the values, and its name for the player.
struct LockedRow {
    size_t offset;
    const char *name;
};

// The same rows the game locks itself: rear wing, suspension geometry, suspension, brake pressure and ballast.
inline constexpr LockedRow kLockedRows[] = {
    {0x04, "rear wing"},          {0x1c, "front camber"},        {0x20, "rear camber"},
    {0x24, "front toe"},          {0x28, "rear toe"},            {0x2c, "front suspension"},
    {0x30, "rear suspension"},    {0x34, "front ride height"},   {0x38, "rear ride height"},
    {0x3c, "front anti-roll bar"}, {0x40, "rear anti-roll bar"}, {0x44, "brake pressure"},
    {0x64, "ballast"},
};

inline int32_t row_value(const Values &values, size_t offset)
{
    int32_t value = 0;
    std::memcpy(&value, values.bytes.data() + offset, sizeof value);
    return value;
}

// The car a setup belongs to, the block the game applies and its values. False if the memory can't be read.
inline bool read_setup(void *setup_manager, uint8_t *setup, uintptr_t &car, Block &block, Values &values)
{
    if (!setup_manager || !setup) {
        return false;
    }
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(setup_manager) + kSetupCar, car) || !car) {
        return false;
    }
    uint8_t choice = 0;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(setup) + kBlockChoice, choice)) {
        return false;
    }
    uintptr_t start = reinterpret_cast<uintptr_t>(setup) + kFirstBlock;
    block = Block::first;
    if (choice) {
        start = reinterpret_cast<uintptr_t>(setup) + kSecondBlock;
        block = Block::second;
    }
    if (!swofty::mem::readable(reinterpret_cast<void *>(start), kBlockSize)) {
        return false;
    }
    std::memcpy(values.bytes.data(), reinterpret_cast<void *>(start), kBlockSize);
    return true;
}

// The locked rows that differ between two setups, as "rear wing, front toe". Empty when none changed.
inline std::string changed_rows(const Values &before, const Values &after)
{
    std::string names;
    for (const LockedRow &row : kLockedRows) {
        if (row_value(before, row.offset) == row_value(after, row.offset)) {
            continue;
        }
        if (!names.empty()) {
            names += ", ";
        }
        names += row.name;
    }
    return names;
}

} // namespace improved_parc_ferme
