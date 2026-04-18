#pragma once
#include "types.h"
#include "position.h"

struct ExtMove { Move move; int score; };

struct MoveList {
    ExtMove moves[MAX_MOVES];
    int size = 0;
    void add(Move m) { moves[size++] = ExtMove{m, 0}; }
};

// Generates pseudo-legal moves. Callers must filter for legality with pos.is_legal().
enum GenType { GEN_ALL, GEN_CAPTURES };

void generate_moves(const Position& pos, MoveList& list, GenType gt = GEN_ALL);
