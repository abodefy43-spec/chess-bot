#include "bitboard.h"
#include "zobrist.h"
#include "position.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include "search.h"
#include "book.h"
#include "gamelog.h"
#include <chrono>

#include <atomic>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

static Position g_pos;
static std::thread g_search_thread;

static void join_search() {
    if (g_search_thread.joinable()) {
        Search::stop_flag.store(true);
        g_search_thread.join();
    }
}

static void cmd_uci() {
    std::cout << "id name Ripper 0.1\n";
    std::cout << "id author you\n";
    std::cout << "option name Hash type spin default 16 min 1 max 4096\n";
    std::cout << "option name Threads type spin default 4 min 1 max 16\n";
    std::cout << "option name OwnBook type check default true\n";
    std::cout << "option name LogData type check default true\n";
    std::cout << "option name LogPath type string default /Users/qeuapp/Desktop/Chess/training_data.csv\n";
    std::cout << "uciok" << std::endl;
}

static void cmd_isready() {
    std::cout << "readyok" << std::endl;
}

static void cmd_ucinewgame() {
    TT.clear();
    Search::init();
    GameLog::new_game();
}

static void cmd_setoption(std::istringstream& iss) {
    std::string token, name, value;
    iss >> token;  // "name"
    std::string cur;
    while (iss >> cur && cur != "value") {
        if (!name.empty()) name += " ";
        name += cur;
    }
    while (iss >> cur) {
        if (!value.empty()) value += " ";
        value += cur;
    }
    if (name == "Hash") {
        int mb = std::stoi(value);
        TT.resize(mb);
    } else if (name == "Threads") {
        Search::set_threads(std::stoi(value));
    } else if (name == "OwnBook") {
        Book::set_enabled(value == "true" || value == "True");
    } else if (name == "LogData") {
        GameLog::set_enabled(value == "true" || value == "True");
    } else if (name == "LogPath") {
        GameLog::set_path(value);
    }
}

static bool apply_move_string(const std::string& s) {
    Move m = g_pos.parse_uci(s);
    if (m == MOVE_NONE) return false;
    // Verify move is among legal moves (parse_uci can produce wrong type for EP/castle fringe cases)
    MoveList list;
    generate_moves(g_pos, list);
    for (int i = 0; i < list.size; ++i) {
        Move c = list.moves[i].move;
        if (from_sq(c) == from_sq(m) && to_sq(c) == to_sq(m)) {
            if (type_of_move(m) == MT_PROMOTION && promo_type(c) != promo_type(m)) continue;
            g_pos.do_move(c);
            return true;
        }
    }
    return false;
}

static void cmd_position(std::istringstream& iss) {
    std::string token;
    iss >> token;

    if (token == "startpos") {
        g_pos.set_startpos();
        iss >> token;  // possibly "moves"
    } else if (token == "fen") {
        std::string fen;
        for (int i = 0; i < 6; ++i) {
            std::string part;
            if (!(iss >> part)) break;
            if (part == "moves") { token = "moves"; break; }
            if (!fen.empty()) fen += " ";
            fen += part;
        }
        g_pos.set_fen(fen);
        if (token != "moves") iss >> token;
    }

    if (token == "moves") {
        std::string mv;
        while (iss >> mv) apply_move_string(mv);
    }
}

static void cmd_go(std::istringstream& iss) {
    join_search();
    SearchLimits lim;
    std::string tok;
    while (iss >> tok) {
        if      (tok == "wtime")    iss >> lim.wtime;
        else if (tok == "btime")    iss >> lim.btime;
        else if (tok == "winc")     iss >> lim.winc;
        else if (tok == "binc")     iss >> lim.binc;
        else if (tok == "movestogo") iss >> lim.movestogo;
        else if (tok == "movetime") iss >> lim.movetime;
        else if (tok == "depth")    iss >> lim.depth;
        else if (tok == "nodes")    iss >> lim.max_nodes;
        else if (tok == "infinite") lim.infinite = true;
    }

    // Probe opening book first — if hit, skip search entirely.
    Move book_move = Book::probe(g_pos);
    if (book_move != MOVE_NONE) {
        std::cout << "info string book hit\n";
        GameLog::record(g_pos, book_move, 0, 0, 0, 0, "book");
        std::cout << "bestmove " << g_pos.move_to_uci(book_move) << std::endl;
        return;
    }

    g_search_thread = std::thread([lim]() {
        auto t0 = std::chrono::steady_clock::now();
        SearchResult res = Search::go(g_pos, lim);
        auto t1 = std::chrono::steady_clock::now();
        int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        GameLog::record(g_pos, res.best_move, res.score, res.depth_reached,
                        res.nodes, ms, "search");
        std::cout << "bestmove " << g_pos.move_to_uci(res.best_move);
        if (res.ponder_move != MOVE_NONE)
            std::cout << " ponder " << g_pos.move_to_uci(res.ponder_move);
        std::cout << std::endl;
    });
}

static void cmd_stop() {
    Search::stop_flag.store(true);
    join_search();
}

// Simple perft for debugging movegen.
static U64 perft(Position& pos, int depth) {
    if (depth == 0) return 1;
    MoveList list;
    generate_moves(pos, list);
    U64 n = 0;
    for (int i = 0; i < list.size; ++i) {
        Move m = list.moves[i].move;
        pos.do_move(m);
        n += perft(pos, depth - 1);
        pos.undo_move(m);
    }
    return n;
}

static void cmd_perft(std::istringstream& iss) {
    int d; iss >> d;
    auto t0 = std::chrono::steady_clock::now();
    MoveList list;
    generate_moves(g_pos, list);
    U64 total = 0;
    for (int i = 0; i < list.size; ++i) {
        Move m = list.moves[i].move;
        g_pos.do_move(m);
        U64 n = (d > 1) ? perft(g_pos, d - 1) : 1;
        g_pos.undo_move(m);
        std::cout << g_pos.move_to_uci(m) << ": " << n << "\n";
        total += n;
    }
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "\nNodes: " << total << "  Time: " << ms << " ms\n";
}

int main() {
    BB::init();
    Zobrist::init();
    Eval::init();
    Search::init();
    TT.resize(16);
    Search::set_threads(4);
    g_pos.set_startpos();
    Book::init();
    GameLog::set_enabled(true);
    GameLog::new_game();

    std::cout << "Ripper 0.1 (UCI)\n" << std::flush;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if      (cmd == "uci")        cmd_uci();
        else if (cmd == "isready")    cmd_isready();
        else if (cmd == "ucinewgame") cmd_ucinewgame();
        else if (cmd == "position")   cmd_position(iss);
        else if (cmd == "go")         cmd_go(iss);
        else if (cmd == "stop")       cmd_stop();
        else if (cmd == "setoption")  cmd_setoption(iss);
        else if (cmd == "quit") {
            cmd_stop();
            break;
        }
        else if (cmd == "d")          g_pos.print();
        else if (cmd == "perft")      cmd_perft(iss);
    }

    join_search();
    return 0;
}
