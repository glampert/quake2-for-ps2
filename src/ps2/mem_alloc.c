
/* ================================================================================================
 * -*- C -*-
 * File: mem_alloc.c
 * Author: Guilherme R. Lampert
 * Created on: 18/10/15
 * Brief: Wrappers for malloc/free/memalign.
 *
 * This source code is released under the GNU GPL v2 license.
 * Check the accompanying LICENSE file for details.
 * ================================================================================================ */

#include "ps2/mem_alloc.h"
#include <stdlib.h>
#include <string.h>

// We fail with a hard error if out-of-memory.
extern void Sys_Error(const char * error, ...);

// For debug printing
static const char * mem_tag_names[MEMTAG_COUNT] =
{
    "MEMTAG_FILESYS",
    "MEMTAG_HUNK_ALLOC",
    "MEMTAG_TEXIMAGE"
};

//=============================================================================
//
// malloc/memalign hooks:
//
//=============================================================================

/*
================
PS2_MemAlloc
================
*/
void * PS2_MemAlloc(int size_bytes, ps2_mem_tag_t tag)
{
    if (size_bytes <= 0)
    {
        Sys_Error("Trying to allocate zero or negative size (%d)!", size_bytes);
    }

    //TODO add tracking counters!
    (void)tag;

    void * ptr = malloc(size_bytes);
    if (ptr == NULL)
    {
        Sys_Error("Out-of-memory for tag %s!", mem_tag_names[tag]);
    }

    return ptr;
}

/*
================
PS2_MemFree
================
*/
void PS2_MemFree(void * ptr, ps2_mem_tag_t tag)
{
    if (ptr == NULL)
    {
        return;
    }

    //TODO add tracking counters!
    (void)tag;

    free(ptr);
}

/*
================
PS2_AddRenderPacketMem
================
*/
void PS2_AddRenderPacketMem(unsigned int size_bytes)
{
    // TODO so we can keep track of memory allocated
    // externally by the SDK for render packets!
    (void)size_bytes;
}

//=============================================================================
//
// Hunk memory allocator (similar to a stack), used by the Game and FS:
//
//=============================================================================

static int hunk_count       = 0;
static int hunk_max_size    = 0;
static int hunk_curr_size   = 0;
static char * hunk_mem_base = NULL;

/*
================
Hunk_Begin
================
*/
void * Hunk_Begin(int maxsize)
{
    hunk_curr_size = 0;
    hunk_max_size  = maxsize;
    hunk_mem_base  = PS2_MemAlloc(maxsize, MEMTAG_HUNK_ALLOC);
    memset(hunk_mem_base, 0, maxsize);
    return hunk_mem_base;
}

/*
================
Hunk_End
================
*/
int Hunk_End(void)
{
    ++hunk_count;
    return hunk_curr_size;
}

/*
================
Hunk_Alloc
================
*/
void * Hunk_Alloc(int size)
{
    size = (size + 31) & ~31; // round to cacheline

    hunk_curr_size += size;
    if (hunk_curr_size > hunk_max_size)
    {
        Sys_Error("Hunk_Alloc: Overflowed!");
    }

    return hunk_mem_base + hunk_curr_size - size;
}

/*
================
Hunk_Free
================
*/
void Hunk_Free(void * base)
{
    if (base != NULL)
    {
        PS2_MemFree(base, MEMTAG_HUNK_ALLOC);
    }
    --hunk_count;
}
