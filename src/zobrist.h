#pragma once
#include "types.h"

namespace Zobrist {
    extern U64 psq[16][64];      // [piece][square]
    extern U64 castling[16];     // [castling rights bitmask]
    extern U64 ep_file[8];       // [file]
    extern U64 side;             // side to move == black

    void init();
}
