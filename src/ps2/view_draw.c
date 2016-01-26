
/* ================================================================================================
 * -*- C -*-
 * File: view_draw.c
 * Author: Guilherme R. Lampert
 * Created on: 23/01/16
 * Brief: 3D view drawing for Refresh::RenderFrame().
 *
 * This source code is released under the GNU GPL v2 license.
 * Check the accompanying LICENSE file for details.
 * ================================================================================================ */

#include "common/q_common.h"
#include "ps2/ref_ps2.h"
#include "ps2/mem_alloc.h"
#include "ps2/model_load.h"
#include "ps2/math_funcs.h"
#include "ps2/vec_mat.h"
#include "ps2/vu1.h"

//=============================================================================
//
// Local view_draw data:
//
//=============================================================================

// Scene viewer/camera:
static m_vec4_t ps2_camera_origin;
static m_vec4_t ps2_camera_lookat;
static m_vec4_t ps2_forward_vec;
static m_vec4_t ps2_right_vec;
static m_vec4_t ps2_up_vec;

// Render matrices:
static m_mat4_t ps2_model_to_world_matrix;
static m_mat4_t ps2_proj_matrix;
static m_mat4_t ps2_view_matrix;
static m_mat4_t ps2_view_proj_matrix;
static m_mat4_t ps2_mvp_matrix;

// Draw counters used to mark visible surfaces:
int ps2_vis_frame_count = 0; // Bumped when going to a new PVS
int ps2_frame_count     = 0; // Used for dlight push checking

// Quake 2 viewcluster crap. Don't know for sure how this works,
// copied from ref_gl... I thinks it's related to underwater detection...
// PS2_BeginRegistration() has to reset them to -1.
int ps2_view_cluster      = -1;
int ps2_view_cluster2     = -1;
int ps2_old_view_cluster  = -1;
int ps2_old_view_cluster2 = -1;

//=============================================================================
//
// view_draw helpers:
//
//=============================================================================

/*
================
PS2_CullBox

Returns true if the box is completely outside the frustum
and should be culled. False if visible and allowed to draw.
================
*/
static inline qboolean PS2_CullBox(const vec3_t mins, const vec3_t maxs)
{
    /*TODO
    int i;
    for (i = 0; i < 4; ++i)
    {
        if (BOX_ON_PLANE_SIDE(mins, maxs, &ps2_frustum[i]) == 2)
        {
            return true;
        }
    }
    */
    return false;
}

/*
================
PS2_DrawNullModel

Remarks: Local function.
================
*/
static void PS2_DrawNullModel(const entity_t * ent)
{
    //TODO
}

/*
================
PS2_DrawBeamModel

Remarks: Local function.
================
*/
static void PS2_DrawBeamModel(const entity_t * ent)
{
    //TODO
}

/*
================
PS2_DrawSpriteModel

Remarks: Local function.
================
*/
static void PS2_DrawSpriteModel(const entity_t * ent)
{
    //TODO
}

/*
================
PS2_DrawAliasMD2Model

Remarks: Local function.
================
*/
static void PS2_DrawAliasMD2Model(const entity_t * ent)
{
    //TODO
}

//=============================================================================
//
// World rendering and visibility/PVS handling:
//
//=============================================================================

/*
================
PS2_PointInLeaf

Remarks: Local function.
================
*/
static const ps2_mdl_leaf_t * PS2_PointInLeaf(const vec3_t p, const ps2_model_t * model)
{
    if (model == NULL || model->nodes == NULL)
    {
        Sys_Error("PS2_PointInLeaf: Bad model!");
    }

    const ps2_mdl_node_t * node = model->nodes;
    for (;;)
    {
        if (node->contents != -1)
        {
            return (const ps2_mdl_leaf_t *)node;
        }

        const cplane_t * plane = node->plane;
        const float d = DotProduct(p, plane->normal) - plane->dist;

        if (d > 0.0f)
        {
            node = node->children[0];
        }
        else
        {
            node = node->children[1];
        }
    }

    Sys_Error("PS2_PointInLeaf: Failed to find point!");
    return NULL; // Never reached.
}

/*
================
PS2_DecompressMdlVis

Remarks: Local function.
================
*/
static byte * PS2_DecompressMdlVis(const byte * in, const ps2_model_t * model)
{
    static byte decompressed[MAX_MAP_LEAFS / 8] __attribute__((aligned(16)));

    int row = (model->vis->numclusters + 7) >> 3;
    byte * out = decompressed;

    if (in == NULL)
    {
        // No vis info, so make all visible:
        while (row)
        {
            *out++ = 0xFF;
            row--;
        }
        return decompressed;
    }

    do
    {
        if (*in)
        {
            *out++ = *in++;
            continue;
        }

        int c = in[1];
        in += 2;
        while (c)
        {
            *out++ = 0;
            c--;
        }
    } while (out - decompressed < row);

    return decompressed;
}

/*
================
PS2_GetClusterPVS

Remarks: Local function.
================
*/
static byte * PS2_GetClusterPVS(int cluster, const ps2_model_t * model)
{
    static qboolean no_vis_initialized = false;
    static byte no_vis[MAX_MAP_LEAFS / 8] __attribute__((aligned(16)));

    if (cluster == -1 || model->vis == NULL)
    {
        if (!no_vis_initialized)
        {
            memset(no_vis, 0xFF, sizeof(no_vis));
            no_vis_initialized = true;
        }
        return no_vis;
    }

    return PS2_DecompressMdlVis((const byte *)model->vis + model->vis->bitofs[cluster][DVIS_PVS], model);
}

/*
================
PS2_MarkLeaves

Mark the leaves and nodes that are in the PVS for the current cluster.
================
*/
static void PS2_MarkLeaves(ps2_model_t * world_mdl)
{
    if (ps2_old_view_cluster  == ps2_view_cluster  &&
        ps2_old_view_cluster2 == ps2_view_cluster2 &&
        ps2_view_cluster != -1)
    {
        return;
    }

    ++ps2_vis_frame_count;
    ps2_old_view_cluster  = ps2_view_cluster;
    ps2_old_view_cluster2 = ps2_view_cluster2;

    int i;
    if (ps2_view_cluster == -1 || world_mdl->vis == NULL)
    {
        // Mark everything:
        for (i = 0; i < world_mdl->num_leafs; ++i)
        {
            world_mdl->leafs[i].vis_frame = ps2_vis_frame_count;
        }
        for (i = 0; i < world_mdl->num_nodes; ++i)
        {
            world_mdl->nodes[i].vis_frame = ps2_vis_frame_count;
        }
        return;
    }

    byte * vis = PS2_GetClusterPVS(ps2_view_cluster, world_mdl);
    byte fat_vis[MAX_MAP_LEAFS / 8];

    // May have to combine two clusters because of solid water boundaries:
    if (ps2_view_cluster2 != ps2_view_cluster)
    {
        memcpy(fat_vis, vis, (world_mdl->num_leafs + 7) / 8);
        vis = PS2_GetClusterPVS(ps2_view_cluster2, world_mdl);

        const int c = (world_mdl->num_leafs + 31) / 32;
        for (i = 0; i < c; ++i)
        {
            ((int *)fat_vis)[i] |= ((int *)vis)[i];
        }
        vis = fat_vis;
    }

    ps2_mdl_leaf_t * leaf;
    for (i = 0, leaf = world_mdl->leafs; i < world_mdl->num_leafs; ++i, ++leaf)
    {
        const int cluster = leaf->cluster;
        if (cluster == -1)
        {
            continue;
        }

        if (vis[cluster >> 3] & (1 << (cluster & 7)))
        {
            ps2_mdl_node_t * node = (ps2_mdl_node_t *)leaf;
            do
            {
                if (node->vis_frame == ps2_vis_frame_count)
                {
                    break;
                }
                node->vis_frame = ps2_vis_frame_count;
                node = node->parent;
            } while (node);
        }
    }
}

/*
================
PS2_RecursiveWorldNode

This function will recursively mark all surfaces that should
be drawn and add them to the appropriate draw chain, so the
next call to PS2_DrawTextureChains() will actually render what
was marked for draw in here.
================
*/
static void PS2_RecursiveWorldNode(const refdef_t * view_def, ps2_model_t * world_mdl, ps2_mdl_node_t * node)
{
    if (node->contents == CONTENTS_SOLID)
    {
        return;
    }
    if (node->vis_frame != ps2_vis_frame_count)
    {
        return;
    }
    if (PS2_CullBox(node->minmaxs, node->minmaxs + 3))
    {
        return;
    }

    // If a leaf node, it can draw.
    if (node->contents != -1)
    {
        ps2_mdl_leaf_t * p_leaf = (ps2_mdl_leaf_t *)node;

        // Check for door connected areas:
        if (view_def->areabits)
        {
            if (!(view_def->areabits[p_leaf->area >> 3] & (1 << (p_leaf->area & 7))))
            {
                return; // Not visible.
            }
        }

        ps2_mdl_surface_t ** mark = p_leaf->first_mark_surface;
        int num_surfs = p_leaf->num_mark_surfaces;
        if (num_surfs)
        {
            do
            {
                (*mark)->vis_frame = ps2_frame_count;
                ++mark;
            } while (--num_surfs);
        }

        return;
    }

    //
    // Node is just a decision point, so go down the appropriate sides.
    //

    float dot;
    int side, sidebit;
    const cplane_t * plane = node->plane;

    // Find which side of the node we are on:
    switch (plane->type)
    {
    case PLANE_X:
        dot = view_def->vieworg[0] - plane->dist;
        break;

    case PLANE_Y:
        dot = view_def->vieworg[1] - plane->dist;
        break;

    case PLANE_Z:
        dot = view_def->vieworg[2] - plane->dist;
        break;

    default:
        dot = DotProduct(view_def->vieworg, plane->normal) - plane->dist;
        break;
    } // switch (plane->type)

    if (dot >= 0.0f)
    {
        side    = 0;
        sidebit = 0;
    }
    else
    {
        side    = 1;
        sidebit = SURF_PLANEBACK;
    }

    // Recurse down the children, front side first:
    PS2_RecursiveWorldNode(view_def, world_mdl, node->children[side]);

    //
    // Add stuff to the draw lists:
    //
    int i;
    ps2_mdl_surface_t * surf;
    for (i = node->num_surfaces, surf = world_mdl->surfaces + node->first_surface; i; --i, ++surf)
    {
        if (surf->vis_frame != ps2_frame_count)
        {
            continue;
        }
        if ((surf->flags & SURF_PLANEBACK) != sidebit)
        {
            continue; // wrong side
        }

        if (surf->texinfo->flags & SURF_SKY)
        {
            // Just adds to visible sky bounds.
            // TODO
        }
        else if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
        {
            // Add to the translucent draw chain.
            // TODO
        }
        else
        {
            /*TODO
            if (qglMTexCoord2fSGIS && !(surf->flags & SURF_DRAWTURB))
            {
                GL_RenderLightmappedPoly(surf);
            }
            else
            {
                // The polygon is visible, so add it to the texture sorted chain:
                // FIXME: this is a hack for animation
                //image = R_TextureAnimation(surf->texinfo);
                //surf->texturechain = image->texturechain;
                //image->texturechain = surf;
            }
            */
        }
    }

    // Finally recurse down the back side:
    PS2_RecursiveWorldNode(view_def, world_mdl, node->children[!side]);
}

/*
================
PS2_DrawBrushModel

Remarks: Local function.
================
*/
static void PS2_DrawBrushModel(const entity_t * ent)
{
    //TODO
}

/*
================
PS2_DrawTextureChains

Remarks: Local function.
================
*/
static void PS2_DrawTextureChains(void)
{
    //TODO
}

/*
================
PS2_SetUpViewClusters

Remarks: Local function.
Resets the ps2_view_cluster globals for each new frame.
================
*/
static void PS2_SetUpViewClusters(const refdef_t * view_def)
{
    if (view_def->rdflags & RDF_NOWORLDMODEL)
    {
        return;
    }

    const ps2_model_t * world_mdl = PS2_ModelGetWorld();
    const ps2_mdl_leaf_t * leaf   = PS2_PointInLeaf(view_def->vieworg, world_mdl);

    ps2_old_view_cluster  = ps2_view_cluster;
    ps2_old_view_cluster2 = ps2_view_cluster2;
    ps2_view_cluster = ps2_view_cluster2 = leaf->cluster;

    // Check above and below so crossing solid water doesn't draw wrong:
    vec3_t temp;
    if (!leaf->contents)
    {
        // Look down a bit:
        VectorCopy(view_def->vieworg, temp);
        temp[2] -= 16.0f;
    }
    else
    {
        // Look up a bit:
        VectorCopy(view_def->vieworg, temp);
        temp[2] += 16.0f;
    }

    leaf = PS2_PointInLeaf(temp, world_mdl);
    if (!(leaf->contents & CONTENTS_SOLID) && (leaf->cluster != ps2_view_cluster2))
    {
        ps2_view_cluster2 = leaf->cluster;
    }
}

//=============================================================================
//
// Public view_draw functions:
//
//=============================================================================

/*
================
PS2_DrawFrameSetup

Called by refexport_t::RenderFrame / PS2_RenderFrame.
================
*/
void PS2_DrawFrameSetup(const refdef_t * view_def)
{
    ++ps2_frame_count;
    PS2_SetUpViewClusters(view_def);

    // Eye position:
    Vec4_Set3(&ps2_camera_origin, view_def->vieworg[0], view_def->vieworg[1], view_def->vieworg[2]);

    // Camera view vectors:
    AngleVectors(view_def->viewangles, (float *)&ps2_forward_vec, (float *)&ps2_right_vec, (float *)&ps2_up_vec);
    Vec4_Add3(&ps2_camera_lookat, &ps2_camera_origin, &ps2_forward_vec);

    const float fov_y  = ps2_deg_to_rad(60.0f);
    const float aspect = 4.0f / 3.0f;
    const float z_near = 4.0f;
    const float z_far  = 4096.0f;

    Mat4_MakeLookAt(&ps2_view_matrix, &ps2_camera_origin, &ps2_camera_lookat, &ps2_up_vec);
    Mat4_MakePerspProjection(&ps2_proj_matrix, fov_y, aspect, viddef.width, viddef.height, z_near, z_far, 4096.0f);

    Mat4_Multiply(&ps2_view_proj_matrix, &ps2_view_matrix, &ps2_proj_matrix);
    Mat4_Copy(&ps2_mvp_matrix, &ps2_view_proj_matrix);
    Mat4_Identity(&ps2_model_to_world_matrix);
}

/*
================
PS2_DrawWorldModel

Called by refexport_t::RenderFrame / PS2_RenderFrame.
================
*/
void PS2_DrawWorldModel(refdef_t * view_def)
{
    if (view_def->rdflags & RDF_NOWORLDMODEL)
    {
        return;
    }

    ps2_model_t * world_mdl = PS2_ModelGetWorld();
    PS2_MarkLeaves(world_mdl);
    PS2_RecursiveWorldNode(view_def, world_mdl, world_mdl->nodes);
    PS2_DrawTextureChains();
}

/*
================
PS2_DrawViewEntities

Called by refexport_t::RenderFrame / PS2_RenderFrame.
================
*/
void PS2_DrawViewEntities(refdef_t * view_def)
{
    int i;
    const ps2_model_t * p_model;
    const entity_t    * p_entity;
    const entity_t    * entities_list = view_def->entities;
    const int           num_entities  = view_def->num_entities;

    //
    // Draw solid entities first:
    //
    for (i = 0; i < num_entities; ++i)
    {
        p_entity = &entities_list[i];
        if (p_entity->flags & RF_TRANSLUCENT)
        {
            continue; // Drawn later.
        }

        if (p_entity->flags & RF_BEAM)
        {
            PS2_DrawBeamModel(p_entity);
            continue;
        }

        // entity_t::model is an opaque pointer outside
        // the Refresh module, so we need the cast.
        p_model = (const ps2_model_t *)p_entity->model;
        if (p_model == NULL)
        {
            PS2_DrawNullModel(p_entity);
            continue;
        }

        switch (p_model->type)
        {
        case MDL_BRUSH :
            PS2_DrawBrushModel(p_entity);
            break;

        case MDL_SPRITE :
            PS2_DrawSpriteModel(p_entity);
            break;

        case MDL_ALIAS :
            PS2_DrawAliasMD2Model(p_entity);
            break;

        default:
            Sys_Error("PS2_DrawViewEntities: Bad model type for '%s'!", p_model->name);
        } // switch (p_model->type)
    }

    //
    // Now draw the translucent/transparent ones:
    //
    //TODO!
}
