"""Train the small NNUE on self-play JSONL.

Usage:
    python3 training/train.py --data self_play/positions.jsonl --epochs 10

Writes:
    training/checkpoint.pt   — PyTorch weights
    training/nnue.bin        — int16-quantized weights for the C++ engine
"""
import argparse
import math
import os
import struct
import sys
import time
import torch
import torch.nn as nn
from torch.utils.data import DataLoader

sys.path.insert(0, os.path.dirname(__file__))
from dataset import PositionsDataset, collate, FEAT_DIM
from model import SmallNNUE


def count_lines(path: str) -> int:
    n = 0
    with open(path, "rb") as f:
        for _ in f: n += 1
    return n


def quantize_and_dump(model: SmallNNUE, out_path: str, scale: int = 256):
    """Serialize weights as int16 (weights) + float32 biases for C++ consumption.

    File layout (little-endian):
        uint32 magic = 0x4E4E5545  # "NNUE"
        uint32 version = 1
        uint32 in_dim, h1, h2, out_dim
        int16  fc1_w[h1 * in_dim]           (scaled by `scale`)
        float  fc1_b[h1]
        int16  fc2_w[h2 * h1]
        float  fc2_b[h2]
        int16  fc3_w[h2]
        float  fc3_b
        int32  weight_scale
    """
    with torch.no_grad():
        w1 = (model.fc1.weight * scale).round().clamp(-32767, 32767).to(torch.int16).cpu().numpy()
        b1 = model.fc1.bias.cpu().numpy().astype("float32")
        w2 = (model.fc2.weight * scale).round().clamp(-32767, 32767).to(torch.int16).cpu().numpy()
        b2 = model.fc2.bias.cpu().numpy().astype("float32")
        w3 = (model.fc3.weight * scale).round().clamp(-32767, 32767).to(torch.int16).cpu().numpy()
        b3 = model.fc3.bias.cpu().numpy().astype("float32")

    with open(out_path, "wb") as f:
        f.write(struct.pack("<II", 0x4E4E5545, 1))
        f.write(struct.pack("<IIII", model.fc1.in_features, model.fc1.out_features,
                            model.fc2.out_features, 1))
        f.write(w1.tobytes())
        f.write(b1.tobytes())
        f.write(w2.tobytes())
        f.write(b2.tobytes())
        f.write(w3.tobytes())
        f.write(b3.tobytes())
        f.write(struct.pack("<i", scale))
    print(f"wrote {out_path} ({os.path.getsize(out_path)/1024:.1f} KB)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="self_play/positions.jsonl")
    ap.add_argument("--epochs", type=int, default=10)
    ap.add_argument("--batch", type=int, default=4096)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--h1", type=int, default=256)
    ap.add_argument("--h2", type=int, default=32)
    ap.add_argument("--out", default="training")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    device = "mps" if torch.backends.mps.is_available() else "cpu"
    print(f"device: {device}")

    total_lines = count_lines(args.data)
    print(f"dataset: {total_lines} raw lines in {args.data}")

    model = SmallNNUE(in_dim=FEAT_DIM, h1=args.h1, h2=args.h2).to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs)
    loss_fn = nn.MSELoss()

    for epoch in range(args.epochs):
        t0 = time.time()
        ds = PositionsDataset(args.data, shuffle_buffer=16384, seed=epoch)
        dl = DataLoader(ds, batch_size=args.batch, collate_fn=collate,
                        num_workers=args.workers, pin_memory=(device != "cpu"))
        running_loss = 0.0
        running_n = 0
        model.train()
        for feats, targets in dl:
            feats = feats.to(device, non_blocking=True)
            targets = targets.to(device, non_blocking=True)
            opt.zero_grad()
            pred = model(feats)
            loss = loss_fn(pred, targets)
            loss.backward()
            opt.step()
            running_loss += loss.item() * feats.size(0)
            running_n += feats.size(0)
            if running_n % (args.batch * 50) == 0:
                print(f"  epoch {epoch} step {running_n//args.batch}: loss={running_loss/running_n:.4f}")
        sched.step()
        print(f"epoch {epoch} done: avg_loss={running_loss/max(1,running_n):.4f}  "
              f"positions_seen={running_n}  time={time.time()-t0:.1f}s")

        # Save checkpoint per epoch (so interruption doesn't lose progress).
        torch.save(model.state_dict(), os.path.join(args.out, "checkpoint.pt"))
        quantize_and_dump(model, os.path.join(args.out, "nnue.bin"))

    print("done.")


if __name__ == "__main__":
    main()
