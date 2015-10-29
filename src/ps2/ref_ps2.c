
/* ================================================================================================
 * -*- C -*-
 * File: ref_ps2.c
 * Author: Guilherme R. Lampert
 * Created on: 13/10/15
 * Brief: Implementation of Quake2 "refresh" methods (renderer module) for the PS2.
 *
 * This source code is released under the GNU GPL v2 license.
 * Check the accompanying LICENSE file for details.
 * ================================================================================================ */

#include "ps2/ref_ps2.h"
#include "ps2/mem_alloc.h"

// PS2DEV SDK:
#include <kernel.h>
#include <dma_tags.h>
#include <gif_tags.h>
#include <dma.h>

//=============================================================================

// Renderer globals (alignment not strictly required, but shouldn't harm either...)
ps2_refresh_t       ps2ref  __attribute__((aligned(16))) = {0};
ps2_perf_counters_t perfcnt __attribute__((aligned(16))) = {0};

// Config vars:
static cvar_t * r_ps2_vid_width      = NULL;
static cvar_t * r_ps2_vid_height     = NULL;
static cvar_t * r_ps2_ui_brightness  = NULL;
static cvar_t * r_ps2_fade_scr_alpha = NULL;
static cvar_t * r_ps2_show_fps       = NULL;
static cvar_t * r_ps2_show_mem_tags  = NULL;

// Average multiple frames together to smooth changes out a bit.
enum { MAX_FPS_HIST = 4 };
static struct
{
    int index;
    int fps_count;
    int previous_time;
    int times_hist[MAX_FPS_HIST];
} fps;

//
// All 2D draw calls are batched and flushed at the end of each frame.
// Uses uint16s since we need quite a few of these. See PS2_Flush2DBatch().
//
typedef struct
{
    s16 tex_index;
    u16 z_index;
    u16 x0, y0;
    u16 x1, y1;
    u16 u0, v0;
    u16 u1, v1;
    byte r, g, b, a;
} screen_quad_t;

enum
{
    DRAW2D_BATCH_SIZE         =  8500, // size of draw2d_batch[] in elements
    DRAW2D_TEX_INDEX_NO_TEX   = -1,    // used by quad->tex_index for non-textured quads
    DRAW2D_TEX_INDEX_FADE_SCR = -2     // dummy value for DrawFadeScreen tex_index
};

// Sequential z-index for 2D primitives added to the draw2d_batch[].
static u16 next_z_index_2d = 0;
#define DRAW2D_NEXT_Z_INDEX() (next_z_index_2d++)

// Set if there's a full screen overlay for this frame (index into draw2d_batch).
// We only allow one per frame, and that's all Quake2 needs!
// -1 if no fade screen/full screen wipe this frame.
static int fade_scr_index = -1;

// 2D draw calls are always batched to try avoiding unnecessary texture switches.
static int next_in_2d_batch = 0;
static screen_quad_t draw2d_batch[DRAW2D_BATCH_SIZE] __attribute__((aligned(16)));
#define DRAW2D_NEXT_BATCH_ELEMENT() (&draw2d_batch[next_in_2d_batch++])

//
// Quake2 cinematics.
// One cinematic frame per rendered frame at most, if active.
//
static struct
{
    int x, y;
    int w, h;
    ps2_teximage_t * teximage;
    qboolean draw_pending;
} cinematic_frame;

// Palette provided by the game to expand 8bit cinematic frames.
static u32 cinematic_palette[256] __attribute__((aligned(16)));

// Cinematic frames are rendered into this RGB16 texture
// and blitted to screen using a full-screen quadrilateral
// that applies this buffer as texture.
static u16 cinematic_buffer[MAX_TEXIMAGE_SIZE * MAX_TEXIMAGE_SIZE] __attribute__((aligned(16)));

//=============================================================================
//
// Local helpers:
//
//=============================================================================

// This assumes the pointer is a member of ps2ref.teximages[]!
#define TEXIMAGE_INDEX(teximage_ptr) ((int)((teximage_ptr) - ps2ref.teximages))

// Test if the texture pointer is from the scrap atlas.
#define TEXIMAGE_IS_SCRAP(teximage_ptr) ((teximage_ptr)->u1 != 0 && (teximage_ptr)->v1 != 0)

//
// Error checks:
//

extern void Sys_Error(const char * error, ...);
#define CHECK_FRAME_STARTED()                                          \
    do                                                                 \
    {                                                                  \
        if (!ps2ref.frame_started)                                     \
        {                                                              \
            Sys_Error("%s called outside begin/end frame!", __func__); \
        }                                                              \
    } while (0)

//
// DMA GIF tag helpers:
//

#define BEGIN_DMA_TAG(qword_ptr) \
    qword_t * _dma_tag_ = (qword_ptr)++

#define END_DMA_TAG(qword_ptr) \
    DMATAG_CNT(_dma_tag_, (qword_ptr) - _dma_tag_ - 1, 0, 0, 0)

#define END_DMA_TAG_AND_CHAIN(qword_ptr) \
    DMATAG_END(_dma_tag_, (qword_ptr) - _dma_tag_ - 1, 0, 0, 0)

#define BEGIN_DMA_TAG_NAMED(tag_name, qword_ptr) \
    tag_name = (qword_ptr)++

#define END_DMA_TAG_NAMED(tag_name, qword_ptr) \
    DMATAG_CNT((tag_name), (qword_ptr) - (tag_name) - 1, 0, 0, 0)

//=============================================================================

/*
================
PS2_VRamAlloc
================
*/
static int PS2_VRamAlloc(int width, int height, int psm, int alignment)
{
    int addr = graph_vram_allocate(width, height, psm, alignment);
    if (addr < 0)
    {
        Sys_Error("Failed to allocate VRam space! Requested: %d, %d, %d\n",
                  width, height, alignment);
    }

    int size = graph_vram_size(width, height, psm, alignment);
    ps2ref.vram_used_bytes += size * 4; // Size is in 32bit VRam words.
    return addr;
}

/*
================
PS2_InitGSBuffers
================
*/
static void PS2_InitGSBuffers(int vidMode, int fbPsm, int zPsm, qboolean interlaced)
{
    // Init GIF DMA channel:
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);

    // FB 0:
    ps2ref.frame_buffers[0].width   = viddef.width;
    ps2ref.frame_buffers[0].height  = viddef.height;
    ps2ref.frame_buffers[0].mask    = 0;
    ps2ref.frame_buffers[0].psm     = fbPsm;
    ps2ref.frame_buffers[0].address = PS2_VRamAlloc(ps2ref.frame_buffers[0].width,
                                                    ps2ref.frame_buffers[0].height,
                                                    ps2ref.frame_buffers[0].psm,
                                                    GRAPH_ALIGN_PAGE);

    // FB 1:
    ps2ref.frame_buffers[1].width   = viddef.width;
    ps2ref.frame_buffers[1].height  = viddef.height;
    ps2ref.frame_buffers[1].mask    = 0;
    ps2ref.frame_buffers[1].psm     = fbPsm;
    ps2ref.frame_buffers[1].address = PS2_VRamAlloc(ps2ref.frame_buffers[1].width,
                                                    ps2ref.frame_buffers[1].height,
                                                    ps2ref.frame_buffers[1].psm,
                                                    GRAPH_ALIGN_PAGE);

    // Z-buffer/depth-buffer (same size as the frame-buffers):
    ps2ref.z_buffer.enable  = DRAW_ENABLE;
    ps2ref.z_buffer.mask    = 0;
    ps2ref.z_buffer.method  = ZTEST_METHOD_GREATER_EQUAL;
    ps2ref.z_buffer.zsm     = zPsm;
    ps2ref.z_buffer.address = PS2_VRamAlloc(ps2ref.frame_buffers[0].width,
                                            ps2ref.frame_buffers[0].height,
                                            ps2ref.z_buffer.zsm,
                                            GRAPH_ALIGN_PAGE);

    // User textures start after the z-buffer.
    // Allocate space for a single 256x256 large texture
    // (pretty much all the space we have left):
    ps2ref.vram_texture_start = PS2_VRamAlloc(MAX_TEXIMAGE_SIZE,
                                              MAX_TEXIMAGE_SIZE,
                                              GS_PSM_32,
                                              GRAPH_ALIGN_BLOCK);

    //
    // Initialize the screen and tie the first framebuffer to the read circuits:
    //

    // Select between NTSC or PAL, based on region:
    if (vidMode == GRAPH_MODE_AUTO)
    {
        vidMode = graph_get_region();
    }

    // Set video mode with flicker filter:
    int graphMode = interlaced ? GRAPH_MODE_INTERLACED : GRAPH_MODE_NONINTERLACED;
    graph_set_mode(graphMode, vidMode, GRAPH_MODE_FIELD, GRAPH_ENABLE);

    // Set screen dimensions and framebuffer:
    graph_set_screen(0, 0,
                     ps2ref.frame_buffers[0].width,
                     ps2ref.frame_buffers[0].height);
    graph_set_bgcolor(0, 0, 0);

    graph_set_framebuffer_filtered(ps2ref.frame_buffers[0].address,
                                   ps2ref.frame_buffers[0].width,
                                   ps2ref.frame_buffers[0].psm,
                                   0, 0);
    graph_enable_output();
}

/*
================
PS2_InitDrawingEnvironment
================
*/
static void PS2_InitDrawingEnvironment(void)
{
    // Temporary packet for the parameter setup:
    packet_t * packet = packet_init(50, PACKET_NORMAL);

    // Set framebuffer and virtual screen offsets:
    qword_t * q = packet->data;
    q = draw_setup_environment(q, 0, &ps2ref.frame_buffers[0], &ps2ref.z_buffer);
    q = draw_primitive_xyoffset(q, 0, 2048 - (viddef.width / 2), 2048 - (viddef.height / 2));

    // Default texture addressing mode: REPEAT
    texwrap_t wrap;
    wrap.horizontal = WRAP_REPEAT;
    wrap.vertical   = WRAP_REPEAT;
    wrap.minu = 0;
    wrap.minv = 0;
    wrap.maxu = MAX_TEXIMAGE_SIZE;
    wrap.maxv = MAX_TEXIMAGE_SIZE;
    q = draw_texture_wrapping(q, 0, &wrap);

    q = draw_finish(q);
    dma_channel_send_normal(DMA_CHANNEL_GIF, packet->data, (q - packet->data), 0, 0);
    dma_wait_fast();

    packet_free(packet);
}

/*
================
PS2_ClearScreen
================
*/
static void PS2_ClearScreen(void)
{
    int width  = viddef.width;
    int height = viddef.height;

    BEGIN_DMA_TAG(ps2ref.current_frame_qwptr);
    ps2ref.current_frame_qwptr = draw_disable_tests(ps2ref.current_frame_qwptr, 0, &ps2ref.z_buffer);

    ps2ref.current_frame_qwptr =
        draw_clear(ps2ref.current_frame_qwptr, 0,
                   2048 - (width  / 2),
                   2048 - (height / 2),
                   width, height,
                   ps2ref.screen_color.r,
                   ps2ref.screen_color.g,
                   ps2ref.screen_color.b);

    ps2ref.current_frame_qwptr = draw_enable_tests(ps2ref.current_frame_qwptr, 0, &ps2ref.z_buffer);
    END_DMA_TAG(ps2ref.current_frame_qwptr);
}

/*
================
PS2_Draw2DBegin
================
*/
static void PS2_Draw2DBegin(void)
{
    if (ps2ref.dmatag_draw2d != NULL)
    {
        Sys_Error("Draw2DBegin: Already in 2D mode!");
    }

    // Reference the external tag 'dmatag_draw2d':
    BEGIN_DMA_TAG_NAMED(ps2ref.dmatag_draw2d, ps2ref.current_frame_qwptr);
    ps2ref.current_frame_qwptr = draw_primitive_xyoffset(ps2ref.current_frame_qwptr, 0, 2048, 2048);
}

/*
================
PS2_Draw2DEnd
================
*/
static void PS2_Draw2DEnd(void)
{
    ps2ref.current_frame_qwptr = draw_primitive_xyoffset(ps2ref.current_frame_qwptr, 0,
                                                         2048 - (viddef.width  / 2),
                                                         2048 - (viddef.height / 2));

    // Close 'dmatag_draw2d'.
    END_DMA_TAG_NAMED(ps2ref.dmatag_draw2d, ps2ref.current_frame_qwptr);
    ps2ref.dmatag_draw2d = NULL;
}

/*
================
PS2_FlushPipeline
================
*/
static void PS2_FlushPipeline(void)
{
    // Add a finish command to the DMA chain:
    BEGIN_DMA_TAG(ps2ref.current_frame_qwptr);
    ps2ref.current_frame_qwptr = draw_finish(ps2ref.current_frame_qwptr);
    END_DMA_TAG_AND_CHAIN(ps2ref.current_frame_qwptr);

    dma_channel_send_chain(DMA_CHANNEL_GIF, ps2ref.current_frame_packet->data,
                           (ps2ref.current_frame_qwptr - ps2ref.current_frame_packet->data),
                           0, 0);

    // Reset packet data pointers:
    ps2ref.current_frame_packet = ps2ref.frame_packets[ps2ref.frame_index];
    ps2ref.current_frame_qwptr  = ps2ref.current_frame_packet->data;

    // Synchronize.
    dma_wait_fast();
    perfcnt.pipe_flushes++;
}

/*
================
PS2_Draw2DAddToPacket
================
*/
enum elem2d_type
{
    ELEM_2D_TEXTURED,
    ELEM_2D_COLOR_ONLY,
    ELEM_2D_FADE_SCR
};
static qword_t * PS2_Draw2DAddToPacket(qword_t * qwptr, const screen_quad_t * quad, enum elem2d_type type)
{
    rect_t rect;
    texrect_t texrect;

    switch (type)
    {
    case ELEM_2D_TEXTURED :
        texrect.v0.x = quad->x0;
        texrect.v0.y = quad->y0;
        texrect.v0.z = 0xFFFFFFFF;
        texrect.t0.u = quad->u0;
        texrect.t0.v = quad->v0;
        texrect.v1.x = quad->x1;
        texrect.v1.y = quad->y1;
        texrect.v1.z = 0xFFFFFFFF;
        texrect.t1.u = quad->u1;
        texrect.t1.v = quad->v1;
        texrect.color.r = quad->r;
        texrect.color.g = quad->g;
        texrect.color.b = quad->b;
        texrect.color.a = quad->a;
        texrect.color.q = 1.0f;
        qwptr = draw_rect_textured(qwptr, 0, &texrect);
        break;

    case ELEM_2D_COLOR_ONLY :
        rect.v0.x = quad->x0;
        rect.v0.y = quad->y0;
        rect.v0.z = 0xFFFFFFFF;
        rect.v1.x = quad->x1;
        rect.v1.y = quad->y1;
        rect.v1.z = 0xFFFFFFFF;
        rect.color.r = quad->r;
        rect.color.g = quad->g;
        rect.color.b = quad->b;
        rect.color.a = quad->a;
        rect.color.q = 1.0f;
        qwptr = draw_rect_filled(qwptr, 0, &rect);
        break;

    case ELEM_2D_FADE_SCR :
        rect.v0.x = quad->x0;
        rect.v0.y = quad->y0;
        rect.v0.z = 0xFFFFFFFF;
        rect.v1.x = quad->x1;
        rect.v1.y = quad->y1;
        rect.v1.z = 0xFFFFFFFF;
        rect.color.r = quad->r;
        rect.color.g = quad->g;
        rect.color.b = quad->b;
        rect.color.a = quad->a;
        rect.color.q = 1.0f;
        // Was experiencing gaps in the emulator,
        // so added these offsets...
        rect.v0.x -= 2;
        rect.v0.y -= 2;
        rect.v1.x += 2;
        rect.v1.y += 2;
        draw_enable_blending();
        // libdraw recommends filled strips for full screen draws.
        // It seems like the GS is slower when dealing with very large polygons.
        qwptr = draw_rect_filled_strips(qwptr, 0, &rect);
        draw_disable_blending();
        break;

    default :
        Sys_Error("Bad elem2d_type enum!!!");
    } // switch (type)

    return qwptr;
}

/*
================
PS2_Draw2DTexChange
================
*/
static void PS2_Draw2DTexChange(u32 tex_index)
{
    // This is only called by PS2_SortAndDraw2DElements.

    if (tex_index > MAX_TEXIMAGES)
    {
        Sys_Error("PS2_Draw2DTexChange: Invalid tex_index %d!!!", (int)tex_index);
    }

    ps2_teximage_t * teximage = &ps2ref.teximages[tex_index];
    if (teximage != ps2ref.current_tex)
    {
        // Note: also check the pic pointer because of the shared scrap atlas!
        qboolean need_tex_switch = (ps2ref.current_tex == NULL);
        if (!need_tex_switch)
        {
            need_tex_switch = (teximage->pic != ps2ref.current_tex->pic);
        }

        if (need_tex_switch)
        {
            // Need to close the current 2D tag, flush, then reopen it.
            END_DMA_TAG_NAMED(ps2ref.dmatag_draw2d, ps2ref.current_frame_qwptr);

            PS2_FlushPipeline();
            PS2_TexImageVRamUpload(teximage);

            BEGIN_DMA_TAG_NAMED(ps2ref.dmatag_draw2d, ps2ref.current_frame_qwptr);
            PS2_TexImageBindCurrent();
        }
    }
}

/*
================
PS2_Draw2DBatchSortPredicate
================
*/
static int PS2_Draw2DBatchSortPredicate(const void * a, const void * b)
{
    const screen_quad_t * q1 = (const screen_quad_t *)a;
    const screen_quad_t * q2 = (const screen_quad_t *)b;

    // Group by texture:
    int result = q1->tex_index - q2->tex_index;
    if (result == 0)
    {
        // Within each texture group, order by z-index.
        return (int)q1->z_index - (int)q2->z_index;
    }
    else
    {
        return result;
    }
}

/*
================
PS2_SortAndDraw2DElements
================
*/
static void PS2_SortAndDraw2DElements(screen_quad_t * batch, int batch_size)
{
    if (batch_size <= 0)
    {
        return;
    }

    // Sort by texture, so we only switch once per texture image:
    qsort(batch, batch_size, sizeof(screen_quad_t), &PS2_Draw2DBatchSortPredicate);

    // Non-textured elements draw first. Since those have negative
    // tex_index, they are at the front of the sorted batch.
    int curr_tex_idx = DRAW2D_TEX_INDEX_NO_TEX;

    int i;
    enum elem2d_type quad_type;
    const screen_quad_t * quad;

    for (i = 0; i < batch_size; ++i)
    {
        quad = &batch[i];

        if (quad->tex_index > DRAW2D_TEX_INDEX_NO_TEX)
        {
            quad_type = ELEM_2D_TEXTURED;
        }
        else
        {
            quad_type = ELEM_2D_COLOR_ONLY;
        }

        // Might have jumped to another texture group...
        if (quad->tex_index != curr_tex_idx)
        {
            if (quad->tex_index > DRAW2D_TEX_INDEX_NO_TEX)
            {
                PS2_Draw2DTexChange((u32)quad->tex_index);
            }
            curr_tex_idx = quad->tex_index;
        }

        ps2ref.current_frame_qwptr =
            PS2_Draw2DAddToPacket(ps2ref.current_frame_qwptr, quad, quad_type);
    }
}

/*
================
PS2_Flush2DBatch
================
*/
static void PS2_Flush2DBatch(void)
{
    if (next_in_2d_batch == 0)
    {
        return; // Nothing this frame.
    }

    if (next_in_2d_batch > DRAW2D_BATCH_SIZE)
    {
        Sys_Error("PS2_Flush2DBatch: Bad next_in_2d_batch value!!!");
    }

    //
    // We want to sort all elements by texture to avoid unnecessary texture switches.
    // But by doing that, we'll break z-order. This is mostly fine for Quake, but it does
    // requires a few workarounds. Fade screens and full screen wipes are the annoying cases.
    //
    // - DrawFill (colored only quads) will draw first, thanks to our sorting predicate,
    //   with preserved z-order.
    //
    // - Textured draws will be next. Depth order withing each texture group is preserved
    //   but order is not guaranteed to remain correct across different textures. We can
    //   get away with this because there's little overlapping of 2D UI elements in Quake,
    //   plus we group small things into the scrap atlas, so depth order errors should be
    //   rare, if at all present.
    //
    // - Fade screens and full screen wipes are not textured, but they cannot be mixed
    //   with the rest of the untextured quads, because for these draw order does matter.
    //   Things can be drawn on top of a fade screen, so we split each sorted batch by
    //   fade screen draw, creating two sub-batches for each fade/wipe, one drawn under
    //   it and one above.
    //

    if (fade_scr_index < 0)
    {
        PS2_SortAndDraw2DElements(draw2d_batch, next_in_2d_batch);
    }
    else // Has one screen fade/wipe, split the batch:
    {
        // Things under the fade overlay:
        PS2_SortAndDraw2DElements(draw2d_batch, fade_scr_index);

        // Draw the overlay:
        ps2ref.current_frame_qwptr = PS2_Draw2DAddToPacket(
                                        ps2ref.current_frame_qwptr,
                                        &draw2d_batch[fade_scr_index],
                                        ELEM_2D_FADE_SCR);

        // Elements on top of the fade overlay:
        PS2_SortAndDraw2DElements(draw2d_batch + fade_scr_index + 1,
                                  next_in_2d_batch - (fade_scr_index + 1));
    }

    // Done! Reset for next frame:
    next_z_index_2d  =  0;
    next_in_2d_batch =  0;
    fade_scr_index   = -1;
}

/*
================
PS2_DrawFullScreenCinematic
================
*/
static void PS2_DrawFullScreenCinematic(void)
{
    texrect_t texrect;

    // Cinematic are not filling the screen properly on my tests...
    // Adding these hackish offsets for now.
    texrect.v0.x = cinematic_frame.x - 1;
    texrect.v0.y = cinematic_frame.y - 1;
    texrect.v1.x = cinematic_frame.x + cinematic_frame.w + 1;
    texrect.v1.y = cinematic_frame.y + cinematic_frame.h + 45;
    texrect.v0.z = 0xFFFFFFFF;
    texrect.v1.z = 0xFFFFFFFF;

    texrect.t0.u = 0;
    texrect.t0.v = 0;
    texrect.t1.u = cinematic_frame.teximage->width;
    texrect.t1.v = cinematic_frame.teximage->height;

    texrect.color.r = (byte)ps2ref.ui_brightness;
    texrect.color.g = (byte)ps2ref.ui_brightness;
    texrect.color.b = (byte)ps2ref.ui_brightness;
    texrect.color.a = (byte)ps2ref.ui_brightness;
    texrect.color.q = 1.0f;

    PS2_TexImageVRamUpload(cinematic_frame.teximage);
    PS2_TexImageBindCurrent();

    ps2ref.current_frame_qwptr = draw_rect_textured(
                    ps2ref.current_frame_qwptr, 0, &texrect);

    // Since the cinematic uses a special texture buffer, we need to flush
    // immediately after the thing is drawn. Disable and re-enable 2D mode for that.
    END_DMA_TAG_NAMED(ps2ref.dmatag_draw2d, ps2ref.current_frame_qwptr);
    PS2_FlushPipeline();
    BEGIN_DMA_TAG_NAMED(ps2ref.dmatag_draw2d, ps2ref.current_frame_qwptr);

    cinematic_frame.draw_pending = false;
}

/*
================
PS2_DrawFPSCounter
================
*/
static void PS2_DrawFPSCounter(void)
{
    int time_millisec = Sys_Milliseconds(); // Real time clock
    int frame_time = time_millisec - fps.previous_time;

    fps.times_hist[fps.index++] = frame_time;
    fps.previous_time = time_millisec;

    // Average our time history when we get enough samples:
    if (fps.index == MAX_FPS_HIST)
    {
        int i, total = 0;
        for (i = 0; i < MAX_FPS_HIST; ++i)
        {
            total += fps.times_hist[i];
        }

        if (total == 0)
        {
            total = 1;
        }

        fps.fps_count = (10000 * MAX_FPS_HIST / total);
        fps.fps_count = (fps.fps_count + 5) / 10;
        fps.index = 0;
    }

    // Draw it at the top-right corner of the screen:
    PS2_DrawFill(viddef.width - 65, 3, viddef.width - 10, 15, 0); // A black bg to give the text more contrast.
    PS2_DrawString(viddef.width - 60, 6, va("FPS %u", fps.fps_count));
}

/*
================
PS2_DrawMemTags
================
*/
static void PS2_DrawMemTags(void)
{
    int i;
    int y = 35;
    u32 total = 0;

    for (i = 0; i < MEMTAG_COUNT; ++i)
    {
        total += ps2_mem_tag_counts[i];

        const char * count_str = PS2_FormatMemoryUnit(ps2_mem_tag_counts[i], true);
        const char * formatted_str = va("%-10s %s", ps2_mem_tag_names[i], count_str);

        PS2_DrawString(viddef.width - 170, y, formatted_str);
        y += 12;
    }

    // Total memory currently allocated (approximately):
    PS2_DrawString(viddef.width - 170, y + 6, va("TOTAL: %s", PS2_FormatMemoryUnit(total, true)));

    // A darker background to give the text more contrast.
    PS2_DrawFill(viddef.width - 180, 30, viddef.width - 10, 85, 2);
}

//=============================================================================
//
// Rendering methods exported to the game and engine (refresh exports):
//
//=============================================================================

/*
================
PS2_RendererInit
================
*/
qboolean PS2_RendererInit(void * unused1, void * unused2)
{
    // These were used by the original GL renderer
    // to pass the HWIND and GLRC Windows handles.
    (void)unused1;
    (void)unused2;

    Com_DPrintf("---- PS2_RendererInit ----\n");

    // These can be set from a config file:
    r_ps2_vid_width       = Cvar_Get("r_ps2_vid_width",  va("%d", DEFAULT_VID_WIDTH),  0);
    r_ps2_vid_height      = Cvar_Get("r_ps2_vid_height", va("%d", DEFAULT_VID_HEIGHT), 0);
    r_ps2_ui_brightness   = Cvar_Get("r_ps2_ui_brightness",  "128", 0);
    r_ps2_fade_scr_alpha  = Cvar_Get("r_ps2_fade_scr_alpha", "100", 0);
    r_ps2_show_fps        = Cvar_Get("r_ps2_show_fps",       "1",   0);
    r_ps2_show_mem_tags   = Cvar_Get("r_ps2_show_mem_tags",  "1",   0);

    // Cache these, since on the PS2 we don't have a way of interacting with the console.
    viddef.width          = (int)r_ps2_vid_width->value;
    viddef.height         = (int)r_ps2_vid_height->value;
    ps2ref.ui_brightness  = (u32)r_ps2_ui_brightness->value;
    ps2ref.fade_scr_alpha = (u32)r_ps2_fade_scr_alpha->value;
    ps2ref.show_fps_count = (qboolean)r_ps2_show_fps->value;
    ps2ref.show_mem_tags  = (qboolean)r_ps2_show_mem_tags->value;

    // Renderer id. Used in a couple places by the game.
    vidref_val = VIDREF_OTHER;

    // Reset the VRam pointer to the first address, just to be sure.
    graph_vram_clear();
    ps2ref.vram_used_bytes = 0;

    // Main renderer and video initialization:
    PS2_InitGSBuffers(GRAPH_MODE_AUTO, GS_PSM_32, GS_PSMZ_32, true);
    PS2_InitDrawingEnvironment();

    // Create double buffer render packets:
    //
    // FRAME_PACKET_SIZE is the number of quadwords per render packet
    // in our double buffer. We have two of them, so this adds up to
    // ~2 Megabytes of memory.
    //
    // NOTE: Currently no overflow checking is done, so drawing a
    // very big mesh could potentially crash the renderer!
    //
    enum { FRAME_PACKET_SIZE = 65535 };
    u32 renderer_packet_mem = 0;

    ps2ref.frame_packets[0] = packet_init(FRAME_PACKET_SIZE, PACKET_NORMAL);
    ps2ref.frame_packets[1] = packet_init(FRAME_PACKET_SIZE, PACKET_NORMAL);
    renderer_packet_mem += (ps2ref.frame_packets[0]->qwords << 4);
    renderer_packet_mem += (ps2ref.frame_packets[1]->qwords << 4);

    // Extra packets for texture uploads:
    ps2ref.tex_upload_packet[0] = packet_init(128, PACKET_NORMAL);
    ps2ref.tex_upload_packet[1] = packet_init(128, PACKET_NORMAL);
    renderer_packet_mem += (ps2ref.tex_upload_packet[0]->qwords << 4);
    renderer_packet_mem += (ps2ref.tex_upload_packet[1]->qwords << 4);

    // One small UCAB packet used to send the flip buffer command:
    ps2ref.flip_fb_packet = packet_init(8, PACKET_UCAB);
    renderer_packet_mem += (ps2ref.flip_fb_packet->qwords << 4);

    // Plus the five packet_ts just allocated above.
    renderer_packet_mem += sizeof(packet_t) * 5;
    ps2_mem_tag_counts[MEMTAG_RENDERER] += renderer_packet_mem;

    // Reset these, to be sure...
    ps2ref.current_frame_packet = NULL;
    ps2ref.current_frame_qwptr  = NULL;
    ps2ref.dmatag_draw2d        = NULL;
    ps2ref.current_tex          = NULL;
    ps2ref.frame_index          = 0;
    memset(&fps, 0, sizeof(fps));

    // Init other aux data and default render states:
    ps2ref.prim_desc.type         = PRIM_TRIANGLE;
    ps2ref.prim_desc.shading      = PRIM_SHADE_GOURAUD;
    ps2ref.prim_desc.mapping      = DRAW_ENABLE;
    ps2ref.prim_desc.fogging      = DRAW_DISABLE;
    ps2ref.prim_desc.blending     = DRAW_DISABLE;
    ps2ref.prim_desc.antialiasing = DRAW_ENABLE;
    ps2ref.prim_desc.mapping_type = PRIM_MAP_ST;
    ps2ref.prim_desc.colorfix     = PRIM_UNFIXED;

    // Default tint is white (no tint).
    ps2ref.prim_color.r = 255;
    ps2ref.prim_color.g = 255;
    ps2ref.prim_color.b = 255;
    ps2ref.prim_color.a = 255;
    ps2ref.prim_color.q = 1.0f;

    // Default clear color is black.
    ps2ref.screen_color.r = 0;
    ps2ref.screen_color.g = 0;
    ps2ref.screen_color.b = 0;
    ps2ref.screen_color.a = 255;
    ps2ref.screen_color.q = 1.0f;

    // Gen the default images.
    PS2_TexImageInit();

    // Make sure it's initially set to a valid palette (global_palette).
    PS2_CinematicSetPalette(NULL);

    // And reserve a texture slot for the cinematic frame.
    cinematic_frame.teximage = PS2_TexImageAlloc();

    Com_DPrintf("---- PS2_RendererInit completed! ( %d, %d ) ----\n", viddef.width, viddef.height);
    ps2ref.initialized = true;
    return true;
}

/*
================
PS2_RendererShutdown
================
*/
void PS2_RendererShutdown(void)
{
    int i;

    if (!ps2ref.initialized)
    {
        return;
    }

    // This is necessary to ensure the crash screen works if
    // called between BeginFrame/EndFrame, not quite sure why...
    if (ps2ref.flip_fb_packet != NULL)
    {
        dma_wait_fast();

        framebuffer_t * fb = &ps2ref.frame_buffers[0];
        qword_t * q = ps2ref.flip_fb_packet->data;

        q = draw_framebuffer(q, 0, fb);
        q = draw_finish(q);

        dma_channel_send_normal_ucab(DMA_CHANNEL_GIF, ps2ref.flip_fb_packet->data,
                                     (q - ps2ref.flip_fb_packet->data), 0);
        dma_wait_fast();
    }

    draw_wait_finish();
    graph_wait_vsync();
    graph_shutdown();

    for (i = 0; i < 2; ++i)
    {
        if (ps2ref.frame_packets[i] != NULL)
        {
            packet_free(ps2ref.frame_packets[i]);
        }
    }

    for (i = 0; i < 2; ++i)
    {
        if (ps2ref.tex_upload_packet[i] != NULL)
        {
            packet_free(ps2ref.tex_upload_packet[i]);
        }
    }

    if (ps2ref.flip_fb_packet != NULL)
    {
        packet_free(ps2ref.flip_fb_packet);
    }

    PS2_TexImageShutdown();
    PS2_MemClearObj(&ps2ref);
}

/*
================
PS2_BeginRegistration
================
*/
void PS2_BeginRegistration(const char * map_name)
{
    Com_DPrintf("*** PS2_BeginRegistration: '%s' ***\n", map_name);
    //TODO
}

/*
================
PS2_RegisterModel
================
*/
struct model_s * PS2_RegisterModel(const char * name)
{
    //TODO
    static int dummy1;
    return (struct model_s *)&dummy1;
}

/*
================
PS2_RegisterSkin
================
*/
struct image_s * PS2_RegisterSkin(const char * name)
{
    return (struct image_s *)PS2_TexImageFindOrLoad(name, IT_SKIN);
}

/*
================
PS2_RegisterPic
================
*/
struct image_s * PS2_RegisterPic(const char * name)
{
    return (struct image_s *)PS2_TexImageFindOrLoad(name, IT_PIC | IT_BUILTIN);
}

/*
================
PS2_SetSky
================
*/
void PS2_SetSky(const char * name, float rotate, vec3_t axis)
{
    Com_DPrintf("PS2_SetSky: '%s'\n", name);
    //TODO
    //
    // Looking at ref_gl, it seems that the renderer can decide
    // between using TGA or PCX for the skyboxes. TGA should be
    // a good choice for the PS2.
}

/*
================
PS2_EndRegistration
================
*/
void PS2_EndRegistration(void)
{
    Com_DPrintf("*** PS2_EndRegistration ***\n");
    //TODO
}

/*
================
PS2_BeginFrame
================
*/
void PS2_BeginFrame(float camera_separation)
{
    // This was for GL stereo rendering, which we don't support.
    (void)camera_separation;

    // Make sure we didn't screw ourselves since the last frame:
    if (ps2ref.frame_index > 1 || ps2ref.frame_started == true)
    {
        Sys_Error("BeginFrame: Inconsistent frame states!!!");
    }

    // Reset these perf counters for the new frame:
    perfcnt.draws2d      = 0;
    perfcnt.tex_uploads  = 0;
    perfcnt.pipe_flushes = 0;

    ps2ref.current_frame_packet = ps2ref.frame_packets[ps2ref.frame_index];
    ps2ref.current_frame_qwptr  = ps2ref.current_frame_packet->data;
    ps2ref.frame_started = true;

    // Start the frame with a wiped screen.
    PS2_ClearScreen();
}

/*
================
PS2_EndFrame
================
*/
void PS2_EndFrame(void)
{
    if (ps2ref.frame_started == false)
    {
        Sys_Error("EndFrame: Inconsistent frame states!!!");
    }

    //
    // Finalize pending 2D draws.
    //
    // All of our 2D drawings are batched, so we concentrate
    // 2D draw call here to ensure only one texture switch per
    // image being used by the 2D elements.
    //
    PS2_Draw2DBegin();
    PS2_DrawFullScreenCinematic();

    // Extra debug overlays:
    if (ps2ref.show_fps_count)
    {
        PS2_DrawFPSCounter();
    }
    if (ps2ref.show_mem_tags)
    {
        PS2_DrawMemTags();
    }

    PS2_Flush2DBatch();
    PS2_Draw2DEnd();

    // Add a finish command to the DMA chain:
    BEGIN_DMA_TAG(ps2ref.current_frame_qwptr);
    ps2ref.current_frame_qwptr = draw_finish(ps2ref.current_frame_qwptr);
    END_DMA_TAG_AND_CHAIN(ps2ref.current_frame_qwptr);

    dma_wait_fast();
    dma_channel_send_chain(DMA_CHANNEL_GIF, ps2ref.current_frame_packet->data,
                           (ps2ref.current_frame_qwptr - ps2ref.current_frame_packet->data),
                           0, 0);

    // V-Sync wait:
    graph_wait_vsync();
    draw_wait_finish();

    graph_set_framebuffer_filtered(ps2ref.frame_buffers[ps2ref.frame_index].address,
                                   ps2ref.frame_buffers[ps2ref.frame_index].width,
                                   ps2ref.frame_buffers[ps2ref.frame_index].psm, 0, 0);

    // Switch context:
    //  1 XOR 1 = 0
    //  0 XOR 1 = 1
    ps2ref.frame_index ^= 1;

    // Swap render framebuffer:
    framebuffer_t * fb = &ps2ref.frame_buffers[ps2ref.frame_index];
    qword_t * q = ps2ref.flip_fb_packet->data;

    q = draw_framebuffer(q, 0, fb);
    q = draw_finish(q);

    dma_wait_fast();
    dma_channel_send_normal_ucab(DMA_CHANNEL_GIF, ps2ref.flip_fb_packet->data,
                                 (q - ps2ref.flip_fb_packet->data), 0);
    draw_wait_finish();
    ps2ref.frame_started = false;
}

/*
================
PS2_RenderFrame
================
*/
void PS2_RenderFrame(refdef_t * view_def)
{
    CHECK_FRAME_STARTED();
    //
    // Quake2 default render mode was 2D, only switching to 3D in here
    // (probably in account of the software renderer).
    //

    (void)view_def;
    //TODO
}

/*
================
PS2_TexImageVRamUpload
================
*/
void PS2_TexImageVRamUpload(ps2_teximage_t * teximage)
{
    // Can be called outside Begin/End frame.

    if (ps2ref.current_tex == teximage) // Already in VRam.
    {
        return;
    }

    //
    // Upload the texture to GS VRam.
    // Since we are using almost all of our video memory with framebuffers
    // and z-buffer, the rest of it can only fit a single large RGBA texture.
    //

    int width;
    int height;
    if (!TEXIMAGE_IS_SCRAP(teximage))
    {
        width  = teximage->width;
        height = teximage->height;
    }
    else
    {
        width  = MAX_TEXIMAGE_SIZE;
        height = MAX_TEXIMAGE_SIZE;
    }

    packet_t * packet = ps2ref.tex_upload_packet[ps2ref.frame_index];
    qword_t * q = packet->data;

    q = draw_texture_transfer(q, teximage->pic, width, height,
                              teximage->texbuf.psm, teximage->texbuf.address, width);
    q = draw_texture_flush(q);

    dma_channel_send_chain(DMA_CHANNEL_GIF, packet->data, (q - packet->data), 0, 0);
    dma_wait_fast();

    ps2ref.current_tex = teximage;
    perfcnt.tex_uploads++;
}

/*
================
PS2_TexImageBindCurrent()
Sets the current texture in VRam as the
one to sample from on subsequent draw calls.
================
*/
void PS2_TexImageBindCurrent(void)
{
    CHECK_FRAME_STARTED();

    if (ps2ref.current_tex == NULL)
    {
        return;
    }

    lod_t         lod;
    clutbuffer_t  clut;
    texbuffer_t   tmp_texbuf;
    texbuffer_t * p_texbuf;

    // Use defaults for most of the parameters:
    lod.mag_filter    = ps2ref.current_tex->mag_filter;
    lod.min_filter    = ps2ref.current_tex->min_filter;
    lod.calculation   = LOD_USE_K;
    lod.max_level     = 0;
    lod.l             = 0;
    lod.k             = 0;
    clut.address      = 0;
    clut.psm          = 0;
    clut.start        = 0;
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.load_method  = CLUT_NO_LOAD;

    if (!TEXIMAGE_IS_SCRAP(ps2ref.current_tex))
    {
        p_texbuf = &ps2ref.current_tex->texbuf;
    }
    else
    {
        // Need a custom texbuf for the scrap, since the width/height are not
        // the size of the backing atlas texture, but the size of the tile.
        byte size_log2 = draw_log2(MAX_TEXIMAGE_SIZE);

        tmp_texbuf.address         = ps2ref.vram_texture_start;
        tmp_texbuf.width           = MAX_TEXIMAGE_SIZE;
        tmp_texbuf.psm             = GS_PSM_32;
        tmp_texbuf.info.width      = size_log2;
        tmp_texbuf.info.height     = size_log2;
        tmp_texbuf.info.components = TEXTURE_COMPONENTS_RGBA;
        tmp_texbuf.info.function   = TEXTURE_FUNCTION_MODULATE;

        p_texbuf = &tmp_texbuf;
    }

    ps2ref.current_frame_qwptr = draw_texture_sampling(ps2ref.current_frame_qwptr, 0, &lod);
    ps2ref.current_frame_qwptr = draw_texturebuffer(ps2ref.current_frame_qwptr, 0, p_texbuf, &clut);
}

/*
================
PS2_DrawGetPicSize
================
*/
void PS2_DrawGetPicSize(int * w, int * h, const char * name)
{
    // This one can be called outside Begin/End frame.
    ps2_teximage_t * teximage = PS2_TexImageFindOrLoad(name, IT_PIC | IT_BUILTIN);
    if (teximage == NULL)
    {
        *w = *h = -1;
        return;
    }
    *w = teximage->width;
    *h = teximage->height;
}

/*
================
PS2_DrawPic
================
*/
void PS2_DrawPic(int x, int y, const char * name)
{
    CHECK_FRAME_STARTED();

    ps2_teximage_t * teximage = PS2_TexImageFindOrLoad(name, IT_PIC | IT_BUILTIN);
    if (teximage == NULL)
    {
        Com_DPrintf("Can't find or load pic: %s\n", name);
        return;
    }
    PS2_DrawTexImage(x, y, teximage);
}

/*
================
PS2_DrawStretchPic
================
*/
void PS2_DrawStretchPic(int x, int y, int w, int h, const char * name)
{
    CHECK_FRAME_STARTED();

    ps2_teximage_t * teximage = PS2_TexImageFindOrLoad(name, IT_PIC | IT_BUILTIN);
    if (teximage == NULL)
    {
        Com_DPrintf("Can't find or load pic: %s\n", name);
        return;
    }
    PS2_DrawStretchTexImage(x, y, w, h, teximage);
}

/*
================
PS2_DrawChar
================
*/
enum { GLYPH_SIZE = 8 }; // size in pixels
void PS2_DrawChar(int x, int y, int c)
{
    // Draws one 8*8 graphics character with 0 being transparent.
    // It can be clipped to the top of the screen to allow the console
    // to be smoothly scrolled off. Based on Draw_Char() from ref_gl.

    CHECK_FRAME_STARTED();

    c &= 255;

    if ((c & 127) == ' ')
    {
        return; // Whitespace
    }
    if (y <= -8)
    {
        return; // Totally off screen
    }
    if (next_in_2d_batch == DRAW2D_BATCH_SIZE)
    {
        return; // No more space this frame!
    }

    int row, col;
    row = (c >> 4) * GLYPH_SIZE;
    col = (c & 15) * GLYPH_SIZE;

    screen_quad_t * quad = DRAW2D_NEXT_BATCH_ELEMENT();

    quad->z_index   = DRAW2D_NEXT_Z_INDEX();
    quad->tex_index = TEXIMAGE_INDEX(builtin_tex_conchars);

    quad->x0 = x;
    quad->y0 = y;
    quad->x1 = x + GLYPH_SIZE;
    quad->y1 = y + GLYPH_SIZE;

    quad->u0 = col;
    quad->v0 = row;
    quad->u1 = col + GLYPH_SIZE;
    quad->v1 = row + GLYPH_SIZE;

    // This will behave differently if texture func is
    // TEXTURE_FUNCTION_MODULATE or TEXTURE_FUNCTION_DECAL!
    quad->r = (byte)ps2ref.ui_brightness;
    quad->g = (byte)ps2ref.ui_brightness;
    quad->b = (byte)ps2ref.ui_brightness;
    quad->a = 255;

    perfcnt.draws2d++;
}

/*
================
PS2_DrawString
================
*/
void PS2_DrawString(int x, int y, const char * s)
{
    int initial_x = x;
    while (*s != '\0')
    {
        PS2_DrawChar(x, y, *s);
        x += GLYPH_SIZE;
        if (*s == '\n')
        {
            y += GLYPH_SIZE + 2;
            x = initial_x;
        }
        ++s;
    }
}

/*
================
PS2_DrawAltString
================
*/
void PS2_DrawAltString(int x, int y, const char * s)
{
    int initial_x = x;
    while (*s != '\0')
    {
        PS2_DrawChar(x, y, *s ^ 0x80); // With this it'll hit the index of a green char (in conchars.pcx).
        x += GLYPH_SIZE;
        if (*s == '\n')
        {
            y += GLYPH_SIZE + 2;
            x = initial_x;
        }
        ++s;
    }
}

/*
================
PS2_DrawTileClear
================
*/
void PS2_DrawTileClear(int x, int y, int w, int h, const char * name)
{
    CHECK_FRAME_STARTED();

    if (next_in_2d_batch == DRAW2D_BATCH_SIZE)
    {
        return; // No more space this frame!
    }

    ps2_teximage_t * teximage = PS2_TexImageFindOrLoad(name, IT_PIC | IT_BUILTIN);
    if (teximage == NULL)
    {
        Com_DPrintf("Can't find or load pic: %s\n", name);
        return;
    }

    screen_quad_t * quad = DRAW2D_NEXT_BATCH_ELEMENT();

    quad->z_index   = DRAW2D_NEXT_Z_INDEX();
    quad->tex_index = TEXIMAGE_INDEX(teximage);

    quad->x0 = x;
    quad->y0 = y;
    quad->x1 = x + w;
    quad->y1 = y + h;

    quad->u0 = x;
    quad->v0 = y;
    quad->u1 = x + w;
    quad->v1 = y + h;

    // This will behave differently if texture func is
    // TEXTURE_FUNCTION_MODULATE or TEXTURE_FUNCTION_DECAL!
    quad->r = (byte)ps2ref.ui_brightness;
    quad->g = (byte)ps2ref.ui_brightness;
    quad->b = (byte)ps2ref.ui_brightness;
    quad->a = 255;

    perfcnt.draws2d++;
}

/*
================
PS2_DrawFill
================
*/
void PS2_DrawFill(int x, int y, int w, int h, int c)
{
    CHECK_FRAME_STARTED();

    if ((u32)c > 255)
    {
        Sys_Error("PS2_DrawFill: Bad color index %d!", c);
    }

    screen_quad_t * quad;

    // Full screen wipe with black (happens on pause and menus), treat it as a screen fade.
    if (c == 0 && x == 0 && y == 0 && w == viddef.width && h == viddef.height)
    {
        if (fade_scr_index >= 0)
        {
            return; // Already issued one this frame!
        }

        quad = DRAW2D_NEXT_BATCH_ELEMENT();

        quad->z_index   = DRAW2D_NEXT_Z_INDEX();
        quad->tex_index = DRAW2D_TEX_INDEX_FADE_SCR;

        quad->x0 = x;
        quad->y0 = y;
        quad->x1 = x + w;
        quad->y1 = y + h;

        quad->r = 0;
        quad->g = 0;
        quad->b = 0;
        quad->a = 255;

        fade_scr_index = (next_in_2d_batch - 1);
    }
    else // Smaller screen element:
    {
        if (next_in_2d_batch == DRAW2D_BATCH_SIZE)
        {
            return; // No more space this frame!
        }

        quad = DRAW2D_NEXT_BATCH_ELEMENT();

        quad->z_index   = DRAW2D_NEXT_Z_INDEX();
        quad->tex_index = DRAW2D_TEX_INDEX_NO_TEX;

        quad->x0 = x;
        quad->y0 = y;
        quad->x1 = x + w;
        quad->y1 = y + h;

        u32 color = global_palette[c];
        quad->r = (color & 0xFF);
        quad->g = (color >>  8) & 0xFF;
        quad->b = (color >> 16) & 0xFF;
        quad->a = 255;
    }

    perfcnt.draws2d++;
}

/*
================
PS2_DrawFadeScreen
================
*/
void PS2_DrawFadeScreen(void)
{
    CHECK_FRAME_STARTED();

    if (fade_scr_index >= 0)
    {
        return; // Already issued one this frame!
    }

    screen_quad_t * quad = DRAW2D_NEXT_BATCH_ELEMENT();

    quad->z_index   = DRAW2D_NEXT_Z_INDEX();
    quad->tex_index = DRAW2D_TEX_INDEX_FADE_SCR;

    quad->x0 = 0;
    quad->y0 = 0;
    quad->x1 = viddef.width;
    quad->y1 = viddef.height;

    quad->r = 0;
    quad->g = 0;
    quad->b = 0;
    quad->a = (byte)ps2ref.fade_scr_alpha;

    fade_scr_index = (next_in_2d_batch - 1);
    perfcnt.draws2d++;
}

/*
================
PS2_DrawStretchRaw
================
*/
void PS2_DrawStretchRaw(int x, int y, int w, int h, int cols, int rows, const byte * data)
{
    CHECK_FRAME_STARTED();

    //
    // This function is only used by Quake2 to draw the cinematic frames, nothing else,
    // so it could have a better name... We'll optimize for that and assume this is not
    // a generic "draw pixels" kind of function.
    //

    int i, j;
    int row, trows;
    int frac, fracstep;
    float hscale;

    const byte * source;
    u16 * dest;

    u32 color;
    byte r, g, b, a;

    if (rows <= MAX_TEXIMAGE_SIZE)
    {
        hscale = 1;
        trows = rows;
    }
    else
    {
        hscale = (float)rows / (float)MAX_TEXIMAGE_SIZE;
        trows = MAX_TEXIMAGE_SIZE;
    }

    // Good idea to clear the buffer first, in case the
    // next upsampling doesn't fill the whole thing.
    memset(cinematic_buffer, 0, sizeof(cinematic_buffer));

    // Upsample to fill our 256*256 cinematic buffer.
    // This is based on the algorithm applied by ref_gl.
    for (i = 0; i < trows; i++)
    {
        row = (int)(i * hscale);
        if (row > rows)
        {
            break;
        }

        source   = data + cols * row;
        dest     = &cinematic_buffer[i * MAX_TEXIMAGE_SIZE];
        fracstep = cols * 0x10000 / MAX_TEXIMAGE_SIZE;
        frac     = fracstep >> 1;

        for (j = 0; j < MAX_TEXIMAGE_SIZE; j++)
        {
            color = cinematic_palette[source[frac >> 16]];

            r = (color & 0xFF);
            g = (color >>  8) & 0xFF;
            b = (color >> 16) & 0xFF;
            a = (color >> 24) & 0xFF;

            dest[j] = ((a & 0x1) << 15) | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3); // Pack RGBA-16
            frac += fracstep;
        }
    }

    // Reset the texture parameters (notice we use linear filtering for smoother sampling):
    PS2_TexImageSetup(cinematic_frame.teximage, "cinematic_frame", MAX_TEXIMAGE_SIZE,
                      MAX_TEXIMAGE_SIZE, TEXTURE_COMPONENTS_RGB, TEXTURE_FUNCTION_MODULATE,
                      GS_PSM_16, LOD_MAG_LINEAR, LOD_MIN_LINEAR, IT_BUILTIN, (byte *)cinematic_buffer);

    // Save these for drawing later.
    cinematic_frame.x = x;
    cinematic_frame.y = y;
    cinematic_frame.w = w;
    cinematic_frame.h = h;
    cinematic_frame.draw_pending = true;
}

/*
================
PS2_DrawTexImage
================
*/
void PS2_DrawTexImage(int x, int y, ps2_teximage_t * teximage)
{
    PS2_DrawStretchTexImage(x, y, teximage->width, teximage->height, teximage);
}

/*
================
PS2_DrawStretchTexImage
================
*/
void PS2_DrawStretchTexImage(int x, int y, int w, int h, ps2_teximage_t * teximage)
{
    CHECK_FRAME_STARTED();

    if (next_in_2d_batch == DRAW2D_BATCH_SIZE)
    {
        return; // No more space this frame!
    }

    screen_quad_t * quad = DRAW2D_NEXT_BATCH_ELEMENT();

    quad->z_index   = DRAW2D_NEXT_Z_INDEX();
    quad->tex_index = TEXIMAGE_INDEX(teximage);

    quad->x0 = x;
    quad->y0 = y;
    quad->x1 = x + w;
    quad->y1 = y + h;

    if (!TEXIMAGE_IS_SCRAP(teximage))
    {
        quad->u0 = 0;
        quad->v0 = 0;
        quad->u1 = teximage->width;
        quad->v1 = teximage->height;
    }
    else // The UVs will be non-zero for a scrap atlas texture.
    {
        quad->u0 = teximage->u0;
        quad->v0 = teximage->v0;
        quad->u1 = teximage->u1;
        quad->v1 = teximage->v1;
    }

    // This will behave differently if texture func is
    // TEXTURE_FUNCTION_MODULATE or TEXTURE_FUNCTION_DECAL!
    quad->r = (byte)ps2ref.ui_brightness;
    quad->g = (byte)ps2ref.ui_brightness;
    quad->b = (byte)ps2ref.ui_brightness;
    quad->a = 255;

    perfcnt.draws2d++;
}

/*
================
PS2_CinematicSetPalette
================
*/
void PS2_CinematicSetPalette(const byte * restrict palette)
{
    if (palette == NULL)
    {
        memcpy(cinematic_palette, global_palette, sizeof(cinematic_palette));
        return;
    }

    int i;
    byte * restrict dest = (byte *)cinematic_palette;

    for (i = 0; i < 256; i++)
    {
        dest[(i * 4) + 0] = palette[(i * 3) + 0];
        dest[(i * 4) + 1] = palette[(i * 3) + 1];
        dest[(i * 4) + 2] = palette[(i * 3) + 2];
        dest[(i * 4) + 3] = 0xFF;
    }
}

/*
================
PS2_AppActivate
================
*/
void PS2_AppActivate(qboolean activate)
{
    // I don't think there's really anything to be done here.
    // The app never goes inactive on the PS2.
    (void)activate;
}

// Cleanup the namespace:
#undef DRAW2D_NEXT_Z_INDEX
#undef DRAW2D_NEXT_BATCH_ELEMENT
#undef TEXIMAGE_INDEX
#undef TEXIMAGE_IS_SCRAP
#undef CHECK_FRAME_STARTED
#undef BEGIN_DMA_TAG
#undef END_DMA_TAG
#undef END_DMA_TAG_AND_CHAIN
#undef BEGIN_DMA_TAG_NAMED
#undef END_DMA_TAG_NAMED

