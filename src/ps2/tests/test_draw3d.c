
/* ================================================================================================
 * -*- C -*-
 * File: test_draw3d.c
 * Author: Guilherme R. Lampert
 * Created on: 19/01/16
 * Brief: Tests for the 3D drawing functions of the Refresh module and the Vector Unit 1.
 *
 * This source code is released under the GNU GPL v2 license.
 * Check the accompanying LICENSE file for details.
 * ================================================================================================ */

#include "client/client.h"
#include "ps2/ref_ps2.h"
#include "ps2/mem_alloc.h"
#include "ps2/vu1.h"
#include "ps2/gs_defs.h"
#include "ps2/math_funcs.h"
#include "ps2/vec_mat.h"

// Functions exported from this file:
void Test_PS2_VU1Triangle(void);
void Test_PS2_VU1Cubes(void);

//=============================================================================
//
// Local shared data / helpers:
//
//=============================================================================

// The VU1 program generated from color_triangles.vcl
extern void VU1Prog_Color_Triangles_CodeStart VU_DATA_SECTION;
extern void VU1Prog_Color_Triangles_CodeEnd   VU_DATA_SECTION;

// GS vertex format for our triangles (reglist):
static const u64 VERTEX_FORMAT = (((u64)GS_REG_RGBAQ) << 0) | (((u64)GS_REG_XYZ2) << 4);

// Number of elements in a vertex; (color + position) in our case:
static const int NUM_VERTEX_ELEMENTS = 2;

// Simple struct to hold the temporary DMA buffer we need
// to send per draw list info to the Vector Unit 1.
typedef struct
{
    // We send one matrix and 2 quadwords for each draw list.
    byte buffer[sizeof(m_mat4_t) + sizeof(m_vec4_t) * 2] __attribute__((aligned(16)));
    byte * ptr;
} draw_data_t;

// ---------------------
//  draw_data_t helpers
// ---------------------

static inline void DrawDataReset(draw_data_t * dd)
{
    dd->ptr = dd->buffer;
}

static inline int DrawDataGetQWordSize(const draw_data_t * dd)
{
    return (dd->ptr - dd->buffer) >> 4;
}

static inline void DrawDataAddMatrix(draw_data_t * dd, const m_mat4_t * m)
{
    // Copy the matrix as-is:
    memcpy(dd->ptr, m, sizeof(*m));
    dd->ptr += sizeof(*m);
}

static inline void DrawDataAddScaleFactors(draw_data_t * dd)
{
    // GS rasterizer scale factors follow the MVP matrix:
    float * scale_vec = (float *)dd->ptr;
    scale_vec[0] = 2048.0f;
    scale_vec[1] = 2048.0f;
    scale_vec[2] = ((float)0xFFFFFF) / 32.0f;
    scale_vec[3] = 1.0f; // unused
    dd->ptr += sizeof(float) * 4;
}

static inline void DrawDataAddVertexCount(draw_data_t * dd, u32 vert_count, u32 dest_address)
{
    // Vertex count and output address (expanded into a quadword):
    u32 * counts = (u32 *)dd->ptr;
    counts[0] = dest_address; // x
    counts[1] = vert_count;   // y
    counts[2] = 0;            // unused
    counts[3] = 0;            // unused
    dd->ptr += sizeof(u32) * 4;
}

static inline int CountVertexLoops(int vertex_qwords, int num_regs)
{
    const float lpq = 2.0f / (float)num_regs;
    return (int)(vertex_qwords * lpq);
}

//=============================================================================
//
// Test_PS2_VU1Triangle -- Draws a rotating triangle using the
// Vector Unit 1 (VU1) hardware accelerated rendering path.
//
//=============================================================================

static void DrawVU1Triangle(draw_data_t * dd, const m_mat4_t * mvp)
{
    DrawDataReset(dd);
    DrawDataAddMatrix(dd, mvp);
    DrawDataAddScaleFactors(dd);
    DrawDataAddVertexCount(dd, 3, 20); // 1 triangle = 3 verts; Write output beginning at qword address 20

    VU1_Begin();

    // Matrix/scales will be uploaded at address 0 in VU memory.
    const int draw_data_qwsize = DrawDataGetQWordSize(dd);
    VU1_ListData(0, dd->buffer, draw_data_qwsize);

    // Data added from here will begin at address 6, right after the MVP, fScales and vert count:
    VU1_ListAddBegin(draw_data_qwsize);

    // The GIF Tag and primitive description:
    const int vert_loops = CountVertexLoops(/* vertex_qwords = */ 6, NUM_VERTEX_ELEMENTS);
    const u64 prim_info  = GS_PRIM(GS_PRIM_TRIANGLE, GS_PRIM_SFLAT, GS_PRIM_TOFF, GS_PRIM_FOFF, GS_PRIM_ABOFF, GS_PRIM_AAON, GS_PRIM_FSTQ, GS_PRIM_C1, 0);
    const u64 gif_tag    = GS_GIFTAG(vert_loops, 1, 1, prim_info, GS_GIFTAG_PACKED, NUM_VERTEX_ELEMENTS);
    VU1_ListAdd128(gif_tag, VERTEX_FORMAT);

    //
    // Vertex format:
    // Color (RGBA bytes)
    // Position (X,Y,Z,W floats)
    //

    // Vertex 0:
    VU1_ListAdd32(50);
    VU1_ListAdd32(50);
    VU1_ListAdd32(127);
    VU1_ListAdd32(127);
    VU1_ListAddFloat(-1.0f);
    VU1_ListAddFloat(1.0f);
    VU1_ListAddFloat(3.0f);
    VU1_ListAddFloat(1.0f);

    // Vertex 1:
    VU1_ListAdd32(50);
    VU1_ListAdd32(50);
    VU1_ListAdd32(127);
    VU1_ListAdd32(127);
    VU1_ListAddFloat(-1.0f);
    VU1_ListAddFloat(-1.0f);
    VU1_ListAddFloat(3.0f);
    VU1_ListAddFloat(1.0f);

    // Vertex 2:
    VU1_ListAdd32(50);
    VU1_ListAdd32(50);
    VU1_ListAdd32(127);
    VU1_ListAdd32(127);
    VU1_ListAddFloat(0.5f);
    VU1_ListAddFloat(0.5f);
    VU1_ListAddFloat(3.0f);
    VU1_ListAddFloat(1.0f);

    // 13 qwords total so far (verts + matrix/scales draw data)
    VU1_ListAddEnd();

    // End the list and start the VU program (located in micromem address 0)
    VU1_End(0);
}

void Test_PS2_VU1Triangle(void)
{
    Com_Printf("====== QPS2 - Test_PS2_VU1Triangle ======\n");

    // The scene viewer (not moving):
    const m_vec4_t camera_origin = { 0.0,  0.0f, -1.0f,  1.0f };
    const m_vec4_t camera_lookat = { 0.0,  0.0f,  1.0f,  1.0f };
    const m_vec4_t camera_up     = { 0.0,  1.0f,  0.0f,  1.0f };

    // Render matrices:
    m_mat4_t proj_matrix;
    m_mat4_t view_matrix;
    m_mat4_t view_proj_matrix;
    m_mat4_t mvp_matrix;
    m_mat4_t translation_matrix;
    m_mat4_t rotation_matrix;
    m_mat4_t model_matrix;

    // Clear the screen to dark gray. Default color is black.
    ps2ref.screen_color.r = 120;
    ps2ref.screen_color.g = 120;
    ps2ref.screen_color.b = 120;

    // Upload the VU1 microprogram:
    VU1_UploadProg(0, &VU1Prog_Color_Triangles_CodeStart, &VU1Prog_Color_Triangles_CodeEnd);

    // Set up the matrices; these never change in this test program:
    Mat4_MakeLookAt(&view_matrix, &camera_origin, &camera_lookat, &camera_up);
    Mat4_MakePerspProjection(&proj_matrix, ps2_deg_to_rad(60.0f), 4.0f / 3.0f, viddef.width, viddef.height, 2.0f, 1000.0f, 4096.0f);
    Mat4_Multiply(&view_proj_matrix, &view_matrix, &proj_matrix);

    // Temp buffer for DMA upload of the renderer matrix, etc.
    draw_data_t dd = {0};

    // Rotate the model, so it looks a little less boring.
    float rotation_angle = 0.0f; // In radians

    for (;;)
    {
        PS2_BeginFrame(0);

        Mat4_MakeRotationZ(&rotation_matrix, rotation_angle);
        Mat4_MakeTranslation(&translation_matrix, 1.0f, 0.0f, 0.0f);

        Mat4_Multiply(&model_matrix, &rotation_matrix, &translation_matrix);
        Mat4_Multiply(&mvp_matrix, &model_matrix, &view_proj_matrix);

        DrawVU1Triangle(&dd, &mvp_matrix);
        rotation_angle += 0.02f;

        PS2_EndFrame();
    }
}

//=============================================================================
//
// Test_PS2_VU1Cubes -- Draws a couple rotating cubes using
// Vector Unit 1 (VU1) hardware accelerated rendering path.
//
//=============================================================================

static const int CUBE_VERT_COUNT  = 24;
static const int CUBE_INDEX_COUNT = 36;
typedef struct
{
    m_vec4_t * vertexes;
    u16      * indexes;
} cube_t;

static void MakeCube(cube_t * cube, const float scale)
{
    // Face indexes:
    const u16 cubeF[6][4] = {
        { 0, 1, 5, 4 },
        { 4, 5, 6, 7 },
        { 7, 6, 2, 3 },
        { 1, 0, 3, 2 },
        { 1, 2, 6, 5 },
        { 0, 4, 7, 3 }
    };

    // Positions/vertexes:
    const float cubeV[8][3] = {
        { -0.5f, -0.5f, -0.5f },
        { -0.5f, -0.5f,  0.5f },
        {  0.5f, -0.5f,  0.5f },
        {  0.5f, -0.5f, -0.5f },
        { -0.5f,  0.5f, -0.5f },
        { -0.5f,  0.5f,  0.5f },
        {  0.5f,  0.5f,  0.5f },
        {  0.5f,  0.5f, -0.5f }
    };

    cube->vertexes = (m_vec4_t *)PS2_MemAllocAligned(16, CUBE_VERT_COUNT  * sizeof(m_vec4_t), MEMTAG_MISC);
    cube->indexes  = (u16      *)PS2_MemAllocAligned(16, CUBE_INDEX_COUNT * sizeof(u16),      MEMTAG_MISC);

    // Fill in the data:
    m_vec4_t * vertex_ptr = cube->vertexes;
    u16      * face_ptr   = cube->indexes;

    // 'i' iterates over the faces, 2 triangles per face:
    int i, j;
    u16 vert_index = 0;
    for (i = 0; i < 6; ++i, vert_index += 4)
    {
        for (j = 0; j < 4; ++j)
        {
            vertex_ptr->x = cubeV[cubeF[i][j]][0] * scale;
            vertex_ptr->y = cubeV[cubeF[i][j]][1] * scale;
            vertex_ptr->z = cubeV[cubeF[i][j]][2] * scale;
            vertex_ptr->w = 1.0f;
            ++vertex_ptr;
        }

        face_ptr[0] = vert_index;
        face_ptr[1] = vert_index + 1;
        face_ptr[2] = vert_index + 2;
        face_ptr += 3;
        face_ptr[0] = vert_index + 2;
        face_ptr[1] = vert_index + 3;
        face_ptr[2] = vert_index;
        face_ptr += 3;
    }
}

static void DrawVU1Cube(draw_data_t * dd, const m_mat4_t * mvp, const cube_t * cube)
{
    DrawDataReset(dd);
    DrawDataAddMatrix(dd, mvp);
    DrawDataAddScaleFactors(dd);
    DrawDataAddVertexCount(dd, CUBE_INDEX_COUNT, 100); // We actually have (7 + 36*2 = 79) qwords of input data

    VU1_Begin();

    // Matrix/scales will be uploaded at address 0 in VU memory.
    const int draw_data_qwsize = DrawDataGetQWordSize(dd);
    VU1_ListData(0, dd->buffer, draw_data_qwsize);

    // Data added from here will begin at address 6, right after the MVP, fScales and vert count:
    VU1_ListAddBegin(draw_data_qwsize);

    // The GIF Tag and primitive description:
    const int vertex_qws = CUBE_INDEX_COUNT * 2; // 2 qwords per vertex (color + position)
    const int vert_loops = CountVertexLoops(vertex_qws, NUM_VERTEX_ELEMENTS);
    const u64 prim_info  = GS_PRIM(GS_PRIM_TRIANGLE, GS_PRIM_SFLAT, GS_PRIM_TOFF, GS_PRIM_FOFF, GS_PRIM_ABOFF, GS_PRIM_AAON, GS_PRIM_FSTQ, GS_PRIM_C1, 0);
    const u64 gif_tag    = GS_GIFTAG(vert_loops, 1, 1, prim_info, GS_GIFTAG_PACKED, NUM_VERTEX_ELEMENTS);
    VU1_ListAdd128(gif_tag, VERTEX_FORMAT);

    const byte colors[4][4] = {
        { 128,   0,   0, 255 }, // red
        {   0, 128,   0, 255 }, // green
        {   0,   0, 128, 255 }, // blue
        { 128, 128,   0, 255 }  // yellow
    };

    // Push one vertex per cube index:
    int i, j, c;
    for (i = 0, j = 0, c = 0; i < CUBE_INDEX_COUNT; ++i, ++j)
    {
        const m_vec4_t * vert = &cube->vertexes[cube->indexes[i]];

        VU1_ListAdd32(colors[c][0]);
        VU1_ListAdd32(colors[c][1]);
        VU1_ListAdd32(colors[c][2]);
        VU1_ListAdd32(colors[c][3]);

        // Each triangle will get a different color.
        if (j == 3)
        {
            c = (c + 1) & 3;
            j = 0;
        }

        VU1_ListAddFloat(vert->x);
        VU1_ListAddFloat(vert->y);
        VU1_ListAddFloat(vert->z);
        VU1_ListAddFloat(vert->w);
    }

    VU1_ListAddEnd();

    // End the list and start the VU program (located in micromem address 0)
    VU1_End(0);
}

void Test_PS2_VU1Cubes(void)
{
    Com_Printf("====== QPS2 - Test_PS2_VU1Cubes ======\n");

    // The scene viewer (not moving):
    const m_vec4_t camera_origin = { 0.0,  0.0f, -1.0f,  1.0f };
    const m_vec4_t camera_lookat = { 0.0,  0.0f,  1.0f,  1.0f };
    const m_vec4_t camera_up     = { 0.0,  1.0f,  0.0f,  1.0f };

    // Render matrices:
    m_mat4_t proj_matrix;
    m_mat4_t view_matrix;
    m_mat4_t view_proj_matrix;
    m_mat4_t mvp_matrix;
    m_mat4_t translation_matrix;
    m_mat4_t rotation_matrix;
    m_mat4_t model_matrix;

    // Clear the screen to dark gray. Default color is black.
    ps2ref.screen_color.r = 120;
    ps2ref.screen_color.g = 120;
    ps2ref.screen_color.b = 120;

    // Upload the VU1 microprogram:
    VU1_UploadProg(0, &VU1Prog_Color_Triangles_CodeStart, &VU1Prog_Color_Triangles_CodeEnd);

    // Set up the matrices; these never change in this test program:
    Mat4_MakeLookAt(&view_matrix, &camera_origin, &camera_lookat, &camera_up);
    Mat4_MakePerspProjection(&proj_matrix, ps2_deg_to_rad(60.0f), 4.0f / 3.0f, viddef.width, viddef.height, 2.0f, 1000.0f, 4096.0f);
    Mat4_Multiply(&view_proj_matrix, &view_matrix, &proj_matrix);

    // Rotate the model, so it looks a little less boring.
    float rotation_angle = 0.0f; // In radians

    cube_t cube = {0};
    MakeCube(&cube, 1.0f);

    // Each cube has its own draw_data_t.
    draw_data_t dd0 = {0};
    draw_data_t dd1 = {0};
    draw_data_t dd2 = {0};

    for (;;)
    {
        PS2_BeginFrame(0);

        Mat4_MakeRotationX(&rotation_matrix, rotation_angle);
        Mat4_MakeTranslation(&translation_matrix, 1.5f, -1.0f, 4.0f);

        Mat4_Multiply(&model_matrix, &rotation_matrix, &translation_matrix);
        Mat4_Multiply(&mvp_matrix, &model_matrix, &view_proj_matrix);

        DrawVU1Cube(&dd0, &mvp_matrix, &cube);

        Mat4_MakeRotationY(&rotation_matrix, rotation_angle);
        Mat4_MakeTranslation(&translation_matrix, -1.0f, -1.0f, 4.0f);

        Mat4_Multiply(&model_matrix, &rotation_matrix, &translation_matrix);
        Mat4_Multiply(&mvp_matrix, &model_matrix, &view_proj_matrix);

        DrawVU1Cube(&dd1, &mvp_matrix, &cube);

        Mat4_MakeRotationZ(&rotation_matrix, rotation_angle);
        Mat4_MakeTranslation(&translation_matrix, 0.0f, 0.5f, 4.0f);

        Mat4_Multiply(&model_matrix, &rotation_matrix, &translation_matrix);
        Mat4_Multiply(&mvp_matrix, &model_matrix, &view_proj_matrix);

        DrawVU1Cube(&dd2, &mvp_matrix, &cube);

        rotation_angle += 0.02f;
        PS2_EndFrame();
    }
}
