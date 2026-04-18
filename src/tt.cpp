#include "tt.h"
#include <cstdlib>
#include <cstring>

TranspositionTable TT;

TranspositionTable::~TranspositionTable() {
    if (table_) std::free(table_);
}

void TranspositionTable::resize(size_t mb) {
    if (table_) { std::free(table_); table_ = nullptr; }
    size_t target_bytes = mb * 1024ULL * 1024ULL;
    size_t want_entries = target_bytes / sizeof(TTEntry);
    // round down to power of two
    size_t entries = 1;
    while ((entries << 1) <= want_entries) entries <<= 1;
    if (entries == 0) entries = 1;
    size_ = entries;
    mask_ = size_ - 1;
    table_ = (TTEntry*)std::calloc(size_, sizeof(TTEntry));
    generation_ = 0;
}

void TranspositionTable::clear() {
    std::memset(table_, 0, size_ * sizeof(TTEntry));
    generation_ = 0;
}

bool TranspositionTable::probe(U64 key, TTEntry& out) const {
    const TTEntry& e = table_[key & mask_];
    if (e.depth && e.key == key) {
        out = e;
        return true;
    }
    return false;
}

void TranspositionTable::store(U64 key, Move move, int score, int eval, int depth, Bound bound, int ply) {
    TTEntry& e = table_[key & mask_];
    bool replace = (e.depth == 0)
                || (e.key == key)
                || ((e.bound_gen & 0xFC) != generation_)
                || (depth + 2 >= e.depth);
    if (!replace) return;

    if (move == MOVE_NONE && e.key == key) move = e.move;  // preserve previous move

    e.key   = key;
    e.move  = move;
    e.score = (int16_t)score_to_tt(score, ply);
    e.eval  = (int16_t)eval;
    e.depth = (uint8_t)(depth + 1);
    e.bound_gen = (uint8_t)(generation_ | bound);
}
