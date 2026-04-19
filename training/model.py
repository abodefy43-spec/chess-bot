"""Small fully-connected net for position evaluation.

Architecture:
    Input 769 (binary, FEN-encoded)
    -> Linear 769x256, bias, clamped ReLU
    -> Linear 256x32,  bias, clamped ReLU
    -> Linear 32x1,   bias, tanh

Output: predicted evaluation in [-1, 1] (same scale as the dataset target).

Clamped ReLU (0..1) matches the Stockfish NNUE activation and makes later
int8/int16 quantization straightforward: every intermediate activation
already fits in a fixed range.
"""
import torch
import torch.nn as nn


class ClampedReLU(nn.Module):
    def forward(self, x):
        return torch.clamp(x, min=0.0, max=1.0)


class SmallNNUE(nn.Module):
    def __init__(self, in_dim: int = 769, h1: int = 256, h2: int = 32):
        super().__init__()
        self.fc1 = nn.Linear(in_dim, h1)
        self.fc2 = nn.Linear(h1, h2)
        self.fc3 = nn.Linear(h2, 1)
        self.act = ClampedReLU()

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.act(self.fc1(x))
        x = self.act(self.fc2(x))
        return torch.tanh(self.fc3(x))
