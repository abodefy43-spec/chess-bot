#include "book.h"
#include "movegen.h"
#include <cstdio>
#include <random>
#include <unordered_map>
#include <vector>

namespace Book {

static std::unordered_map<U64, std::vector<Move>> table;
static bool enabled_ = true;

// Each string is one opening line from the initial position, given as UCI moves
// separated by spaces. Lines whose first illegal token is reached are skipped
// from that point onward (earlier plies are still recorded).
static const char* const LINES[] = {
    // === 1.e4 e5 ===
    // Ruy Lopez Closed
    "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8 h2h3",
    "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 c8g4",
    "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 e8g8 c2c3 d7d5",
    "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8c5",
    "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 d7d6 c2c3",
    "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f6e4 d2d4 b7b5 a4b3 d7d5",
    "e2e4 e7e5 g1f3 b8c6 f1b5 g8f6 e1g1 f6e4 d2d4 f8e7 f1e1 e4d6",
    "e2e4 e7e5 g1f3 b8c6 f1b5 f7f5 b1c3 f5e4 c3e4",
    // Italian
    "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4 c1d2 b4d2",
    "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 b2b4 c5b4 c2c3 b4a5 d2d4",
    "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 d2d3 g8f6 b1c3 d7d6",
    "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5 c2c3 a7a6",
    "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d4 e5d4 e1g1 f6e4",
    "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 f3g5 d7d5 e4d5 c6a5 c4b5 c7c6 d5c6 b7c6 b5e2",
    "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 f3g5 d7d5 e4d5 f6d5",
    // Scotch
    "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6 b1c3 f8b4 d4c6 b7c6 f1d3",
    "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 d8h4 d4b5 f8b4",
    "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 f8c5 c1e3 d8f6",
    // Four Knights / Spanish Four Knights
    "e2e4 e7e5 g1f3 b8c6 b1c3 g8f6 f1b5 f8b4 e1g1 e8g8 d2d3 d7d6 c1g5",
    "e2e4 e7e5 g1f3 b8c6 b1c3 g8f6 f1c4 f8c5",
    // Petrov
    "e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5 f1d3 f8e7",
    "e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 b1c3 e4c3 d2c3",
    // King's Gambit
    "e2e4 e7e5 f2f4 e5f4 g1f3 g7g5 h2h4 g5g4 f3e5 g8f6",
    "e2e4 e7e5 f2f4 e5f4 f1c4 d8h4 e1f1 b7b5",
    "e2e4 e7e5 f2f4 f8c5 g1f3 d7d6",
    // Vienna
    "e2e4 e7e5 b1c3 g8f6 f2f4 d7d5 f4e5 f6e4",
    "e2e4 e7e5 b1c3 g8f6 g2g3 f8c5 f1g2 d7d6",
    // Philidor
    "e2e4 e7e5 g1f3 d7d6 d2d4 e5d4 f3d4 g8f6 b1c3 f8e7",
    "e2e4 e7e5 g1f3 d7d6 d2d4 g8f6 b1c3 b8d7",
    // Center Game / misc
    "e2e4 e7e5 d2d4 e5d4 d1d4 b8c6 d4e3 g8f6 b1c3",

    // === Sicilian ===
    // Najdorf
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1g5 e7e6 f2f4 f8e7 d1f3 d8c7",
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1e3 e7e5 d4b3 c8e6",
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 f1e2 e7e5 d4b3 f8e7",
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 f1c4 e7e6",
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 f2f3 e7e5",
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 h2h3 e7e5",
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 a2a4",
    // Dragon
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6 c1e3 f8g7 f2f3 e8g8 d1d2 b8c6",
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6 f1e2 f8g7 e1g1 e8g8",
    // Classical
    "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 d7d6 c1g5 e7e6 d1d2 a7a6",
    "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 d7d6 f1e2 e7e6 e1g1 f8e7",
    // Sveshnikov
    "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5 d4b5 d7d6 c1g5 a7a6 b5a3 b7b5",
    "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5 d4b5 d7d6 b5d6 f8d6",
    // Scheveningen
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e6 f1e2 f8e7 e1g1 e8g8",
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e6 g2g4",
    // Kan / Paulsen
    "e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 a7a6 f1d3 g8f6 e1g1 d8c7",
    "e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 a7a6 b1c3 d8c7 f1d3",
    // Taimanov
    "e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 b8c6 b1c3 d8c7 f1e2 a7a6",
    "e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 b8c6 b1c3 a7a6 c1e3 g8f6",
    // Accelerated Dragon
    "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g7g6 b1c3 f8g7 c1e3 g8f6 f1c4 e8g8",
    "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g7g6 c2c4 g8f6 b1c3 d7d6",
    // Rossolimo
    "e2e4 c7c5 g1f3 b8c6 f1b5 g7g6 e1g1 f8g7 f1e1 e7e5",
    "e2e4 c7c5 g1f3 b8c6 f1b5 e7e6 e1g1 g8e7",
    "e2e4 c7c5 g1f3 b8c6 f1b5 d7d6 b5c6",
    // Moscow Variation
    "e2e4 c7c5 g1f3 d7d6 f1b5 c8d7 b5d7 d8d7",
    "e2e4 c7c5 g1f3 d7d6 f1b5 b8d7",
    // Closed Sicilian
    "e2e4 c7c5 b1c3 b8c6 g2g3 g7g6 f1g2 f8g7 d2d3 d7d6 f2f4",
    "e2e4 c7c5 b1c3 b8c6 g2g3 g7g6 f1g2 f8g7 d2d3 e7e6 c1e3",
    // Alapin
    "e2e4 c7c5 c2c3 d7d5 e4d5 d8d5 d2d4 g8f6 g1f3 e7e6",
    "e2e4 c7c5 c2c3 g8f6 e4e5 f6d5 d2d4 c5d4",
    "e2e4 c7c5 c2c3 e7e6 d2d4 d7d5",
    // Grand Prix / Smith-Morra
    "e2e4 c7c5 d2d4 c5d4 c2c3 d4c3 b1c3 b8c6 g1f3 d7d6",
    "e2e4 c7c5 b1c3 b8c6 f2f4 g7g6 g1f3 f8g7",
    // 2.Nf3 g6
    "e2e4 c7c5 g1f3 g7g6 d2d4 c5d4 f3d4",

    // === French ===
    "e2e4 e7e6 d2d4 d7d5 b1c3 f8b4 e4e5 c7c5 a2a3 b4c3 b2c3 g8e7",
    "e2e4 e7e6 d2d4 d7d5 b1c3 f8b4 e4e5 g8e7 a2a3 b4c3",
    "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5 f8e7 e4e5 f6d7 g5e7 d8e7",
    "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 e4e5 f6d7 f2f4 c7c5",
    "e2e4 e7e6 d2d4 d7d5 b1d2 g8f6 e4e5 f6d7 f1d3 c7c5",
    "e2e4 e7e6 d2d4 d7d5 b1d2 c7c5 e4d5 e6d5 g1f3 b8c6",
    "e2e4 e7e6 d2d4 d7d5 e4e5 c7c5 c2c3 b8c6 g1f3 d8b6",
    "e2e4 e7e6 d2d4 d7d5 e4d5 e6d5 g1f3 g8f6 f1d3 f8d6",

    // === Caro-Kann ===
    "e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6 h2h4 h7h6 g1f3 b8d7",
    "e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 b8d7 g1f3 g8f6",
    "e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 g1f3 e7e6 f1e2 c6c5",
    "e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 b1c3 e7e6 g2g4 f5g6 g1e2 c6c5",
    "e2e4 c7c6 d2d4 d7d5 e4d5 c6d5 c2c4 g8f6 b1c3 e7e6",
    "e2e4 c7c6 d2d4 d7d5 e4d5 c6d5 f1d3 b8c6 c2c3 g8f6",
    "e2e4 c7c6 g1f3 d7d5 b1c3 c8g4 h2h3 g4f3 d1f3",
    "e2e4 c7c6 d2d4 d7d5 b1d2 d5e4 d2e4 b8d7 g1f3",

    // === Pirc / Modern / Alekhine ===
    "e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 f2f4 f8g7 g1f3 e8g8 f1d3 b8c6",
    "e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 g1f3 f8g7 f1e2 e8g8",
    "e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 c1e3 f8g7 d1d2 c7c6",
    "e2e4 g7g6 d2d4 f8g7 b1c3 d7d6 f2f4 g8f6 g1f3 e8g8",
    "e2e4 g7g6 d2d4 f8g7 g1f3 d7d6 f1e2 g8f6",
    "e2e4 g8f6 e4e5 f6d5 d2d4 d7d6 g1f3 g7g6 f1c4 d5b6 c4b3",
    "e2e4 g8f6 e4e5 f6d5 d2d4 d7d6 c2c4 d5b6 e5d6 c7d6",
    "e2e4 g8f6 b1c3 d7d5 e4d5 f6d5",

    // === Scandinavian ===
    "e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 g8f6 g1f3 c7c6",
    "e2e4 d7d5 e4d5 d8d5 b1c3 d5d6",
    "e2e4 d7d5 e4d5 d8d5 b1c3 d5d8",
    "e2e4 d7d5 e4d5 g8f6 d2d4 f6d5 g1f3 g7g6",
    "e2e4 d7d5 e4d5 g8f6 c2c4 c7c6",

    // === 1.d4 d5 openings ===
    // QGA
    "d2d4 d7d5 c2c4 d5c4 g1f3 g8f6 e2e3 e7e6 f1c4 c7c5 e1g1 a7a6",
    "d2d4 d7d5 c2c4 d5c4 e2e4 e7e5 g1f3 e5d4 f1c4 g8f6",
    "d2d4 d7d5 c2c4 d5c4 g1f3 a7a6 e2e3 g8f6",
    // QGD
    "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8 g1f3 h7h6 g5h4 b7b6",
    "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 b8d7 g1f3 f8e7 e2e3 e8g8",
    "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 h7h6 g5h4 e8g8 g1f3 b7b6",
    "d2d4 d7d5 c2c4 e7e6 b1c3 c7c6 g1f3 g8f6 c1g5 d5c4",
    "d2d4 d7d5 c2c4 e7e6 g1f3 g8f6 b1c3 f8e7 c1g5 h7h6",
    // Tarrasch QGD
    "d2d4 d7d5 c2c4 e7e6 b1c3 c7c5 c4d5 e6d5 g1f3 b8c6 g2g3 g8f6 f1g2 f8e7",
    "d2d4 d7d5 c2c4 e7e6 b1c3 c7c5 c4d5 c5d4",
    // Slav
    "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5 e2e3 e7e6 f1c4 f8b4",
    "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 a7a6 c4c5",
    "d2d4 d7d5 c2c4 c7c6 b1c3 g8f6 e2e3 e7e6 g1f3 b8d7",
    // Semi-Slav
    "d2d4 d7d5 c2c4 c7c6 b1c3 g8f6 g1f3 e7e6 c1g5 h7h6 g5f6 d8f6",
    "d2d4 d7d5 c2c4 c7c6 b1c3 g8f6 g1f3 e7e6 e2e3 b8d7 f1d3 d5c4 d3c4 b7b5 c4d3 c8b7",
    "d2d4 d7d5 c2c4 c7c6 b1c3 g8f6 e2e3 e7e6 g1f3 f8d6",
    // Exchange Slav
    "d2d4 d7d5 c2c4 c7c6 c4d5 c6d5",
    // London
    "d2d4 d7d5 g1f3 g8f6 c1f4 e7e6 e2e3 f8d6 f4d6 c7d6",
    "d2d4 d7d5 g1f3 g8f6 c1f4 c7c5 e2e3 b8c6",
    "d2d4 d7d5 g1f3 g8f6 c1f4 c7c6 e2e3 c8f5",
    // Colle
    "d2d4 d7d5 g1f3 g8f6 e2e3 c7c5 c2c3 e7e6 f1d3 b8c6",

    // === 1.d4 Nf6 ===
    // KID
    "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8 f1e2 e7e5 e1g1 b8c6 d4d5 c6e7",
    "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 f2f3 e8g8 c1e3 e7e5",
    "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 h2h3 e8g8 f1e2 e7e5",
    "d2d4 g8f6 c2c4 g7g6 g1f3 f8g7 g2g3 e8g8 f1g2 d7d6 e1g1 b8d7",
    "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 g2g3 e8g8 f1g2 d7d6",
    // Nimzo-Indian
    "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 e8g8 f1d3 d7d5 g1f3 c7c5 e1g1 b8c6",
    "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 d1c2 e8g8 a2a3 b4c3 c2c3 b7b6",
    "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 a2a3 b4c3 b2c3 c7c5",
    "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 g1f3 c7c5 e2e3 b8c6",
    "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 f2f3 d7d5 a2a3 b4c3",
    // Queen's Indian
    "d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 g2g3 c8b7 f1g2 f8e7 e1g1 e8g8 b1c3 f6e4",
    "d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 g2g3 c8a6 b2b3 f8b4 c1d2 b4e7",
    "d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 a2a3 c8b7 b1c3 d7d5",
    // Grünfeld
    "d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 c4d5 f6d5 e2e4 d5c3 b2c3 f8g7 g1f3 c7c5",
    "d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 g1f3 f8g7 c1f4 e8g8",
    "d2d4 g8f6 c2c4 g7g6 b1c3 d7d5 d1b3 d5c4 b3c4 f8g7",
    // Benoni
    "d2d4 g8f6 c2c4 c7c5 d4d5 e7e6 b1c3 e6d5 c4d5 d7d6 e2e4 g7g6 g1f3 f8g7",
    "d2d4 g8f6 c2c4 c7c5 d4d5 e7e6 g1f3 e6d5 c4d5 d7d6 b1c3 g7g6",
    "d2d4 g8f6 c2c4 c7c5 d4d5 b7b5 c4b5 a7a6 b5a6 g7g6",
    "d2d4 g8f6 c2c4 e7e6 g1f3 c7c5 d4d5 e6d5 c4d5 d7d6",
    // Dutch
    "d2d4 f7f5 g2g3 g8f6 f1g2 e7e6 g1f3 d7d5 e1g1 f8d6 c2c4 c7c6",
    "d2d4 f7f5 g2g3 g8f6 f1g2 g7g6 g1f3 f8g7 e1g1 e8g8 c2c4 d7d6",
    "d2d4 f7f5 g2g3 g8f6 f1g2 e7e6 g1f3 f8e7 e1g1 e8g8 c2c4 d7d6",
    "d2d4 f7f5 c2c4 g8f6 b1c3 e7e6 g1f3 f8b4",
    // Trompowsky / Torre / Veresov
    "d2d4 g8f6 c1g5 e7e6 e2e4 h7h6 g5f6 d8f6",
    "d2d4 g8f6 c1g5 c7c5 d4d5 d8b6",
    "d2d4 g8f6 g1f3 e7e6 c1g5 h7h6 g5f6 d8f6",
    "d2d4 g8f6 g1f3 d7d5 c1g5 e7e6",
    "d2d4 g8f6 b1c3 d7d5 c1g5",

    // === Flank: English, Réti, KIA, Bird's ===
    "c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 g2g3 f8b4 f1g2 e8g8",
    "c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 g2g3 d7d5 c4d5 f6d5",
    "c2c4 e7e5 b1c3 g8f6 g2g3 f8b4 f1g2 e8g8",
    "c2c4 c7c5 g1f3 g8f6 b1c3 b8c6 g2g3 g7g6 f1g2 f8g7",
    "c2c4 c7c5 b1c3 g8f6 g2g3 d7d5 c4d5 f6d5",
    "c2c4 e7e6 b1c3 d7d5 d2d4 g8f6 g1f3 f8e7 c1g5",
    "c2c4 g7g6 g1f3 f8g7 g2g3 e7e5 b1c3 g8e7",
    "g1f3 d7d5 c2c4 e7e6 g2g3 g8f6 f1g2 f8e7 e1g1 e8g8",
    "g1f3 d7d5 c2c4 c7c6 b2b3 g8f6 c1b2 c8f5",
    "g1f3 d7d5 g2g3 g8f6 f1g2 e7e6 e1g1 f8e7 d2d3 e8g8",
    "g1f3 g8f6 c2c4 g7g6 b1c3 d7d5 c4d5 f6d5",
    "g1f3 c7c5 g2g3 b8c6 f1g2 g7g6 d2d4 c5d4 f3d4",
    "f2f4 d7d5 g1f3 g8f6 e2e3 g7g6 f1e2 f8g7 e1g1 e8g8",
    "f2f4 e7e5 f4e5 d7d6 e5d6 f8d6 g1f3",
    "b2b3 e7e5 c1b2 b8c6 e2e3 g8f6 f1b5 f8d6",
    "b2b4 e7e5 c1b2 f8b4 b2e5 g8f6",
    "b1c3 d7d5 e2e4 d5d4 c3e2 e7e5",
    "d2d3 d7d5 g1f3 g8f6 g2g3 c7c5",

    // === Transpositions & rare ===
    "e2e4 b7b6 d2d4 c8b7 b1c3 e7e6 g1f3 f8b4",
    "e2e4 b8c6 d2d4 d7d5 b1c3 d5e4 d4d5 c6e5",
    "e2e4 a7a6 d2d4 b7b5 g1f3 c8b7",
    "d2d4 e7e6 c2c4 b7b6 g1f3 c8b7",
    "d2d4 b7b6 c2c4 c8b7 b1c3 e7e6",
    "d2d4 c7c5 d4d5 e7e5 e2e4 d7d6",
    "d2d4 c7c6 c2c4 d7d5 g1f3 g8f6 b1c3 e7e6",
    "d2d4 d7d6 e2e4 g8f6 b1c3 g7g6",
    "d2d4 g7g6 c2c4 f8g7 b1c3 d7d6 e2e4 g8f6",
    "d2d4 e7e6 e2e4 d7d5 b1c3 g8f6",
    "d2d4 g8f6 c2c4 c7c6",
    "e2e4 e7e5 g1f3 b8c6 b1c3 g8f6 d2d4 e5d4 f3d4",
    "e2e4 e7e5 g1f3 b8c6 c2c3 g8f6 d2d4 f6e4",
    "e2e4 e7e5 f1c4 g8f6 d2d3",
    "e2e4 e7e5 d2d3 g8f6 g1f3 b8c6 g2g3",
    "e2e4 e7e5 g1e2 g8f6 b1c3",
    "e2e4 c7c5 f1c4 e7e6",
    "e2e4 e7e6 d2d3 d7d5 b1d2 g8f6",
    "e2e4 e7e5 g1f3 b8c6 c2c4 g8f6",

    // === Additional Sicilian lines ===
    "e2e4 c7c5 g1f3 d7d6 f1b5 g8f6 b1c3",
    "e2e4 c7c5 b1c3 d7d6 g2g3 b8c6 f1g2 g8f6",
    "e2e4 c7c5 g1e2 b8c6 b1c3 g7g6",
    "e2e4 c7c5 g1f3 d7d6 c2c3 g8f6 f1d3",
    "e2e4 c7c5 g1f3 e7e6 d2d3 b8c6 g2g3 g8f6",
    "e2e4 c7c5 g1f3 b8c6 c2c3 g8f6 e4e5 f6d5 d2d4 c5d4",
    "e2e4 c7c5 g1f3 g7g6 c2c3 f8g7 d2d4 c5d4 c3d4",
    "e2e4 c7c5 g1f3 b8c6 f1b5 g8f6 b5c6 b7c6 e4e5 f6d5",
    "e2e4 c7c5 g1f3 b8c6 b1c3 e7e5 f1c4 f8e7",
    "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 d8c7",
    "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 e7e6 b1c3 a7a6",
    "e2e4 c7c5 d2d4 d7d6 g1f3",

    // === Additional 1.d4 variations ===
    "d2d4 d7d5 c2c4 g8f6",
    "d2d4 d7d5 c2c4 b8c6",
    "d2d4 d7d5 c2c4 c8f5",
    "d2d4 d7d5 c2c4 c7c5 c4d5 d8d5",
    "d2d4 d7d5 b1c3 g8f6 c1g5 c8f5",
    "d2d4 d7d5 c1f4 g8f6 e2e3 e7e6",
    "d2d4 g8f6 g1f3 g7g6 g2g3 f8g7 f1g2 e8g8",
    "d2d4 g8f6 g1f3 d7d5 g2g3 c7c6 f1g2 c8f5",
    "d2d4 g8f6 g1f3 e7e6 g2g3 b7b6 f1g2 c8b7",
    "d2d4 g8f6 g1f3 c7c5 d4d5 e7e6 c2c4 e6d5",
    "d2d4 g8f6 c2c4 e7e6 g1f3 d7d5 c1g5 d5c4",
    "d2d4 g8f6 c2c4 d7d6 b1c3 e7e5 g1f3 b8d7",
    "d2d4 g8f6 c2c4 g7g6 f2f3 d7d5 c4d5 f6d5 e2e4 d5b6",
    "d2d4 g8f6 c2c4 e7e6 g2g3 d7d5 f1g2 f8e7",

    // === More black responses after 1.e4 ===
    "e2e4 c7c6 b1c3 d7d5 g1f3 d5e4 c3e4 c8g4",
    "e2e4 d7d5 b1c3 d5d4 c3e2 e7e5",
    "e2e4 c7c5 c2c4 b8c6 b1c3 g7g6",
    "e2e4 e7e5 g1f3 g8f6 b1c3 b8c6 d2d4 e5d4 f3d4",
    "e2e4 e7e5 b1c3 b8c6 g1f3 g8f6 d2d4 e5d4 c3d5",
    "e2e4 e7e5 b1c3 g8f6 f2f4 d7d5 f4e5 f6e4 g1f3",

    // === More after 1.Nf3 / c4 ===
    "g1f3 g8f6 c2c4 b7b6 b1c3 c8b7 e2e4 d7d6",
    "g1f3 c7c5 c2c4 g8f6 b1c3 b8c6 g2g3 g7g6",
    "c2c4 b7b6 d2d4 c8b7 b1c3 e7e6 e2e4 f8b4",
    "c2c4 g8f6 g1f3 g7g6 b1c3 f8g7 d2d4 e8g8",
    "c2c4 g8f6 b1c3 e7e5 g2g3 f8b4 f1g2 e8g8 e2e4",
    "c2c4 g8f6 b1c3 g7g6 e2e4 d7d6 d2d4 f8g7",
    "c2c4 c7c6 g1f3 d7d5 b2b3 c8f5 c1b2 e7e6",
    "c2c4 c7c6 d2d4 d7d5 c4d5 c6d5",
};

constexpr int NUM_LINES = (int)(sizeof(LINES) / sizeof(LINES[0]));

void set_enabled(bool on) { enabled_ = on; }
bool is_enabled() { return enabled_; }
size_t size() { return table.size(); }

static Move find_legal_move(const Position& pos, const std::string& uci) {
    Move m = pos.parse_uci(uci);
    if (m == MOVE_NONE) return MOVE_NONE;
    MoveList list;
    generate_moves(pos, list);
    for (int i = 0; i < list.size; ++i) {
        Move c = list.moves[i].move;
        if (from_sq(c) == from_sq(m) && to_sq(c) == to_sq(m)) {
            if (type_of_move(m) == MT_PROMOTION && promo_type(c) != promo_type(m)) continue;
            return c;
        }
    }
    return MOVE_NONE;
}

void init() {
    table.clear();
    int lines_ok = 0, lines_partial = 0;
    for (int li = 0; li < NUM_LINES; ++li) {
        Position p;
        p.set_startpos();
        const char* s = LINES[li];
        bool full = true;
        int plies_done = 0;
        while (*s) {
            while (*s == ' ') ++s;
            if (!*s) break;
            const char* end = s;
            while (*end && *end != ' ') ++end;
            std::string tok(s, end - s);
            s = end;
            Move m = find_legal_move(p, tok);
            if (m == MOVE_NONE) { full = false; break; }
            auto& vec = table[p.key()];
            bool seen = false;
            for (Move e : vec) if (e == m) { seen = true; break; }
            if (!seen) vec.push_back(m);
            p.do_move(m);
            ++plies_done;
        }
        if (full) ++lines_ok;
        else if (plies_done > 0) ++lines_partial;
    }
    std::printf("info string book: %d lines loaded (%d full, %d partial), %zu positions\n",
                lines_ok + lines_partial, lines_ok, lines_partial, table.size());
    std::fflush(stdout);
}

Move probe(const Position& pos) {
    if (!enabled_) return MOVE_NONE;
    auto it = table.find(pos.key());
    if (it == table.end() || it->second.empty()) return MOVE_NONE;
    const auto& moves = it->second;
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, moves.size() - 1);
    return moves[dist(rng)];
}

} // namespace Book
