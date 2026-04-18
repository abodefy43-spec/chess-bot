#pragma once
#include "types.h"
#include "position.h"

namespace Book {
    void init();                         // Build the in-memory book from opening lines.
    Move probe(const Position& pos);     // Returns a book move for the position, or MOVE_NONE.
    void set_enabled(bool on);
    bool is_enabled();
    size_t size();                       // Number of unique book positions loaded.
}
