"""Streams (features, target) tensors from self_play positions.jsonl.

Feature encoding (769-dim binary vector — simple but effective for a first NN eval):
    bits  0.. 63: white pawns by square
    bits 64..127: white knights
    ... (12 piece types total, 64 bits each)
    bit   768:    side-to-move (1 if white, 0 if black)

Target: tanh(score_cp / 400) ∈ [-1, 1], blended with game result.
    target = 0.7 * tanh(cp/400) + 0.3 * result_from_stm

    where result_from_stm is +1 if the side-to-move eventually won, -1 if lost, 0 if draw.

Positions with null score (book moves) are dropped unless we have a game result.
"""
import json
import math
import random
import torch
from torch.utils.data import IterableDataset, get_worker_info

# 64 squares × (color + piece_type tuple). Index into the 768-dim feature block.
# piece_to_index: W_PAWN=0, W_KNIGHT=1, ..., W_KING=5, B_PAWN=6, ..., B_KING=11
PIECE_TO_IDX = {
    "P": 0, "N": 1, "B": 2, "R": 3, "Q": 4, "K": 5,
    "p": 6, "n": 7, "b": 8, "r": 9, "q": 10, "k": 11,
}
FEAT_DIM = 12 * 64 + 1  # 769


def fen_to_features(fen: str) -> torch.Tensor:
    """FEN -> 769-dim binary feature vector."""
    parts = fen.split()
    board_str = parts[0]
    stm = parts[1]  # "w" or "b"

    feats = torch.zeros(FEAT_DIM, dtype=torch.float32)
    rank = 7
    file = 0
    for c in board_str:
        if c == "/":
            rank -= 1
            file = 0
        elif c.isdigit():
            file += int(c)
        else:
            sq = rank * 8 + file
            idx = PIECE_TO_IDX[c] * 64 + sq
            feats[idx] = 1.0
            file += 1
    if stm == "w":
        feats[768] = 1.0
    return feats


def target_from_row(row: dict) -> float | None:
    """Compute a training target from a JSONL row. Returns None to skip."""
    cp = row.get("score_cp")
    result = row.get("result", "*")
    stm = row.get("stm", "w")

    # Map PGN result to the side-to-move's perspective.
    if result == "1-0":
        r = 1.0 if stm == "w" else -1.0
    elif result == "0-1":
        r = -1.0 if stm == "w" else 1.0
    elif result == "1/2-1/2":
        r = 0.0
    else:
        r = None

    if cp is None and r is None:
        return None
    if cp is None:
        return r
    # Convert cp (from stm's perspective) to [-1,1].
    cp_part = math.tanh(max(-1500, min(1500, int(cp))) / 400.0)
    if r is None:
        return cp_part
    return 0.7 * cp_part + 0.3 * r


class PositionsDataset(IterableDataset):
    def __init__(self, jsonl_path: str, shuffle_buffer: int = 8192, seed: int = 0):
        super().__init__()
        self.jsonl_path = jsonl_path
        self.shuffle_buffer = shuffle_buffer
        self.seed = seed

    def __iter__(self):
        info = get_worker_info()
        worker_id = 0 if info is None else info.id
        num_workers = 1 if info is None else info.num_workers
        rng = random.Random(self.seed + worker_id)
        buf: list[tuple[torch.Tensor, float]] = []

        def flush_one():
            if not buf:
                return None
            i = rng.randrange(len(buf))
            buf[i], buf[-1] = buf[-1], buf[i]
            return buf.pop()

        with open(self.jsonl_path, "r") as f:
            for line_no, line in enumerate(f):
                if line_no % num_workers != worker_id:
                    continue
                row = json.loads(line)
                t = target_from_row(row)
                if t is None:
                    continue
                feats = fen_to_features(row["fen"])
                buf.append((feats, t))
                if len(buf) >= self.shuffle_buffer:
                    yield flush_one()
        while buf:
            yield flush_one()


def collate(batch):
    feats = torch.stack([b[0] for b in batch])
    targets = torch.tensor([b[1] for b in batch], dtype=torch.float32).unsqueeze(1)
    return feats, targets
