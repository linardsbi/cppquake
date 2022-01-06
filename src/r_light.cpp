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
// r_light.c

#include <cmath>
#include "quakedef.hpp"
#include "r_local.hpp"
#include "client.hpp"
#include "r_shared.hpp"
#include "model.hpp"
#include "d_iface.hpp"
#include "mathlib.hpp"
#include "bspfile.hpp"
#include "render.hpp"
#include "common.hpp"

int r_dlightframecount;


/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight() {
    int i = 0, j = 0, k = 0;

//
// light animations
// 'm' is normal light, 'a' is no light, 'z' is double bright
    i = (int) (cl.time * 10);
    for (j = 0; j < MAX_LIGHTSTYLES; j++) {
        if (!cl_lightstyle[j].length) {
            d_lightstylevalue[j] = 256;
            continue;
        }
        k = i % cl_lightstyle[j].length;
        k = k >= 64 ? 63 : k;

        k = cl_lightstyle[j].map[k] - 'a';
        k = k * 22;
        d_lightstylevalue[j] = k;
    }
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights(dlight_t *light, int bit, mnode_t *node) {
    mplane_t *splitplane = nullptr;
    float dist = NAN;
    msurface_t *surf = nullptr;
    int i = 0;

    if (node->contents < 0)
        return;

    splitplane = node->plane;
    dist = glm::dot (light->origin, splitplane->normal) - splitplane->dist;

    if (dist > light->radius) {
        R_MarkLights(light, bit, node->children[0]);
        return;
    }
    if (dist < -light->radius) {
        R_MarkLights(light, bit, node->children[1]);
        return;
    }

// mark the polygons
    surf = cl.worldmodel->surfaces + node->firstsurface;
    for (i = 0; i < node->numsurfaces; i++, surf++) {
        if (surf->dlightframe != r_dlightframecount) {
            surf->dlightbits = 0;
            surf->dlightframe = r_dlightframecount;
        }
        surf->dlightbits |= bit;
    }

    R_MarkLights(light, bit, node->children[0]);
    R_MarkLights(light, bit, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights() {
    int i = 0;
    dlight_t *l = nullptr;

    r_dlightframecount = r_framecount + 1;    // because the count hasn't
    //  advanced yet for this frame
    l = cl_dlights;

    for (i = 0; i < MAX_DLIGHTS; i++, l++) {
        if (l->die < cl.time || !l->radius)
            continue;
        R_MarkLights(l, 1 << i, cl.worldmodel->nodes);
    }
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

auto RecursiveLightPoint(mnode_t *node, vec3 start, vec3 end) -> int {
    if (node->contents < 0)
        return -1;        // didn't hit anything

// calculate mid point

// FIXME: optimize for axial
    const auto plane = node->plane;
    const auto front = glm::dot (start, plane->normal) - plane->dist;
    const auto back = glm::dot (end, plane->normal) - plane->dist;
    const auto side = front < 0;

    if ((back < 0) == side)
        return RecursiveLightPoint(node->children[side], start, end);


    const auto frac = front / (front - back);
    vec3 mid = start + (end - start) * frac;

// go down front side	
    auto r = RecursiveLightPoint(node->children[side], start, mid);
    if (r >= 0)
        return r;        // hit something

    if ((back < 0) == side)
        return -1;        // didn't hit anuthing

// check for impact on this node

    auto *surf = cl.worldmodel->surfaces + node->firstsurface;
    for (int i = 0; i < node->numsurfaces; i++, surf++) {
        if (surf->flags & SURF_DRAWTILED)
            continue;    // no lightmaps

        const auto tex = surf->texinfo;

        const auto s = glm::dot (mid, vec3{tex->vecs[0]}) + tex->vecs[0][3];
        const auto t = glm::dot (mid, vec3{tex->vecs[1]}) + tex->vecs[1][3];

        if (s < surf->texturemins[0] ||
            t < surf->texturemins[1])
            continue;

        short ds = s - surf->texturemins[0];
        short dt = t - surf->texturemins[1];

        if (ds > surf->extents[0] || dt > surf->extents[1])
            continue;

        if (!surf->samples)
            return 0;

        ds >>= 4;
        dt >>= 4;

        auto lightmap = surf->samples;
        r = 0;
        if (lightmap) {
            lightmap += dt * ((surf->extents[0] >> 4) + 1) + ds;

            for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255;
                 maps++) {
                const auto scale = d_lightstylevalue[surf->styles[maps]];
                r += *lightmap * scale;
                lightmap += ((surf->extents[0] >> 4) + 1) *
                            ((surf->extents[1] >> 4) + 1);
            }

            r >>= 8;
        }

        return r;
    }

// go down back side
    return RecursiveLightPoint(node->children[!side], mid, end);
}

auto R_LightPoint(vec3 p) -> int {
    vec3 end;
    int r = 0;

    if (!cl.worldmodel->lightdata)
        return 255;

    end[0] = p[0];
    end[1] = p[1];
    end[2] = p[2] - 2048;

    r = RecursiveLightPoint(cl.worldmodel->nodes, p, end);

    if (r == -1)
        r = 0;

    if (r < r_refdef.ambientlight)
        r = r_refdef.ambientlight;

    return r;
}

