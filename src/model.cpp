/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include <cmath>
#include "quakedef.hpp"
#include "r_local.hpp"

model_t *loadmodel;
char loadname[32];    // for hunk tags

void Mod_LoadSpriteModel(model_t *mod, void *buffer);

void Mod_LoadBrushModel(model_t *mod, void *buffer);

void Mod_LoadAliasModel(model_t *mod, void *buffer);

auto Mod_LoadModel(model_t *mod, qboolean crash) -> model_t *;

byte mod_novis[MAX_MAP_LEAFS / 8];

#define    MAX_MOD_KNOWN    256
model_t mod_known[MAX_MOD_KNOWN];
int mod_numknown;

// values for model_t's needload
#define NL_PRESENT        0
#define NL_NEEDS_LOADED    1
#define NL_UNREFERENCED    2

/*
===============
Mod_Init
===============
*/
void Mod_Init() {
    memset(mod_novis, 0xff, sizeof(mod_novis));
}

/*
===============
Mod_Extradata

Caches the data if needed
===============
*/
auto Mod_Extradata(model_t *mod) -> void * {
    void *r = nullptr;

    r = Cache_Check(&mod->cache);
    if (r)
        return r;

    Mod_LoadModel(mod, true);

    if (!mod->cache.data)
        Sys_Error("Mod_Extradata: caching failed");
    return mod->cache.data;
}

/*
===============
Mod_PointInLeaf
===============
*/
auto Mod_PointInLeaf(vec3 &p, model_t *model) -> mleaf_t * {
    mnode_t *node = nullptr;
    float d = NAN;
    mplane_t *plane = nullptr;

    if (!model || !model->nodes)
        Sys_Error("Mod_PointInLeaf: bad model");

    node = model->nodes;
    while (true) {
        if (node->contents < 0)
            return (mleaf_t *) node;
        plane = node->plane;
        d = glm::dot (p, plane->normal) - plane->dist;
        if (d > 0)
            node = node->children[0];
        else
            node = node->children[1];
    }

    return nullptr;    // never reached
}


/*
===================
Mod_DecompressVis
===================
*/
auto Mod_DecompressVis(byte *in, model_t *model) -> byte * {
    static byte decompressed[MAX_MAP_LEAFS / 8];
    int c = 0;
    byte *out = nullptr;
    int row = 0;

    row = (model->numleafs + 7) >> 3;
    out = decompressed;

    if (!in) {    // no vis info, so make all visible
        while (row) {
            *out++ = 0xff;
            row--;
        }
        return decompressed;
    }

    do {
        if (*in) {
            *out++ = *in++;
            continue;
        }

        c = in[1];
        in += 2;
        while (c) {
            *out++ = 0;
            c--;
        }
    } while (out - decompressed < row);

    return decompressed;
}

auto Mod_LeafPVS(mleaf_t *leaf, model_t *model) -> byte * {
    if (leaf == model->leafs)
        return mod_novis;
    return Mod_DecompressVis(leaf->compressed_vis, model);
}

/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll() {
    int i = 0;
    model_t *mod = nullptr;


    for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++) {
        mod->needload = NL_UNREFERENCED;
//FIX FOR CACHE_ALLOC ERRORS:
        if (mod->type == mod_sprite) mod->cache.data = nullptr;
    }
}

/*
==================
Mod_FindName

==================
*/
auto Mod_FindName(std::string_view name) -> model_t * {
    int i = 0;
    model_t *mod = nullptr;
    model_t *avail = nullptr;

    if (!name[0])
        Sys_Error("Mod_ForName: NULL name");

//
// search the currently loaded models
//
    for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++) {
        if (!Q_strcmp(mod->name, name))
            break;
        if (mod->needload == NL_UNREFERENCED)
            if (!avail || mod->type != mod_alias)
                avail = mod;
    }

    if (i == mod_numknown) {
        if (mod_numknown == MAX_MOD_KNOWN) {
            if (avail) {
                mod = avail;
                if (mod->type == mod_alias)
                    if (Cache_Check(&mod->cache))
                        Cache_Free(&mod->cache);
            } else
                Sys_Error("mod_numknown == MAX_MOD_KNOWN");
        } else
            mod_numknown++;
        std::strcpy(mod->name, name.data());
        mod->needload = NL_NEEDS_LOADED;
    }

    return mod;
}

/*
==================
Mod_TouchModel

==================
*/
void Mod_TouchModel(char *name) {
    model_t *mod = nullptr;

    mod = Mod_FindName(name);

    if (mod->needload == NL_PRESENT) {
        if (mod->type == mod_alias)
            Cache_Check(&mod->cache);
    }
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
auto Mod_LoadModel(model_t *mod, qboolean crash) -> model_t * {
    unsigned *buf = nullptr;
    byte stackbuf[1024];        // avoid dirtying the cache heap

    if (mod->type == mod_alias) {
        if (Cache_Check(&mod->cache)) {
            mod->needload = NL_PRESENT;
            return mod;
        }
    } else {
        if (mod->needload == NL_PRESENT)
            return mod;
    }

//
// because the world is so huge, load it one piece at a time
//

//
// load the file
//
    buf = (unsigned *) COM_LoadStackFile(mod->name, stackbuf, sizeof(stackbuf));
    if (!buf) {
        if (crash)
            Sys_Error("Mod_NumForName: %s not found", mod->name);
        return nullptr;
    }

//
// allocate a new model
//

    const auto filename = COM_FileBase(mod->name);
    std::strncpy(loadname, filename.cbegin(), filename.length());
    loadmodel = mod;

//
// fill it in
//

// call the apropriate loader
    mod->needload = NL_PRESENT;

    switch (LittleLong(*(unsigned *) buf)) {
        case IDPOLYHEADER:
            Mod_LoadAliasModel(mod, buf);
            break;

        case IDSPRITEHEADER:
            Mod_LoadSpriteModel(mod, buf);
            break;

        default:
            Mod_LoadBrushModel(mod, buf);
            break;
    }

    return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
auto Mod_ForName(std::string_view name, qboolean crash) -> model_t * {
    model_t *mod = Mod_FindName(name);

    return Mod_LoadModel(mod, crash);
}


/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

byte *mod_base;

template <typename DataType>
static inline std::size_t check_lump_size(const lump_t *l) {
  const auto in = *std::bit_cast<DataType*>(mod_base + l->fileofs);
  if (l->filelen % sizeof(in))
    Sys_Error("Mod_LoadFaces: funny lump size in %s", loadmodel->name);
  return l->filelen / sizeof(in);
}

/*
=================
Mod_LoadTextures
=================
*/
void Mod_LoadTextures(lump_t *l) {
    if (l->filelen == 0) {
        loadmodel->textures = nullptr;
        return;
    }
    auto *m = std::bit_cast<dmiptexlump_t*>(mod_base + l->fileofs);

    loadmodel->numtextures = m->nummiptex;
    loadmodel->textures = hunkAllocName<decltype(loadmodel->textures)>(
            loadmodel->numtextures * sizeof(*loadmodel->textures), loadname);

    for (int i = 0; i < m->nummiptex; i++) {
        m->dataofs[i] = LittleLong(m->dataofs[i]);
        if (m->dataofs[i] == -1)
            continue;
        auto *mt = std::bit_cast<miptex_t *>((byte *) m + m->dataofs[i]);
        mt->width = LittleLong(mt->width);
        mt->height = LittleLong(mt->height);
        for (int j = 0; j < MIPLEVELS; j++)
            mt->offsets[j] = LittleLong(mt->offsets[j]);

        if ((mt->width & 15) || (mt->height & 15))
            Sys_Error("Texture %s is not 16 aligned", mt->name);
        const auto pixels = mt->width * mt->height / 64 * 85;
        auto *tx = hunkAllocName<texture_t *>(sizeof(texture_t) + pixels, loadname);
        loadmodel->textures[i] = tx;

        memcpy(tx->name, mt->name, sizeof(tx->name));
        tx->width = mt->width;
        tx->height = mt->height;
        for (int j = 0; j < MIPLEVELS; j++)
            tx->offsets[j] = mt->offsets[j] + sizeof(texture_t) - sizeof(miptex_t);
        // the pixels immediately follow the structures
        memcpy(tx + 1, mt + 1, pixels);

        if (!Q_strncmp(mt->name, "sky", 3))
            R_InitSky(tx);
    }

//
// sequence the animations
//
    for (int i = 0; i < m->nummiptex; i++) {
        auto *tx = loadmodel->textures[i];
        if (!tx || tx->name[0] != '+')
            continue;
        if (tx->anim_next)
            continue;    // allready sequenced

        // find the number of frames in the animation
        texture_t *anims[10]{};
        texture_t *altanims[10]{};

        auto max = tx->name[1];
        auto altmax = 0;
        if (max >= 'a' && max <= 'z')
            max -= 'a' - 'A';
        if (max >= '0' && max <= '9') {
            max -= '0';
            altmax = 0;
            anims[max] = tx;
            max++;
        } else if (max >= 'A' && max <= 'J') {
            altmax = max - 'A';
            max = 0;
            altanims[altmax] = tx;
            altmax++;
        } else
            Sys_Error("Bad animating texture %s", tx->name);

        for (int j = i + 1; j < m->nummiptex; j++) {
            auto *tx2 = loadmodel->textures[j];
            if (!tx2 || tx2->name[0] != '+')
                continue;
            if (strcmp(tx2->name + 2, tx->name + 2) != 0)
                continue;

            auto num = tx2->name[1];
            if (num >= 'a' && num <= 'z')
                num -= 'a' - 'A';
            if (num >= '0' && num <= '9') {
                num -= '0';
                anims[num] = tx2;
                if (num + 1 > max)
                    max = num + 1;
            } else if (num >= 'A' && num <= 'J') {
                num = num - 'A';
                altanims[num] = tx2;
                if (num + 1 > altmax)
                    altmax = num + 1;
            } else
                Sys_Error("Bad animating texture %s", tx->name);
        }

#define    ANIM_CYCLE    2
        // link them all together
        for (int j = 0; j < max; j++) {
            auto *tx2 = anims[j];
            if (!tx2)
                Sys_Error("Missing frame %i of %s", j, tx->name);
            tx2->anim_total = max * ANIM_CYCLE;
            tx2->anim_min = j * ANIM_CYCLE;
            tx2->anim_max = (j + 1) * ANIM_CYCLE;
            tx2->anim_next = anims[(j + 1) % max];
            if (altmax)
                tx2->alternate_anims = altanims[0];
        }
        for (int j = 0; j < altmax; j++) {
            auto *tx2 = altanims[j];
            if (!tx2)
                Sys_Error("Missing frame %i of %s", j, tx->name);
            tx2->anim_total = altmax * ANIM_CYCLE;
            tx2->anim_min = j * ANIM_CYCLE;
            tx2->anim_max = (j + 1) * ANIM_CYCLE;
            tx2->anim_next = altanims[(j + 1) % altmax];
            if (max)
                tx2->alternate_anims = anims[0];
        }
    }
}

/*
=================
Mod_LoadLighting
=================
*/
void Mod_LoadLighting(lump_t *l) {
    if (!l->filelen) {
        loadmodel->lightdata = nullptr;
        return;
    }
    loadmodel->lightdata = hunkAllocName<decltype(loadmodel->lightdata)>(l->filelen, loadname);
    memcpy(loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility(lump_t *l) {
    if (!l->filelen) {
        loadmodel->visdata = nullptr;
        return;
    }
    loadmodel->visdata = hunkAllocName<decltype(loadmodel->visdata)>(l->filelen, loadname);
    memcpy(loadmodel->visdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadEntities
=================
*/
void Mod_LoadEntities(lump_t *l) {
    if (!l->filelen) {
        loadmodel->entities = nullptr;
        return;
    }
    loadmodel->entities = hunkAllocName<decltype(loadmodel->entities)>(l->filelen, loadname);
    memcpy(loadmodel->entities, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes(lump_t *l) {
    auto *in = reinterpret_cast<dvertex_t *>(mod_base + l->fileofs);
    if (l->filelen % sizeof(*in))
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    auto count = l->filelen / sizeof(*in);
    auto *out = hunkAllocName<mvertex_t *>(count * sizeof(mvertex_t), loadname);

    loadmodel->vertexes = out;
    loadmodel->numvertexes = count;

    for (std::size_t i = 0; i < count; i++, in++, out++) {
        std::memcpy(out, in, sizeof(mvertex_t));
    }
}

/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels(lump_t *l) {
    dmodel_t *in = nullptr;
    dmodel_t *out = nullptr;
    int i = 0, j = 0, count = 0;

    in = reinterpret_cast<decltype(in)>(mod_base + l->fileofs);
    if (l->filelen % sizeof(*in))
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    count = l->filelen / sizeof(*in);
    out = hunkAllocName<decltype(out)>(count * sizeof(*out), loadname);

    loadmodel->submodels = out;
    loadmodel->numsubmodels = count;

    for (i = 0; i < count; i++, in++, out++) {
        for (j = 0; j < 3; j++) {    // spread the mins / maxs by a pixel
            out->mins[j] = LittleFloat(in->mins[j]) - 1;
            out->maxs[j] = LittleFloat(in->maxs[j]) + 1;
            out->origin[j] = LittleFloat(in->origin[j]);
        }
        for (j = 0; j < MAX_MAP_HULLS; j++)
            out->headnode[j] = LittleLong(in->headnode[j]);
        out->visleafs = LittleLong(in->visleafs);
        out->firstface = LittleLong(in->firstface);
        out->numfaces = LittleLong(in->numfaces);
    }
}

/*
=================
Mod_LoadEdges
=================
*/
void Mod_LoadEdges(lump_t *l) {
    dedge_t *in = nullptr;
    medge_t *out = nullptr;
    int i = 0, count = 0;

    in = reinterpret_cast<decltype(in)>(mod_base + l->fileofs);
    if (l->filelen % sizeof(*in))
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    count = l->filelen / sizeof(*in);
    out = hunkAllocName<decltype(out)>((count + 1) * sizeof(*out), loadname);

    loadmodel->edges = out;
    loadmodel->numedges = count;

    for (i = 0; i < count; i++, in++, out++) {
        out->v[0] = (unsigned short) LittleShort(in->v[0]);
        out->v[1] = (unsigned short) LittleShort(in->v[1]);
    }
}

/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo(lump_t *l) {
    std::size_t texture_count = check_lump_size<texinfo_t>(l);
    auto *out = hunkAllocName<mtexinfo_t*>(texture_count * sizeof(mtexinfo_t), loadname);

    loadmodel->texinfo = out;
    loadmodel->numtexinfo = texture_count;

    for (int i = 0, offset = 0; i < texture_count; i++, offset += sizeof(texinfo_t), out++) {
      const auto in = *std::bit_cast<texinfo_t*>(mod_base + l->fileofs + offset);
        for (int j = 0; j < 4; j++) {
          out->vecs[0][j] = LittleFloat(in.vecs[0][j]);
          out->vecs[1][j] = LittleFloat(in.vecs[1][j]);
        }

        auto len1 = glm::length(out->vecs[0]);
        const auto len2 = glm::length(out->vecs[1]);
        len1 = (len1 + len2) / 2.F;
        if (len1 < 0.32)
            out->mipadjust = 4;
        else if (len1 < 0.49)
            out->mipadjust = 3;
        else if (len1 < 0.99)
            out->mipadjust = 2;
        else
            out->mipadjust = 1;
#if 0
        if (len1 + len2 < 0.001)
            out->mipadjust = 1;		// don't crash
        else
            out->mipadjust = 1 / floor( (len1+len2)/2 + 0.1 );
#endif

        const auto miptex = LittleLong(in.miptex);
        out->flags = static_cast<decltype(out->flags)>(LittleLong(in.flags));

        if (!loadmodel->textures) {
            out->texture = r_notexture_mip;    // checkerboard texture
            out->flags = 0;
        } else {
            if (miptex >= loadmodel->numtextures)
                Sys_Error("miptex >= loadmodel->numtextures");
            out->texture = loadmodel->textures[miptex];
            if (!out->texture) {
                out->texture = r_notexture_mip; // texture not found
                out->flags = 0;
            }
        }
    }
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents(msurface_t *s) {
  vec2 mins = {999999, 999999};
  vec2 maxs = {-99999, -99999};
  const auto *tex = s->texinfo;
    for (int i = 0; i < s->numedges; i++) {
        const auto e = loadmodel->surfedges[s->firstedge + i];

        const auto *v = e >= 0 ? &loadmodel->vertexes[loadmodel->edges[e].v[0]]
                                 : &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

        for (int j = 0; j < 2; j++) {
            const auto val = glm::dot(v->position, {tex->vecs[j]}) + tex->vecs[j][3];
            if (val < mins[j])
                mins[j] = val;
            if (val > maxs[j])
                maxs[j] = val;
        }
    }

    vec2 bmins = glm::floor(mins / 16.F);
    vec2 bmaxs = glm::ceil(maxs / 16.F);
    for (int i = 0; i < 2; i++) {
        s->texturemins[i] = bmins[i] * 16;
        s->extents[i] = (bmaxs[i] - bmins[i]) * 16;
        if (!(tex->flags & TEX_SPECIAL) && s->extents[i] > 256)
            Sys_Error("Bad surface extents: %d", s->extents[i]);
    }
}

/*
=================
Mod_LoadFaces
=================
*/
void Mod_LoadFaces(lump_t *l) {
    const auto face_count = check_lump_size<dface_t>(l);
    auto *out = hunkAllocName<msurface_t *>(face_count * sizeof(msurface_t), loadname);

    loadmodel->surfaces = out;
    loadmodel->numsurfaces = face_count;

    for (int current = 0, offset = 0
         ; current < face_count
         ; current++, offset += sizeof(dface_t), out++) {
      const auto in = *std::bit_cast<dface_t*>(mod_base + l->fileofs + offset);
        out->firstedge = LittleLong(in.firstedge);
        out->numedges = LittleShort(in.numedges);
        out->flags = 0;

        const auto planenum = LittleShort(in.planenum);
        const auto side = LittleShort(in.side);
        if (side)
            out->flags |= SURF_PLANEBACK;

        out->plane = loadmodel->planes + planenum;

        out->texinfo = loadmodel->texinfo + LittleShort(in.texinfo);

        CalcSurfaceExtents(out);

        // lighting info

        int i = 0;
        for (; i < MAXLIGHTMAPS; i++)
            out->styles[i] = static_cast<byte>(in.styles[i]);
        i = LittleLong(in.lightofs);
        if (i == -1)
            out->samples = nullptr;
        else
            out->samples = loadmodel->lightdata + i;

        // set the drawing flags flag

        if (!Q_strncmp(out->texinfo->texture->name, "sky", 3))    // sky
        {
            out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
            continue;
        }

        if (!Q_strncmp(out->texinfo->texture->name, "*", 1))        // turbulent
        {
            out->flags |= (SURF_DRAWTURB | SURF_DRAWTILED);
            for (i = 0; i < 2; i++) {
                out->extents[i] = 16384;
                out->texturemins[i] = -8192;
            }
            continue;
        }
    }
}


/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent(mnode_t *node, mnode_t *parent) {
    node->parent = parent;
    if (node->contents < 0)
        return;
    Mod_SetParent(node->children[0], node);
    Mod_SetParent(node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes(lump_t *l) {
    const auto node_count = check_lump_size<dnode_t>(l);
    auto *out = hunkAllocName<mnode_t*>(node_count * sizeof(mnode_t), loadname);

    loadmodel->nodes = out;
    loadmodel->numnodes = node_count;

    for (int i = 0, offset = 0; i < node_count; i++, offset += sizeof(dnode_t), out++) {
      const auto in = *std::bit_cast<dnode_t*>(mod_base + l->fileofs + offset);
        for (int j = 0; j < 3; j++) {
            out->minmaxs[j] = LittleShort(in.mins[j]);
            out->minmaxs[3 + j] = LittleShort(in.maxs[j]);
        }

        auto p = LittleLong(in.planenum);
        out->plane = loadmodel->planes + p;

        out->firstsurface = in.firstface;
        out->numsurfaces = in.numfaces;

        for (int j = 0; j < 2; j++) {
            p = LittleShort(in.children[j]);
            if (p >= 0)
                out->children[j] = loadmodel->nodes + p;
            else
                out->children[j] = (mnode_t *) (loadmodel->leafs + (-1 - p));
        }
    }

    Mod_SetParent(loadmodel->nodes, nullptr);    // sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs(lump_t *l) {
    const auto leaf_count = check_lump_size<dleaf_t>(l);
    auto *out = hunkAllocName<mleaf_t*>(leaf_count * sizeof(mleaf_t), loadname);

    loadmodel->leafs = out;
    loadmodel->numleafs = leaf_count;

    for (int i = 0, offset = 0; i < leaf_count; i++, offset += sizeof(dleaf_t), out++) {
      const auto in = *std::bit_cast<dleaf_t*>(mod_base + l->fileofs + offset);
        for (int j = 0; j < 3; j++) {
            out->minmaxs[j] = LittleShort(in.mins[j]);
            out->minmaxs[3 + j] = LittleShort(in.maxs[j]);
        }

        auto p = LittleLong(in.contents);
        out->contents = p;

        out->firstmarksurface = loadmodel->marksurfaces +
                                in.firstmarksurface;
        out->nummarksurfaces = in.nummarksurfaces;

        p = LittleLong(in.visofs);
        if (p == -1)
            out->compressed_vis = nullptr;
        else
            out->compressed_vis = loadmodel->visdata + p;
        out->efrags = nullptr;

        for (int j = 0; j < 4; j++)
            out->ambient_sound_level[j] = static_cast<byte>(in.ambient_level[j]);
    }
}

/*
=================
Mod_LoadClipnodes
=================
*/
void Mod_LoadClipnodes(lump_t *l) {
    dclipnode_t *in = nullptr, *out = nullptr;
    int i = 0, count = 0;
    hull_t *hull = nullptr;

    in = reinterpret_cast<decltype(in)>(mod_base + l->fileofs);
    if (l->filelen % sizeof(*in))
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    count = l->filelen / sizeof(*in);
    out = hunkAllocName<decltype(out)>(count * sizeof(*out), loadname);

    loadmodel->clipnodes = out;
    loadmodel->numclipnodes = count;

    hull = &loadmodel->hulls[1];
    hull->clipnodes = out;
    hull->firstclipnode = 0;
    hull->lastclipnode = count - 1;
    hull->planes = loadmodel->planes;
    hull->clip_mins[0] = -16;
    hull->clip_mins[1] = -16;
    hull->clip_mins[2] = -24;
    hull->clip_maxs[0] = 16;
    hull->clip_maxs[1] = 16;
    hull->clip_maxs[2] = 32;

    hull = &loadmodel->hulls[2];
    hull->clipnodes = out;
    hull->firstclipnode = 0;
    hull->lastclipnode = count - 1;
    hull->planes = loadmodel->planes;
    hull->clip_mins[0] = -32;
    hull->clip_mins[1] = -32;
    hull->clip_mins[2] = -24;
    hull->clip_maxs[0] = 32;
    hull->clip_maxs[1] = 32;
    hull->clip_maxs[2] = 64;

    for (i = 0; i < count; i++, out++, in++) {
        std::memcpy(out, in, sizeof(dclipnode_t));
    }
}

/*
=================
Mod_MakeHull0

Deplicate the drawing hull structure as a clipping hull
=================
*/
void Mod_MakeHull0() {
    mnode_t *in = nullptr, *child = nullptr;
    dclipnode_t *out = nullptr;
    int i = 0, j = 0, count = 0;
    hull_t *hull = nullptr;

    hull = &loadmodel->hulls[0];

    in = loadmodel->nodes;
    count = loadmodel->numnodes;
    out = hunkAllocName<decltype(out)>(count * sizeof(*out), loadname);

    hull->clipnodes = out;
    hull->firstclipnode = 0;
    hull->lastclipnode = count - 1;
    hull->planes = loadmodel->planes;

    for (i = 0; i < count; i++, out++, in++) {
        out->planenum = in->plane - loadmodel->planes;
        for (j = 0; j < 2; j++) {
            child = in->children[j];
            if (child->contents < 0)
                out->children[j] = child->contents;
            else
                out->children[j] = child - loadmodel->nodes;
        }
    }
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces(lump_t *l) {
    int i = 0, j = 0, count = 0;
    short *in = nullptr;
    msurface_t **out = nullptr;

    in = reinterpret_cast<decltype(in)>(mod_base + l->fileofs);
    if (l->filelen % sizeof(*in))
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    count = l->filelen / sizeof(*in);
    out = hunkAllocName<decltype(out)>(count * sizeof(*out), loadname);

    loadmodel->marksurfaces = out;
    loadmodel->nummarksurfaces = count;

    for (i = 0; i < count; i++) {
        j = LittleShort(in[i]);
        if (j >= loadmodel->numsurfaces)
            Sys_Error("Mod_ParseMarksurfaces: bad surface number");
        out[i] = loadmodel->surfaces + j;
    }
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges(lump_t *l) {
    int i = 0, count = 0;
    int *in = nullptr, *out = nullptr;

    in = reinterpret_cast<decltype(in)>(mod_base + l->fileofs);
    if (l->filelen % sizeof(*in))
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    count = l->filelen / sizeof(*in);
    out = hunkAllocName<decltype(out)>(count * sizeof(*out), loadname);

    loadmodel->surfedges = out;
    loadmodel->numsurfedges = count;

    std::memcpy(out, in, sizeof(int) * count);
}

/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes(lump_t *l) {
    int i = 0, j = 0;
    mplane_t *out = nullptr;
    dplane_t *in = nullptr;
    int count = 0;
    int bits = 0;

    in = reinterpret_cast<decltype(in)>(mod_base + l->fileofs);
    if (l->filelen % sizeof(*in))
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    count = l->filelen / sizeof(*in);
    out = hunkAllocName<decltype(out)>(count * 2 * sizeof(*out), loadname);

    loadmodel->planes = out;
    loadmodel->numplanes = count;

    for (i = 0; i < count; i++, in++, out++) {
        bits = 0;
        for (j = 0; j < 3; j++) {
            out->normal[j] = LittleFloat(in->normal[j]);
            if (out->normal[j] < 0)
                bits |= 1 << j;
        }

        out->dist = LittleFloat(in->dist);
        out->type = static_cast<decltype(out->type)>(LittleLong(in->type));
        out->signbits = bits;
    }
}

/*
=================
RadiusFromBounds
=================
*/
inline auto RadiusFromBounds(vec3 mins, vec3 maxs) -> float {
    vec3 corner;

    for (int i = 0; i < 3; i++) {
        corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
    }

    return glm::length(corner);
}

/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel(model_t *mod, void *buffer) {
    int i = 0, j = 0;
    dheader_t *header = nullptr;
    dmodel_t *bm = nullptr;

    loadmodel->type = mod_brush;

    header = (dheader_t *) buffer;

    i = static_cast<decltype(i)>(LittleLong(header->version));
    if (i != BSPVERSION)
        Sys_Error("Mod_LoadBrushModel: %s has wrong version number (%i should be %i)", mod->name, i, BSPVERSION);

// swap all the lumps
    mod_base = (byte *) header;

    for (i = 0; i < sizeof(dheader_t) / 4; i++)
        ((int *) header)[i] = LittleLong(((int *) header)[i]);

// load into heap

    Mod_LoadVertexes(&header->lumps[LUMP_VERTEXES]);
    Mod_LoadEdges(&header->lumps[LUMP_EDGES]);
    Mod_LoadSurfedges(&header->lumps[LUMP_SURFEDGES]);
    Mod_LoadTextures(&header->lumps[LUMP_TEXTURES]);
    Mod_LoadLighting(&header->lumps[LUMP_LIGHTING]);
    Mod_LoadPlanes(&header->lumps[LUMP_PLANES]);
    Mod_LoadTexinfo(&header->lumps[LUMP_TEXINFO]);
    Mod_LoadFaces(&header->lumps[LUMP_FACES]);
    Mod_LoadMarksurfaces(&header->lumps[LUMP_MARKSURFACES]);
    Mod_LoadVisibility(&header->lumps[LUMP_VISIBILITY]);
    Mod_LoadLeafs(&header->lumps[LUMP_LEAFS]);
    Mod_LoadNodes(&header->lumps[LUMP_NODES]);
    Mod_LoadClipnodes(&header->lumps[LUMP_CLIPNODES]);
    Mod_LoadEntities(&header->lumps[LUMP_ENTITIES]);
    Mod_LoadSubmodels(&header->lumps[LUMP_MODELS]);

    Mod_MakeHull0();

    mod->numframes = 2;        // regular and alternate animation
    mod->flags = 0;

//
// set up the submodels (FIXME: this is confusing)
//
    for (i = 0; i < mod->numsubmodels; i++) {
        bm = &mod->submodels[i];

        mod->hulls[0].firstclipnode = bm->headnode[0];
        for (j = 1; j < MAX_MAP_HULLS; j++) {
            mod->hulls[j].firstclipnode = bm->headnode[j];
            mod->hulls[j].lastclipnode = mod->numclipnodes - 1;
        }

        mod->firstmodelsurface = bm->firstface;
        mod->nummodelsurfaces = bm->numfaces;

        mod->maxs = bm->maxs;
        mod->mins = bm->mins;
        mod->radius = RadiusFromBounds(mod->mins, mod->maxs);

        mod->numleafs = bm->visleafs;

        if (i < mod->numsubmodels - 1) {    // duplicate the basic information
            char name[10];

            sprintf(name, "*%i", i + 1);
            loadmodel = Mod_FindName(name);
            *loadmodel = *mod;
            strcpy(loadmodel->name, name);
            mod = loadmodel;
        }
    }
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

/*
=================
Mod_LoadAliasFrame
=================
*/
auto Mod_LoadAliasFrame(void *pin, int *pframeindex, int numv,
                        trivertx_t *pbboxmin, trivertx_t *pbboxmax, aliashdr_t *pheader, char *name) -> void * {

    const auto *pdaliasframe = (daliasframe_t *) pin;

    strcpy(name, pdaliasframe->name);

    for (int i = 0; i < 3; i++) {
        // these are byte values, so we don't have to worry about
        // endianness
        pbboxmin->v[i] = pdaliasframe->bboxmin.v[i];
        pbboxmax->v[i] = pdaliasframe->bboxmax.v[i];
    }

    auto *pinframe = (trivertx_t *) (pdaliasframe + 1);
    auto *pframe = hunkAllocName<trivertx_t *>(numv * sizeof(trivertx_t), loadname);

    *pframeindex = (byte *) pframe - (byte *) pheader;

    for (int j = 0; j < numv; j++) {
        int k = 0;

        // these are all byte values, so no need to deal with endianness
        pframe[j].lightnormalindex = pinframe[j].lightnormalindex;

        for (k = 0; k < 3; k++) {
            pframe[j].v[k] = pinframe[j].v[k];
        }
    }

    pinframe += numv;

    return (void *) pinframe;
}


/*
=================
Mod_LoadAliasGroup
=================
*/
auto Mod_LoadAliasGroup(void *pin, int *pframeindex, int numv,
                        trivertx_t *pbboxmin, trivertx_t *pbboxmax, aliashdr_t *pheader, char *name) -> void * {
    daliasgroup_t *pingroup = nullptr;
    maliasgroup_t *paliasgroup = nullptr;
    int i = 0, numframes = 0;
    daliasinterval_t *pin_intervals = nullptr;
    float *poutintervals = nullptr;
    void *ptemp = nullptr;

    pingroup = (daliasgroup_t *) pin;

    numframes = static_cast<decltype(numframes)>(LittleLong(pingroup->numframes));

    paliasgroup = hunkAllocName<decltype(paliasgroup)>(sizeof(maliasgroup_t) +
                                                       (numframes - 1) * sizeof(paliasgroup->frames[0]), loadname);

    paliasgroup->numframes = numframes;

    for (i = 0; i < 3; i++) {
        // these are byte values, so we don't have to worry about endianness
        pbboxmin->v[i] = pingroup->bboxmin.v[i];
        pbboxmax->v[i] = pingroup->bboxmax.v[i];
    }

    *pframeindex = (byte *) paliasgroup - (byte *) pheader;

    pin_intervals = (daliasinterval_t *) (pingroup + 1);

    poutintervals = hunkAllocName<decltype(poutintervals)>(numframes * sizeof(float), loadname);

    paliasgroup->intervals = (byte *) poutintervals - (byte *) pheader;

    for (i = 0; i < numframes; i++) {
        *poutintervals = LittleFloat(pin_intervals->interval);
        if (*poutintervals <= 0.0)
            Sys_Error("Mod_LoadAliasGroup: interval<=0");

        poutintervals++;
        pin_intervals++;
    }

    ptemp = (void *) pin_intervals;

    for (i = 0; i < numframes; i++) {
        ptemp = Mod_LoadAliasFrame(ptemp,
                                   &paliasgroup->frames[i].frame,
                                   numv,
                                   &paliasgroup->frames[i].bboxmin,
                                   &paliasgroup->frames[i].bboxmax,
                                   pheader, name);
    }

    return ptemp;
}


/*
=================
Mod_LoadAliasSkin
=================
*/
auto Mod_LoadAliasSkin(void *pin, int *pskinindex, int skinsize,
                       aliashdr_t *pheader) -> void * {
    int i = 0;
    byte *pskin = nullptr, *pinskin = nullptr;
    unsigned short *pusskin = nullptr;

    pskin = hunkAllocName<decltype(pskin)>(skinsize * r_pixbytes, loadname);
    pinskin = (byte *) pin;
    *pskinindex = (byte *) pskin - (byte *) pheader;

    if (r_pixbytes == 1) {
        memcpy(pskin, pinskin, skinsize);
    } else if (r_pixbytes == 2) {
        pusskin = (unsigned short *) pskin;

        for (i = 0; i < skinsize; i++)
            pusskin[i] = d_8to16table[pinskin[i]];
    } else {
        Sys_Error("Mod_LoadAliasSkin: driver set invalid r_pixbytes: %d\n",
                  r_pixbytes);
    }

    pinskin += skinsize;

    return ((void *) pinskin);
}


/*
=================
Mod_LoadAliasSkinGroup
=================
*/
auto Mod_LoadAliasSkinGroup(void *pin, int *pskinindex, int skinsize,
                            aliashdr_t *pheader) -> void * {
    daliasskingroup_t *pinskingroup = nullptr;
    maliasskingroup_t *paliasskingroup = nullptr;
    int i = 0, numskins = 0;
    daliasskininterval_t *pinskinintervals = nullptr;
    float *poutskinintervals = nullptr;
    void *ptemp = nullptr;

    pinskingroup = (daliasskingroup_t *) pin;

    numskins = static_cast<decltype(numskins)>(LittleLong(pinskingroup->numskins));

    paliasskingroup = hunkAllocName<decltype(paliasskingroup)>(sizeof(maliasskingroup_t) +
                                                               (numskins - 1) * sizeof(paliasskingroup->skindescs[0]),
                                                               loadname);

    paliasskingroup->numskins = numskins;

    *pskinindex = (byte *) paliasskingroup - (byte *) pheader;

    pinskinintervals = (daliasskininterval_t *) (pinskingroup + 1);

    poutskinintervals = hunkAllocName<decltype(poutskinintervals)>(numskins * sizeof(float), loadname);

    paliasskingroup->intervals = (byte *) poutskinintervals - (byte *) pheader;

    for (i = 0; i < numskins; i++) {
        *poutskinintervals = LittleFloat(pinskinintervals->interval);
        if (*poutskinintervals <= 0)
            Sys_Error("Mod_LoadAliasSkinGroup: interval<=0");

        poutskinintervals++;
        pinskinintervals++;
    }

    ptemp = (void *) pinskinintervals;

    for (i = 0; i < numskins; i++) {
        ptemp = Mod_LoadAliasSkin(ptemp,
                                  &paliasskingroup->skindescs[i].skin, skinsize, pheader);
    }

    return ptemp;
}


/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel(model_t *mod, void *buffer) {
    int i = 0;
    mdl_t *pmodel = nullptr, *pinmodel = nullptr;
    stvert_t *pstverts = nullptr, *pinstverts = nullptr;
    aliashdr_t *pheader = nullptr;
    mtriangle_t *ptri = nullptr;
    dtriangle_t *pintriangles = nullptr;
    int version = 0, numframes = 0, numskins = 0;
    int size = 0;
    daliasframetype_t *pframetype = nullptr;
    daliasskintype_t *pskintype = nullptr;
    maliasskindesc_t *pskindesc = nullptr;
    int skinsize = 0;
    int start = 0, end = 0, total = 0;

    start = Hunk_LowMark();

    pinmodel = (mdl_t *) buffer;

    version = static_cast<decltype(version)>(LittleLong(pinmodel->version));
    if (version != ALIAS_VERSION)
        Sys_Error("%s has wrong version number (%i should be %i)",
                  mod->name, version, ALIAS_VERSION);

//
// allocate space for a working header, plus all the data except the frames,
// skin and group info
//
    size = sizeof(aliashdr_t) + (LittleLong(pinmodel->numframes) - 1) *
                                sizeof(pheader->frames[0]) +
           sizeof(mdl_t) +
           LittleLong(pinmodel->numverts) * sizeof(stvert_t) +
           LittleLong(pinmodel->numtris) * sizeof(mtriangle_t);

    pheader = hunkAllocName<decltype(pheader)>(size, loadname);
    pmodel = (mdl_t *) ((byte *) &pheader[1] +
                        (LittleLong(pinmodel->numframes) - 1) *
                        sizeof(pheader->frames[0]));

//	mod->cache.data = pheader;
    mod->flags = static_cast<decltype(mod->flags)>(LittleLong(pinmodel->flags));

//
// endian-adjust and copy the data, starting with the alias model header
//
    pmodel->boundingradius = LittleFloat(pinmodel->boundingradius);
    pmodel->numskins = static_cast<decltype(pmodel->numskins)>(LittleLong(pinmodel->numskins));
    pmodel->skinwidth = static_cast<decltype(pmodel->skinwidth)>(LittleLong(pinmodel->skinwidth));
    pmodel->skinheight = static_cast<decltype(pmodel->skinheight)>(LittleLong(pinmodel->skinheight));

    if (pmodel->skinheight > MAX_LBM_HEIGHT)
        Sys_Error("model %s has a skin taller than %d", mod->name,
                  MAX_LBM_HEIGHT);

    pmodel->numverts = static_cast<decltype(pmodel->numverts)>(LittleLong(pinmodel->numverts));

    if (pmodel->numverts <= 0)
        Sys_Error("model %s has no vertices", mod->name);

    if (pmodel->numverts > MAXALIASVERTS)
        Sys_Error("model %s has too many vertices", mod->name);

    pmodel->numtris = static_cast<decltype(pmodel->numtris)>(LittleLong(pinmodel->numtris));

    if (pmodel->numtris <= 0)
        Sys_Error("model %s has no triangles", mod->name);

    pmodel->numframes = static_cast<decltype(pmodel->numframes)>(LittleLong(pinmodel->numframes));
    pmodel->size = LittleFloat(pinmodel->size) * ALIAS_BASE_SIZE_RATIO;
    mod->synctype = static_cast<decltype(mod->synctype)>(LittleLong(pinmodel->synctype));
    mod->numframes = pmodel->numframes;

    for (i = 0; i < 3; i++) {
        pmodel->scale[i] = LittleFloat(pinmodel->scale[i]);
        pmodel->scale_origin[i] = LittleFloat(pinmodel->scale_origin[i]);
        pmodel->eyeposition[i] = LittleFloat(pinmodel->eyeposition[i]);
    }

    numskins = pmodel->numskins;
    numframes = pmodel->numframes;

    if (pmodel->skinwidth & 0x03)
        Sys_Error("Mod_LoadAliasModel: skinwidth not multiple of 4");

    pheader->model = (byte *) pmodel - (byte *) pheader;

//
// load the skins
//
    skinsize = pmodel->skinheight * pmodel->skinwidth;

    if (numskins < 1)
        Sys_Error("Mod_LoadAliasModel: Invalid # of skins: %d\n", numskins);

    pskintype = (daliasskintype_t *) &pinmodel[1];

    pskindesc = hunkAllocName<decltype(pskindesc)>(numskins * sizeof(maliasskindesc_t),
                                                   loadname);

    pheader->skindesc = (byte *) pskindesc - (byte *) pheader;

    for (i = 0; i < numskins; i++) {
        auto skintype = static_cast<aliasskintype_t>(LittleLong(pskintype->type));
        pskindesc[i].type = skintype;

        if (skintype == ALIAS_SKIN_SINGLE) {
            pskintype = (daliasskintype_t *)
                    Mod_LoadAliasSkin(pskintype + 1,
                                      &pskindesc[i].skin,
                                      skinsize, pheader);
        } else {
            pskintype = (daliasskintype_t *)
                    Mod_LoadAliasSkinGroup(pskintype + 1,
                                           &pskindesc[i].skin,
                                           skinsize, pheader);
        }
    }

//
// set base s and t vertices
//
    pstverts = (stvert_t *) &pmodel[1];
    pinstverts = (stvert_t *) pskintype;

    pheader->stverts = (byte *) pstverts - (byte *) pheader;

    for (i = 0; i < pmodel->numverts; i++) {
        pstverts[i].onseam = static_cast<decltype(pstverts[i].onseam)>(LittleLong(pinstverts[i].onseam));
        // put s and t in 16.16 format
        pstverts[i].s = static_cast<decltype(pstverts[i].s)>(LittleLong(pinstverts[i].s) << 16);
        pstverts[i].t = static_cast<decltype(pstverts[i].t)>(LittleLong(pinstverts[i].t) << 16);
    }

//
// set up the triangles
//
    ptri = (mtriangle_t *) &pstverts[pmodel->numverts];
    pintriangles = (dtriangle_t *) &pinstverts[pmodel->numverts];

    pheader->triangles = (byte *) ptri - (byte *) pheader;

    for (i = 0; i < pmodel->numtris; i++) {
        int j = 0;

        ptri[i].facesfront = static_cast<decltype(ptri[i].facesfront)>(LittleLong(pintriangles[i].facesfront));

        for (j = 0; j < 3; j++) {
            ptri[i].vertindex[j] =
                    LittleLong(pintriangles[i].vertindex[j]);
        }
    }

//
// load the frames
//
    if (numframes < 1)
        Sys_Error("Mod_LoadAliasModel: Invalid # of frames: %d\n", numframes);

    pframetype = (daliasframetype_t *) &pintriangles[pmodel->numtris];
    // auto *test = (maliasframedesc_t*) malloc(numframes * 2 * sizeof(maliasframedesc_t));
    // memcpy(test, pheader->frames, numframes);
    // memcpy(test + numframes, pheader->frames, numframes);
    // const auto *dest = static_cast<void*>(pheader->frames);
    // dest = static_cast<void*>(test);
    // const auto test = pheader->frames[numframes];
    for (i = 0; i < numframes; i++) {
        auto frametype = static_cast<aliasframetype_t>(LittleLong(pframetype->type));
        pheader->frames[i].type = frametype;

        if (frametype == ALIAS_SINGLE) {
            pframetype = (daliasframetype_t *)
                    Mod_LoadAliasFrame(pframetype + 1,
                                       &pheader->frames[i].frame,
                                       pmodel->numverts,
                                       &pheader->frames[i].bboxmin,
                                       &pheader->frames[i].bboxmax,
                                       pheader, pheader->frames[i].name);
        } else {
            pframetype = (daliasframetype_t *)
                    Mod_LoadAliasGroup(pframetype + 1,
                                       &pheader->frames[i].frame,
                                       pmodel->numverts,
                                       &pheader->frames[i].bboxmin,
                                       &pheader->frames[i].bboxmax,
                                       pheader, pheader->frames[i].name);
        }
    }

    mod->type = mod_alias;

// FIXME: do this right
    mod->mins[0] = mod->mins[1] = mod->mins[2] = -16;
    mod->maxs[0] = mod->maxs[1] = mod->maxs[2] = 16;

//
// move the complete, relocatable alias model to the cache
//	
    end = Hunk_LowMark();
    total = end - start;

    cacheAlloc<void *>(&mod->cache, total, loadname);
    if (!mod->cache.data)
        return;
    memcpy(mod->cache.data, pheader, total);

    Hunk_FreeToLowMark(start);
}

//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/
auto Mod_LoadSpriteFrame(void *pin, mspriteframe_t **ppframe) -> void * {
    dspriteframe_t *pinframe = nullptr;
    mspriteframe_t *pspriteframe = nullptr;
    int i = 0, width = 0, height = 0, size = 0, origin[2];
    unsigned short *ppixout = nullptr;
    byte *ppixin = nullptr;

    pinframe = (dspriteframe_t *) pin;

    width = static_cast<decltype(width)>(LittleLong(pinframe->width));
    height = static_cast<decltype(height)>(LittleLong(pinframe->height));
    size = width * height;

    pspriteframe = hunkAllocName<decltype(pspriteframe)>(sizeof(mspriteframe_t) + size * r_pixbytes,
                                                         loadname);

    Q_memset(pspriteframe, 0, sizeof(mspriteframe_t) + size);
    *ppframe = pspriteframe;

    pspriteframe->width = width;
    pspriteframe->height = height;
    origin[0] = LittleLong(pinframe->origin[0]);
    origin[1] = LittleLong(pinframe->origin[1]);

    pspriteframe->up = origin[1];
    pspriteframe->down = origin[1] - height;
    pspriteframe->left = origin[0];
    pspriteframe->right = width + origin[0];

    if (r_pixbytes == 1) {
        memcpy(&pspriteframe->pixels[0], (byte *) (pinframe + 1), size);
    } else if (r_pixbytes == 2) {
        ppixin = (byte *) (pinframe + 1);
        ppixout = (unsigned short *) &pspriteframe->pixels[0];

        for (i = 0; i < size; i++)
            ppixout[i] = d_8to16table[ppixin[i]];
    } else {
        Sys_Error("Mod_LoadSpriteFrame: driver set invalid r_pixbytes: %d\n",
                  r_pixbytes);
    }

    return (void *) ((byte *) pinframe + sizeof(dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
auto Mod_LoadSpriteGroup(void *pin, mspriteframe_t **ppframe) -> void * {
    dspritegroup_t *pingroup = nullptr;
    mspritegroup_t *pspritegroup = nullptr;
    int i = 0, numframes = 0;
    dspriteinterval_t *pin_intervals = nullptr;
    float *poutintervals = nullptr;
    void *ptemp = nullptr;

    pingroup = (dspritegroup_t *) pin;

    numframes = static_cast<decltype(numframes)>(LittleLong(pingroup->numframes));

    pspritegroup = hunkAllocName<decltype(pspritegroup)>(sizeof(mspritegroup_t) +
                                                         (numframes - 1) * sizeof(pspritegroup->frames[0]), loadname);

    pspritegroup->numframes = numframes;

    *ppframe = (mspriteframe_t *) pspritegroup;

    pin_intervals = (dspriteinterval_t *) (pingroup + 1);

    poutintervals = hunkAllocName<decltype(poutintervals)>(numframes * sizeof(float), loadname);

    pspritegroup->intervals = poutintervals;

    for (i = 0; i < numframes; i++) {
        *poutintervals = LittleFloat(pin_intervals->interval);
        if (*poutintervals <= 0.0)
            Sys_Error("Mod_LoadSpriteGroup: interval<=0");

        poutintervals++;
        pin_intervals++;
    }

    ptemp = (void *) pin_intervals;

    for (i = 0; i < numframes; i++) {
        ptemp = Mod_LoadSpriteFrame(ptemp, &pspritegroup->frames[i]);
    }

    return ptemp;
}


/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel(model_t *mod, void *buffer) {
    int i = 0;
    int version = 0;
    dsprite_t *pin = nullptr;
    msprite_t *psprite = nullptr;
    int numframes = 0;
    dspriteframetype_t *pframetype = nullptr;

    pin = (dsprite_t *) buffer;

    version = static_cast<decltype(version)>(LittleLong(pin->version));
    if (version != SPRITE_VERSION)
        Sys_Error("%s has wrong version number "
                  "(%i should be %i)", mod->name, version, SPRITE_VERSION);

    numframes = static_cast<decltype(numframes)>(LittleLong(pin->numframes));
    long size = sizeof(msprite_t) + (numframes - 1) * sizeof(psprite->frames);

    psprite = hunkAllocName<decltype(psprite)>(size, loadname);

    mod->cache.data = psprite;

    psprite->type = static_cast<decltype(psprite->type)>(LittleLong(pin->type));
    psprite->maxwidth = static_cast<decltype(psprite->maxwidth)>(LittleLong(pin->width));
    psprite->maxheight = static_cast<decltype(psprite->maxheight)>(LittleLong(pin->height));
    psprite->beamlength = LittleFloat(pin->beamlength);
    mod->synctype = static_cast<decltype(mod->synctype)>(LittleLong(pin->synctype));
    psprite->numframes = numframes;

    mod->mins[0] = mod->mins[1] = -psprite->maxwidth / 2;
    mod->maxs[0] = mod->maxs[1] = psprite->maxwidth / 2;
    mod->mins[2] = -psprite->maxheight / 2;
    mod->maxs[2] = psprite->maxheight / 2;

//
// load the frames
//
    if (numframes < 1)
        Sys_Error("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);

    mod->numframes = numframes;
    mod->flags = 0;

    pframetype = (dspriteframetype_t *) (pin + 1);

    for (i = 0; i < numframes; i++) {
        spriteframetype_t frametype{};

        frametype = static_cast<decltype(frametype)>(LittleLong(pframetype->type));
        psprite->frames[i].type = frametype;

        if (frametype == SPR_SINGLE) {
            pframetype = (dspriteframetype_t *)
                    Mod_LoadSpriteFrame(pframetype + 1,
                                        &psprite->frames[i].frameptr);
        } else {
            pframetype = (dspriteframetype_t *)
                    Mod_LoadSpriteGroup(pframetype + 1,
                                        &psprite->frames[i].frameptr);
        }
    }

    mod->type = mod_sprite;
}

//=============================================================================

/*
================
Mod_Print
================
*/
void Mod_Print() {
    int i = 0;
    model_t *mod = nullptr;

    Con_Printf("Cached models:\n");
    for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++) {
        Con_Printf("%8p : %s", mod->cache.data, mod->name);
        if (mod->needload & NL_UNREFERENCED)
            Con_Printf(" (!R)");
        if (mod->needload & NL_NEEDS_LOADED)
            Con_Printf(" (!P)");
        Con_Printf("\n");
    }
}


