#pragma once
#include "types.h"
#include "position.h"
#include <atomic>
#include <chrono>

struct SearchLimits {
    int wtime = 0, btime = 0, winc = 0, binc = 0;
    int movestogo = 0;
    int movetime = 0;
    int depth = MAX_PLY;
    U64 max_nodes = 0;  // 0 = unlimited
    bool infinite = false;
};

struct SearchResult {
    Move best_move = MOVE_NONE;
    Move ponder_move = MOVE_NONE;
    int  score = 0;
    int  depth_reached = 0;
    U64  nodes = 0;
};

namespace Search {
    extern std::atomic<bool> stop_flag;

    void init();
    void set_threads(int n);
    int  threads();
    SearchResult go(Position& pos, const SearchLimits& limits);
}
