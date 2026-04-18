#include "search.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace Search {

std::atomic<bool> stop_flag{false};

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::milliseconds;

struct SearchStack {
    Move killer[2] = { MOVE_NONE, MOVE_NONE };
    Move excluded = MOVE_NONE;
    int  static_eval = VALUE_NONE;
};

// Per-thread state (each search thread has its own copies).
thread_local int  history[2][64][64];
thread_local Move pv_table[MAX_PLY][MAX_PLY];
thread_local int  pv_len[MAX_PLY];
thread_local U64  t_nodes;            // nodes counted by this thread
thread_local int  t_thread_id = 0;

// Shared search controls.
static std::atomic<U64> g_total_nodes{0};
static Clock::time_point g_start;
static int  g_soft_time_ms;
static int  g_hard_time_ms;
static SearchLimits g_limits;
static int  g_num_threads = 1;

static inline int piece_value(PieceType pt) {
    static const int v[7] = { 0, 100, 320, 330, 500, 900, 20000 };
    return v[pt];
}

static inline bool is_capture(const Position& pos, Move m) {
    if (type_of_move(m) == MT_EN_PASSANT) return true;
    return pos.piece_on(to_sq(m)) != NO_PIECE;
}

static inline bool check_time() {
    if (stop_flag.load(std::memory_order_relaxed)) return true;
    if (g_limits.infinite) return false;
    auto now = Clock::now();
    int elapsed = (int)std::chrono::duration_cast<Ms>(now - g_start).count();
    if (g_hard_time_ms > 0 && elapsed >= g_hard_time_ms) {
        stop_flag.store(true, std::memory_order_relaxed);
        return true;
    }
    if (g_limits.max_nodes && g_total_nodes.load(std::memory_order_relaxed) >= g_limits.max_nodes) {
        stop_flag.store(true, std::memory_order_relaxed);
        return true;
    }
    return false;
}

// MVV-LVA: victim - aggressor/10 (rough ordering score)
static int mvv_lva(const Position& pos, Move m) {
    PieceType victim = type_of(pos.piece_on(to_sq(m)));
    if (type_of_move(m) == MT_EN_PASSANT) victim = PAWN;
    PieceType attacker = type_of(pos.piece_on(from_sq(m)));
    return piece_value(victim) * 10 - piece_value(attacker);
}

static void score_moves(const Position& pos, MoveList& list, Move tt_move,
                        const SearchStack* ss) {
    Color us = pos.side_to_move();
    for (int i = 0; i < list.size; ++i) {
        Move m = list.moves[i].move;
        if (m == tt_move) {
            list.moves[i].score = 1 << 30;
        } else if (type_of_move(m) == MT_PROMOTION && promo_type(m) == QUEEN) {
            list.moves[i].score = 900000 + mvv_lva(pos, m);
        } else if (is_capture(pos, m)) {
            list.moves[i].score = 800000 + mvv_lva(pos, m);
        } else if (m == ss->killer[0]) {
            list.moves[i].score = 700000;
        } else if (m == ss->killer[1]) {
            list.moves[i].score = 600000;
        } else {
            list.moves[i].score = history[us][from_sq(m)][to_sq(m)];
        }
    }
}

// Select the next best-scored move via a single pass (selection sort per pick).
static Move pick_next_move(MoveList& list, int start) {
    int best_i = start;
    int best_s = list.moves[start].score;
    for (int i = start + 1; i < list.size; ++i) {
        if (list.moves[i].score > best_s) {
            best_s = list.moves[i].score;
            best_i = i;
        }
    }
    if (best_i != start) std::swap(list.moves[start], list.moves[best_i]);
    return list.moves[start].move;
}

static int quiescence(Position& pos, int alpha, int beta, int ply) {
    ++t_nodes;
    if ((t_nodes & 2047) == 0) {
        g_total_nodes.fetch_add(2048, std::memory_order_relaxed);
        check_time();
    }
    if (stop_flag.load(std::memory_order_relaxed)) return 0;
    if (ply >= MAX_PLY - 1) return Eval::evaluate(pos);

    int stand_pat = Eval::evaluate(pos);
    if (stand_pat >= beta) return stand_pat;
    if (stand_pat > alpha) alpha = stand_pat;

    MoveList list;
    generate_moves(pos, list, GEN_CAPTURES);

    SearchStack dummy_ss;
    score_moves(pos, list, MOVE_NONE, &dummy_ss);

    int best = stand_pat;
    for (int i = 0; i < list.size; ++i) {
        Move m = pick_next_move(list, i);
        // Delta pruning: if even a queen capture can't raise alpha, skip.
        // (Approximate, turned off here near the endgame to preserve tactics.)
        if (!pos.is_legal(m)) continue;

        pos.do_move(m);
        int score = -quiescence(pos, -beta, -alpha, ply + 1);
        pos.undo_move(m);

        if (stop_flag.load(std::memory_order_relaxed)) return 0;

        if (score > best) {
            best = score;
            if (score > alpha) alpha = score;
            if (alpha >= beta) break;
        }
    }
    return best;
}

static int negamax(Position& pos, int depth, int alpha, int beta, int ply,
                   SearchStack* ss, bool cut_node, bool do_null) {
    pv_len[ply] = 0;

    if (ply > 0 && pos.is_repetition_or_50move()) return VALUE_DRAW;

    bool in_check = pos.in_check();

    // Check extension
    if (in_check) depth++;

    if (depth <= 0) return quiescence(pos, alpha, beta, ply);

    ++t_nodes;
    if ((t_nodes & 2047) == 0) {
        g_total_nodes.fetch_add(2048, std::memory_order_relaxed);
        check_time();
    }
    if (stop_flag.load(std::memory_order_relaxed)) return 0;
    if (ply >= MAX_PLY - 1) return Eval::evaluate(pos);

    bool pv_node = (beta - alpha) > 1;
    bool root = (ply == 0);

    int orig_alpha = alpha;

    // Mate distance pruning
    if (!root) {
        alpha = std::max(alpha, -VALUE_MATE + ply);
        beta  = std::min(beta,   VALUE_MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }

    // TT probe
    TTEntry tte;
    bool tt_hit = TT.probe(pos.key(), tte);
    Move tt_move = MOVE_NONE;
    int  tt_score = VALUE_NONE;
    if (tt_hit) {
        tt_move = tte.move;
        tt_score = TranspositionTable::score_from_tt(tte.score, ply);
        if (!pv_node && tte.depth >= depth + 1) {
            Bound b = Bound(tte.bound_gen & 3);
            if ((b == BOUND_EXACT)
             || (b == BOUND_LOWER && tt_score >= beta)
             || (b == BOUND_UPPER && tt_score <= alpha))
                return tt_score;
        }
    }

    // Static eval
    int static_eval;
    if (in_check) static_eval = VALUE_NONE;
    else if (tt_hit) static_eval = tte.eval ? tte.eval : Eval::evaluate(pos);
    else static_eval = Eval::evaluate(pos);
    ss->static_eval = static_eval;

    // Reverse futility pruning
    if (!pv_node && !in_check && depth <= 8
        && static_eval - 80 * depth >= beta
        && static_eval < VALUE_MATE_IN_MAX_PLY)
        return static_eval;

    // Null move pruning
    if (!pv_node && !in_check && do_null && depth >= 3
        && static_eval >= beta
        && pos.non_pawn_material(pos.side_to_move()) > 0) {
        int R = 3 + depth / 4;
        pos.do_null_move();
        int score = -negamax(pos, depth - R - 1, -beta, -beta + 1, ply + 1, ss + 1,
                             !cut_node, false);
        pos.undo_null_move();
        if (stop_flag.load(std::memory_order_relaxed)) return 0;
        if (score >= beta) {
            if (score >= VALUE_MATE_IN_MAX_PLY) score = beta;
            return score;
        }
    }

    MoveList list;
    generate_moves(pos, list, GEN_ALL);
    score_moves(pos, list, tt_move, ss);

    int best_score = -VALUE_INF;
    Move best_move = MOVE_NONE;
    int moves_searched = 0;

    for (int i = 0; i < list.size; ++i) {
        Move m = pick_next_move(list, i);
        if (!pos.is_legal(m)) continue;

        bool is_cap_or_promo = is_capture(pos, m) || type_of_move(m) == MT_PROMOTION;
        moves_searched++;

        // Late move reduction
        int new_depth = depth - 1;
        int extension = 0;
        int reduction = 0;
        if (depth >= 3 && moves_searched >= 4 && !in_check && !is_cap_or_promo) {
            reduction = 1 + (moves_searched > 8);
            if (pv_node) reduction = std::max(0, reduction - 1);
        }

        pos.do_move(m);

        int score;
        if (moves_searched == 1) {
            score = -negamax(pos, new_depth + extension, -beta, -alpha, ply + 1, ss + 1,
                             !cut_node, true);
        } else {
            score = -negamax(pos, new_depth - reduction, -(alpha + 1), -alpha, ply + 1,
                             ss + 1, true, true);
            if (score > alpha && reduction > 0)
                score = -negamax(pos, new_depth, -(alpha + 1), -alpha, ply + 1, ss + 1,
                                 !cut_node, true);
            if (score > alpha && score < beta)
                score = -negamax(pos, new_depth, -beta, -alpha, ply + 1, ss + 1, false, true);
        }

        pos.undo_move(m);

        if (stop_flag.load(std::memory_order_relaxed)) return 0;

        if (score > best_score) {
            best_score = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;
                // Update PV
                pv_table[ply][0] = m;
                std::memcpy(&pv_table[ply][1], &pv_table[ply+1][0], pv_len[ply+1] * sizeof(Move));
                pv_len[ply] = pv_len[ply+1] + 1;
                if (alpha >= beta) {
                    // Beta cutoff
                    if (!is_cap_or_promo) {
                        // Update killers
                        if (ss->killer[0] != m) {
                            ss->killer[1] = ss->killer[0];
                            ss->killer[0] = m;
                        }
                        // History
                        Color us = pos.side_to_move();
                        int bonus = depth * depth;
                        int& h = history[us][from_sq(m)][to_sq(m)];
                        h += bonus - h * std::abs(bonus) / 16384;
                    }
                    break;
                }
            }
        }
    }

    if (moves_searched == 0) {
        return in_check ? -VALUE_MATE + ply : VALUE_DRAW;
    }

    // TT store
    Bound bound = (best_score >= beta) ? BOUND_LOWER
                : (best_score > orig_alpha) ? BOUND_EXACT
                : BOUND_UPPER;
    TT.store(pos.key(), best_move, best_score, static_eval, depth, bound, ply);

    return best_score;
}

void init() {
    // History is thread_local — only the calling thread's table is cleared here.
    std::memset(history, 0, sizeof(history));
}

void set_threads(int n) {
    if (n < 1) n = 1;
    if (n > 32) n = 32;
    g_num_threads = n;
}

int threads() { return g_num_threads; }

static void setup_time(Position& pos, const SearchLimits& lim) {
    g_soft_time_ms = 0;
    g_hard_time_ms = 0;
    if (lim.movetime > 0) {
        g_soft_time_ms = lim.movetime;
        g_hard_time_ms = lim.movetime;
        return;
    }
    int my_time  = (pos.side_to_move() == WHITE) ? lim.wtime : lim.btime;
    int opp_time = (pos.side_to_move() == WHITE) ? lim.btime : lim.wtime;
    int inc      = (pos.side_to_move() == WHITE) ? lim.winc  : lim.binc;
    if (my_time <= 0 && inc <= 0) return;

    // Expected moves remaining. Fast games tend to be longer in plies; also
    // we want to be extra conservative when we can't afford to flag.
    int mtg = lim.movestogo > 0 ? lim.movestogo
            : my_time < 15000  ? 50
            : my_time < 60000  ? 45
            : my_time < 180000 ? 40
            :                    30;

    // Reserve a cushion for the end of the game when the clock is low-ish.
    int reserve = 0;
    if (lim.movestogo == 0) {
        if      (my_time < 30000)  reserve = my_time / 4;
        else if (my_time < 120000) reserve = my_time / 6;
        else if (my_time < 300000) reserve = my_time / 8;
    }
    int usable = std::max(0, my_time - reserve);

    int soft = usable / mtg + inc * 3 / 4;
    int hard = soft * 3;

    // Absolute caps as fraction of remaining time.
    int soft_cap = my_time / 6;   // never > 16.7% per move
    int hard_cap = my_time / 4;   // never > 25% even in panic
    if (soft > soft_cap) soft = soft_cap;
    if (hard > hard_cap) hard = hard_cap;

    // Match opponent's pace: if they're way ahead on time, speed up.
    if (opp_time > 0 && my_time > 0) {
        if (my_time * 3 < opp_time) {              // we have <33% of opp
            soft = soft / 2;
            hard = hard / 2;
        } else if (my_time * 4 < opp_time * 5) {   // <80% of opp
            soft = soft * 4 / 5;
            hard = hard * 4 / 5;
        } else if (my_time > opp_time * 2 && inc > 0) {
            // We're way ahead — think a bit longer.
            soft = soft * 5 / 4;
        }
    }

    // Emergency clock.
    if (my_time < 3000) {
        soft = std::max(20, my_time / 60);
        hard = std::max(50, my_time / 30);
    } else if (my_time < 8000 && inc < 500) {
        soft = std::max(60, my_time / 50);
        hard = std::max(120, my_time / 25);
    }

    // Floors + safety margin (avoid flagging on UCI/network latency).
    if (soft < 10) soft = 10;
    if (hard < 10) hard = 10;
    int safety = my_time > 1000 ? 100 : 30;
    if (soft > my_time - safety) soft = std::max(10, my_time - safety);
    if (hard > my_time - safety) hard = std::max(10, my_time - safety);

    g_soft_time_ms = soft;
    g_hard_time_ms = hard;
}

// One thread's iterative-deepening loop. thread_id 0 is the reporter
// (prints UCI info, owns the returned result). Workers assist via the
// shared TT so the main thread searches deeper per iteration (Lazy SMP).
static void thread_loop(int thread_id, Position pos, SearchResult* out_result,
                        int max_depth) {
    t_thread_id = thread_id;
    t_nodes = 0;
    std::memset(history, 0, sizeof(history));

    SearchStack stack[MAX_PLY + 4];
    for (auto& s : stack) {
        s.killer[0] = s.killer[1] = MOVE_NONE;
        s.excluded = MOVE_NONE;
        s.static_eval = VALUE_NONE;
    }

    int last_score = 0;
    bool is_main = (thread_id == 0);

    // Workers start one depth deeper on even ids to diverge search order.
    int start_depth = 1 + ((thread_id > 0) ? (thread_id % 2) : 0);

    for (int depth = start_depth; depth <= max_depth; ++depth) {
        int window = 25;
        int alpha = (depth >= 4) ? last_score - window : -VALUE_INF;
        int beta  = (depth >= 4) ? last_score + window :  VALUE_INF;
        int score;

        while (true) {
            score = negamax(pos, depth, alpha, beta, 0, stack + 1, false, true);
            if (stop_flag.load()) break;
            if (score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(score - window, -VALUE_INF);
                window += window / 2;
            } else if (score >= beta) {
                beta = std::min(score + window, (int)VALUE_INF);
                window += window / 2;
            } else {
                break;
            }
        }

        if (stop_flag.load()) break;

        last_score = score;

        if (is_main) {
            out_result->best_move = pv_table[0][0];
            if (pv_len[0] > 1) out_result->ponder_move = pv_table[0][1];
            out_result->score = score;
            out_result->depth_reached = depth;

            auto now = Clock::now();
            int elapsed = (int)std::chrono::duration_cast<Ms>(now - g_start).count();
            U64 nodes_total = g_total_nodes.load(std::memory_order_relaxed) + t_nodes;

            std::printf("info depth %d score ", depth);
            if (std::abs(score) >= VALUE_MATE_IN_MAX_PLY) {
                int mate_in = (score > 0 ? (VALUE_MATE - score + 1) / 2 : -(VALUE_MATE + score) / 2);
                std::printf("mate %d", mate_in);
            } else {
                std::printf("cp %d", score);
            }
            std::printf(" nodes %llu time %d nps %llu pv",
                        (unsigned long long)nodes_total, elapsed,
                        (unsigned long long)(elapsed > 0 ? nodes_total * 1000 / elapsed : nodes_total));
            for (int i = 0; i < pv_len[0]; ++i) {
                std::printf(" %s", pos.move_to_uci(pv_table[0][i]).c_str());
            }
            std::printf("\n");
            std::fflush(stdout);

            // Only the main thread signals stop based on time/mate conditions.
            if (!g_limits.infinite && g_soft_time_ms > 0 && elapsed >= g_soft_time_ms) {
                stop_flag.store(true);
                break;
            }
            if (std::abs(score) >= VALUE_MATE_IN_MAX_PLY && depth >= 4) {
                int mate_plies = VALUE_MATE - std::abs(score);
                if (depth >= mate_plies + 2) { stop_flag.store(true); break; }
            }
        }
    }

    g_total_nodes.fetch_add(t_nodes, std::memory_order_relaxed);
}

SearchResult go(Position& pos, const SearchLimits& limits) {
    stop_flag.store(false);
    g_total_nodes.store(0);
    g_start = Clock::now();
    g_limits = limits;
    setup_time(pos, limits);

    TT.new_search();

    SearchResult res;
    int max_depth = limits.depth > 0 ? limits.depth : MAX_PLY;
    if (max_depth > MAX_PLY - 2) max_depth = MAX_PLY - 2;

    std::vector<std::thread> workers;
    for (int i = 1; i < g_num_threads; ++i) {
        workers.emplace_back(thread_loop, i, pos, &res, max_depth);
    }
    thread_loop(0, pos, &res, max_depth);
    stop_flag.store(true);
    for (auto& t : workers) t.join();

    res.nodes = g_total_nodes.load(std::memory_order_relaxed);
    return res;
}

} // namespace Search
