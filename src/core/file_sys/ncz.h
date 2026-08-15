// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "common/common_types.h"
#include "core/file_sys/vfs/vfs_types.h"

namespace FileSys {

/// An NSZ is an ordinary PFS0 whose large NCAs were replaced by NCZs, so nothing but this format
/// stands between a compressed dump and a playable one. An NCZ keeps the NCA's first 0x4000 bytes
/// verbatim and stores the rest decrypted and Zstandard-compressed, section by section; turning it
/// back into an NCA means decompressing and then re-encrypting each section with the key and
/// counter recorded for it.
///
/// This is deliberately a one-shot conversion rather than a layer to read a game through. The
/// stream is compressed front to back, so reading it in the order a running title asks for would
/// mean decompressing from the beginning every time.

/// Whether the file carries the NCZ section header where an NCA would have its body.
[[nodiscard]] bool IsNcz(const VirtualFile& file);

/// The size the NCA will have once rebuilt, read from the section table rather than by unpacking,
/// so a container can be laid out before any of it is decompressed.
[[nodiscard]] std::optional<u64> NczDecompressedSize(const VirtualFile& ncz);

/// Whether the file is a PFS0 holding at least one NCZ -- an NSZ rather than a plain NSP.
[[nodiscard]] bool IsNsz(const VirtualFile& file);

/// What a compressed dump can say about itself without being unpacked. A packer leaves the small
/// archives alone and only squeezes the program, so the control data -- the name and the icon the
/// library shows -- is still readable in place.
struct NszPresentation {
    std::string title;
    std::vector<u8> icon;
};

/// Reads the name and icon out of a dump without unpacking it. Empty fields where the dump keeps
/// its control archive compressed too, or where the keys to open it are missing.
[[nodiscard]] std::optional<NszPresentation> ReadNszPresentation(const VirtualFile& nsz);

/// Rebuilds an NSZ as an ordinary NSP: entries that are NCZs are unpacked and renamed back to
/// .nca, everything else is copied across untouched. `progress` is called with bytes written and
/// the total expected, for a screen that wants to show it. Returns false on the first failure,
/// leaving `out` incomplete.
bool ConvertNszToNsp(const VirtualFile& nsz, const VirtualFile& out,
                     const std::function<void(u64, u64)>& progress = {});

/// Writes the reconstructed NCA into `out` and returns its SHA-256, which an NSZ names its files
/// by: the first 32 characters of an NCZ's filename are the first 16 bytes of this hash, so the
/// result can be checked without trusting this code. Returns nullopt if the file is not an NCZ or
/// is truncated.
[[nodiscard]] std::optional<std::array<u8, 0x20>> DecompressNcz(const VirtualFile& ncz,
                                                                const VirtualFile& out,
                                                                u64 out_offset = 0);

} // namespace FileSys
