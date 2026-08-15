// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <vector>

#include <openssl/evp.h>
#include <zstd.h>

#include "common/logging.h"
#include "core/crypto/aes_util.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/ncz.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/partition_filesystem.h"
#include "core/file_sys/romfs.h"
#include "core/loader/loader.h"
#include "core/file_sys/vfs/vfs.h"

namespace FileSys {

namespace {

constexpr u64 NCA_HEADER_SIZE = 0x4000;
constexpr u64 CHUNK_SIZE = 0x400000;

constexpr std::array<u8, 8> SECTION_MAGIC{'N', 'C', 'Z', 'S', 'E', 'C', 'T', 'N'};
constexpr std::array<u8, 8> BLOCK_MAGIC{'N', 'C', 'Z', 'B', 'L', 'O', 'C', 'K'};

/// One entry of the section table that follows the NCZSECTN magic.
struct NczSection {
    u64 offset;
    u64 size;
    u64 crypto_type;
    u64 padding;
    std::array<u8, 0x10> key;
    std::array<u8, 0x10> counter;
};
static_assert(sizeof(NczSection) == 0x40, "NczSection has the wrong layout");

/// Header of the optional block-compressed variant, where the stream is cut into independently
/// compressed blocks so it can be read out of order.
struct NczBlockHeader {
    std::array<u8, 8> magic;
    u8 version;
    u8 type;
    u8 unused;
    u8 block_size_exponent;
    u32 block_count;
    u64 decompressed_size;
};
static_assert(sizeof(NczBlockHeader) == 0x18, "NczBlockHeader has the wrong layout");

/// Only these two mean the section was stored decrypted and has to be put back under its key.
bool IsEncrypted(u64 crypto_type) {
    return crypto_type == 3 || crypto_type == 4;
}

/// An NCA section's counter is the recorded nonce in the top half and the position, counted in
/// 16-byte blocks, big-endian in the bottom half. Seeking is therefore just recomputing it.
void SeekCounter(Core::Crypto::AESCipher<Core::Crypto::Key128>& cipher, const NczSection& section,
                 u64 offset) {
    std::array<u8, 0x10> iv{};
    std::memcpy(iv.data(), section.counter.data(), 8);
    const u64 block = offset >> 4;
    for (std::size_t i = 0; i < 8; ++i) {
        iv[8 + i] = static_cast<u8>(block >> (56 - i * 8));
    }
    cipher.SetIV(iv);
}

/// Pulls the compressed stream out of the file a piece at a time so a multi-gigabyte NCA never has
/// to be held whole.
class ZstdStream {
public:
    explicit ZstdStream(const VirtualFile& file_, u64 start)
        : file{file_}, read_offset{start}, stream{ZSTD_createDStream()} {
        if (stream != nullptr) {
            ZSTD_initDStream(stream);
        }
        in_buffer.resize(ZSTD_DStreamInSize());
    }

    ~ZstdStream() {
        if (stream != nullptr) {
            ZSTD_freeDStream(stream);
        }
    }

    ZstdStream(const ZstdStream&) = delete;
    ZstdStream& operator=(const ZstdStream&) = delete;

    bool Valid() const {
        return stream != nullptr;
    }

    /// Fills `dest` with up to `size` decompressed bytes, returning how many arrived. A short read
    /// means the stream ended.
    std::size_t Read(u8* dest, std::size_t size) {
        ZSTD_outBuffer out{dest, size, 0};
        while (out.pos < out.size) {
            if (input.pos == input.size) {
                const std::size_t got = file->Read(in_buffer.data(), in_buffer.size(), read_offset);
                if (got == 0) {
                    break;
                }
                read_offset += got;
                input = ZSTD_inBuffer{in_buffer.data(), got, 0};
            }
            const std::size_t result = ZSTD_decompressStream(stream, &out, &input);
            if (ZSTD_isError(result)) {
                LOG_ERROR(Common_Filesystem, "NCZ: zstd failed: {}", ZSTD_getErrorName(result));
                return out.pos;
            }
            if (result == 0 && input.pos == input.size) {
                break; // frame finished and nothing buffered
            }
        }
        return out.pos;
    }

private:
    VirtualFile file;
    u64 read_offset;
    ZSTD_DStream* stream;
    std::vector<u8> in_buffer;
    ZSTD_inBuffer input{nullptr, 0, 0};
};

/// Writes a run of bytes that belongs to one section, encrypting it first when the section calls
/// for it, and folds it into the running hash.
bool EmitSectionRun(const VirtualFile& out, EVP_MD_CTX* sha, const NczSection& section,
                    u64 position, u8* data, std::size_t size, u64& written) {
    if (IsEncrypted(section.crypto_type)) {
        Core::Crypto::Key128 key{};
        std::memcpy(key.data(), section.key.data(), key.size());
        Core::Crypto::AESCipher<Core::Crypto::Key128> cipher{key, Core::Crypto::Mode::CTR};
        SeekCounter(cipher, section, position);
        cipher.Transcode(data, size, data, Core::Crypto::Op::Encrypt);
    }
    EVP_DigestUpdate(sha, data, size);
    if (out->Write(data, size, written) != size) {
        LOG_ERROR(Common_Filesystem, "NCZ: could not write {} bytes at {}", size, written);
        return false;
    }
    written += size;
    return true;
}

/// Reads just the section table, which is all that is needed to know how big the NCA will be.
std::optional<std::vector<NczSection>> ReadSections(const VirtualFile& ncz, u64& cursor) {
    cursor = NCA_HEADER_SIZE + SECTION_MAGIC.size();
    u64 count = 0;
    if (ncz->ReadObject(&count, cursor) != sizeof(count) || count == 0 || count > 0x1000) {
        return std::nullopt;
    }
    cursor += sizeof(count);
    std::vector<NczSection> sections(count);
    const std::size_t table_size = count * sizeof(NczSection);
    if (ncz->ReadBytes(sections.data(), table_size, cursor) != table_size) {
        return std::nullopt;
    }
    cursor += table_size;
    return sections;
}

} // Anonymous namespace

bool IsNcz(const VirtualFile& file) {
    if (file == nullptr || file->GetSize() < NCA_HEADER_SIZE + SECTION_MAGIC.size()) {
        return false;
    }
    std::array<u8, 8> magic{};
    if (file->Read(magic.data(), magic.size(), NCA_HEADER_SIZE) != magic.size()) {
        return false;
    }
    return magic == SECTION_MAGIC;
}

std::optional<std::array<u8, 0x20>> DecompressNcz(const VirtualFile& ncz, const VirtualFile& out,
                                                 u64 out_offset) {
    if (!IsNcz(ncz) || out == nullptr) {
        return std::nullopt;
    }

    EVP_MD_CTX* sha = EVP_MD_CTX_new();
    if (sha == nullptr) {
        return std::nullopt;
    }
    EVP_DigestInit_ex(sha, EVP_sha256(), nullptr);
    // Every early return below has to let the digest go, so tie it to the scope.
    struct DigestGuard {
        EVP_MD_CTX* ctx;
        ~DigestGuard() {
            EVP_MD_CTX_free(ctx);
        }
    } guard{sha};

    // The NCA header is not compressed and not encrypted any differently than it already was.
    std::vector<u8> header(NCA_HEADER_SIZE);
    if (ncz->Read(header.data(), header.size(), 0) != header.size()) {
        return std::nullopt;
    }
    EVP_DigestUpdate(sha, header.data(), header.size());
    if (out->Write(header.data(), header.size(), out_offset) != header.size()) {
        return std::nullopt;
    }
    u64 written = out_offset + NCA_HEADER_SIZE;

    u64 cursor = 0;
    const auto parsed = ReadSections(ncz, cursor);
    if (!parsed) {
        LOG_ERROR(Common_Filesystem, "NCZ: unreadable section table");
        return std::nullopt;
    }
    const std::vector<NczSection>& sections = *parsed;

    // Either the stream starts here, or a block index does.
    std::array<u8, 8> maybe_block{};
    ncz->Read(maybe_block.data(), maybe_block.size(), cursor);
    const bool block_compressed = maybe_block == BLOCK_MAGIC;

    if (block_compressed) {
        NczBlockHeader block{};
        if (ncz->ReadObject(&block, cursor) != sizeof(block)) {
            return std::nullopt;
        }
        cursor += sizeof(block);
        std::vector<u32> block_sizes(block.block_count);
        const std::size_t list_size = block.block_count * sizeof(u32);
        if (ncz->ReadBytes(block_sizes.data(), list_size, cursor) != list_size) {
            return std::nullopt;
        }
        cursor += list_size;

        const std::size_t block_size = static_cast<std::size_t>(1) << block.block_size_exponent;
        std::vector<u8> compressed(block_size);
        std::vector<u8> plain(block_size);
        u64 position = sections.front().offset;
        std::size_t section_id = 0;

        for (u32 i = 0; i < block.block_count; ++i) {
            const std::size_t stored = block_sizes[i];
            if (ncz->Read(compressed.data(), stored, cursor) != stored) {
                return std::nullopt;
            }
            cursor += stored;

            std::size_t plain_size = stored;
            if (stored < block_size) {
                // Short means it was worth compressing; equal means it was stored as it is.
                plain_size = ZSTD_decompress(plain.data(), plain.size(), compressed.data(), stored);
                if (ZSTD_isError(plain_size)) {
                    LOG_ERROR(Common_Filesystem, "NCZ: block {} failed: {}", i,
                              ZSTD_getErrorName(plain_size));
                    return std::nullopt;
                }
            } else {
                std::memcpy(plain.data(), compressed.data(), stored);
            }

            // A block can straddle a section boundary, and each side needs its own key.
            std::size_t consumed = 0;
            while (consumed < plain_size) {
                while (section_id + 1 < sections.size() &&
                       position >= sections[section_id].offset + sections[section_id].size) {
                    ++section_id;
                }
                const NczSection& section = sections[section_id];
                const u64 section_end = section.offset + section.size;
                const std::size_t run = static_cast<std::size_t>(
                    std::min<u64>(plain_size - consumed, section_end - position));
                if (run == 0) {
                    break;
                }
                if (!EmitSectionRun(out, sha, section, position, plain.data() + consumed, run,
                                    written)) {
                    return std::nullopt;
                }
                consumed += run;
                position += run;
            }
        }
    } else {
        ZstdStream stream{ncz, cursor};
        if (!stream.Valid()) {
            return std::nullopt;
        }
        std::vector<u8> buffer(CHUNK_SIZE);
        for (const NczSection& section : sections) {
            u64 position = section.offset;
            const u64 end = section.offset + section.size;
            while (position < end) {
                const std::size_t want =
                    static_cast<std::size_t>(std::min<u64>(CHUNK_SIZE, end - position));
                const std::size_t got = stream.Read(buffer.data(), want);
                if (got == 0) {
                    LOG_ERROR(Common_Filesystem, "NCZ: stream ended {} bytes early",
                              end - position);
                    return std::nullopt;
                }
                if (!EmitSectionRun(out, sha, section, position, buffer.data(), got, written)) {
                    return std::nullopt;
                }
                position += got;
            }
        }
    }

    std::array<u8, 0x20> digest{};
    unsigned int digest_size = 0;
    EVP_DigestFinal_ex(sha, digest.data(), &digest_size);
    return digest;
}

std::optional<u64> NczDecompressedSize(const VirtualFile& ncz) {
    if (!IsNcz(ncz)) {
        return std::nullopt;
    }
    u64 cursor = 0;
    const auto sections = ReadSections(ncz, cursor);
    if (!sections) {
        return std::nullopt;
    }
    u64 end = NCA_HEADER_SIZE;
    for (const NczSection& section : *sections) {
        end = std::max(end, section.offset + section.size);
    }
    return end;
}

bool IsNsz(const VirtualFile& file) {
    if (file == nullptr) {
        return false;
    }
    const PartitionFilesystem pfs{file};
    if (pfs.GetStatus() != Loader::ResultStatus::Success) {
        return false;
    }
    for (const VirtualFile& inner : pfs.GetFiles()) {
        if (inner->GetExtension() == "ncz") {
            return true;
        }
    }
    return false;
}

std::optional<NszPresentation> ReadNszPresentation(const VirtualFile& nsz) {
    if (nsz == nullptr) {
        return std::nullopt;
    }
    const PartitionFilesystem pfs{nsz};
    if (pfs.GetStatus() != Loader::ResultStatus::Success) {
        return std::nullopt;
    }
    NszPresentation out;

    // The metadata archive says what this dump is and which title it belongs to. It is always tiny,
    // so a packer never touches it.
    for (const VirtualFile& inner : pfs.GetFiles()) {
        if (!inner->GetName().ends_with(".cnmt.nca")) {
            continue;
        }
        const NCA meta{inner};
        if (meta.GetStatus() != Loader::ResultStatus::Success || meta.GetSubdirectories().empty()) {
            continue;
        }
        for (const VirtualFile& entry : meta.GetSubdirectories()[0]->GetFiles()) {
            if (entry->GetExtension() != "cnmt") {
                continue;
            }
            const CNMT cnmt{entry};
            out.title_id = cnmt.GetTitleID();
            out.addon = cnmt.GetType() != TitleType::Application;
            break;
        }
        break;
    }

    for (const VirtualFile& inner : pfs.GetFiles()) {
        const std::string name = inner->GetName();
        // Only the plain archives are worth opening, and the metadata one is not the control one.
        if (inner->GetExtension() != "nca" || name.ends_with(".cnmt.nca")) {
            continue;
        }
        const NCA nca{inner};
        if (nca.GetStatus() != Loader::ResultStatus::Success ||
            nca.GetType() != NCAContentType::Control) {
            continue;
        }
        const auto romfs = nca.GetRomFS();
        if (romfs == nullptr) {
            continue;
        }
        const auto extracted = ExtractRomFS(romfs);
        if (extracted == nullptr) {
            continue;
        }
        auto nacp_file = extracted->GetFile("control.nacp");
        if (nacp_file == nullptr) {
            nacp_file = extracted->GetFile("Control.nacp");
        }
        if (nacp_file != nullptr) {
            out.title = NACP{nacp_file}.GetApplicationName();
        }
        // Whichever language the dump happens to carry; the picture is the same in all of them.
        for (const VirtualFile& file : extracted->GetFiles()) {
            if (file->GetName().starts_with("icon_")) {
                out.icon = file->ReadAllBytes();
                break;
            }
        }
        if (!out.title.empty() || !out.icon.empty()) {
            return out;
        }
    }
    // An update usually carries no control archive of its own; its identity is still worth having.
    return out.title_id != 0 ? std::optional{out} : std::nullopt;
}

bool ConvertNszToNsp(const VirtualFile& nsz, const VirtualFile& out,
                     const std::function<void(u64, u64)>& progress) {
    if (nsz == nullptr || out == nullptr) {
        return false;
    }
    const PartitionFilesystem pfs{nsz};
    if (pfs.GetStatus() != Loader::ResultStatus::Success) {
        LOG_ERROR(Common_Filesystem, "NSZ: not a readable PFS0");
        return false;
    }

    // What each entry will be called and how big it will end up. An NCZ becomes the NCA it was made
    // from, which is the same name with the extension put back, and a size the section table knows.
    struct Entry {
        VirtualFile source;
        std::string name;
        u64 size;
        bool compressed;
    };
    std::vector<Entry> entries;
    u64 total = 0;
    for (const VirtualFile& inner : pfs.GetFiles()) {
        const std::string name = inner->GetName();
        if (inner->GetExtension() == "ncz") {
            const auto size = NczDecompressedSize(inner);
            if (!size) {
                LOG_ERROR(Common_Filesystem, "NSZ: {} has no usable section table", name);
                return false;
            }
            entries.push_back({inner, name.substr(0, name.size() - 3) + "nca", *size, true});
        } else {
            entries.push_back({inner, name, inner->GetSize(), false});
        }
        total += entries.back().size;
    }
    if (entries.empty()) {
        return false;
    }

    // PFS0: a header, one 0x18 record per file, then the names, then the bodies. The string table
    // is padded so the first body starts on a 0x20 boundary, which is what every tool that writes
    // these does.
    std::string strtab;
    std::vector<u32> name_offsets;
    for (const Entry& entry : entries) {
        name_offsets.push_back(static_cast<u32>(strtab.size()));
        strtab += entry.name;
        strtab += '\0';
    }
    while (strtab.size() % 0x20 != 0) {
        strtab += '\0';
    }

    const u64 entry_count = entries.size();
    const u64 body_start = 0x10 + entry_count * 0x18 + strtab.size();

    std::vector<u8> head(body_start, 0);
    const std::array<u8, 4> magic{'P', 'F', 'S', '0'};
    std::memcpy(head.data(), magic.data(), magic.size());
    const u32 count32 = static_cast<u32>(entry_count);
    const u32 strtab32 = static_cast<u32>(strtab.size());
    std::memcpy(head.data() + 0x4, &count32, sizeof(count32));
    std::memcpy(head.data() + 0x8, &strtab32, sizeof(strtab32));

    u64 relative = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        u8* record = head.data() + 0x10 + i * 0x18;
        const u64 size = entries[i].size;
        const u32 name_offset = name_offsets[i];
        const u32 reserved = 0;
        std::memcpy(record + 0x00, &relative, sizeof(relative));
        std::memcpy(record + 0x08, &size, sizeof(size));
        std::memcpy(record + 0x10, &name_offset, sizeof(name_offset));
        std::memcpy(record + 0x14, &reserved, sizeof(reserved));
        relative += size;
    }
    std::memcpy(head.data() + 0x10 + entry_count * 0x18, strtab.data(), strtab.size());
    if (out->Write(head.data(), head.size(), 0) != head.size()) {
        return false;
    }

    u64 position = body_start;
    u64 done = 0;
    std::vector<u8> copy_buffer(0x400000);
    for (const Entry& entry : entries) {
        if (entry.compressed) {
            const auto hash = DecompressNcz(entry.source, out, position);
            if (!hash) {
                LOG_ERROR(Common_Filesystem, "NSZ: {} could not be unpacked", entry.name);
                return false;
            }
            // An NCZ is named after the hash of the NCA it came from, so a wrong result announces
            // itself here rather than as a title that will not boot.
            static constexpr char DIGITS[] = "0123456789abcdef";
            std::string hex;
            for (std::size_t i = 0; i < 0x10; ++i) {
                hex += DIGITS[(*hash)[i] >> 4];
                hex += DIGITS[(*hash)[i] & 0xF];
            }
            const std::string expected = entry.source->GetName().substr(0, 0x20);
            if (hex != expected) {
                LOG_ERROR(Common_Filesystem, "NSZ: {} rebuilt to {}, expected {}", entry.name, hex,
                          expected);
                return false;
            }
        } else {
            u64 copied = 0;
            while (copied < entry.size) {
                const std::size_t want = static_cast<std::size_t>(
                    std::min<u64>(copy_buffer.size(), entry.size - copied));
                const std::size_t got = entry.source->Read(copy_buffer.data(), want, copied);
                if (got == 0 || out->Write(copy_buffer.data(), got, position + copied) != got) {
                    return false;
                }
                copied += got;
            }
        }
        position += entry.size;
        done += entry.size;
        if (progress) {
            progress(done, total);
        }
    }
    return true;
}

} // namespace FileSys
