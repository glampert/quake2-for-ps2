
/* ================================================================================================
 * -*- C -*-
 * File: mem_alloc.h
 * Author: Guilherme R. Lampert
 * Created on: 18/10/15
 * Brief: Wrappers for malloc/free/memalign.
 *
 * This source code is released under the GNU GPL v2 license.
 * Check the accompanying LICENSE file for details.
 * ================================================================================================ */

#ifndef PS2_MEM_ALLOC_H
#define PS2_MEM_ALLOC_H

// NOTE:
// Be sure to updated ps2_mem_tag_names[] in the .c when changing this!
typedef enum
{
    MEMTAG_MISC,       // Miscellaneous/uncategorized (includes the estimate size of the ELF executable).
    MEMTAG_QUAKE,      // Game allocations: Z_Malloc/Z_TagMalloc/etc.
    MEMTAG_RENDERER,   // Things related to rendering / the refresh module.
    MEMTAG_TEXIMAGE,   // Allocs related to images/textures/palettes.
    MEMTAG_HUNK_ALLOC, // Allocs for the Hunk_*() functions.
    MEMTAG_COUNT       // Number of entries in this enum. Internal use only.
} ps2_mem_tag_t;

extern const char * ps2_mem_tag_names[MEMTAG_COUNT];  // Printable strings for the above enum.
extern unsigned int ps2_mem_tag_counts[MEMTAG_COUNT]; // Current memory counts for each of the above tags.

// Allocators:
void * PS2_MemAlloc(int size_bytes, ps2_mem_tag_t tag);
void PS2_MemFree(void * ptr, int size_bytes, ps2_mem_tag_t tag);

// Formatter for printing the memory tags.
const char * PS2_FormatMemoryUnit(unsigned int memorySizeInBytes, int abbreviated);

// Wrapper for memseting pointers to objects/structures.
#define PS2_MemClearObj(obj_ptr) memset((obj_ptr), 0, sizeof(*(obj_ptr)))

#endif // PS2_MEM_ALLOC_H
