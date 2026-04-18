#pragma once
#include "types.h"

enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

struct TTEntry {
    U64    key;         // full 64-bit key for verification
    Move   move;        // best move (2 bytes)
    int16_t score;      // search score (adjusted for mate distance when stored)
    int16_t eval;       // static eval at this node
    uint8_t depth;      // search depth + 1 (0 means unused)
    uint8_t bound_gen;  // low 2 bits: bound, high 6 bits: generation (age)
};

static_assert(sizeof(TTEntry) == 16, "TTEntry must be 16 bytes");

class TranspositionTable {
public:
    TranspositionTable() { resize(16); }  // 16 MB default
    ~TranspositionTable();

    void resize(size_t mb);
    void clear();
    void new_search() { generation_ += 4; } // step of 4 because low 2 bits hold bound

    bool probe(U64 key, TTEntry& out) const;
    void store(U64 key, Move move, int score, int eval, int depth, Bound bound, int ply);

    static int score_to_tt(int score, int ply) {
        if (score >=  VALUE_MATE_IN_MAX_PLY) return score + ply;
        if (score <= -VALUE_MATE_IN_MAX_PLY) return score - ply;
        return score;
    }
    static int score_from_tt(int score, int ply) {
        if (score >=  VALUE_MATE_IN_MAX_PLY) return score - ply;
        if (score <= -VALUE_MATE_IN_MAX_PLY) return score + ply;
        return score;
    }

private:
    TTEntry* table_ = nullptr;
    size_t   size_ = 0;      // number of entries (power of two)
    size_t   mask_ = 0;
    uint8_t  generation_ = 0;
};

extern TranspositionTable TT;
