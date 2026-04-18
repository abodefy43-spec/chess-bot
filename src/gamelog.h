#pragma once
#include "types.h"
#include "position.h"
#include <string>

namespace GameLog {
    void set_enabled(bool on);
    bool is_enabled();
    void set_path(const std::string& path);
    void new_game();
    void record(const Position& pos_before, Move move, int score_cp,
                int depth, U64 nodes, int time_ms, const char* source);
}
