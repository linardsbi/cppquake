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
// r_bsp.c

#include <cmath>
#include "quakedef.hpp"
#include "r_local.hpp"

//
// current entity info
//
qboolean insubmodel;
entity_t *currententity;
vec3 modelorg, base_modelorg;
// modelorg is the viewpoint reletive to
// the currently rendering entity
vec3 r_entorigin;    // the currently rendering entity in world
// coordinates

vec3 entity_rotation[3];

vec3 r_worldmodelorg;

int r_currentbkey;

using solidstate_t = enum {
    touchessolid, drawnode, nodrawnode
};

#define MAX_BMODEL_VERTS    500            // 6K
#define MAX_BMODEL_EDGES    1000        // 12K

static mvertex_t *pbverts;
static bedge_t *pbedges;
static int numbverts, numbedges;

static mvertex_t *pfrontenter, *pfrontexit;

static qboolean makeclippededge;


//===========================================================================

/*
================
R_EntityRotate
================
*/
void R_EntityRotate(vec3 &vec) {
    vec3 tvec;

    tvec = vec;
    vec[0] = glm::dot (entity_rotation[0], tvec);
    vec[1] = glm::dot (entity_rotation[1], tvec);
    vec[2] = glm::dot (entity_rotation[2], tvec);
}


/*
================
R_RotateBmodel
================
*/
void R_RotateBmodel() {
// TODO: should use a look-up table
// TODO: should really be stored with the entity instead of being reconstructed
// TODO: could cache lazily, stored in the entity
// TODO: share work with R_SetUpAliasTransform

// yaw
    auto angle = currententity->angles[YAW];
    angle = angle * M_PI * 2 / 360;
    auto s = sin(angle);
    auto c = cos(angle);

    vec3 temp[3];
    vec3 temp1[3];
    vec3 temp2[3];
    vec3 temp3[3];
    temp1[0][0] = c;
    temp1[0][1] = s;
    temp1[0][2] = 0;
    temp1[1][0] = -s;
    temp1[1][1] = c;
    temp1[1][2] = 0;
    temp1[2][0] = 0;
    temp1[2][1] = 0;
    temp1[2][2] = 1;

// pitch
    angle = currententity->angles[PITCH];
    angle = angle * M_PI * 2 / 360;
    s = sin(angle);
    c = cos(angle);

    temp2[0][0] = c;
    temp2[0][1] = 0;
    temp2[0][2] = -s;
    temp2[1][0] = 0;
    temp2[1][1] = 1;
    temp2[1][2] = 0;
    temp2[2][0] = s;
    temp2[2][1] = 0;
    temp2[2][2] = c;

    R_ConcatRotations(temp2, temp1, temp3);

// roll
    angle = currententity->angles[ROLL];
    angle = angle * M_PI * 2 / 360;
    s = sin(angle);
    c = cos(angle);

    temp1[0][0] = 1;
    temp1[0][1] = 0;
    temp1[0][2] = 0;
    temp1[1][0] = 0;
    temp1[1][1] = c;
    temp1[1][2] = s;
    temp1[2][0] = 0;
    temp1[2][1] = -s;
    temp1[2][2] = c;

    R_ConcatRotations(temp1, temp3, entity_rotation);

//
// rotate modelorg and the transformation matrix
//
    R_EntityRotate(modelorg);
    R_EntityRotate(vpn);
    R_EntityRotate(vright);
    R_EntityRotate(vup);

    R_TransformFrustum();
}


/*
================
R_RecursiveClipBPoly
================
*/
void R_RecursiveClipBPoly(bedge_t *pedges, mnode_t *pnode, msurface_t *psurf) {
    bedge_t *psideedges[2], *pnextedge = nullptr, *ptedge = nullptr;
    int i = 0, side = 0, lastside = 0;
    float dist = NAN, frac = NAN, lastdist = NAN;
    mplane_t *splitplane = nullptr, tplane;
    mvertex_t *pvert = nullptr, *plastvert = nullptr, *ptvert = nullptr;
    mnode_t *pn = nullptr;

    psideedges[0] = psideedges[1] = nullptr;

    makeclippededge = false;

// transform the BSP plane into model space
// FIXME: cache these?
    splitplane = pnode->plane;
    tplane.dist = splitplane->dist -
                  glm::dot(r_entorigin, splitplane->normal);
    tplane.normal[0] = glm::dot (entity_rotation[0], splitplane->normal);
    tplane.normal[1] = glm::dot (entity_rotation[1], splitplane->normal);
    tplane.normal[2] = glm::dot (entity_rotation[2], splitplane->normal);

// clip edges to BSP plane
    for (; pedges; pedges = pnextedge) {
        pnextedge = pedges->pnext;

        // set the status for the last point as the previous point
        // FIXME: cache this stuff somehow?
        plastvert = pedges->v[0];
        lastdist = glm::dot (plastvert->position, tplane.normal) -
                   tplane.dist;

        if (lastdist > 0)
            lastside = 0;
        else
            lastside = 1;

        pvert = pedges->v[1];

        dist = glm::dot (pvert->position, tplane.normal) - tplane.dist;

        if (dist > 0)
            side = 0;
        else
            side = 1;

        if (side != lastside) {
            // clipped
            if (numbverts >= MAX_BMODEL_VERTS)
                return;

            // generate the clipped vertex
            frac = lastdist / (lastdist - dist);
            ptvert = &pbverts[numbverts++];
            ptvert->position[0] = plastvert->position[0] +
                                  frac * (pvert->position[0] -
                                          plastvert->position[0]);
            ptvert->position[1] = plastvert->position[1] +
                                  frac * (pvert->position[1] -
                                          plastvert->position[1]);
            ptvert->position[2] = plastvert->position[2] +
                                  frac * (pvert->position[2] -
                                          plastvert->position[2]);

            // split into two edges, one on each side, and remember entering
            // and exiting points
            // FIXME: share the clip edge by having a winding direction flag?
            if (numbedges >= (MAX_BMODEL_EDGES - 1)) {
                Con_Printf("Out of edges for bmodel\n");
                return;
            }

            ptedge = &pbedges[numbedges];
            ptedge->pnext = psideedges[lastside];
            psideedges[lastside] = ptedge;
            ptedge->v[0] = plastvert;
            ptedge->v[1] = ptvert;

            ptedge = &pbedges[numbedges + 1];
            ptedge->pnext = psideedges[side];
            psideedges[side] = ptedge;
            ptedge->v[0] = ptvert;
            ptedge->v[1] = pvert;

            numbedges += 2;

            if (side == 0) {
                // entering for front, exiting for back
                pfrontenter = ptvert;
                makeclippededge = true;
            } else {
                pfrontexit = ptvert;
                makeclippededge = true;
            }
        } else {
            // add the edge to the appropriate side
            pedges->pnext = psideedges[side];
            psideedges[side] = pedges;
        }
    }

// if anything was clipped, reconstitute and add the edges along the clip
// plane to both sides (but in opposite directions)
    if (makeclippededge) {
        if (numbedges >= (MAX_BMODEL_EDGES - 2)) {
            Con_Printf("Out of edges for bmodel\n");
            return;
        }

        ptedge = &pbedges[numbedges];
        ptedge->pnext = psideedges[0];
        psideedges[0] = ptedge;
        ptedge->v[0] = pfrontexit;
        ptedge->v[1] = pfrontenter;

        ptedge = &pbedges[numbedges + 1];
        ptedge->pnext = psideedges[1];
        psideedges[1] = ptedge;
        ptedge->v[0] = pfrontenter;
        ptedge->v[1] = pfrontexit;

        numbedges += 2;
    }

// draw or recurse further
    for (i = 0; i < 2; i++) {
        if (psideedges[i]) {
            // draw if we've reached a non-solid leaf, done if all that's left is a
            // solid leaf, and continue down the tree if it's not a leaf
            pn = pnode->children[i];

            // we're done with this branch if the node or leaf isn't in the PVS
            if (pn->visframe == r_visframecount) {
                if (pn->contents < 0) {
                    if (pn->contents != CONTENTS_SOLID) {
                        r_currentbkey = ((mleaf_t *) pn)->key;
                        R_RenderBmodelFace(psideedges[i], psurf);
                    }
                } else {
                    R_RecursiveClipBPoly(psideedges[i], pnode->children[i],
                                         psurf);
                }
            }
        }
    }
}


/*
================
R_DrawSolidClippedSubmodelPolygons
================
*/
void R_DrawSolidClippedSubmodelPolygons(model_t *pmodel) {
    int i = 0, j = 0, lindex = 0;
    vec_t dot = NAN;
    msurface_t *psurf = nullptr;
    int numsurfaces = 0;
    mplane_t *pplane = nullptr;
    mvertex_t bverts[MAX_BMODEL_VERTS];
    bedge_t bedges[MAX_BMODEL_EDGES], *pbedge = nullptr;
    medge_t *pedge = nullptr, *pedges = nullptr;

// FIXME: use bounding-box-based frustum clipping info?

    psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
    numsurfaces = pmodel->nummodelsurfaces;
    pedges = pmodel->edges;

    for (i = 0; i < numsurfaces; i++, psurf++) {
        // find which side of the node we are on
        pplane = psurf->plane;

        dot = glm::dot (modelorg, pplane->normal) - pplane->dist;

        // draw the polygon
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
            (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
            // FIXME: use bounding-box-based frustum clipping info?

            // copy the edges to bedges, flipping if necessary so always
            // clockwise winding
            // FIXME: if edges and vertices get caches, these assignments must move
            // outside the loop, and overflow checking must be done here
            pbverts = bverts;
            pbedges = bedges;
            numbverts = numbedges = 0;

            if (psurf->numedges > 0) {
                pbedge = &bedges[numbedges];
                numbedges += psurf->numedges;

                for (j = 0; j < psurf->numedges; j++) {
                    lindex = pmodel->surfedges[psurf->firstedge + j];

                    if (lindex > 0) {
                        pedge = &pedges[lindex];
                        pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[0]];
                        pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[1]];
                    } else {
                        lindex = -lindex;
                        pedge = &pedges[lindex];
                        pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[1]];
                        pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[0]];
                    }

                    pbedge[j].pnext = &pbedge[j + 1];
                }

                pbedge[j - 1].pnext = nullptr;    // mark end of edges

                R_RecursiveClipBPoly(pbedge, currententity->topnode, psurf);
            } else {
                Sys_Error("no edges in bmodel");
            }
        }
    }
}


/*
================
R_DrawSubmodelPolygons
================
*/
void R_DrawSubmodelPolygons(model_t *pmodel, int clipflags) {
    int i = 0;
    vec_t dot = NAN;
    msurface_t *psurf = nullptr;
    int numsurfaces = 0;
    mplane_t *pplane = nullptr;

// FIXME: use bounding-box-based frustum clipping info?

    psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
    numsurfaces = pmodel->nummodelsurfaces;

    for (i = 0; i < numsurfaces; i++, psurf++) {
        // find which side of the node we are on
        pplane = psurf->plane;

        dot = glm::dot (modelorg, pplane->normal) - pplane->dist;

        // draw the polygon
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
            (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
            r_currentkey = ((mleaf_t *) currententity->topnode)->key;

            // FIXME: use bounding-box-based frustum clipping info?
            R_RenderFace(psurf, clipflags);
        }
    }
}


/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode(mnode_t *node, int clipflags) {
    int i = 0, c = 0, side = 0, *pindex = nullptr;
    vec3 acceptpt, rejectpt;
    mplane_t *plane = nullptr;
    msurface_t *surf = nullptr, **mark = nullptr;
    mleaf_t *pleaf = nullptr;
    double d = NAN, dot = NAN;

    if (node->contents == CONTENTS_SOLID)
        return;        // solid

    if (node->visframe != r_visframecount)
        return;

// cull the clipping planes if not trivial accept
// FIXME: the compiler is doing a lousy job of optimizing here; it could be
//  twice as fast in ASM
    if (clipflags) {
        for (i = 0; i < 4; i++) {
            if (!(clipflags & (1 << i)))
                continue;    // don't need to clip against it

            // generate accept and reject points
            // FIXME: do with fast look-ups or integer tests based on the sign bit
            // of the floating point values

            pindex = pfrustum_indexes[i];

            rejectpt[0] = (float) node->minmaxs[pindex[0]];
            rejectpt[1] = (float) node->minmaxs[pindex[1]];
            rejectpt[2] = (float) node->minmaxs[pindex[2]];

            d = glm::dot (rejectpt, view_clipplanes[i].normal);
            d -= view_clipplanes[i].dist;

            if (d <= 0)
                return;

            acceptpt[0] = (float) node->minmaxs[pindex[3 + 0]];
            acceptpt[1] = (float) node->minmaxs[pindex[3 + 1]];
            acceptpt[2] = (float) node->minmaxs[pindex[3 + 2]];

            d = glm::dot (acceptpt, view_clipplanes[i].normal);
            d -= view_clipplanes[i].dist;

            if (d >= 0)
                clipflags &= ~(1 << i);    // node is entirely on screen
        }
    }

// if a leaf node, draw stuff
    if (node->contents < 0) {
        pleaf = (mleaf_t *) node;

        mark = pleaf->firstmarksurface;
        c = pleaf->nummarksurfaces;

        if (c) {
            do {
                (*mark)->visframe = r_framecount;
                mark++;
            } while (--c);
        }

        // deal with model fragments in this leaf
        if (pleaf->efrags) {
            R_StoreEfrags(&pleaf->efrags);
        }

        pleaf->key = r_currentkey;
        r_currentkey++;        // all bmodels in a leaf share the same key
    } else {
        // node is just a decision point, so go down the apropriate sides

        // find which side of the node we are on
        plane = node->plane;

        switch (plane->type) {
            case PLANE_X:
                dot = modelorg[0] - plane->dist;
                break;
            case PLANE_Y:
                dot = modelorg[1] - plane->dist;
                break;
            case PLANE_Z:
                dot = modelorg[2] - plane->dist;
                break;
            default:
                dot = glm::dot (modelorg, plane->normal) - plane->dist;
                break;
        }

        if (dot >= 0)
            side = 0;
        else
            side = 1;

        // recurse down the children, front side first
        R_RecursiveWorldNode(node->children[side], clipflags);

        // draw stuff
        c = node->numsurfaces;

        if (c) {
            surf = cl.worldmodel->surfaces + node->firstsurface;

            if (dot < -BACKFACE_EPSILON) {
                do {
                    if ((surf->flags & SURF_PLANEBACK) &&
                        (surf->visframe == r_framecount)) {
                        if (r_drawpolys) {
                            if (r_worldpolysbacktofront) {
                                if (numbtofpolys < MAX_BTOFPOLYS) {
                                    pbtofpolys[numbtofpolys].clipflags =
                                            clipflags;
                                    pbtofpolys[numbtofpolys].psurf = surf;
                                    numbtofpolys++;
                                }
                            } else {
                                R_RenderPoly(surf, clipflags);
                            }
                        } else {
                            R_RenderFace(surf, clipflags);
                        }
                    }

                    surf++;
                } while (--c);
            } else if (dot > BACKFACE_EPSILON) {
                do {
                    if (!(surf->flags & SURF_PLANEBACK) &&
                        (surf->visframe == r_framecount)) {
                        if (r_drawpolys) {
                            if (r_worldpolysbacktofront) {
                                if (numbtofpolys < MAX_BTOFPOLYS) {
                                    pbtofpolys[numbtofpolys].clipflags =
                                            clipflags;
                                    pbtofpolys[numbtofpolys].psurf = surf;
                                    numbtofpolys++;
                                }
                            } else {
                                R_RenderPoly(surf, clipflags);
                            }
                        } else {
                            R_RenderFace(surf, clipflags);
                        }
                    }

                    surf++;
                } while (--c);
            }

            // all surfaces on the same node share the same sequence number
            r_currentkey++;
        }

        // recurse down the back side
        R_RecursiveWorldNode(node->children[!side], clipflags);
    }
}


/*
================
R_RenderWorld
================
*/
void R_RenderWorld() {
    int i = 0;
    model_t *clmodel = nullptr;
    btofpoly_t btofpolys[MAX_BTOFPOLYS];

    pbtofpolys = btofpolys;

    currententity = &cl_entities[0];
    modelorg = r_origin;
    clmodel = currententity->model;
    r_pcurrentvertbase = clmodel->vertexes;

    R_RecursiveWorldNode(clmodel->nodes, 15);

// if the driver wants the polygons back to front, play the visible ones back
// in that order
    if (r_worldpolysbacktofront) {
        for (i = numbtofpolys - 1; i >= 0; i--) {
            R_RenderPoly(btofpolys[i].psurf, btofpolys[i].clipflags);
        }
    }
}


