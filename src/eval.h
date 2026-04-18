#pragma once
#include "position.h"

namespace Eval {
    void init();
    int evaluate(const Position& pos);  // Returns score from side-to-move's perspective.
}
