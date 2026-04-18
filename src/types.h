#pragma once
#include <cstdint>
#include <string>

using U64 = uint64_t;
using Bitboard = uint64_t;

enum Color : int { WHITE = 0, BLACK = 1, NO_COLOR = 2 };
constexpr Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int {
    NO_PIECE_TYPE = 0, PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6
};

enum Piece : int {
    NO_PIECE = 0,
    W_PAWN = 1, W_KNIGHT = 2, W_BISHOP = 3, W_ROOK = 4, W_QUEEN = 5, W_KING = 6,
    B_PAWN = 9, B_KNIGHT = 10, B_BISHOP = 11, B_ROOK = 12, B_QUEEN = 13, B_KING = 14
};
constexpr Piece make_piece(Color c, PieceType pt) { return Piece((c << 3) | pt); }
constexpr PieceType type_of(Piece p) { return PieceType(p & 7); }
constexpr Color color_of(Piece p) { return Color(p >> 3); }

enum Square : int {
    A1=0, B1,C1,D1,E1,F1,G1,H1,
    A2,B2,C2,D2,E2,F2,G2,H2,
    A3,B3,C3,D3,E3,F3,G3,H3,
    A4,B4,C4,D4,E4,F4,G4,H4,
    A5,B5,C5,D5,E5,F5,G5,H5,
    A6,B6,C6,D6,E6,F6,G6,H6,
    A7,B7,C7,D7,E7,F7,G7,H7,
    A8,B8,C8,D8,E8,F8,G8,H8,
    SQ_NONE = 64
};

constexpr int file_of(Square s) { return s & 7; }
constexpr int rank_of(Square s) { return s >> 3; }
constexpr Square make_square(int f, int r) { return Square((r << 3) | f); }
constexpr Square flip_rank(Square s) { return Square(s ^ 56); }

enum CastlingRights : int {
    NO_CASTLING = 0,
    WHITE_OO  = 1, WHITE_OOO = 2,
    BLACK_OO  = 4, BLACK_OOO = 8,
    WHITE_CASTLING = WHITE_OO | WHITE_OOO,
    BLACK_CASTLING = BLACK_OO | BLACK_OOO,
    ANY_CASTLING   = WHITE_CASTLING | BLACK_CASTLING
};

// Move encoding (16 bits):
// bits  0-5 : from square
// bits  6-11: to square
// bits 12-13: promotion piece type - KNIGHT (0=N,1=B,2=R,3=Q)
// bits 14-15: move type (0=normal, 1=promotion, 2=en passant, 3=castling)
using Move = uint16_t;
constexpr Move MOVE_NONE = 0;
constexpr Move MOVE_NULL = 65;

enum MoveType { MT_NORMAL = 0, MT_PROMOTION = 1, MT_EN_PASSANT = 2, MT_CASTLING = 3 };

constexpr Move make_move(Square from, Square to) { return Move((to << 6) | from); }
constexpr Move make_move(Square from, Square to, MoveType mt, PieceType promo = KNIGHT) {
    return Move((mt << 14) | ((promo - KNIGHT) << 12) | (to << 6) | from);
}
constexpr Square from_sq(Move m) { return Square(m & 0x3F); }
constexpr Square to_sq(Move m)   { return Square((m >> 6) & 0x3F); }
constexpr MoveType type_of_move(Move m) { return MoveType((m >> 14) & 3); }
constexpr PieceType promo_type(Move m)  { return PieceType(((m >> 12) & 3) + KNIGHT); }

// Score/value constants
using Value = int;
constexpr Value VALUE_ZERO     = 0;
constexpr Value VALUE_DRAW     = 0;
constexpr Value VALUE_MATE     = 32000;
constexpr Value VALUE_INF      = 32001;
constexpr Value VALUE_NONE     = 32002;
constexpr Value VALUE_MATE_IN_MAX_PLY = VALUE_MATE - 256;

constexpr int MAX_PLY  = 128;
constexpr int MAX_MOVES = 256;

inline std::string sq_to_str(Square s) {
    if (s == SQ_NONE) return "-";
    char f = 'a' + file_of(s);
    char r = '1' + rank_of(s);
    return std::string(1, f) + std::string(1, r);
}
