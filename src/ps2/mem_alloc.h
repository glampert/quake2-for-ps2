
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
// Be sure to updated mem_tag_names[] in the .c when changing this!
typedef enum
{
    MEMTAG_FILESYS,    // Miscellaneous file systems allocations.
    MEMTAG_HUNK_ALLOC, // Allocs for the Hunk_*() functions.
    MEMTAG_TEXIMAGE,   // Allocs related to images/textures/palettes.
    MEMTAG_COUNT       // Number of entries in this enum. Internal use only.
} ps2_mem_tag_t;

void * PS2_MemAlloc(int size_bytes, ps2_mem_tag_t tag);
void PS2_MemFree(void * ptr, ps2_mem_tag_t tag);
void PS2_AddRenderPacketMem(unsigned int size_bytes);

#define PS2_MemClearObj(obj_ptr) memset((obj_ptr), 0, sizeof(*(obj_ptr)))

#endif // PS2_MEM_ALLOC_H
