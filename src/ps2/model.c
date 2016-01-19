
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
#include "ps2/ref_ps2.h"
#include "ps2/mem_alloc.h"
#include "common/q_files.h"

//=============================================================================

// Extra debug printing during model load when defined.
//#define VERBOSE_MODEL_LOADER

// Memory for the model structures is statically allocated.
enum { MDL_POOL_SIZE = 512 };

// Stats for debug printing:
int ps2_model_pool_used     = 0;
int ps2_model_cache_hits    = 0;
int ps2_unused_models_freed = 0;
int ps2_inline_models_used  = 0;
int ps2_models_failed       = 0;

// World instance. Usually a reference to model_pool[0].
ps2_model_t * ps2_world_model = NULL;

// Pool of models used by world/entities/sprites:
static ps2_model_t model_pool[MDL_POOL_SIZE];

// The inline * models from the current map are kept separate.
// These are only referenced by the world geometry.
static ps2_model_t inline_models[MDL_POOL_SIZE];

// Used to hash the model filenames.
extern u32 Sys_HashString(const char * str);

//=============================================================================

/*
==============
PS2_ModelInit
==============
*/
void PS2_ModelInit(void)
{
    if (ps2_model_pool_used != 0 || ps2_inline_models_used != 0)
    {
        Sys_Error("Invalid PS2_ModelInit call!");
    }

    // No additional initialization right now...
}

/*
==============
PS2_ModelShutdown
==============
*/
void PS2_ModelShutdown(void)
{
    u32 i;
    ps2_model_t * model_iter = model_pool;
    for (i = 0; i < MDL_POOL_SIZE; ++i, ++model_iter)
    {
        if (model_iter->type != MDL_NULL)
        {
            PS2_ModelFree(model_iter);
        }
    }

    memset(model_pool,    0, sizeof(model_pool));
    memset(inline_models, 0, sizeof(inline_models));

    ps2_model_pool_used    = 0;
    ps2_inline_models_used = 0;
}

/*
==============
PS2_ModelAlloc
==============
*/
ps2_model_t * PS2_ModelAlloc(void)
{
    if (ps2_model_pool_used == MDL_POOL_SIZE)
    {
        Sys_Error("Out of model objects!!!");
    }

    //
    // Find a free slot in the model pool:
    //
    u32 i;
    ps2_model_t * model_iter = model_pool;
    for (i = 0; i < MDL_POOL_SIZE; ++i, ++model_iter)
    {
        if (model_iter->type == MDL_NULL)
        {
            ++ps2_model_pool_used;
            return model_iter;
        }
    }

    Sys_Error("Out of model objects! Can't find a free slot!");
    return NULL;
}

/*
==============
PS2_ModelFree
==============
*/
void PS2_ModelFree(ps2_model_t * mdl)
{
    if (mdl == NULL)
    {
        return;
    }

    Hunk_Free(&mdl->hunk);
    PS2_MemClearObj(mdl);
    --ps2_model_pool_used;
}

/*
==============
PS2_ModelFreeUnused
==============
*/
void PS2_ModelFreeUnused(void)
{
    u32 i;
    ps2_model_t * model_iter = model_pool;
    for (i = 0; i < MDL_POOL_SIZE; ++i, ++model_iter)
    {
        if (model_iter->type == MDL_NULL)
        {
            continue;
        }
        if (model_iter->registration_sequence != ps2ref.registration_sequence)
        {
            PS2_ModelFree(model_iter);
            ++ps2_unused_models_freed;
        }
    }
}

//=============================================================================
//
// MD2 (AKA Alias Models) loading helpers:
//
//=============================================================================

/*
==============
LoadAliasMD2Model

Remarks: Local function.
Fails with a Sys_Error if the data is invalid.
Adapted from ref_gl.
==============
*/
static void LoadAliasMD2Model(ps2_model_t * mdl, const void * mdl_data)
{
    int i, j;
    const dmdl_t * p_mdl_data_in = (const dmdl_t *)mdl_data;
    const int version = LittleLong(p_mdl_data_in->version);

    if (version != ALIAS_VERSION)
    {
        Sys_Error("Model '%s' has wrong version number (%i should be %i)",
                  mdl->name, version, ALIAS_VERSION);
    }

    dmdl_t * p_header_out = (dmdl_t *)Hunk_BlockAlloc(&mdl->hunk,
                                        LittleLong(p_mdl_data_in->ofs_end));

    //
    // Byte swap the header fields and validate:
    //
    for (i = 0; i < sizeof(dmdl_t) / 4; ++i)
    {
        ((int *)p_header_out)[i] = LittleLong(((const int *)mdl_data)[i]);
    }

    if (p_header_out->skinheight > MAX_MDL_SKIN_HEIGHT)
    {
        Sys_Error("Model '%s' has a skin taller than %d.", mdl->name, MAX_MDL_SKIN_HEIGHT);
    }
    if (p_header_out->num_xyz <= 0)
    {
        Sys_Error("Model '%s' has no vertices!", mdl->name);
    }
    if (p_header_out->num_xyz > MAX_VERTS)
    {
        Sys_Error("Model '%s' has too many vertices!", mdl->name);
    }
    if (p_header_out->num_st <= 0)
    {
        Sys_Error("Model '%s' has no st vertices!", mdl->name);
    }
    if (p_header_out->num_tris <= 0)
    {
        Sys_Error("Model '%s' has no triangles!", mdl->name);
    }
    if (p_header_out->num_frames <= 0)
    {
        Sys_Error("Model '%s' has no frames!", mdl->name);
    }

    //
    // S and T texture coordinates:
    //
    const dstvert_t * p_st_in = (const dstvert_t *)((const byte *)p_mdl_data_in + p_header_out->ofs_st);
    dstvert_t * p_st_out = (dstvert_t *)((byte *)p_header_out + p_header_out->ofs_st);

    for (i = 0; i < p_header_out->num_st; ++i)
    {
        p_st_out[i].s = LittleShort(p_st_in[i].s);
        p_st_out[i].t = LittleShort(p_st_in[i].t);
    }

    //
    // Triangle lists:
    //
    const dtriangle_t * p_tris_in = (const dtriangle_t *)((const byte *)p_mdl_data_in + p_header_out->ofs_tris);
    dtriangle_t * p_tris_out = (dtriangle_t *)((byte *)p_header_out + p_header_out->ofs_tris);

    for (i = 0; i < p_header_out->num_tris; ++i)
    {
        for (j = 0; j < 3; j++)
        {
            p_tris_out[i].index_xyz[j] = LittleShort(p_tris_in[i].index_xyz[j]);
            p_tris_out[i].index_st[j]  = LittleShort(p_tris_in[i].index_st[j]);
        }
    }

    //
    // Animation frames:
    //
    for (i = 0; i < p_header_out->num_frames; ++i)
    {
        const daliasframe_t * p_frame_in;
        daliasframe_t * p_frame_out;

        p_frame_in = (const daliasframe_t *)((const byte *)p_mdl_data_in +
                                             p_header_out->ofs_frames +
                                             i * p_header_out->framesize);

        p_frame_out = (daliasframe_t *)((byte *)p_header_out +
                                        p_header_out->ofs_frames +
                                        i * p_header_out->framesize);

        memcpy(p_frame_out->name, p_frame_in->name, sizeof(p_frame_out->name));

        for (j = 0; j < 3; ++j)
        {
            p_frame_out->scale[j]     = LittleFloat(p_frame_in->scale[j]);
            p_frame_out->translate[j] = LittleFloat(p_frame_in->translate[j]);
        }

        // Verts are all 8 bit, so no swapping needed.
        memcpy(p_frame_out->verts, p_frame_in->verts,
               p_header_out->num_xyz * sizeof(dtrivertx_t));
    }

    //
    // The GL Cmds:
    //
    const int * p_cmds_in = (const int *)((const byte *)p_mdl_data_in + p_header_out->ofs_glcmds);
    int * p_cmds_out = (int *)((byte *)p_header_out + p_header_out->ofs_glcmds);

    for (i = 0; i < p_header_out->num_glcmds; ++i)
    {
        p_cmds_out[i] = LittleLong(p_cmds_in[i]);
    }

    // Set defaults for these:
    mdl->mins[0] = -32;
    mdl->mins[1] = -32;
    mdl->mins[2] = -32;
    mdl->maxs[0] =  32;
    mdl->maxs[1] =  32;
    mdl->maxs[2] =  32;

    mdl->type = MDL_ALIAS;
    mdl->num_frames = p_header_out->num_frames;

    //
    // Register all skins:
    //
    memcpy((char *)p_header_out + p_header_out->ofs_skins,
           (const char *)p_mdl_data_in + p_header_out->ofs_skins,
           p_header_out->num_skins * MAX_SKINNAME);

    for (i = 0; i < p_header_out->num_skins; ++i)
    {
        const char * p_skin_name = (const char *)p_header_out + p_header_out->ofs_skins + i * MAX_SKINNAME;
        mdl->skins[i] = PS2_TexImageFindOrLoad(p_skin_name, IT_SKIN);
    }

    #ifdef VERBOSE_MODEL_LOADER
    Com_DPrintf("New Alias model '%s' loaded!\n", mdl->name);
    #endif // VERBOSE_MODEL_LOADER
}

//=============================================================================
//
// Sprite model loading helpers:
//
//=============================================================================

/*
==============
LoadSpriteModel

Remarks: Local function.
==============
*/
static void LoadSpriteModel(ps2_model_t * mdl, const void * mdl_data)
{
    (void)mdl_data;
    //TODO
    //
    //Note: be sure to reference all images so they get loaded!
    mdl->type = MDL_SPRITE;
}

//=============================================================================
//
// Brush/world model loading helpers:
//
//=============================================================================

/*
==============
LoadBrushModel

Remarks: Local function.
==============
*/
static void LoadBrushModel(ps2_model_t * mdl, const void * mdl_data)
{
    (void)mdl_data;
    //TODO
    //
    //Note: be sure to reference all images so they get loaded!
    mdl->type = MDL_BRUSH;
}

/*
==============
FindInlineModel

Remarks: Local function.
==============
*/
static inline ps2_model_t * FindInlineModel(const char * name)
{
    //FIXME temporarily disabled for testing
    int i = 0;
    /*
    int i = atoi(name + 1);
    if (i < 1 || !ps2_world_model || i >= ps2_world_model->num_submodels)
    {
        Sys_Error("Bad inline model number or null world model!");
    }
    */

    ++ps2_inline_models_used;
    return &inline_models[i];
}

//=============================================================================
//
// Public model/world loaders:
//
//=============================================================================

/*
==============
PS2_ModelFindOrLoad
==============
*/
ps2_model_t * PS2_ModelFindOrLoad(const char * name, int flags)
{
    if (name == NULL || *name == '\0')
    {
        Com_DPrintf("FindModel: Null/empty model name!\n");
        ++ps2_models_failed;
        return NULL;
    }

    //
    // Inline models are grabbed from a separate pool:
    //
    if (name[0] == '*')
    {
        return FindInlineModel(name);
    }

    //
    // Search the currently loaded models first:
    //
    u32 i;
    const u32 name_hash = Sys_HashString(name); // Compare by hash code, much cheaper.
    ps2_model_t * model_iter = model_pool;
    for (i = 0; i < MDL_POOL_SIZE; ++i, ++model_iter)
    {
        if (model_iter->type == MDL_NULL)
        {
            continue;
        }
        if ((name_hash == model_iter->hash) && (flags & model_iter->type))
        {
            if (ps2ref.registration_started)
            {
                ++ps2_model_cache_hits;
            }

            #ifdef VERBOSE_MODEL_LOADER
            Com_DPrintf("Model '%s' already in cache.\n", name);
            #endif // VERBOSE_MODEL_LOADER

            model_iter->registration_sequence = ps2ref.registration_sequence;
            return model_iter;
        }
    }

    //
    // Else, load from file for the first time:
    //
    ps2_model_t * new_model = PS2_ModelAlloc();
    strncpy(new_model->name, name, MAX_QPATH); // Save the name string for console printing
    new_model->hash = name_hash;               // We've already computed the name hash above!

    // Load raw file data:
    void * file_data = NULL;
    const int file_len = FS_LoadFile(name, &file_data);
    if (file_data == NULL || file_len <= 0)
    {
        Com_DPrintf("WARNING: Unable to find model '%s'! Failed to open file.\n", name);

        // Put it back into the pool.
        PS2_ModelFree(new_model);
        ++ps2_models_failed;
        return NULL;
    }

    // Call the appropriate loader:
    const u32 id = LittleLong(*(u32 *)file_data);
    switch (id)
    {
    case IDALIASHEADER :
// TODO
// Should probably try to load the MD2 in-place to avoid allocating twice.
// Or maybe we'll just preprocess it and save an optimized PS2 renderer representation?...
//
//        Hunk_New(&new_model->hunk, 0x200000, MEMTAG_MDL_ALIAS);
        Hunk_New(&new_model->hunk, file_len + 512, MEMTAG_MDL_ALIAS);
        LoadAliasMD2Model(new_model, file_data);
        break;

    case IDSPRITEHEADER :
//TODO
//        Hunk_New(&new_model->hunk, 0x10000, MEMTAG_MDL_SPRITE);
        Hunk_New(&new_model->hunk, file_len + 512, MEMTAG_MDL_SPRITE);
        LoadSpriteModel(new_model, file_data);
        break;

    case IDBSPHEADER :
//TODO Should use a resident large block instead??? Sized for the biggest map???
// Might also want to keep a resident block for the file contents, to avoid alloc/dealloc cycles...
//
//        Hunk_New(&new_model->hunk, 0x1000000, MEMTAG_MDL_WORLD);
        Hunk_New(&new_model->hunk, file_len + 2048, MEMTAG_MDL_WORLD);
        LoadBrushModel(new_model, file_data);
        break;

    default :
        Sys_Error("FindModel: Unknown file id (0x%X) for '%s'!", id, name);
    } // switch (id)

    // Done with the original file.
    FS_FreeFile(file_data);

    // Reference it:
    new_model->registration_sequence = ps2ref.registration_sequence;
    return new_model;
}

/*
==============
PS2_ModelLoadWorld

Remarks: Fails with a Sys_Error if the world model cannot be loaded.
==============
*/
void PS2_ModelLoadWorld(const char * name)
{
    // This function is only called by BeginRegistration,
    // so it's a good place to reset these counters.
    ps2_unused_models_freed = 0;
    ps2_model_cache_hits    = 0;
    ps2_inline_models_used  = 0;
    ps2_models_failed       = 0;

    if (name == NULL || *name == '\0')
    {
        Sys_Error("LoadWorld: Null/empty map name!\n");
    }

    char fullname[MAX_QPATH];
    Com_sprintf(fullname, sizeof(fullname), "maps/%s.bsp", name);

    // Explicitly free the old map if different.
    // This guarantees that that the first model is the world map.
    cvar_t * flushmap = Cvar_Get("flushmap", "0", 0);
    if (strcmp(model_pool[0].name, fullname) != 0 || flushmap->value)
    {
        PS2_ModelFree(&model_pool[0]);
    }

    ps2_world_model = PS2_ModelFindOrLoad(fullname, MDL_BRUSH);
    if (ps2_world_model == NULL)
    {
        Sys_Error("Unable to load level '%s'!", fullname);
    }
}
