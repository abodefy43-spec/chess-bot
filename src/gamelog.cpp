#include "gamelog.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>

namespace GameLog {

static bool enabled_ = false;
static std::string path_ = "/Users/qeuapp/Desktop/Chess/training_data.csv";
static int ply_counter_ = 0;
static U64 game_id_ = 0;
static std::mutex file_mtx_;
static bool header_written_ = false;

static U64 now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void set_enabled(bool on) { enabled_ = on; }
bool is_enabled() { return enabled_; }

void set_path(const std::string& p) {
    std::lock_guard<std::mutex> lock(file_mtx_);
    path_ = p;
    header_written_ = false;
}

void new_game() {
    game_id_ = now_ms();
    ply_counter_ = 0;
}

static void ensure_header() {
    std::ifstream in(path_);
    if (in.good()) { header_written_ = true; return; }
    std::ofstream out(path_, std::ios::app);
    if (out.good()) {
        out << "game_id,ply,stm,fen,move_uci,score_cp,depth,nodes,time_ms,source\n";
        header_written_ = true;
    }
}

void record(const Position& pos_before, Move move, int score_cp,
            int depth, U64 nodes, int time_ms, const char* source) {
    if (!enabled_ || move == MOVE_NONE) return;
    std::lock_guard<std::mutex> lock(file_mtx_);
    if (game_id_ == 0) new_game();
    if (!header_written_) ensure_header();

    std::ofstream out(path_, std::ios::app);
    if (!out.good()) return;

    // Note: FEN contains no commas but might contain spaces; quote it just in case.
    std::string fen = pos_before.fen();
    std::string mv  = pos_before.move_to_uci(move);
    const char* stm = (pos_before.side_to_move() == WHITE) ? "w" : "b";

    out << game_id_ << ','
        << ply_counter_ << ','
        << stm << ','
        << '"' << fen << '"' << ','
        << mv << ','
        << score_cp << ','
        << depth << ','
        << nodes << ','
        << time_ms << ','
        << source << '\n';

    ++ply_counter_;
}

} // namespace GameLog
