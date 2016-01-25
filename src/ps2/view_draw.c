
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
#include "ps2/vu1.h"

//=============================================================================
//
// Local view_draw helpers:
//
//=============================================================================

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
// Public view_draw functions:
//
//=============================================================================

/*
================
PS2_DrawFrameSetup
================
*/
void PS2_DrawFrameSetup(refdef_t * view_def)
{
    //TODO
}

/*
================
PS2_DrawWorldModel
================
*/
void PS2_DrawWorldModel(refdef_t * view_def)
{
    //TODO
}

/*
================
PS2_DrawViewEntities
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
