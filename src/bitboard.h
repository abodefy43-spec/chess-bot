#pragma once
#include "types.h"

namespace BB {

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_H = FILE_A << 7;
constexpr Bitboard RANK_1 = 0xFFULL;
constexpr Bitboard RANK_2 = RANK_1 << 8;
constexpr Bitboard RANK_4 = RANK_1 << 24;
constexpr Bitboard RANK_5 = RANK_1 << 32;
constexpr Bitboard RANK_7 = RANK_1 << 48;
constexpr Bitboard RANK_8 = RANK_1 << 56;

constexpr Bitboard sq_bb(Square s) { return 1ULL << s; }

inline int popcount(Bitboard b) { return __builtin_popcountll(b); }
inline Square lsb(Bitboard b) { return Square(__builtin_ctzll(b)); }
inline Square msb(Bitboard b) { return Square(63 ^ __builtin_clzll(b)); }
inline Square pop_lsb(Bitboard& b) { Square s = lsb(b); b &= b - 1; return s; }

enum Dir : int {
    NORTH = 8, SOUTH = -8, EAST = 1, WEST = -1,
    NE = NORTH + EAST, NW = NORTH + WEST,
    SE = SOUTH + EAST, SW = SOUTH + WEST
};

template<Dir D>
constexpr Bitboard shift(Bitboard b) {
    return D == NORTH ? b << 8
         : D == SOUTH ? b >> 8
         : D == EAST  ? (b & ~FILE_H) << 1
         : D == WEST  ? (b & ~FILE_A) >> 1
         : D == NE    ? (b & ~FILE_H) << 9
         : D == NW    ? (b & ~FILE_A) << 7
         : D == SE    ? (b & ~FILE_H) >> 7
         : D == SW    ? (b & ~FILE_A) >> 9 : 0;
}

void init();

// Precomputed tables
extern Bitboard PawnAttacks[2][64];
extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];
// BetweenBB[a][b] = squares strictly between a and b on a rank/file/diagonal,
// or 0 if they are not aligned. Used for check interposition & pin checks.
extern Bitboard BetweenBB[64][64];
// LineBB[a][b] = the entire rank/file/diagonal containing a and b (including
// a and b themselves), or 0 if not aligned. Used for pinned-piece movement.
extern Bitboard LineBB[64][64];

Bitboard bishop_attacks(Square s, Bitboard occ);
Bitboard rook_attacks(Square s, Bitboard occ);
inline Bitboard queen_attacks(Square s, Bitboard occ) {
    return bishop_attacks(s, occ) | rook_attacks(s, occ);
}
Bitboard attacks_of(PieceType pt, Square s, Bitboard occ);

void print(Bitboard b);

} // namespace BB
