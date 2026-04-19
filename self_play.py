#!/usr/bin/env python3
"""
Self-play data generator for the Ripper chess engine.

Runs N games of Ripper vs Ripper in parallel, saves each game as PGN
and each (position, move, eval) triple to a JSONL file for later NN training.

Usage:
    python3 self_play.py --games 1000 --workers 4 --movetime 300

Stop with Ctrl-C. Already-written games are preserved.
"""
import argparse
import concurrent.futures
import datetime
import json
import os
import signal
import sys
import threading
import time
import chess
import chess.engine
import chess.pgn

DEFAULT_ENGINE = "/Users/qeuapp/Desktop/Chess/ripper"
DEFAULT_OUTPUT = "/Users/qeuapp/Desktop/Chess/self_play"

stop_event = threading.Event()
jsonl_lock = threading.Lock()
counter_lock = threading.Lock()
games_completed = 0
plies_total = 0


def open_engine(engine_path: str, hash_mb: int, threads: int) -> chess.engine.SimpleEngine:
    e = chess.engine.SimpleEngine.popen_uci(engine_path)
    # Suppress per-game CSV logging from the engine; this script handles data.
    try:
        e.configure({"Hash": hash_mb, "Threads": threads,
                     "LogData": False, "OwnBook": True})
    except chess.engine.EngineError:
        pass
    return e


def score_to_cp(score, turn_is_white: bool) -> int | None:
    if score is None:
        return None
    pov = score.white() if turn_is_white else score.black()
    if pov.is_mate():
        m = pov.mate()
        if m is None:
            return None
        return 10000 - m if m > 0 else -10000 - m
    return pov.score()


def play_one_game(engine_path: str, movetime_ms: int, game_id: int,
                  output_dir: str, hash_mb: int, threads: int):
    global games_completed, plies_total

    white = open_engine(engine_path, hash_mb, threads)
    black = open_engine(engine_path, hash_mb, threads)

    try:
        board = chess.Board()
        game = chess.pgn.Game()
        game.headers["Event"] = "Ripper Self-Play"
        game.headers["Site"] = "local"
        game.headers["Date"] = datetime.date.today().isoformat()
        game.headers["Round"] = str(game_id)
        game.headers["White"] = "Ripper"
        game.headers["Black"] = "Ripper"
        game.headers["TimeControl"] = f"{movetime_ms}ms/move"
        node = game

        positions = []
        limit = chess.engine.Limit(time=movetime_ms / 1000.0)

        while not board.is_game_over(claim_draw=True):
            if stop_event.is_set():
                break
            engine = white if board.turn == chess.WHITE else black
            fen = board.fen()
            ply = board.ply()
            result = engine.play(board, limit, info=chess.engine.Info.ALL)
            if result.move is None:
                break
            info = result.info or {}
            score_cp = score_to_cp(info.get("score"), board.turn == chess.WHITE)

            positions.append({
                "game_id": game_id,
                "ply": ply,
                "stm": "w" if board.turn == chess.WHITE else "b",
                "fen": fen,
                "move": result.move.uci(),
                "score_cp": score_cp,
                "depth": info.get("depth"),
                "nodes": info.get("nodes"),
                "time_ms": int((info.get("time") or 0) * 1000),
            })
            board.push(result.move)
            node = node.add_variation(result.move)

        result_str = board.result(claim_draw=True)
        game.headers["Result"] = result_str
        for p in positions:
            p["result"] = result_str

        # Write PGN per game (cheap, easy to inspect / prune).
        pgn_path = os.path.join(output_dir, "pgn", f"game_{game_id:06d}.pgn")
        with open(pgn_path, "w") as f:
            print(game, file=f)

        # Append every position to a single JSONL file under lock.
        jsonl_path = os.path.join(output_dir, "positions.jsonl")
        with jsonl_lock:
            with open(jsonl_path, "a") as f:
                for p in positions:
                    f.write(json.dumps(p) + "\n")

        with counter_lock:
            games_completed += 1
            plies_total += len(positions)
        return (game_id, result_str, len(positions))
    finally:
        try: white.quit()
        except Exception: pass
        try: black.quit()
        except Exception: pass


def progress_printer(total: int, start: float):
    while not stop_event.is_set():
        with counter_lock:
            done = games_completed
            plies = plies_total
        elapsed = time.time() - start
        gps = done / elapsed if elapsed > 0 else 0
        eta = (total - done) / gps if gps > 0 else 0
        sys.stdout.write(
            f"\r[{done}/{total}] games  |  {plies} positions  |  "
            f"{gps:.2f} games/s  |  ETA {int(eta)}s    "
        )
        sys.stdout.flush()
        if done >= total:
            break
        time.sleep(1)
    print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default=DEFAULT_ENGINE)
    ap.add_argument("--output", default=DEFAULT_OUTPUT)
    ap.add_argument("--games", type=int, default=200)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--movetime", type=int, default=300, help="ms per move")
    ap.add_argument("--hash", type=int, default=64, help="hash MB per engine")
    ap.add_argument("--threads", type=int, default=1, help="threads per engine")
    args = ap.parse_args()

    if not os.access(args.engine, os.X_OK):
        sys.exit(f"engine not executable: {args.engine}  (did you run `make`?)")

    os.makedirs(os.path.join(args.output, "pgn"), exist_ok=True)

    def handle_sigint(*_):
        if not stop_event.is_set():
            print("\nstopping after current games finish... (Ctrl-C again to force)")
            stop_event.set()
        else:
            os._exit(1)
    signal.signal(signal.SIGINT, handle_sigint)

    print(f"Self-play: {args.games} games, {args.workers} workers, {args.movetime}ms/move")
    print(f"Output: {args.output}")
    print(f"Engine: {args.engine}")
    print()

    start = time.time()
    printer = threading.Thread(target=progress_printer, args=(args.games, start), daemon=True)
    printer.start()

    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
            futures = [
                ex.submit(play_one_game, args.engine, args.movetime, i,
                          args.output, args.hash, args.threads)
                for i in range(args.games)
            ]
            for f in concurrent.futures.as_completed(futures):
                exc = f.exception()
                if exc is not None:
                    print(f"\ngame error: {exc!r}")
                if stop_event.is_set():
                    for fut in futures:
                        fut.cancel()
                    break
    finally:
        stop_event.set()
        printer.join(timeout=2)
        elapsed = time.time() - start
        print()
        print(f"Done. {games_completed} games, {plies_total} positions in {elapsed:.0f}s")
        print(f"PGN: {args.output}/pgn/")
        print(f"JSONL: {args.output}/positions.jsonl")


if __name__ == "__main__":
    main()
