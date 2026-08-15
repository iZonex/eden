// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Checks the NCZ reader against the only oracle the format offers: an NCZ is named after the
// SHA-256 of the NCA it came from, so a correct conversion reproduces its own filename.

#include <cstdio>
#include <filesystem>
#include <string>

#include <string_view>

#include "core/file_sys/ncz.h"
#include "core/file_sys/vfs/vfs_real.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::puts("usage: ncz-check <in.ncz> <out.nca>");
        return 2;
    }
    FileSys::RealVfsFilesystem vfs;
    const auto in = vfs.OpenFile(argv[1], FileSys::OpenMode::Read);
    if (in == nullptr) {
        std::puts("cannot open input");
        return 2;
    }
    const auto out = vfs.CreateFile(argv[2], FileSys::OpenMode::ReadWrite);
    if (out == nullptr) {
        std::puts("cannot create output");
        return 2;
    }
    if (std::string_view{argv[1]}.ends_with(".nsz")) {
        std::printf("IsNsz: %s\n", FileSys::IsNsz(in) ? "yes" : "no");
        const bool ok = FileSys::ConvertNszToNsp(in, out, [](u64 done, u64 total) {
            std::printf("  %llu / %llu\n", static_cast<unsigned long long>(done),
                        static_cast<unsigned long long>(total));
        });
        std::printf("%s\n", ok ? "CONVERTED (every NCA matched its own hash)" : "FAILED");
        return ok ? 0 : 1;
    }
    std::printf("IsNcz: %s\n", FileSys::IsNcz(in) ? "yes" : "no");
    const auto hash = FileSys::DecompressNcz(in, out);
    if (!hash) {
        std::puts("FAILED: decompression returned nothing");
        return 1;
    }
    std::string hex;
    for (const auto byte : *hash) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", byte);
        hex += buf;
    }
    const std::string expected = std::filesystem::path(argv[1]).stem().string();
    std::printf("expected (from filename): %s\n", expected.c_str());
    std::printf("computed (first 32):      %s\n", hex.substr(0, 32).c_str());
    const bool ok = hex.substr(0, 32) == expected;
    std::printf("%s\n", ok ? "MATCH" : "MISMATCH");
    return ok ? 0 : 1;
}
