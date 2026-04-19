#pragma once
#include "position.h"

namespace Eval {
    void init();
    int evaluate(const Position& pos);            // NNUE if loaded, else classical.
    int evaluate_classical(const Position& pos);  // PeSTO tapered eval.
}
