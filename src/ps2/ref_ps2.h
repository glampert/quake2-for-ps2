
/* ================================================================================================
 * -*- C -*-
 * File: ref_ps2.h
 * Author: Guilherme R. Lampert
 * Created on: 17/10/15
 * Brief: Implementation of Quake2 "refresh" module (renderer module) for the PS2.
 *
 * This source code is released under the GNU GPL v2 license.
 * Check the accompanying LICENSE file for details.
 * ================================================================================================ */

#ifndef PS2_REFRESH_H
#define PS2_REFRESH_H

#include "client/client.h"

// PS2DEV SDK:
#include <tamtypes.h>
#include <gs_psm.h>
#include <graph.h>
#include <draw.h>
#include <draw3d.h>
#include <packet.h>

/*
==============================================================

PS2 renderer API types:

==============================================================
*/

// Misc renderer constants.
enum
{
    // 640x448 is the standard NTSC resolution.
    DEFAULT_VID_WIDTH  = 640,
    DEFAULT_VID_HEIGHT = 448,

    // Upper texture image limits:
    MAX_TEXIMAGES      = 1024,
    MAX_TEXIMAGE_SIZE  = 256
};

// Type tag for textures/images (used internally Quake2).
// These can be ORed for image search criteria.
typedef enum
{
    IT_SKIN    = (1 << 1),
    IT_SPRITE  = (1 << 2),
    IT_WALL    = (1 << 3),
    IT_SKY     = (1 << 4),
    IT_PIC     = (1 << 5),
    IT_BUILTIN = (1 << 6)
} ps2_imagetype_t;

// A texture or 2D image:
typedef struct
{
    byte *          pic;                       // Pointer to heap memory, must be freed with PS2_TexImageFree.
    ps2_imagetype_t type;                      // Types of textures used by Quake. See ps2_imagetype_t.
    u16             width;                     // Width in pixels;  Must be > 0 && <= MAX_TEXIMAGE_SIZE.
    u16             height;                    // Height in pixels; Must be > 0 && <= MAX_TEXIMAGE_SIZE.
    u16             mag_filter;                // One of the LOD_MAG_* from libdraw.
    u16             min_filter;                // One of the LOD_MIN_* from libdraw.
    u16             u0, v0;                    // Offsets into the scrap if this is allocate from the scrap, zero otherwise.
    u16             u1, v1;                    // If not zero, this is a scrap image. In such case, use these instead of w&h.
    texbuffer_t     texbuf;                    // GS texture buffer info from libdraw.
    u32             hash;                      // Hash of the following string, for faster lookup.
    char            name[MAX_QPATH];           // Name or id. If from a file, game path including extension.
} ps2_teximage_t;

// Common renderer state structure (AKA the "refresh" module):
typedef struct
{
    qboolean         initialized;              // Flag to keep track of initialization of our global ps2ref.
    qboolean         show_fps_count;           // Draws a frames per sec counter in the top-right corner of the screen.
    qboolean         frame_started;            // Set by BeginFrame, cleared at EndFrame. Some calls must be in between.
    zbuffer_t        z_buffer;                 // PS2 depth buffer information.
    framebuffer_t    frame_buffers[2];         // PS2 color framebuffers (double buffered).
    packet_t *       frame_packets[2];         // Big packets for all the drawings in a frame.
    packet_t *       tex_upload_packet[2];     // Scratch packets used only for texture uploads to VRam.
    packet_t *       flip_fb_packet;           // Small UCAB packet used to flip the frame buffers.
    packet_t *       current_frame_packet;     // One of the frame_packets[] pointers for current frame.
    qword_t  *       current_frame_qwptr;      // Pointer to the qword_ts of current_frame_packet.
    qword_t  *       dmatag_draw2d;            // Gif tag used by the 2D mode.
    prim_t           prim_desc;                // Misc 3D rendering params.
    color_t          prim_color;               // Color tint of 3D primitives.
    color_t          screen_color;             // Color used to wipe the screen on BeginFrame.
    u32              ui_brightness;            // From 0 to 255, defines the color added to the 2D UI textures, such as text.
    u32              fade_scr_alpha;           // How dark to fade the screen: 255=totally black, 0=no fade.
    u32              frame_index;              // Index of the current frame buffer.
    u32              vram_used_bytes;          // Bytes of VRam currently committed.
    u32              vram_texture_start;       // Start of VRam after screen buffers where we can alloc textures.
    u32              num_teximages;            // Total textures used in the teximages array.
    ps2_teximage_t * current_tex;              // Pointer to the current game texture in VRam (points to teximages[]).
    ps2_teximage_t   teximages[MAX_TEXIMAGES]; // All the textures used by a game level must fit in here!
} ps2_refresh_t;

// Renderer perf counters for debugging and profiling:
typedef struct
{
    u32 draws2d;
    u32 tex_uploads;
    u32 scrap_allocs;
    u32 pipe_flushes;
} ps2_perf_counters_t;

// Renderer globals:
extern int vidref_val;
extern viddef_t viddef;
extern ps2_refresh_t ps2ref;
extern ps2_perf_counters_t perfcnt;

// Palette used to expand the 8bits textures to RGBA-32. Imported from colormap.pcx.
extern const u32 global_palette[256];

// Built-in texture images that are always available:
extern ps2_teximage_t * builtin_tex_conchars;
extern ps2_teximage_t * builtin_tex_conback;
extern ps2_teximage_t * builtin_tex_inventory;
extern ps2_teximage_t * builtin_tex_help;
extern ps2_teximage_t * builtin_tex_backtile;
extern ps2_teximage_t * builtin_tex_debug;

/*
==============================================================

PS2 renderer API functions:

==============================================================
*/

/*
 * Init/shutdown:
 */

qboolean PS2_RendererInit(void * unused1, void * unused2);
void PS2_RendererShutdown(void);

/*
 * Level loading / resource allocation:
 */

void PS2_BeginRegistration(const char * map_name);
void PS2_EndRegistration(void);
void PS2_SetSky(const char * name, float rotate, vec3_t axis);

struct model_s * PS2_RegisterModel(const char * name);
struct image_s * PS2_RegisterSkin(const char * name);
struct image_s * PS2_RegisterPic(const char * name);

/*
 * Frame rendering (3D stuff):
 */

void PS2_BeginFrame(float camera_separation);
void PS2_EndFrame(void);
void PS2_RenderFrame(refdef_t * view_def);

/*
 * 2D overlay rendering (origin at the top-left corner of the screen):
 */

void PS2_DrawGetPicSize(int * w, int * h, const char * name);
void PS2_DrawPic(int x, int y, const char * name);
void PS2_DrawStretchPic(int x, int y, int w, int h, const char * name);
void PS2_DrawStretchRaw(int x, int y, int w, int h, int cols, int rows, const byte * data);
void PS2_DrawTileClear(int x, int y, int w, int h, const char * name);

void PS2_DrawTexImage(int x, int y, ps2_teximage_t * teximage);
void PS2_DrawStretchTexImage(int x, int y, int w, int h, ps2_teximage_t * teximage);

void PS2_DrawChar(int x, int y, int c);
void PS2_DrawString(int x, int y, const char * s);
void PS2_DrawAltString(int x, int y, const char * s);

void PS2_DrawFill(int x, int y, int w, int h, int c);
void PS2_DrawFadeScreen(void);

/*
 * Miscellaneous:
 */

void PS2_CinematicSetPalette(const byte * restrict palette);
void PS2_AppActivate(qboolean activate);

/*
 * PS2 Texture image setup/management:
 */

void PS2_TexImageInit(void);
void PS2_TexImageShutdown(void);

ps2_teximage_t * PS2_TexImageAlloc(void);
ps2_teximage_t * PS2_TexImageFindOrLoad(const char * name, int flags);

void PS2_TexImageVRamUpload(ps2_teximage_t * teximage);
void PS2_TexImageBindCurrent(void);

void PS2_TexImageSetup(ps2_teximage_t * teximage, const char * name, int w, int h, int components,
                       int func, int psm, int mag_filter, int min_filter, ps2_imagetype_t type, byte * pic);
void PS2_TexImageFree(ps2_teximage_t * teximage);

// Scrap texture atlas:
ps2_teximage_t * Scrap_AllocBlock(const byte * pic8in, int w, int h, const char * pic_name);

/*
 * Image loading for textures/sprites:
 */

qboolean PCX_LoadFromMemory(const char * filename, const byte * data, int data_len,
                            byte ** pic, u32 * palette, int * width, int * height);
qboolean PCX_LoadFromFile(const char * filename, byte ** pic,
                          u32 * palette, int * width, int * height);

/*
 * Convert from 8bit palettized to RGB[A]:
 */

void Img_UnPalettize32(int width, int height, const byte * restrict pic8in, // Palettized 8bits to RGBA 32bits.
                       const u32 * restrict palette, byte * restrict pic32out);
void Img_UnPalettize24(int width, int height, const byte * restrict pic8in, // Palettized 8bits to RGB 24bits.
                       const u32 * restrict palette, byte * restrict pic24out);
void Img_UnPalettize16(int width, int height, const byte * restrict pic8in, // Palettized 8bits to RGBA 16bits (1-5-5-5).
                       const u32 * restrict palette, byte * restrict pic16out);

#endif // PS2_REFRESH_H
