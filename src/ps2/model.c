
/* ================================================================================================
 * -*- C -*-
 * File: model.c
 * Author: Guilherme R. Lampert
 * Created on: 29/10/15
 * Brief: 3D model loading routines.
 *
 * This source code is released under the GNU GPL v2 license.
 * Check the accompanying LICENSE file for details.
 * ================================================================================================ */

#include "ps2/model.h"

//=============================================================================
// Local data:

enum { MDL_POOL_SIZE = 512 };

// Pool of models used by world/entities/sprites:
static u32 model_pool_used = 0;
static ps2_model_t model_pool[MDL_POOL_SIZE];

// The inline * models from the current map are kept separate.
// These are only referenced by the world geometry.
static ps2_model_t inline_models[MDL_POOL_SIZE];

//=============================================================================

// Used to hash the model filenames.
extern u32 Sys_HashString(const char * str);

/*
==============
PS2_ModelInit
==============
*/
void PS2_ModelInit(void)
{
    if (model_pool_used != 0)
    {
        Sys_Error("Invalid PS2_ModelInit call!");
    }

    //TODO
}

/*
==============
PS2_ModelShutdown
==============
*/
void PS2_ModelShutdown(void)
{
    //TODO
    // free all models, etc

    memset(model_pool,    0, sizeof(model_pool));
    memset(inline_models, 0, sizeof(inline_models));
    model_pool_used = 0;
}

/*
==============
PS2_ModelAlloc
==============
*/
ps2_model_t * PS2_ModelAlloc(void)
{
    if (model_pool_used == MDL_POOL_SIZE)
    {
        Sys_Error("Out of model objects!!!");
    }

    //TODO assume always sequential, or allow empty gaps???
    // -- should also look into the registration_sequence trick used by ref_gl
    return &model_pool[model_pool_used++];
}

/*
==============
PS2_ModelFindOrLoad
==============
*/
ps2_model_t * PS2_ModelFindOrLoad(const char * name, int flags)
{
    if (name == NULL || *name == '\0')
    {
        Com_DPrintf("FindModel: Null/empty model name!");
        return NULL;
    }

    //TODO
    return NULL;
}

/*
==============
PS2_ModelLoadWorld
==============
*/
ps2_model_t * PS2_ModelLoadWorld(const char * name)
{
    //TODO
    return NULL;
}

/*
==============
PS2_ModelFree
==============
*/
void PS2_ModelFree(ps2_model_t * mdl)
{
    //TODO
}
