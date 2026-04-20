# Ripper

A chess engine in C++20, built from scratch. Plays on Lichess as [Akera_bot](https://lichess.org/@/Akera_bot).

Estimated strength: **~2000–2200 Elo**, single binary, no dependencies.

## Highlights

- **Bitboard board representation** with legal move generation (pins + checkers + king-danger, computed once per node — not make/unmake legality filtering)
- **Lazy SMP** multi-threaded alpha-beta search (configurable 1–16 threads, ~2.5× speedup at 4 threads)
- **Static Exchange Evaluation (SEE)** for capture ordering and quiescence pruning
- **Transposition table** with Zobrist hashing, aging, and depth-preferred replacement
- **Iterative deepening** with aspiration windows, null-move pruning, late-move reductions, reverse futility, check extensions
- **Tapered evaluation** using PeSTO piece-square tables + bishop pair
- **418-line opening book** (1,558 unique positions) covering all major main lines deep
- **UCI protocol** — plug into any UCI GUI (Arena, Cute Chess) or Lichess via lichess-bot
- **NNUE scaffolding** — full training pipeline (PyTorch → int16 quantization → C++ inference), experimental (see below)
- **Adaptive time management** — tiered per-move caps for bullet/blitz/rapid/classical, opponent-time-aware

## Benchmarks

### Movegen correctness (perft, all exact)
| Position            | Depth | Nodes          |
|---------------------|:-----:|---------------:|
| Start position      | 5     | 4,865,609      |
| Kiwipete            | 4     | 4,085,603      |
| Endgame (pos. 3)    | 5     | 674,624        |
| Underpromotion test | 6     | 71,179,139     |

### Search speed (M-series, 3-sec middlegame search from a Najdorf position)
| Threads | Depth reached | NPS        |
|:-------:|:-------------:|-----------:|
| 1       | 16            | 22 M/s     |
| 4       | 17            | 55 M/s     |

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│                      UCI front-end                          │
│          (main.cpp: command loop, options, bookkeeping)     │
└────────────┬───────────────────────────────────┬────────────┘
             │                                   │
 ┌───────────▼───────────┐          ┌────────────▼───────────┐
 │   Search              │          │   Opening book          │
 │   - Iterative deepen. │          │   - 418 hand-authored   │
 │   - Negamax + α-β     │          │     lines               │
 │   - Quiescence + SEE  │          │   - Hash-indexed probe  │
 │   - Null/LMR/RFP      │          └─────────────────────────┘
 │   - Lazy SMP (N thr.) │
 └───┬────────────┬──────┘          ┌─────────────────────────┐
     │            │                 │   Transposition table   │
     │            └────────────────▶│   - Zobrist + age       │
     │                              └─────────────────────────┘
     │
 ┌───▼───────────────────┐          ┌─────────────────────────┐
 │   Evaluate            │          │   Move generation       │
 │   - PeSTO (classical) │◀─────────│   - Bitboards (pins,    │
 │   - NNUE (optional)   │          │     checkers, legal)    │
 └───────────────────────┘          │   - Classical sliders   │
                                    └─────────────────────────┘
```

## Build & run

Requires a C++20 compiler (clang or gcc). No external dependencies.

```bash
make
./ripper
```

Then at the prompt:
```
uci
position startpos
go movetime 2000
```

## Play against it on Lichess

The bot runs as [@Akera_bot](https://lichess.org/@/Akera_bot) when the owner launches the bridge. To set up your own:

1. Create a fresh Lichess account with zero games played.
2. Upgrade it to a BOT account: `curl -d '' https://lichess.org/api/bot/account/upgrade -H "Authorization: Bearer $YOUR_TOKEN"`
3. Copy `.env.example` to `.env` and paste your bot token.
4. Double-click `start_bot.command` (macOS) — it auto-clones the `lichess-bot` bridge, generates config, builds the engine, and goes online.

## Training data + NNUE (experimental)

The `training/` directory contains a working NN pipeline:
- `self_play.py` — parallel self-play generator, writes PGN + JSONL training data
- `training/dataset.py` — streams `(fen → 769-dim features, target)` pairs
- `training/model.py` — `SmallNNUE` PyTorch module (769 → 256 → 32 → 1, clamped-ReLU)
- `training/train.py` — training loop + int16 quantizer producing `nnue.bin` for the C++ engine
- `src/nnue.cpp` — dequantized forward pass, linked into the engine

### Honest phase-A result

Trained on 363,062 positions from 2,500 self-play games in 2 min. Loss curve:

```
epoch 0:  0.259
epoch 1:  0.174
epoch 2:  0.131
epoch 3:  0.109
epoch 4:  0.096
epoch 5:  0.087
epoch 6:  0.080
epoch 7:  0.077
```

Python and C++ inference match within search noise, confirming the quantization and integration are correct. However the resulting network lost a 20-game gauntlet to the classical PeSTO eval **0–20** — the training corpus is too small and the teacher signal (my own engine's eval) creates a distillation ceiling. **NNUE is disabled by default** (`UseNNUE=false`). The infrastructure is real, the experiment is documented, and the path forward is clear: larger dataset (master games or 10× more self-play), HalfKP features, longer training.

## Project layout

```
src/              C++ engine
  types.h         Bitboard + move encoding
  bitboard.cpp    Attack tables, rays, BetweenBB/LineBB
  position.cpp    Position, make/unmake, SEE, legality helpers
  movegen.cpp     Legal move generation (pins + checkers + king danger)
  eval.cpp        PeSTO tapered evaluation
  nnue.cpp        Neural-net inference (optional)
  search.cpp      Negamax + quiescence + Lazy SMP
  tt.cpp          Transposition table
  book.cpp        Opening book (418 lines, 1558 positions)
  uci.cpp         UCI I/O (in main.cpp)
training/         PyTorch NNUE training
self_play.py      Parallel engine-vs-engine data generator
start_bot.command macOS one-click Lichess bot launcher
config.template.yml lichess-bot configuration template
```

## Roadmap

Ordered by expected Elo per effort:

1. **Magic bitboards** for sliding pieces (+10–20% NPS)
2. **Clustered TT** with 4-entry buckets (+20–60 Elo)
3. **Better classical eval** — mobility, king safety, passed-pawn bonuses (+100–200 Elo)
4. **Phase-B NNUE** — HalfKP features, incremental accumulator, SIMD int8 inference, trained on master-game outcomes (+300–500 Elo if done right)
5. **History gravity, continuation history, counter-moves** (+50–100 Elo)

## License

MIT.
