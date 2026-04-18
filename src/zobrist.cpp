#include "zobrist.h"

namespace Zobrist {
    U64 psq[16][64];
    U64 castling[16];
    U64 ep_file[8];
    U64 side;

    // Simple splitmix64 PRNG with a fixed seed for reproducibility.
    static U64 next(U64& state) {
        U64 z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    void init() {
        U64 state = 0xC0FFEE1234567890ULL;
        for (int p = 0; p < 16; ++p)
            for (int s = 0; s < 64; ++s)
                psq[p][s] = next(state);
        for (int c = 0; c < 16; ++c)
            castling[c] = next(state);
        for (int f = 0; f < 8; ++f)
            ep_file[f] = next(state);
        side = next(state);
    }
}
