/* ================================================================================================
 * File: miniz_export.h
 * Brief: Stands in for the header miniz's CMake build generates, for the host tools built with
 *        miniz (pakzip). The game's copy is ps2/save/miniz_cfg/miniz_export.h; this one differs
 *        on purpose: the host has memory to spare, so the compressor keeps its full-size hash
 *        tables (no TDEFL_LESS_MEMORY) for a slightly better ratio, and may use malloc.
 *        Plain C: it is part of the C library build too.
 *
 * This source code is released under the GNU GPL v2 license.
 * ================================================================================================ */

#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

/* Statically linked: no symbol visibility decoration. */
#define MINIZ_EXPORT

/* Only raw deflate/inflate (tdefl/tinfl) and mz_crc32 are used; the zip is written by hand. */
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ZLIB_APIS

#endif /* MINIZ_EXPORT_H */
