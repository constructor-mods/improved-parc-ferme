// SPDX-License-Identifier: MIT
// What this addon tells other addons (Improved Car Setups marks these rows on the setup screen).
#pragma once
#include "setup_rows.hpp"

#include <cstdint>

namespace improved_parc_ferme {

struct ParcFermeRows {
    static constexpr const char *name = "msyeett.parc-ferme.rows";
    static constexpr uint32_t version = 1;
    uint32_t size;
    // The rows a change to now sends this car (its session number) to the pit lane: bit n = the setup row at n * 4
    // in the setup values. 0 when no change counts (no parc ferme yet, the rules off, or after the start).
    uint64_t (*locked_rows)(int car);
    // 1 when this car already starts from the pit lane.
    int (*pit_lane_start)(int car);
};

// Every row parc ferme locks, as bits.
inline uint64_t locked_row_bits()
{
    uint64_t bits = 0;
    for (const LockedRow &row : kLockedRows) {
        bits |= uint64_t(1) << (row.offset / 4);
    }
    return bits;
}

} // namespace improved_parc_ferme
