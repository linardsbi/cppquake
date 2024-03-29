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
// r_alias.c: routines for setting up to draw alias models

#include <cmath>
#include "quakedef.hpp"
#include "r_local.hpp"
#include "d_local.hpp"    // FIXME: shouldn't be needed (is needed for patch
// right now, but that should move)

#define LIGHT_MIN    5        // lowest light value we'll allow, to avoid the
//  need for inner-loop light clamping

mtriangle_t *ptriangles;
affinetridesc_t r_affinetridesc;

void *acolormap;    // FIXME: should go away

trivertx_t *r_apverts;

// TODO: these probably will go away with optimized rasterization
mdl_t *pmdl;
vec3 r_plightvec;
int r_ambientlight;
float r_shadelight;
aliashdr_t *paliashdr;
finalvert_t *pfinalverts;
auxvert_t *pauxverts;
static float ziscale;
static model_t *pmodel;

static vec3 alias_forward, alias_right, alias_up;

static maliasskindesc_t *pskindesc;

int r_amodels_drawn;
int a_skinwidth;
int r_anumverts;

vec4 aliastransform[3]{};

using aedge_t = struct {
    int index0;
    int index1;
};

static constexpr aedge_t aedges[12] = {
        {0, 1},
        {1, 2},
        {2, 3},
        {3, 0},
        {4, 5},
        {5, 6},
        {6, 7},
        {7, 4},
        {0, 5},
        {1, 4},
        {2, 7},
        {3, 6}
};

#define NUMVERTEXNORMALS    162

vec3 r_avertexnormals[NUMVERTEXNORMALS] = {
#include "anorms.hpp"
};

void R_AliasTransformAndProjectFinalVerts(finalvert_t *fv,
                                          stvert_t *pstverts);

void R_AliasSetUpTransform(int trivial_accept);

void R_AliasTransformVector(vec3 in, vec3 &out);

void R_AliasTransformFinalVert(finalvert_t *fv, auxvert_t *av,
                               trivertx_t *pverts, stvert_t *pstverts);

void R_AliasProjectFinalVert(finalvert_t *fv, auxvert_t *av);


/*
================
R_AliasCheckBBox
================
*/
auto R_AliasCheckBBox() -> qboolean {
    int i = 0, flags = 0, frame = 0, numv = 0;
    aliashdr_t *pahdr = nullptr;
    float zi = NAN, frac = NAN;
    finalvert_t *pv0 = nullptr, *pv1 = nullptr, viewpts[16];
    auxvert_t *pa0 = nullptr, *pa1 = nullptr, viewaux[16];
    maliasframedesc_t *pframedesc = nullptr;
    qboolean zclipped = 0, zfullyclipped = 0;
    unsigned anyclip = 0, allclip = 0;
    int minz = 0;

// expand, rotate, and translate points into worldspace

    currententity->trivial_accept = 0;
    pmodel = currententity->model;
    pahdr = static_cast<aliashdr_t *>(Mod_Extradata(pmodel));
    pmdl = (mdl_t *) ((byte *) pahdr + pahdr->model);

    R_AliasSetUpTransform(0);

// construct the base bounding box for this frame
    frame = currententity->frame;
// TODO: don't repeat this check when drawing?
    if ((frame >= pmdl->numframes) || (frame < 0)) {
        Con_DPrintf("No such frame %d %s\n", frame,
                    pmodel->name);
        frame = 0;
    }

    pframedesc = &pahdr->frames[frame];

    std::array<vec3, 8> basepts{};
// x worldspace coordinates
    basepts[0][0] = basepts[1][0] = basepts[2][0] = basepts[3][0] =
            (float) pframedesc->bboxmin.v[0];
    basepts[4][0] = basepts[5][0] = basepts[6][0] = basepts[7][0] =
            (float) pframedesc->bboxmax.v[0];

// y worldspace coordinates
    basepts[0][1] = basepts[3][1] = basepts[5][1] = basepts[6][1] =
            (float) pframedesc->bboxmin.v[1];
    basepts[1][1] = basepts[2][1] = basepts[4][1] = basepts[7][1] =
            (float) pframedesc->bboxmax.v[1];

// z worldspace coordinates
    basepts[0][2] = basepts[1][2] = basepts[4][2] = basepts[5][2] =
            (float) pframedesc->bboxmin.v[2];
    basepts[2][2] = basepts[3][2] = basepts[6][2] = basepts[7][2] =
            (float) pframedesc->bboxmax.v[2];

    zclipped = false;
    zfullyclipped = true;

    minz = 9999;
    for (i = 0; i < 8; i++) {
        R_AliasTransformVector(basepts[i], viewaux[i].fv);

        if (viewaux[i].fv[2] < ALIAS_Z_CLIP_PLANE) {
            // we must clip points that are closer than the near clip plane
            viewpts[i].flags = ALIAS_Z_CLIP;
            zclipped = true;
        } else {
            if (viewaux[i].fv[2] < minz)
                minz = viewaux[i].fv[2];
            viewpts[i].flags = 0;
            zfullyclipped = false;
        }
    }


    if (zfullyclipped) {
        return false;    // everything was near-z-clipped
    }

    numv = 8;

    if (zclipped) {
        // organize points by edges, use edges to get new points (possible trivial
        // reject)
        for (i = 0; i < 12; i++) {
            // edge endpoints
            pv0 = &viewpts[aedges[i].index0];
            pv1 = &viewpts[aedges[i].index1];
            pa0 = &viewaux[aedges[i].index0];
            pa1 = &viewaux[aedges[i].index1];

            // if one end is clipped and the other isn't, make a new point
            if (pv0->flags ^ pv1->flags) {
                frac = (ALIAS_Z_CLIP_PLANE - pa0->fv[2]) /
                       (pa1->fv[2] - pa0->fv[2]);
                viewaux[numv].fv[0] = pa0->fv[0] +
                                      (pa1->fv[0] - pa0->fv[0]) * frac;
                viewaux[numv].fv[1] = pa0->fv[1] +
                                      (pa1->fv[1] - pa0->fv[1]) * frac;
                viewaux[numv].fv[2] = ALIAS_Z_CLIP_PLANE;
                viewpts[numv].flags = 0;
                numv++;
            }
        }
    }

// project the vertices that remain after clipping
    anyclip = 0;
    allclip = ALIAS_XY_CLIP_MASK;

    for (i = 0; i < numv; i++) {
        // we don't need to bother with vertices that were z-clipped
        if (viewpts[i].flags & ALIAS_Z_CLIP)
            continue;

        zi = 1.0 / viewaux[i].fv[2];

        // FIXME: do with chop mode in ASM, or convert to float
        const auto v0 = (viewaux[i].fv[0] * xscale * zi) + xcenter;
        const auto v1 = (viewaux[i].fv[1] * yscale * zi) + ycenter;

        flags = 0;

        if (v0 < r_refdef.fvrectx)
            flags |= ALIAS_LEFT_CLIP;
        if (v1 < r_refdef.fvrecty)
            flags |= ALIAS_TOP_CLIP;
        if (v0 > r_refdef.fvrectright)
            flags |= ALIAS_RIGHT_CLIP;
        if (v1 > r_refdef.fvrectbottom)
            flags |= ALIAS_BOTTOM_CLIP;

        anyclip |= flags;
        allclip &= flags;
    }

    if (allclip)
        return false;    // trivial reject off one side

    currententity->trivial_accept = !anyclip & !zclipped;

    if (currententity->trivial_accept) {
        if (minz > (r_aliastransition + (pmdl->size * r_resfudge))) {
            currententity->trivial_accept |= 2;
        }
    }

    return true;
}


/*
================
R_AliasTransformVector
================
*/
void R_AliasTransformVector(vec3 in, vec3 &out) {
    out[0] = glm::dot(in, vec3{aliastransform[0]}) + aliastransform[0][3];
    out[1] = glm::dot(in, vec3{aliastransform[1]}) + aliastransform[1][3];
    out[2] = glm::dot(in, vec3{aliastransform[2]}) + aliastransform[2][3];
}


/*
================
R_AliasPreparePoints

General clipped case
================
*/
void R_AliasPreparePoints() {
    int i = 0;
    stvert_t *pstverts = nullptr;
    finalvert_t *fv = nullptr;
    auxvert_t *av = nullptr;
    mtriangle_t *ptri = nullptr;
    finalvert_t *pfv[3];

    pstverts = (stvert_t *) ((byte *) paliashdr + paliashdr->stverts);
    r_anumverts = pmdl->numverts;
    fv = pfinalverts;
    av = pauxverts;

    for (i = 0; i < r_anumverts; i++, fv++, av++, r_apverts++, pstverts++) {
        R_AliasTransformFinalVert(fv, av, r_apverts, pstverts);
        if (av->fv[2] < ALIAS_Z_CLIP_PLANE)
            fv->flags |= ALIAS_Z_CLIP;
        else {
            R_AliasProjectFinalVert(fv, av);

            if (fv->v[0] < r_refdef.aliasvrect.x)
                fv->flags |= ALIAS_LEFT_CLIP;
            if (fv->v[1] < r_refdef.aliasvrect.y)
                fv->flags |= ALIAS_TOP_CLIP;
            if (fv->v[0] > r_refdef.aliasvrectright)
                fv->flags |= ALIAS_RIGHT_CLIP;
            if (fv->v[1] > r_refdef.aliasvrectbottom)
                fv->flags |= ALIAS_BOTTOM_CLIP;
        }
    }

//
// clip and draw all triangles
//
    r_affinetridesc.numtriangles = 1;

    ptri = (mtriangle_t *) ((byte *) paliashdr + paliashdr->triangles);
    for (i = 0; i < pmdl->numtris; i++, ptri++) {
        pfv[0] = &pfinalverts[ptri->vertindex[0]];
        pfv[1] = &pfinalverts[ptri->vertindex[1]];
        pfv[2] = &pfinalverts[ptri->vertindex[2]];

        if (pfv[0]->flags & pfv[1]->flags & pfv[2]->flags & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP))
            continue;        // completely clipped

        if (!((pfv[0]->flags | pfv[1]->flags | pfv[2]->flags) &
              (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP))) {    // totally unclipped
            r_affinetridesc.pfinalverts = pfinalverts;
            r_affinetridesc.ptriangles = ptri;
            D_PolysetDraw();
        } else {    // partially clipped
            R_AliasClipTriangle(ptri);
        }
    }
}


/*
================
R_AliasSetUpTransform
================
*/
void R_AliasSetUpTransform(int trivial_accept) {
    int i = 0;
    vec4 rotationmatrix[3]{};
    vec4 t2matrix[3]{};
    static vec4 tmatrix[3]{};
    vec3 angles;

// TODO: should really be stored with the entity instead of being reconstructed
// TODO: should use a look-up table
// TODO: could cache lazily, stored in the entity

    angles[ROLL] = currententity->angles[ROLL];
    angles[PITCH] = -currententity->angles[PITCH];
    angles[YAW] = currententity->angles[YAW];
    AngleVectors(angles, alias_forward, alias_right, alias_up);

    tmatrix[0][0] = pmdl->scale[0];
    tmatrix[1][1] = pmdl->scale[1];
    tmatrix[2][2] = pmdl->scale[2];

    tmatrix[0][3] = pmdl->scale_origin[0];
    tmatrix[1][3] = pmdl->scale_origin[1];
    tmatrix[2][3] = pmdl->scale_origin[2];

// TODO: can do this with simple matrix rearrangement

    for (i = 0; i < 3; i++) {
        t2matrix[i][0] = alias_forward[i];
        t2matrix[i][1] = -alias_right[i];
        t2matrix[i][2] = alias_up[i];
    }

    t2matrix[0][3] = -modelorg[0];
    t2matrix[1][3] = -modelorg[1];
    t2matrix[2][3] = -modelorg[2];

// FIXME: can do more efficiently than full concatenation
    R_ConcatTransforms(t2matrix, tmatrix, rotationmatrix);

// TODO: should be global, set when vright, etc., set
    const vec4 viewmatrix[3] = { to_vec4(vright), to_vec4(-vup), to_vec4(vpn)};

    R_ConcatTransforms(viewmatrix, rotationmatrix, aliastransform);

// do the scaling up of x and y to screen coordinates as part of the transform
// for the unclipped case (it would mess up clipping in the clipped case).
// Also scale down z, so 1/z is scaled 31 bits for free, and scale down x and y
// correspondingly so the projected x and y come out right
// FIXME: make this work for clipped case too?
    if (trivial_accept != 0) {
        aliastransform[0] *= aliasxscale * (1.0f / ((float) 0x8000 * 0x10000));
        aliastransform[1] *= aliasyscale * (1.0f / ((float) 0x8000 * 0x10000));
        aliastransform[2] *= 1.0f / ((float) 0x8000 * 0x10000);
    }
}


/*
================
R_AliasTransformFinalVert
================
*/
void R_AliasTransformFinalVert(finalvert_t *fv, auxvert_t *av,
                               trivertx_t *pverts, stvert_t *pstverts) {

  const vec3 tempvert = {pverts->v[0], pverts->v[1], pverts->v[2]};

    av->fv[0] = glm::dot(tempvert, vec3(aliastransform[0])) +
                aliastransform[0][3];
    av->fv[1] = glm::dot(tempvert, vec3{aliastransform[1]}) +
                aliastransform[1][3];
    av->fv[2] = glm::dot(tempvert, vec3{aliastransform[2]}) +
                aliastransform[2][3];

    fv->v[2] = pstverts->s;
    fv->v[3] = pstverts->t;

    fv->flags = pstverts->onseam;

// lighting
    const vec3 plightnormal = r_avertexnormals[pverts->lightnormalindex];
    const auto lightcos = glm::dot (plightnormal, r_plightvec);
    auto temp = r_ambientlight;

    if (lightcos < 0) {
        temp += (int) (r_shadelight * lightcos);

        // clamp; because we limited the minimum ambient and shading light, we
        // don't have to clamp low light, just bright
        if (temp < 0)
            temp = 0;
    }

    fv->v[4] = temp;
}


#if    !id386

/*
================
R_AliasTransformAndProjectFinalVerts
================
*/
void R_AliasTransformAndProjectFinalVerts(finalvert_t *fv, stvert_t *pstverts) {
    auto *pverts = r_apverts;
    for (int i = 0; i < r_anumverts; i++, fv++, pverts++, pstverts++) {
        const vec3 temp_pvert = { pverts->v[0], pverts->v[1], pverts->v[2] };
        // transform and project
        const auto zi = 1.F / (glm::dot(temp_pvert, vec3{ aliastransform[2] }) + aliastransform[2][3]);

        // x, y, and z are scaled down by 1/2**31 in the transform, so 1/z is
        // scaled up by 1/2**31, and the scaling cancels out for x and y in the
        // projection
        fv->v[5] = zi;

        fv->v[0] = ((glm::dot(temp_pvert, { aliastransform[0] }) + aliastransform[0][3]) * zi) + aliasxcenter;
        fv->v[1] = ((glm::dot(temp_pvert, { aliastransform[1] }) + aliastransform[1][3]) * zi) + aliasycenter;

        fv->v[2] = pstverts->s;
        fv->v[3] = pstverts->t;
        fv->flags = pstverts->onseam;

        // lighting
        const auto plightnormal = r_avertexnormals[pverts->lightnormalindex];
        const auto lightcos = glm::dot(plightnormal, r_plightvec);
        auto temp = r_ambientlight;

        if (lightcos < 0) {
            temp += (int)(r_shadelight * lightcos);

            // clamp; because we limited the minimum ambient and shading light, we
            // don't have to clamp low light, just bright
            if (temp < 0)
                temp = 0;
        }

        fv->v[4] = temp;
    }
}

#endif


/*
================
R_AliasProjectFinalVert
================
*/
void R_AliasProjectFinalVert(finalvert_t *fv, auxvert_t *av) {
    float zi = NAN;

// project points
    zi = 1.0 / av->fv[2];

    fv->v[5] = zi * ziscale;

    fv->v[0] = (av->fv[0] * aliasxscale * zi) + aliasxcenter;
    fv->v[1] = (av->fv[1] * aliasyscale * zi) + aliasycenter;
}


/*
================
R_AliasPrepareUnclippedPoints
================
*/
void R_AliasPrepareUnclippedPoints() {
    stvert_t *pstverts = nullptr;
    finalvert_t *fv = nullptr;

    pstverts = (stvert_t *) ((byte *) paliashdr + paliashdr->stverts);
    r_anumverts = pmdl->numverts;
// FIXME: just use pfinalverts directly?
    fv = pfinalverts;

    R_AliasTransformAndProjectFinalVerts(fv, pstverts);

    if (r_affinetridesc.drawtype)
        D_PolysetDrawFinalVerts(fv, r_anumverts);

    r_affinetridesc.pfinalverts = pfinalverts;
    r_affinetridesc.ptriangles = (mtriangle_t *)
            ((byte *) paliashdr + paliashdr->triangles);
    r_affinetridesc.numtriangles = pmdl->numtris;

    D_PolysetDraw();
}

/*
===============
R_AliasSetupSkin
===============
*/
void R_AliasSetupSkin() {
    int skinnum = 0;
    int i = 0, numskins = 0;
    maliasskingroup_t *paliasskingroup = nullptr;
    float *pskinintervals = nullptr, fullskininterval = NAN;
    float skintargettime = NAN, skintime = NAN;

    skinnum = currententity->skinnum;
    if ((skinnum >= pmdl->numskins) || (skinnum < 0)) {
        Con_DPrintf("R_AliasSetupSkin: no such skin # %d\n", skinnum);
        skinnum = 0;
    }

    pskindesc = ((maliasskindesc_t *)
            ((byte *) paliashdr + paliashdr->skindesc)) + skinnum;
    a_skinwidth = pmdl->skinwidth;

    if (pskindesc->type == ALIAS_SKIN_GROUP) {
        paliasskingroup = (maliasskingroup_t *) ((byte *) paliashdr +
                                                 pskindesc->skin);
        pskinintervals = (float *)
                ((byte *) paliashdr + paliasskingroup->intervals);
        numskins = paliasskingroup->numskins;
        fullskininterval = pskinintervals[numskins - 1];

        skintime = cl.time + currententity->syncbase;

        // when loading in Mod_LoadAliasSkinGroup, we guaranteed all interval
        // values are positive, so we don't have to worry about division by 0
        skintargettime = skintime -
                         ((int) (skintime / fullskininterval)) * fullskininterval;

        for (i = 0; i < (numskins - 1); i++) {
            if (pskinintervals[i] > skintargettime)
                break;
        }

        pskindesc = &paliasskingroup->skindescs[i];
    }

    r_affinetridesc.pskindesc = pskindesc;
    r_affinetridesc.pskin = (void *) ((byte *) paliashdr + pskindesc->skin);
    r_affinetridesc.skinwidth = a_skinwidth;
    r_affinetridesc.seamfixupX16 = (a_skinwidth >> 1) << 16;
    r_affinetridesc.skinheight = pmdl->skinheight;
}

/*
================
R_AliasSetupLighting
================
*/
void R_AliasSetupLighting(alight_t *plighting) {

// guarantee that no vertex will ever be lit below LIGHT_MIN, so we don't have
// to clamp off the bottom
    r_ambientlight = plighting->ambientlight;

    if (r_ambientlight < LIGHT_MIN)
        r_ambientlight = LIGHT_MIN;

    r_ambientlight = (255 - r_ambientlight) << VID_CBITS;

    if (r_ambientlight < LIGHT_MIN)
        r_ambientlight = LIGHT_MIN;

    r_shadelight = plighting->shadelight;

    if (r_shadelight < 0)
        r_shadelight = 0;

    r_shadelight *= VID_GRADES;

// rotate the lighting vector into the model's frame of reference
    r_plightvec[0] = glm::dot (*plighting->plightvec, alias_forward);
    r_plightvec[1] = -glm::dot (*plighting->plightvec, alias_right);
    r_plightvec[2] = glm::dot (*plighting->plightvec, alias_up);
}

/*
=================
R_AliasSetupFrame

set r_apverts
=================
*/
void R_AliasSetupFrame() {
    int frame = 0;
    int i = 0, numframes = 0;
    maliasgroup_t *paliasgroup = nullptr;
    float *pintervals = nullptr, fullinterval = NAN, targettime = NAN, time = NAN;

    frame = currententity->frame;
    if ((frame >= pmdl->numframes) || (frame < 0)) {
        Con_DPrintf("R_AliasSetupFrame: no such frame %d\n", frame);
        frame = 0;
    }

    if (paliashdr->frames[frame].type == ALIAS_SINGLE) {
        r_apverts = (trivertx_t *)
                ((byte *) paliashdr + paliashdr->frames[frame].frame);
        return;
    }

    paliasgroup = (maliasgroup_t *)
            ((byte *) paliashdr + paliashdr->frames[frame].frame);
    pintervals = (float *) ((byte *) paliashdr + paliasgroup->intervals);
    numframes = paliasgroup->numframes;
    fullinterval = pintervals[numframes - 1];

    time = realtime + currententity->syncbase;

//
// when loading in Mod_LoadAliasGroup, we guaranteed all interval values
// are positive, so we don't have to worry about division by 0
//
    // todo: seperate timer for current animation duration? this function seems to be called every .1 seconds 
    targettime = time - ((int) (time / fullinterval)) * fullinterval;
    float time_diff{};
    for (i = 0; i < (numframes - 1); i++) {
        if (pintervals[i] > targettime) {
            time_diff = pintervals[i] - targettime;
            break;
        }
    }

    auto *new_frame = (trivertx_t *)
            ((byte *) paliashdr + paliasgroup->frames[i].frame);

    if (i > 0) {
        const auto *old_frame = (trivertx_t *)
            ((byte *) paliashdr + paliasgroup->frames[i - 1].frame);
        const auto old_start_time = pintervals[i - 1];
        float next_start_time = pintervals[i];
        const auto part = (time_diff / (next_start_time - old_start_time));
        
        new_frame->v[0] = ((new_frame->v[0] - old_frame->v[0]) * part) + old_frame->v[0];
        new_frame->v[1] = ((new_frame->v[1] - old_frame->v[1]) * part) + old_frame->v[1];
        new_frame->v[2] = ((new_frame->v[2] - old_frame->v[2]) * part) + old_frame->v[2];
    }

    r_apverts = new_frame;
}


/*
================
R_AliasDrawModel
================
*/
void R_AliasDrawModel(alight_t *plighting) {
    finalvert_t finalverts[MAXALIASVERTS +
                           ((CACHE_SIZE - 1) / sizeof(finalvert_t)) + 1];
    auxvert_t auxverts[MAXALIASVERTS];

    r_amodels_drawn++;

// cache align
    pfinalverts = (finalvert_t *)
            (((long) &finalverts[0] + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
    pauxverts = &auxverts[0];

    paliashdr = static_cast<aliashdr_t *>(Mod_Extradata(currententity->model));
    pmdl = (mdl_t *) ((byte *) paliashdr + paliashdr->model);

    R_AliasSetupSkin();
    R_AliasSetUpTransform(currententity->trivial_accept);
    R_AliasSetupLighting(plighting);
    R_AliasSetupFrame();

    if (!currententity->colormap)
        Sys_Error("R_AliasDrawModel: !currententity->colormap");

    r_affinetridesc.drawtype = (currententity->trivial_accept == 3) &&
                               r_recursiveaffinetriangles;

    if (r_affinetridesc.drawtype) {
        D_PolysetUpdateTables();        // FIXME: precalc...
    } else {
#if    id386
        D_Aff8Patch (currententity->colormap);
#endif
    }

    acolormap = currententity->colormap;

    if (currententity != &cl.viewent)
        ziscale = (float) 0x8000 * (float) 0x10000;
    else
        ziscale = (float) 0x8000 * (float) 0x10000 * 3.0;

    if (currententity->trivial_accept)
        R_AliasPrepareUnclippedPoints();
    else
        R_AliasPreparePoints();
}

