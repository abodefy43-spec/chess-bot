#pragma once
#include "position.h"
#include <string>

namespace NNUE {
    bool load(const std::string& path);
    bool is_loaded();
    // Returns a centipawn eval from side-to-move's perspective, or INT_MIN if not loaded.
    int  evaluate(const Position& pos);
}
