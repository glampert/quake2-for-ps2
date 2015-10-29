
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
        PS2_MemFree(base, hunk_max_size, MEMTAG_HUNK_ALLOC);
    }
    --hunk_count;
}
