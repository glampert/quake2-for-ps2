
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

//=============================================================================
//
// Test_PS2_VU1Triangle -- Draws a rotating triangle using the
// Vector Unit 1 (VU1) hardware accelerated rendering path.
//
//=============================================================================

// The VU1 program generated from test_triangle.vcl
extern void vu1Triangle_CodeStart VU1_DATA_SEGMENT;
extern void vu1Triangle_CodeEnd   VU1_DATA_SEGMENT;

// Render matrices:
static m_mat4_t proj_matrix;
static m_mat4_t view_matrix;
static m_mat4_t view_proj_matrix;

// The scene viewer (not moving):
static m_vec4_t camera_origin = { 0.0,  0.0f, -1.0f,  1.0f };
static m_vec4_t camera_lookat = { 0.0,  0.0f,  1.0f,  1.0f };
static m_vec4_t camera_up     = { 0.0,  1.0f,  0.0f,  1.0f };

// Rotate the triangle, so it looks less boring.
static float rotation_angle = 0.0f;

// GS vertex format for our triangle (reglist):
// - RGBAQ color
// - 3 XYZW vertexes
#define TRIANGLE_VERTEX_DEF \
		((u64)GS_REG_RGBAQ) << 0 | \
		((u64)GS_REG_XYZ2)  << 4 | \
		((u64)GS_REG_XYZ2)  << 8 | \
		((u64)GS_REG_XYZ2)  << 12

static void DrawVU1Triangle(m_mat4_t * mvp)
{
    // Has to live at least until the VIF DMA is completed.
    static float data_buffer[16 + 4] __attribute__((aligned(16)));
    memcpy(data_buffer, mvp, sizeof(m_mat4_t));

    // GS rasterizer scale factors follow the MVP matrix:
    float * fScales = data_buffer + 16;
    fScales[0] = 2048.0f;
    fScales[1] = 2048.0f;
    fScales[2] = ((float)0xFFFFFF) / 32.0f;
    fScales[3] = 1.0f;

    VU1_Begin();

    // Matrix/scales will be uploaded at address 0 in VU memory.
    VU1_ListData(0, data_buffer, 5);

    // Data added from here will begin at address 5, right after the MVP and fScales:
    VU1_ListAddBegin(5);

    // The gifTag:
    VU1_ListAdd128(GS_GIFTAG(1, 1, 1, GS_PRIM(GS_PRIM_TRIANGLE, GS_PRIM_SFLAT, GS_PRIM_TOFF,
                                              GS_PRIM_FOFF, GS_PRIM_ABOFF, GS_PRIM_AAOFF, GS_PRIM_FSTQ,
                                              GS_PRIM_C1, 0), GS_GIFTAG_PACKED, /* num_regs = */ 4), TRIANGLE_VERTEX_DEF);

    // Other possible primitive settings:
    /*
    VU1_ListAdd128(GS_GIFTAG(9, GS_GIFTAG_EWITHOUT, 0, 0, GS_GIFTAG_PACKED, 1), GS_GIFTAG_AD);
    VU1_ListAdd128(GS_PRMODECONT(1), GS_REG(PRMODECONT));

    VU1_ListAdd128(GS_TEST(GS_TEST_AOFF, GS_TEST_AALWAYS, 0xFF, GS_TEST_AKEEP,
                         GS_TEST_DAOFF, 0, GS_TEST_ZOFF, GS_TEST_ZALWAYS), GS_REG(TEST_1));

    VU1_ListAdd128(GS_COLCLAMP(GS_COLCLAMP_ON), GS_REG(COLCLAMP));

    VU1_ListAdd128(GS_DIMX(-4, 2, -3, 3, 0, -2, 1, -1, -3, 3, -4, 2, 1, -1, 0, -2), GS_REG(DIMX));
    VU1_ListAdd128(GS_DTHE(GS_DTHE_ON), GS_REG(DTHE));
    */

    // Color:
    VU1_ListAdd32(127);
    VU1_ListAdd32(50);
    VU1_ListAdd32(50);
    VU1_ListAdd32(127);

    // Vertex positions (x,y,z,w):
    VU1_ListAddFloat(-1.0f);
    VU1_ListAddFloat(1.0f);
    VU1_ListAddFloat(3.0f);
    VU1_ListAddFloat(1.0f);

    VU1_ListAddFloat(-1.0f);
    VU1_ListAddFloat(-1.0f);
    VU1_ListAddFloat(3.0f);
    VU1_ListAddFloat(1.0f);

    VU1_ListAddFloat(0.5f);
    VU1_ListAddFloat(0.5f);
    VU1_ListAddFloat(3.0f);
    VU1_ListAddFloat(1.0f);

    VU1_ListAddEnd();

    // End the list and start the vu program (located in micromem address 0)
    VU1_End(0);
}

void Test_PS2_VU1Triangle(void)
{
    Com_Printf("====== QPS2 - Test_PS2_VU1Triangle ======\n");

    // Clear the screen to dark gray. Default color is black.
    ps2ref.screen_color.r = 120;
    ps2ref.screen_color.g = 120;
    ps2ref.screen_color.b = 120;

    // Upload the VU1 microprogram:
    VU1_UploadProg(0, &vu1Triangle_CodeStart, &vu1Triangle_CodeEnd);

    // Set up the matrices; they never change in this test program:
    Mat4_MakeLookAt(&view_matrix, &camera_origin, &camera_lookat, &camera_up);
    Mat4_MakePerspProjection(&proj_matrix, ps2_deg_to_rad(60.0f), 4.0f / 3.0f, viddef.width, viddef.height, 2.0f, 1000.0f, 4096.0f);
    Mat4_Multiply(&view_proj_matrix, &view_matrix, &proj_matrix);

    m_mat4_t translation_matrix;
    m_mat4_t rotation_matrix;
    m_mat4_t model_matrix;
    m_mat4_t mvp_matrix;

    for (;;)
    {
        PS2_BeginFrame(0);

        Mat4_MakeRotationZ(&rotation_matrix, rotation_angle);
        Mat4_MakeTranslation(&translation_matrix, 1.0f, 0.0f, 0.0f);

        Mat4_Multiply(&model_matrix, &rotation_matrix, &translation_matrix);
        Mat4_Multiply(&mvp_matrix, &model_matrix, &view_proj_matrix);

        DrawVU1Triangle(&mvp_matrix);
        rotation_angle += 0.02f;

        PS2_EndFrame();
    }
}
