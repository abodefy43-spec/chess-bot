#include "bitboard.h"
#include <cstdio>

namespace BB {

Bitboard PawnAttacks[2][64];
Bitboard KnightAttacks[64];
Bitboard KingAttacks[64];

// Rays from each square in 8 directions. Used for classical sliding attacks.
// Direction indices: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW
static Bitboard Rays[8][64];

static const int DirDelta[8] = { 8, 9, 1, -7, -8, -9, -1, 7 };

// True if destination square is still on the board relative to source (no wrap).
static bool on_board_step(int from, int to) {
    if (to < 0 || to > 63) return false;
    int df = (to & 7) - (from & 7);
    int dr = (to >> 3) - (from >> 3);
    if (df > 1 || df < -1) return false;
    if (dr > 1 || dr < -1) return false;
    return true;
}

void init() {
    // Knight & King
    const int knight_df[8]   = { 1, -1, 2, -2, -2, 2, -1, 1 };
    const int knight_dr[8]   = { 2, 2, 1, 1, -1, -1, -2, -2 };
    const int king_deltas[8] = { 8, 9, 1, -7, -8, -9, -1, 7 };

    for (int s = 0; s < 64; ++s) {
        int f = s & 7, r = s >> 3;
        Bitboard n = 0, k = 0;
        for (int i = 0; i < 8; ++i) {
            int nf = f + knight_df[i], nr = r + knight_dr[i];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8)
                n |= 1ULL << (nr * 8 + nf);
            int to = s + king_deltas[i];
            if (on_board_step(s, to))
                k |= 1ULL << to;
        }
        KnightAttacks[s] = n;
        KingAttacks[s] = k;
    }

    // Pawn attacks
    for (int s = 0; s < 64; ++s) {
        int f = s & 7, r = s >> 3;
        Bitboard w = 0, b = 0;
        if (r < 7) {
            if (f > 0) w |= 1ULL << (s + 7);
            if (f < 7) w |= 1ULL << (s + 9);
        }
        if (r > 0) {
            if (f < 7) b |= 1ULL << (s - 7);
            if (f > 0) b |= 1ULL << (s - 9);
        }
        PawnAttacks[WHITE][s] = w;
        PawnAttacks[BLACK][s] = b;
    }

    // Rays in 8 directions
    for (int s = 0; s < 64; ++s) {
        for (int d = 0; d < 8; ++d) {
            Bitboard ray = 0;
            int prev = s;
            int cur = s + DirDelta[d];
            while (cur >= 0 && cur < 64 && on_board_step(prev, cur)) {
                ray |= 1ULL << cur;
                prev = cur;
                cur += DirDelta[d];
            }
            Rays[d][s] = ray;
        }
    }
}

// Classical positive-direction ray attack: stop at first blocker (blocker square included).
// dir_positive: bits increase (N, NE, E, NW); we use lsb to find first blocker.
// For negative-direction rays (S, SW, W, SE), blocker bit is the highest in the masked ray.
static inline Bitboard pos_ray(int s, int d, Bitboard occ) {
    Bitboard ray = Rays[d][s];
    Bitboard blockers = ray & occ;
    if (blockers) {
        Square b = lsb(blockers);
        ray ^= Rays[d][b];
    }
    return ray;
}

static inline Bitboard neg_ray(int s, int d, Bitboard occ) {
    Bitboard ray = Rays[d][s];
    Bitboard blockers = ray & occ;
    if (blockers) {
        Square b = msb(blockers);
        ray ^= Rays[d][b];
    }
    return ray;
}

// Positive directions: N(0), NE(1), E(2), NW(7)
// Negative directions: SE(3), S(4), SW(5), W(6)
Bitboard rook_attacks(Square s, Bitboard occ) {
    return pos_ray(s, 0, occ) | pos_ray(s, 2, occ)
         | neg_ray(s, 4, occ) | neg_ray(s, 6, occ);
}

Bitboard bishop_attacks(Square s, Bitboard occ) {
    return pos_ray(s, 1, occ) | pos_ray(s, 7, occ)
         | neg_ray(s, 3, occ) | neg_ray(s, 5, occ);
}

Bitboard attacks_of(PieceType pt, Square s, Bitboard occ) {
    switch (pt) {
        case KNIGHT: return KnightAttacks[s];
        case BISHOP: return bishop_attacks(s, occ);
        case ROOK:   return rook_attacks(s, occ);
        case QUEEN:  return queen_attacks(s, occ);
        case KING:   return KingAttacks[s];
        default:     return 0;
    }
}

void print(Bitboard b) {
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) {
            printf("%c ", (b & (1ULL << (r*8+f))) ? 'X' : '.');
        }
        printf("\n");
    }
    printf("\n");
}

} // namespace BB
