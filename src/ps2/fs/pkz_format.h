#pragma once
/* ================================================================================================
 * File: pkz_format.h
 * Brief: The compressed game pack, pakN.pkz: a standard zip whose deflated entries can be read
 *        from the middle. Written by src/tools/host/pakzip.cpp from id's pakN.pak, read by
 *        pkz_pack.cpp, which the engine's file system (common/filesys.c) loads it through.
 *
 *        Header-only and free of ps2sdk types on purpose, like audio/spu_adpcm.h: the converter
 *        and the console read the layout from this one file.
 *
 *        Why a zip at all: off a console's USB drive a level load is mostly waiting for reads,
 *        and deflate halves pak0.pak (175 MB to 92 MB), so a load reads half the bytes. Any zip
 *        tool can list, test or extract the pack.
 *
 *        Why restart points: the loaders seek. The collision model and the renderer read a .bsp
 *        lump by lump, out of order, and the MD2 loader reads its frames after the GL commands
 *        that follow them. A deflate stream can only be decoded from its start, and a .bsp is
 *        too big to inflate into RAM whole at a level change. So the converter ends every
 *        block of kDefaultLog2BlockBytes uncompressed bytes with a deflate full flush, which
 *        byte-aligns the stream and forgets the history, and records where each new block
 *        starts. A seek then restarts the inflater at the nearest block at or before the
 *        target, and decodes and drops less than one block to get there. The flushes are plain
 *        deflate (an empty stored block each), so other tools still read the entries; they cost
 *        under 1% of the pack's size at 32 KB.
 *
 *        The layout (APPNOTE.TXT 6.3, nothing past 2.0 features):
 *
 *          - Methods 0 (stored) and 8 (deflate) only; no zip64, encryption or data descriptors.
 *          - Local headers have no extra field, so an entry's data starts right after its name.
 *          - Every central directory entry carries the extra field below. Its presence is also
 *            what tells the reader the local header has no extra field: a zip from another tool
 *            still loads, but its data offsets come from reading each local header once, and it
 *            has no restart points, so seeking backwards inflates from the start again.
 *
 *          Extra field kExtraId ("Q2"), little-endian:
 *            u16 id, u16 size           the standard extra field header
 *            u8  version                kExtraVersion
 *            u8  log2BlockBytes         restart point spacing in uncompressed bytes, or 0: none
 *            u16 reserved               0
 *            u32 restartOffsets[n]      compressed offset of block k + 1, from the entry's data
 *                                       start; n = RestartCount(size, log2BlockBytes)
 *
 * This source code is released under the GNU GPL v2 license.
 * ================================================================================================ */

#include <cstdint>

namespace ps2::pkz {

// ------------------------------------------------------------------------------------------------
// Zip records
// ------------------------------------------------------------------------------------------------

constexpr std::uint32_t kLocalHeaderSig     = 0x04034B50u; // "PK\3\4"
constexpr std::uint32_t kCentralHeaderSig   = 0x02014B50u; // "PK\1\2"
constexpr std::uint32_t kEndOfCentralDirSig = 0x06054B50u; // "PK\5\6"

constexpr std::uint32_t kLocalHeaderBytes     = 30;
constexpr std::uint32_t kCentralHeaderBytes   = 46;
constexpr std::uint32_t kEndOfCentralDirBytes = 22;
constexpr std::uint32_t kMaxZipCommentBytes   = 0xFFFFu;

// Field offsets inside a local header.
constexpr std::uint32_t kLocalNameLenOfs  = 26;
constexpr std::uint32_t kLocalExtraLenOfs = 28;

// Field offsets inside a central directory header.
constexpr std::uint32_t kCentralFlagsOfs       = 8;
constexpr std::uint32_t kCentralMethodOfs      = 10;
constexpr std::uint32_t kCentralCrcOfs         = 16;
constexpr std::uint32_t kCentralCompSizeOfs    = 20;
constexpr std::uint32_t kCentralRawSizeOfs     = 24;
constexpr std::uint32_t kCentralNameLenOfs     = 28;
constexpr std::uint32_t kCentralExtraLenOfs    = 30;
constexpr std::uint32_t kCentralCommentLenOfs  = 32;
constexpr std::uint32_t kCentralDiskStartOfs   = 34;
constexpr std::uint32_t kCentralLocalHeaderOfs = 42;

// Field offsets inside the end of central directory record.
constexpr std::uint32_t kEndDiskOfs         = 4;
constexpr std::uint32_t kEndCentralDiskOfs  = 6;
constexpr std::uint32_t kEndDiskEntriesOfs  = 8;
constexpr std::uint32_t kEndTotalEntriesOfs = 10;
constexpr std::uint32_t kEndCentralSizeOfs  = 12;
constexpr std::uint32_t kEndCentralOfsOfs   = 16;
constexpr std::uint32_t kEndCommentLenOfs   = 20;

constexpr std::uint16_t kMethodStored  = 0;
constexpr std::uint16_t kMethodDeflate = 8;

// General purpose flag bits.
constexpr std::uint16_t kFlagEncrypted      = 0x0001u;
constexpr std::uint16_t kFlagDataDescriptor = 0x0008u; // Sizes after the data; the central directory still has them.
constexpr std::uint16_t kFlagStrongCrypto   = 0x0040u;

// What pakzip writes into "version made by" / "version needed": 2.0, MS-DOS attributes.
constexpr std::uint16_t kZipVersion = 20;

// A 32-bit field holding this means the real value is in a zip64 record, which isn't supported.
constexpr std::uint32_t kZip64Marker = 0xFFFFFFFFu;

// ------------------------------------------------------------------------------------------------
// The "Q2" extra field
// ------------------------------------------------------------------------------------------------

constexpr std::uint16_t kExtraId           = 0x3251u; // 'Q', '2' as a little-endian u16.
constexpr std::uint8_t  kExtraVersion      = 1;
constexpr std::uint32_t kExtraHeaderBytes  = 4; // id + size
constexpr std::uint32_t kExtraFixedBytes   = 4; // version, log2BlockBytes, reserved
constexpr std::uint32_t kRestartOffsetSize = 4;

// 32 KB blocks: the deflate window, so a restart discards at most one window of output.
// Measured on pak0.pak: 16 KB blocks cost 1.4% of the pack, 32 KB 0.8%, 64 KB 0.5%.
constexpr int kDefaultLog2BlockBytes = 15;
constexpr int kMinLog2BlockBytes     = 12;
constexpr int kMaxLog2BlockBytes     = 24;

// An extra field's size is a u16, which caps the restart points one entry can have.
constexpr std::uint32_t kMaxRestartsPerEntry =
    (0xFFFFu - kExtraHeaderBytes - kExtraFixedBytes) / kRestartOffsetSize;

// Restart points an entry of rawBytes has: one per block after the first.
constexpr std::uint32_t RestartCount(const std::uint32_t rawBytes, const int log2BlockBytes)
{
    return (log2BlockBytes == 0 || rawBytes == 0) ? 0u : ((rawBytes - 1u) >> log2BlockBytes);
}

// ------------------------------------------------------------------------------------------------
// Little-endian access (zip fields are unaligned)
// ------------------------------------------------------------------------------------------------

inline std::uint16_t ReadU16(const std::uint8_t * p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

inline std::uint32_t ReadU32(const std::uint8_t * p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

inline void WriteU16(std::uint8_t * p, const std::uint16_t v)
{
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

inline void WriteU32(std::uint8_t * p, const std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}

// ------------------------------------------------------------------------------------------------
// Parsing the extra field
// ------------------------------------------------------------------------------------------------

// The Q2 field of one central directory entry, found by FindQ2Extra.
struct Q2Extra
{
    int                  log2BlockBytes; // 0: no restart points.
    std::uint32_t        restartCount;
    const std::uint8_t * restartOffsets; // restartCount little-endian u32s, unaligned.
};

// Walks an entry's extra fields for the Q2 one. Returns 1 if found and well formed, 0 if the
// entry has none, -1 if it has one that is malformed or doesn't match rawBytes.
inline int FindQ2Extra(const std::uint8_t * extra, const std::uint32_t extraBytes,
                       const std::uint32_t rawBytes, Q2Extra & out)
{
    std::uint32_t at = 0;
    while (at + kExtraHeaderBytes <= extraBytes)
    {
        const std::uint16_t id   = ReadU16(extra + at);
        const std::uint32_t size = ReadU16(extra + at + 2);
        const std::uint8_t * data = extra + at + kExtraHeaderBytes;

        if (at + kExtraHeaderBytes + size > extraBytes)
        {
            return (id == kExtraId) ? -1 : 0; // Truncated field: no Q2 data to trust either way.
        }

        if (id == kExtraId)
        {
            if (size < kExtraFixedBytes || data[0] != kExtraVersion)
            {
                return -1;
            }

            const int log2BlockBytes = data[1];
            if (log2BlockBytes != 0 && (log2BlockBytes < kMinLog2BlockBytes || log2BlockBytes > kMaxLog2BlockBytes))
            {
                return -1;
            }

            const std::uint32_t restarts = RestartCount(rawBytes, log2BlockBytes);
            if (size != kExtraFixedBytes + restarts * kRestartOffsetSize)
            {
                return -1;
            }

            out.log2BlockBytes = log2BlockBytes;
            out.restartCount   = restarts;
            out.restartOffsets = data + kExtraFixedBytes;
            return 1;
        }

        at += kExtraHeaderBytes + size;
    }
    return 0;
}

} // namespace ps2::pkz
