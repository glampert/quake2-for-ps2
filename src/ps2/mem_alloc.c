
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
#include "game/q_shared.h" // For mem_hunk_t

#include <stdlib.h>
#include <string.h>

// We fail with a hard error if out-of-memory.
extern void Sys_Error(const char * error, ...);

// For debug printing
const char * ps2_mem_tag_names[MEMTAG_COUNT] =
{
    "MISC",
    "QUAKE",
    "RENDERER",
    "TEX_IMAGE",
    "HUNK_ALLOC"
};

// Tags updated on every alloc/free.
unsigned int ps2_mem_tag_counts[MEMTAG_COUNT] = {0};

//=============================================================================
//
// malloc/free hooks:
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

    void * ptr = malloc(size_bytes);
    if (ptr == NULL)
    {
        Sys_Error("Out-of-memory for tag %s! Failed to alloc %d bytes.",
                  ps2_mem_tag_names[tag], size_bytes);
    }

    ps2_mem_tag_counts[tag] += size_bytes;
    return ptr;
}

/*
================
PS2_MemFree
================
*/
void PS2_MemFree(void * ptr, int size_bytes, ps2_mem_tag_t tag)
{
    if (ptr == NULL)
    {
        return;
    }

    ps2_mem_tag_counts[tag] -= size_bytes;
    free(ptr);
}

/*
================
PS2_FormatMemoryUnit
================
*/
const char * PS2_FormatMemoryUnit(unsigned int size_bytes, int abbreviated)
{
    enum
    {
        KILOBYTE = 1024,
        MEGABYTE = 1024 * KILOBYTE,
        GIGABYTE = 1024 * MEGABYTE
    };

    const char * mem_unit_str;
    float adjusted_size;

    if (size_bytes < KILOBYTE)
    {
        mem_unit_str  = abbreviated ? "B" : "Bytes";
        adjusted_size = (float)size_bytes;
    }
    else if (size_bytes < MEGABYTE)
    {
        mem_unit_str  = abbreviated ? "KB" : "Kilobytes";
        adjusted_size = size_bytes / 1024.0f;
    }
    else if (size_bytes < GIGABYTE)
    {
        mem_unit_str  = abbreviated ? "MB" : "Megabytes";
        adjusted_size = size_bytes / 1024.0f / 1024.0f;
    }
    else
    {
        mem_unit_str  = abbreviated ? "GB" : "Gigabytes";
        adjusted_size = size_bytes / 1024.0f / 1024.0f / 1024.0f;
    }

    char * fmtbuf;
    int chars_written;
    char num_str_buf[100];

    // Max chars reserved for the output string, max `num_str_buf + 28` chars for the unit str:
    enum
    {
        MEM_UNIT_STR_MAXLEN = sizeof(num_str_buf) + 28
    };
    static int bufnum = 0;
    static char local_str_buf[8][MEM_UNIT_STR_MAXLEN];

    fmtbuf = local_str_buf[bufnum];
    bufnum = (bufnum + 1) & 7;

    // We only care about the first 2 decimal digs
    chars_written = snprintf(num_str_buf, 100, "%.2f", adjusted_size);
    if (chars_written <= 0)
    {
        fmtbuf[0] = '?';
        fmtbuf[1] = '?';
        fmtbuf[2] = '?';
        fmtbuf[3] = '\0';
        return fmtbuf; // Error return
    }

    // Remove trailing zeros if no significant decimal digits:
    char * p = num_str_buf;
    for (; *p; ++p)
    {
        if (*p == '.')
        {
            // Find the end of the string
            while (*++p)
            {
            }

            // Remove trailing zeros
            while (*--p == '0')
            {
                *p = '\0';
            }
            // If the dot was left alone at the end, remove it too
            if (*p == '.')
            {
                *p = '\0';
            }
            break;
        }
    }

    // Consolidate the strings:
    chars_written = snprintf(fmtbuf, MEM_UNIT_STR_MAXLEN, "%s %s", num_str_buf, mem_unit_str);
    if (chars_written <= 0)
    {
        fmtbuf[0] = '?';
        fmtbuf[1] = '?';
        fmtbuf[2] = '?';
        fmtbuf[3] = '\0';
        return fmtbuf; // Error return
    }

    return fmtbuf;
}

//=============================================================================
//
// Hunk memory allocator (similar to a stack), used by the Game and Renderer:
//
//=============================================================================

/*
================
Hunk_New
================
*/
void Hunk_New(mem_hunk_t * hunk, int max_size)
{
    hunk->curr_size = 0;
    hunk->max_size  = max_size;
    hunk->base_ptr  = PS2_MemAlloc(max_size, MEMTAG_HUNK_ALLOC);
    memset(hunk->base_ptr, 0, max_size);
}

/*
================
Hunk_Free
================
*/
void Hunk_Free(mem_hunk_t * hunk)
{
    if (hunk->base_ptr != NULL)
    {
        PS2_MemFree(hunk->base_ptr, hunk->max_size, MEMTAG_HUNK_ALLOC);
        PS2_MemClearObj(hunk);
    }
}

/*
================
Hunk_BlockAlloc
================
*/
byte * Hunk_BlockAlloc(mem_hunk_t * hunk, int block_size)
{
    // This is the way Quake2 does it, so I ain't changing it...
    block_size = (block_size + 31) & ~31; // round to cacheline

    // The hunk stack doesn't resize.
    hunk->curr_size += block_size;
    if (hunk->curr_size > hunk->max_size)
    {
        Sys_Error("Hunk_BlockAlloc: Overflowed with %d bytes request!", block_size);
    }

    return hunk->base_ptr + hunk->curr_size - block_size;
}

/*
================
Hunk_GetTail
================
*/
int Hunk_GetTail(mem_hunk_t * hunk)
{
    return hunk->curr_size;
}
