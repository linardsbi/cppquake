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
// d_edge.c

#include <cmath>
#include "quakedef.hpp"
#include "d_local.hpp"

static int miplevel;

float scale_for_mip;
int screenwidth;
int ubasestep, errorterm, erroradjustup, erroradjustdown;
int vstartscan;

// FIXME: should go away
extern void R_RotateBmodel();

extern void R_TransformFrustum();

vec3 transformed_modelorg;

/*
==============
D_DrawPoly

==============
*/
void D_DrawPoly() {
// this driver takes spans, not polygons
}


/*
=============
D_MipLevelForScale
=============
*/
auto D_MipLevelForScale(float scale) -> int {
    int lmiplevel = 0;

    if (scale >= d_scalemip[0])
        lmiplevel = 0;
    else if (scale >= d_scalemip[1])
        lmiplevel = 1;
    else if (scale >= d_scalemip[2])
        lmiplevel = 2;
    else
        lmiplevel = 3;

    if (lmiplevel < d_minmip)
        lmiplevel = d_minmip;

    return lmiplevel;
}


/*
==============
D_DrawSolidSurface
==============
*/

// FIXME: clean this up

void D_DrawSolidSurface(surf_t *surf, int color) {
    espan_t *span = nullptr;
    byte *pdest = nullptr;
    int u = 0, u2 = 0, pix = 0;

    pix = (color << 24) | (color << 16) | (color << 8) | color;
    for (span = surf->spans; span; span = span->pnext) {
        pdest = (byte *) d_viewbuffer + screenwidth * span->v;
        u = span->u;
        u2 = span->u + span->count - 1;
        ((byte *) pdest)[u] = pix;

        if (u2 - u < 8) {
            for (u++; u <= u2; u++)
                ((byte *) pdest)[u] = pix;
        } else {
            for (u++; u & 3; u++)
                ((byte *) pdest)[u] = pix;

            u2 -= 4;
            for (; u <= u2; u += 4)
                *(int *) ((byte *) pdest + u) = pix;
            u2 += 4;
            for (; u <= u2; u++)
                ((byte *) pdest)[u] = pix;
        }
    }
}


/*
==============
D_CalcGradients
==============
*/
void D_CalcGradients(msurface_t *pface) {
    mplane_t *pplane = nullptr;
    float mipscale = NAN;
    vec3 p_temp1;
    vec3 p_saxis, p_taxis;
    float t = NAN;

    pplane = pface->plane;

    mipscale = 1.0 / (float) (1 << miplevel);

    TransformVector(pface->texinfo->vecs[0], p_saxis);
    TransformVector(pface->texinfo->vecs[1], p_taxis);

    t = xscaleinv * mipscale;
    d_sdivzstepu = p_saxis[0] * t;
    d_tdivzstepu = p_taxis[0] * t;

    t = yscaleinv * mipscale;
    d_sdivzstepv = -p_saxis[1] * t;
    d_tdivzstepv = -p_taxis[1] * t;

    d_sdivzorigin = p_saxis[2] * mipscale - xcenter * d_sdivzstepu -
                    ycenter * d_sdivzstepv;
    d_tdivzorigin = p_taxis[2] * mipscale - xcenter * d_tdivzstepu -
                    ycenter * d_tdivzstepv;

    p_temp1 = transformed_modelorg * mipscale;

    t = 0x10000 * mipscale;
    sadjust = ((fixed16_t) (glm::dot (p_temp1, p_saxis) * 0x10000 + 0.5)) -
              ((pface->texturemins[0] << 16) >> miplevel)
              + pface->texinfo->vecs[0][3] * t;
    tadjust = ((fixed16_t) (glm::dot (p_temp1, p_taxis) * 0x10000 + 0.5)) -
              ((pface->texturemins[1] << 16) >> miplevel)
              + pface->texinfo->vecs[1][3] * t;

//
// -1 (-epsilon) so we never wander off the edge of the texture
//
    bbextents = ((pface->extents[0] << 16) >> miplevel) - 1;
    bbextentt = ((pface->extents[1] << 16) >> miplevel) - 1;
}


/*
==============
D_DrawSurfaces
==============
*/
void D_DrawSurfaces() {
    surf_t *s = nullptr;
    msurface_t *pface = nullptr;
    surfcache_t *pcurrentcache = nullptr;
    vec3 world_transformed_modelorg;
    vec3 local_modelorg;

    currententity = &cl_entities[0];
    TransformVector(modelorg, transformed_modelorg);
    world_transformed_modelorg = transformed_modelorg;

// TODO: could preset a lot of this at mode set time
    if (r_drawflat.value) {
        for (s = &surfaces[1]; s < surface_p; s++) {
            if (!s->spans)
                continue;

            d_zistepu = s->d_zistepu;
            d_zistepv = s->d_zistepv;
            d_ziorigin = s->d_ziorigin;

            D_DrawSolidSurface(s, *static_cast<int *>(s->data) & 0xFF);
            D_DrawZSpans(s->spans);
        }
    } else {
        for (s = &surfaces[1]; s < surface_p; s++) {
            if (!s->spans)
                continue;

            r_drawnpolycount++;

            d_zistepu = s->d_zistepu;
            d_zistepv = s->d_zistepv;
            d_ziorigin = s->d_ziorigin;

            if (s->flags & SURF_DRAWSKY) {
                if (!r_skymade) {
                    R_MakeSky();
                }

                D_DrawSkyScans8(s->spans);
                D_DrawZSpans(s->spans);
            } else if (s->flags & SURF_DRAWBACKGROUND) {
                // set up a gradient for the background surface that places it
                // effectively at infinity distance from the viewpoint
                d_zistepu = 0;
                d_zistepv = 0;
                d_ziorigin = -0.9f;

                D_DrawSolidSurface(s, (int) r_clearcolor.value & 0xFF);
                D_DrawZSpans(s->spans);
            } else if (s->flags & SURF_DRAWTURB) {
                pface = static_cast<msurface_t *>(s->data);
                miplevel = 0;
                cacheblock = (pixel_t *)
                        ((byte *) pface->texinfo->texture +
                         pface->texinfo->texture->offsets[0]);
                cachewidth = 64;

                if (s->insubmodel) {
                    // FIXME: we don't want to do all this for every polygon!
                    // TODO: store once at start of frame
                    currententity = s->entity;    //FIXME: make this passed in to
                    // R_RotateBmodel ()
                    local_modelorg = r_origin - currententity->origin;
                    TransformVector(local_modelorg, transformed_modelorg);

                    R_RotateBmodel();    // FIXME: don't mess with the frustum,
                    // make entity passed in
                }

                D_CalcGradients(pface);
                Turbulent8(s->spans);
                D_DrawZSpans(s->spans);

                if (s->insubmodel) {
                    //
                    // restore the old drawing state
                    // FIXME: we don't want to do this every time!
                    // TODO: speed up
                    //
                    currententity = &cl_entities[0];
                    transformed_modelorg = world_transformed_modelorg;
                    vpn = base_vpn;
                    vup = base_vup;
                    vright = base_vright;
                    modelorg = base_modelorg;
                    R_TransformFrustum();
                }
            } else {
                if (s->insubmodel) {
                    // FIXME: we don't want to do all this for every polygon!
                    // TODO: store once at start of frame
                    currententity = s->entity;    //FIXME: make this passed in to
                    // R_RotateBmodel ()
                    local_modelorg = r_origin - currententity->origin;
                    TransformVector(local_modelorg, transformed_modelorg);

                    R_RotateBmodel();    // FIXME: don't mess with the frustum,
                    // make entity passed in
                }

                pface = static_cast<msurface_t *>(s->data);
                miplevel = D_MipLevelForScale(s->nearzi * scale_for_mip
                                              * pface->texinfo->mipadjust);

                // FIXME: make this passed in to D_CacheSurface
                pcurrentcache = D_CacheSurface(pface, miplevel);

                cacheblock = (pixel_t *) pcurrentcache->data;
                cachewidth = static_cast<int>(pcurrentcache->width);

                D_CalcGradients(pface);

                (*d_drawspans)(s->spans);

                D_DrawZSpans(s->spans);

                if (s->insubmodel) {
                    //
                    // restore the old drawing state
                    // FIXME: we don't want to do this every time!
                    // TODO: speed up
                    //
                    currententity = &cl_entities[0];
                    transformed_modelorg = world_transformed_modelorg;
                    vpn = base_vpn;
                    vup = base_vup;
                    vright = base_vright;
                    modelorg = base_modelorg;
                    R_TransformFrustum();
                }
            }
        }
    }
}

