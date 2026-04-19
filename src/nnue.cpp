#include "nnue.h"
#include "bitboard.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <vector>

namespace NNUE {

namespace {

constexpr uint32_t MAGIC   = 0x4E4E5545;  // "NNUE" little-endian
constexpr int FEAT_DIM     = 769;

// Dequantized weights (stored as float for simple inference).
struct Net {
    int in_dim   = 0;
    int h1       = 0;
    int h2       = 0;
    int out_dim  = 0;
    float scale  = 0.0f;

    // fc1: w1[h1 * in_dim], b1[h1]
    std::vector<float> w1, b1;
    // fc2: w2[h2 * h1], b2[h2]
    std::vector<float> w2, b2;
    // fc3: w3[out_dim * h2], b3[out_dim]
    std::vector<float> w3, b3;
};

Net g_net;
bool g_loaded = false;

template<typename T>
bool read(std::ifstream& f, T* dst, size_t n = 1) {
    f.read(reinterpret_cast<char*>(dst), sizeof(T) * n);
    return f.gcount() == (std::streamsize)(sizeof(T) * n);
}

inline float clamped_relu(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

// Map (piece, square) -> feature index.
inline int feature_index(Piece p, Square s) {
    // White P=0, N=1, ..., K=5; Black P=6, ..., K=11.
    int color = color_of(p);
    int pt    = type_of(p) - 1;        // PAWN=1 -> 0
    return (color * 6 + pt) * 64 + s;
}

} // namespace

bool is_loaded() { return g_loaded; }

bool load(const std::string& path) {
    g_loaded = false;
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::printf("info string NNUE load: cannot open %s\n", path.c_str()); return false; }

    uint32_t magic, version;
    if (!read(f, &magic) || !read(f, &version) || magic != MAGIC) {
        std::printf("info string NNUE load: bad magic/version\n");
        return false;
    }

    uint32_t in_dim, h1, h2, out_dim;
    if (!read(f, &in_dim) || !read(f, &h1) || !read(f, &h2) || !read(f, &out_dim)) return false;
    if ((int)in_dim != FEAT_DIM || out_dim != 1) {
        std::printf("info string NNUE load: unexpected dims %u/%u/%u/%u\n",
                    in_dim, h1, h2, out_dim);
        return false;
    }

    g_net.in_dim = in_dim; g_net.h1 = h1; g_net.h2 = h2; g_net.out_dim = out_dim;

    auto read_int16_as_float = [&](std::vector<float>& out, size_t n) {
        std::vector<int16_t> raw(n);
        if (!read(f, raw.data(), n)) return false;
        out.resize(n);
        for (size_t i = 0; i < n; ++i) out[i] = raw[i];
        return true;
    };
    auto read_float = [&](std::vector<float>& out, size_t n) {
        out.resize(n);
        return read(f, out.data(), n);
    };

    if (!read_int16_as_float(g_net.w1, (size_t)h1 * in_dim)) return false;
    if (!read_float(g_net.b1, h1)) return false;
    if (!read_int16_as_float(g_net.w2, (size_t)h2 * h1)) return false;
    if (!read_float(g_net.b2, h2)) return false;
    if (!read_int16_as_float(g_net.w3, (size_t)out_dim * h2)) return false;
    if (!read_float(g_net.b3, out_dim)) return false;

    int32_t scale_i;
    if (!read(f, &scale_i)) return false;
    g_net.scale = (float)scale_i;

    // Dequantize weights by dividing by scale (applied once at load).
    for (auto& v : g_net.w1) v /= g_net.scale;
    for (auto& v : g_net.w2) v /= g_net.scale;
    for (auto& v : g_net.w3) v /= g_net.scale;

    g_loaded = true;
    std::printf("info string NNUE loaded: %d-%d-%d-%d (%zu KB)\n",
                in_dim, h1, h2, out_dim,
                (g_net.w1.size() + g_net.w2.size() + g_net.w3.size()) * sizeof(float) / 1024);
    std::fflush(stdout);
    return true;
}

int evaluate(const Position& pos) {
    if (!g_loaded) return INT32_MIN;

    // Layer 1 accumulator: bias + sum of w1 columns for each active feature.
    std::vector<float> h(g_net.h1);
    std::copy(g_net.b1.begin(), g_net.b1.end(), h.begin());

    // Side-to-move feature.
    if (pos.side_to_move() == WHITE) {
        for (int j = 0; j < g_net.h1; ++j)
            h[j] += g_net.w1[j * g_net.in_dim + 768];
    }

    // Piece features: iterate all pieces.
    Bitboard occ = pos.pieces();
    while (occ) {
        Square s = BB::pop_lsb(occ);
        int feat = feature_index(pos.piece_on(s), s);
        int col_base = feat;
        for (int j = 0; j < g_net.h1; ++j)
            h[j] += g_net.w1[j * g_net.in_dim + col_base];
    }
    for (auto& v : h) v = clamped_relu(v);

    std::vector<float> h2(g_net.h2);
    std::copy(g_net.b2.begin(), g_net.b2.end(), h2.begin());
    for (int j = 0; j < g_net.h2; ++j) {
        float sum = h2[j];
        const float* row = &g_net.w2[j * g_net.h1];
        for (int i = 0; i < g_net.h1; ++i) sum += row[i] * h[i];
        h2[j] = clamped_relu(sum);
    }

    float out = g_net.b3[0];
    for (int i = 0; i < g_net.h2; ++i) out += g_net.w3[i] * h2[i];
    out = std::tanh(out);

    // Scale [-1, 1] to centipawns. 400 cp = ~1 pawn unit gap → output near ±1 means decisive.
    int cp = (int)std::round(out * 600.0f);
    return cp;
}

} // namespace NNUE
