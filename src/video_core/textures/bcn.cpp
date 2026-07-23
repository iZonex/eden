// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stb_dxt.h>
#include <string.h>
#include "common/alignment.h"
#include "video_core/textures/bcn.h"

#include <algorithm>
#include <cstring>
#include <cmath>

#include "video_core/textures/workers.h"

namespace Tegra::Texture::BCN {

using BCNCompressor = void(u8* block_output, const u8* block_input, bool any_alpha);

template <u32 BytesPerBlock, bool ThresholdAlpha = false>
void CompressBCN(std::span<const uint8_t> data, uint32_t width, uint32_t height, uint32_t depth,
                 std::span<uint8_t> output, BCNCompressor f) {
    constexpr u8 alpha_threshold = 128;
    constexpr u32 bytes_per_px = 4;
    const u32 plane_dim = width * height;

    Common::ThreadWorker& workers{GetThreadWorkers()};

    for (u32 z = 0; z < depth; z++) {
        for (u32 y = 0; y < height; y += 4) {
            auto compress_row = [z, y, width, height, plane_dim, f, data, output]() {
                for (u32 x = 0; x < width; x += 4) {
                    // Gather 4x4 block of RGBA texels
                    u8 input_colors[4][4][4];
                    bool any_alpha = false;

                    for (u32 j = 0; j < 4; j++) {
                        for (u32 i = 0; i < 4; i++) {
                            const size_t coord =
                                (z * plane_dim + (y + j) * width + (x + i)) * bytes_per_px;

                            if ((x + i < width) && (y + j < height)) {
                                if constexpr (ThresholdAlpha) {
                                    if (data[coord + 3] >= alpha_threshold) {
                                        input_colors[j][i][0] = data[coord + 0];
                                        input_colors[j][i][1] = data[coord + 1];
                                        input_colors[j][i][2] = data[coord + 2];
                                        input_colors[j][i][3] = 255;
                                    } else {
                                        any_alpha = true;
                                        memset(input_colors[j][i], 0, bytes_per_px);
                                    }
                                } else {
                                    memcpy(input_colors[j][i], &data[coord], bytes_per_px);
                                }
                            } else {
                                memset(input_colors[j][i], 0, bytes_per_px);
                            }
                        }
                    }

                    const u32 bytes_per_row = BytesPerBlock * Common::DivideUp(width, 4U);
                    const u32 bytes_per_plane = bytes_per_row * Common::DivideUp(height, 4U);
                    f(output.data() + z * bytes_per_plane + (y / 4) * bytes_per_row +
                          (x / 4) * BytesPerBlock,
                      reinterpret_cast<u8*>(input_colors), any_alpha);
                }
            };
            workers.QueueWork(std::move(compress_row));
        }
        workers.WaitForRequests();
    }
}

void CompressBC1(std::span<const uint8_t> data, uint32_t width, uint32_t height, uint32_t depth,
                 std::span<uint8_t> output) {
    CompressBCN<8, true>(data, width, height, depth, output,
                         [](u8* block_output, const u8* block_input, bool any_alpha) {
                             stb_compress_bc1_block(block_output, block_input, any_alpha,
                                                    STB_DXT_NORMAL);
                         });
}

void CompressBC3(std::span<const uint8_t> data, uint32_t width, uint32_t height, uint32_t depth,
                 std::span<uint8_t> output) {
    CompressBCN<16, false>(data, width, height, depth, output,
                           [](u8* block_output, const u8* block_input, bool any_alpha) {
                               stb_compress_bc3_block(block_output, block_input, STB_DXT_NORMAL);
                           });
}

// Very rough translation of the compose shader, probably worth finding a library similiar to stb_dxt that supports BC7 mode 5
// I'd recommend https://github.com/BinomialLLC/basis_universal but whoever handles CPM can add it
void CompressBC7(std::span<const uint8_t> data, uint32_t width, uint32_t height, uint32_t depth,
                 std::span<uint8_t> output) {
    CompressBCN<16, false>(data, width, height, depth, output,
        [](u8* block_output, const u8* block_input, bool any_alpha) {
            float texel_r[16], texel_g[16], texel_b[16], texel_a[16];
            float mean_r = 0, mean_g = 0, mean_b = 0;

            for (int i = 0; i < 16; ++i) {
                texel_r[i] = block_input[i * 4 + 0] / 255.0f;
                texel_g[i] = block_input[i * 4 + 1] / 255.0f;
                texel_b[i] = block_input[i * 4 + 2] / 255.0f;
                texel_a[i] = block_input[i * 4 + 3] / 255.0f;
                mean_r += texel_r[i];
                mean_g += texel_g[i];
                mean_b += texel_b[i];
            }
            mean_r /= 16.0f; mean_g /= 16.0f; mean_b /= 16.0f;

            float c00 = 0, c01 = 0, c02 = 0;
            float c11 = 0, c12 = 0, c22 = 0;
            for (int i = 0; i < 16; ++i) {
                float dr = texel_r[i] - mean_r;
                float dg = texel_g[i] - mean_g;
                float db = texel_b[i] - mean_b;
                c00 += dr * dr; c01 += dr * dg; c02 += dr * db;
                c11 += dg * dg; c12 += dg * db; c22 += db * db;
            }

            float axis_r = 1.0f, axis_g = 1.0f, axis_b = 1.0f;
            for (int iter = 0; iter < 4; ++iter) {
                float nx = c00 * axis_r + c01 * axis_g + c02 * axis_b;
                float ny = c01 * axis_r + c11 * axis_g + c12 * axis_b;
                float nz = c02 * axis_r + c12 * axis_g + c22 * axis_b;
                axis_r = nx; axis_g = ny; axis_b = nz;
                float len_sq = axis_r * axis_r + axis_g * axis_g + axis_b * axis_b;
                if (len_sq > 1e-12f) {
                    float inv_len = 1.0f / std::sqrt(len_sq);
                    axis_r *= inv_len; axis_g *= inv_len; axis_b *= inv_len;
                } else {
                    axis_r = 1.0f; axis_g = 0.0f; axis_b = 0.0f;
                }
            }

            float min_proj_rgb = 1e9f, max_proj_rgb = -1e9f;
            for (int i = 0; i < 16; ++i) {
                float proj = (texel_r[i] - mean_r) * axis_r +
                             (texel_g[i] - mean_g) * axis_g +
                             (texel_b[i] - mean_b) * axis_b;
                min_proj_rgb = std::min(min_proj_rgb, proj);
                max_proj_rgb = std::max(max_proj_rgb, proj);
            }

            float e0_r = mean_r + axis_r * max_proj_rgb;
            float e0_g = mean_g + axis_g * max_proj_rgb;
            float e0_b = mean_b + axis_b * max_proj_rgb;
            float e1_r = mean_r + axis_r * min_proj_rgb;
            float e1_g = mean_g + axis_g * min_proj_rgb;
            float e1_b = mean_b + axis_b * min_proj_rgb;

            auto quantize7 = [](float v) { return (uint32_t)std::clamp((int)std::round(v * 127.0f), 0, 127); };
            auto recon7 = [](uint32_t v) { return static_cast<float>((v << 1) | (v >> 6)) / 255.0f; };
            auto quantize8 = [](float v) { return (uint32_t)std::clamp((int)std::round(v * 255.0f), 0, 255); };
            auto recon8 = [](uint32_t v) { return static_cast<float>(v) / 255.0f; };

            uint32_t e0c[3] = {quantize7(e0_r), quantize7(e0_g), quantize7(e0_b)};
            uint32_t e1c[3] = {quantize7(e1_r), quantize7(e1_g), quantize7(e1_b)};

            float rc0[3] = {recon7(e0c[0]), recon7(e0c[1]), recon7(e0c[2])};
            float rc1[3] = {recon7(e1c[0]), recon7(e1c[1]), recon7(e1c[2])};

            constexpr float BC7_WEIGHTS2[4] = {0.0f, 21.0f / 64.0f, 43.0f / 64.0f, 64.0f / 64.0f};

            uint32_t rgb_indices = 0;
            for (int i = 0; i < 16; ++i) {
                float best_d = 1e9f;
                uint32_t best_idx = 0;
                for (uint32_t w = 0; w < 4; ++w) {
                    float mix_w = BC7_WEIGHTS2[w];
                    float mr = rc0[0] * (1.0f - mix_w) + rc1[0] * mix_w;
                    float mg = rc0[1] * (1.0f - mix_w) + rc1[1] * mix_w;
                    float mb = rc0[2] * (1.0f - mix_w) + rc1[2] * mix_w;
                    float dr = texel_r[i] - mr, dg = texel_g[i] - mg, db = texel_b[i] - mb;
                    float d2 = dr * dr + dg * dg + db * db;
                    if (d2 < best_d) { best_d = d2; best_idx = w; }
                }
                rgb_indices |= (best_idx << (i * 2));
            }

            float min_a = 1e9f, max_a = -1e9f;
            for (int i = 0; i < 16; ++i) {
                min_a = std::min(min_a, texel_a[i]);
                max_a = std::max(max_a, texel_a[i]);
            }

            uint32_t a0c = quantize8(max_a);
            uint32_t a1c = quantize8(min_a);
            float ra0 = recon8(a0c);
            float ra1 = recon8(a1c);

            uint32_t a_indices = 0;
            for (int i = 0; i < 16; ++i) {
                float best_d = 1e9f;
                uint32_t best_idx = 0;
                for (uint32_t w = 0; w < 4; ++w) {
                    float mix_w = BC7_WEIGHTS2[w];
                    float ma = ra0 * (1.0f - mix_w) + ra1 * mix_w;
                    float da = std::abs(texel_a[i] - ma);
                    if (da < best_d) { best_d = da; best_idx = w; }
                }
                a_indices |= (best_idx << (i * 2));
            }

            if ((rgb_indices & 3) >= 2) {
                std::swap(e0c[0], e1c[0]); std::swap(e0c[1], e1c[1]); std::swap(e0c[2], e1c[2]);
                rgb_indices = (~rgb_indices);
            }
            if ((a_indices & 3) >= 2) {
                std::swap(a0c, a1c);
                a_indices = (~a_indices);
            }

            uint64_t out[2] = {0, 0};
            uint32_t bit_pos = 0;
            auto put_bits = [&](uint32_t value, uint32_t bit_width) {
                uint64_t v = value & ((1ULL << bit_width) - 1);
                if (bit_pos < 64) {
                    out[0] |= (v << bit_pos);
                    if (bit_pos + bit_width > 64) {
                        out[1] |= (v >> (64 - bit_pos));
                    }
                } else {
                    out[1] |= (v << (bit_pos - 64));
                }
                bit_pos += bit_width;
            };

            put_bits(1 << 5, 6);
            put_bits(0, 2);

            put_bits(e0c[0], 7); put_bits(e1c[0], 7);
            put_bits(e0c[1], 7); put_bits(e1c[1], 7);
            put_bits(e0c[2], 7); put_bits(e1c[2], 7);

            put_bits(a0c, 8); put_bits(a1c, 8);

            put_bits(rgb_indices & 1, 1);
            put_bits((rgb_indices >> 2) & 0x3FFFFFFF, 30);

            put_bits(a_indices & 1, 1);
            put_bits((a_indices >> 2) & 0x3FFFFFFF, 30);

            std::memcpy(block_output, out, 16);
        });
}

} // namespace Tegra::Texture::BCN
